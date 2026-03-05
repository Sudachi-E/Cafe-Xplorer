#include "VideoDecoder.hpp"
#include <whb/log.h>

VideoDecoder::VideoDecoder()
    : mFormatCtx(nullptr), mVideoCodecCtx(nullptr), mAudioCodecCtx(nullptr),
      mSwsCtx(nullptr), mSwrCtx(nullptr), mAvioCtx(nullptr), mFrame(nullptr), 
      mFrameRGB(nullptr), mAudioFrame(nullptr), mPacket(nullptr), 
      mVideoStreamIndex(-1), mAudioStreamIndex(-1), mWidth(0), mHeight(0),
      mDuration(0.0), mCurrentTime(0.0), mAudioTime(0.0), mBuffer(nullptr), 
      mAvioBuffer(nullptr), mFile(nullptr), mAudioDevice(0), mAudioBuffer(nullptr), 
      mAudioBufferSize(0), mAudioBufferIndex(0), mPacketMutex(nullptr), 
      mAudioPacket(nullptr), mPacketReaderThread(nullptr) {
    mPacketMutex = SDL_CreateMutex();
    SDL_AtomicSet(&mReaderThreadRunning, 0);
}

VideoDecoder::~VideoDecoder() {
    Close();
    if (mPacketMutex) {
        SDL_DestroyMutex(mPacketMutex);
        mPacketMutex = nullptr;
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

bool VideoDecoder::Open(const std::string& path) {
    WHBLogPrintf("VideoDecoder::Open: Starting to open: %s", path.c_str());
    
    static bool ffmpegInitialized = false;
    if (!ffmpegInitialized) {
        WHBLogPrintf("VideoDecoder::Open: Initializing FFmpeg");
        
        // List available audio decoders
        WHBLogPrintf("VideoDecoder::Open: Available audio decoders:");
        void* opaque = nullptr;
        const AVCodec* codec = nullptr;
        int count = 0;
        while ((codec = av_codec_iterate(&opaque))) {
            if (av_codec_is_decoder(codec) && codec->type == AVMEDIA_TYPE_AUDIO) {
                WHBLogPrintf("  - %s (ID: %d)", codec->name, codec->id);
                count++;
                if (count >= 10) break; // Limit output
            }
        }
        WHBLogPrintf("VideoDecoder::Open: Listed %d audio decoders", count);
        
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
            
            // Check for ID3 tag
            if (header[0] == 'I' && header[1] == 'D' && header[2] == '3') {
                WHBLogPrintf("VideoDecoder::Open: ID3v2 tag detected");
            }
            // Check for MP3 frame sync
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
        mHeight = 3; // Signal: no video or audio stream
        Close();
        return false;
    }
    
    if (mVideoStreamIndex != -1) {
        WHBLogPrintf("VideoDecoder::Open: Setting up video codec");
        
        AVCodecParameters* codecParams = mFormatCtx->streams[mVideoStreamIndex]->codecpar;
        WHBLogPrintf("VideoDecoder::Open: Video codec ID: %d", codecParams->codec_id);
        
        const AVCodec* codec = nullptr;
        
        if (codecParams->codec_id == AV_CODEC_ID_H264) {
            WHBLogPrintf("VideoDecoder::Open: H.264 detected, trying hardware decoder");
            codec = avcodec_find_decoder_by_name("h264_wiiu");
            if (codec) {
                WHBLogPrintf("VideoDecoder::Open: Using hardware H.264 decoder!");
            } else {
                WHBLogPrintf("VideoDecoder::Open: Hardware decoder not available, using optimized software");
                codec = avcodec_find_decoder(codecParams->codec_id);
            }
        } else {
            codec = avcodec_find_decoder(codecParams->codec_id);
        }
        
        if (!codec) {
            WHBLogPrintf("VideoDecoder::Open: FAILED - Video codec not found");
            mWidth = codecParams->codec_id;
            mHeight = 4;
            Close();
            return false;
        }
        WHBLogPrintf("VideoDecoder::Open: Video codec found: %s", codec->name);
        
        mVideoCodecCtx = avcodec_alloc_context3(codec);
        if (!mVideoCodecCtx) {
            WHBLogPrintf("VideoDecoder::Open: FAILED - Could not allocate video codec context");
            mWidth = -5;
            mHeight = 5;
            Close();
            return false;
        }
        
        if (avcodec_parameters_to_context(mVideoCodecCtx, codecParams) < 0) {
            WHBLogPrintf("VideoDecoder::Open: FAILED - Could not copy video codec params");
            mWidth = -6;
            mHeight = 6;
            Close();
            return false;
        }
        
        mVideoCodecCtx->thread_count = 2;  // Wii U has 3 cores, use 2 for decoding
        mVideoCodecCtx->thread_type = FF_THREAD_SLICE;  // Slice-based threading
        if (codecParams->codec_id == AV_CODEC_ID_H264) {
            mVideoCodecCtx->flags2 |= AV_CODEC_FLAG2_FAST;  // Fast decoding
            mVideoCodecCtx->skip_loop_filter = AVDISCARD_NONREF;  // Skip loop filter for non-reference frames
        }
        WHBLogPrintf("VideoDecoder::Open: Enabled multi-threading (2 threads) and fast decode");
        
        if (avcodec_open2(mVideoCodecCtx, codec, nullptr) < 0) {
            WHBLogPrintf("VideoDecoder::Open: FAILED - Could not open video codec");
            mWidth = -7;
            mHeight = 7;
            Close();
            return false;
        }
        
        mWidth = mVideoCodecCtx->width;
        mHeight = mVideoCodecCtx->height;
        WHBLogPrintf("VideoDecoder::Open: Video codec opened. Dimensions: %dx%d", mWidth, mHeight);
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
        
        int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, mWidth, mHeight, 1);
        WHBLogPrintf("VideoDecoder::Open: Allocating RGB buffer (%d bytes)", numBytes);
        mBuffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));
        av_image_fill_arrays(mFrameRGB->data, mFrameRGB->linesize, mBuffer,
                            AV_PIX_FMT_RGBA, mWidth, mHeight, 1);
        
        WHBLogPrintf("VideoDecoder::Open: Initializing SWS context");
        mSwsCtx = sws_getContext(mWidth, mHeight, mVideoCodecCtx->pix_fmt,
                                mWidth, mHeight, AV_PIX_FMT_RGBA,
                                SWS_BILINEAR, nullptr, nullptr, nullptr);
        
        if (!mSwsCtx) {
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
    
    StopAudio();
    
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
    SDL_UnlockMutex(mPacketMutex);
    
    if (mSwsCtx) {
        sws_freeContext(mSwsCtx);
        mSwsCtx = nullptr;
    }
    
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
    }
    
    if (mAudioFrame) {
        av_frame_free(&mAudioFrame);
    }
    
    if (mFrame) {
        av_frame_free(&mFrame);
    }
    
    if (mPacket) {
        av_packet_free(&mPacket);
    }
    
    if (mAudioPacket) {
        av_packet_free(&mAudioPacket);
    }
    
    if (mVideoCodecCtx) {
        avcodec_free_context(&mVideoCodecCtx);
    }
    
    if (mAudioCodecCtx) {
        avcodec_free_context(&mAudioCodecCtx);
    }
    
    if (mAvioCtx) {
        av_freep(&mAvioCtx->buffer);
        avio_context_free(&mAvioCtx);
    }
    
    if (mFormatCtx) {
        avformat_close_input(&mFormatCtx);
    }
    
    if (mFile) {
        fclose(mFile);
        mFile = nullptr;
    }
    
    mVideoStreamIndex = -1;
    mAudioStreamIndex = -1;
}

bool VideoDecoder::ReadFrame(SDL_Texture* texture) {
    if (!mFormatCtx) {
        return false;
    }
    
    static int frameCount = 0;
    static Uint32 lastLogTime = 0;
    static Uint32 totalDecodeTime = 0;
    static Uint32 totalScaleTime = 0;
    static Uint32 totalTextureTime = 0;
    static int framesProcessed = 0;
    
    Uint32 frameStartTime = SDL_GetTicks();
    frameCount++;
    
    Uint32 lockStartTime = SDL_GetTicks();
    SDL_LockMutex(mPacketMutex);
    Uint32 lockEndTime = SDL_GetTicks();
    
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
    
    if (avcodec_send_packet(mVideoCodecCtx, pkt) < 0) {
        av_packet_free(&pkt);
        WHBLogPrintf("[VIDEO] ERROR - Failed to send packet to decoder");
        return true;
    }
    
    Uint32 decodeStartTime = SDL_GetTicks();
    int receiveResult = avcodec_receive_frame(mVideoCodecCtx, mFrame);
    Uint32 decodeEndTime = SDL_GetTicks();
    
    if (receiveResult == 0) {
        if (mFrame->pts != AV_NOPTS_VALUE) {
            mCurrentTime = mFrame->pts * av_q2d(mFormatCtx->streams[mVideoStreamIndex]->time_base);
        }
        
        Uint32 decodeTime = decodeEndTime - decodeStartTime;
        totalDecodeTime += decodeTime;
        framesProcessed++;
        
        if (texture) {
            Uint32 scaleStartTime = SDL_GetTicks();
            sws_scale(mSwsCtx, mFrame->data, mFrame->linesize, 0, mHeight,
                     mFrameRGB->data, mFrameRGB->linesize);
            Uint32 scaleEndTime = SDL_GetTicks();
            Uint32 scaleTime = scaleEndTime - scaleStartTime;
            totalScaleTime += scaleTime;
            
            Uint32 textureStartTime = SDL_GetTicks();
            SDL_UpdateTexture(texture, nullptr, mFrameRGB->data[0], mFrameRGB->linesize[0]);
            Uint32 textureEndTime = SDL_GetTicks();
            Uint32 textureTime = textureEndTime - textureStartTime;
            totalTextureTime += textureTime;
        }
        
        Uint32 frameEndTime = SDL_GetTicks();
        Uint32 totalFrameTime = frameEndTime - frameStartTime;
        
        Uint32 now = SDL_GetTicks();
        if ((now - lastLogTime) > 2000) {
            double avgDecode = framesProcessed > 0 ? (double)totalDecodeTime / framesProcessed : 0;
            double avgScale = framesProcessed > 0 ? (double)totalScaleTime / framesProcessed : 0;
            double avgTexture = framesProcessed > 0 ? (double)totalTextureTime / framesProcessed : 0;
            double avDrift = mCurrentTime - mAudioTime;
            
            WHBLogPrintf("[VIDEO] Frame #%d vPTS=%.2f aPTS=%.2f drift=%.0fms vQ=%d aQ=%d", 
                         frameCount, mCurrentTime, mAudioTime, avDrift * 1000.0, videoQueueSize, audioQueueSize);
            WHBLogPrintf("[VIDEO] Timing: total=%ums decode=%.1fms scale=%.1fms texture=%.1fms lock=%ums", 
                         totalFrameTime, avgDecode, avgScale, avgTexture, lockEndTime - lockStartTime);
            
            lastLogTime = now;
            totalDecodeTime = 0;
            totalScaleTime = 0;
            totalTextureTime = 0;
            framesProcessed = 0;
        }
    } else if (receiveResult == AVERROR(EAGAIN)) {
    } else {
        WHBLogPrintf("[VIDEO] ERROR - avcodec_receive_frame returned %d", receiveResult);
    }
    
    av_packet_free(&pkt);
    return true;
}

bool VideoDecoder::Seek(double seconds) {
    if (!mFormatCtx) {
        return false;
    }
    
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
    
    int streamIndex = (mVideoStreamIndex >= 0) ? mVideoStreamIndex : mAudioStreamIndex;
    if (streamIndex < 0) {
        SDL_UnlockMutex(mPacketMutex);
        return false;
    }
    
    int64_t timestamp = (int64_t)(seconds / av_q2d(mFormatCtx->streams[streamIndex]->time_base));
    
    if (av_seek_frame(mFormatCtx, streamIndex, timestamp, AVSEEK_FLAG_BACKWARD) < 0) {
        SDL_UnlockMutex(mPacketMutex);
        return false;
    }
    
    if (mVideoCodecCtx) {
        avcodec_flush_buffers(mVideoCodecCtx);
    }
    if (mAudioCodecCtx) {
        avcodec_flush_buffers(mAudioCodecCtx);
    }
    
    mAudioBufferIndex = 0;
    mAudioBufferSize = 0;
    
    mCurrentTime = seconds;
    
    SDL_UnlockMutex(mPacketMutex);
    return true;
}

double VideoDecoder::GetFrameRate() const {
    if (!mFormatCtx || mVideoStreamIndex < 0) {
        return 30.0; // Default fallback
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
            SDL_Delay(10);  // Wait a bit before checking again
            Uint32 waitEndTime = SDL_GetTicks();
            totalWaitTime += (waitEndTime - waitStartTime);
            waitsPerformed++;
            continue;
        }
        
        Uint32 readStartTime = SDL_GetTicks();
        int ret = av_read_frame(mFormatCtx, pkt);
        Uint32 readEndTime = SDL_GetTicks();
        
        if (ret < 0) {
            SDL_UnlockMutex(mPacketMutex);
            if (ret == AVERROR_EOF) {
                WHBLogPrintf("[READER] EOF (v=%d a=%d)", videoPacketsRead, audioPacketsRead);
            } else {
                char errbuf[128];
                av_strerror(ret, errbuf, sizeof(errbuf));
                WHBLogPrintf("[READER] Error: %d (%s)", ret, errbuf);
            }
            break;
        }
        
        totalReadTime += (readEndTime - readStartTime);
        readsPerformed++;
        
        // Queue the packet
        if (pkt->stream_index == mVideoStreamIndex) {
            AVPacket* videoPkt = av_packet_alloc();
            av_packet_ref(videoPkt, pkt);
            mVideoPacketQueue.push_back(videoPkt);
            videoPacketsRead++;
        } else if (pkt->stream_index == mAudioStreamIndex) {
            AVPacket* audioPkt = av_packet_alloc();
            av_packet_ref(audioPkt, pkt);
            mAudioPacketQueue.push_back(audioPkt);
            audioPacketsRead++;
        }
        
        av_packet_unref(pkt);
        SDL_UnlockMutex(mPacketMutex);
        
        // Log every 5 seconds for more frequent updates
        Uint32 now = SDL_GetTicks();
        if ((now - lastLogTime) > 5000) {
            double avgRead = readsPerformed > 0 ? (double)totalReadTime / readsPerformed : 0;
            double avgWait = waitsPerformed > 0 ? (double)totalWaitTime / waitsPerformed : 0;
            
            WHBLogPrintf("[READER] Read v=%d a=%d (Q: v=%d a=%d)", 
                         videoPacketsRead, audioPacketsRead, videoQueueSize, audioQueueSize);
            WHBLogPrintf("[READER] Timing: avgRead=%.1fms avgWait=%.1fms waits=%d", 
                         avgRead, avgWait, waitsPerformed);
            
            lastLogTime = now;
            videoPacketsRead = 0;
            audioPacketsRead = 0;
            totalReadTime = 0;
            totalWaitTime = 0;
            readsPerformed = 0;
            waitsPerformed = 0;
        }
    }
    
    av_packet_free(&pkt);
    WHBLogPrintf("[READER] Thread stopped");
}
