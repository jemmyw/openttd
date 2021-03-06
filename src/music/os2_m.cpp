/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file os2_m.cpp Music playback on OS/2. */

#include "../stdafx.h"
#include "../openttd.h"
#include "os2_m.h"

#define INCL_DOS
#define INCL_OS2MM
#define INCL_WIN

#include <stdarg.h>
#include <os2.h>
#include <os2me.h>

/**********************
 * OS/2 MIDI PLAYER
 **********************/

/* Interesting how similar the MCI API in OS/2 is to the Win32 MCI API,
 * eh? Anyone would think they both came from the same place originally! ;)
 */

static long CDECL MidiSendCommand(const char *cmd, ...)
{
	va_list va;
	char buf[512];
	va_start(va, cmd);
	vseprintf(buf, lastof(buf), cmd, va);
	va_end(va);
	return mciSendString(buf, NULL, 0, NULL, 0);
}

static FMusicDriver_OS2 iFMusicDriver_OS2;

void MusicDriver_OS2::PlaySong(const char *filename)
{
	MidiSendCommand("close all");

	if (MidiSendCommand("open %s type sequencer alias song", filename) != 0) {
		return;
	}

	MidiSendCommand("play song from 0");
}

void MusicDriver_OS2::StopSong()
{
	MidiSendCommand("close all");
}

void MusicDriver_OS2::SetVolume(byte vol)
{
	MidiSendCommand("set song audio volume %d", ((vol/127)*100));
}

bool MusicDriver_OS2::IsSongPlaying()
{
	char buf[16];
	mciSendString("status song mode", buf, sizeof(buf), NULL, 0);
	return strcmp(buf, "playing") == 0 || strcmp(buf, "seeking") == 0;
}

const char *MusicDriver_OS2::Start(const char * const *parm)
{
	return 0;
}

void MusicDriver_OS2::Stop()
{
	MidiSendCommand("close all");
}
