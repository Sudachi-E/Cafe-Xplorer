#pragma once

#include <SDL2/SDL.h>
#include <string>
#include <cstdio>
#include <queue>
#include <deque>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/dict.h>
#include <libavutil/opt.h>
}

class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();
    
    bool Open(const std::string& path);
    void Close();
    
    bool ReadFrame(SDL_Texture* texture);
    bool Seek(double seconds);
    void SetCurrentTime(double time) { mCurrentTime = time; }
    
    int GetWidth() const { return mWidth; }
    int GetHeight() const { return mHeight; }
    double GetDuration() const { return mDuration; }
    double GetCurrentTime() const { return mCurrentTime; }
    double GetAudioTime() const { return mAudioTime; }
    double GetFrameRate() const;
    bool HasAudio() const { return mAudioStreamIndex >= 0; }
    bool HasVideo() const { return mVideoStreamIndex >= 0; }
    bool IsRawVideo() const { return mVideoCodecCtx && mVideoCodecCtx->codec_id == AV_CODEC_ID_RAWVIDEO; }
    AVCodecID GetVideoCodecID() const { return mVideoCodecCtx ? mVideoCodecCtx->codec_id : AV_CODEC_ID_NONE; }
    
    void StartAudio();
    void StopAudio();
    void PauseAudio(bool pause);
    bool IsAudioPlaying() const { return mAudioDevice > 0; }
    
    friend void AudioCallback(void* userdata, Uint8* stream, int len);
    
private:
    static int ReadPacket(void* opaque, uint8_t* buf, int buf_size);
    static int64_t Seek(void* opaque, int64_t offset, int whence);
    
    AVFormatContext* mFormatCtx;
    AVCodecContext* mVideoCodecCtx;
    AVCodecContext* mAudioCodecCtx;
    struct SwsContext* mSwsCtx;
    struct SwrContext* mSwrCtx;
    AVIOContext* mAvioCtx;
    
    AVFrame* mFrame;
    AVFrame* mFrameRGB;
    AVFrame* mAudioFrame;
    AVPacket* mPacket;
    
    int mVideoStreamIndex;
    int mAudioStreamIndex;
    int mWidth;
    int mHeight;
    double mDuration;
    double mCurrentTime;
    double mAudioTime;
    
    uint8_t* mBuffer;
    uint8_t* mAvioBuffer;
    uint8_t* mAudioBuffer;
    int mAudioBufferSize;
    int mAudioBufferIndex;
    
    SDL_AudioDeviceID mAudioDevice;
    SDL_mutex* mPacketMutex;
    AVPacket* mAudioPacket;
    std::deque<AVPacket*> mAudioPacketQueue;
    std::deque<AVPacket*> mVideoPacketQueue;
    
    SDL_Thread* mPacketReaderThread;
    SDL_atomic_t mReaderThreadRunning;
    static int PacketReaderThreadFunc(void* data);
    void PacketReaderLoop();
    
    FILE* mFile;
};
