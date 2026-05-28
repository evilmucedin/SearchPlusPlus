find_package(benchmark CONFIG QUIET)

if(NOT benchmark_FOUND)
  message(STATUS "google-benchmark not found; skipping benchmark targets. "
                 "Install via vcpkg (benchmark) or `brew install google-benchmark` to enable.")
  return()
endif()

function(spp_add_bench name src)
  add_executable(${name} ${src})
  target_link_libraries(${name} PRIVATE spp_core benchmark::benchmark benchmark::benchmark_main)
  target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
  spp_apply_warnings(${name})
  # Benches link the sanitizer-instrumented spp_core, so they must also pull
  # in the matching runtime — otherwise asan/ubsan symbols go unresolved at
  # link time on the asan preset and on macOS.
  spp_apply_sanitizers(${name})
endfunction()

spp_add_bench(posting_list_bench       posting_list_bench.cpp)
spp_add_bench(query_latency_bench      query_latency_bench.cpp)
spp_add_bench(indexing_throughput_bench indexing_throughput_bench.cpp)
spp_add_bench(rerank_latency_bench     rerank_latency_bench.cpp)
