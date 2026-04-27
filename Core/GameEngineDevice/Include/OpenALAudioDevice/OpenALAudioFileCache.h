/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// OpenALAudioFileCache — ref-counted cache of fully decoded PCM16 game audio
// buffers, keyed by VFS path. Decodes arbitrary formats via FFmpegFile so the
// backend stays format-agnostic; the Miles equivalent is AudioFileCache in
// MilesAudioManager.h.

#pragma once

#include "Common/AsciiString.h"
#include "Common/STLTypedefs.h"

#include <AL/al.h>

class OpenALAudioFileCache
{
public:
	OpenALAudioFileCache();
	~OpenALAudioFileCache();

	OpenALAudioFileCache(const OpenALAudioFileCache&) = delete;
	OpenALAudioFileCache& operator=(const OpenALAudioFileCache&) = delete;

	// Returns an ALuint buffer handle for the decoded contents of `path`. On
	// first call per path, opens the file via TheFileSystem, decodes to PCM16
	// with FFmpeg, uploads via alBufferData, and records an entry. Subsequent
	// calls bump a refcount and hand back the same buffer. Returns 0 on
	// failure.
	ALuint openBuffer(const AsciiString& path);

	// Drop one reference. When the count drops to zero the buffer becomes a
	// candidate for eviction; actual alDeleteBuffers happens lazily when we
	// need to reclaim space or on shutdown.
	void closeBuffer(ALuint buffer);

	// Tear everything down — stops nothing, just deletes every buffer.
	// Callers must have stopped any AL source that still has one of our
	// buffers bound before calling this (OpenALAudioManager::closeDevice
	// handles that).
	void reset();

private:
	struct Entry
	{
		AsciiString  path;
		ALuint       buffer;
		Int          refCount;
	};

	std::vector<Entry> m_entries;
};
