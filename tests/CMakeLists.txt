find_package(GTest CONFIG REQUIRED)

function(spp_add_test_target name)
  set(_srcs ${ARGN})
  add_executable(${name} ${_srcs})
  target_link_libraries(${name} PRIVATE spp_core GTest::gtest GTest::gtest_main)
  target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR})
  spp_apply_warnings(${name})
  spp_apply_sanitizers(${name})
  # Register the whole binary as a single ctest. Avoids gtest_discover_tests, which
  # spawns the test binary at configure time — that pre-run can deadlock under ASan
  # on macOS, and adds nothing here since each binary already runs its full suite.
  add_test(NAME ${name} COMMAND ${name})
endfunction()

spp_add_test_target(base_test
  base/status_test.cpp
  base/expected_test.cpp
)

spp_add_test_target(json_test
  json/json_parser_test.cpp
  json/json_serializer_test.cpp
)

spp_add_test_target(analyze_test
  analyze/standard_analyzer_test.cpp
)

spp_add_test_target(store_test
  store/varbyte_test.cpp
  store/file_io_test.cpp
  store/filesystem_directory_test.cpp
)

spp_add_test_target(index_test
  index/schema_test.cpp
  index/mutable_segment_test.cpp
  index/segment_roundtrip_test.cpp
  index/translog_test.cpp
  index/index_writer_test.cpp
)

spp_add_test_target(query_test
  query/query_parser_test.cpp
  query/iterators_test.cpp
  query/bm25_test.cpp
  query/searcher_test.cpp
  query/features_test.cpp
  query/ranker_test.cpp
)

spp_add_test_target(http_test
  http/http_parser_test.cpp
  http/http_server_test.cpp
)

spp_add_test_target(integration_test
  integration/e2e_http_test.cpp
  integration/nrt_test.cpp
  integration/crash_recovery_test.cpp
  integration/rerank_test.cpp
  integration/ltr_storage_test.cpp
  integration/search_concurrency_test.cpp
)
