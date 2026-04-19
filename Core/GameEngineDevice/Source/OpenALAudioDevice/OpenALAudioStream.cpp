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

#include "PreRTS.h"
#include "OpenALAudioDevice/OpenALAudioStream.h"

OpenALAudioStream::OpenALAudioStream()
	: m_source(0)
	, m_queuedCount(0)
{
	alGenSources(1, &m_source);
	alSourcef(m_source, AL_PITCH, 1.0f);
	alSourcef(m_source, AL_GAIN, 1.0f);
	alSource3f(m_source, AL_POSITION, 0.0f, 0.0f, 0.0f);
	alSource3f(m_source, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
	alSourcei(m_source, AL_SOURCE_RELATIVE, AL_TRUE);
}

OpenALAudioStream::~OpenALAudioStream()
{
	reset();
	if (m_source != 0) {
		alDeleteSources(1, &m_source);
		m_source = 0;
	}
	for (ALuint buf : m_freeBuffers) {
		alDeleteBuffers(1, &buf);
	}
	m_freeBuffers.clear();
}

void OpenALAudioStream::bufferData(const UnsignedByte* data, Int sizeInBytes, ALenum format, Int sampleRateHz)
{
	if (m_source == 0 || data == nullptr || sizeInBytes <= 0) {
		return;
	}

	reclaimProcessed();

	const ALuint buf = allocBuffer();
	alBufferData(buf, format, data, sizeInBytes, sampleRateHz);
	alSourceQueueBuffers(m_source, 1, &buf);
	++m_queuedCount;
}

void OpenALAudioStream::play()
{
	if (m_source == 0) return;
	if (isPlaying()) return;
	if (m_queuedCount == 0) return;
	alSourcePlay(m_source);
}

void OpenALAudioStream::reset()
{
	if (m_source == 0) return;

	alSourceStop(m_source);

	ALint queued = 0;
	alGetSourcei(m_source, AL_BUFFERS_QUEUED, &queued);
	for (ALint i = 0; i < queued; ++i) {
		ALuint buf = 0;
		alSourceUnqueueBuffers(m_source, 1, &buf);
		if (buf != 0) {
			m_freeBuffers.push_back(buf);
		}
	}
	m_queuedCount = 0;
}

void OpenALAudioStream::update()
{
	reclaimProcessed();
}

Bool OpenALAudioStream::isPlaying() const
{
	if (m_source == 0) return false;
	ALint state = 0;
	alGetSourcei(m_source, AL_SOURCE_STATE, &state);
	return state == AL_PLAYING;
}

ALuint OpenALAudioStream::allocBuffer()
{
	if (!m_freeBuffers.empty()) {
		const ALuint buf = m_freeBuffers.front();
		m_freeBuffers.pop_front();
		return buf;
	}
	ALuint buf = 0;
	alGenBuffers(1, &buf);
	return buf;
}

void OpenALAudioStream::reclaimProcessed()
{
	if (m_source == 0) return;
	ALint processed = 0;
	alGetSourcei(m_source, AL_BUFFERS_PROCESSED, &processed);
	for (ALint i = 0; i < processed; ++i) {
		ALuint buf = 0;
		alSourceUnqueueBuffers(m_source, 1, &buf);
		if (buf != 0) {
			m_freeBuffers.push_back(buf);
			--m_queuedCount;
		}
	}
}
