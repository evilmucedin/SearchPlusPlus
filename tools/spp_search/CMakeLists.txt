add_executable(spp_search main.cpp)
target_link_libraries(spp_search PRIVATE spp_core)
spp_apply_warnings(spp_search)
spp_apply_sanitizers(spp_search)
