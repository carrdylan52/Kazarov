# Two games' port libraries in one build, configured only: test_port_library.py reads what each
# generates. The packages kz_port_library links are stand-ins.
cmake_minimum_required(VERSION 3.28)
project(kz_two_ports LANGUAGES CXX)

foreach(package SDL3::SDL3 Vulkan::Vulkan nlohmann_json::nlohmann_json)
    add_library(${package} INTERFACE IMPORTED)
endforeach()
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/KazarovPort.cmake)

string(ASCII 1 2 3 4 first_pixel)
string(ASCII 5 6 7 8 second_pixel)
file(WRITE ${CMAKE_BINARY_DIR}/first.rgba "${first_pixel}")
file(WRITE ${CMAKE_BINARY_DIR}/second.rgba "${second_pixel}")

kz_port_library(first APP_NAME "First" APP_ID first ENV_PREFIX FIRST_ MIN_VULKAN 1.3
    ICON ${CMAKE_BINARY_DIR}/first.rgba THIRD_PARTY_DIRS ${THIRD_PARTY})
kz_port_library(second APP_NAME "Second" APP_ID second ENV_PREFIX SECOND_ MIN_VULKAN 1.4
    ICON ${CMAKE_BINARY_DIR}/second.rgba THIRD_PARTY_DIRS ${THIRD_PARTY} SHADER_DIRS ${SECOND_SHADERS})

foreach(target first second)
    file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/${target}.includes
        CONTENT "$<JOIN:$<TARGET_PROPERTY:${target},INTERFACE_INCLUDE_DIRECTORIES>,\n>\n")
    get_target_property(sources ${target} SOURCES)
    list(FILTER sources INCLUDE REGEX "\\.spv\\.hpp$")
    list(JOIN sources "\n" sources)
    file(WRITE ${CMAKE_BINARY_DIR}/${target}.shaders "${sources}\n")
endforeach()
