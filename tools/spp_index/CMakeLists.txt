add_executable(spp_index main.cpp)
target_link_libraries(spp_index PRIVATE spp_core)
spp_apply_warnings(spp_index)
spp_apply_sanitizers(spp_index)
