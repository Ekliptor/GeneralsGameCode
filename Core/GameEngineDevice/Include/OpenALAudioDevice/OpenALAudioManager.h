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

// OpenALAudioManager — cross-platform audio backend (Phase 1 scaffolding).
//
// Scope landed in Phase 1:
//   - ALC device + context lifecycle
//   - ALC device enumeration surfaced through the AudioManager provider API
//   - Video-audio interop via OpenALAudioStream (used by FFmpegVideoPlayer)
//   - Listener position + global volume tracking
//
// ToDo — follow-up phase(s): full game SFX + music parity with MilesAudioManager
// (2D/3D sample pools, event routing, file cache, EFX reverb, priority culling,
// speaker-surround downmix). Stubs below return neutral defaults so the build
// matrix stays green while that work is scheduled.

#pragma once

#include "Common/AsciiString.h"
#include "Common/GameAudio.h"

#include <AL/al.h>
#include <AL/alc.h>

#include <vector>

class OpenALAudioStream;

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

	// Playback control — stubs for now, see ToDo above.
	virtual void stopAudio(AudioAffect which) override {}
	virtual void pauseAudio(AudioAffect which) override {}
	virtual void resumeAudio(AudioAffect which) override {}
	virtual void pauseAmbient(Bool shouldPause) override {}

	virtual void killAudioEventImmediately(AudioHandle audioEvent) override {}

	// Music
	virtual void nextMusicTrack() override {}
	virtual void prevMusicTrack() override {}
	virtual Bool isMusicPlaying() const override { return false; }
	virtual Bool hasMusicTrackCompleted(const AsciiString& trackName, Int numberOfTimes) const override { return false; }
	virtual AsciiString getMusicTrackName() const override { return AsciiString::TheEmptyString; }

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

	virtual Bool doesViolateLimit(AudioEventRTS* event) const override { return false; }
	virtual Bool isPlayingLowerPriority(AudioEventRTS* event) const override { return false; }
	virtual Bool isPlayingAlready(AudioEventRTS* event) const override { return false; }
	virtual Bool isObjectPlayingVoice(UnsignedInt objID) const override { return false; }

	virtual void adjustVolumeOfPlayingAudio(AsciiString eventName, Real newVolume) override {}
	virtual void removePlayingAudio(AsciiString eventName) override {}
	virtual void removeAllDisabledAudio() override {}

	virtual Bool has3DSensitiveStreamsPlaying() const override { return false; }

	// Video-audio interop. Returns OpenALAudioStream* usable by FFmpegVideoPlayer.
	virtual void* getVideoAudioStreamHandle() override;
	virtual void releaseVideoAudioStreamHandle() override;

	virtual void friend_forcePlayAudioEventRTS(const AudioEventRTS* eventToPlay) override {}

	virtual void setPreferredProvider(AsciiString providerNdx) override { m_prefProvider = providerNdx; }
	virtual void setPreferredSpeaker(AsciiString speakerType) override { m_prefSpeaker = speakerType; }

	virtual Real getFileLengthMS(AsciiString strToLoad) const override;
	virtual void closeAnySamplesUsingFile(const void* fileToClose) override {}

	// PCM format helper consumed by FFmpegVideoPlayer and any direct buffer uploader.
	static ALenum getALFormat(Int numChannels, Int bitsPerSample);

protected:
	virtual void setDeviceListenerPosition() override;

	void enumerateProviders();

	ALCdevice* m_device;
	ALCcontext* m_context;

	std::vector<AsciiString> m_providers;
	UnsignedInt m_selectedProvider;
	UnsignedInt m_speakerType;
	AsciiString m_prefProvider;
	AsciiString m_prefSpeaker;

	OpenALAudioStream* m_videoAudioStream;
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
