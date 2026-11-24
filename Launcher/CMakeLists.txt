cmake_minimum_required(VERSION 3.16)
project(Launcher C)

# The engine's HUD font (runner/hud.c) is shared with the games.
set(SMSGG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../Recomp/smsggrecomp" CACHE PATH "smsggrecomp engine")

find_package(SDL2 CONFIG REQUIRED)

add_executable(Launcher WIN32 launcher.c release_build.c "${SMSGG_ROOT}/runner/hud.c" "${SMSGG_ROOT}/runner/art.c")
target_include_directories(Launcher PRIVATE "${SMSGG_ROOT}/runner")
# Name / version from app_info.toml (the project's one place for them)
set(APP_INFO "${CMAKE_CURRENT_SOURCE_DIR}/../app_info.toml")
file(READ "${APP_INFO}" _ai)
string(REGEX MATCH "
name[ 	]*=[ 	]*\"([^\"]*)\"" _m "
${_ai}")
set(APP_NAME "${CMAKE_MATCH_1}")
string(REGEX MATCH "
version[ 	]*=[ 	]*\"([0-9]+)\.([0-9]+)\.([0-9]+)\"" _m "
${_ai}")
set(APP_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
set(APP_VERSION_COMMA "${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3},0")
if(NOT APP_NAME OR NOT CMAKE_MATCH_3 MATCHES "^[0-9]+$")
    message(FATAL_ERROR "app_info.toml: name / version (MAJOR.MINOR.PATCH) not found")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${APP_INFO}")
target_compile_definitions(Launcher PRIVATE "APP_NAME=\"${APP_NAME}\"" "APP_VERSION=\"${APP_VERSION}\"")
target_compile_options(Launcher PRIVATE -Wall -Wextra -Wno-format-truncation -Wno-missing-field-initializers -O2)
if (TARGET SDL2::SDL2main)
    target_link_libraries(Launcher PRIVATE SDL2::SDL2main)
endif()
target_link_libraries(Launcher PRIVATE SDL2::SDL2 gdiplus m)
# Program icon: Images/Icon.ico (made by Tools/make_icon.py from Images/Icon.png + IconLarge.png).
include(CheckLanguage)
check_language(RC)
set(LAUNCHER_ICON "${CMAKE_CURRENT_SOURCE_DIR}/../Images/Icon.ico")
if(CMAKE_RC_COMPILER AND EXISTS "${LAUNCHER_ICON}")
    enable_language(RC)
    get_filename_component(LAUNCHER_ICON "${LAUNCHER_ICON}" ABSOLUTE)
    set(_rc "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc")
    file(CONFIGURE OUTPUT "${_rc}" CONTENT "1 ICON \"${LAUNCHER_ICON}\"\n")
    set_source_files_properties("${_rc}" PROPERTIES OBJECT_DEPENDS "${LAUNCHER_ICON}")
    target_sources(Launcher PRIVATE "${_rc}")
endif()

add_custom_command(TARGET Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:Launcher>")
