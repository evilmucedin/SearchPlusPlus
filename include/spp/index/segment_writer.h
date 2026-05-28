#pragma once

#include "spp/base/expected.h"
#include "spp/base/status.h"
#include "spp/index/mutable_segment.h"
#include "spp/index/segment_info.h"
#include "spp/store/directory.h"

#include <string>

namespace spp::index {

// Seal a MutableSegment to disk. On success, returns the SegmentInfo for the
// caller to add to the manifest. All segment files are fsynced before this call returns.
Expected<SegmentInfo> SealSegment(MutableSegment& segment, store::Directory& dir, std::string stem);

}  // namespace spp::index
