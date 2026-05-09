#pragma once

#include <SDL2/SDL.h>
#include <string>
#include <deque>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
}

class AudioDecoder {
public:
    AudioDecoder();
    ~AudioDecoder();

    bool Open(const std::string& path);
    void Close();

    void StartAudio();
    void StopAudio();
    void PauseAudio(bool pause);

    bool Seek(double seconds);

    double GetDuration()    const { return mDuration; }
    double GetCurrentTime() const { return mCurrentTime; }
    bool   IsPlaying()      const { return mAudioDevice > 0 && !mPaused; }
    bool   IsPaused()       const { return mPaused; }
    bool   IsOpen()         const { return mFormatCtx != nullptr; }

    friend void AudioDecoderCallback(void* userdata, Uint8* stream, int len);

private:
    static int     ReadPacket(void* opaque, uint8_t* buf, int buf_size);
    static int64_t SeekIO(void* opaque, int64_t offset, int whence);

    // Decodes the next chunk of audio into mAudioBuffer
    int DecodeNextChunk();

    AVFormatContext* mFormatCtx;
    AVCodecContext*  mCodecCtx;
    SwrContext*      mSwrCtx;
    AVIOContext*     mAvioCtx;

    AVFrame*  mFrame;
    AVPacket* mPacket;

    int    mAudioStreamIndex;
    double mDuration;
    double mCurrentTime;

    // Decodeds the PCM ring buffer
    uint8_t* mAudioBuffer;
    int      mAudioBufferSize;
    int      mAudioBufferIndex;

    SDL_AudioDeviceID mAudioDevice;
    SDL_mutex*        mMutex;
    bool              mPaused;
    bool              mEOF;

    FILE*    mFile;
    uint8_t* mAvioBuffer;
};
