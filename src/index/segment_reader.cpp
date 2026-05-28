#include "spp/index/segment_reader.h"

#include "spp/index/segment_info.h"
#include "spp/store/file_io.h"
#include "spp/store/varbyte.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace spp::index {

namespace {

Expected<std::uint32_t> ReadU32LE(const char*& p, const char* end) {
    if (end - p < 4)
        return Status::Corruption("u32 truncated");
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        v |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[i])) << (8 * i);
    }
    p += 4;
    return v;
}
Expected<std::uint64_t> ReadU64LE(const char*& p, const char* end) {
    if (end - p < 8)
        return Status::Corruption("u64 truncated");
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(p[i])) << (8 * i);
    }
    p += 8;
    return v;
}
Expected<std::string_view> ReadStrView(const char*& p, const char* end) {
    auto len = store::DecodeVarUint32(p, end);
    if (!len.ok())
        return len.status();
    if (static_cast<std::uint64_t>(end - p) < *len) {
        return Status::Corruption("string truncated");
    }
    std::string_view out{p, *len};
    p += *len;
    return out;
}

Expected<float> ReadF32LE(const char*& p, const char* end) {
    auto bits = ReadU32LE(p, end);
    if (!bits.ok())
        return bits.status();
    float f;
    std::memcpy(&f, &*bits, sizeof(f));
    return f;
}

}  // namespace

Expected<std::unique_ptr<SegmentReader>> SegmentReader::Open(store::Directory& dir,
                                                             std::string stem) {
    auto si_src = dir.ReadFile(stem + kSegInfoExt);
    if (!si_src.ok())
        return si_src.status();
    auto tim_src = dir.ReadFile(stem + kSegTermsExt);
    if (!tim_src.ok())
        return tim_src.status();
    auto doc_src = dir.ReadFile(stem + kSegPostingsExt);
    if (!doc_src.ok())
        return doc_src.status();
    auto fdt_src = dir.ReadFile(stem + kSegStoredExt);
    if (!fdt_src.ok())
        return fdt_src.status();
    auto fdx_src = dir.ReadFile(stem + kSegStoredIdxExt);
    if (!fdx_src.ok())
        return fdx_src.status();

    auto reader = std::unique_ptr<SegmentReader>(new SegmentReader());
    reader->info_.stem = std::move(stem);

    // Parse .si
    {
        const std::string_view sv = si_src->view();
        const char* p = sv.data();
        const char* end = p + sv.size();
        SPP_ASSIGN_OR_RETURN(auto magic, ReadU32LE(p, end));
        if (magic != kSegmentFormatMagic)
            return Status::Corruption(".si bad magic");
        SPP_ASSIGN_OR_RETURN(auto version, ReadU32LE(p, end));
        reader->info_.format_version = version;
        if (version != kSegmentFormatVersion) {
            return Status::Corruption(".si unsupported format version");
        }
        SPP_ASSIGN_OR_RETURN(auto doc_count, ReadU64LE(p, end));
        reader->info_.doc_count = static_cast<DocId>(doc_count);
        SPP_ASSIGN_OR_RETURN(auto fp_view, ReadStrView(p, end));
        reader->info_.analyzer_fingerprint = std::string{fp_view};
        SPP_ASSIGN_OR_RETURN(auto field_count, store::DecodeVarUint32(p, end));
        reader->fields_.reserve(field_count);
        for (std::uint32_t i = 0; i < field_count; ++i) {
            FieldStats fs;
            SPP_ASSIGN_OR_RETURN(auto name_view, ReadStrView(p, end));
            fs.name = std::string{name_view};
            SPP_ASSIGN_OR_RETURN(auto dc, store::DecodeVarUint32(p, end));
            fs.doc_count = dc;
            SPP_ASSIGN_OR_RETURN(auto sfl, store::DecodeVarUint64(p, end));
            fs.sum_field_length = sfl;
            SPP_ASSIGN_OR_RETURN(auto tc, store::DecodeVarUint64(p, end));
            fs.term_count = tc;
            SPP_ASSIGN_OR_RETURN(auto to, store::DecodeVarUint64(p, end));
            fs.tim_offset = to;
            SPP_ASSIGN_OR_RETURN(auto ts, store::DecodeVarUint64(p, end));
            fs.tim_size = ts;
            SPP_ASSIGN_OR_RETURN(auto pdo, store::DecodeVarUint64(p, end));
            fs.doc_offset = pdo;
            SPP_ASSIGN_OR_RETURN(auto pds, store::DecodeVarUint64(p, end));
            fs.doc_size = pds;
            // v0.2 trailing block: boost, position_decay, has_positions, has_token_weights.
            SPP_ASSIGN_OR_RETURN(auto boost, ReadF32LE(p, end));
            fs.boost = boost;
            SPP_ASSIGN_OR_RETURN(auto pdec, ReadF32LE(p, end));
            fs.position_decay = pdec;
            if (end - p < 2)
                return Status::Corruption(".si truncated at field flags");
            fs.has_positions = (p[0] != 0);
            fs.has_token_weights = (p[1] != 0);
            p += 2;
            FieldRead fr;
            fr.stats = std::move(fs);
            reader->fields_.push_back(std::move(fr));
        }
    }

    // Copy doc/fdt buffers (v0.1 keeps them in heap memory).
    reader->doc_buf_.assign(doc_src->view().begin(), doc_src->view().end());
    reader->fdt_buf_.assign(fdt_src->view().begin(), fdt_src->view().end());
    reader->info_.fields.reserve(reader->fields_.size());

    // Parse .tim per-field.
    {
        const std::string_view tim_sv = tim_src->view();
        for (auto& fr : reader->fields_) {
            const std::uint64_t off = fr.stats.tim_offset;
            const std::uint64_t sz = fr.stats.tim_size;
            if (off + sz > tim_sv.size())
                return Status::Corruption(".tim slice out of range");
            const char* p = tim_sv.data() + off;
            const char* end = p + sz;
            fr.terms.reserve(static_cast<std::size_t>(fr.stats.term_count));
            while (p < end) {
                TermEntry te;
                SPP_ASSIGN_OR_RETURN(auto term_view, ReadStrView(p, end));
                te.term.assign(term_view.data(), term_view.size());
                SPP_ASSIGN_OR_RETURN(auto df, store::DecodeVarUint32(p, end));
                te.df = df;
                SPP_ASSIGN_OR_RETURN(auto ttf, store::DecodeVarUint64(p, end));
                te.total_tf = ttf;
                SPP_ASSIGN_OR_RETURN(auto po, store::DecodeVarUint64(p, end));
                te.postings_offset = po;
                SPP_ASSIGN_OR_RETURN(auto pb, store::DecodeVarUint64(p, end));
                te.postings_bytes = pb;
                fr.terms.push_back(std::move(te));
            }
            // Defensive sort (writer already emits sorted, but trust-but-verify).
            if (!std::is_sorted(
                    fr.terms.begin(), fr.terms.end(), [](const TermEntry& a, const TermEntry& b) {
                        return a.term < b.term;
                    })) {
                std::sort(fr.terms.begin(),
                          fr.terms.end(),
                          [](const TermEntry& a, const TermEntry& b) { return a.term < b.term; });
            }
            reader->info_.fields.push_back(fr.stats);
        }
    }

    // Parse .fdx
    {
        const std::string_view sv = fdx_src->view();
        const char* p = sv.data();
        const char* end = p + sv.size();
        SPP_ASSIGN_OR_RETURN(auto count, ReadU64LE(p, end));
        reader->fdx_offsets_.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            SPP_ASSIGN_OR_RETURN(auto off, ReadU64LE(p, end));
            reader->fdx_offsets_.push_back(off);
        }
        if (count != reader->info_.doc_count) {
            return Status::Corruption(".fdx doc count != .si doc count");
        }
    }

    // v0.2: .dvq is optional. A missing file simply means no per-doc quality
    // was stored in this segment; it is not an error.
    {
        auto dvq_src = dir.ReadFile(reader->info_.stem + kSegDocQualityExt);
        if (dvq_src.ok()) {
            const std::string_view sv = (*dvq_src).view();
            const char* p = sv.data();
            const char* end = p + sv.size();
            SPP_ASSIGN_OR_RETURN(auto magic, ReadU32LE(p, end));
            if (magic != kSegDocQualityMagic)
                return Status::Corruption(".dvq bad magic");
            SPP_ASSIGN_OR_RETURN(auto count, ReadU32LE(p, end));
            if (count != reader->info_.doc_count)
                return Status::Corruption(".dvq doc count mismatch");
            reader->doc_quality_.reserve(count);
            for (std::uint32_t i = 0; i < count; ++i) {
                SPP_ASSIGN_OR_RETURN(auto v, ReadF32LE(p, end));
                reader->doc_quality_.push_back(v);
            }
        }
    }

    return reader;
}

const FieldRead* SegmentReader::field(FieldId field_id) const {
    if (field_id >= fields_.size())
        return nullptr;
    return &fields_[field_id];
}

FieldId SegmentReader::GetFieldId(std::string_view name) const {
    for (std::size_t i = 0; i < fields_.size(); ++i) {
        if (fields_[i].stats.name == name)
            return static_cast<FieldId>(i);
    }
    return kInvalidFieldId;
}

const TermEntry* SegmentReader::FindTerm(FieldId field_id, std::string_view term) const {
    const FieldRead* fr = field(field_id);
    if (fr == nullptr)
        return nullptr;
    auto it = std::lower_bound(
        fr->terms.begin(), fr->terms.end(), term, [](const TermEntry& te, std::string_view t) {
            return te.term < t;
        });
    if (it == fr->terms.end() || it->term != term)
        return nullptr;
    return &(*it);
}

std::string_view SegmentReader::PostingBytes(FieldId field_id, const TermEntry& te) const {
    const FieldRead* fr = field(field_id);
    if (fr == nullptr)
        return {};
    const std::uint64_t abs_off = fr->stats.doc_offset + te.postings_offset;
    if (abs_off + te.postings_bytes > doc_buf_.size())
        return {};
    return std::string_view{doc_buf_.data() + abs_off, te.postings_bytes};
}

float SegmentReader::DocQuality(DocId id) const {
    if (id >= doc_quality_.size())
        return 0.0f;
    return doc_quality_[id];
}

std::string_view SegmentReader::StoredFields(DocId id) const {
    if (id >= fdx_offsets_.size())
        return {};
    const std::uint64_t off = fdx_offsets_[id];
    if (off >= fdt_buf_.size())
        return {};
    const char* p = fdt_buf_.data() + off;
    const char* end = fdt_buf_.data() + fdt_buf_.size();
    auto len = store::DecodeVarUint32(p, end);
    if (!len.ok())
        return {};
    if (static_cast<std::uint64_t>(end - p) < *len)
        return {};
    return std::string_view{p, *len};
}

}  // namespace spp::index
