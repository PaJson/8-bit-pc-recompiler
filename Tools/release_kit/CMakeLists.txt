# Release kit: everything a game exe needs except the C the recompiler makes from the player's ROM.
# Built by Tools/make_release.py (or the GitHub release workflow) with MSYS2's gcc; the release then
# builds each game on the player's PC with SmsRecomp.exe + the trimmed gcc it ships (Launcher.exe
# --build), linking the generated C with these libraries. Nothing here is derived from a ROM.
#
#   libsmsgg_runner.a          the engine's runner (sms_add_runner_library: same sources / flags as a game)
#   libhooks_<Family>.a        our per-game code, one per Common/<platform>/<Family>/ folder
#   SmsInterp.exe              generic interpreter (sms_add_interp): title screens before a game is built
#   app_icon.o                 the program icon (Images/Icon.ico) as a linkable resource object
#   kit_flags.txt              the compile definitions every generated file must be compiled with
cmake_minimum_required(VERSION 3.20)
project(ReleaseKit LANGUAGES C)

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SMSGG_ROOT "${PROJECT_ROOT}/Recomp/smsggrecomp" CACHE PATH "SMS/GG framework")
include("${SMSGG_ROOT}/cmake/SmsGame.cmake")

sms_add_runner_library(smsgg_runner)
sms_add_interp(SmsInterp)

file(GLOB _families LIST_DIRECTORIES true "${PROJECT_ROOT}/Common/sms/*")
foreach(_dir IN LISTS _families)
    if(IS_DIRECTORY "${_dir}")
        get_filename_component(_name "${_dir}" NAME)
        file(GLOB _src "${_dir}/*.c")
        if(_src)
            add_library(hooks_${_name} STATIC ${_src})
            sms_runner_settings(hooks_${_name})
        endif()
    endif()
endforeach()

if(WIN32 AND CMAKE_RC_COMPILER)
    set(_ico "${PROJECT_ROOT}/Images/Icon.ico")
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc" CONTENT "1 ICON \"${_ico}\"\n")
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/app_icon.o"
        COMMAND "${CMAKE_RC_COMPILER}" -O coff "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc" "${CMAKE_CURRENT_BINARY_DIR}/app_icon.o"
        DEPENDS "${_ico}" VERBATIM)
    add_custom_target(app_icon ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/app_icon.o")
endif()

# The definitions of a game's generated C (= the runner's; read by make_release.py into the release).
get_target_property(_defs smsgg_runner COMPILE_DEFINITIONS)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/kit_flags.txt" CONTENT "${_defs}\n")
