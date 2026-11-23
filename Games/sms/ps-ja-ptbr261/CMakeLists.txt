cmake_minimum_required(VERSION 3.20)
project(PsJaPtbr261Recomp LANGUAGES C)
set(SMSGG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../Recomp/smsggrecomp" CACHE PATH "SMS/GG framework")
include("${SMSGG_ROOT}/cmake/SmsGame.cmake")
sms_add_game(PsJaPtbr261Recomp)

# Phantasy Star QoL hooks (auto-map, EXP/Meseta/encounter options) shared by all PS builds.
target_sources(PsJaPtbr261Recomp PRIVATE ../../../Common/sms/PhantasyStar/ps_automap.c ../../../Common/sms/PhantasyStar/ps_qol.c ../../../Common/sms/PhantasyStar/ps_worldmap.c ../../../Common/sms/PhantasyStar/ps_panel.c ../../../Common/sms/PhantasyStar/ps_items.c ../../../Common/sms/PhantasyStar/ps_fly.c ../../../Common/sms/PhantasyStar/ps_sound.c ../../../Common/sms/PhantasyStar/ps_fixes.c)
