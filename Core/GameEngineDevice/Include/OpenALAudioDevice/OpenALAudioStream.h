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

// OpenALAudioStream — streaming OpenAL source with a rolling buffer queue.
// Used by the FFmpeg video player for the movie audio track and (optionally)
// for long music streams that don't fit the normal sample cache.

#pragma once

#include "Lib/BaseType.h"

#include <AL/al.h>
#include <AL/alc.h>

#include <deque>

class OpenALAudioStream
{
public:
	OpenALAudioStream();
	~OpenALAudioStream();

	OpenALAudioStream(const OpenALAudioStream&) = delete;
	OpenALAudioStream& operator=(const OpenALAudioStream&) = delete;

	// Queue a block of PCM audio for playback. The data is copied into a GL buffer.
	void bufferData(const UnsignedByte* data, Int sizeInBytes, ALenum format, Int sampleRateHz);

	// Start the source if it has queued data and is not already playing.
	void play();

	// Stop playback, drop all queued buffers. Safe to call repeatedly.
	void reset();

	// Recycle completed buffers back into the free pool. Call before bufferData
	// in the producer loop so the free list stays non-empty.
	void update();

	// True when the source is actively rendering audio.
	Bool isPlaying() const;

	ALuint getSource() const { return m_source; }

private:
	ALuint allocBuffer();
	void reclaimProcessed();

	ALuint m_source;
	std::deque<ALuint> m_freeBuffers;
	Int m_queuedCount;
};
