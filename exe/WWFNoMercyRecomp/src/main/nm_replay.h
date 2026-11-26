// [replay 2026-11-14] Record a game's inputs, replay them, and hash the game's structures every retrace: the QA tool for
// proving that a build which moved the game's memory around still plays the same game (memory_groundwork DESIGN.md,
// proof P3). See nm_replay.cpp; the clock it relies on is ultramodern/lockstep.hpp. Nothing here runs unless asked for.
#pragma once
#include "ultramodern/ultra64.h"

void nm_replay_init();                    // main(), before the game starts: reads the NM_REPLAY_* settings
void nm_replay_pads(OSContPad* pads);     // arcade_cont.cpp: the four pads just read -- recorded, or replaced by the recording's
bool nm_headless();                       // NM_HEADLESS=1: no picture is drawn (the game's logic and its frame lists as ever)
