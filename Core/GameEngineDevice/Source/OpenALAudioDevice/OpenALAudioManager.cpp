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
#include "OpenALAudioDevice/OpenALAudioManager.h"
#include "OpenALAudioDevice/OpenALAudioStream.h"

#include "Common/AudioAffect.h"
#include "Common/FileSystem.h"
#include "Common/File.h"

extern "C" {
	#include <libavcodec/avcodec.h>
	#include <libavformat/avformat.h>
}

OpenALAudioManager::OpenALAudioManager()
	: m_device(nullptr)
	, m_context(nullptr)
	, m_selectedProvider(PROVIDER_ERROR)
	, m_speakerType(0)
	, m_videoAudioStream(nullptr)
{
}

OpenALAudioManager::~OpenALAudioManager()
{
	releaseVideoAudioStreamHandle();
	closeDevice();
}

void OpenALAudioManager::init()
{
	AudioManager::init();
	enumerateProviders();
	// Mirrors MilesAudioManager::init(): open the backend device here so that
	// TheAudio is ready to accept sources/buffers before any subsystem (e.g.
	// the video player's primeVideoAudio) asks for a stream handle.
	openDevice();
}

void OpenALAudioManager::postProcessLoad()
{
	AudioManager::postProcessLoad();
}

void OpenALAudioManager::reset()
{
	AudioManager::reset();
	if (m_videoAudioStream) {
		m_videoAudioStream->reset();
	}
}

void OpenALAudioManager::update()
{
	AudioManager::update();
	if (m_videoAudioStream) {
		m_videoAudioStream->update();
	}
}

void OpenALAudioManager::openDevice()
{
	if (m_device != nullptr) {
		return;
	}

	m_device = alcOpenDevice(nullptr); // default device
	if (m_device == nullptr) {
		DEBUG_LOG(("OpenALAudioManager: alcOpenDevice failed"));
		return;
	}

	m_context = alcCreateContext(m_device, nullptr);
	if (m_context == nullptr) {
		DEBUG_LOG(("OpenALAudioManager: alcCreateContext failed"));
		alcCloseDevice(m_device);
		m_device = nullptr;
		return;
	}

	alcMakeContextCurrent(m_context);
	alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
	alListener3f(AL_VELOCITY, 0.0f, 0.0f, 0.0f);
	const ALfloat orientation[6] = { 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f };
	alListenerfv(AL_ORIENTATION, orientation);
}

void OpenALAudioManager::closeDevice()
{
	if (m_videoAudioStream) {
		delete m_videoAudioStream;
		m_videoAudioStream = nullptr;
	}

	if (m_context) {
		alcMakeContextCurrent(nullptr);
		alcDestroyContext(m_context);
		m_context = nullptr;
	}
	if (m_device) {
		alcCloseDevice(m_device);
		m_device = nullptr;
	}
}

void OpenALAudioManager::enumerateProviders()
{
	m_providers.clear();

	const ALCchar* names = nullptr;
	if (alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT") == AL_TRUE) {
		names = alcGetString(nullptr, ALC_ALL_DEVICES_SPECIFIER);
	} else if (alcIsExtensionPresent(nullptr, "ALC_ENUMERATION_EXT") == AL_TRUE) {
		names = alcGetString(nullptr, ALC_DEVICE_SPECIFIER);
	}

	if (names) {
		const ALCchar* p = names;
		while (*p) {
			m_providers.push_back(AsciiString(p));
			p += std::strlen(p) + 1;
		}
	}

	if (m_providers.empty()) {
		m_providers.push_back(AsciiString("Default"));
	}

	if (m_selectedProvider == PROVIDER_ERROR) {
		m_selectedProvider = 0;
	}
}

UnsignedInt OpenALAudioManager::getProviderCount() const
{
	return static_cast<UnsignedInt>(m_providers.size());
}

AsciiString OpenALAudioManager::getProviderName(UnsignedInt providerNum) const
{
	if (providerNum >= m_providers.size()) {
		return AsciiString::TheEmptyString;
	}
	return m_providers[providerNum];
}

UnsignedInt OpenALAudioManager::getProviderIndex(AsciiString providerName) const
{
	for (UnsignedInt i = 0; i < m_providers.size(); ++i) {
		if (m_providers[i] == providerName) {
			return i;
		}
	}
	return PROVIDER_ERROR;
}

void OpenALAudioManager::selectProvider(UnsignedInt providerNdx)
{
	// ToDo: closing and reopening an ALC device on a different name requires
	// tearing down every live OpenAL source. For now, treat the selection as
	// a preference that takes effect on next openDevice().
	if (providerNdx < m_providers.size()) {
		m_selectedProvider = providerNdx;
	}
}

void OpenALAudioManager::unselectProvider()
{
	m_selectedProvider = PROVIDER_ERROR;
}

void OpenALAudioManager::setDeviceListenerPosition()
{
	if (m_context == nullptr) return;

	alListener3f(AL_POSITION,
		static_cast<ALfloat>(m_listenerPosition.x),
		static_cast<ALfloat>(m_listenerPosition.y),
		static_cast<ALfloat>(m_listenerPosition.z));

	// m_listenerOrientation is a forward-pointing vector in the game's coord system.
	const ALfloat orientation[6] = {
		static_cast<ALfloat>(m_listenerOrientation.x),
		static_cast<ALfloat>(m_listenerOrientation.y),
		static_cast<ALfloat>(m_listenerOrientation.z),
		0.0f, 0.0f, 1.0f
	};
	alListenerfv(AL_ORIENTATION, orientation);
}

void* OpenALAudioManager::getVideoAudioStreamHandle()
{
	if (m_videoAudioStream == nullptr) {
		m_videoAudioStream = new OpenALAudioStream();
	}
	return m_videoAudioStream;
}

void OpenALAudioManager::releaseVideoAudioStreamHandle()
{
	if (m_videoAudioStream) {
		m_videoAudioStream->reset();
	}
}

ALenum OpenALAudioManager::getALFormat(Int numChannels, Int bitsPerSample)
{
	if (numChannels == 1) {
		if (bitsPerSample == 8)  return AL_FORMAT_MONO8;
		if (bitsPerSample == 16) return AL_FORMAT_MONO16;
	} else if (numChannels == 2) {
		if (bitsPerSample == 8)  return AL_FORMAT_STEREO8;
		if (bitsPerSample == 16) return AL_FORMAT_STEREO16;
	}
	return AL_FORMAT_MONO16;
}

Real OpenALAudioManager::getFileLengthMS(AsciiString strToLoad) const
{
	if (strToLoad.isEmpty()) {
		return 0.0f;
	}

	AVFormatContext* fmt = nullptr;
	if (avformat_open_input(&fmt, strToLoad.str(), nullptr, nullptr) != 0) {
		return 0.0f;
	}
	Real lengthMS = 0.0f;
	if (avformat_find_stream_info(fmt, nullptr) >= 0 && fmt->duration > 0) {
		lengthMS = static_cast<Real>(fmt->duration) * 1000.0f / static_cast<Real>(AV_TIME_BASE);
	}
	avformat_close_input(&fmt);
	return lengthMS;
}

Real OpenALAudioManagerNull::getFileLengthMS(AsciiString strToLoad) const
{
	// The null backend still reports real file lengths so scripts can compute
	// CRC-relevant delays even when no device is open (same contract as
	// MilesAudioManagerDummy inheriting Miles' real getFileLengthMS).
	return OpenALAudioManager::getFileLengthMS(strToLoad);
}
