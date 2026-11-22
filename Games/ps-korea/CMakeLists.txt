cmake_minimum_required(VERSION 3.20)
project(PsKoreaRecomp LANGUAGES C)
set(SMSGG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../Recomp/smsggrecomp" CACHE PATH "SMS/GG framework")
include("${SMSGG_ROOT}/cmake/SmsGame.cmake")
sms_add_game(PsKoreaRecomp)

# Phantasy Star QoL hooks (auto-map, EXP/Meseta/encounter options) shared by all PS builds.
target_sources(PsKoreaRecomp PRIVATE ../../Common/PhantasyStar/ps_automap.c ../../Common/PhantasyStar/ps_qol.c ../../Common/PhantasyStar/ps_worldmap.c ../../Common/PhantasyStar/ps_panel.c ../../Common/PhantasyStar/ps_items.c ../../Common/PhantasyStar/ps_fly.c ../../Common/PhantasyStar/ps_sound.c ../../Common/PhantasyStar/ps_fixes.c)
