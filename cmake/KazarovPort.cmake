# Rules for a game's host port: the shared port library under port/.
#
# Include from the port's CMakeLists.txt once C++ is enabled and SDL3, Vulkan and
# nlohmann_json are found. Generated headers go to ${CMAKE_BINARY_DIR}/generated.

get_filename_component(KZ_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# kz_port_shaders(<out_var> DIRS <dir>...)
#
# Every *.vert and *.frag in DIRS becomes generated/shaders/<file>.spv.hpp, its SPIR-V as a
# const uint32_t array named after the file (draw.frag -> draw_frag). <out_var> receives the headers.
function(kz_port_shaders out_var)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "DIRS")
    find_program(GLSLANG_VALIDATOR NAMES glslangValidator REQUIRED)
    set(header_dir ${CMAKE_BINARY_DIR}/generated/shaders)
    set(headers "")
    foreach(dir IN LISTS arg_DIRS)
        file(GLOB shaders CONFIGURE_DEPENDS ${dir}/*.vert ${dir}/*.frag)
        foreach(shader IN LISTS shaders)
            get_filename_component(shader_name ${shader} NAME)
            string(REPLACE "." "_" shader_symbol ${shader_name})
            set(header ${header_dir}/${shader_name}.spv.hpp)
            add_custom_command(OUTPUT ${header}
                COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.4 --quiet --vn ${shader_symbol}
                    --depfile ${header}.d -o ${header} ${shader}
                DEPENDS ${shader}
                DEPFILE ${header}.d
                COMMENT "Compiling ${shader_name} to SPIR-V")
            list(APPEND headers ${header})
        endforeach()
    endforeach()
    set(${out_var} ${headers} PARENT_SCOPE)
endfunction()

# kz_port_library(<target> APP_NAME <name> APP_ID <id> ENV_PREFIX <prefix> MIN_VULKAN <1.3|1.4>
#                 [SHADER_DIRS <dir>...] [DEFINES <define>...])
#
# An OBJECT library of the shared host code (port/gfx), compiled without any game header.
# APP_NAME is the name the game shows, APP_ID its lower-case identifier, ENV_PREFIX what its
# environment variables start with, underscore included, MIN_VULKAN the oldest Vulkan version the game
# accepts in the loader and the device. They reach the code as generated/app/app_info.hpp.
# SHADER_DIRS adds the game's own shaders to the renderer's.
function(kz_port_library target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "APP_NAME;APP_ID;ENV_PREFIX;MIN_VULKAN" "SHADER_DIRS;DEFINES")
    foreach(required APP_NAME APP_ID ENV_PREFIX MIN_VULKAN)
        if(NOT DEFINED arg_${required})
            message(FATAL_ERROR "kz_port_library(${target}) needs ${required}")
        endif()
    endforeach()

    if(NOT arg_MIN_VULKAN MATCHES "^1\\.([34])$")
        message(FATAL_ERROR "kz_port_library(${target}) MIN_VULKAN is 1.3 or 1.4, not ${arg_MIN_VULKAN}")
    endif()
    # VK_MAKE_API_VERSION(0, 1, minor, 0), so the header needs no Vulkan include.
    math(EXPR min_vulkan_version "(1 << 22) | (${CMAKE_MATCH_1} << 12)" OUTPUT_FORMAT HEXADECIMAL)

    set(generated ${CMAKE_BINARY_DIR}/generated)
    foreach(name APP_NAME APP_ID ENV_PREFIX)
        string(REPLACE "\\" "\\\\" escaped "${arg_${name}}")
        string(REPLACE "\"" "\\\"" escaped "${escaped}")
        set(${name}_LITERAL "\"${escaped}\"")
    endforeach()
    file(CONFIGURE OUTPUT ${generated}/app/app_info.hpp
        CONTENT "#pragma once\n\n#include <cstdint>\n\nnamespace app {\ninline constexpr const char *kName = ${APP_NAME_LITERAL};\ninline constexpr const char *kId = ${APP_ID_LITERAL};\ninline constexpr uint32_t kMinVulkanVersion = ${min_vulkan_version};\n} // namespace app\n\n#define KZ_APP_ENV(name) ${ENV_PREFIX_LITERAL} name\n")

    kz_port_shaders(shader_headers DIRS ${KZ_ROOT}/port/gfx/shaders ${arg_SHADER_DIRS})
    file(GLOB_RECURSE sources CONFIGURE_DEPENDS ${KZ_ROOT}/port/gfx/*.cpp)

    add_library(${target} OBJECT ${sources} ${shader_headers})
    target_include_directories(${target} PUBLIC ${KZ_ROOT}/port ${generated})
    target_link_libraries(${target} PUBLIC SDL3::SDL3 Vulkan::Vulkan nlohmann_json::nlohmann_json)
    target_compile_definitions(${target} PUBLIC ${arg_DEFINES})
    target_compile_options(${target} PRIVATE -Wall -ffunction-sections -fdata-sections)
endfunction()
