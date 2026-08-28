cmake_minimum_required(VERSION 3.21)

option(SABERSTAGE_HOST_TESTS "Build only platform-neutral SaberStage tests" OFF)

if(SABERSTAGE_HOST_TESTS)
    project(SaberStageHostTests LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    set(SABERSTAGE_RAPIDJSON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/includes/rapidjson/include")
    if(NOT EXISTS "${SABERSTAGE_RAPIDJSON_DIR}/rapidjson/document.h")
        set(SABERSTAGE_RAPIDJSON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/includes/beatsaber-hook/shared/rapidjson/include")
    endif()
    if(NOT EXISTS "${SABERSTAGE_RAPIDJSON_DIR}/rapidjson/document.h")
        message(FATAL_ERROR "RapidJSON headers not restored. Run qpm restore first.")
    endif()

    add_library(saberstage_camera_core STATIC
        src/camera/CameraProfile.cpp
        src/camera/FrameDemand.cpp
        src/camera/Math.cpp
        src/camera/MotionPipeline.cpp
        src/camera/MovementScript.cpp
    )
    target_include_directories(saberstage_camera_core PUBLIC include "${SABERSTAGE_RAPIDJSON_DIR}")
    target_compile_definitions(saberstage_camera_core PRIVATE SABERSTAGE_HOST_BUILD=1)

    add_library(saberstage_settings STATIC
        src/settings/SettingsModel.cpp
        src/settings/SettingsService.cpp
    )
    target_include_directories(saberstage_settings PUBLIC include "${SABERSTAGE_RAPIDJSON_DIR}")
    target_compile_definitions(saberstage_settings PRIVATE SABERSTAGE_HOST_BUILD=1)
    target_link_libraries(saberstage_settings PUBLIC saberstage_camera_core)

    enable_testing()
    add_executable(saberstage_settings_tests tests/SettingsTests.cpp)
    target_link_libraries(saberstage_settings_tests PRIVATE saberstage_settings)
    add_test(NAME saberstage.settings COMMAND saberstage_settings_tests)

    add_executable(saberstage_camera_tests tests/CameraTests.cpp)
    target_link_libraries(saberstage_camera_tests PRIVATE saberstage_camera_core)
    add_test(NAME saberstage.camera COMMAND saberstage_camera_tests)

    add_executable(saberstage_recording_state_tests tests/RecordingStateTests.cpp)
    target_include_directories(saberstage_recording_state_tests PRIVATE include)
    add_test(NAME saberstage.recording_state COMMAND saberstage_recording_state_tests)
    return()
endif()

include(qpm_defines.cmake)
project(${COMPILE_ID} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-frtti -fexceptions -Wall -Wextra)
add_compile_definitions(
    VERSION="${MOD_VERSION}"
    MOD_ID="${MOD_ID}"
    SABERSTAGE_TARGET_GAME_VERSION="1.40.8_7379"
    SABERSTAGE_QPM_VERSION="1.5.11"
    SABERSTAGE_NDK_VERSION="27.3.13750724"
    USE_CODEGEN_FIELDS
)

file(GLOB_RECURSE SABERSTAGE_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.c)
file(GLOB_RECURSE INLINE_HOOK_C CONFIGURE_DEPENDS ${EXTERN_DIR}/includes/beatsaber-hook/shared/inline-hook/*.c)
file(GLOB_RECURSE INLINE_HOOK_CPP CONFIGURE_DEPENDS ${EXTERN_DIR}/includes/beatsaber-hook/shared/inline-hook/*.cpp)

add_library(${COMPILE_ID} SHARED ${SABERSTAGE_SOURCES} ${INLINE_HOOK_C} ${INLINE_HOOK_CPP})
target_include_directories(${COMPILE_ID} PRIVATE
    include
    src
    ${SHARED_DIR}
    ${EXTERN_DIR}/includes/${CODEGEN_ID}/include
)
target_link_libraries(${COMPILE_ID} PRIVATE -llog)
include(extern.cmake)

if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set_property(TARGET ${COMPILE_ID} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    target_compile_options(${COMPILE_ID} PRIVATE -O3 -flto)
endif()

add_custom_command(TARGET ${COMPILE_ID} POST_BUILD
    COMMAND ${CMAKE_STRIP} -d --strip-all "$<TARGET_FILE:${COMPILE_ID}>" -o "$<TARGET_FILE_DIR:${COMPILE_ID}>/stripped_lib${COMPILE_ID}.so"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${COMPILE_ID}>/debug"
    COMMAND ${CMAKE_COMMAND} -E rename "$<TARGET_FILE:${COMPILE_ID}>" "$<TARGET_FILE_DIR:${COMPILE_ID}>/debug/lib${COMPILE_ID}.so"
    COMMAND ${CMAKE_COMMAND} -E rename "$<TARGET_FILE_DIR:${COMPILE_ID}>/stripped_lib${COMPILE_ID}.so" "$<TARGET_FILE_DIR:${COMPILE_ID}>/lib${COMPILE_ID}.so"
    COMMENT "Preserving symbols under debug/ and packaging stripped library"
)
