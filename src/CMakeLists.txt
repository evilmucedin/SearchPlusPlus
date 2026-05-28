add_library(spp_core STATIC
  base/status.cpp
  base/logging.cpp

  json/json_parser.cpp
  json/json_serializer.cpp

  analyze/standard_analyzer.cpp

  store/platform.cpp
  store/filesystem_directory.cpp
  store/file_io.cpp
  store/varbyte.cpp

  index/schema.cpp
  index/mutable_segment.cpp
  index/segment_writer.cpp
  index/segment_reader.cpp
  index/manifest.cpp
  index/translog.cpp
  index/index_writer.cpp

  query/query_ast.cpp
  query/query_parser.cpp
  query/iterators.cpp
  query/bm25_scorer.cpp
  query/features.cpp
  query/ranker.cpp
  query/searcher.cpp


  http/platform.cpp
  http/http_message.cpp
  http/http_parser.cpp
  http/http_server.cpp
  http/router.cpp

  server/search_server.cpp
)

# Embed the CatBoost C++ export. The default file is a stub that returns
# features[0] (== BM25 total) so the engine builds without a real model;
# point SPP_LTR_MODEL_FILE at your CatBoost export and rebuild to deploy.
if(SPP_WITH_LTR_MODEL)
  target_sources(spp_core PRIVATE ${SPP_LTR_MODEL_FILE})
  target_include_directories(spp_core PRIVATE ${CMAKE_SOURCE_DIR})
endif()

target_include_directories(spp_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(spp_core PUBLIC cxx_std_20)

if(WIN32)
  target_link_libraries(spp_core PUBLIC ws2_32)
endif()

find_package(Threads REQUIRED)
target_link_libraries(spp_core PUBLIC Threads::Threads)

spp_apply_warnings(spp_core)
spp_apply_sanitizers(spp_core)
