#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_system.h>
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
#include <libavutil/buffer.h>
#include <libavutil/dict.h>
#include <libavutil/opt.h>
}

class VideoDecoder {
public:
    enum class DecodeMode {
        Hardware = 0,
        HardwareNoBFrames = 1,
        Software = 2
    };

    VideoDecoder();
    ~VideoDecoder();
    
    bool Open(const std::string& path);
    void Close();

    static bool HardwareDecodeAvailable();
    static bool HardwareCanDecode(AVCodecParameters* params);
    static const char* DecodeModeName(DecodeMode mode);
    static DecodeMode GetDecodeMode();
    static void SetDecodeMode(DecodeMode mode);

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
    bool IsHardwareDecoding() const { return mDecodeMode != DecodeMode::Software; }
    bool IsNV12Output() const {
        return mVideoCodecCtx && mVideoCodecCtx->pix_fmt == AV_PIX_FMT_NV12;
    }
    bool IsPresentingNV12() const { return mUseNV12; }
    void SetPresentingNV12(bool presenting) { mUseNV12 = presenting; }
    DecodeMode GetActiveDecodeMode() const { return mDecodeMode; }
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
        uint8_t* pixels;
        size_t size;
        int pitch;
        uint8_t* y;
        uint8_t* uv;
        AVBufferRef* hold;
        double pts;
        int epoch;
    };

    static constexpr size_t kMaxReadyFrames = 8;
    static constexpr size_t kMaxReadyBytes = 12 * 1024 * 1024;
    static constexpr size_t kMaxVideoPacketQueue = 60;
    static constexpr Uint32 kFrameWorkerIdleDelayMs = 2;
    static constexpr Uint32 kReaderIdleDelayMs = 50;
    static constexpr Uint32 kReadyQueueBackpressureDelayMs = 5;
    static constexpr double kDecodeAheadSeconds = 0.30;
    static constexpr Uint32 kSeekEpochStep = 1;
    static constexpr int kHardwareWatchdogPackets = 120;

    bool OpenVideoDecoder(DecodeMode mode);
    bool ReopenVideoDecoder();
    bool SyncSwsContext(int width, int height);
    static DecodeMode DemoteDecodeMode(DecodeMode mode);
    static void DemoteToNextMode();

    bool PopReadyFrame(DecodedVideoFrame& frame, double targetPts = -1.0);
    bool QueueReadyFrame(DecodedVideoFrame&& frame);
    void ClearReadyFrames();
    void ReleaseReadyFrame(DecodedVideoFrame& frame);
    bool HasReadyFrameRoom() const;
    size_t ReadyFrameBytes() const;
    uint8_t* AcquireFrameBuffer(size_t size);
    void RecycleFrameBuffer(uint8_t* pixels);
    void FreeFrameBuffers();
    void HoldForGpu(AVBufferRef* hold);
    void ReleaseGpuFrames();

    static int ReadPacket(void* opaque, uint8_t* buf, int buf_size);
    static int64_t Seek(void* opaque, int64_t offset, int whence);

    AVFormatContext* mFormatCtx;
    AVCodecContext* mVideoCodecCtx;
    AVCodecContext* mAudioCodecCtx;
    struct SwsContext* mSwsCtx;
    struct SwrContext* mSwrCtx;
    AVIOContext* mAvioCtx;

    DecodeMode mDecodeMode;
    int mVideoPacketsDecoded;
    int mVideoPicturesDecoded;

    bool mUseNV12;

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

    int mSwsWidth;
    int mSwsHeight;

    struct FrameBuffer {        uint8_t* data;
        size_t size;
    };
    std::vector<FrameBuffer> mFrameBufferPool;
    std::vector<FrameBuffer> mFrameBuffersInUse;

    static constexpr size_t kGpuHeldFrames = 3;
    AVBufferRef* mGpuHeldFrames[kGpuHeldFrames] = {};
    size_t mGpuHeldIndex = 0;

    Uint64 mStatWindowStart;
    Uint64 mStatDecodeUs;
    Uint64 mStatScaleUs;
    Uint64 mStatWaitUs;
    int mStatFrames;
    Uint64 mStatLastDecodeStart;
    void ReportFrameWorkerStats(bool finalReport);

    void* mPresentedTexture;
    int mPresentedUseNV12;
    uintptr_t mPresentedY;
    uintptr_t mPresentedUV;
    int mPresentedPitch;
    int mPresentedCount;

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
