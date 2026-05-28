# Builds the `searchplusplus` Python extension module.
#
# Off by default — gated by SPP_BUILD_PYTHON. Requires:
#   - Python3 development headers (the Python.h-providing package on your OS)
#   - pybind11 (vcpkg manifest pulls it in when SPP_BUILD_PYTHON=ON; or
#     install via `pip install pybind11` and pass its CMake path).

find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(searchplusplus MODULE
  src/searchplusplus.cpp
)

target_link_libraries(searchplusplus PRIVATE spp_core)
target_include_directories(searchplusplus PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Warnings policy matches the rest of the tree, but pybind11 itself emits
# a few -Wconversion / -Wsign-conversion warnings on some compilers — the
# `spp_apply_warnings` macro is fine here because we don't enable those.
spp_apply_warnings(searchplusplus)
spp_apply_sanitizers(searchplusplus)

# Put the .so next to the example so the example can `import searchplusplus`
# without a PYTHONPATH dance.
set_target_properties(searchplusplus PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python"
)
