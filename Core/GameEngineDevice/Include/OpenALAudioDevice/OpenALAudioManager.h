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

// OpenALAudioManager — cross-platform audio backend.
//
// Phase 1 landed:
//   - ALC device + context lifecycle
//   - ALC device enumeration surfaced through the AudioManager provider API
//   - Video-audio interop via OpenALAudioStream (used by FFmpegVideoPlayer)
//   - Listener position + global volume tracking
//
// Phase 2 (this file): 2D game SFX + music playback through the AudioEventRTS
// pipeline, enough to drive the shell/main-menu audio. PCM decoding is
// delegated to FFmpegFile (see OpenALAudioFileCache); OpenAL only sees the
// resulting PCM16 blob.
//
// ToDo — follow-up phase(s): 3D positional audio (distance attenuation + pan),
// AT_Streaming speech, EFX reverb, priority/limit culling, and speaker-surround
// downmix. The corresponding overrides below still return neutral defaults.

#pragma once

#include "Common/AsciiString.h"
#include "Common/AudioAffect.h"
#include "Common/AudioEventInfo.h"
#include "Common/AudioEventRTS.h"
#include "Common/GameAudio.h"

#include <AL/al.h>
#include <AL/alc.h>

#include <list>
#include <vector>

class OpenALAudioStream;
class OpenALAudioFileCache;

class OpenALAudioManager : public AudioManager
{
public:
	OpenALAudioManager();
	virtual ~OpenALAudioManager() override;

#if defined(RTS_DEBUG)
	virtual void audioDebugDisplay(DebugDisplayInterface* dd, void* userData, FILE* fp = nullptr) override {}
#endif

	// SubsystemInterface
	virtual void init() override;
	virtual void postProcessLoad() override;
	virtual void reset() override;
	virtual void update() override;

	// Device lifecycle
	virtual void openDevice() override;
	virtual void closeDevice() override;
	virtual void* getDevice() override { return m_device; }

	// Playback control — walk m_playing* and drive AL source state.
	virtual void stopAudio(AudioAffect which) override;
	virtual void pauseAudio(AudioAffect which) override;
	virtual void resumeAudio(AudioAffect which) override;
	virtual void pauseAmbient(Bool shouldPause) override;

	virtual void killAudioEventImmediately(AudioHandle audioEvent) override;

	// Music
	virtual void nextMusicTrack() override;
	virtual void prevMusicTrack() override;
	virtual Bool isMusicPlaying() const override;
	virtual Bool hasMusicTrackCompleted(const AsciiString& trackName, Int numberOfTimes) const override;
	virtual AsciiString getMusicTrackName() const override;

	// Listener — called by AudioManager::update(). We forward immediately into
	// the backend so positional audio reflects the current tactical camera.
	virtual void setListenerPosition(const Coord3D* newListenerPos, const Coord3D* newListenerOrientation) override;

	// Provider enumeration (one entry per ALC device name)
	virtual UnsignedInt getProviderCount() const override;
	virtual AsciiString getProviderName(UnsignedInt providerNum) const override;
	virtual UnsignedInt getProviderIndex(AsciiString providerName) const override;
	virtual void selectProvider(UnsignedInt providerNdx) override;
	virtual void unselectProvider() override;
	virtual UnsignedInt getSelectedProvider() const override { return m_selectedProvider; }
	virtual void setSpeakerType(UnsignedInt speakerType) override { m_speakerType = speakerType; }
	virtual UnsignedInt getSpeakerType() override { return m_speakerType; }

	virtual void notifyOfAudioCompletion(UnsignedInt audioCompleted, UnsignedInt flags) override {}

	// Sample pool limits (surfaced to SoundManager for culling heuristics).
	virtual UnsignedInt getNum2DSamples() const override { return 32; }
	virtual UnsignedInt getNum3DSamples() const override { return 32; }
	virtual UnsignedInt getNumStreams() const override { return 4; }

	virtual Bool doesViolateLimit(AudioEventRTS* event) const override;
	virtual Bool isPlayingLowerPriority(AudioEventRTS* event) const override;
	virtual Bool isPlayingAlready(AudioEventRTS* event) const override;
	virtual Bool isObjectPlayingVoice(UnsignedInt objID) const override;

	virtual void adjustVolumeOfPlayingAudio(AsciiString eventName, Real newVolume) override;
	virtual void removePlayingAudio(AsciiString eventName) override;
	virtual void removeAllDisabledAudio() override {}

	virtual Bool has3DSensitiveStreamsPlaying() const override { return false; }

	// Video-audio interop. Returns OpenALAudioStream* usable by FFmpegVideoPlayer.
	virtual void* getVideoAudioStreamHandle() override;
	virtual void releaseVideoAudioStreamHandle() override;

	virtual void friend_forcePlayAudioEventRTS(const AudioEventRTS* eventToPlay) override;

	virtual void setPreferredProvider(AsciiString providerNdx) override { m_prefProvider = providerNdx; }
	virtual void setPreferredSpeaker(AsciiString speakerType) override { m_prefSpeaker = speakerType; }

	virtual Real getFileLengthMS(AsciiString strToLoad) const override;
	virtual void closeAnySamplesUsingFile(const void* fileToClose) override {}

	// AudioManager overrides that need backend-aware behaviour
	virtual void setVolume(Real volume, AudioAffect whichToAffect) override;
	virtual void processRequestList() override;

	// PCM format helper consumed by FFmpegVideoPlayer and OpenALAudioFileCache.
	static ALenum getALFormat(Int numChannels, Int bitsPerSample);

protected:
	virtual void setDeviceListenerPosition() override;

	void enumerateProviders();

	// -- Phase 2: game-audio playback -----------------------------------------

	struct PlayingAudio
	{
		ALuint         source       = 0;
		ALuint         buffer       = 0;    // owned by m_fileCache, do NOT alDelete here
		AudioEventRTS* event        = nullptr;
		AudioType      type         = AT_SoundEffect;
		AudioAffect    affect       = AudioAffect_Sound;
		Bool           paused       = FALSE;
		Bool           positional   = FALSE; // 3D world-anchored; update AL_POSITION every frame
		Bool           wantsPortion = FALSE; // event has attack/decay sounds; advance on STOP
	};

	// Called from update(); separate helpers mirror MilesAudioManager.
	void processPlayingList();

	// Find a free source (either from pool or by stealing the oldest one-shot);
	// returns 0 if none can be freed.
	ALuint acquireSource();
	void   releaseSource(ALuint source);

	// Start playback for a freshly received AR_Play request. Takes ownership of
	// the AudioEventRTS* (will releaseAudioEventRTS when playback finishes).
	void playAudioEvent(AudioEventRTS* event);

	// AR_Stop / AR_Pause dispatch by handle.
	void stopAudioByHandle(AudioHandle handle, Bool immediate);
	void pauseAudioByHandle(AudioHandle handle);

	// Walk all playing lists and stop/pause/resume entries whose affect
	// intersects `which`.
	void stopOrFadeByAffect(AudioAffect which);
	void pauseByAffect(AudioAffect which);
	void resumeByAffect(AudioAffect which);

	// Detach a completed/cancelled entry: stop the source, return it to the
	// pool, close the cache ref, release the event. Iterator-safe: returns
	// the iterator to the next element.
	std::list<PlayingAudio*>::iterator finalizeEntry(std::list<PlayingAudio*>& list,
	                                                  std::list<PlayingAudio*>::iterator it);

	// Push AL_GAIN / AL_PITCH on one playing entry from current category and
	// event state. Called on setVolume and whenever a fresh source is spun up.
	void applyVolume(PlayingAudio* pa);

	// Three-tier volume math mirroring MilesAudioManager::getEffectiveVolume:
	// event volume * event shift * category (system * script) volume.
	Real effectiveVolume(const AudioEventRTS* event) const;

	// Map AudioType to the AudioAffect bit we're going to query for on/volume.
	static AudioAffect affectFor(AudioType t);

	// Miles-parity helpers for the request queue: frame-based delay deferral.
	Bool shouldProcessRequestThisFrame(const AudioRequest* req) const;
	void adjustRequest(AudioRequest* req);

	// Select filename by current PortionToPlay — PP_Attack/PP_Sound/PP_Decay
	// map onto getAttackFilename/getFilename/getDecayFilename. Returns empty
	// for PP_Done.
	AsciiString filenameForPortion(AudioEventRTS* event) const;

	// Called when a positional / multi-portion / looping source hits AL_STOPPED.
	// Either rewinds+replays the same buffer, swaps to the next portion's file,
	// or returns false to signal that the caller should finalize the entry.
	Bool handleSourceStopped(PlayingAudio* pa);

	// Remove the lowest-priority playing entry (used by SoundManager after
	// isPlayingLowerPriority reports true). Mirrors Miles' same-named helper.
	void killLowestPrioritySoundImmediately();

	// Count active plays of `eventName` across all lists, including pending
	// requests that haven't been processed yet. Used by doesViolateLimit /
	// isPlayingAlready.
	Int countEventName(const AsciiString& eventName) const;

	// -- Phase 1 state --------------------------------------------------------

	ALCdevice*  m_device;
	ALCcontext* m_context;

	std::vector<AsciiString> m_providers;
	UnsignedInt m_selectedProvider;
	UnsignedInt m_speakerType;
	AsciiString m_prefProvider;
	AsciiString m_prefSpeaker;

	OpenALAudioStream* m_videoAudioStream;

	// -- Phase 2 state --------------------------------------------------------

	OpenALAudioFileCache* m_fileCache;

	// Pre-allocated AL sources; popped/pushed as one-shots play and complete.
	std::vector<ALuint> m_sourcePool;

	// Dedicated music source kept alive for the lifetime of the device so
	// looping doesn't compete for the general pool.
	ALuint m_musicSource;

	// Active playback. Music has its own list so track-boundary logic in
	// MusicManager can look up by AudioHandle without walking SFX.
	std::list<PlayingAudio*> m_playingSounds;
	std::list<PlayingAudio*> m_playingMusic;
	std::list<PlayingAudio*> m_playingSpeech;

	AsciiString m_currentMusicTrackName;

	// Bumped each time a non-looping music track reaches AL_STOPPED, keyed by
	// event name. Scripted "MusicHasCompletedSomeAmount" conditions read this.
	std::hash_map<AsciiString, UnsignedInt,
	              rts::hash<AsciiString>, rts::equal_to<AsciiString> > m_musicCompletions;
};

// Headless variant: never opens an ALC device. Mirrors MilesAudioManagerDummy so
// tests and dedicated servers don't need a working audio card.
class OpenALAudioManagerNull : public OpenALAudioManager
{
public:
	virtual void openDevice() override {}
	virtual void closeDevice() override {}
	virtual void* getDevice() override { return nullptr; }
	virtual void* getVideoAudioStreamHandle() override { return nullptr; }
	virtual void releaseVideoAudioStreamHandle() override {}
	virtual void setDeviceListenerPosition() override {}
	virtual Real getFileLengthMS(AsciiString strToLoad) const override;
};
