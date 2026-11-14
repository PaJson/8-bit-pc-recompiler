cmake_minimum_required(VERSION 3.16)
project(Launcher C)

# The engine's HUD font (runner/hud.c) is shared with the games.
set(SMSGG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../Recomp/smsggrecomp" CACHE PATH "smsggrecomp engine")

find_package(SDL2 CONFIG REQUIRED)

add_executable(Launcher WIN32 launcher.c "${SMSGG_ROOT}/runner/hud.c")
target_include_directories(Launcher PRIVATE "${SMSGG_ROOT}/runner")
target_compile_options(Launcher PRIVATE -Wall -Wextra -Wno-format-truncation -Wno-missing-field-initializers -O2)
if (TARGET SDL2::SDL2main)
    target_link_libraries(Launcher PRIVATE SDL2::SDL2main)
endif()
target_link_libraries(Launcher PRIVATE SDL2::SDL2 gdiplus m)
add_custom_command(TARGET Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:Launcher>")
