cmake_minimum_required(VERSION 3.20)
project(daysetta-metal CXX)
# Arch-neutral (aarch64/x86_64). Builds Darling's indium (Metal on Vulkan) and a headless smoke test.
set(INDIUM_DIR "$ENV{HOME}/darling/src/external/metal/deps/indium" CACHE PATH "indium source")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include stdexcept -include cstdint -include cstring") # newer GCC/libstdc++
add_subdirectory(${INDIUM_DIR} indium EXCLUDE_FROM_ALL)
find_package(PNG REQUIRED)
set(AIR "${INDIUM_DIR}/test/triangle/AAPLShaders.metallib")
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shader.h
  COMMAND xxd -i -n shader ${AIR} ${CMAKE_CURRENT_BINARY_DIR}/shader.h DEPENDS ${AIR})
add_executable(offscreen-triangle offscreen-triangle/main.cpp ${CMAKE_CURRENT_BINARY_DIR}/shader.h)
target_include_directories(offscreen-triangle PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(offscreen-triangle PRIVATE indium PNG::PNG)
set_target_properties(offscreen-triangle PROPERTIES CXX_STANDARD 17)
add_dependencies(offscreen-triangle iridium mtl2spv)
