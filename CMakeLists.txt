cmake_minimum_required(VERSION 3.16)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

if(APPLE)
  if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
    # ARM Macs start at OS 11.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum OS X deployment version")
  else()
    # SDL2 requires at least this version for everything to work properly.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.9" CACHE STRING "Minimum OS X deployment version")
  endif()
endif()

project(pd C CXX)

include(TargetArch)
include(FindPkgConfig)
include(AssetUtils)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# On first build set the build type to RelWithDebInfo if it's not already set
if (NOT EXISTS ${CMAKE_BINARY_DIR}/CMakeCache.txt)
  if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
  endif()
endif()

# Options

set(ROMID "ntsc-final" CACHE STRING "Which ROM to build an executable for: ntsc-final, pal-final or jpn-final")
set(DPRINT 0 CACHE BOOL "Enable debug print statements")
option(FORCE_CROSSCOMPILE "Force cross compiling" NO)

# Determine target arch

target_architecture(TARGET_ARCH)
if(TARGET_ARCH STREQUAL "i386")
  # for historical reasons
  set(TARGET_ARCH "i686")
endif()

# TODO
set(TARGET_IS_BIG_ENDIAN FALSE)

if((TARGET_ARCH MATCHES "unknown") OR (TARGET_ARCH STREQUAL ""))
  message(FATAL_ERROR "Unknown target arch")
elseif(TARGET_ARCH MATCHES "64")
  set(TARGET_IS_64BIT TRUE)
else()
  set(TARGET_IS_64BIT FALSE)
endif()

message(STATUS "Target arch: ${TARGET_ARCH}")

# Determine target OS and platform identifier ($arch-$os)

if(WIN32)
  # Windows
  set(TARGET_OS "windows")
elseif(APPLE)
  # OSX
  set(TARGET_OS "osx")
elseif(NINTENDO_SWITCH)
  # Switch
  set(TARGET_OS "nswitch")
elseif(UNIX)
  # Generic *nix; assume Linux
  set(TARGET_OS "linux")
else()
  # ???
  message(FATAL_ERROR "Unknown target OS or compiler")
endif()

set(TARGET_PLATFORM "${TARGET_ARCH}-${TARGET_OS}")

message(STATUS "Target platform: ${TARGET_PLATFORM}")

# Calculate version info and binary name

set(VERSION_IS_PAL 0)

if(ROMID STREQUAL "ntsc-final")
  set(VERSION 2)
  set(BIN_REGION "")
elseif(ROMID STREQUAL "pal-final")
  set(VERSION 4)
  set(BIN_REGION ".pal")
  set(VERSION_IS_PAL 1)
elseif(ROMID STREQUAL "jpn-final")
  set(VERSION 5)
  set(BIN_REGION ".jpn")
else()
  message(FATAL_ERROR "Unknown ROM ID: ${ROMID}")
endif()

set(VERSION_TARGET "${TARGET_PLATFORM}")
set(VERSION_ARCH "${TARGET_ARCH}")
set(VERSION_ROMID "${ROMID}")
set(VERSION_BUILD "${CMAKE_BUILD_TYPE}")

# The commit hash and branch resolved HERE are configure-time values and go stale
# as soon as anything is committed after the build directory is made. They are
# kept only for the Switch nacp, which is packed at configure time anyway.
# THE HEADER THE BINARY REPORTS ITSELF FROM DOES NOT USE THEM - it is written on
# every build by cmake/GitVersion.cmake, via the versioninfo target below.
execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE VERSION_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)

execute_process(
  COMMAND git rev-parse --abbrev-ref HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE VERSION_BRANCH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)

if((VERSION_BRANCH STREQUAL "") OR (VERSION_BRANCH STREQUAL "HEAD"))
  set(VERSION_BRANCH "port-custom")
endif()

message(STATUS "Version: ${VERSION_ROMID}, branch ${VERSION_BRANCH} (hash stamped per build)")

# Written before anything compiles, on every build, by the script above.
add_custom_target(versioninfo ALL
  COMMAND ${CMAKE_COMMAND}
    -DSRC_DIR=${CMAKE_SOURCE_DIR}
    -DTEMPLATE=${CMAKE_SOURCE_DIR}/port/include/versioninfo.h.in
    -DOUT_FILE=${CMAKE_BINARY_DIR}/port/include/versioninfo.h
    -DVERSION_TARGET=${VERSION_TARGET}
    -DVERSION_ARCH=${VERSION_ARCH}
    -DVERSION_ROMID=${VERSION_ROMID}
    -DVERSION_BUILD=${VERSION_BUILD}
    -P ${CMAKE_SOURCE_DIR}/cmake/GitVersion.cmake
  COMMENT "Stamping build with the current commit"
  VERBATIM
)

# And once now, so the first configure leaves a header on disk for anything that
# reads it before the target has run.
execute_process(
  COMMAND ${CMAKE_COMMAND}
    -DSRC_DIR=${CMAKE_SOURCE_DIR}
    -DTEMPLATE=${CMAKE_SOURCE_DIR}/port/include/versioninfo.h.in
    -DOUT_FILE=${CMAKE_BINARY_DIR}/port/include/versioninfo.h
    -DVERSION_TARGET=${VERSION_TARGET}
    -DVERSION_ARCH=${VERSION_ARCH}
    -DVERSION_ROMID=${VERSION_ROMID}
    -DVERSION_BUILD=${VERSION_BUILD}
    -P ${CMAKE_SOURCE_DIR}/cmake/GitVersion.cmake
)

set(ASSET_DIR "src/assets/${ROMID}")
set(GENERATED_DIR "src/generated/${ROMID}")

set(ENV{ROMID} ${ROMID})

# Build folder postfix

set(BUILD_ID "${VERSION_ROMID}-${VERSION_TARGET}")

# Compiler defs

add_definitions(
  -DMATCHING=0
  -DPIRACYCHECKS=0
  -DROM_SIZE=32
  -DAVOID_UB=1
  -D_LANGUAGE_C=1
  -DPAL=${VERSION_IS_PAL}
  -DDPRINT=${DPRINT}
  -DVERSION=${VERSION}
)

if(TARGET_IS_64BIT)
  add_definitions(-DPLATFORM_64BIT=1)
endif()

# Compiler flags

# Include directories
include_directories("${CMAKE_SOURCE_DIR}/include")
include_directories("${CMAKE_SOURCE_DIR}/include/PR")
include_directories("${CMAKE_SOURCE_DIR}/src/include")
include_directories("${CMAKE_BINARY_DIR}/src/generated/${ROMID}")
include_directories("${CMAKE_SOURCE_DIR}/src/lib/ultra/audio")
include_directories("${CMAKE_SOURCE_DIR}/port/include")
include_directories("${CMAKE_SOURCE_DIR}/port/lua")
include_directories("${CMAKE_BINARY_DIR}/port/include")
include_directories("${CMAKE_SOURCE_DIR}/port/third_party/imgui")
include_directories("${CMAKE_SOURCE_DIR}/port/third_party/imgui/backends")

# Set Release optimization to -Og until I fix the -O2 issues
if(NOT (CMAKE_BUILD_TYPE STREQUAL "Debug"))
  add_compile_options(-Og)
endif()

# Common CFLAGS
add_compile_options(
  -fno-inline-functions
  -fno-strict-aliasing
  -funsigned-char
  -fwrapv
  -include "versioninfo.h"
)

# Common warning flags
add_compile_options(
  -Wall
  -Wno-address
  -Wno-int-in-bool-context
  -Wno-misleading-indentation
  -Wno-missing-braces
  -Wno-multichar
  -Wno-tautological-compare
  -Wno-unused-but-set-variable
  -Wno-unused-value
  -Wno-unused-variable
  -fdiagnostics-color=always
)

# C specific flags
add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wno-pointer-sign>)

# C++ specific flags
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-function>)

# Platform-specific flags
if (APPLE)
  # Make it look for libs in the executable folder manually since CMake's rpath system is wack
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rpath @executable_path")
else()
  # This warning is not recognized by XCode/Clang for some reason.
  add_compile_options(-Wno-format-truncation)
endif()

# Dependencies

set(LIBS )

# SDL2
find_package(SDL2 REQUIRED)
if(NINTENDO_SWITCH)
  if(NOT SDL2_LIBRARY OR NOT SDL2_INCLUDE_DIR)
    # sometimes FindSDL2 just wants us to suffer and does not set these
    set(SDL2_LIBRARY "SDL2")
    set(SDL2_INCLUDE_DIR "${DEVKITPRO}/portlibs/switch/include/SDL2")
  endif()
endif()
include_directories(SYSTEM "${SDL2_INCLUDE_DIR}")
list(APPEND LIBS "${SDL2_LIBRARY}")

# ZLib
find_package(ZLIB REQUIRED)
if(ZLIB_INCLUDE_DIR)
  include_directories(SYSTEM "${ZLIB_INCLUDE_DIR}")
endif()
list(APPEND LIBS "${ZLIB_LIBRARY}")

# GL
if(NOT GL_LIBRARY)
  if(WIN32)
    set(GL_LIBRARY opengl32)
  elseif(NINTENDO_SWITCH)
    set(GL_LIBRARY EGL glapi drm_nouveau)
  elseif(APPLE)
    set(GL_LIBRARY "-framework OpenGL")
  else()
    set(GL_LIBRARY GL)
  endif()
endif()
list(APPEND LIBS "${GL_LIBRARY}")

# Platform-specific extra libs
if(NOT EXTRA_LIBRARIES)
  if(WIN32)
    set(EXTRA_LIBRARIES dbghelp -static-libstdc++)
  elseif(NINTENDO_SWITCH)
    set(EXTRA_LIBRARIES stdc++ pthread)
  else()
    set(EXTRA_LIBRARIES stdc++ m dl)
  endif()
endif()
list(APPEND LIBS "${EXTRA_LIBRARIES}")

# Sources
#
# CONFIGURE_DEPENDS on every glob: without it the file list is fixed at the
# moment the build dir was configured, so a source file added afterwards is
# silently left out and only shows up as undefined symbols at link. Costs a
# stat of the globbed directories per build; needs cmake 3.12 and works on the
# Makefile and Ninja generators, which is all this project uses.

file(GLOB_RECURSE SRC_PORT CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/port/*.c")
file(GLOB_RECURSE SRC_PORT_CXX CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/port/*.cpp")

# The vendored Lua runtime is built as its own library (pd_lua, below) so its
# sources are not compiled with the game's flags. SRC_PORT is GLOB_RECURSE, so
# without this filter port/lua/*.c would land in the pd target.
list(FILTER SRC_PORT EXCLUDE REGEX "/port/lua/")
list(FILTER SRC_PORT_CXX EXCLUDE REGEX "/port/third_party/imgui/")
list(APPEND SRC_PORT_CXX
  port/third_party/imgui/imgui.cpp
  port/third_party/imgui/imgui_draw.cpp
  port/third_party/imgui/imgui_tables.cpp
  port/third_party/imgui/imgui_widgets.cpp
  port/third_party/imgui/backends/imgui_impl_sdl2.cpp
)

file(GLOB_RECURSE SRC_GAME CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/game/*.c")

file(GLOB SRC_LIB_MP3 CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/lib/mp3/*.c")
file(GLOB SRC_LIB_NAUDIO CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/lib/naudio/*.c")
file(GLOB SRC_LIB_ULTRA_AUDIO CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/lib/ultra/audio/*.c")
file(GLOB SRC_LIB_GU CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/lib/ultra/gu/*.c")
set(SRC_LIB
  src/lib/ultra/io/vimodentsclan1.c
  src/lib/ultra/io/vimodepallan1.c
  src/lib/ultra/io/vimodempallan1.c
  src/lib/ultra/io/vitbl.c
  src/lib/ailist.c
  src/lib/anim.c
  src/lib/args.c
  src/lib/audiodma.c
  src/lib/audiomgr.c
  src/lib/base.c
  src/lib/collision.c
  src/lib/debughud.c
  src/lib/dma.c
  src/lib/fault.c
  src/lib/joy.c
  src/lib/lib_2f490_c.c
  src/lib/lib_2fc60.c
  src/lib/lib_3e3e0.c
  src/lib/lib_04f60nb.c
  src/lib/lib_17ce0.c
  src/lib/lib_034d0.c
  src/lib/lib_39c80.c
  src/lib/lib_15850.c
  src/lib/mema.c
  src/lib/memp.c
  src/lib/model.c
  src/lib/modelasm_c.c
  src/lib/mp3.c
  src/lib/mtx_c.c
  src/lib/mtx.c
  src/lib/music.c
  src/lib/path.c
  src/lib/profile.c
  src/lib/rdp.c
  src/lib/rmon.c
  src/lib/rng_c.c
  src/lib/rzip_c.c
  src/lib/snd.c
  src/lib/sndcue.c
  src/lib/speaker.c
  src/lib/varsinit.c
  src/lib/vi.c
  src/textureconfig.c
)
list(PREPEND SRC_LIB "${CMAKE_SOURCE_DIR}")

set(SRC
  ${SRC_GAME}
  ${SRC_PORT}
  ${SRC_PORT_CXX}
  ${SRC_LIB_MP3}
  ${SRC_LIB_NAUDIO}
  ${SRC_LIB_ULTRA_AUDIO}
  ${SRC_LIB_GU}
  ${SRC_LIB}
)

if(WIN32)
  list(APPEND SRC "${CMAKE_SOURCE_DIR}/dist/windows/icon.rc")
endif()

# Asset files

generate_asset_headers("lang/" "${CMAKE_SOURCE_DIR}/tools/assetmgr/mklang" "en" JSON_HEADERS)
generate_asset_headers("pads/" "${CMAKE_SOURCE_DIR}/tools/assetmgr/mkpads" "" JSON_HEADERS)
generate_asset_headers("tiles/" "${CMAKE_SOURCE_DIR}/tools/assetmgr/mktiles" "" JSON_HEADERS)
generate_asset_headers("animations.json" "${CMAKE_SOURCE_DIR}/tools/assetmgr/mkanims" "" JSON_HEADERS)
generate_asset_headers("sequences.json" "${CMAKE_SOURCE_DIR}/tools/assetmgr/mksequences" "" JSON_HEADERS)
generate_asset_headers("textures.json" "${CMAKE_SOURCE_DIR}/tools/assetmgr/mktextures" "" JSON_HEADERS)

add_custom_target(pd_headers ALL DEPENDS ${JSON_HEADERS})

# Generated AI scripting artifacts: docs/aicommands.md (command reference) and
# scripts/ai.lua (Lua helper library) come from the engine source
# (src/include/commands.h + src/game/chrai.c) via tools/gen_aicommands.py so
# they cannot drift. Regenerated at build time when those inputs change; with
# no Python interpreter the committed copies are used. Wiring as in Kai.
find_program(PYTHON3_EXECUTABLE NAMES python3 python)
if(PYTHON3_EXECUTABLE)
  set(AISCRIPT_GEN "${CMAKE_SOURCE_DIR}/tools/gen_aicommands.py")
  set(AISCRIPT_OUTPUTS
    "${CMAKE_SOURCE_DIR}/docs/aicommands.md"
    "${CMAKE_SOURCE_DIR}/scripts/ai.lua")
  add_custom_command(
    OUTPUT ${AISCRIPT_OUTPUTS}
    COMMAND "${PYTHON3_EXECUTABLE}" "${AISCRIPT_GEN}"
    DEPENDS "${AISCRIPT_GEN}"
            "${CMAKE_SOURCE_DIR}/src/include/commands.h"
            "${CMAKE_SOURCE_DIR}/src/game/chrai.c"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Generating AI command reference + Lua helper library"
    VERBATIM)
  add_custom_target(pd_aiscripts ALL DEPENDS ${AISCRIPT_OUTPUTS})

  # The debugger's ailist names (port/src/imgui_ailistnames.h) come from the
  # setups' ailists[] tables and AILIST_* defines plus g_GlobalAilists, via
  # tools/mkailistnames.py. Same arrangement: regenerated when a setup or the
  # global lists change, the committed copy used without Python. A new setup
  # file is picked up at the next configure (the glob is evaluated then).
  set(AILISTNAMES_GEN "${CMAKE_SOURCE_DIR}/tools/mkailistnames.py")
  set(AILISTNAMES_OUTPUT "${CMAKE_SOURCE_DIR}/port/src/imgui_ailistnames.h")
  file(GLOB AILISTNAMES_SETUPS "${CMAKE_SOURCE_DIR}/src/setups/*.c")
  add_custom_command(
    OUTPUT "${AILISTNAMES_OUTPUT}"
    COMMAND "${PYTHON3_EXECUTABLE}" "${AILISTNAMES_GEN}" "${CMAKE_SOURCE_DIR}"
    DEPENDS "${AILISTNAMES_GEN}"
            "${CMAKE_SOURCE_DIR}/src/game/gailists.c"
            "${CMAKE_SOURCE_DIR}/src/include/constants.h"
            ${AILISTNAMES_SETUPS}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Generating debugger ailist names"
    VERBATIM)
  add_custom_target(pd_ailistnames ALL DEPENDS "${AILISTNAMES_OUTPUT}")
else()
  message(STATUS "python not found: using committed docs/aicommands.md + scripts/ai.lua + port/src/imgui_ailistnames.h")
endif()

# Stage setups. A modified setup ships as a per-file xdelta of its inflated
# bytes against the vanilla file (basedir/data/mods/mod_fojo/patches/
# U<name>Z.xdelta), never as the setup itself. tools/mksetups --patches
# compiles src/setups/<name>.c for MIPS (clang + lld + llvm-objcopy, no MIPS
# gcc, no N64 tree), diffs it against the vanilla file inflated out of the base
# ROM with xdelta3, and upserts the manifest row; tools/mkfiletable then
# rewrites filetable.dat, proving every patch applies as it goes. Same
# arrangement as the generated headers above: rebuilt when a setup source or a
# tool changes, written straight into the basedir so the committed copies are
# the shipped ones, and a machine without the tools builds the game against
# the committed copies and says so.
#
# Which setups are built is decided by which patches the mod ships: every
# patches/U<name>Z.xdelta is one output, its source src/setups/<name>.c. A
# setup that is not modified has no patch and is not built - it is already in
# the ROM. Ship a new one by running `tools/mksetups --patches --mod-dir
# basedir/data/mods/mod_fojo <name>` once and reconfiguring; the glob is read
# at configure. One mksetups run for the whole set, not one per file: it
# regenerates src/generated/<romid> on every run and parallel runs would race.
#
# The ROMs come from this tree only - basedir/data or basedir/data/roms, the
# same folders the game mounts from - never from PD_ROMFILE or wherever the
# installed game keeps its copy; a build should not depend on anything
# outside its own checkout. The base ROM is the ntsc one, and every ROM a
# manifest names (the jpn one for Mikado's byName rows) has to be there too.
# No base ROM, no target.
set(FOJO_MOD_DIR "${CMAKE_SOURCE_DIR}/basedir/data/mods/mod_fojo")
file(GLOB FOJO_SETUP_PATCHES "${FOJO_MOD_DIR}/patches/Usetup*Z.xdelta" "${FOJO_MOD_DIR}/patches/Ump_setup*Z.xdelta")
find_program(FOJO_CLANG NAMES clang HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin)
find_program(FOJO_LLD NAMES ld.lld HINTS /opt/homebrew/opt/lld/bin /usr/local/opt/lld/bin)
find_program(FOJO_OBJCOPY NAMES llvm-objcopy HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin)
find_program(FOJO_XDELTA3 NAMES xdelta3)
set(FOJO_ROM_DIR "${CMAKE_SOURCE_DIR}/basedir/data")
set(FOJO_BASE_ROM "")
foreach(c "${FOJO_ROM_DIR}/pd.ntsc-final.z64" "${FOJO_ROM_DIR}/roms/pd.ntsc-final.z64")
  if(EXISTS "${c}")
    set(FOJO_BASE_ROM "${c}")
    break()
  endif()
endforeach()

# mkfiletable as a host tool, built the way its Makefile builds it: the engine's
# own rompatch decoder, parson for the json, zlib for the 1173 segments.
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
  add_executable(mkfiletable
    "${CMAKE_SOURCE_DIR}/tools/mkfiletable/mkfiletable.c"
    "${CMAKE_SOURCE_DIR}/tools/mkfiletable/pdft_write.c"
    "${CMAKE_SOURCE_DIR}/port/src/rompatch.c"
    "${CMAKE_SOURCE_DIR}/tools/mkfiletable/vendor/parson/parson.c")
  target_include_directories(mkfiletable PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/mkfiletable/vendor/parson"
    "${CMAKE_SOURCE_DIR}/port/include"
    "${CMAKE_SOURCE_DIR}/include")
  target_compile_definitions(mkfiletable PRIVATE _LANGUAGE_C)
  target_compile_options(mkfiletable PRIVATE -std=c99 -Wall)
  target_link_libraries(mkfiletable PRIVATE ZLIB::ZLIB)
  set_target_properties(mkfiletable PROPERTIES EXCLUDE_FROM_ALL FALSE)
endif()

if(PYTHON3_EXECUTABLE AND FOJO_CLANG AND FOJO_LLD AND FOJO_OBJCOPY AND FOJO_XDELTA3
   AND FOJO_BASE_ROM AND FOJO_SETUP_PATCHES AND TARGET mkfiletable)
  set(FOJO_SETUP_NAMES "")
  set(FOJO_SETUP_SOURCES "")
  foreach(out ${FOJO_SETUP_PATCHES})
    get_filename_component(fn "${out}" NAME)
    string(REGEX REPLACE "^U(.*)Z\\.xdelta$" "\\1" name "${fn}")
    list(APPEND FOJO_SETUP_NAMES "${name}")
    list(APPEND FOJO_SETUP_SOURCES "${CMAKE_SOURCE_DIR}/src/setups/${name}.c")
  endforeach()
  get_filename_component(FOJO_LLVM_BIN "${FOJO_CLANG}" DIRECTORY)
  get_filename_component(FOJO_LLD_BIN "${FOJO_LLD}" DIRECTORY)
  # mkfiletable tries <dir>/roms/<file> then <dir>/<file>, as the game does.
  set(FOJO_ROM_DIRS --rom-dir "${FOJO_ROM_DIR}")
  add_custom_command(
    OUTPUT ${FOJO_SETUP_PATCHES}
    COMMAND "${CMAKE_COMMAND}" -E env "LLVM_BIN=${FOJO_LLVM_BIN}" "LLD_BIN=${FOJO_LLD_BIN}"
            "${PYTHON3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/mksetups"
            --patches --mod-dir "${FOJO_MOD_DIR}" --rom "${FOJO_BASE_ROM}" --xdelta3 "${FOJO_XDELTA3}"
            ${FOJO_SETUP_NAMES}
    DEPENDS "${CMAKE_SOURCE_DIR}/tools/mksetups"
            "${CMAKE_SOURCE_DIR}/ld/zero-lld.ld"
            ${FOJO_SETUP_SOURCES}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Building stage setup patches into basedir/data/mods/mod_fojo/patches"
    VERBATIM)
  add_custom_command(
    OUTPUT "${FOJO_MOD_DIR}/filetable.dat"
    COMMAND mkfiletable mod_fojo --workspace "${FOJO_MOD_DIR}" --output "${FOJO_MOD_DIR}"
            ${FOJO_ROM_DIRS} --rom-dir "${FOJO_MOD_DIR}" --allow-orphans
    DEPENDS mkfiletable ${FOJO_SETUP_PATCHES} "${FOJO_MOD_DIR}/mod_fojo_filetable.json"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Rebuilding mod_fojo/filetable.dat"
    VERBATIM)
  add_custom_target(pd_setups ALL DEPENDS ${FOJO_SETUP_PATCHES} "${FOJO_MOD_DIR}/filetable.dat")
else()
  message(STATUS "setups: not rebuilding - needs python, clang, ld.lld, llvm-objcopy, xdelta3, zlib and a base ROM (basedir/data/pd.ntsc-final.z64 or basedir/data/roms/, and the jpn rom beside it); using the committed basedir/data/mods/mod_fojo/patches + filetable.dat")
endif()

# Game target

set(BIN_NAME "pd${BIN_REGION}.${TARGET_ARCH}")
add_executable(pd ${SRC})
add_dependencies(pd pd_headers versioninfo)
if(PYTHON3_EXECUTABLE)
  add_dependencies(pd pd_ailistnames)
endif()
if(TARGET pd_setups)
  add_dependencies(pd pd_setups)
endif()
# Vendored Lua 5.4 (MIT, lua.org/PUC-Rio), carried for the KAI script layer.
# Nothing calls it yet - this builds and links it so the intake that does can
# land on its own. lua.c / luac.c / onelua.c are the standalone interpreter and
# compiler mains and would each bring a second main().
file(GLOB SRC_LUA "${CMAKE_SOURCE_DIR}/port/lua/*.c")
list(FILTER SRC_LUA EXCLUDE REGEX "/(lua|luac|onelua)\\.c$")
add_library(pd_lua STATIC ${SRC_LUA})
target_include_directories(pd_lua PUBLIC "${CMAKE_SOURCE_DIR}/port/lua")

target_link_libraries(pd ${LIBS} pd_lua)
set_target_properties(pd PROPERTIES OUTPUT_NAME "${BIN_NAME}")
if(WIN32)
  set_target_properties(pd PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# Additional commands for specific platforms.

if(NINTENDO_SWITCH)
  add_custom_command(TARGET pd POST_BUILD
    COMMAND nacptool --create "Perfect Dark" "fgsfds and contributors" "${VERSION_ROMID}/${VERSION_BRANCH}/${VERSION_HASH}" "${BIN_NAME}.nacp"
    COMMAND aarch64-none-elf-strip -o ${CMAKE_BINARY_DIR}/${BIN_NAME}.stripped.elf ${CMAKE_BINARY_DIR}/${BIN_NAME}.elf
    COMMAND elf2nro ${CMAKE_BINARY_DIR}/${BIN_NAME}.stripped.elf ${CMAKE_BINARY_DIR}/${BIN_NAME}.nro --icon=${CMAKE_SOURCE_DIR}/dist/nswitch/icon.jpg --nacp=${BIN_NAME}.nacp
  )
endif()
