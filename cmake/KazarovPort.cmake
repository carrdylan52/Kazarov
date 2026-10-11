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

# kz_port_third_party(<out_var> [SEARCH <dir>...])
#
# The single-file libraries the port library compiles in, at pinned commits: each is taken from the
# first SEARCH directory that has it (a Flatpak build has no network and lists them as sources), or
# downloaded into the build tree. <out_var> receives the directories to put on the include path.
function(kz_port_third_party out_var)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SEARCH")
    set(dr_libs https://raw.githubusercontent.com/mackron/dr_libs/dfe8377631000664666519fdb83da193fd8037f4)
    set(stb https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20)
    set(files
        dr_wav.h ${dr_libs}/dr_wav.h 03e70c1a2d9787cd7ed3e966c075bea7bac6373f759db9cd7ca9ccdfc4ec4493
        dr_flac.h ${dr_libs}/dr_flac.h 111144e778f55738db6851cb226015c419e00d04b916a09506d4856d9cff945c
        stb_vorbis.c ${stb}/stb_vorbis.c 4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636
        stb_truetype.h ${stb}/stb_truetype.h ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab
        stb_image.h ${stb}/stb_image.h 594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3)
    set(fetched ${CMAKE_BINARY_DIR}/third_party)
    set(dirs "")
    while(files)
        list(POP_FRONT files name url hash)
        set(found "")
        foreach(dir IN LISTS arg_SEARCH)
            if(EXISTS ${dir}/${name})
                set(found ${dir})
                break()
            endif()
        endforeach()
        if(NOT found)
            set(found ${fetched})
            if(NOT EXISTS ${fetched}/${name})
                file(DOWNLOAD ${url} ${fetched}/${name} EXPECTED_HASH SHA256=${hash})
            endif()
        endif()
        list(APPEND dirs ${found})
    endwhile()
    list(REMOVE_DUPLICATES dirs)
    set(${out_var} ${dirs} PARENT_SCOPE)
endfunction()

# kz_port_library(<target> APP_NAME <name> APP_ID <id> ENV_PREFIX <prefix> MIN_VULKAN <1.3|1.4> ICON <file>
#                 [APP_IDENTIFIER <reverse-dns>] [PROJECT_LABEL <text>] [PROJECT_URL <url>]
#                 [DISCORD_CLIENT_ID <id>] [DISCORD_IMAGE <asset>] [DISCORD_IMAGE_TEXT <text>]
#                 [SHADER_DIRS <dir>...] [THIRD_PARTY_DIRS <dir>...] [DEFINES <define>...])
#
# An OBJECT library of the shared host code (port/gfx, port/platform, port/audio, port/sce_core,
# port/runtime), compiled without any game header. APP_NAME is the name the game shows, APP_ID its
# lower-case identifier (the directory of an installed copy's data and saves), ENV_PREFIX what its
# environment variables start with, underscore included, MIN_VULKAN the oldest Vulkan version the
# game accepts in the loader and the device, ICON the window icon as raw square RGBA pixels.
# APP_IDENTIFIER names the game to the desktop (APP_ID if not given). PROJECT_LABEL and PROJECT_URL
# are the Discord presence's button, DISCORD_* its application and small image; without
# DISCORD_CLIENT_ID there is no presence. They reach the code as generated/app/app_info.hpp and
# generated/app/icon.hpp. SHADER_DIRS adds the game's own shaders to the renderer's.
# THIRD_PARTY_DIRS are where kz_port_third_party looks before it downloads.
function(kz_port_library target)
    set(strings APP_NAME APP_ID APP_IDENTIFIER ENV_PREFIX PROJECT_LABEL PROJECT_URL DISCORD_CLIENT_ID
        DISCORD_IMAGE DISCORD_IMAGE_TEXT)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "${strings};MIN_VULKAN;ICON"
        "SHADER_DIRS;THIRD_PARTY_DIRS;DEFINES")
    foreach(required APP_NAME APP_ID ENV_PREFIX MIN_VULKAN ICON)
        if(NOT DEFINED arg_${required})
            message(FATAL_ERROR "kz_port_library(${target}) needs ${required}")
        endif()
    endforeach()
    if(NOT DEFINED arg_APP_IDENTIFIER)
        set(arg_APP_IDENTIFIER ${arg_APP_ID})
    endif()

    if(NOT arg_MIN_VULKAN MATCHES "^1\\.([34])$")
        message(FATAL_ERROR "kz_port_library(${target}) MIN_VULKAN is 1.3 or 1.4, not ${arg_MIN_VULKAN}")
    endif()
    # VK_MAKE_API_VERSION(0, 1, minor, 0), so the header needs no Vulkan include.
    math(EXPR min_vulkan_version "(1 << 22) | (${CMAKE_MATCH_1} << 12)" OUTPUT_FORMAT HEXADECIMAL)

    set(generated ${CMAKE_BINARY_DIR}/generated)
    foreach(name IN LISTS strings)
        string(REPLACE "\\" "\\\\" escaped "${arg_${name}}")
        string(REPLACE "\"" "\\\"" escaped "${escaped}")
        set(${name}_LITERAL "\"${escaped}\"")
    endforeach()
    file(CONFIGURE OUTPUT ${generated}/app/app_info.hpp
        CONTENT "#pragma once

#include <cstdint>

namespace app {
inline constexpr const char *kName = ${APP_NAME_LITERAL};
inline constexpr const char *kId = ${APP_ID_LITERAL};
inline constexpr const char *kIdentifier = ${APP_IDENTIFIER_LITERAL};
inline constexpr uint32_t kMinVulkanVersion = ${min_vulkan_version};
inline constexpr const char *kProjectLabel = ${PROJECT_LABEL_LITERAL};
inline constexpr const char *kProjectUrl = ${PROJECT_URL_LITERAL};
inline constexpr const char *kDiscordClientId = ${DISCORD_CLIENT_ID_LITERAL};
inline constexpr const char *kDiscordImage = ${DISCORD_IMAGE_LITERAL};
inline constexpr const char *kDiscordImageText = ${DISCORD_IMAGE_TEXT_LITERAL};
} // namespace app

#define KZ_APP_ENV(name) ${ENV_PREFIX_LITERAL} name
")

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${arg_ICON})
    file(SIZE ${arg_ICON} icon_bytes)
    set(icon_size 0)
    set(icon_square 0)
    while(icon_square LESS icon_bytes)
        math(EXPR icon_size "${icon_size} + 1")
        math(EXPR icon_square "${icon_size} * ${icon_size} * 4")
    endwhile()
    if(NOT icon_square EQUAL icon_bytes)
        message(FATAL_ERROR "kz_port_library(${target}) ICON ${arg_ICON} is not square RGBA pixels")
    endif()
    file(READ ${arg_ICON} icon_hex HEX)
    string(REGEX REPLACE "(..)" "0x\\1," icon_data "${icon_hex}")
    file(CONFIGURE OUTPUT ${generated}/app/icon.hpp
        CONTENT "#pragma once\n\n#include <cstdint>\n\nnamespace app {\ninline constexpr int kIconSize = ${icon_size};\ninline constexpr std::uint8_t kIconRgba[] = {${icon_data}};\n} // namespace app\n")

    kz_port_shaders(shader_headers DIRS ${KZ_ROOT}/port/gfx/shaders ${arg_SHADER_DIRS})
    file(GLOB_RECURSE sources CONFIGURE_DEPENDS ${KZ_ROOT}/port/gfx/*.cpp ${KZ_ROOT}/port/platform/*.cpp
        ${KZ_ROOT}/port/audio/*.cpp)
    file(GLOB_RECURSE game_math CONFIGURE_DEPENDS ${KZ_ROOT}/port/sce_core/*.cpp
        ${KZ_ROOT}/port/runtime/*.cpp)
    kz_port_third_party(third_party_dirs SEARCH ${arg_THIRD_PARTY_DIRS})

    add_library(${target} OBJECT ${sources} ${game_math} ${shader_headers})
    target_include_directories(${target} PUBLIC ${KZ_ROOT}/port ${generated})
    target_include_directories(${target} SYSTEM PRIVATE ${third_party_dirs})
    # The game's arithmetic, which these carry out for it, rounds the same on arm64 as on x86-64 only
    # without fused multiply-adds.
    set_property(SOURCE ${game_math} APPEND PROPERTY COMPILE_OPTIONS -ffp-contract=off)
    target_link_libraries(${target} PUBLIC SDL3::SDL3 Vulkan::Vulkan nlohmann_json::nlohmann_json)
    target_compile_definitions(${target} PUBLIC ${arg_DEFINES})
    target_compile_options(${target} PRIVATE -Wall -ffunction-sections -fdata-sections)
endfunction()
