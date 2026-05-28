#include "spp/index/manifest.h"

#include "spp/json/json_parser.h"
#include "spp/json/json_serializer.h"
#include "spp/json/json_value.h"
#include "spp/store/file_io.h"

#include <string>
#include <utility>

namespace spp::index {

Expected<Manifest> LoadManifest(store::Directory& dir) {
    if (!dir.Exists(kManifestName)) {
        return Manifest{};
    }
    auto src = dir.ReadFile(kManifestName);
    if (!src.ok())
        return src.status();
    auto parsed = spp::json::Parse(src->view());
    if (!parsed.ok())
        return parsed.status();
    const auto& v = *parsed;
    if (!v.is_object())
        return Status::Corruption("manifest: expected object");

    Manifest m;
    if (const auto* g = v.find("generation"); g && g->is_int()) {
        m.generation = static_cast<Generation>(g->as_int());
    }
    if (const auto* s = v.find("segments"); s && s->is_array()) {
        for (const auto& seg : s->as_array()) {
            if (!seg.is_object())
                return Status::Corruption("manifest: segment must be object");
            ManifestSegment ms;
            if (const auto* st = seg.find("stem"); st && st->is_string()) {
                ms.stem = st->as_string();
            } else {
                return Status::Corruption("manifest: segment missing stem");
            }
            if (const auto* dc = seg.find("doc_count"); dc && dc->is_int()) {
                ms.doc_count = static_cast<DocId>(dc->as_int());
            }
            m.segments.push_back(std::move(ms));
        }
    }
    return m;
}

Status SaveManifest(store::Directory& dir, const Manifest& manifest) {
    spp::json::JsonArray segs;
    for (const auto& s : manifest.segments) {
        spp::json::JsonObject o;
        o["stem"] = s.stem;
        o["doc_count"] = static_cast<std::int64_t>(s.doc_count);
        segs.push_back(spp::json::JsonValue{std::move(o)});
    }
    spp::json::JsonObject root;
    root["generation"] = static_cast<std::int64_t>(manifest.generation);
    root["segments"] = spp::json::JsonValue{std::move(segs)};
    const std::string blob = spp::json::SerializePretty(spp::json::JsonValue{std::move(root)}, 2);

    // Write to tmp, fsync the file, rename atomically, fsync the dir.
    auto sink = dir.CreateFile(kManifestTmpName);
    if (!sink.ok())
        return sink.status();
    SPP_RETURN_IF_ERROR((*sink)->Append(blob));
    SPP_RETURN_IF_ERROR((*sink)->Close());
    SPP_RETURN_IF_ERROR(dir.AtomicRenameWithin(kManifestTmpName, kManifestName));
    return dir.Sync();
}

}  // namespace spp::index
