#pragma once

#include <SDL2/SDL.h>
#include <string>
#include <cstdio>
#include <queue>
#include <deque>
#include <vector>
#include <utility>

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
    
    bool ReadFrame(SDL_Texture* texture, double targetPts = -1.0);
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
    bool HasReadyFrame() const;
    bool IsRawVideo() const { return mVideoCodecCtx && mVideoCodecCtx->codec_id == AV_CODEC_ID_RAWVIDEO; }
    AVCodecID GetVideoCodecID() const { return mVideoCodecCtx ? mVideoCodecCtx->codec_id : AV_CODEC_ID_NONE; }
    const std::string& GetFailedCodecName() const { return mFailedCodecName; }
    
    void StartAudio();
    void StopAudio();
    void PauseAudio(bool pause);
    void SetPlaybackArmed(bool armed);
    bool IsPlaybackArmed() { return SDL_AtomicGet(&mPlaybackArmed) != 0; }
    bool IsAudioPlaying() const { return mAudioDevice > 0; }
    
    friend void AudioCallback(void* userdata, Uint8* stream, int len);
    
private:
    struct DecodedVideoFrame {
        std::vector<uint8_t> pixels;
        int pitch;
        double pts;
        int epoch;
    };

    static constexpr size_t kMaxReadyFrames = 8;
    static constexpr size_t kMaxVideoPacketQueue = 60;
    static constexpr Uint32 kFrameWorkerIdleDelayMs = 2;
    static constexpr Uint32 kReaderIdleDelayMs = 50;
    static constexpr Uint32 kReadyQueueBackpressureDelayMs = 5;
    static constexpr double kDecodeAheadSeconds = 0.30; // ~0.3s max decode lead
    static constexpr Uint32 kSeekEpochStep = 1;

    bool PopReadyFrame(DecodedVideoFrame& frame, double targetPts = -1.0);
    bool QueueReadyFrame(DecodedVideoFrame&& frame);
    void ClearReadyFrames();

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
    uint8_t* mReadyBuf;
    size_t mReadyBufSize;

    SDL_AudioDeviceID mAudioDevice;
    SDL_mutex* mPacketMutex;
    SDL_mutex* mVideoDecodeMutex;
    AVPacket* mAudioPacket;
    std::deque<AVPacket*> mAudioPacketQueue;
    std::deque<AVPacket*> mVideoPacketQueue;
    std::deque<DecodedVideoFrame> mReadyFrameQueue;

    SDL_Thread* mPacketReaderThread;
    SDL_atomic_t mReaderThreadRunning;
    SDL_atomic_t mReaderReachedEOF;
    SDL_Thread* mFrameWorkerThread;
    SDL_atomic_t mFrameWorkerRunning;
    SDL_atomic_t mPlaybackArmed;
    SDL_atomic_t mDecodeEpoch;
    static int PacketReaderThreadFunc(void* data);
    static int FrameWorkerThreadFunc(void* data);
    void PacketReaderLoop();
    void FrameWorkerLoop();

    std::string mFailedCodecName;

    FILE* mFile;
};
