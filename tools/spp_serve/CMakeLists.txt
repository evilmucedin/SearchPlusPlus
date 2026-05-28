add_executable(spp_serve main.cpp)
target_link_libraries(spp_serve PRIVATE spp_core)
spp_apply_warnings(spp_serve)
spp_apply_sanitizers(spp_serve)
