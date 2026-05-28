#pragma once

#include "spp/base/expected.h"
#include "spp/base/status.h"
#include "spp/base/types.h"
#include "spp/store/directory.h"

#include <cstdint>
#include <string>
#include <vector>

namespace spp::index {

struct ManifestSegment {
    std::string stem;
    DocId doc_count = 0;
};

struct Manifest {
    Generation generation = 0;
    std::vector<ManifestSegment> segments;
};

inline constexpr const char* kManifestName = "manifest";
inline constexpr const char* kManifestTmpName = "manifest.tmp";

// Read the manifest from `dir`. If no manifest file is present, returns an empty
// manifest at generation 0.
Expected<Manifest> LoadManifest(store::Directory& dir);

// Atomically write the manifest to `dir`: write to tmp file, fsync, rename, fsync dir.
Status SaveManifest(store::Directory& dir, const Manifest& manifest);

}  // namespace spp::index
