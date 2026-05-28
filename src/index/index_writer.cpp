#include "spp/index/index_writer.h"

#include "spp/index/manifest.h"
#include "spp/index/segment_reader.h"
#include "spp/index/segment_writer.h"
#include "spp/json/json_parser.h"
#include "spp/json/json_serializer.h"
#include "spp/json/json_value.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace spp::index {

namespace {

constexpr const char* kSchemaFilenameDefault = "schema.json";

std::string MakeSegmentStem(Generation gen, std::uint32_t local_id) {
    char buf[64];
    std::snprintf(
        buf, sizeof(buf), "seg-%020llu-%010u", static_cast<unsigned long long>(gen), local_id);
    return std::string{buf};
}

Status WriteSchemaJson(store::Directory& dir, const Schema& schema, std::string_view filename) {
    const std::string blob = spp::json::SerializePretty(schema.ToJson(), 2);
    auto sink = dir.CreateFile(filename);
    if (!sink.ok())
        return sink.status();
    SPP_RETURN_IF_ERROR((*sink)->Append(blob));
    SPP_RETURN_IF_ERROR((*sink)->Close());
    return dir.Sync();
}

Expected<Schema> ReadSchemaJson(store::Directory& dir, std::string_view filename) {
    auto src = dir.ReadFile(filename);
    if (!src.ok())
        return src.status();
    auto v = spp::json::Parse(src->view());
    if (!v.ok())
        return v.status();
    return Schema::FromJson(*v);
}

// Build a minimal canonical JSON for translog from a Document.
std::string DocToJson(const Document& doc) {
    spp::json::JsonObject o;
    o[std::string{Schema::kIdField}] = doc.id;
    for (const auto& [k, v] : doc.fields)
        o[k] = v;
    return spp::json::Serialize(spp::json::JsonValue{std::move(o)});
}

// Recover a Document from translog JSON.
Expected<Document> DocFromJson(std::string_view json) {
    auto v = spp::json::Parse(json);
    if (!v.ok())
        return v.status();
    if (!v->is_object())
        return Status::Corruption("translog record: expected object");
    Document doc;
    for (const auto& [k, val] : v->as_object()) {
        if (!val.is_string()) {
            // Skip non-string values silently for v0.1 (only text fields supported).
            continue;
        }
        if (k == std::string{Schema::kIdField}) {
            doc.id = val.as_string();
        } else {
            doc.fields[k] = val.as_string();
        }
    }
    if (doc.id.empty())
        return Status::Corruption("translog record: missing _id");
    return doc;
}

}  // namespace

IndexWriter::~IndexWriter() {
    (void)Close();
}

Expected<std::unique_ptr<IndexWriter>> IndexWriter::Open(const std::filesystem::path& dir_path,
                                                         const IndexOpenOptions& opts) {
    auto dir_or = store::OpenFilesystemDirectory(dir_path);
    if (!dir_or.ok())
        return dir_or.status();

    auto w = std::unique_ptr<IndexWriter>(new IndexWriter());
    w->dir_path_ = dir_path;
    w->dir_ = std::move(*dir_or);
    SPP_RETURN_IF_ERROR(w->BootstrapInitial(opts));
    SPP_RETURN_IF_ERROR(w->ReplayTranslog());
    return w;
}

Status IndexWriter::BootstrapInitial(const IndexOpenOptions& opts) {
    const std::string schema_file =
        opts.schema_filename.empty() ? kSchemaFilenameDefault : opts.schema_filename;

    // Load or write the schema.
    if (dir_->Exists(schema_file)) {
        auto loaded = ReadSchemaJson(*dir_, schema_file);
        if (!loaded.ok())
            return loaded.status();
        schema_ = std::make_shared<Schema>(std::move(*loaded));
    } else {
        if (opts.initial_schema == nullptr) {
            return Status::FailedPrecondition(
                "index directory has no schema.json and no initial_schema was provided");
        }
        schema_ = std::make_shared<Schema>(*opts.initial_schema);
        SPP_RETURN_IF_ERROR(WriteSchemaJson(*dir_, *schema_, schema_file));
    }

    // Load manifest and open the sealed segments listed there.
    auto manifest_or = LoadManifest(*dir_);
    if (!manifest_or.ok())
        return manifest_or.status();
    const Manifest manifest = std::move(*manifest_or);

    current_gen_.store(manifest.generation, std::memory_order_release);

    sealed_segments_.clear();
    sealed_segments_.reserve(manifest.segments.size());
    for (const auto& ms : manifest.segments) {
        auto seg = SegmentReader::Open(*dir_, ms.stem);
        if (!seg.ok())
            return seg.status();
        sealed_segments_.push_back(std::shared_ptr<const SegmentReader>(std::move(*seg)));
    }

    // Open the translog (append mode).
    auto tl = Translog::Open(dir_path_);
    if (!tl.ok())
        return tl.status();
    translog_ = std::move(*tl);

    // Fresh mutable segment.
    mutable_segment_ = std::make_unique<MutableSegment>(*schema_);

    // Publish initial reader. Construction-time, no contention.
    PublishReaderLocked(std::make_shared<const IndexReader>(schema_, sealed_segments_));
    return Status::Ok();
}

Status IndexWriter::ReplayTranslog() {
    auto records_or = translog_->Replay();
    if (!records_or.ok())
        return records_or.status();
    for (const auto& rec : *records_or) {
        auto doc_or = DocFromJson(rec);
        if (!doc_or.ok()) {
            // Skip corrupt trailing records (Translog::Replay already truncates torn ones).
            continue;
        }
        SPP_ASSIGN_OR_RETURN(auto _id, mutable_segment_->AddDocument(*doc_or));
        (void)_id;
    }
    return Status::Ok();
}

Status IndexWriter::AddDocument(const Document& doc, std::string_view raw_json) {
    std::lock_guard<std::mutex> g(mu_);
    if (translog_ == nullptr)
        return Status::FailedPrecondition("writer is closed");

    const std::string serialized = raw_json.empty() ? DocToJson(doc) : std::string{raw_json};
    SPP_RETURN_IF_ERROR(translog_->Append(serialized));
    auto id = mutable_segment_->AddDocument(doc);
    if (!id.ok())
        return id.status();
    return Status::Ok();
}

Expected<std::shared_ptr<const SegmentReader>> IndexWriter::SealCurrentSegment() {
    const DocId pending = mutable_segment_->doc_count();
    if (pending == 0)
        return std::shared_ptr<const SegmentReader>{};

    const Generation new_gen = current_gen_.load(std::memory_order_acquire) + 1;
    const std::uint32_t seg_id = next_segment_id_.fetch_add(1, std::memory_order_relaxed);
    const std::string stem = MakeSegmentStem(new_gen, seg_id);

    auto info_or = SealSegment(*mutable_segment_, *dir_, stem);
    if (!info_or.ok())
        return info_or.status();

    auto seg_or = SegmentReader::Open(*dir_, stem);
    if (!seg_or.ok())
        return seg_or.status();
    return std::shared_ptr<const SegmentReader>(std::move(*seg_or));
}

Expected<Generation> IndexWriter::Refresh() {
    std::lock_guard<std::mutex> g(mu_);
    if (translog_ == nullptr)
        return Status::FailedPrecondition("writer is closed");

    auto sealed_or = SealCurrentSegment();
    if (!sealed_or.ok())
        return sealed_or.status();

    const bool produced_segment = (*sealed_or) != nullptr;
    if (produced_segment) {
        sealed_segments_.push_back(*sealed_or);
    }
    Generation new_gen = current_gen_.load(std::memory_order_acquire);
    if (produced_segment)
        new_gen += 1;

    if (produced_segment) {
        // Write the manifest.
        Manifest m;
        m.generation = new_gen;
        m.segments.reserve(sealed_segments_.size());
        for (const auto& seg : sealed_segments_) {
            ManifestSegment ms;
            ms.stem = seg->stem();
            ms.doc_count = seg->doc_count();
            m.segments.push_back(std::move(ms));
        }
        SPP_RETURN_IF_ERROR(SaveManifest(*dir_, m));
        current_gen_.store(new_gen, std::memory_order_release);

        // Allocate fresh in-memory segment + truncate translog (we've persisted everything).
        mutable_segment_ = std::make_unique<MutableSegment>(*schema_);
        SPP_RETURN_IF_ERROR(translog_->Truncate());
    }

    PublishReaderLocked(std::make_shared<const IndexReader>(schema_, sealed_segments_));
    return new_gen;
}

Status IndexWriter::Close() {
    // Close does NOT auto-seal. Any docs added since the last Refresh remain in the
    // translog and will be replayed on next Open. Callers that want a clean shutdown
    // should call Refresh() first.
    std::lock_guard<std::mutex> g(mu_);
    if (translog_ == nullptr)
        return Status::Ok();
    (void)translog_->Close();
    translog_.reset();
    return Status::Ok();
}

void IndexWriter::PublishReaderLocked(std::shared_ptr<const IndexReader> r) {
    // Caller already holds mu_.
    published_reader_ = std::move(r);
}

std::shared_ptr<const IndexReader> IndexWriter::CurrentReader() const {
    std::lock_guard<std::mutex> g(mu_);
    return published_reader_;
}

}  // namespace spp::index
