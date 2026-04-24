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
#include "OpenALAudioDevice/OpenALAudioFileCache.h"

#include "Common/AudioAffect.h"
#include "Common/AudioEventInfo.h"
#include "Common/AudioRequest.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/AudioSettings.h"
#include "Common/FileSystem.h"
#include "Common/File.h"
#include "Common/GameAudio.h"
#include "Common/GameCommon.h"

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
	, m_fileCache(nullptr)
	, m_musicSource(0)
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
	// Stop everything that was playing under the previous ruleset; the next
	// addAudioEvent call from the game will repopulate.
	stopOrFadeByAffect(AudioAffect_All);
	if (m_videoAudioStream) {
		m_videoAudioStream->reset();
	}
}

void OpenALAudioManager::update()
{
	AudioManager::update();
	processRequestList();
	processPlayingList();
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

	// Pre-allocate the 2D source pool. Matches getNum2DSamples() == 32. If the
	// driver refuses to hand out that many we just fall through with a smaller
	// pool; playAudioEvent() tolerates an empty pool.
	m_sourcePool.clear();
	m_sourcePool.reserve(getNum2DSamples());
	for (UnsignedInt i = 0; i < getNum2DSamples(); ++i) {
		ALuint src = 0;
		alGenSources(1, &src);
		if (alGetError() != AL_NO_ERROR || src == 0) {
			break;
		}
		alSourcei(src, AL_SOURCE_RELATIVE, AL_TRUE);
		m_sourcePool.push_back(src);
	}

	// Dedicated music source so looping music doesn't contend with SFX.
	alGenSources(1, &m_musicSource);
	if (alGetError() != AL_NO_ERROR) {
		m_musicSource = 0;
	} else {
		alSourcei(m_musicSource, AL_SOURCE_RELATIVE, AL_TRUE);
	}

	if (m_fileCache == nullptr) {
		m_fileCache = new OpenALAudioFileCache();
	}
}

void OpenALAudioManager::closeDevice()
{
	// Stop everything first so no source is referencing a buffer the cache is
	// about to alDeleteBuffers.
	stopOrFadeByAffect(AudioAffect_All);

	// Finalize any entries still hanging around (stopOrFadeByAffect only
	// alSourceStop's them; the playing lists still own the PlayingAudio).
	while (!m_playingSounds.empty())
		finalizeEntry(m_playingSounds, m_playingSounds.begin());
	while (!m_playingMusic.empty())
		finalizeEntry(m_playingMusic, m_playingMusic.begin());
	while (!m_playingSpeech.empty())
		finalizeEntry(m_playingSpeech, m_playingSpeech.begin());

	if (m_videoAudioStream) {
		delete m_videoAudioStream;
		m_videoAudioStream = nullptr;
	}

	for (ALuint src : m_sourcePool) {
		if (src != 0) {
			alDeleteSources(1, &src);
		}
	}
	m_sourcePool.clear();

	if (m_musicSource != 0) {
		alDeleteSources(1, &m_musicSource);
		m_musicSource = 0;
	}

	if (m_fileCache) {
		delete m_fileCache;
		m_fileCache = nullptr;
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

// ============================================================================
// Phase 2: game-audio playback
// ============================================================================

AudioAffect OpenALAudioManager::affectFor(AudioType t)
{
	switch (t)
	{
		case AT_Music:     return AudioAffect_Music;
		case AT_Streaming: return AudioAffect_Speech;
		case AT_SoundEffect:
		default:           return AudioAffect_Sound;
	}
}

Real OpenALAudioManager::effectiveVolume(const AudioEventRTS* event) const
{
	// Mirrors MilesAudioManager::getEffectiveVolume (MilesAudioManager.cpp:2664).
	// Three-tier volume is event volume * volume shift * category multiplier,
	// with an extra distance-fade pass for positional 3D sound effects.
	if (event == nullptr) return 0.0f;
	const AudioEventInfo* info = event->getAudioEventInfo();
	if (info == nullptr) return 0.0f;

	Real volume = event->getVolume() * event->getVolumeShift();

	switch (info->m_soundType)
	{
		case AT_Music:
			volume *= m_musicVolume;
			break;
		case AT_Streaming:
			volume *= m_speechVolume;
			break;
		case AT_SoundEffect:
			if (event->isPositionalAudio())
			{
				volume *= m_sound3DVolume;
				// getCurrentPosition is non-const on AudioEventRTS (it refreshes
				// from the attached Object/Drawable) and effectiveVolume() is
				// const, so cast away const locally. The mutation on the event
				// is about caching the latest object position.
				AudioEventRTS* mutable_event = const_cast<AudioEventRTS*>(event);
				const Coord3D* pos = mutable_event->getCurrentPosition();
				if (pos)
				{
					Coord3D delta = m_listenerPosition;
					delta.sub(pos);
					const Real objDistance = delta.length();

					Real objMinDistance = info->m_minDistance;
					Real objMaxDistance = info->m_maxDistance;
					const AudioSettings* audioSettings = getAudioSettings();
					if (audioSettings && (info->m_type & ST_GLOBAL))
					{
						objMinDistance = static_cast<Real>(audioSettings->m_globalMinRange);
						objMaxDistance = static_cast<Real>(audioSettings->m_globalMaxRange);
					}

					if (objDistance >= objMaxDistance)
					{
						volume = 0.0f;
					}
					else if (audioSettings
						&& audioSettings->m_use3DSoundRangeVolumeFade
						&& objDistance > objMinDistance
						&& objMaxDistance > objMinDistance)
					{
						Real attenuation = (objDistance - objMinDistance) /
						                   (objMaxDistance - objMinDistance);
						attenuation = pow(attenuation,
						                  audioSettings->m_3DSoundRangeVolumeFadeExponent);
						volume *= 1.0f - attenuation;
					}
				}
			}
			else
			{
				volume *= m_soundVolume;
			}
			break;
	}

	if (volume < 0.0f) volume = 0.0f;
	if (volume > 1.0f) volume = 1.0f;
	return volume;
}

void OpenALAudioManager::applyVolume(PlayingAudio* pa)
{
	if (pa == nullptr || pa->source == 0) return;
	alSourcef(pa->source, AL_GAIN, effectiveVolume(pa->event));
}

ALuint OpenALAudioManager::acquireSource()
{
	if (m_sourcePool.empty()) {
		// No freely available source. Steal the oldest non-looping one-shot.
		for (auto it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			PlayingAudio* pa = *it;
			if (pa == nullptr) continue;
			const AudioEventInfo* info = pa->event ? pa->event->getAudioEventInfo() : nullptr;
			const Bool isLoop = info && BitIsSet(info->m_control, AC_LOOP);
			if (!isLoop) {
				ALuint stolen = pa->source;
				alSourceStop(stolen);
				alSourcei(stolen, AL_BUFFER, 0);
				if (m_fileCache) m_fileCache->closeBuffer(pa->buffer);
				if (pa->event) releaseAudioEventRTS(pa->event);
				delete pa;
				m_playingSounds.erase(it);
				return stolen;
			}
		}
		return 0;
	}
	ALuint src = m_sourcePool.back();
	m_sourcePool.pop_back();
	return src;
}

void OpenALAudioManager::releaseSource(ALuint source)
{
	if (source == 0) return;
	alSourceStop(source);
	alSourcei(source, AL_BUFFER, 0);
	m_sourcePool.push_back(source);
}

std::list<OpenALAudioManager::PlayingAudio*>::iterator
OpenALAudioManager::finalizeEntry(std::list<PlayingAudio*>& list,
                                   std::list<PlayingAudio*>::iterator it)
{
	PlayingAudio* pa = *it;
	if (pa) {
		if (pa->source != 0) {
			alSourceStop(pa->source);
			alSourcei(pa->source, AL_BUFFER, 0);
			if (pa->source == m_musicSource) {
				// Music source is persistent; don't return to SFX pool.
			} else {
				m_sourcePool.push_back(pa->source);
			}
		}
		if (m_fileCache && pa->buffer != 0) {
			m_fileCache->closeBuffer(pa->buffer);
		}
		if (pa->event) {
			releaseAudioEventRTS(pa->event);
		}
		delete pa;
	}
	return list.erase(it);
}

AsciiString OpenALAudioManager::filenameForPortion(AudioEventRTS* event) const
{
	if (event == nullptr) return AsciiString::TheEmptyString;
	switch (event->getNextPlayPortion())
	{
		case PP_Attack: return event->getAttackFilename();
		case PP_Decay:  return event->getDecayFilename();
		case PP_Sound:
		default:        return event->getFilename();
	}
}

void OpenALAudioManager::playAudioEvent(AudioEventRTS* event)
{
	if (event == nullptr) {
		return;
	}
	if (m_fileCache == nullptr) {
		releaseAudioEventRTS(event);
		return;
	}

	const AudioEventInfo* info = event->getAudioEventInfo();
	if (info == nullptr) {
		releaseAudioEventRTS(event);
		return;
	}

	const AudioType type = info->m_soundType;

	// generatePlayInfo (already called by the base addAudioEvent) seeds
	// m_portionToPlayNext to PP_Attack when attack sounds exist, otherwise
	// PP_Sound. Read it so the right file loads below.
	const AsciiString path = filenameForPortion(event);
	if (path.isEmpty()) {
		releaseAudioEventRTS(event);
		return;
	}

	ALuint buffer = m_fileCache->openBuffer(path);
	if (buffer == 0) {
		releaseAudioEventRTS(event);
		return;
	}

	ALuint source = 0;
	if (type == AT_Music) {
		// Dedicated music source; if something was already on it, finalize first
		// so buffers and event lifetimes unwind cleanly.
		while (!m_playingMusic.empty()) {
			finalizeEntry(m_playingMusic, m_playingMusic.begin());
		}
		source = m_musicSource;
	} else {
		source = acquireSource();
	}

	if (source == 0) {
		m_fileCache->closeBuffer(buffer);
		releaseAudioEventRTS(event);
		return;
	}

	alSourceStop(source);
	alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
	const Bool isLoopControl = BitIsSet(info->m_control, AC_LOOP);
	// Hard-loop the AL source only if the event is a permanent loop (loopCount
	// == 0 with AC_LOOP) AND has no attack/decay portion to transition to.
	// Otherwise we drive loops and portion transitions from handleSourceStopped.
	const Bool hasPortions = !event->getAttackFilename().isEmpty()
		|| !event->getDecayFilename().isEmpty();
	const Bool permanentLoop = isLoopControl && info->m_loopCount == 0 && !hasPortions;
	alSourcei(source, AL_LOOPING, permanentLoop ? AL_TRUE : AL_FALSE);
	alSourcef(source, AL_PITCH, event->getPitchShift() > 0.0f ? event->getPitchShift() : 1.0f);

	PlayingAudio* pa = new PlayingAudio();
	pa->source       = source;
	pa->buffer       = buffer;
	pa->event        = event;
	pa->type         = type;
	pa->affect       = affectFor(type);
	pa->paused       = FALSE;
	pa->positional   = event->isPositionalAudio() && type == AT_SoundEffect;
	pa->wantsPortion = hasPortions || (isLoopControl && info->m_loopCount != 0);

	if (pa->positional) {
		alSourcei(source, AL_SOURCE_RELATIVE, AL_FALSE);
		// We apply Miles' fade curve ourselves on AL_GAIN; keep OpenAL's own
		// distance model out of the picture so it doesn't double-attenuate.
		alSourcef(source, AL_ROLLOFF_FACTOR, 0.0f);
		const Coord3D* p = event->getCurrentPosition();
		if (p) {
			alSource3f(source, AL_POSITION,
				static_cast<ALfloat>(p->x),
				static_cast<ALfloat>(p->y),
				static_cast<ALfloat>(p->z));
		}
	} else {
		alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
		alSource3f(source, AL_POSITION, 0.0f, 0.0f, 0.0f);
	}

	applyVolume(pa);

	alSourcePlay(source);

	const ALenum err = alGetError();
	if (err != AL_NO_ERROR) {
		DEBUG_LOG(("OpenALAudioManager: alSourcePlay failed (0x%x) for '%s'",
			err, path.str()));
	}

	if (type == AT_Music) {
		m_currentMusicTrackName = event->getEventName();
		m_playingMusic.push_back(pa);
	} else if (type == AT_Streaming) {
		m_playingSpeech.push_back(pa);
		if (event->getUninterruptible()) {
			// Other speech will cull automatically in addAudioEvent via the
			// base class checking getDisallowSpeech(); we set it here so that
			// window cleared on finalize.
			setDisallowSpeech(TRUE);
		}
	} else {
		m_playingSounds.push_back(pa);
	}
}

Bool OpenALAudioManager::shouldProcessRequestThisFrame(const AudioRequest* req) const
{
	// Mirrors MilesAudioManager::shouldProcessRequestThisFrame (line 2483).
	if (req == nullptr || !req->m_usePendingEvent) return TRUE;
	if (req->m_pendingEvent == nullptr) return TRUE;
	return req->m_pendingEvent->getDelay() < MSEC_PER_LOGICFRAME_REAL;
}

void OpenALAudioManager::adjustRequest(AudioRequest* req)
{
	// Mirrors MilesAudioManager::adjustRequest (line 2497): tick the delay
	// timer by one logic frame so a delayed event becomes eligible next frame.
	if (req == nullptr || !req->m_usePendingEvent || req->m_pendingEvent == nullptr) {
		return;
	}
	req->m_pendingEvent->decrementDelay(MSEC_PER_LOGICFRAME_REAL);
	req->m_requiresCheckForSample = TRUE;
}

void OpenALAudioManager::processRequestList()
{
	// Mirrors MilesAudioManager::processRequestList (line 2224). Iterate rather
	// than drain so delayed requests can be left in place for next frame.
	auto it = m_audioRequests.begin();
	while (it != m_audioRequests.end()) {
		AudioRequest* req = *it;
		if (req == nullptr) {
			it = m_audioRequests.erase(it);
			continue;
		}

		if (!shouldProcessRequestThisFrame(req)) {
			adjustRequest(req);
			++it;
			continue;
		}

		switch (req->m_request)
		{
			case AR_Play:
				if (req->m_usePendingEvent && req->m_pendingEvent != nullptr) {
					AudioEventRTS* ev = req->m_pendingEvent;
					req->m_pendingEvent = nullptr;
					playAudioEvent(ev);
				}
				break;
			case AR_Stop:
				if (!req->m_usePendingEvent) {
					stopAudioByHandle(req->m_handleToInteractOn, /*immediate=*/FALSE);
				}
				break;
			case AR_Pause:
				if (!req->m_usePendingEvent) {
					pauseAudioByHandle(req->m_handleToInteractOn);
				}
				break;
		}

		releaseAudioRequest(req);
		it = m_audioRequests.erase(it);
	}
}

Bool OpenALAudioManager::handleSourceStopped(PlayingAudio* pa)
{
	// Return TRUE if the entry should keep playing (we rewound / swapped
	// buffer / kicked the source), FALSE if the caller should finalize it.
	if (pa == nullptr || pa->event == nullptr || pa->source == 0) return FALSE;

	AudioEventRTS* ev = pa->event;
	const AudioEventInfo* info = ev->getAudioEventInfo();
	if (info == nullptr) return FALSE;

	const Bool hasFiniteLoopCount = BitIsSet(info->m_control, AC_LOOP) && info->m_loopCount != 0;

	// Finite-loop events (loopCount > 0) want to replay the same buffer until
	// the count hits zero. AL_LOOPING is off for these (see playAudioEvent),
	// so we manage it here.
	if (hasFiniteLoopCount) {
		ev->decreaseLoopCount();
		if (ev->hasMoreLoops()) {
			alSourceRewind(pa->source);
			alSourcePlay(pa->source);
			return TRUE;
		}
	}

	// Attack/Sound/Decay transitions: advance to the next portion, swap the
	// buffer on the existing source, and kick it again.
	if (pa->wantsPortion) {
		ev->advanceNextPlayPortion();
		if (ev->getNextPlayPortion() != PP_Done) {
			const AsciiString next = filenameForPortion(ev);
			if (!next.isEmpty() && m_fileCache != nullptr) {
				ALuint nextBuf = m_fileCache->openBuffer(next);
				if (nextBuf != 0) {
					alSourceStop(pa->source);
					alSourcei(pa->source, AL_BUFFER, static_cast<ALint>(nextBuf));
					m_fileCache->closeBuffer(pa->buffer);
					pa->buffer = nextBuf;
					applyVolume(pa);
					alSourcePlay(pa->source);
					return TRUE;
				}
			}
		}
	}

	return FALSE;
}

void OpenALAudioManager::processPlayingList()
{
	// SFX + speech lists: poll for stopped, transition portions / loops,
	// refresh positional sources.
	auto updateNonMusic = [this](std::list<PlayingAudio*>& list) {
		auto it = list.begin();
		while (it != list.end()) {
			PlayingAudio* pa = *it;
			if (pa == nullptr || pa->source == 0) {
				it = list.erase(it);
				continue;
			}

			// If the event's owner died (unit destroyed, drawable removed),
			// stop the sound. Matches MilesAudioManager.cpp:2305-2308.
			if (pa->event && pa->event->isDead()) {
				it = finalizeEntry(list, it);
				continue;
			}

			if (pa->paused) {
				++it;
				continue;
			}

			ALint state = 0;
			alGetSourcei(pa->source, AL_SOURCE_STATE, &state);
			if (state == AL_STOPPED) {
				if (handleSourceStopped(pa)) {
					++it;
				} else {
					// Releasing uninterruptible speech? Clear the disallow flag.
					if (pa->type == AT_Streaming && pa->event
						&& pa->event->getUninterruptible()) {
						setDisallowSpeech(FALSE);
					}
					it = finalizeEntry(list, it);
				}
				continue;
			}

			// Live source: refresh position + gain every frame. Position is
			// cheap enough to push unconditionally; AL ignores unchanged
			// values.
			if (pa->positional && pa->event) {
				const Coord3D* p = pa->event->getCurrentPosition();
				if (p) {
					alSource3f(pa->source, AL_POSITION,
						static_cast<ALfloat>(p->x),
						static_cast<ALfloat>(p->y),
						static_cast<ALfloat>(p->z));
				}
				applyVolume(pa);
			} else if (m_volumeHasChanged) {
				applyVolume(pa);
			}
			++it;
		}
	};

	updateNonMusic(m_playingSounds);
	updateNonMusic(m_playingSpeech);

	// Music list: same state-machine but completion bumps m_musicCompletions
	// so hasMusicTrackCompleted() can fire scripted music rotations.
	auto it = m_playingMusic.begin();
	while (it != m_playingMusic.end()) {
		PlayingAudio* pa = *it;
		if (pa == nullptr || pa->source == 0) {
			it = m_playingMusic.erase(it);
			continue;
		}
		if (pa->paused) {
			++it;
			continue;
		}
		ALint state = 0;
		alGetSourcei(pa->source, AL_SOURCE_STATE, &state);
		if (state == AL_STOPPED) {
			if (handleSourceStopped(pa)) {
				++it;
				continue;
			}
			if (pa->event) {
				++m_musicCompletions[pa->event->getEventName()];
			}
			it = finalizeEntry(m_playingMusic, it);
			continue;
		}
		if (m_volumeHasChanged) {
			applyVolume(pa);
		}
		++it;
	}

	m_volumeHasChanged = FALSE;
}

void OpenALAudioManager::stopAudioByHandle(AudioHandle handle, Bool immediate)
{
	(void)immediate;
	auto walk = [&](std::list<PlayingAudio*>& list) {
		for (auto it = list.begin(); it != list.end(); ++it) {
			PlayingAudio* pa = *it;
			if (pa && pa->event && pa->event->getPlayingHandle() == handle) {
				finalizeEntry(list, it);
				return true;
			}
		}
		return false;
	};
	if (walk(m_playingSounds)) return;
	if (walk(m_playingMusic)) return;
	walk(m_playingSpeech);
}

void OpenALAudioManager::pauseAudioByHandle(AudioHandle handle)
{
	auto walk = [&](std::list<PlayingAudio*>& list) {
		for (PlayingAudio* pa : list) {
			if (pa && pa->event && pa->event->getPlayingHandle() == handle) {
				alSourcePause(pa->source);
				pa->paused = TRUE;
				return true;
			}
		}
		return false;
	};
	if (walk(m_playingSounds)) return;
	if (walk(m_playingMusic)) return;
	walk(m_playingSpeech);
}

void OpenALAudioManager::stopOrFadeByAffect(AudioAffect which)
{
	auto walk = [&](std::list<PlayingAudio*>& list) {
		auto it = list.begin();
		while (it != list.end()) {
			PlayingAudio* pa = *it;
			if (pa && (pa->affect & which)) {
				it = finalizeEntry(list, it);
			} else {
				++it;
			}
		}
	};
	walk(m_playingSounds);
	walk(m_playingMusic);
	walk(m_playingSpeech);
}

void OpenALAudioManager::pauseByAffect(AudioAffect which)
{
	auto walk = [&](std::list<PlayingAudio*>& list) {
		for (PlayingAudio* pa : list) {
			if (pa && (pa->affect & which) && pa->source != 0 && !pa->paused) {
				alSourcePause(pa->source);
				pa->paused = TRUE;
			}
		}
	};
	walk(m_playingSounds);
	walk(m_playingMusic);
	walk(m_playingSpeech);
}

void OpenALAudioManager::resumeByAffect(AudioAffect which)
{
	auto walk = [&](std::list<PlayingAudio*>& list) {
		for (PlayingAudio* pa : list) {
			if (pa && (pa->affect & which) && pa->source != 0 && pa->paused) {
				alSourcePlay(pa->source);
				pa->paused = FALSE;
			}
		}
	};
	walk(m_playingSounds);
	walk(m_playingMusic);
	walk(m_playingSpeech);
}

void OpenALAudioManager::stopAudio(AudioAffect which)
{
	stopOrFadeByAffect(which);
}

void OpenALAudioManager::pauseAudio(AudioAffect which)
{
	pauseByAffect(which);
}

void OpenALAudioManager::resumeAudio(AudioAffect which)
{
	resumeByAffect(which);
}

void OpenALAudioManager::pauseAmbient(Bool shouldPause)
{
	// No dedicated ambient channel yet; treat as pausing all non-music 2D sfx.
	if (shouldPause) {
		pauseByAffect((AudioAffect)(AudioAffect_Sound | AudioAffect_Sound3D));
	} else {
		resumeByAffect((AudioAffect)(AudioAffect_Sound | AudioAffect_Sound3D));
	}
}

void OpenALAudioManager::killAudioEventImmediately(AudioHandle audioEvent)
{
	stopAudioByHandle(audioEvent, /*immediate=*/TRUE);
}

void OpenALAudioManager::nextMusicTrack()
{
	// Use the base class's track-name table to pick the successor; hand off
	// to MusicManager which queues AR_Play via the request pipe.
	if (m_currentMusicTrackName.isEmpty()) {
		if (m_musicTracks.empty()) return;
		m_currentMusicTrackName = m_musicTracks.front();
	} else {
		m_currentMusicTrackName = nextTrackName(m_currentMusicTrackName);
	}
	stopOrFadeByAffect(AudioAffect_Music);

	AudioEventRTS ev(m_currentMusicTrackName);
	TheAudio->addAudioEvent(&ev);
}

void OpenALAudioManager::prevMusicTrack()
{
	if (m_currentMusicTrackName.isEmpty()) {
		if (m_musicTracks.empty()) return;
		m_currentMusicTrackName = m_musicTracks.back();
	} else {
		m_currentMusicTrackName = prevTrackName(m_currentMusicTrackName);
	}
	stopOrFadeByAffect(AudioAffect_Music);

	AudioEventRTS ev(m_currentMusicTrackName);
	TheAudio->addAudioEvent(&ev);
}

Bool OpenALAudioManager::isMusicPlaying() const
{
	for (const PlayingAudio* pa : m_playingMusic) {
		if (pa == nullptr || pa->source == 0 || pa->paused) continue;
		ALint state = 0;
		alGetSourcei(pa->source, AL_SOURCE_STATE, &state);
		if (state == AL_PLAYING) return TRUE;
	}
	return FALSE;
}

Bool OpenALAudioManager::hasMusicTrackCompleted(const AsciiString& trackName, Int numberOfTimes) const
{
	if (trackName.isEmpty() || numberOfTimes <= 0) return FALSE;
	auto it = m_musicCompletions.find(trackName);
	if (it == m_musicCompletions.end()) return FALSE;
	return it->second >= static_cast<UnsignedInt>(numberOfTimes);
}

AsciiString OpenALAudioManager::getMusicTrackName() const
{
	return m_currentMusicTrackName;
}

void OpenALAudioManager::adjustVolumeOfPlayingAudio(AsciiString eventName, Real newVolume)
{
	auto walk = [&](std::list<PlayingAudio*>& list) {
		for (PlayingAudio* pa : list) {
			if (pa && pa->event && pa->event->getEventName() == eventName) {
				pa->event->setVolume(newVolume);
				applyVolume(pa);
			}
		}
	};
	walk(m_playingSounds);
	walk(m_playingMusic);
	walk(m_playingSpeech);
}

void OpenALAudioManager::removePlayingAudio(AsciiString eventName)
{
	auto walk = [&](std::list<PlayingAudio*>& list) {
		auto it = list.begin();
		while (it != list.end()) {
			PlayingAudio* pa = *it;
			if (pa && pa->event && pa->event->getEventName() == eventName) {
				it = finalizeEntry(list, it);
			} else {
				++it;
			}
		}
	};
	walk(m_playingSounds);
	walk(m_playingMusic);
	walk(m_playingSpeech);
}

void OpenALAudioManager::setVolume(Real volume, AudioAffect whichToAffect)
{
	// Base class updates the category volume state vars; once that lands we
	// can push the resulting effectiveVolume to every playing AL source.
	AudioManager::setVolume(volume, whichToAffect);

	auto walk = [&](std::list<PlayingAudio*>& list) {
		for (PlayingAudio* pa : list) {
			if (pa && pa->source != 0 && (pa->affect & whichToAffect)) {
				applyVolume(pa);
			}
		}
	};
	walk(m_playingSounds);
	walk(m_playingMusic);
	walk(m_playingSpeech);
}

void OpenALAudioManager::setListenerPosition(const Coord3D* newListenerPos, const Coord3D* newListenerOrientation)
{
	// Base class stores the state; push it into OpenAL right away so positional
	// sources that were just started track the current tactical camera.
	AudioManager::setListenerPosition(newListenerPos, newListenerOrientation);
	setDeviceListenerPosition();
}

// ============================================================================
// Polyphony culling
// ============================================================================

Int OpenALAudioManager::countEventName(const AsciiString& eventName) const
{
	Int count = 0;
	auto walk = [&](const std::list<PlayingAudio*>& list) {
		for (const PlayingAudio* pa : list) {
			if (pa && pa->event && pa->event->getEventName() == eventName) ++count;
		}
	};
	walk(m_playingSounds);
	walk(m_playingMusic);
	walk(m_playingSpeech);
	for (const AudioRequest* req : m_audioRequests) {
		if (req && req->m_usePendingEvent && req->m_pendingEvent
			&& req->m_pendingEvent->getEventName() == eventName) {
			++count;
		}
	}
	return count;
}

Bool OpenALAudioManager::isPlayingAlready(AudioEventRTS* event) const
{
	if (event == nullptr) return FALSE;
	return countEventName(event->getEventName()) > 0;
}

Bool OpenALAudioManager::isObjectPlayingVoice(UnsignedInt objID) const
{
	if (objID == 0) return FALSE;
	auto walk = [&](const std::list<PlayingAudio*>& list) {
		for (const PlayingAudio* pa : list) {
			if (!pa || !pa->event) continue;
			const AudioEventInfo* info = pa->event->getAudioEventInfo();
			if (!info || !(info->m_type & ST_VOICE)) continue;
			// Const getter on AudioEventRTS isn't available for ObjectID; cast
			// away const locally since the query is read-only semantically.
			AudioEventRTS* mutable_event = const_cast<AudioEventRTS*>(pa->event);
			if (static_cast<UnsignedInt>(mutable_event->getObjectID()) == objID) return true;
		}
		return false;
	};
	if (walk(m_playingSounds)) return TRUE;
	if (walk(m_playingMusic)) return TRUE;
	if (walk(m_playingSpeech)) return TRUE;
	return FALSE;
}

Bool OpenALAudioManager::doesViolateLimit(AudioEventRTS* event) const
{
	if (event == nullptr) return FALSE;
	const AudioEventInfo* info = event->getAudioEventInfo();
	if (info == nullptr || info->m_limit <= 0) return FALSE;

	const Int count = countEventName(event->getEventName());
	if (count < info->m_limit) return FALSE;

	// AC_INTERRUPT means "over-limit is OK as long as I kill the oldest one".
	// The actual kill happens in the caller; we just signal non-violation.
	if (BitIsSet(info->m_control, AC_INTERRUPT)) {
		return FALSE;
	}
	return TRUE;
}

Bool OpenALAudioManager::isPlayingLowerPriority(AudioEventRTS* event) const
{
	if (event == nullptr) return FALSE;
	const AudioPriority myPriority = event->getAudioPriority();
	auto walk = [&](const std::list<PlayingAudio*>& list) {
		for (const PlayingAudio* pa : list) {
			if (pa && pa->event && pa->event->getAudioPriority() < myPriority) {
				return true;
			}
		}
		return false;
	};
	if (walk(m_playingSounds)) return TRUE;
	if (walk(m_playingSpeech)) return TRUE;
	return FALSE;
}

void OpenALAudioManager::killLowestPrioritySoundImmediately()
{
	// Find the lowest-priority entry across SFX + speech (music is not evicted)
	// and finalize it so the source returns to the pool.
	PlayingAudio* victim = nullptr;
	std::list<PlayingAudio*>* victimList = nullptr;
	std::list<PlayingAudio*>::iterator victimIt;

	auto scan = [&](std::list<PlayingAudio*>& list) {
		for (auto it = list.begin(); it != list.end(); ++it) {
			PlayingAudio* pa = *it;
			if (!pa || !pa->event) continue;
			if (!victim
				|| pa->event->getAudioPriority() < victim->event->getAudioPriority())
			{
				victim = pa;
				victimList = &list;
				victimIt = it;
			}
		}
	};
	scan(m_playingSounds);
	scan(m_playingSpeech);

	if (victim && victimList) {
		finalizeEntry(*victimList, victimIt);
	}
}

// ============================================================================
// friend_forcePlayAudioEventRTS — synchronous, bypasses the request queue.
// ============================================================================

void OpenALAudioManager::friend_forcePlayAudioEventRTS(const AudioEventRTS* eventToPlay)
{
	if (eventToPlay == nullptr) return;

	// Resolve metadata if the caller handed us a bare event.
	if (!eventToPlay->getAudioEventInfo()) {
		getInfoForAudioEvent(eventToPlay);
		if (!eventToPlay->getAudioEventInfo()) {
			DEBUG_CRASH(("No info for forced audio event '%s'",
				eventToPlay->getEventName().str()));
			return;
		}
	}

	switch (eventToPlay->getAudioEventInfo()->m_soundType)
	{
		case AT_Music:
			if (!isOn(AudioAffect_Music)) return;
			break;
		case AT_SoundEffect:
			if (!isOn(AudioAffect_Sound) || !isOn(AudioAffect_Sound3D)) return;
			break;
		case AT_Streaming:
			if (!isOn(AudioAffect_Speech)) return;
			break;
	}

	// Heap-allocate a mutable copy: playAudioEvent takes ownership and will
	// releaseAudioEventRTS it on completion. Matches the lifecycle that the
	// base addAudioEvent uses at GameAudio.cpp:440.
	AudioEventRTS* ev = MSGNEW("AudioEventRTS") AudioEventRTS(*eventToPlay);
	ev->generateFilename();
	ev->generatePlayInfo();

	for (std::list<std::pair<AsciiString, Real> >::iterator it = m_adjustedVolumes.begin();
		 it != m_adjustedVolumes.end(); ++it)
	{
		if (it->first == ev->getEventName()) {
			ev->setVolume(it->second);
			break;
		}
	}

	playAudioEvent(ev);
}
