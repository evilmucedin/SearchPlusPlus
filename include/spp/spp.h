#pragma once

// Umbrella header for the SearchPlusPlus public API.
//
// Pulls in the headers a typical embedder needs to build, refresh, and
// query an index. Include this when you'd otherwise cherry-pick from
// spp/base, spp/index, and spp/query and don't have a reason to be
// selective about compile times.
//
// Modules not surfaced here (spp/analyze, spp/json, spp/store, spp/server)
// are either internal to the library or only relevant when extending it —
// reach for the per-module headers directly in those cases.

#include "spp/base/expected.h"
#include "spp/index/document.h"
#include "spp/index/index_reader.h"
#include "spp/index/index_writer.h"
#include "spp/index/schema.h"
#include "spp/query/query_parser.h"
#include "spp/query/searcher.h"
