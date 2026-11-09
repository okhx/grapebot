cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 23)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
if (NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "macOS architecture")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

set(CMAKE_EXPORT_COMPILE_COMMANDS true)


project(grape VERSION 1.0.1)
include(CTest)

if (WIN32 AND NOT GRAPE_PC)
    message(FATAL_ERROR "Configure Windows builds from the pc directory.")
endif()

if (APPLE)
    enable_language(OBJCXX)
endif()

if (ANDROID OR GEODE_TARGET_PLATFORM STREQUAL "iOS")
    set(GRAPE_MOBILE ON)
else()
    set(GRAPE_MOBILE OFF)
endif()

set(ARC_FEATURE_FULL ON CACHE BOOL "" FORCE)

file(GLOB IMGUI_VENDOR_CANDIDATES LIST_DIRECTORIES true
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/*/lib/imgui_lib")
list(LENGTH IMGUI_VENDOR_CANDIDATES IMGUI_VENDOR_COUNT)
if (NOT IMGUI_VENDOR_COUNT EQUAL 1)
    message(FATAL_ERROR "Expected exactly one vendored imgui_lib directory")
endif()
list(GET IMGUI_VENDOR_CANDIDATES 0 IMGUI_VENDOR_DIR)

add_library(${PROJECT_NAME} SHARED
    src/main.cpp

    src/engine/engine.cpp
    src/engine/timeline.cpp
    src/engine/clock.cpp
    src/engine/tick_patch.cpp

    src/assist/autoclicker.cpp
    src/assist/pathfinder.cpp
    src/assist/cps.cpp
    src/assist/hitboxes.cpp
    src/features/death_guard.cpp
    src/features/intentional_death.cpp
    src/features/layout.cpp
    src/features/mirror.cpp
    src/features/noclip.cpp

    src/hooks/CCActionManager.cpp
    src/hooks/CCDirector.cpp
    src/hooks/CCScheduler.cpp
    src/hooks/EndLevelLayer.cpp
    src/hooks/EnhancedGameObject.cpp
    src/hooks/FMODAudioEngine.cpp
    src/hooks/GJBaseGameLayer.cpp
    src/hooks/LevelEditorLayer.cpp
    src/hooks/PauseLayer.cpp
    src/hooks/PlayLayer.cpp
    src/hooks/PlayerObject.cpp
    src/hooks/RandTriggerGameObject.cpp
    src/ui/mobile_menu.cpp

    src/vine/importer.cpp
    src/vine/session.cpp

    src/physics/collisions.cpp
    src/physics/gjbasegamelayer.cpp
    src/physics/gravity.cpp
    src/physics/object.cpp
    src/physics/player.cpp

    src/label/builtin_labels.cpp
    src/label/label.cpp
    src/label/variables.cpp

    src/checkpoint/practice.cpp
    src/checkpoint/player_state.cpp

    src/trajectory/trajectory.cpp

    src/render/renderer.cpp
    src/render/dsp.cpp
    src/render/pass.cpp

    src/util/crash_log.cpp
    src/util/input_trace.cpp

    src/ui/touch_overlay.cpp
)

if (NOT GRAPE_MOBILE)
    target_sources(${PROJECT_NAME} PRIVATE
        src/render/core/av_api.cpp
        src/render/core/gpu_capture.cpp
        src/render/core/av_output.cpp
        src/render/pc_pipeline.cpp
    )
endif()

if (NOT GRAPE_MOBILE)
    target_sources(${PROJECT_NAME} PRIVATE
        src/ui/manager.cpp
        src/ui/hook.cpp
        src/ui/theme.cpp
        ${IMGUI_VENDOR_DIR}/imgui.cpp
        ${IMGUI_VENDOR_DIR}/imgui_draw.cpp
        ${IMGUI_VENDOR_DIR}/imgui_tables.cpp
        ${IMGUI_VENDOR_DIR}/imgui_widgets.cpp
        ${IMGUI_VENDOR_DIR}/misc/cpp/imgui_stdlib.cpp
        ${IMGUI_VENDOR_DIR}/backends/imgui_impl_opengl3.cpp
    )
endif()

if (GEODE_TARGET_PLATFORM STREQUAL "iOS")
    target_sources(${PROJECT_NAME} PRIVATE src/render/ios_video_writer.mm)
    set_source_files_properties(src/render/ios_video_writer.mm PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc"
    )
    find_library(AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
    find_library(COREMEDIA_FRAMEWORK CoreMedia REQUIRED)
    find_library(COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
    target_link_libraries(${PROJECT_NAME}
        ${AVFOUNDATION_FRAMEWORK}
        ${COREMEDIA_FRAMEWORK}
        ${COREVIDEO_FRAMEWORK}
    )
endif()

if (APPLE AND NOT GEODE_TARGET_PLATFORM STREQUAL "iOS")
    target_sources(${PROJECT_NAME} PRIVATE
        src/ui/macos_input.mm
        ${IMGUI_VENDOR_DIR}/backends/imgui_impl_osx.mm
        ${IMGUI_VENDOR_DIR}/backends/imgui_impl_opengl2.cpp
    )
    set_source_files_properties(
        src/ui/macos_input.mm
        ${IMGUI_VENDOR_DIR}/backends/imgui_impl_osx.mm
        PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc"
        SKIP_PRECOMPILE_HEADERS ON
    )
    set_source_files_properties(${IMGUI_VENDOR_DIR}/backends/imgui_impl_opengl2.cpp
        PROPERTIES COMPILE_OPTIONS
        "-Wno-error=unused-variable;-Wno-error=unused-parameter")
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
    find_library(COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
    find_library(GAMECONTROLLER_FRAMEWORK GameController REQUIRED)
    target_link_libraries(${PROJECT_NAME}
        ${COCOA_FRAMEWORK}
        ${IOKIT_FRAMEWORK}
        ${COREVIDEO_FRAMEWORK}
        ${GAMECONTROLLER_FRAMEWORK}
    )
    target_link_options(${PROJECT_NAME} PRIVATE "-Wl,-undefined,dynamic_lookup")
endif()

if (WIN32)
    target_sources(${PROJECT_NAME} PRIVATE
        src/hooks/VideoOptionsLayer.cpp
        ${IMGUI_VENDOR_DIR}/backends/imgui_impl_win32.cpp
        src/util/midhook.cpp
    )
    if (GRAPE_PC)
        target_sources(${PROJECT_NAME} PRIVATE
            ${GRAPE_PC_DIR}/src/imgui_menu.cpp
            ${GRAPE_PC_DIR}/src/skeet_menu.cpp
            ${GRAPE_PC_DIR}/src/skrt_ui.cpp
        )
        target_include_directories(${PROJECT_NAME} PRIVATE
            ${GRAPE_PC_DIR}/src
        )
        target_compile_definitions(${PROJECT_NAME} PRIVATE
            GRAPE_PC=1
        )
        set_property(TARGET ${PROJECT_NAME} PROPERTY
            INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        target_compile_options(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Release>:/Gw>
            $<$<CONFIG:Release>:/Gy>
            $<$<CONFIG:Release>:/GF>
            $<$<CONFIG:Release>:/Oi>
            $<$<CONFIG:Release>:/Ob3>
            $<$<CONFIG:Release>:/Zc:inline>
            $<$<CONFIG:Release>:/GS>
            $<$<CONFIG:Release>:/guard:cf>
            $<$<CONFIG:Release>:/sdl>
            $<$<CONFIG:Release>:/Brepro>
        )
        target_link_options(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Release>:/OPT:REF>
            $<$<CONFIG:Release>:/OPT:ICF>
            $<$<CONFIG:Release>:/DYNAMICBASE>
            $<$<CONFIG:Release>:/HIGHENTROPYVA>
            $<$<CONFIG:Release>:/NXCOMPAT>
            $<$<CONFIG:Release>:/GUARD:CF>
            $<$<CONFIG:Release>:/CETCOMPAT>
            $<$<CONFIG:Release>:/INCREMENTAL:NO>
        )
    endif()
endif()

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-dollar-in-identifier-extension)
endif()
target_include_directories(${PROJECT_NAME} PRIVATE
    src include
    src/oxorany
    ${IMGUI_VENDOR_DIR}
    ${IMGUI_VENDOR_DIR}/backends
)

target_sources(${PROJECT_NAME} PRIVATE src/oxorany/oxorany.cpp)
set_source_files_properties(src/oxorany/oxorany.cpp PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON
    COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/w>$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>"
)

if (NOT GRAPE_MOBILE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        IMGUI_IMPL_OPENGL_LOADER_CUSTOM
    )
endif()


if (GRAPE_MOBILE OR GRAPE_PC OR APPLE)
    include(FetchContent)
    FetchContent_Declare(grape_lua
        URL https://www.lua.org/ftp/lua-5.4.8.tar.gz
        URL_HASH SHA256=4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_GetProperties(grape_lua)
    if (NOT grape_lua_POPULATED)
        FetchContent_Populate(grape_lua)
    endif()
    set(LUA_SRC "${grape_lua_SOURCE_DIR}/src")
    add_library(grape_lua STATIC
        ${LUA_SRC}/lapi.c ${LUA_SRC}/lauxlib.c ${LUA_SRC}/lbaselib.c
        ${LUA_SRC}/lcode.c ${LUA_SRC}/lcorolib.c ${LUA_SRC}/lctype.c
        ${LUA_SRC}/ldebug.c ${LUA_SRC}/ldo.c ${LUA_SRC}/ldump.c
        ${LUA_SRC}/lfunc.c ${LUA_SRC}/lgc.c ${LUA_SRC}/llex.c
        ${LUA_SRC}/lmathlib.c ${LUA_SRC}/lmem.c ${LUA_SRC}/lobject.c
        ${LUA_SRC}/lopcodes.c ${LUA_SRC}/lparser.c ${LUA_SRC}/lstate.c
        ${LUA_SRC}/lstring.c ${LUA_SRC}/lstrlib.c ${LUA_SRC}/ltable.c
        ${LUA_SRC}/ltablib.c ${LUA_SRC}/ltm.c ${LUA_SRC}/lundump.c
        ${LUA_SRC}/lutf8lib.c ${LUA_SRC}/lvm.c ${LUA_SRC}/lzio.c
    )
    target_include_directories(grape_lua PUBLIC "${LUA_SRC}")
    target_sources(${PROJECT_NAME} PRIVATE src/scripting/script_engine.cpp)
    target_link_libraries(${PROJECT_NAME} grape_lua)
endif()

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

if (ANDROID)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-includetype_traits>")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

file(GLOB GRAPE_PHYSICS_HEADERS
    "${CMAKE_BINARY_DIR}/bindings/bindings/Geode/binding*/GameObjectPhysics.hpp")
foreach(grape_physics_header IN LISTS GRAPE_PHYSICS_HEADERS)
    file(READ "${grape_physics_header}" grape_physics_contents)
    if (NOT grape_physics_contents MATCHES "GRAPE_PHYSICS_COPY_SHIM_V2")
        string(REPLACE "#pragma once"
            "#pragma once\n#include <cstring>"
            grape_physics_contents "${grape_physics_contents}")
        string(REPLACE "class GameObjectPhysics {\npublic:"
            "class GameObjectPhysics {\npublic:\n    // GRAPE_PHYSICS_COPY_SHIM_V2: see the root CMakeLists.txt\n    GameObjectPhysics(GameObjectPhysics const& other) { std::memcpy(this, &other, sizeof(*this)); }\n    GameObjectPhysics& operator=(GameObjectPhysics const& other) { std::memcpy(this, &other, sizeof(*this)); return *this; }"
            grape_physics_contents "${grape_physics_contents}")
        file(WRITE "${grape_physics_header}" "${grape_physics_contents}")
    endif()
endforeach()

CPMAddPackage("gh:stephenberry/glaze#3bfd53e70db09247f3726a65abd7c7c2414a1601")
target_link_libraries(${PROJECT_NAME} glaze::glaze)

if (WIN32)
    CPMAddPackage("gh:zyantific/zydis#bffbb610cfea643b98e87658b9058382f7522807")
    target_link_libraries(${PROJECT_NAME} Zydis)

    CPMAddPackage("gh:cursey/safetyhook#f44cc070a8340f2f26649553c49533475417304d")
    target_link_libraries(${PROJECT_NAME} safetyhook)
endif()

if (WIN32)
    target_link_libraries(${PROJECT_NAME} dbghelp pdh)
endif()
target_compile_options(GeodeBindings PRIVATE -Wno-overloaded-virtual -Wno-unused-variable)

if(GRAPE_PC)
    if(EXISTS "${GRAPE_PC_DIR}/license-public-key.hex")
        file(READ "${GRAPE_PC_DIR}/license-public-key.hex" GRAPE_LICENSE_PUBKEY)
    endif()
    string(STRIP "${GRAPE_LICENSE_PUBKEY}" GRAPE_LICENSE_PUBKEY)
    string(TOLOWER "${GRAPE_LICENSE_PUBKEY}" GRAPE_LICENSE_PUBKEY)
    if(NOT GRAPE_LICENSE_PUBKEY MATCHES "^04[0-9a-f]+$")
        message(FATAL_ERROR
            "No licence public key. Run `node pc/server/scripts/license-keys.mjs`, put the "
            "printed private key in the server's LICENSE_SIGNING_KEY, and rebuild.")
    endif()
    string(LENGTH "${GRAPE_LICENSE_PUBKEY}" pubkey_length)
    if(NOT pubkey_length EQUAL 130)
        message(FATAL_ERROR "pc/license-public-key.hex must hold a 65-byte uncompressed P-256 point")
    endif()
    set(protection_dir "${CMAKE_CURRENT_BINARY_DIR}/protected-source")
    file(MAKE_DIRECTORY "${protection_dir}")
    configure_file("${GRAPE_PC_DIR}/src/protection.cpp.in"
                   "${protection_dir}/protection.cpp" @ONLY)
    target_sources(${PROJECT_NAME} PRIVATE
        "${protection_dir}/protection.cpp"
    )
    target_include_directories(${PROJECT_NAME} PRIVATE
                               "${CMAKE_CURRENT_SOURCE_DIR}/src")
    set_source_files_properties(
        "${protection_dir}/protection.cpp"
        PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON
        COMPILE_OPTIONS "-w")

    if(GRAPE_VMPROTECT_SDK)
        find_file(GRAPE_VMPROTECT_HEADER VMProtectSDK.h
            PATHS "${GRAPE_VMPROTECT_SDK}" "${GRAPE_VMPROTECT_SDK}/include"
                  "${GRAPE_VMPROTECT_SDK}/Include/C" "${GRAPE_VMPROTECT_SDK}/../Include/C"
            NO_DEFAULT_PATH)
        find_library(GRAPE_VMPROTECT_LIB
            NAMES VMProtectSDK64 VMProtectSDK
            PATHS "${GRAPE_VMPROTECT_SDK}" "${GRAPE_VMPROTECT_SDK}/lib"
                  "${GRAPE_VMPROTECT_SDK}/lib/Windows" "${GRAPE_VMPROTECT_SDK}/Lib"
                  "${GRAPE_VMPROTECT_SDK}/Lib/Windows" "${GRAPE_VMPROTECT_SDK}/Windows"
            NO_DEFAULT_PATH)
        if(NOT GRAPE_VMPROTECT_HEADER OR NOT GRAPE_VMPROTECT_LIB)
            message(FATAL_ERROR
                "GRAPE_VMPROTECT_SDK is set but VMProtectSDK.h or its 64-bit "
                "import library was not found under it")
        endif()
        get_filename_component(vmprotect_include "${GRAPE_VMPROTECT_HEADER}" DIRECTORY)
        target_include_directories(${PROJECT_NAME} PRIVATE "${vmprotect_include}")
        target_link_libraries(${PROJECT_NAME} "${GRAPE_VMPROTECT_LIB}")
        target_compile_definitions(${PROJECT_NAME} PRIVATE GRAPE_VMPROTECT=1)
        message(STATUS "VMProtect markers enabled (SDK: ${GRAPE_VMPROTECT_SDK})")

        if(GRAPE_VMPROTECT_EXE)
            separate_arguments(vmprotect_launch UNIX_COMMAND "${GRAPE_VMPROTECT_LAUNCH}")
            separate_arguments(vmprotect_args UNIX_COMMAND "${GRAPE_VMPROTECT_ARGS}")

            if(NOT GRAPE_VMPROTECT_PROJECT AND EXISTS "${GRAPE_PC_DIR}/vmprotect/grape.vmp")
                set(GRAPE_VMPROTECT_PROJECT "${GRAPE_PC_DIR}/vmprotect/grape.vmp")
            endif()
            set(vmprotect_project_args "")
            if(GRAPE_VMPROTECT_PROJECT)
                if(NOT EXISTS "${GRAPE_VMPROTECT_PROJECT}")
                    message(FATAL_ERROR "GRAPE_VMPROTECT_PROJECT does not exist: ${GRAPE_VMPROTECT_PROJECT}")
                endif()
                set(vmprotect_project_args -pf "${GRAPE_VMPROTECT_PROJECT}")
                message(STATUS "VMProtect integrity project: ${GRAPE_VMPROTECT_PROJECT}")
            else()
                message(WARNING
                    "VMProtect will virtualize the marked regions, but no "
                    "integrity project (GRAPE_VMPROTECT_PROJECT or "
                    "pc/vmprotect/grape.vmp) was given, so File Memory "
                    "Protection and Import Protection are OFF. See "
                    "pc/vmprotect/README.md.")
            endif()
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                        "$<TARGET_FILE:${PROJECT_NAME}>"
                        "$<TARGET_FILE:${PROJECT_NAME}>.prelink"
                COMMENT "Snapshotting pre-VMProtect $<TARGET_FILE_NAME:${PROJECT_NAME}>"
                VERBATIM)
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${vmprotect_launch} "${GRAPE_VMPROTECT_EXE}"
                        "$<TARGET_FILE:${PROJECT_NAME}>"
                        "$<TARGET_FILE:${PROJECT_NAME}>"
                        ${vmprotect_project_args}
                        ${vmprotect_args}
                COMMENT "VMProtect: virtualizing marked regions in $<TARGET_FILE_NAME:${PROJECT_NAME}>"
                VERBATIM)
            message(STATUS "VMProtect protector: ${GRAPE_VMPROTECT_LAUNCH} ${GRAPE_VMPROTECT_EXE}")
        else()
            message(WARNING
                "VMProtect markers are compiled in, but GRAPE_VMPROTECT_EXE is "
                "not set: the built DLL is NOT virtualized. Set it to the "
                "console protector, or run the protector over the DLL by hand.")
        endif()
    endif()
endif()

setup_geode_mod(${PROJECT_NAME})

if (APPLE OR ANDROID)
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wno-error=dollar-in-identifier-extension
    )
endif()

if (APPLE)
    set_source_files_properties(
        ${IMGUI_VENDOR_DIR}/imgui.cpp
        ${IMGUI_VENDOR_DIR}/imgui_draw.cpp
        ${IMGUI_VENDOR_DIR}/imgui_tables.cpp
        ${IMGUI_VENDOR_DIR}/imgui_widgets.cpp
        ${IMGUI_VENDOR_DIR}/misc/cpp/imgui_stdlib.cpp
        ${IMGUI_VENDOR_DIR}/backends/imgui_impl_opengl3.cpp
        PROPERTIES COMPILE_OPTIONS
        "-Wno-error=unused-variable;-Wno-error=unused-parameter;-include${CMAKE_CURRENT_SOURCE_DIR}/src/render/gl.hpp"
    )
endif()

if (BUILD_TESTING AND NOT ANDROID AND NOT APPLE)
    add_executable(grape_atomic_file_test tests/atomic_file.cpp)
    target_include_directories(grape_atomic_file_test PRIVATE src)
    add_test(NAME grape.atomic_file COMMAND grape_atomic_file_test)

    add_executable(grape_keybind_test tests/keybind.cpp)
    target_include_directories(grape_keybind_test PRIVATE src)
    target_link_libraries(grape_keybind_test PRIVATE glaze::glaze)
    add_test(NAME grape.keybind COMMAND grape_keybind_test)

    add_executable(grape_vine_format_test tests/vine_format.cpp)
    target_include_directories(grape_vine_format_test PRIVATE src)
    add_test(NAME grape.vine_format COMMAND grape_vine_format_test)

    add_executable(grape_foresight_test tests/foresight.cpp)
    target_include_directories(grape_foresight_test PRIVATE src)
    add_test(NAME grape.foresight COMMAND grape_foresight_test)

    add_executable(grape_zstd_test tests/zstd.cpp)
    target_include_directories(grape_zstd_test PRIVATE src)
    add_test(NAME grape.zstd COMMAND grape_zstd_test)

    add_executable(grape_foreign_test tests/foreign.cpp)
    target_include_directories(grape_foreign_test PRIVATE src)
    add_test(NAME grape.foreign COMMAND grape_foreign_test)

    add_executable(grape_macro_size_test tests/macro_size.cpp)
    target_include_directories(grape_macro_size_test PRIVATE src)
    add_test(NAME grape.macro_size COMMAND grape_macro_size_test)

    add_executable(grape_vine_core_test tests/vine_core.cpp)
    target_include_directories(grape_vine_core_test PRIVATE src)
    add_test(NAME grape.vine_core COMMAND grape_vine_core_test)

    add_executable(grape_tickplan_test tests/tickplan.cpp)
    target_include_directories(grape_tickplan_test PRIVATE src)
    add_test(NAME grape.tickplan COMMAND grape_tickplan_test)


    add_executable(grape_rift_test tests/rift.cpp)
    target_include_directories(grape_rift_test PRIVATE src)
    add_test(NAME grape.rift COMMAND grape_rift_test)

    add_executable(grape_pathchoice_test tests/pathchoice.cpp)
    target_include_directories(grape_pathchoice_test PRIVATE src)
    add_test(NAME grape.pathchoice COMMAND grape_pathchoice_test)

    add_executable(grape_gapsteer_test tests/gapsteer.cpp)
    target_include_directories(grape_gapsteer_test PRIVATE src)
    add_test(NAME grape.gapsteer COMMAND grape_gapsteer_test)

    add_executable(grape_ufogap_test tests/ufogap.cpp)
    target_include_directories(grape_ufogap_test PRIVATE src)
    add_test(NAME grape.ufogap COMMAND grape_ufogap_test)

    add_executable(grape_trailsnap_test tests/trailsnap.cpp)
    target_include_directories(grape_trailsnap_test PRIVATE src)
    add_test(NAME grape.trailsnap COMMAND grape_trailsnap_test)

    add_executable(grape_reset_tracker_test tests/reset_tracker.cpp)
    target_include_directories(grape_reset_tracker_test PRIVATE src)
    add_test(NAME grape.reset_tracker COMMAND grape_reset_tracker_test)

    add_executable(grape_death_plan_test tests/death_plan.cpp)
    target_include_directories(grape_death_plan_test PRIVATE src)
    add_test(NAME grape.death_plan COMMAND grape_death_plan_test)

    add_executable(grape_practice_parts_test tests/practice_parts.cpp)
    target_include_directories(grape_practice_parts_test PRIVATE src)
    add_test(NAME grape.practice_parts COMMAND grape_practice_parts_test)

    add_executable(grape_variance_tag_test tests/variance_tag.cpp)
    target_include_directories(grape_variance_tag_test PRIVATE src)
    add_test(NAME grape.variance_tag COMMAND grape_variance_tag_test)

    add_executable(grape_step_budget_test tests/step_budget.cpp)
    target_include_directories(grape_step_budget_test PRIVATE src)
    add_test(NAME grape.step_budget COMMAND grape_step_budget_test)

    add_executable(grape_renderfade_test tests/renderfade.cpp)
    target_include_directories(grape_renderfade_test PRIVATE src)
    add_test(NAME grape.renderfade COMMAND grape_renderfade_test)

    add_executable(grape_notifications_test tests/notifications.cpp)
    target_include_directories(grape_notifications_test PRIVATE src)
    add_test(NAME grape.notifications COMMAND grape_notifications_test)

    add_executable(grape_gltf_test tests/gltf.cpp)
    target_include_directories(grape_gltf_test PRIVATE src pc/src)
    target_link_libraries(grape_gltf_test PRIVATE glaze::glaze)
    add_test(NAME grape.gltf COMMAND grape_gltf_test)

    add_executable(grape_vine_slc_test tests/vine_slc.cpp)
    target_include_directories(grape_vine_slc_test PRIVATE src)
    add_test(NAME grape.vine_slc COMMAND grape_vine_slc_test)

    add_executable(grape_sha3_test tests/sha3.cpp)
    target_include_directories(grape_sha3_test PRIVATE src)
    add_test(NAME grape.sha3 COMMAND grape_sha3_test)
endif()
