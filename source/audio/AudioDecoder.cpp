#include "AudioDecoder.hpp"
#include <whb/log.h>
#include <cstring>
#include <algorithm>

// SDL audio callback – called from the SDL audio thread
void AudioDecoderCallback(void* userdata, Uint8* stream, int len) {
    AudioDecoder* dec = static_cast<AudioDecoder*>(userdata);
    SDL_memset(stream, 0, len);

    int remaining = len;
    Uint8* dst = stream;

    while (remaining > 0) {
        SDL_LockMutex(dec->mMutex);

        // Refills the buffer if empty
        while (dec->mAudioBufferIndex >= dec->mAudioBufferSize && !dec->mEOF) {
            SDL_UnlockMutex(dec->mMutex);
            int decoded = dec->DecodeNextChunk();
            SDL_LockMutex(dec->mMutex);
            if (decoded <= 0) {
                dec->mEOF = true;
                break;
            }
        }

        if (dec->mAudioBufferIndex >= dec->mAudioBufferSize) {
            SDL_UnlockMutex(dec->mMutex);
            break;
        }

        int available = dec->mAudioBufferSize - dec->mAudioBufferIndex;
        int toCopy    = std::min(available, remaining);

        SDL_memcpy(dst, dec->mAudioBuffer + dec->mAudioBufferIndex, toCopy);
        dec->mAudioBufferIndex += toCopy;
        dst       += toCopy;
        remaining -= toCopy;

        SDL_UnlockMutex(dec->mMutex);
    }
}

// Custom I/O callbacks for FFmpeg
int AudioDecoder::ReadPacket(void* opaque, uint8_t* buf, int buf_size) {
    FILE* f = static_cast<FILE*>(opaque);
    int n = (int)fread(buf, 1, buf_size, f);
    return (n == 0) ? AVERROR_EOF : n;
}

int64_t AudioDecoder::SeekIO(void* opaque, int64_t offset, int whence) {
    FILE* f = static_cast<FILE*>(opaque);
    if (whence == AVSEEK_SIZE) {
        long pos = ftell(f);
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, pos, SEEK_SET);
        return size;
    }
    return (fseek(f, (long)offset, whence) == 0) ? ftell(f) : -1;
}

AudioDecoder::AudioDecoder()
    : mFormatCtx(nullptr), mCodecCtx(nullptr), mSwrCtx(nullptr),
      mAvioCtx(nullptr), mFrame(nullptr), mPacket(nullptr),
      mAudioStreamIndex(-1), mDuration(0.0), mCurrentTime(0.0),
      mAudioBuffer(nullptr), mAudioBufferSize(0), mAudioBufferIndex(0),
      mAudioDevice(0), mMutex(nullptr), mPaused(false), mEOF(false),
      mFile(nullptr), mAvioBuffer(nullptr) {
    mMutex = SDL_CreateMutex();
}

AudioDecoder::~AudioDecoder() {
    Close();
    if (mMutex) {
        SDL_DestroyMutex(mMutex);
        mMutex = nullptr;
    }
}

bool AudioDecoder::Open(const std::string& path) {
    WHBLogPrintf("AudioDecoder::Open: %s", path.c_str());

    mFile = fopen(path.c_str(), "rb");
    if (!mFile) {
        WHBLogPrintf("AudioDecoder::Open: fopen failed");
        return false;
    }

    const int avioBufSize = 32768;
    mAvioBuffer = (uint8_t*)av_malloc(avioBufSize);
    if (!mAvioBuffer) {
        WHBLogPrintf("AudioDecoder::Open: av_malloc failed");
        fclose(mFile); mFile = nullptr;
        return false;
    }

    mAvioCtx = avio_alloc_context(mAvioBuffer, avioBufSize, 0, mFile,
                                   &AudioDecoder::ReadPacket, nullptr,
                                   &AudioDecoder::SeekIO);
    if (!mAvioCtx) {
        WHBLogPrintf("AudioDecoder::Open: avio_alloc_context failed");
        av_free(mAvioBuffer); mAvioBuffer = nullptr;
        fclose(mFile); mFile = nullptr;
        return false;
    }

    mFormatCtx = avformat_alloc_context();
    if (!mFormatCtx) {
        WHBLogPrintf("AudioDecoder::Open: avformat_alloc_context failed");
        Close();
        return false;
    }
    mFormatCtx->pb = mAvioCtx;

    int ret = avformat_open_input(&mFormatCtx, nullptr, nullptr, nullptr);
    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        WHBLogPrintf("AudioDecoder::Open: avformat_open_input failed: %s", errbuf);
        Close();
        return false;
    }

    if (avformat_find_stream_info(mFormatCtx, nullptr) < 0) {
        WHBLogPrintf("AudioDecoder::Open: avformat_find_stream_info failed");
        Close();
        return false;
    }

    // Finds the audio stream
    for (unsigned i = 0; i < mFormatCtx->nb_streams; i++) {
        if (mFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            mAudioStreamIndex = (int)i;
            break;
        }
    }
    if (mAudioStreamIndex < 0) {
        WHBLogPrintf("AudioDecoder::Open: No audio stream found");
        Close();
        return false;
    }

    AVCodecParameters* par = mFormatCtx->streams[mAudioStreamIndex]->codecpar;
    WHBLogPrintf("AudioDecoder::Open: Audio codec: %s  rate=%d  ch=%d",
                 avcodec_get_name(par->codec_id), par->sample_rate, par->channels);

    const AVCodec* codec = avcodec_find_decoder(par->codec_id);
    if (!codec) {
        WHBLogPrintf("AudioDecoder::Open: No decoder for codec %s", avcodec_get_name(par->codec_id));
        Close();
        return false;
    }

    mCodecCtx = avcodec_alloc_context3(codec);
    if (!mCodecCtx) { Close(); return false; }

    if (avcodec_parameters_to_context(mCodecCtx, par) < 0) { Close(); return false; }
    if (avcodec_open2(mCodecCtx, codec, nullptr) < 0) {
        WHBLogPrintf("AudioDecoder::Open: avcodec_open2 failed");
        Close();
        return false;
    }

    // Resampler to convert to stereo for SDL
    mSwrCtx = swr_alloc();
    if (!mSwrCtx) { Close(); return false; }

    int64_t inLayout = mCodecCtx->channel_layout
                       ? mCodecCtx->channel_layout
                       : av_get_default_channel_layout(mCodecCtx->channels);

    av_opt_set_int(mSwrCtx, "in_channel_layout",  inLayout,                    0);
    av_opt_set_int(mSwrCtx, "out_channel_layout", AV_CH_LAYOUT_STEREO,         0);
    av_opt_set_int(mSwrCtx, "in_sample_rate",     mCodecCtx->sample_rate,      0);
    av_opt_set_int(mSwrCtx, "out_sample_rate",    44100,                        0);
    av_opt_set_sample_fmt(mSwrCtx, "in_sample_fmt",  mCodecCtx->sample_fmt,    0);
    av_opt_set_sample_fmt(mSwrCtx, "out_sample_fmt", AV_SAMPLE_FMT_S16,        0);

    if (swr_init(mSwrCtx) < 0) {
        WHBLogPrintf("AudioDecoder::Open: swr_init failed");
        Close();
        return false;
    }

    // Duration handling
    if (mFormatCtx->duration != AV_NOPTS_VALUE)
        mDuration = mFormatCtx->duration / (double)AV_TIME_BASE;

    // Allocation for frame/packet 
    mFrame  = av_frame_alloc();
    mPacket = av_packet_alloc();
    if (!mFrame || !mPacket) { Close(); return false; }

    // Audio buffer
    int bufBytes = 44100 * 2 * 2 * 2; // 2 sec * 2 ch * 2 bytes/sample
    mAudioBuffer = (uint8_t*)av_malloc(bufBytes);
    if (!mAudioBuffer) { Close(); return false; }
    mAudioBufferSize  = 0;
    mAudioBufferIndex = 0;

    WHBLogPrintf("AudioDecoder::Open: Success. Duration=%.2fs", mDuration);
    return true;
}

void AudioDecoder::Close() {
    StopAudio();

    SDL_LockMutex(mMutex);

    if (mAudioBuffer) {
        av_free(mAudioBuffer);
        mAudioBuffer      = nullptr;
        mAudioBufferSize  = 0;
        mAudioBufferIndex = 0;
    }

    if (mFrame)  { av_frame_free(&mFrame);   }
    if (mPacket) { av_packet_free(&mPacket); }

    if (mSwrCtx) { swr_free(&mSwrCtx); }

    if (mCodecCtx) { avcodec_free_context(&mCodecCtx); }

    if (mAvioCtx) {
        av_freep(&mAvioCtx->buffer);
        avio_context_free(&mAvioCtx);
        mAvioBuffer = nullptr;
    }

    if (mFormatCtx) { avformat_close_input(&mFormatCtx); }

    if (mFile) { fclose(mFile); mFile = nullptr; }

    mAudioStreamIndex = -1;
    mDuration         = 0.0;
    mCurrentTime      = 0.0;
    mEOF              = false;
    mPaused           = false;

    SDL_UnlockMutex(mMutex);
}

int AudioDecoder::DecodeNextChunk() {
    if (!mFormatCtx || !mCodecCtx) return -1;

    // Read audio packets from the stream
    while (true) {
        int ret = av_read_frame(mFormatCtx, mPacket);
        if (ret == AVERROR_EOF || ret < 0) {
            avcodec_send_packet(mCodecCtx, nullptr);
        } else if (mPacket->stream_index != mAudioStreamIndex) {
            av_packet_unref(mPacket);
            continue;
        } else {
            ret = avcodec_send_packet(mCodecCtx, mPacket);
            av_packet_unref(mPacket);
            if (ret < 0) return -1;
        }

        ret = avcodec_receive_frame(mCodecCtx, mFrame);
        if (ret == AVERROR(EAGAIN)) continue;
        if (ret == AVERROR_EOF)     return -1;
        if (ret < 0)                return -1;

        // Update current time from  (needed for MPEG synchronization)
        if (mFrame->pts != AV_NOPTS_VALUE) {
            mCurrentTime = mFrame->pts *
                av_q2d(mFormatCtx->streams[mAudioStreamIndex]->time_base);
        }

        int outSamples = swr_get_out_samples(mSwrCtx, mFrame->nb_samples);
        int bufBytes   = outSamples * 2 * 2;

        if (bufBytes > (44100 * 2 * 2 * 2)) {
            bufBytes = 44100 * 2 * 2 * 2;
        }

        uint8_t* outPtr = mAudioBuffer;
        int converted = swr_convert(mSwrCtx, &outPtr, outSamples,
                                    (const uint8_t**)mFrame->data,
                                    mFrame->nb_samples);
        if (converted < 0) return -1;

        mAudioBufferSize  = converted * 2 * 2;
        mAudioBufferIndex = 0;

        av_frame_unref(mFrame);
        return mAudioBufferSize;
    }
}

bool AudioDecoder::Seek(double seconds) {
    if (!mFormatCtx || mAudioStreamIndex < 0) return false;

    SDL_LockMutex(mMutex);

    AVStream* st = mFormatCtx->streams[mAudioStreamIndex];
    int64_t ts   = (int64_t)(seconds / av_q2d(st->time_base));

    int ret = av_seek_frame(mFormatCtx, mAudioStreamIndex, ts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        // Fallback if issues arise : seek on the default stream instead
        int64_t tsAVT = (int64_t)(seconds * AV_TIME_BASE);
        ret = av_seek_frame(mFormatCtx, -1, tsAVT, AVSEEK_FLAG_BACKWARD);
    }

    if (ret >= 0) {
        avcodec_flush_buffers(mCodecCtx);
        mAudioBufferSize  = 0;
        mAudioBufferIndex = 0;
        mCurrentTime      = seconds;
        mEOF              = false;
    }

    SDL_UnlockMutex(mMutex);
    return ret >= 0;
}

// Start/Stop/Pause audio handling
void AudioDecoder::StartAudio() {
    if (mAudioDevice > 0) return;

    SDL_AudioSpec want{}, got{};
    want.freq     = 44100;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = 2048;
    want.callback = AudioDecoderCallback;
    want.userdata = this;

    mAudioDevice = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);
    if (mAudioDevice == 0) {
        WHBLogPrintf("AudioDecoder::StartAudio: SDL_OpenAudioDevice failed: %s", SDL_GetError());
        return;
    }

    mPaused = false;
    mEOF    = false;
    SDL_PauseAudioDevice(mAudioDevice, 0);
    WHBLogPrintf("AudioDecoder::StartAudio: Playing on device %u", mAudioDevice);
}

void AudioDecoder::StopAudio() {
    if (mAudioDevice > 0) {
        SDL_CloseAudioDevice(mAudioDevice);
        mAudioDevice = 0;
        WHBLogPrintf("AudioDecoder::StopAudio: Device closed");
    }
    mPaused = false;
}

void AudioDecoder::PauseAudio(bool pause) {
    if (mAudioDevice > 0) {
        SDL_PauseAudioDevice(mAudioDevice, pause ? 1 : 0);
        mPaused = pause;
    }
}
