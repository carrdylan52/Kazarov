# The port library's own tests, without a game:
#
#   cmake -S port/tests -B build/port-tests -G Ninja && cmake --build build/port-tests
#   ctest --test-dir build/port-tests
#
# A game's test executable compiles the same *_test.cpp files against its own build of the library.
cmake_minimum_required(VERSION 3.28)
project(kazarov_port_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(SDL3 REQUIRED CONFIG)
find_package(Vulkan REQUIRED)
find_package(nlohmann_json 3 REQUIRED CONFIG)
find_package(GTest REQUIRED)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/KazarovPort.cmake)

# Any square RGBA image will do; kz_port_library only embeds it.
set(icon ${CMAKE_BINARY_DIR}/icon.rgba)
string(ASCII 64 96 128 127 pixel)
file(WRITE ${icon} "${pixel}")
kz_port_library(kz_port APP_NAME "Kazarov" APP_ID kazarov ENV_PREFIX KZ_ MIN_VULKAN 1.3 ICON ${icon})

file(GLOB tests CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)
add_executable(kz_port_tests main.cpp ${tests} $<TARGET_OBJECTS:kz_port>)
target_include_directories(kz_port_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kz_port_tests PRIVATE kz_port GTest::gtest)

# A case finishes in milliseconds, except the pipeline compiles, which a slow runner's lavapipe
# stretches past the default.
set(KZ_TEST_TIMEOUT 5 CACHE STRING "Seconds a test case may run before it is cut off")
enable_testing()
include(GoogleTest)
gtest_discover_tests(kz_port_tests DISCOVERY_MODE PRE_TEST TEST_FILTER -PlatformClock.*
    PROPERTIES TIMEOUT ${KZ_TEST_TIMEOUT})
gtest_discover_tests(kz_port_tests DISCOVERY_MODE PRE_TEST TEST_FILTER PlatformClock.*
    PROPERTIES TIMEOUT ${KZ_TEST_TIMEOUT} RUN_SERIAL TRUE)
