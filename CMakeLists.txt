cmake_minimum_required(VERSION 3.20)
project(daysetta LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

file(GLOB_RECURSE DAYSETTA_CORE CONFIGURE_DEPENDS
  src/ipc/*.cpp
  src/protocol/*.cpp
  src/event/*.cpp
  src/scene/*.cpp
  src/render/*.cpp
  src/sdk/*.cpp
  src/server/server.cpp
)

add_library(daysetta_core STATIC ${DAYSETTA_CORE})
target_include_directories(daysetta_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(daysetta_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(daysetta_core PUBLIC pthread)

function(daysetta_executable name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE daysetta_core)
  target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic -Werror)
endfunction()

# ds-exec: genuine-Apple-code execution layer (software PAC, LSE/PAC lowering, cache mapping).
file(GLOB DS_EXEC_SRC CONFIGURE_DEPENDS src/exec/*.cpp src/darwin/*.cpp)
add_library(ds-exec STATIC ${DS_EXEC_SRC})
target_include_directories(ds-exec PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(ds-exec PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(ds-exec PUBLIC pthread)

add_executable(ds-exec-probe tools/ds-exec-probe/main.cpp)
target_link_libraries(ds-exec-probe PRIVATE ds-exec)
target_compile_options(ds-exec-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(ds-run tools/ds-run/main.cpp)
target_link_libraries(ds-run PRIVATE ds-exec)
target_compile_options(ds-run PRIVATE -Wall -Wextra -Wpedantic -Werror)

daysetta_executable(daysetta-server src/server/main.cpp)
daysetta_executable(daysetta-ctl src/ctl/main.cpp)
daysetta_executable(daysetta-session src/session/main.cpp)

file(GLOB DAYSETTA_BROKER_SRC CONFIGURE_DEPENDS src/broker/*.cpp)
add_executable(daysetta-broker ${DAYSETTA_BROKER_SRC})
target_link_libraries(daysetta-broker PRIVATE daysetta_core)
target_compile_options(daysetta-broker PRIVATE -Wall -Wextra -Wpedantic -Werror)

option(DAYSETTA_BUILD_TESTS "Build the DaySetta test suite" ON)
if(DAYSETTA_BUILD_TESTS)
  enable_testing()
  file(GLOB DAYSETTA_TESTS CONFIGURE_DEPENDS tests/*.cpp)
  add_executable(daysetta_tests ${DAYSETTA_TESTS})
  target_link_libraries(daysetta_tests PRIVATE daysetta_core ds-exec)
  target_compile_options(daysetta_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_definitions(daysetta_tests PRIVATE
    DS_BROKER_BIN="$<TARGET_FILE:daysetta-broker>"
    DS_SERVER_BIN="$<TARGET_FILE:daysetta-server>"
    DS_CTL_BIN="$<TARGET_FILE:daysetta-ctl>"
    DS_PROBE_BIN="$<TARGET_FILE:ds-exec-probe>"
    DS_RUN_BIN="$<TARGET_FILE:ds-run>"
  )
  add_dependencies(daysetta_tests daysetta-broker daysetta-server daysetta-ctl ds-exec-probe ds-run)
  add_test(NAME daysetta COMMAND daysetta_tests)
  set_tests_properties(daysetta PROPERTIES TIMEOUT 180)
  if(CMAKE_CROSSCOMPILING)  # child processes (broker/server) exec via binfmt_misc qemu: give them the sysroot too
    set_tests_properties(daysetta PROPERTIES ENVIRONMENT "QEMU_LD_PREFIX=${CMAKE_FIND_ROOT_PATH}")
  endif()
endif()
