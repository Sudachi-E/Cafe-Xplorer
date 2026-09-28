#include "VideoDecoder.hpp"
#include <whb/log.h>
#include <coreinit/cache.h>
#include <malloc.h>

static const char* const kHardwareDecoderName = "h264_wiiu";

static VideoDecoder::DecodeMode s_decodeMode = VideoDecoder::DecodeMode::Hardware;

static constexpr size_t kHardwareFrameAlignment = 1024;

namespace {

struct HardwareFrames {
    int size = 0;
    std::vector<AVBufferRef*> spare;
};

HardwareFrames* GetHardwareFrames(AVCodecContext* ctx) {
    return static_cast<HardwareFrames*>(ctx->opaque);
}

void FreeHardwareFrame(void* opaque, uint8_t* data) {
    free(data);
}

AVBufferRef* AllocHardwareFrame(int size) {
    void* data = memalign(kHardwareFrameAlignment, size);
    if (!data) {
        return nullptr;
    }
    DCFlushRange(data, size);
    AVBufferRef* ref = av_buffer_create((uint8_t*)data, size, FreeHardwareFrame, nullptr, 0);
    if (!ref) {
        free(data);
    }
    return ref;
}

int GetHardwareFrame(AVCodecContext* ctx, AVFrame* frame, int flags) {
    HardwareFrames* frames = GetHardwareFrames(ctx);
    int pitch, rows, size;

    if (!frames || frame->format != AV_PIX_FMT_NV12 || frame->width <= 0 || frame->height <= 0 ||
        frame->width > 4096 || frame->height > 4096) {
        return avcodec_default_get_buffer2(ctx, frame, flags);
    }

    pitch = (frame->width + 255) & ~255;
    rows = (frame->height + 15) & ~15;
    size = pitch * rows * 3 / 2;

    if (size != frames->size) {
        for (AVBufferRef* ref : frames->spare) {
            av_buffer_unref(&ref);
        }
        frames->spare.clear();
        frames->size = size;
    }

    AVBufferRef* ref = frames->spare.empty() ? AllocHardwareFrame(size) : nullptr;
    if (!ref && !frames->spare.empty()) {
        ref = frames->spare.back();
        frames->spare.pop_back();
    }
    if (!ref) {
        return AVERROR(ENOMEM);
    }

    frame->buf[0] = ref;
    frame->data[0] = ref->data;
    frame->data[1] = frame->data[0] + pitch * rows;
    frame->linesize[0] = frame->linesize[1] = pitch;
    frame->extended_data = frame->data;
    return 0;
}

void AttachHardwareFrames(AVCodecContext* ctx) {
    ctx->opaque = new HardwareFrames;
    ctx->get_buffer2 = GetHardwareFrame;
}

void DetachHardwareFrames(AVCodecContext* ctx) {
    if (!ctx || ctx->get_buffer2 != GetHardwareFrame) {
        return;
    }
    HardwareFrames* frames = GetHardwareFrames(ctx);
    ctx->get_buffer2 = avcodec_default_get_buffer2;
    ctx->opaque = nullptr;
    if (frames) {
        for (AVBufferRef* ref : frames->spare) {
            av_buffer_unref(&ref);
        }
        delete frames;
    }
}

}  // namespace

VideoDecoder::VideoDecoder()
    : mFormatCtx(nullptr), mVideoCodecCtx(nullptr), mAudioCodecCtx(nullptr),
      mSwsCtx(nullptr), mSwrCtx(nullptr), mAvioCtx(nullptr),
      mDecodeMode(DecodeMode::Software), mVideoPacketsDecoded(0), mVideoPicturesDecoded(0),
      mUseNV12(false),
      mFrame(nullptr), mFrameRGB(nullptr), mAudioFrame(nullptr), mPacket(nullptr),
      mVideoStreamIndex(-1), mAudioStreamIndex(-1), mWidth(0), mHeight(0),
      mDuration(0.0), mCurrentTime(0.0), mAudioTime(0.0), mBuffer(nullptr),
      mAvioBuffer(nullptr), mAudioBuffer(nullptr), mAudioBufferSize(0),
      mAudioBufferIndex(0),
      mSwsWidth(0), mSwsHeight(0),
      mStatWindowStart(0), mStatDecodeUs(0), mStatScaleUs(0), mStatWaitUs(0),
      mStatFrames(0), mStatLastDecodeStart(0),
      mPresentedTexture(nullptr), mPresentedUseNV12(0), mPresentedY(0), mPresentedUV(0), mPresentedPitch(0), mPresentedCount(0), mAudioDevice(0), mPacketMutex(nullptr),
      mVideoDecodeMutex(nullptr), mAudioPacket(nullptr), mPacketReaderThread(nullptr),
      mReaderReachedEOF(0), mFrameWorkerThread(nullptr), mFile(nullptr) {
    mPacketMutex = SDL_CreateMutex();
    mVideoDecodeMutex = SDL_CreateMutex();
    SDL_AtomicSet(&mReaderThreadRunning, 0);
    SDL_AtomicSet(&mFrameWorkerRunning, 0);
    SDL_AtomicSet(&mPlaybackArmed, 0);
    SDL_AtomicSet(&mDecodeEpoch, 0);
}

VideoDecoder::~VideoDecoder() {
    Close();
    if (mPacketMutex) {
        SDL_DestroyMutex(mPacketMutex);
        mPacketMutex = nullptr;
    }
    if (mVideoDecodeMutex) {
        SDL_DestroyMutex(mVideoDecodeMutex);
        mVideoDecodeMutex = nullptr;
    }
}

int VideoDecoder::ReadPacket(void* opaque, uint8_t* buf, int buf_size) {
    FILE* file = static_cast<FILE*>(opaque);
    return fread(buf, 1, buf_size, file);
}

int64_t VideoDecoder::Seek(void* opaque, int64_t offset, int whence) {
    FILE* file = static_cast<FILE*>(opaque);
    
    if (whence == AVSEEK_SIZE) {
        long pos = ftell(file);
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, pos, SEEK_SET);
        return size;
    }
    
    if (fseek(file, offset, whence) != 0) {
        return -1;
    }
    
    return ftell(file);
}

bool VideoDecoder::HardwareDecodeAvailable() {
    static int available = -1;
    if (available < 0) {
        const AVCodec* codec = avcodec_find_decoder_by_name(kHardwareDecoderName);
        available = codec ? 1 : 0;
        WHBLogPrintf("VideoDecoder: hardware H.264 decoder (%s) %s", kHardwareDecoderName,
                     available ? "available" : "NOT in this FFmpeg build");
    }
    return available != 0;
}

bool VideoDecoder::HardwareCanDecode(AVCodecParameters* params) {
    if (!params || params->codec_id != AV_CODEC_ID_H264) {
        return false;
    }

    if (params->width <= 0 || params->width > 1920 ||
        params->height <= 0 || params->height > 1088) {
        return false;
    }
    if (params->format == AV_PIX_FMT_YUV420P10LE ||
        params->profile == FF_PROFILE_H264_HIGH_10 ||
        params->profile == FF_PROFILE_H264_HIGH_422 ||
        params->profile == FF_PROFILE_H264_HIGH_444_PREDICTIVE) {
        return false;
    }
    return true;
}

const char* VideoDecoder::DecodeModeName(DecodeMode mode) {
    switch (mode) {
        case DecodeMode::Hardware:          return kHardwareDecoderName;
        case DecodeMode::HardwareNoBFrames: return "h264_wiiu (no B-frames)";
        case DecodeMode::Software:          return "software h264";
    }
    return "unknown";
}

VideoDecoder::DecodeMode VideoDecoder::GetDecodeMode() {
    return s_decodeMode;
}

void VideoDecoder::SetDecodeMode(DecodeMode mode) {
    s_decodeMode = mode;
}

VideoDecoder::DecodeMode VideoDecoder::DemoteDecodeMode(DecodeMode mode) {
    DecodeMode next = mode;
    if (next == DecodeMode::Hardware) {
        next = DecodeMode::HardwareNoBFrames;
    } else if (next == DecodeMode::HardwareNoBFrames) {
        next = DecodeMode::Software;
    }
    return next;
}

void VideoDecoder::DemoteToNextMode() {
    DecodeMode current = s_decodeMode;
    DecodeMode next = DemoteDecodeMode(current);
    if (next == current) {
        return;
    }
    s_decodeMode = next;
    WHBLogPrintf("VideoDecoder: falling back to %s", DecodeModeName(next));
}

bool VideoDecoder::OpenVideoDecoder(DecodeMode mode) {
    AVCodecParameters* codecParams = mFormatCtx->streams[mVideoStreamIndex]->codecpar;
    const AVCodec* codec = nullptr;
    bool hardware = false;

    if (mode != DecodeMode::Software && HardwareCanDecode(codecParams) && HardwareDecodeAvailable()) {
        codec = avcodec_find_decoder_by_name(kHardwareDecoderName);
        hardware = codec != nullptr;
    }

    if (!codec) {
        if (codecParams->codec_id == AV_CODEC_ID_H264) {
            codec = avcodec_find_decoder_by_name("h264");
        } else {
            codec = avcodec_find_decoder(codecParams->codec_id);
        }
    }

    if (!codec) {
        WHBLogPrintf("VideoDecoder::OpenVideoDecoder: no decoder for %s",
                     avcodec_get_name(codecParams->codec_id));
        return false;
    }

    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        WHBLogPrintf("VideoDecoder::OpenVideoDecoder: could not allocate the codec context");
        return false;
    }

    if (avcodec_parameters_to_context(ctx, codecParams) < 0) {
        WHBLogPrintf("VideoDecoder::OpenVideoDecoder: could not copy the codec parameters");
        avcodec_free_context(&ctx);
        return false;
    }

    ctx->pkt_timebase = mFormatCtx->streams[mVideoStreamIndex]->time_base;

    if (hardware) {
        AttachHardwareFrames(ctx);

        if (mode == DecodeMode::HardwareNoBFrames) {
            ctx->skip_frame = AVDISCARD_NONREF;
        }
    } else {
        ctx->thread_count = 3;
        ctx->thread_type = FF_THREAD_SLICE | FF_THREAD_FRAME;
        if (codecParams->codec_id == AV_CODEC_ID_H264) {
            ctx->flags2 |= AV_CODEC_FLAG2_FAST;
            ctx->skip_loop_filter = AVDISCARD_NONREF;
            ctx->skip_frame = AVDISCARD_NONREF;
        }
    }

    if (avcodec_open2(ctx, codec, nullptr) < 0) {
        WHBLogPrintf("VideoDecoder::OpenVideoDecoder: could not open %s", codec->name);
        DetachHardwareFrames(ctx);
        avcodec_free_context(&ctx);
        return false;
    }

    mVideoCodecCtx = ctx;
    mDecodeMode = hardware ? mode : DecodeMode::Software;
    mVideoPacketsDecoded = 0;
    mVideoPicturesDecoded = 0;
    mUseNV12 = false;

    WHBLogPrintf("VideoDecoder::OpenVideoDecoder: %s (%s)", codec->name,
                 DecodeModeName(mDecodeMode));
    return true;
}

bool VideoDecoder::ReopenVideoDecoder() {
    if (mVideoStreamIndex < 0) {
        return false;
    }

    DemoteToNextMode();

    if (mVideoCodecCtx) {
        DetachHardwareFrames(mVideoCodecCtx);
        avcodec_free_context(&mVideoCodecCtx);
        mVideoCodecCtx = nullptr;
    }

    if (!OpenVideoDecoder(s_decodeMode)) {
        mDecodeMode = DecodeMode::Software;
        return false;
    }

    SDL_AtomicAdd(&mDecodeEpoch, 1);
    return true;
}

bool VideoDecoder::SyncSwsContext(int width, int height) {
    if (!mVideoCodecCtx || !mFrameRGB || width <= 0 || height <= 0) {
        return false;
    }

    if (mSwsCtx && mSwsWidth == width && mSwsHeight == height) {
        mWidth = width;
        mHeight = height;
        return true;
    }

    int swsFlags = SWS_FAST_BILINEAR;
    if (mVideoCodecCtx->codec_id == AV_CODEC_ID_RAWVIDEO) {
        swsFlags = SWS_POINT;
        WHBLogPrintf("VideoDecoder: Using SWS_POINT (fastest) for raw video");
    } else if (mVideoCodecCtx->width == width && mVideoCodecCtx->height == height) {
        swsFlags = SWS_POINT;
        WHBLogPrintf("VideoDecoder: No scaling needed, using SWS_POINT (colour conversion only)");
    }

    if (mSwsCtx) {
        sws_freeContext(mSwsCtx);
        mSwsCtx = nullptr;
    }

    mSwsCtx = sws_getContext(width, height, mVideoCodecCtx->pix_fmt,
                            width, height, AV_PIX_FMT_RGBA,
                            swsFlags, nullptr, nullptr, nullptr);
    if (!mSwsCtx) {
        WHBLogPrintf("VideoDecoder: could not create the scaler for %dx%d", width, height);
        return false;
    }

    int srcRange = 0;
    int dstRange = 1;
    if (mVideoCodecCtx->pix_fmt == AV_PIX_FMT_YUVJ420P ||
        mVideoCodecCtx->pix_fmt == AV_PIX_FMT_YUVJ422P ||
        mVideoCodecCtx->pix_fmt == AV_PIX_FMT_YUVJ444P ||
        mVideoCodecCtx->pix_fmt == AV_PIX_FMT_YUVJ440P) {
        srcRange = 1;
        WHBLogPrintf("VideoDecoder: Detected JPEG pixel format, using full-range YUV");
    }

    int *inv_table, *table;
    int brightness, contrast, saturation;
    sws_getColorspaceDetails(mSwsCtx, &inv_table, &srcRange, &table, &dstRange,
                             &brightness, &contrast, &saturation);
    sws_setColorspaceDetails(mSwsCtx, inv_table, srcRange, table, dstRange,
                             brightness, contrast, saturation);

    mSwsWidth = width;
    mSwsHeight = height;
    mWidth = width;
    mHeight = height;

    if (mBuffer) {
        av_free(mBuffer);
        mBuffer = nullptr;
    }
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, width, height, 1);
    WHBLogPrintf("VideoDecoder: Allocating RGB buffer (%d bytes)", numBytes);
    mBuffer = (uint8_t*)av_malloc(numBytes);
    if (!mBuffer) {
        return false;
    }
    av_image_fill_arrays(mFrameRGB->data, mFrameRGB->linesize, mBuffer,
                        AV_PIX_FMT_RGBA, width, height, 1);
    return true;
}

static void LogFFmpeg(void* avcl, int level, const char* fmt, va_list vl) {
    if (level > AV_LOG_INFO) {
        return;
    }
    char line[512];
    vsnprintf(line, sizeof(line), fmt, vl);
    WHBLogPrintf("[FFmpeg] %s", line);
}

bool VideoDecoder::Open(const std::string& path) {
    WHBLogPrintf("===========================================");
    WHBLogPrintf("VideoDecoder::Open: Starting to open: %s", path.c_str());
    WHBLogPrintf("===========================================");

    static bool ffmpegLogRedirected = false;
    if (!ffmpegLogRedirected) {
        ffmpegLogRedirected = true;
        av_log_set_callback(LogFFmpeg);
    }
    
    static bool ffmpegInitialized = false;
    if (!ffmpegInitialized) {
        WHBLogPrintf("VideoDecoder::Open: Initializing FFmpeg - Listing ALL available decoders");
        
        WHBLogPrintf("VideoDecoder::Open: === VIDEO DECODERS ===");
        void* opaque = nullptr;
        const AVCodec* codec = nullptr;
        int videoCount = 0;
        while ((codec = av_codec_iterate(&opaque))) {
            if (av_codec_is_decoder(codec) && codec->type == AVMEDIA_TYPE_VIDEO) {
                WHBLogPrintf("  VIDEO: %s (ID: %d)", codec->name, codec->id);
                videoCount++;
                if (videoCount >= 20) {
                    WHBLogPrintf("  ... (truncated, showing first 20)");
                    break;
                }
            }
        }
        WHBLogPrintf("VideoDecoder::Open: Total video decoders found: %d", videoCount);
        
        WHBLogPrintf("VideoDecoder::Open: === AUDIO DECODERS ===");
        opaque = nullptr;
        codec = nullptr;
        int audioCount = 0;
        while ((codec = av_codec_iterate(&opaque))) {
            if (av_codec_is_decoder(codec) && codec->type == AVMEDIA_TYPE_AUDIO) {
                WHBLogPrintf("  AUDIO: %s (ID: %d)", codec->name, codec->id);
                audioCount++;
                if (audioCount >= 15) {
                    WHBLogPrintf("  ... (truncated, showing first 15)");
                    break;
                }
            }
        }
        WHBLogPrintf("VideoDecoder::Open: Total audio decoders found: %d", audioCount);
        
        WHBLogPrintf("VideoDecoder::Open: === CHECKING FOR SPECIFIC CODECS ===");
        const AVCodec* mpeg4_codec = avcodec_find_decoder(AV_CODEC_ID_MPEG4);
        if (mpeg4_codec) {
            WHBLogPrintf("  ✓ MPEG4 decoder (ID 13) FOUND: %s", mpeg4_codec->name);
        } else {
            WHBLogPrintf("  ✗ MPEG4 decoder (ID 13) NOT FOUND!");
        }
        
        const AVCodec* h264_codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        if (h264_codec) {
            WHBLogPrintf("  ✓ H264 decoder (ID 27) FOUND: %s", h264_codec->name);
        } else {
            WHBLogPrintf("  ✗ H264 decoder (ID 27) NOT FOUND!");
        }
        
        WHBLogPrintf("VideoDecoder::Open: === INITIALIZATION COMPLETE ===");
        ffmpegInitialized = true;
    }
    
    FILE* testFile = fopen(path.c_str(), "rb");
    if (testFile) {
        unsigned char header[16];
        size_t bytesRead = fread(header, 1, 16, testFile);
        fclose(testFile);
        
        WHBLogPrintf("VideoDecoder::Open: File exists, read %zu bytes", bytesRead);
        if (bytesRead >= 4) {
            WHBLogPrintf("VideoDecoder::Open: First 4 bytes: %02X %02X %02X %02X", 
                         header[0], header[1], header[2], header[3]);
            
            if (header[0] == 'R' && header[1] == 'I' && header[2] == 'F' && header[3] == 'F') {
                WHBLogPrintf("VideoDecoder::Open: ✓ AVI/RIFF container detected");
            }
            // Check for MP4 signature
            else if (bytesRead >= 8 && header[4] == 'f' && header[5] == 't' && header[6] == 'y' && header[7] == 'p') {
                WHBLogPrintf("VideoDecoder::Open: ✓ MP4 container detected");
            }
            else if (header[0] == 'I' && header[1] == 'D' && header[2] == '3') {
                WHBLogPrintf("VideoDecoder::Open: ID3v2 tag detected");
            }
            else if (header[0] == 0xFF && (header[1] & 0xE0) == 0xE0) {
                WHBLogPrintf("VideoDecoder::Open: MP3 frame sync detected");
            }
        }
    } else {
        WHBLogPrintf("VideoDecoder::Open: WARNING - Could not open file for inspection");
    }
    
    WHBLogPrintf("VideoDecoder::Open: Attempting direct FFmpeg open");
    mFormatCtx = nullptr;
    
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "probesize", "10000000", 0);  // Increase probe size
    av_dict_set(&opts, "analyzeduration", "10000000", 0);  // Increase analyze duration
    
    int ret = avformat_open_input(&mFormatCtx, path.c_str(), nullptr, &opts);
    av_dict_free(&opts);
    
    if (ret != 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        WHBLogPrintf("VideoDecoder::Open: Direct open failed (%d: %s), trying with MP3 format hint", ret, errbuf);
        
        const AVInputFormat* mp3_fmt = av_find_input_format("mp3");
        if (!mp3_fmt) {
            WHBLogPrintf("VideoDecoder::Open: MP3 format not found, trying mpegaudio");
            mp3_fmt = av_find_input_format("mpegaudio");
        }
        if (!mp3_fmt) {
            WHBLogPrintf("VideoDecoder::Open: mpegaudio format not found, trying mp2");
            mp3_fmt = av_find_input_format("mp2");
        }
        
        if (mp3_fmt) {
            WHBLogPrintf("VideoDecoder::Open: Found audio format '%s', trying with format hint", mp3_fmt->name);
            mFormatCtx = nullptr;
            ret = avformat_open_input(&mFormatCtx, path.c_str(), const_cast<AVInputFormat*>(mp3_fmt), nullptr);
            if (ret != 0) {
                av_strerror(ret, errbuf, sizeof(errbuf));
                WHBLogPrintf("VideoDecoder::Open: Format hint failed (%d: %s)", ret, errbuf);
            }
        } else {
            WHBLogPrintf("VideoDecoder::Open: No audio format demuxer found!");
        }
        
        if (ret != 0) {
            WHBLogPrintf("VideoDecoder::Open: All direct methods failed, trying custom I/O");
            
            WHBLogPrintf("VideoDecoder::Open: Opening file with fopen");
            mFile = fopen(path.c_str(), "rb");
            if (!mFile) {
                WHBLogPrintf("VideoDecoder::Open: FAILED - fopen returned NULL");
                mWidth = -999;
                mHeight = 1;
                return false;
            }
            WHBLogPrintf("VideoDecoder::Open: File opened successfully");
        
        const int avio_buffer_size = 32768;
        WHBLogPrintf("VideoDecoder::Open: Allocating AVIO buffer (%d bytes)", avio_buffer_size);
        mAvioBuffer = (uint8_t*)av_malloc(avio_buffer_size);
        if (!mAvioBuffer) {
            WHBLogPrintf("VideoDecoder::Open: FAILED - av_malloc returned NULL");
            fclose(mFile);
            mFile = nullptr;
            mWidth = -998;
            mHeight = 1;
            return false;
        }
        
            WHBLogPrintf("VideoDecoder::Open: Creating AVIO context");
            mAvioCtx = avio_alloc_context(mAvioBuffer, avio_buffer_size, 0, mFile,
                                           &VideoDecoder::ReadPacket, nullptr, &VideoDecoder::Seek);
            if (!mAvioCtx) {
                WHBLogPrintf("VideoDecoder::Open: FAILED - avio_alloc_context returned NULL");
                av_free(mAvioBuffer);
                fclose(mFile);
                mFile = nullptr;
                mWidth = -997;
                mHeight = 1;
                return false;
            }
            
            WHBLogPrintf("VideoDecoder::Open: Allocating format context");
            mFormatCtx = avformat_alloc_context();
            if (!mFormatCtx) {
                WHBLogPrintf("VideoDecoder::Open: FAILED - avformat_alloc_context returned NULL");
                mWidth = -996;
                mHeight = 1;
                Close();
                return false;
            }
            
            mFormatCtx->pb = mAvioCtx;
            
            WHBLogPrintf("VideoDecoder::Open: Opening input with avformat_open_input (custom I/O)");
            ret = avformat_open_input(&mFormatCtx, nullptr, nullptr, nullptr);
            if (ret != 0) {
                char errbuf[128];
                av_strerror(ret, errbuf, sizeof(errbuf));
                WHBLogPrintf("VideoDecoder::Open: FAILED - avformat_open_input returned %d: %s", ret, errbuf);
                mWidth = ret;
                mHeight = 1;
                Close();
                return false;
            }
        }
    }
    WHBLogPrintf("VideoDecoder::Open: Input opened successfully");
    
    WHBLogPrintf("VideoDecoder::Open: Finding stream info");
    if (avformat_find_stream_info(mFormatCtx, nullptr) < 0) {
        WHBLogPrintf("VideoDecoder::Open: FAILED - avformat_find_stream_info failed");
        mWidth = -2;
        mHeight = 2;
        Close();
        return false;
    }
    WHBLogPrintf("VideoDecoder::Open: Found %u streams", mFormatCtx->nb_streams);
    
    for (unsigned i = 0; i < mFormatCtx->nb_streams; i++) {
        AVMediaType type = mFormatCtx->streams[i]->codecpar->codec_type;
        WHBLogPrintf("VideoDecoder::Open: Stream %u type: %d", i, type);
        
        if (type == AVMEDIA_TYPE_VIDEO && mVideoStreamIndex < 0) {
            mVideoStreamIndex = i;
            WHBLogPrintf("VideoDecoder::Open: Found video stream at index %d", i);
        }
        if (type == AVMEDIA_TYPE_AUDIO && mAudioStreamIndex < 0) {
            mAudioStreamIndex = i;
            WHBLogPrintf("VideoDecoder::Open: Found audio stream at index %d", i);
        }
    }
    
    if (mVideoStreamIndex == -1 && mAudioStreamIndex == -1) {
        WHBLogPrintf("VideoDecoder::Open: FAILED - No video or audio stream found");
        mWidth = -3;
        mHeight = 3;
        Close();
        return false;
    }
    
    if (mVideoStreamIndex != -1) {
        WHBLogPrintf("===========================================");
        WHBLogPrintf("VideoDecoder::Open: SETTING UP VIDEO CODEC");
        WHBLogPrintf("===========================================");

        AVCodecParameters* codecParams = mFormatCtx->streams[mVideoStreamIndex]->codecpar;
        WHBLogPrintf("VideoDecoder::Open: Video codec ID: %d", codecParams->codec_id);
        WHBLogPrintf("VideoDecoder::Open: Video codec name: %s", avcodec_get_name(codecParams->codec_id));
        WHBLogPrintf("VideoDecoder::Open: Video dimensions: %dx%d", codecParams->width, codecParams->height);
        WHBLogPrintf("VideoDecoder::Open: Video profile: %d, bitrate: %lld", codecParams->profile, codecParams->bit_rate);

        DecodeMode wanted = s_decodeMode;
        if (wanted != DecodeMode::Software) {
            if (HardwareCanDecode(codecParams) && HardwareDecodeAvailable()) {
                WHBLogPrintf("VideoDecoder::Open: stream is within what the Wii U's decoder takes, trying %s",
                             DecodeModeName(wanted));
            } else {
                WHBLogPrintf("VideoDecoder::Open: stream is not for the Wii U's decoder (needs H.264, at most 1920x1088, 8 bit 4:2:0), decoding on the CPU");
                wanted = DecodeMode::Software;
            }
        }

        if (!OpenVideoDecoder(wanted)) {
            for (DecodeMode mode = DemoteDecodeMode(wanted); mode != wanted; mode = DemoteDecodeMode(mode)) {
                WHBLogPrintf("VideoDecoder::Open: %s did not work out, trying %s",
                             DecodeModeName(wanted), DecodeModeName(mode));
                if (OpenVideoDecoder(mode)) {
                    s_decodeMode = mode;
                    break;
                }
                if (mode == DecodeMode::Software) {
                    break;
                }
            }
        }

        if (!mVideoCodecCtx) {
            WHBLogPrintf("===========================================");
            WHBLogPrintf("VideoDecoder::Open: ✗✗✗ CODEC NOT FOUND ✗✗✗");
            WHBLogPrintf("===========================================");
            WHBLogPrintf("  Codec ID: %d", codecParams->codec_id);
            WHBLogPrintf("  Codec Name: %s", avcodec_get_name(codecParams->codec_id));
            WHBLogPrintf("  This means FFmpeg doesn't have a decoder for this codec!");
            WHBLogPrintf("  Check if FFmpeg was built with this codec enabled.");
            WHBLogPrintf("===========================================");
            mWidth = codecParams->codec_id;
            mHeight = 4;
            mFailedCodecName = avcodec_get_name(codecParams->codec_id);
            Close();
            return false;
        }

        mWidth = mVideoCodecCtx->width;
        mHeight = mVideoCodecCtx->height;
        WHBLogPrintf("VideoDecoder::Open: Video codec opened. Dimensions: %dx%d", mWidth, mHeight);
        WHBLogPrintf("VideoDecoder::Open: Pixel format: %s", av_get_pix_fmt_name(mVideoCodecCtx->pix_fmt));
        WHBLogPrintf("VideoDecoder::Open: Codec: %s, decoding: %s", avcodec_get_name(mVideoCodecCtx->codec_id),
                     DecodeModeName(mDecodeMode));
    } else {
        WHBLogPrintf("VideoDecoder::Open: Audio-only file detected");
        mWidth = 1;
        mHeight = 1;
    }

    if (mAudioStreamIndex != -1) {
        WHBLogPrintf("VideoDecoder::Open: Setting up audio codec");

        AVCodecParameters* audioCodecParams = mFormatCtx->streams[mAudioStreamIndex]->codecpar;
        WHBLogPrintf("VideoDecoder::Open: Audio codec ID: %d", audioCodecParams->codec_id);
        WHBLogPrintf("VideoDecoder::Open: Audio codec name: %s", avcodec_get_name(audioCodecParams->codec_id));

        const AVCodec* audioCodec = avcodec_find_decoder(audioCodecParams->codec_id);
        if (!audioCodec) {
            WHBLogPrintf("VideoDecoder::Open: WARNING - Audio codec not found for ID %d", audioCodecParams->codec_id);

            audioCodec = avcodec_find_decoder_by_name("mp3");
            if (!audioCodec) {
                audioCodec = avcodec_find_decoder_by_name("mp3float");
            }
            if (!audioCodec) {
                audioCodec = avcodec_find_decoder_by_name("mp3on4");
            }

            if (audioCodec) {
                WHBLogPrintf("VideoDecoder::Open: Found alternative MP3 decoder: %s", audioCodec->name);
            } else {
                WHBLogPrintf("VideoDecoder::Open: ERROR - No MP3 decoder available, continuing without audio");
                mWidth = audioCodecParams->codec_id;
                mHeight = 4;
            }
        }

        if (audioCodec) {
            WHBLogPrintf("VideoDecoder::Open: Audio codec found: %s", audioCodec->name);

            mAudioCodecCtx = avcodec_alloc_context3(audioCodec);
            if (!mAudioCodecCtx) {
                WHBLogPrintf("VideoDecoder::Open: WARNING - Could not allocate audio codec context");
            } else {
                if (avcodec_parameters_to_context(mAudioCodecCtx, audioCodecParams) < 0) {
                    WHBLogPrintf("VideoDecoder::Open: WARNING - Could not copy audio codec params");
                    avcodec_free_context(&mAudioCodecCtx);
                } else if (avcodec_open2(mAudioCodecCtx, audioCodec, nullptr) < 0) {
                    WHBLogPrintf("VideoDecoder::Open: WARNING - Could not open audio codec");
                    avcodec_free_context(&mAudioCodecCtx);
                } else {
                    WHBLogPrintf("VideoDecoder::Open: Audio codec opened successfully");
                    WHBLogPrintf("  Sample rate: %d Hz", mAudioCodecCtx->sample_rate);
                    WHBLogPrintf("  Channels: %d", mAudioCodecCtx->channels);
                    WHBLogPrintf("  Format: %d (%s)", mAudioCodecCtx->sample_fmt,
                                 av_get_sample_fmt_name(mAudioCodecCtx->sample_fmt));

                    mSwrCtx = swr_alloc();
                    if (mSwrCtx) {
                        av_opt_set_int(mSwrCtx, "in_channel_layout", mAudioCodecCtx->channel_layout ?
                                       mAudioCodecCtx->channel_layout : av_get_default_channel_layout(mAudioCodecCtx->channels), 0);
                        av_opt_set_int(mSwrCtx, "out_channel_layout", mAudioCodecCtx->channel_layout ?
                                       mAudioCodecCtx->channel_layout : av_get_default_channel_layout(mAudioCodecCtx->channels), 0);
                        av_opt_set_int(mSwrCtx, "in_sample_rate", mAudioCodecCtx->sample_rate, 0);
                        av_opt_set_int(mSwrCtx, "out_sample_rate", mAudioCodecCtx->sample_rate, 0);
                        av_opt_set_sample_fmt(mSwrCtx, "in_sample_fmt", mAudioCodecCtx->sample_fmt, 0);
                        av_opt_set_sample_fmt(mSwrCtx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

                        if (swr_init(mSwrCtx) < 0) {
                            WHBLogPrintf("VideoDecoder::Open: WARNING - Could not initialize audio resampler");
                            swr_free(&mSwrCtx);
                            mSwrCtx = nullptr;
                        } else {
                            WHBLogPrintf("VideoDecoder::Open: Audio resampler initialized (converting to S16)");
                        }
                    }
                }
            }
        }
    }

    if (mFormatCtx->duration != AV_NOPTS_VALUE) {
        mDuration = mFormatCtx->duration / (double)AV_TIME_BASE;
        WHBLogPrintf("VideoDecoder::Open: Duration: %.2f seconds", mDuration);
    } else {
        WHBLogPrintf("VideoDecoder::Open: WARNING - Duration not available");
    }

    WHBLogPrintf("VideoDecoder::Open: Allocating frames");
    mFrame = av_frame_alloc();
    mPacket = av_packet_alloc();
    mAudioPacket = av_packet_alloc();

    if (!mFrame || !mPacket || !mAudioPacket) {
        WHBLogPrintf("VideoDecoder::Open: FAILED - Could not allocate frame or packet");
        mWidth = -8;
        mHeight = 8;
        Close();
        return false;
    }

    if (mVideoStreamIndex != -1) {
        WHBLogPrintf("VideoDecoder::Open: Allocating video-specific resources");
        mFrameRGB = av_frame_alloc();
        if (!mFrameRGB) {
            WHBLogPrintf("VideoDecoder::Open: FAILED - Could not allocate RGB frame");
            mWidth = -8;
            mHeight = 8;
            Close();
            return false;
        }

        if (mVideoCodecCtx->pix_fmt == AV_PIX_FMT_NONE) {
            WHBLogPrintf("VideoDecoder::Open: FAILED - Invalid pixel format AV_PIX_FMT_NONE");
            mWidth = -10;
            mHeight = 10;
            Close();
            return false;
        }

        WHBLogPrintf("VideoDecoder::Open: Initializing SWS context");

        if (!SyncSwsContext(mWidth, mHeight)) {
            WHBLogPrintf("VideoDecoder::Open: FAILED - Could not initialize SWS context");
            mWidth = -9;
            mHeight = 9;
            Close();
            return false;
        }
    } else {
        WHBLogPrintf("VideoDecoder::Open: Skipping video-specific resources (audio-only)");
    }

    WHBLogPrintf("VideoDecoder::Open: SUCCESS - File opened and ready");

    SDL_AtomicSet(&mReaderThreadRunning, 1);
    SDL_AtomicSet(&mReaderReachedEOF, 0);
    SDL_AtomicSet(&mPlaybackArmed, 0);
    SDL_AtomicSet(&mDecodeEpoch, 1);
    if (mVideoStreamIndex != -1) {
        SDL_AtomicSet(&mFrameWorkerRunning, 1);
        mFrameWorkerThread = SDL_CreateThread(FrameWorkerThreadFunc, "FrameWorker", this);
        if (!mFrameWorkerThread) {
            WHBLogPrintf("VideoDecoder::Open: WARNING - Failed to create frame worker thread");
            SDL_AtomicSet(&mFrameWorkerRunning, 0);
        } else {
            WHBLogPrintf("VideoDecoder::Open: Frame worker thread started");
        }
    }

    mPacketReaderThread = SDL_CreateThread(PacketReaderThreadFunc, "PacketReader", this);
    if (!mPacketReaderThread) {
        WHBLogPrintf("VideoDecoder::Open: WARNING - Failed to create packet reader thread");
    } else {
        WHBLogPrintf("VideoDecoder::Open: Packet reader thread started");
    }
    
    return true;
}

void VideoDecoder::Close() {
    if (mPacketReaderThread) {
        WHBLogPrintf("VideoDecoder::Close: Stopping packet reader thread");
        SDL_AtomicSet(&mReaderThreadRunning, 0);
        SDL_WaitThread(mPacketReaderThread, nullptr);
        mPacketReaderThread = nullptr;
    }

    if (mFrameWorkerThread) {
        WHBLogPrintf("VideoDecoder::Close: Stopping frame worker thread");
        SDL_AtomicSet(&mFrameWorkerRunning, 0);
        SDL_WaitThread(mFrameWorkerThread, nullptr);
        mFrameWorkerThread = nullptr;
    }

    SDL_LockMutex(mPacketMutex);
    while (!mVideoPacketQueue.empty()) {
        AVPacket* pkt = mVideoPacketQueue.front();
        mVideoPacketQueue.pop_front();
        av_packet_free(&pkt);
    }
    while (!mAudioPacketQueue.empty()) {
        AVPacket* pkt = mAudioPacketQueue.front();
        mAudioPacketQueue.pop_front();
        av_packet_free(&pkt);
    }
    for (DecodedVideoFrame& frame : mReadyFrameQueue) {
        ReleaseReadyFrame(frame);
    }
    mReadyFrameQueue.clear();
    FreeFrameBuffers();
    SDL_UnlockMutex(mPacketMutex);

    StopAudio();

    SDL_AtomicAdd(&mDecodeEpoch, 1);

    if (mSwsCtx) {
        sws_freeContext(mSwsCtx);
        mSwsCtx = nullptr;
    }
    mSwsWidth = 0;
    mSwsHeight = 0;

    if (mSwrCtx) {
        swr_free(&mSwrCtx);
        mSwrCtx = nullptr;
    }

    if (mBuffer) {
        av_free(mBuffer);
        mBuffer = nullptr;
    }

    if (mAudioBuffer) {
        av_free(mAudioBuffer);
        mAudioBuffer = nullptr;
        mAudioBufferSize = 0;
        mAudioBufferIndex = 0;
    }

    if (mFrameRGB) {
        av_frame_free(&mFrameRGB);
        mFrameRGB = nullptr;
    }

    if (mAudioFrame) {
        av_frame_free(&mAudioFrame);
        mAudioFrame = nullptr;
    }

    if (mFrame) {
        av_frame_free(&mFrame);
        mFrame = nullptr;
    }

    if (mPacket) {
        av_packet_free(&mPacket);
        mPacket = nullptr;
    }

    if (mAudioPacket) {
        av_packet_free(&mAudioPacket);
        mAudioPacket = nullptr;
    }

    if (mVideoCodecCtx) {
        DetachHardwareFrames(mVideoCodecCtx);
        avcodec_free_context(&mVideoCodecCtx);
        mVideoCodecCtx = nullptr;
    }

    if (mAudioCodecCtx) {
        avcodec_free_context(&mAudioCodecCtx);
        mAudioCodecCtx = nullptr;
    }

    if (mFormatCtx) {
        avformat_close_input(&mFormatCtx);
        mFormatCtx = nullptr;
    }

    if (mAvioCtx) {
        av_freep(&mAvioCtx->buffer);
        avio_context_free(&mAvioCtx);
        mAvioCtx = nullptr;
    }

    if (mFile) {
        fclose(mFile);
        mFile = nullptr;
    }
    mAvioBuffer = nullptr;
    SDL_AtomicSet(&mReaderReachedEOF, 0);
    SDL_AtomicSet(&mPlaybackArmed, 0);
    SDL_AtomicSet(&mDecodeEpoch, 0);

    mVideoStreamIndex = -1;
    mAudioStreamIndex = -1;
    mDecodeMode = DecodeMode::Software;
    mVideoPacketsDecoded = 0;
    mVideoPicturesDecoded = 0;
    mUseNV12 = false;
    ReleaseGpuFrames();
    mStatWindowStart = 0;
    mStatDecodeUs = 0;
    mStatScaleUs = 0;
    mStatWaitUs = 0;
    mStatFrames = 0;
    mStatLastDecodeStart = 0;
}

bool VideoDecoder::Seek(double seconds) {
    if (!mFormatCtx) {
        return false;
    }

    if (seconds < 0.0) {
        seconds = 0.0;
    }

    if (mDuration > 0.0 && seconds > mDuration) {
        seconds = mDuration;
    }

    WHBLogPrintf("VideoDecoder::Seek: Seeking to %.3f seconds", seconds);

    SDL_AtomicAdd(&mDecodeEpoch, kSeekEpochStep);

    SDL_LockMutex(mPacketMutex);
    while (!mAudioPacketQueue.empty()) {
        AVPacket* pkt = mAudioPacketQueue.front();
        mAudioPacketQueue.pop_front();
        av_packet_free(&pkt);
    }
    while (!mVideoPacketQueue.empty()) {
        AVPacket* pkt = mVideoPacketQueue.front();
        mVideoPacketQueue.pop_front();
        av_packet_free(&pkt);
    }
    for (DecodedVideoFrame& frame : mReadyFrameQueue) {
        ReleaseReadyFrame(frame);
    }
    mReadyFrameQueue.clear();
    SDL_AtomicSet(&mReaderReachedEOF, 0);
    SDL_UnlockMutex(mPacketMutex);

    SDL_LockMutex(mVideoDecodeMutex);
    if (mVideoCodecCtx) {
        avcodec_flush_buffers(mVideoCodecCtx);
    }
    if (mAudioCodecCtx) {
        avcodec_flush_buffers(mAudioCodecCtx);
    }
    SDL_UnlockMutex(mVideoDecodeMutex);

    int64_t timestamp = (int64_t)(seconds * AV_TIME_BASE);
    int ret = av_seek_frame(mFormatCtx, mVideoStreamIndex, timestamp, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        WHBLogPrintf("VideoDecoder::Seek: FAILED - %s", errbuf);
        return false;
    }

    SDL_LockMutex(mVideoDecodeMutex);
    if (mVideoCodecCtx) {
        avcodec_flush_buffers(mVideoCodecCtx);
    }
    if (mAudioCodecCtx) {
        avcodec_flush_buffers(mAudioCodecCtx);
    }
    SDL_UnlockMutex(mVideoDecodeMutex);

    mCurrentTime = seconds;
    mAudioTime = seconds;
    mAudioBufferIndex = 0;
    mAudioBufferSize = 0;

    return true;
}

bool VideoDecoder::ReadFrame(SDL_Texture* texture, double targetPts) {
    if (!mFormatCtx) {
        return false;
    }

    if (SDL_AtomicGet(&mFrameWorkerRunning)) {
        DecodedVideoFrame frame;
        if (!PopReadyFrame(frame, targetPts)) {
            SDL_LockMutex(mPacketMutex);
            bool readerReachedEOF = SDL_AtomicGet(&mReaderReachedEOF) != 0;
            bool queuesEmpty = mVideoPacketQueue.empty() && mAudioPacketQueue.empty() && mReadyFrameQueue.empty();
            SDL_UnlockMutex(mPacketMutex);

            if (readerReachedEOF && queuesEmpty) {
                WHBLogPrintf("[FRAME] No more frames available");
                return false;
            }

            return true;
        }

        mCurrentTime = frame.pts;
        mPresentedTexture = texture;
        mPresentedUseNV12 = 0;
        mPresentedY = (uintptr_t) frame.y;
        mPresentedUV = (uintptr_t) frame.uv;
        mPresentedPitch = frame.pitch;
        mPresentedCount++;
        if (texture) {
            if (frame.pixels) {
                SDL_UpdateTexture(texture, nullptr, frame.pixels, frame.pitch);
            }
        }

        SDL_LockMutex(mPacketMutex);
        if (frame.pixels) {
            RecycleFrameBuffer(frame.pixels);
        }
        SDL_UnlockMutex(mPacketMutex);
        return true;
    }

    (void)targetPts;

    static int frameCount = 0;
    static Uint32 lastLogTime = 0;
    static Uint32 totalDecodeTime = 0;
    static Uint32 totalScaleTime = 0;
    static Uint32 totalTextureTime = 0;
    static Uint32 totalPacketWaitTime = 0;
    static Uint32 totalSendPacketTime = 0;
    static Uint32 totalReceiveFrameTime = 0;
    static int framesProcessed = 0;

    Uint32 frameStartTime = SDL_GetTicks();
    frameCount++;

    Uint32 lockStartTime = SDL_GetTicks();
    SDL_LockMutex(mPacketMutex);

    if (mVideoPacketQueue.empty()) {
        SDL_UnlockMutex(mPacketMutex);
        Uint32 now = SDL_GetTicks();
        if ((now - lastLogTime) > 2000) {
            WHBLogPrintf("[VIDEO] STARVATION - No video packets available");
            lastLogTime = now;
        }
        return true;
    }

    AVPacket* pkt = mVideoPacketQueue.front();
    mVideoPacketQueue.pop_front();
    int audioQueueSize = mAudioPacketQueue.size();
    int videoQueueSize = mVideoPacketQueue.size();

    SDL_UnlockMutex(mPacketMutex);
    Uint32 unlockEndTime = SDL_GetTicks();
    totalPacketWaitTime += (unlockEndTime - lockStartTime);

    Uint32 sendPacketStartTime = SDL_GetTicks();
    SDL_LockMutex(mVideoDecodeMutex);
    mVideoPacketsDecoded++;
    if (IsHardwareDecoding() && mVideoPicturesDecoded == 0 &&
        mVideoPacketsDecoded >= kHardwareWatchdogPackets) {
        WHBLogPrintf("[VIDEO] No pictures from the hardware decoder after %d packets, switching decoder",
                     mVideoPacketsDecoded);
        bool reopened = ReopenVideoDecoder();
        SDL_UnlockMutex(mVideoDecodeMutex);
        av_packet_free(&pkt);
        ClearReadyFrames();
        if (!reopened) {
            return false;
        }
        return true;
    }
    if (avcodec_send_packet(mVideoCodecCtx, pkt) < 0) {
        SDL_UnlockMutex(mVideoDecodeMutex);
        av_packet_free(&pkt);
        WHBLogPrintf("[VIDEO] ERROR - Failed to send packet to decoder");
        return true;
    }
    Uint32 sendPacketEndTime = SDL_GetTicks();
    totalSendPacketTime += (sendPacketEndTime - sendPacketStartTime);

    Uint32 receiveFrameStartTime = SDL_GetTicks();
    int receiveResult = avcodec_receive_frame(mVideoCodecCtx, mFrame);
    Uint32 receiveFrameEndTime = SDL_GetTicks();
    SDL_UnlockMutex(mVideoDecodeMutex);
    totalReceiveFrameTime += (receiveFrameEndTime - receiveFrameStartTime);

    if (receiveResult == 0) {
        mVideoPicturesDecoded++;

        if (!SyncSwsContext(mFrame->width, mFrame->height)) {
            av_packet_free(&pkt);
            return false;
        }

        if (mFrame->pts != AV_NOPTS_VALUE) {
            mCurrentTime = mFrame->pts * av_q2d(mFormatCtx->streams[mVideoStreamIndex]->time_base);
        } else {
            double frameRate = GetFrameRate();
            if (frameRate > 0) {
                mCurrentTime += 1.0 / frameRate;
            }
        }

        Uint32 decodeTime = receiveFrameEndTime - receiveFrameStartTime;
        totalDecodeTime += decodeTime;
        framesProcessed++;

        if (texture) {
            Uint32 scaleStartTime = SDL_GetTicks();
            int result = sws_scale(mSwsCtx,
                                  (const uint8_t* const*)mFrame->data,
                                  mFrame->linesize,
                                  0, mHeight,
                                  mFrameRGB->data,
                                  mFrameRGB->linesize);

            Uint32 scaleEndTime = SDL_GetTicks();
            Uint32 scaleTime = scaleEndTime - scaleStartTime;
            totalScaleTime += scaleTime;

            if (result > 0) {
                Uint32 textureStartTime = SDL_GetTicks();
                SDL_UpdateTexture(texture, nullptr, mFrameRGB->data[0], mFrameRGB->linesize[0]);
                Uint32 textureEndTime = SDL_GetTicks();
                Uint32 textureTime = textureEndTime - textureStartTime;
                totalTextureTime += textureTime;
            }
        }

        Uint32 frameEndTime = SDL_GetTicks();
        Uint32 totalFrameTime = frameEndTime - frameStartTime;

        Uint32 now = SDL_GetTicks();
        if ((now - lastLogTime) > 2000) {
            double avgDecode = framesProcessed > 0 ? (double)totalDecodeTime / framesProcessed : 0;
            double avgScale = framesProcessed > 0 ? (double)totalScaleTime / framesProcessed : 0;
            double avgTexture = framesProcessed > 0 ? (double)totalTextureTime / framesProcessed : 0;
            double avgPacketWait = framesProcessed > 0 ? (double)totalPacketWaitTime / framesProcessed : 0;
            double avgSendPacket = framesProcessed > 0 ? (double)totalSendPacketTime / framesProcessed : 0;
            double avgReceiveFrame = framesProcessed > 0 ? (double)totalReceiveFrameTime / framesProcessed : 0;
            double avDrift = mCurrentTime - mAudioTime;
            double fps = framesProcessed / 2.0;

            WHBLogPrintf("[VIDEO] Frame #%d vPTS=%.2f aPTS=%.2f drift=%.0fms vQ=%d aQ=%d FPS=%.1f",
                         frameCount, mCurrentTime, mAudioTime, avDrift * 1000.0, videoQueueSize, audioQueueSize, fps);
            WHBLogPrintf("[VIDEO] Timing: total=%ums pktWait=%.1fms sendPkt=%.1fms recvFrame=%.1fms",
                         totalFrameTime, avgPacketWait, avgSendPacket, avgReceiveFrame);
            WHBLogPrintf("[VIDEO] Timing: decode=%.1fms scale=%.1fms texture=%.1fms",
                         avgDecode, avgScale, avgTexture);
            WHBLogPrintf("[VIDEO] Codec: %s (%dx%d) PixFmt: %s",
                         avcodec_get_name(mVideoCodecCtx->codec_id),
                         mWidth, mHeight,
                         av_get_pix_fmt_name(mVideoCodecCtx->pix_fmt));

            lastLogTime = now;
            totalDecodeTime = 0;
            totalScaleTime = 0;
            totalTextureTime = 0;
            totalPacketWaitTime = 0;
            totalSendPacketTime = 0;
            totalReceiveFrameTime = 0;
            framesProcessed = 0;
        }
    } else if (receiveResult == AVERROR(EAGAIN)) {
    } else {
        WHBLogPrintf("[VIDEO] ERROR - avcodec_receive_frame returned %d", receiveResult);
    }

    av_packet_free(&pkt);
    return true;
}

bool VideoDecoder::HasReadyFrame() const {
    SDL_LockMutex(mPacketMutex);
    bool ready = !mReadyFrameQueue.empty();
    SDL_UnlockMutex(mPacketMutex);
    return ready;
}

bool VideoDecoder::PopReadyFrame(DecodedVideoFrame& frame, double targetPts) {
    SDL_LockMutex(mPacketMutex);
    int currentEpoch = SDL_AtomicGet(&mDecodeEpoch);

    // Discard stale frames first
    while (!mReadyFrameQueue.empty() && mReadyFrameQueue.front().epoch != currentEpoch) {
        ReleaseReadyFrame(mReadyFrameQueue.front());
        mReadyFrameQueue.pop_front();
    }

    if (!mReadyFrameQueue.empty()) {
        if (targetPts >= 0.0) {
            double best = 1e9;
            double newestPts = -1.0;
            size_t bestIdx = 0;
            size_t newestIdx = 0;
            size_t n = mReadyFrameQueue.size();
            bool anyDue = false;
            for (size_t i = 0; i < n; ++i) {
                if (mReadyFrameQueue[i].epoch != currentEpoch) {
                    continue;
                }
                if (mReadyFrameQueue[i].pts > newestPts) {
                    newestPts = mReadyFrameQueue[i].pts;
                    newestIdx = i;
                }
                if (mReadyFrameQueue[i].pts > targetPts) {
                    continue;
                }
                anyDue = true;
                double d = targetPts - mReadyFrameQueue[i].pts;
                if (d < best) {
                    best = d;
                    bestIdx = i;
                }
            }

            if (!anyDue) {
                if (newestPts >= 0.0) {
                    // All frames ahead of the clock: hold until due.
                    SDL_UnlockMutex(mPacketMutex);
                    return false;
                }

                if (mReadyFrameQueue.front().epoch != currentEpoch) {
                    SDL_UnlockMutex(mPacketMutex);
                    return false;
                }
                bestIdx = 0;
            }

            // Drop everything older than the best frame so the queue stays
            // ordered for the next call.
            for (size_t i = 0; i < bestIdx; ++i) {
                ReleaseReadyFrame(mReadyFrameQueue.front());
                mReadyFrameQueue.pop_front();
            }
        }

        if (mReadyFrameQueue.front().epoch == currentEpoch) {
            frame = std::move(mReadyFrameQueue.front());
            mReadyFrameQueue.pop_front();
            SDL_UnlockMutex(mPacketMutex);
            return true;
        }
    }

    SDL_UnlockMutex(mPacketMutex);
    return false;
}

size_t VideoDecoder::ReadyFrameBytes() const {
    size_t bytes = 0;
    for (const DecodedVideoFrame& frame : mReadyFrameQueue) {
        bytes += frame.size;
    }
    return bytes;
}

bool VideoDecoder::HasReadyFrameRoom() const {
    return mReadyFrameQueue.size() < kMaxReadyFrames && ReadyFrameBytes() < kMaxReadyBytes;
}

uint8_t* VideoDecoder::AcquireFrameBuffer(size_t size) {
    if (size == 0) {
        return nullptr;
    }

    for (size_t i = 0; i < mFrameBufferPool.size(); i++) {
        if (mFrameBufferPool[i].size == size) {
            FrameBuffer buffer = mFrameBufferPool[i];
            mFrameBufferPool.erase(mFrameBufferPool.begin() + i);
            mFrameBuffersInUse.push_back(buffer);
            return buffer.data;
        }
    }

    for (FrameBuffer& buffer : mFrameBufferPool) {
        av_free(buffer.data);
    }
    mFrameBufferPool.clear();

    if (mFrameBuffersInUse.size() >= kMaxReadyFrames + 2) {
        return nullptr;
    }
    uint8_t* data = (uint8_t*)av_malloc(size);
    if (data) {
        mFrameBuffersInUse.push_back({ data, size });
    }
    return data;
}

void VideoDecoder::RecycleFrameBuffer(uint8_t* pixels) {
    if (!pixels) {
        return;
    }
    for (size_t i = 0; i < mFrameBuffersInUse.size(); i++) {
        if (mFrameBuffersInUse[i].data == pixels) {
            mFrameBufferPool.push_back(mFrameBuffersInUse[i]);
            mFrameBuffersInUse.erase(mFrameBuffersInUse.begin() + i);
            return;
        }
    }
    av_free(pixels);
}

void VideoDecoder::FreeFrameBuffers() {
    for (FrameBuffer& buffer : mFrameBufferPool) {
        av_free(buffer.data);
    }
    for (FrameBuffer& buffer : mFrameBuffersInUse) {
        av_free(buffer.data);
    }
    mFrameBufferPool.clear();
    mFrameBuffersInUse.clear();
}

void VideoDecoder::HoldForGpu(AVBufferRef* hold) {
    if (!hold) {
        return;
    }
    if (mGpuHeldFrames[mGpuHeldIndex]) {
        av_buffer_unref(&mGpuHeldFrames[mGpuHeldIndex]);
    }
    mGpuHeldFrames[mGpuHeldIndex] = hold;
    mGpuHeldIndex = (mGpuHeldIndex + 1) % kGpuHeldFrames;
}

void VideoDecoder::ReleaseGpuFrames() {
    for (size_t i = 0; i < kGpuHeldFrames; i++) {
        if (mGpuHeldFrames[i]) {
            av_buffer_unref(&mGpuHeldFrames[i]);
        }
    }
    mGpuHeldIndex = 0;
}

bool VideoDecoder::QueueReadyFrame(DecodedVideoFrame&& frame) {
    SDL_LockMutex(mPacketMutex);

    if (frame.epoch != SDL_AtomicGet(&mDecodeEpoch) || (!frame.pixels && !frame.hold)) {
        static int rejected = 0;
        if (rejected < 4) {
            rejected++;
            WHBLogPrintf("[DIAG] QueueReadyFrame rejected: epoch %d vs %d, pixels %p, hold %p, y %p",
                         frame.epoch, SDL_AtomicGet(&mDecodeEpoch),
                         (void*) frame.pixels, (void*) frame.hold, (void*) frame.y);
        }
        if (frame.pixels) {
            RecycleFrameBuffer(frame.pixels);
        }
        av_buffer_unref(&frame.hold);
        SDL_UnlockMutex(mPacketMutex);
        return false;
    }

    if (mReadyFrameQueue.size() >= kMaxReadyFrames || !HasReadyFrameRoom()) {
        if (frame.pixels) {
            RecycleFrameBuffer(frame.pixels);
        }
        av_buffer_unref(&frame.hold);
        frame.pixels = nullptr;
        frame.hold = nullptr;
        SDL_UnlockMutex(mPacketMutex);
        SDL_Delay(kReadyQueueBackpressureDelayMs);
        return false;
    }

    mReadyFrameQueue.push_back(std::move(frame));
    SDL_UnlockMutex(mPacketMutex);
    return true;
}

void VideoDecoder::ClearReadyFrames() {
    SDL_LockMutex(mPacketMutex);
    for (DecodedVideoFrame& frame : mReadyFrameQueue) {
        ReleaseReadyFrame(frame);
    }
    mReadyFrameQueue.clear();
    SDL_UnlockMutex(mPacketMutex);
}

void VideoDecoder::ReleaseReadyFrame(DecodedVideoFrame& frame) {
    if (frame.pixels) {
        RecycleFrameBuffer(frame.pixels);
        frame.pixels = nullptr;
    }
    av_buffer_unref(&frame.hold);
    frame.size = 0;
}

int VideoDecoder::FrameWorkerThreadFunc(void* data) {
    VideoDecoder* decoder = static_cast<VideoDecoder*>(data);
    decoder->FrameWorkerLoop();
    return 0;
}

void VideoDecoder::FrameWorkerLoop() {
    WHBLogPrintf("[FRAME] Thread started");

    int workerEpoch = SDL_AtomicGet(&mDecodeEpoch);

    while (SDL_AtomicGet(&mFrameWorkerRunning)) {
        if (!SDL_AtomicGet(&mPlaybackArmed)) {
            SDL_Delay(kFrameWorkerIdleDelayMs);
            continue;
        }

        int currentEpoch = SDL_AtomicGet(&mDecodeEpoch);
        if (currentEpoch != workerEpoch) {
            workerEpoch = currentEpoch;
            SDL_LockMutex(mVideoDecodeMutex);
            if (mVideoCodecCtx) {
                avcodec_flush_buffers(mVideoCodecCtx);
            }
            SDL_UnlockMutex(mVideoDecodeMutex);
            ClearReadyFrames();
        }

        AVPacket* pkt = nullptr;
        bool readerReachedEOF = false;

        SDL_LockMutex(mPacketMutex);
        if (!mVideoPacketQueue.empty()) {
            pkt = mVideoPacketQueue.front();
            mVideoPacketQueue.pop_front();
            if (pkt->stream_index != mVideoStreamIndex) {
                av_packet_free(&pkt);
                SDL_UnlockMutex(mPacketMutex);
                continue;
            }
        } else {
            readerReachedEOF = SDL_AtomicGet(&mReaderReachedEOF) != 0;
        }
        SDL_UnlockMutex(mPacketMutex);

        if (!pkt) {
            SDL_Delay(readerReachedEOF ? kReaderIdleDelayMs : kFrameWorkerIdleDelayMs);
            continue;
        }

        if (mAudioStreamIndex >= 0) {
            while (SDL_AtomicGet(&mFrameWorkerRunning) &&
                   workerEpoch == SDL_AtomicGet(&mDecodeEpoch)) {
                double readyPts;
                SDL_LockMutex(mPacketMutex);
                readyPts = mReadyFrameQueue.empty()
                               ? -1.0
                               : mReadyFrameQueue.back().pts;
                SDL_UnlockMutex(mPacketMutex);

                if (readyPts < 0.0) {
                    break;
                }
                double ahead = readyPts - mAudioTime;
                if (ahead <= kDecodeAheadSeconds) {
                    break;
                }
                SDL_Delay(kFrameWorkerIdleDelayMs);
            }
            if (workerEpoch != SDL_AtomicGet(&mDecodeEpoch)) {
                continue;
            }
        }

        SDL_LockMutex(mVideoDecodeMutex);

        mStatLastDecodeStart = SDL_GetPerformanceCounter();
        mVideoPacketsDecoded++;
        if (IsHardwareDecoding() && mVideoPicturesDecoded == 0 &&
            mVideoPacketsDecoded >= kHardwareWatchdogPackets) {
            WHBLogPrintf("[FRAME] No pictures from the hardware decoder after %d packets, switching decoder",
                         mVideoPacketsDecoded);
            bool reopened = ReopenVideoDecoder();
            workerEpoch = SDL_AtomicGet(&mDecodeEpoch);
            av_packet_free(&pkt);
            SDL_UnlockMutex(mVideoDecodeMutex);
            ClearReadyFrames();
            if (!reopened) {
                SDL_AtomicSet(&mFrameWorkerRunning, 0);
            }
            continue;
        }

        int sendResult = avcodec_send_packet(mVideoCodecCtx, pkt);
        av_packet_free(&pkt);
        if (sendResult < 0) {
            SDL_UnlockMutex(mVideoDecodeMutex);
            continue;
        }

        while (SDL_AtomicGet(&mFrameWorkerRunning) && workerEpoch == SDL_AtomicGet(&mDecodeEpoch)) {
            int receiveResult = avcodec_receive_frame(mVideoCodecCtx, mFrame);
            if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
                break;
            }
            if (receiveResult < 0) {
                WHBLogPrintf("[FRAME] avcodec_receive_frame returned %d", receiveResult);
                break;
            }

            mVideoPicturesDecoded++;

            mStatDecodeUs += (SDL_GetPerformanceCounter() - mStatLastDecodeStart) * 1000000ULL / SDL_GetPerformanceFrequency();

            if (!SyncSwsContext(mFrame->width, mFrame->height)) {
                av_frame_unref(mFrame);
                break;
            }

            double pts = mCurrentTime;
            if (mFrame->pts != AV_NOPTS_VALUE) {
                pts = mFrame->pts * av_q2d(mFormatCtx->streams[mVideoStreamIndex]->time_base);
            } else {
                double frameRate = GetFrameRate();
                if (frameRate > 0) {
                    pts += 1.0 / frameRate;
                }
            }

            if (mUseNV12 && mFrame->buf[0] && mFrame->data[0] && mFrame->data[1]) {
                DecodedVideoFrame readyFrame;
                readyFrame.pixels = nullptr;
                readyFrame.size = 0;
                readyFrame.pitch = mFrame->linesize[0];
                readyFrame.y = mFrame->data[0];
                readyFrame.uv = mFrame->data[1];
                readyFrame.hold = av_buffer_ref(mFrame->buf[0]);
                readyFrame.pts = pts;
                readyFrame.epoch = workerEpoch;

                while (SDL_AtomicGet(&mFrameWorkerRunning) &&
                       workerEpoch == SDL_AtomicGet(&mDecodeEpoch)) {
                    bool slotFree;
                    SDL_LockMutex(mPacketMutex);
                    slotFree = HasReadyFrameRoom();
                    SDL_UnlockMutex(mPacketMutex);
                    if (slotFree) {
                        break;
                    }
                    mStatWaitUs += (SDL_GetPerformanceCounter() - mStatLastDecodeStart) * 1000000ULL / SDL_GetPerformanceFrequency();
                    SDL_UnlockMutex(mVideoDecodeMutex);
                    SDL_Delay(kReadyQueueBackpressureDelayMs);
                    SDL_LockMutex(mVideoDecodeMutex);
                    mStatLastDecodeStart = SDL_GetPerformanceCounter();
                }
                if (workerEpoch == SDL_AtomicGet(&mDecodeEpoch)) {
                    QueueReadyFrame(std::move(readyFrame));
                } else {
                    av_buffer_unref(&readyFrame.hold);
                }
            } else if (mSwsCtx) {
                Uint64 scaleStart = SDL_GetPerformanceCounter();
                uint8_t* target[4] = { nullptr };
                int targetLinesize[4] = { 0 };
                size_t numBytes = (size_t)av_image_get_buffer_size(
                    AV_PIX_FMT_RGBA, mWidth, mHeight, 1);

                SDL_LockMutex(mPacketMutex);
                uint8_t* targetBuffer = AcquireFrameBuffer(numBytes);
                SDL_UnlockMutex(mPacketMutex);

                if (targetBuffer) {
                    av_image_fill_arrays(target, targetLinesize, targetBuffer,
                                        AV_PIX_FMT_RGBA, mWidth, mHeight, 1);

                    int scaled = sws_scale(mSwsCtx,
                                          (const uint8_t* const*)mFrame->data,
                                          mFrame->linesize,
                                          0, mFrame->height,
                                          target, targetLinesize);

                    mStatScaleUs += (SDL_GetPerformanceCounter() - scaleStart) * 1000000ULL / SDL_GetPerformanceFrequency();

                    if (scaled > 0) {
                        DecodedVideoFrame readyFrame;
                        readyFrame.pixels = targetBuffer;
                        readyFrame.size = numBytes;
                        readyFrame.pitch = targetLinesize[0];
                        readyFrame.y = nullptr;
                        readyFrame.uv = nullptr;
                        readyFrame.hold = nullptr;
                        readyFrame.pts = pts;
                        readyFrame.epoch = workerEpoch;

                        while (SDL_AtomicGet(&mFrameWorkerRunning) &&
                               workerEpoch == SDL_AtomicGet(&mDecodeEpoch)) {
                            bool slotFree;
                            SDL_LockMutex(mPacketMutex);
                            slotFree = HasReadyFrameRoom();
                            SDL_UnlockMutex(mPacketMutex);
                            if (slotFree) {
                                break;
                            }
                            mStatWaitUs += (SDL_GetPerformanceCounter() - mStatLastDecodeStart) * 1000000ULL / SDL_GetPerformanceFrequency();
                            SDL_UnlockMutex(mVideoDecodeMutex);
                            SDL_Delay(kReadyQueueBackpressureDelayMs);
                            SDL_LockMutex(mVideoDecodeMutex);
                            mStatLastDecodeStart = SDL_GetPerformanceCounter();
                        }
                        if (workerEpoch == SDL_AtomicGet(&mDecodeEpoch)) {
                            QueueReadyFrame(std::move(readyFrame));
                        } else {
                            SDL_LockMutex(mPacketMutex);
                            RecycleFrameBuffer(readyFrame.pixels);
                            SDL_UnlockMutex(mPacketMutex);
                        }
                    } else {
                        SDL_LockMutex(mPacketMutex);
                        RecycleFrameBuffer(targetBuffer);
                        SDL_UnlockMutex(mPacketMutex);
                    }
                } else {
                    mStatScaleUs += (SDL_GetPerformanceCounter() - scaleStart) * 1000000ULL / SDL_GetPerformanceFrequency();
                }
            }

            mStatFrames++;
            av_frame_unref(mFrame);
        }

        SDL_UnlockMutex(mVideoDecodeMutex);

        ReportFrameWorkerStats(false);
    }

    ReportFrameWorkerStats(true);

    WHBLogPrintf("[FRAME] Thread stopped");
}

void VideoDecoder::ReportFrameWorkerStats(bool finalReport) {
    Uint64 now = SDL_GetPerformanceCounter();
    Uint64 elapsed = now - mStatWindowStart;
    if (!finalReport && elapsed < 2 * SDL_GetPerformanceFrequency()) {
        return;
    }
    if (mStatWindowStart == 0) {
        mStatWindowStart = now;
        return;
    }
    if (mStatFrames == 0 && !finalReport) {
        mStatWindowStart = now;
        return;
    }

    double seconds = (double)elapsed / SDL_GetPerformanceFrequency();
    double decode = (double)mStatDecodeUs / 1000.0;
    double scale = (double)mStatScaleUs / 1000.0;
    double wait = (double)mStatWaitUs / 1000.0;
    WHBLogPrintf("[FRAME] %d pictures in %.1f s (%.1f/s, budget %.1f ms): decode %.1f ms, colour %.1f ms, waiting %.1f ms [%s]",
                 mStatFrames, seconds, mStatFrames / (seconds > 0.0 ? seconds : 1.0),
                 1000.0 / GetFrameRate(), mStatFrames ? decode / mStatFrames : 0.0,
                 mStatFrames ? scale / mStatFrames : 0.0, mStatFrames ? wait / mStatFrames : 0.0,
                 DecodeModeName(mDecodeMode));
    WHBLogPrintf("[FRAME] presented %d so far: texture %p, useNV12 %d, y %08x uv %08x pitch %d",
                 mPresentedCount, mPresentedTexture, mPresentedUseNV12,
                 (unsigned) mPresentedY, (unsigned) mPresentedUV, mPresentedPitch);

    mStatWindowStart = now;
    mStatDecodeUs = 0;
    mStatScaleUs = 0;
    mStatWaitUs = 0;
    mStatFrames = 0;
}

double VideoDecoder::GetFrameRate() const {
    if (!mFormatCtx || mVideoStreamIndex < 0) {
        return 30.0;
    }
    
    AVStream* stream = mFormatCtx->streams[mVideoStreamIndex];
    AVRational frameRate = stream->avg_frame_rate;
    
    if (frameRate.den > 0 && frameRate.num > 0) {
        return (double)frameRate.num / (double)frameRate.den;
    }
    
    frameRate = stream->r_frame_rate;
    if (frameRate.den > 0 && frameRate.num > 0) {
        return (double)frameRate.num / (double)frameRate.den;
    }
    
    return 30.0;
}

void AudioCallback(void* userdata, Uint8* stream, int len) {
    VideoDecoder* decoder = static_cast<VideoDecoder*>(userdata);
    
    static int callbackCount = 0;
    static Uint32 lastLogTime = 0;
    static int underrunCount = 0;
    static Uint32 totalBytesRequested = 0;
    static Uint32 totalBytesWritten = 0;
    static Uint32 totalDecodeTime = 0;
    static Uint32 totalResampleTime = 0;
    static Uint32 totalLockTime = 0;
    static int decodesPerformed = 0;
    
    Uint32 callbackStartTime = SDL_GetTicks();
    callbackCount++;
    
    SDL_memset(stream, 0, len);
    
    if (!decoder->mAudioCodecCtx || !decoder->mFormatCtx) {
        return;
    }
    
    int bytesWritten = 0;
    totalBytesRequested += len;
    bool hadUnderrun = false;
    
    while (bytesWritten < len) {
        if (decoder->mAudioBufferIndex < decoder->mAudioBufferSize) {
            int bytesToCopy = decoder->mAudioBufferSize - decoder->mAudioBufferIndex;
            if (bytesToCopy > len - bytesWritten) {
                bytesToCopy = len - bytesWritten;
            }
            
            SDL_memcpy(stream + bytesWritten, 
                      decoder->mAudioBuffer + decoder->mAudioBufferIndex, 
                      bytesToCopy);
            
            bytesWritten += bytesToCopy;
            decoder->mAudioBufferIndex += bytesToCopy;
        } else {
            if (!decoder->mAudioFrame) {
                decoder->mAudioFrame = av_frame_alloc();
            }
            
            Uint32 lockStartTime = SDL_GetTicks();
            SDL_LockMutex(decoder->mPacketMutex);
            Uint32 lockEndTime = SDL_GetTicks();
            totalLockTime += (lockEndTime - lockStartTime);
            
            bool gotAudio = false;
            int packetsProcessed = 0;
            Uint32 decodeStartTime = SDL_GetTicks();
            
            while (!gotAudio && !decoder->mAudioPacketQueue.empty()) {
                AVPacket* audioPkt = decoder->mAudioPacketQueue.front();
                decoder->mAudioPacketQueue.pop_front();
                packetsProcessed++;
                
                if (avcodec_send_packet(decoder->mAudioCodecCtx, audioPkt) >= 0) {
                    if (avcodec_receive_frame(decoder->mAudioCodecCtx, decoder->mAudioFrame) == 0) {
                        gotAudio = true;
                        decodesPerformed++;
                        
                        Uint32 decodeEndTime = SDL_GetTicks();
                        totalDecodeTime += (decodeEndTime - decodeStartTime);
                        
                        if (decoder->mAudioFrame->pts != AV_NOPTS_VALUE) {
                            decoder->mAudioTime = decoder->mAudioFrame->pts * 
                                av_q2d(decoder->mFormatCtx->streams[decoder->mAudioStreamIndex]->time_base);
                        }
                        
                        Uint32 resampleStartTime = SDL_GetTicks();
                        if (decoder->mSwrCtx) {
                            int out_samples = av_rescale_rnd(
                                swr_get_delay(decoder->mSwrCtx, decoder->mAudioCodecCtx->sample_rate) + decoder->mAudioFrame->nb_samples,
                                decoder->mAudioCodecCtx->sample_rate,
                                decoder->mAudioCodecCtx->sample_rate,
                                AV_ROUND_UP
                            );
                            
                            int out_size = av_samples_get_buffer_size(
                                nullptr,
                                decoder->mAudioCodecCtx->channels,
                                out_samples,
                                AV_SAMPLE_FMT_S16,
                                0
                            );
                            
                            if (out_size > decoder->mAudioBufferSize) {
                                av_free(decoder->mAudioBuffer);
                                decoder->mAudioBuffer = (uint8_t*)av_malloc(out_size);
                                decoder->mAudioBufferSize = out_size;
                            }
                            
                            uint8_t* out_buf = decoder->mAudioBuffer;
                            int converted_samples = swr_convert(
                                decoder->mSwrCtx,
                                &out_buf,
                                out_samples,
                                (const uint8_t**)decoder->mAudioFrame->data,
                                decoder->mAudioFrame->nb_samples
                            );
                            
                            if (converted_samples > 0) {
                                decoder->mAudioBufferSize = av_samples_get_buffer_size(
                                    nullptr,
                                    decoder->mAudioCodecCtx->channels,
                                    converted_samples,
                                    AV_SAMPLE_FMT_S16,
                                    0
                                );
                                decoder->mAudioBufferIndex = 0;
                            }
                        } else {
                            int dataSize = av_samples_get_buffer_size(
                                nullptr,
                                decoder->mAudioCodecCtx->channels,
                                decoder->mAudioFrame->nb_samples,
                                decoder->mAudioCodecCtx->sample_fmt,
                                1
                            );
                            
                            if (dataSize > decoder->mAudioBufferSize) {
                                av_free(decoder->mAudioBuffer);
                                decoder->mAudioBuffer = (uint8_t*)av_malloc(dataSize);
                                decoder->mAudioBufferSize = dataSize;
                            }
                            
                            SDL_memcpy(decoder->mAudioBuffer, decoder->mAudioFrame->data[0], dataSize);
                            decoder->mAudioBufferIndex = 0;
                        }
                        Uint32 resampleEndTime = SDL_GetTicks();
                        totalResampleTime += (resampleEndTime - resampleStartTime);
                    }
                }
                
                av_packet_free(&audioPkt);
            }
            
            SDL_UnlockMutex(decoder->mPacketMutex);
            
            if (!gotAudio) {
                hadUnderrun = true;
                underrunCount++;
                break;
            }
        }
    }
    
    totalBytesWritten += bytesWritten;
    
    Uint32 callbackEndTime = SDL_GetTicks();
    Uint32 callbackDuration = callbackEndTime - callbackStartTime;
    
    Uint32 now = SDL_GetTicks();
    if ((hadUnderrun && underrunCount <= 5) || (now - lastLogTime) > 2000) {
        SDL_LockMutex(decoder->mPacketMutex);
        int audioQueueSize = decoder->mAudioPacketQueue.size();
        int videoQueueSize = decoder->mVideoPacketQueue.size();
        SDL_UnlockMutex(decoder->mPacketMutex);
        
        double fillRate = totalBytesRequested > 0 ? 
            (100.0 * totalBytesWritten / totalBytesRequested) : 0.0;
        double avDrift = decoder->mCurrentTime - decoder->mAudioTime;
        
        double avgDecode = decodesPerformed > 0 ? (double)totalDecodeTime / decodesPerformed : 0;
        double avgResample = decodesPerformed > 0 ? (double)totalResampleTime / decodesPerformed : 0;
        double avgLock = callbackCount > 0 ? (double)totalLockTime / callbackCount : 0;
        
        if (hadUnderrun) {
            WHBLogPrintf("[AUDIO] UNDERRUN #%d aPTS=%.2f vPTS=%.2f drift=%.0fms aQ=%d vQ=%d fill=%.0f%%", 
                         underrunCount, decoder->mAudioTime, decoder->mCurrentTime, avDrift * 1000.0,
                         audioQueueSize, videoQueueSize, fillRate);
        } else {
            WHBLogPrintf("[AUDIO] aPTS=%.2f vPTS=%.2f drift=%.0fms aQ=%d vQ=%d fill=%.0f%%", 
                         decoder->mAudioTime, decoder->mCurrentTime, avDrift * 1000.0,
                         audioQueueSize, videoQueueSize, fillRate);
        }
        WHBLogPrintf("[AUDIO] Timing: callback=%ums decode=%.1fms resample=%.1fms lock=%.1fms", 
                     callbackDuration, avgDecode, avgResample, avgLock);
        
        lastLogTime = now;
        totalBytesRequested = 0;
        totalBytesWritten = 0;
        totalDecodeTime = 0;
        totalResampleTime = 0;
        totalLockTime = 0;
        decodesPerformed = 0;
    }
}

void VideoDecoder::StartAudio() {
    if (!mAudioCodecCtx || mAudioDevice > 0) {
        return;
    }
    
    WHBLogPrintf("VideoDecoder::StartAudio: Initializing SDL audio");
    
    SDL_AudioSpec wanted_spec, obtained_spec;
    SDL_zero(wanted_spec);
    
    wanted_spec.freq = mAudioCodecCtx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = mAudioCodecCtx->channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = 1024;
    wanted_spec.callback = AudioCallback;
    wanted_spec.userdata = this;
    
    WHBLogPrintf("  Requested: %d Hz, %d channels", wanted_spec.freq, wanted_spec.channels);
    
    mAudioDevice = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &obtained_spec, 0);
    if (mAudioDevice == 0) {
        WHBLogPrintf("VideoDecoder::StartAudio: FAILED - %s", SDL_GetError());
        return;
    }
    
    WHBLogPrintf("  Obtained: %d Hz, %d channels", obtained_spec.freq, obtained_spec.channels);
    WHBLogPrintf("VideoDecoder::StartAudio: Audio device opened, starting playback");
    
    SDL_PauseAudioDevice(mAudioDevice, 0);
}

void VideoDecoder::StopAudio() {
    if (mAudioDevice > 0) {
        WHBLogPrintf("VideoDecoder::StopAudio: Stopping audio playback");
        SDL_CloseAudioDevice(mAudioDevice);
        mAudioDevice = 0;
    }
}

void VideoDecoder::PauseAudio(bool pause) {
    if (mAudioDevice > 0) {
        SDL_PauseAudioDevice(mAudioDevice, pause ? 1 : 0);
        WHBLogPrintf("VideoDecoder::PauseAudio: Audio %s", pause ? "paused" : "resumed");
    }
}

void VideoDecoder::SetPlaybackArmed(bool armed) {
    SDL_AtomicSet(&mPlaybackArmed, armed ? 1 : 0);
}

int VideoDecoder::PacketReaderThreadFunc(void* data) {
    VideoDecoder* decoder = static_cast<VideoDecoder*>(data);
    decoder->PacketReaderLoop();
    return 0;
}

void VideoDecoder::PacketReaderLoop() {
    WHBLogPrintf("[READER] Thread started");
    
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        WHBLogPrintf("[READER] Failed to allocate packet");
        return;
    }
    
    int videoPacketsRead = 0;
    int audioPacketsRead = 0;
    int videoPacketsDropped = 0;
    Uint32 lastLogTime = SDL_GetTicks();
    Uint32 totalReadTime = 0;
    Uint32 totalWaitTime = 0;
    int readsPerformed = 0;
    int waitsPerformed = 0;
    
    while (SDL_AtomicGet(&mReaderThreadRunning)) {
        SDL_LockMutex(mPacketMutex);
        
        int videoQueueSize = mVideoPacketQueue.size();
        int audioQueueSize = mAudioPacketQueue.size();
        
        if (videoQueueSize > 30 && audioQueueSize > 30) {
            SDL_UnlockMutex(mPacketMutex);
            Uint32 waitStartTime = SDL_GetTicks();
            SDL_Delay(10);
            Uint32 waitEndTime = SDL_GetTicks();
            totalWaitTime += (waitEndTime - waitStartTime);
            waitsPerformed++;
            continue;
        }
        
        Uint32 readStartTime = SDL_GetTicks();
        int ret = av_read_frame(mFormatCtx, pkt);
        Uint32 readEndTime = SDL_GetTicks();
        
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                SDL_AtomicSet(&mReaderReachedEOF, 1);
            }
            SDL_UnlockMutex(mPacketMutex);
            if (ret == AVERROR_EOF) {
                WHBLogPrintf("[READER] EOF (v=%d a=%d) - waiting for seek or stop", videoPacketsRead, audioPacketsRead);
                SDL_Delay(50);
                continue;
            } else {
                char errbuf[128];
                av_strerror(ret, errbuf, sizeof(errbuf));
                WHBLogPrintf("[READER] Error: %d (%s)", ret, errbuf);
                break;
            }
        }
        
        totalReadTime += (readEndTime - readStartTime);
        readsPerformed++;
        
        if (pkt->stream_index == mVideoStreamIndex) {
            AVPacket* videoPkt = av_packet_alloc();
            if (videoPkt && av_packet_ref(videoPkt, pkt) == 0) {
                mVideoPacketQueue.push_back(videoPkt);
                videoPacketsRead++;
            } else {
                WHBLogPrintf("[READER] WARNING - Failed to queue video packet");
                av_packet_free(&videoPkt);
            }
        } else if (pkt->stream_index == mAudioStreamIndex) {
            AVPacket* audioPkt = av_packet_alloc();
            if (audioPkt && av_packet_ref(audioPkt, pkt) == 0) {
                mAudioPacketQueue.push_back(audioPkt);
                audioPacketsRead++;
            } else {
                WHBLogPrintf("[READER] WARNING - Failed to queue audio packet");
                av_packet_free(&audioPkt);
            }
        }
        
        av_packet_unref(pkt);
        SDL_UnlockMutex(mPacketMutex);
        
        Uint32 now = SDL_GetTicks();
        if ((now - lastLogTime) > 5000) {
            double avgRead = readsPerformed > 0 ? (double)totalReadTime / readsPerformed : 0;
            double avgWait = waitsPerformed > 0 ? (double)totalWaitTime / waitsPerformed : 0;
            
            WHBLogPrintf("[READER] Read v=%d a=%d dropped=%d (Q: v=%d a=%d)",
                         videoPacketsRead, audioPacketsRead, videoPacketsDropped, videoQueueSize, audioQueueSize);
            WHBLogPrintf("[READER] Timing: avgRead=%.1fms avgWait=%.1fms waits=%d", 
                         avgRead, avgWait, waitsPerformed);
            
            lastLogTime = now;
            videoPacketsRead = 0;
            audioPacketsRead = 0;
            videoPacketsDropped = 0;
            totalReadTime = 0;
            totalWaitTime = 0;
            readsPerformed = 0;
            waitsPerformed = 0;
        }
    }
    
    av_packet_free(&pkt);
    WHBLogPrintf("[READER] Thread stopped");
}
