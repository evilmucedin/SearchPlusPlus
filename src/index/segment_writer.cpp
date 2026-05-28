#include "spp/index/segment_writer.h"

#include "spp/index/segment_info.h"
#include "spp/store/file_io.h"
#include "spp/store/varbyte.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace spp::index {

namespace {

// Little-endian fixed-width writers for the .si header.
void AppendU32LE(std::string& out, std::uint32_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
    out.push_back(static_cast<char>((v >> 16) & 0xFF));
    out.push_back(static_cast<char>((v >> 24) & 0xFF));
}
void AppendU64LE(std::string& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
void AppendStr(std::string& out, std::string_view s) {
    store::EncodeVarUint32(static_cast<std::uint32_t>(s.size()), out);
    out.append(s);
}

Status WritePostings(const PostingList& sorted_postings, std::string& out_doc) {
    // df, then delta-encoded doc-ids and tfs, both varuint.
    store::EncodeVarUint32(static_cast<std::uint32_t>(sorted_postings.size()), out_doc);
    DocId prev = 0;
    for (std::size_t i = 0; i < sorted_postings.size(); ++i) {
        const auto& p = sorted_postings[i];
        const std::uint32_t delta = (i == 0) ? p.doc_id : (p.doc_id - prev);
        store::EncodeVarUint32(delta, out_doc);
        store::EncodeVarUint32(p.tf, out_doc);
        prev = p.doc_id;
    }
    return Status::Ok();
}

}  // namespace

Expected<SegmentInfo> SealSegment(MutableSegment& segment,
                                  store::Directory& dir,
                                  std::string stem) {
    SegmentInfo info;
    info.stem = stem;
    info.format_version = kSegmentFormatVersion;
    info.doc_count = segment.doc_count();

    const Schema& schema = segment.schema();
    info.fields.reserve(schema.field_count());

    // Build .tim and .doc as in-memory buffers, then write them out.
    std::string tim_buf;
    std::string doc_buf;

    for (std::size_t fi = 0; fi < schema.field_count(); ++fi) {
        FieldStats fs;
        fs.name = schema.fields()[fi].name;
        const MutableFieldData& fd = segment.field_data()[fi];
        fs.doc_count = fd.doc_count;
        fs.sum_field_length = fd.sum_field_length;
        fs.term_count = fd.postings.size();
        fs.tim_offset = tim_buf.size();
        fs.doc_offset = doc_buf.size();

        // Sort terms.
        std::vector<const std::string*> sorted_terms;
        sorted_terms.reserve(fd.postings.size());
        for (const auto& [t, _] : fd.postings)
            sorted_terms.push_back(&t);
        std::sort(sorted_terms.begin(),
                  sorted_terms.end(),
                  [](const std::string* a, const std::string* b) { return *a < *b; });

        for (const std::string* term : sorted_terms) {
            const PostingList& pl_in = fd.postings.at(*term);

            // Sort the per-term posting list by doc_id (insertion order may not match).
            PostingList pl = pl_in;
            std::sort(pl.begin(), pl.end(), [](const Posting& a, const Posting& b) {
                return a.doc_id < b.doc_id;
            });

            const std::uint64_t postings_offset = doc_buf.size();
            std::uint64_t total_tf = 0;
            for (const auto& p : pl)
                total_tf += p.tf;

            SPP_RETURN_IF_ERROR(WritePostings(pl, doc_buf));
            const std::uint64_t postings_bytes = doc_buf.size() - postings_offset;

            // Term dictionary entry: term, df, total_tf, postings_offset_within_field,
            // postings_bytes.
            AppendStr(tim_buf, *term);
            store::EncodeVarUint32(static_cast<std::uint32_t>(pl.size()), tim_buf);
            store::EncodeVarUint64(total_tf, tim_buf);
            store::EncodeVarUint64(postings_offset - fs.doc_offset, tim_buf);
            store::EncodeVarUint64(postings_bytes, tim_buf);
        }

        fs.tim_size = tim_buf.size() - fs.tim_offset;
        fs.doc_size = doc_buf.size() - fs.doc_offset;
        info.fields.push_back(std::move(fs));
    }

    // Stored fields .fdt and .fdx.
    std::string fdt_buf;
    std::string fdx_buf;
    AppendU64LE(fdx_buf, static_cast<std::uint64_t>(segment.stored_field_blobs().size()));
    for (const auto& blob : segment.stored_field_blobs()) {
        AppendU64LE(fdx_buf, static_cast<std::uint64_t>(fdt_buf.size()));
        store::EncodeVarUint32(static_cast<std::uint32_t>(blob.size()), fdt_buf);
        fdt_buf.append(blob);
    }

    // .si: magic, version, doc_count, analyzer fingerprint, field stats table.
    std::string si_buf;
    AppendU32LE(si_buf, kSegmentFormatMagic);
    AppendU32LE(si_buf, info.format_version);
    AppendU64LE(si_buf, info.doc_count);
    AppendStr(si_buf, info.analyzer_fingerprint);
    store::EncodeVarUint32(static_cast<std::uint32_t>(info.fields.size()), si_buf);
    for (const auto& fs : info.fields) {
        AppendStr(si_buf, fs.name);
        store::EncodeVarUint32(fs.doc_count, si_buf);
        store::EncodeVarUint64(fs.sum_field_length, si_buf);
        store::EncodeVarUint64(fs.term_count, si_buf);
        store::EncodeVarUint64(fs.tim_offset, si_buf);
        store::EncodeVarUint64(fs.tim_size, si_buf);
        store::EncodeVarUint64(fs.doc_offset, si_buf);
        store::EncodeVarUint64(fs.doc_size, si_buf);
    }

    // Write each file and fsync.
    auto write_one = [&](const char* ext, const std::string& bytes) -> Status {
        auto sink = dir.CreateFile(stem + ext);
        if (!sink.ok())
            return sink.status();
        SPP_RETURN_IF_ERROR((*sink)->Append(bytes));
        return (*sink)->Close();
    };

    SPP_RETURN_IF_ERROR(write_one(kSegPostingsExt, doc_buf));
    SPP_RETURN_IF_ERROR(write_one(kSegTermsExt, tim_buf));
    SPP_RETURN_IF_ERROR(write_one(kSegStoredExt, fdt_buf));
    SPP_RETURN_IF_ERROR(write_one(kSegStoredIdxExt, fdx_buf));
    SPP_RETURN_IF_ERROR(write_one(kSegInfoExt, si_buf));
    SPP_RETURN_IF_ERROR(dir.Sync());

    return info;
}

}  // namespace spp::index
