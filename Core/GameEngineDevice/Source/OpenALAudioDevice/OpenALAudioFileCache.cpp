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
#include "OpenALAudioDevice/OpenALAudioFileCache.h"
#include "OpenALAudioDevice/OpenALAudioManager.h"

#include "Common/File.h"
#include "Common/FileSystem.h"
#include "VideoDevice/FFmpeg/FFmpegFile.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

#include <vector>

namespace
{
	// Apple's OpenAL on Apple Silicon crashes inside
	// AudioConverterChain::PostBuild → caulk allocator when handed buffers at
	// off-rate sample rates (Generals content is largely 22050 Hz; modern
	// hardware mixes at 48000). Resample everything to a universally-accepted
	// rate in swr before alBufferData so OpenAL's internal converter chain has
	// at most a rate-matched pass to do. 44100 is the safest target: standard
	// CD rate, universally supported, and a clean integer multiple of 22050
	// (cheap upsample for the most common Generals content).
	constexpr Int kTargetSampleRate = 44100;

	// FFmpeg audio frames arrive in whatever sample format the codec decided;
	// OpenAL wants interleaved PCM16. Reuse the swr pattern from
	// FFmpegVideoPlayer.cpp::onFrame and accumulate everything into one blob.
	struct DecodeState
	{
		std::vector<uint8_t> pcm;
		SwrContext*          swr           = nullptr;
		AVSampleFormat       swrInFmt      = AV_SAMPLE_FMT_NONE;
		Int                  swrInChannels = 0;
		Int                  swrInRate     = 0;
		Int                  outChannels   = 0;
		Int                  outRate       = 0;
		Bool                 failed        = false;
	};

	// Append `samples` worth of swr output (sized for `outCh` interleaved S16)
	// onto st->pcm, growing the vector to the actual converted size. Pass
	// `nb_in` and `inData==nullptr` to drain swr's internal buffer at EOF.
	void appendSwrOutput(DecodeState* st, Int outCh, int nb_in, const uint8_t* const* inData)
	{
		// Ask swr how many output samples it could produce given the queued
		// input + this call. With resampling 22050→44100 a 1024-sample frame
		// can yield ~2048 output samples; sizing by nb_in alone underflows.
		const int est = swr_get_out_samples(st->swr, nb_in);
		const int outSamplesCap = (est > 0) ? est : (nb_in > 0 ? nb_in * 4 : 4096);
		const int allocBytes = outSamplesCap * outCh * static_cast<int>(sizeof(int16_t));
		if (allocBytes <= 0) {
			return;
		}

		const size_t priorSize = st->pcm.size();
		st->pcm.resize(priorSize + allocBytes);

		uint8_t* outPtrs[1] = { st->pcm.data() + priorSize };
		const int converted = swr_convert(st->swr, outPtrs, outSamplesCap, inData, nb_in);
		if (converted <= 0) {
			st->pcm.resize(priorSize);
			return;
		}

		const int actualBytes = converted * outCh * static_cast<int>(sizeof(int16_t));
		st->pcm.resize(priorSize + actualBytes);
	}

	void onAudioFrame(AVFrame* frame, int streamIdx, int streamType, void* userData)
	{
		(void)streamIdx;
		if (streamType != AVMEDIA_TYPE_AUDIO) {
			return;
		}
		DecodeState* st = static_cast<DecodeState*>(userData);
		if (st->failed) {
			return;
		}

		const AVSampleFormat inFmt = static_cast<AVSampleFormat>(frame->format);
		const Int inCh   = frame->ch_layout.nb_channels;
		const Int inRate = frame->sample_rate;

		// OpenAL can't consume anything wider than stereo — collapse everything
		// else down to stereo. Mirrors FFmpegVideoPlayer's channel handling.
		const Int outCh = (inCh == 1) ? 1 : 2;
		AVChannelLayout outLayout;
		av_channel_layout_default(&outLayout, outCh);

		if (st->swr == nullptr
			|| st->swrInFmt      != inFmt
			|| st->swrInChannels != inCh
			|| st->swrInRate     != inRate)
		{
			swr_free(&st->swr);
			const int swrErr = swr_alloc_set_opts2(&st->swr,
				&outLayout,        AV_SAMPLE_FMT_S16, kTargetSampleRate,
				&frame->ch_layout, inFmt,             inRate,
				0, nullptr);
			if (swrErr < 0 || st->swr == nullptr || swr_init(st->swr) < 0) {
				DEBUG_LOG(("OpenALAudioFileCache: swr_init failed (fmt=%d ch=%d→%d rate=%d→%d)",
					inFmt, inCh, outCh, inRate, kTargetSampleRate));
				swr_free(&st->swr);
				av_channel_layout_uninit(&outLayout);
				st->failed = true;
				return;
			}
			st->swrInFmt      = inFmt;
			st->swrInChannels = inCh;
			st->swrInRate     = inRate;
			st->outChannels   = outCh;
			st->outRate       = kTargetSampleRate;
		}
		av_channel_layout_uninit(&outLayout);

		appendSwrOutput(st, outCh, frame->nb_samples,
			const_cast<const uint8_t* const*>(frame->extended_data));
	}

	// After the decode loop, swr can hold up to ~one filter-tap window of
	// trailing samples internally. For short UI sounds those samples ARE the
	// audible tail; drain them with a NULL input pass.
	void drainSwr(DecodeState* st)
	{
		if (st == nullptr || st->swr == nullptr || st->failed) {
			return;
		}
		for (;;) {
			const size_t before = st->pcm.size();
			appendSwrOutput(st, st->outChannels, 0, nullptr);
			if (st->pcm.size() == before) {
				break;
			}
		}
	}
}

OpenALAudioFileCache::OpenALAudioFileCache()
{
}

OpenALAudioFileCache::~OpenALAudioFileCache()
{
	reset();
}

ALuint OpenALAudioFileCache::openBuffer(const AsciiString& path)
{
	if (path.isEmpty()) {
		return 0;
	}

	for (Entry& e : m_entries) {
		if (e.path == path) {
			++e.refCount;
			return e.buffer;
		}
	}

	File* file = TheFileSystem->openFile(path.str(), File::READ | File::BINARY | File::STREAMING);
	if (file == nullptr) {
		DEBUG_LOG(("OpenALAudioFileCache: could not open '%s'", path.str()));
		return 0;
	}

	// FFmpegFile takes ownership of the File* and will close it.
	FFmpegFile ffmpeg;
	if (!ffmpeg.open(file)) {
		DEBUG_LOG(("OpenALAudioFileCache: FFmpegFile.open failed for '%s'", path.str()));
		return 0;
	}

	if (!ffmpeg.hasAudio()) {
		DEBUG_LOG(("OpenALAudioFileCache: no audio stream in '%s'", path.str()));
		return 0;
	}

	DecodeState st;
	ffmpeg.setUserData(&st);
	ffmpeg.setFrameCallback(onAudioFrame);
	while (ffmpeg.decodePacket()) {
		if (st.failed) {
			break;
		}
	}
	drainSwr(&st);
	swr_free(&st.swr);

	if (st.failed || st.pcm.empty() || st.outRate <= 0 || st.outChannels <= 0) {
		DEBUG_LOG(("OpenALAudioFileCache: no decoded audio for '%s'", path.str()));
		return 0;
	}

	ALuint buffer = 0;
	alGenBuffers(1, &buffer);
	if (buffer == 0) {
		DEBUG_LOG(("OpenALAudioFileCache: alGenBuffers failed"));
		return 0;
	}

	const ALenum format = OpenALAudioManager::getALFormat(st.outChannels, 16);
	alBufferData(buffer, format, st.pcm.data(), static_cast<ALsizei>(st.pcm.size()), st.outRate);

	const ALenum err = alGetError();
	if (err != AL_NO_ERROR) {
		DEBUG_LOG(("OpenALAudioFileCache: alBufferData failed (0x%x) for '%s'", err, path.str()));
		alDeleteBuffers(1, &buffer);
		return 0;
	}

	Entry entry;
	entry.path     = path;
	entry.buffer   = buffer;
	entry.refCount = 1;
	m_entries.push_back(entry);
	return buffer;
}

void OpenALAudioFileCache::closeBuffer(ALuint buffer)
{
	if (buffer == 0) {
		return;
	}
	for (Entry& e : m_entries) {
		if (e.buffer == buffer) {
			if (e.refCount > 0) {
				--e.refCount;
			}
			return;
		}
	}
}

void OpenALAudioFileCache::reset()
{
	for (Entry& e : m_entries) {
		if (e.buffer != 0) {
			alDeleteBuffers(1, &e.buffer);
		}
	}
	m_entries.clear();
}
