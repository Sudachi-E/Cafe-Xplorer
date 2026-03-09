#include "VideoPlayerScreen.hpp"
#include "../Gfx.hpp"
#include "../filemanager/PathConverter.hpp"
#include <whb/log.h>

VideoPlayerScreen::VideoPlayerScreen(const std::string& videoPath)
    : mVideoPath(PathConverter::ToRealPath(videoPath)), mVideoTexture(nullptr), mShouldClose(false), 
      mLoadError(false), mIsPlaying(false), mIsPaused(false), mInitialized(false),
      mShowUI(true), mIsRawVideo(false), mShowRawVideoWarning(false),
      mVideoWidth(1280), mVideoHeight(720), mPlaybackStartTime(0), 
      mPlaybackStartPTS(0.0), mFrameDelay(33.0) {
    
}

VideoPlayerScreen::~VideoPlayerScreen() {
    WHBLogPrintf("VideoPlayerScreen::~VideoPlayerScreen - Starting");
    
    if (mVideoTexture) {
        WHBLogPrintf("VideoPlayerScreen::~VideoPlayerScreen - Destroying texture");
        SDL_DestroyTexture(mVideoTexture);
        WHBLogPrintf("VideoPlayerScreen::~VideoPlayerScreen - Texture destroyed");
    }
    
    WHBLogPrintf("VideoPlayerScreen::~VideoPlayerScreen - Closing decoder");
    mDecoder.Close();
    WHBLogPrintf("VideoPlayerScreen::~VideoPlayerScreen - Complete");
}

void VideoPlayerScreen::Draw() {
    Gfx::Clear(Gfx::COLOR_BLACK);
    
    if (mShowUI) {
        DrawTopBar(mVideoPath.c_str());
        DrawBottomBar("B: Back", "A: Play/Pause", "L/R: Seek");
    }
    
    if (!mInitialized && !mLoadError) {
        InitializeVideo();
    }
    
    if (mShowRawVideoWarning) {
        Gfx::Clear(Gfx::COLOR_BLACK);
        
        int centerX = Gfx::SCREEN_WIDTH / 2;
        int centerY = Gfx::SCREEN_HEIGHT / 2;
        
        Gfx::Print(centerX, centerY - 30, 48, Gfx::COLOR_WHITE, 
                   "Raw Video Not Supported", Gfx::ALIGN_CENTER);
        
        Gfx::Print(centerX, centerY + 40, 32, Gfx::COLOR_ALT_TEXT,
                   "Press B to go back", Gfx::ALIGN_CENTER);
        
        return;
    }
    
    if (mLoadError) {
        char errorMsg[256];
        if (mVideoHeight == 1) {
            if (mVideoWidth == -999) {
                snprintf(errorMsg, sizeof(errorMsg), "File not found or can't be opened");
            } else {
                snprintf(errorMsg, sizeof(errorMsg), "FFmpeg error: %d (0x%X)", mVideoWidth, (unsigned int)mVideoWidth);
            }
        } else if (mVideoHeight == 2) {
            snprintf(errorMsg, sizeof(errorMsg), "Failed to read stream info");
        } else if (mVideoHeight == 3) {
            snprintf(errorMsg, sizeof(errorMsg), "No video stream found");
        } else if (mVideoHeight == 4) {
            snprintf(errorMsg, sizeof(errorMsg), "Codec not found (ID: %d)", mVideoWidth);
        } else if (mVideoHeight == 5) {
            snprintf(errorMsg, sizeof(errorMsg), "Failed to allocate codec context");
        } else if (mVideoHeight == 6) {
            snprintf(errorMsg, sizeof(errorMsg), "Failed to copy codec params");
        } else if (mVideoHeight == 7) {
            snprintf(errorMsg, sizeof(errorMsg), "Failed to open codec");
        } else if (mVideoHeight == 8) {
            snprintf(errorMsg, sizeof(errorMsg), "Failed to allocate frames");
        } else if (mVideoHeight == 9) {
            snprintf(errorMsg, sizeof(errorMsg), "Failed to init scaler");
        } else {
            snprintf(errorMsg, sizeof(errorMsg), "Unknown error");
        }
        
        Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2, 48,
                   Gfx::COLOR_WHITE, "Failed to load video", Gfx::ALIGN_CENTER);
        Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2 + 60, 28,
                   Gfx::COLOR_ALT_TEXT, errorMsg, Gfx::ALIGN_CENTER);
        return;
    }
    
    if (!mVideoTexture) {
        Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2, 48,
                   Gfx::COLOR_WHITE, "Loading video...", Gfx::ALIGN_CENTER);
        return;
    }
    
    SDL_Rect dstRect;
    CalculateDisplayRect(dstRect);
    
    SDL_RenderCopy(Gfx::GetRenderer(), mVideoTexture, nullptr, &dstRect);
    
    if (mShowUI) {
        DrawPlaybackControls();
    }
}

bool VideoPlayerScreen::Update(Input &input) {
    if (mShowRawVideoWarning) {
        if (input.data.buttons_d & Input::BUTTON_B) {
            mShouldClose = true;
            return false;
        }
        return true;
    }
    
    if (input.data.buttons_d & Input::BUTTON_B) {
        mShouldClose = true;
        return false;
    }
    
    if (input.data.buttons_d & Input::BUTTON_A) {
        if (!mIsPlaying) {
            mIsPlaying = true;
            mIsPaused = false;
            mShowUI = false;
            double videoPTS = mDecoder.GetCurrentTime();
            double audioPTS = mDecoder.GetAudioTime();
            mDecoder.StartAudio();
            WHBLogPrintf("[SYNC] PLAY vPTS=%.2f aPTS=%.2f", videoPTS, audioPTS);
        } else {
            mIsPaused = !mIsPaused;
            if (!mIsPaused) {
                mShowUI = false;
                double videoPTS = mDecoder.GetCurrentTime();
                double audioPTS = mDecoder.GetAudioTime();
                mDecoder.PauseAudio(false);
                WHBLogPrintf("[SYNC] RESUME vPTS=%.2f aPTS=%.2f", videoPTS, audioPTS);
            } else {
                mShowUI = true;  // Show UI when pausing
                mDecoder.PauseAudio(true);
                WHBLogPrintf("[SYNC] PAUSE");
            }
        }
    }
    
    if (input.data.buttons_d & Input::BUTTON_L) {
        Uint32 seekStart = SDL_GetTicks();
        double newTime = mDecoder.GetCurrentTime() - 10.0;
        if (newTime < 0) newTime = 0;
        mDecoder.Seek(newTime);
        Uint32 seekEnd = SDL_GetTicks();
        WHBLogPrintf("[SYNC] Seek back took %ums", seekEnd - seekStart);
    }
    
    if (input.data.buttons_d & Input::BUTTON_R) {
        Uint32 seekStart = SDL_GetTicks();
        double newTime = mDecoder.GetCurrentTime() + 10.0;
        if (newTime > mDecoder.GetDuration()) newTime = mDecoder.GetDuration();
        mDecoder.Seek(newTime);
        Uint32 seekEnd = SDL_GetTicks();
        WHBLogPrintf("[SYNC] Seek forward took %ums", seekEnd - seekStart);
    }
    
    if (mIsPlaying && !mIsPaused) {
        UpdatePlayback();
    }
    
    return true;
}

void VideoPlayerScreen::DrawPlaybackControls() {
    const char* statusText = "Not Playing";
    if (mIsPlaying) {
        statusText = mIsPaused ? "Paused" : "Playing";
    }
    
    Gfx::Print(40, Gfx::SCREEN_HEIGHT - 100, 36, Gfx::COLOR_WHITE, 
               statusText, Gfx::ALIGN_LEFT);
    
    char timeStr[64];
    snprintf(timeStr, sizeof(timeStr), "%.1f / %.1f s", 
             mDecoder.GetCurrentTime(), mDecoder.GetDuration());
    Gfx::Print(Gfx::SCREEN_WIDTH - 40, Gfx::SCREEN_HEIGHT - 100, 36, 
               Gfx::COLOR_WHITE, timeStr, Gfx::ALIGN_RIGHT);
    
    int barWidth = Gfx::SCREEN_WIDTH - 80;
    int barHeight = 8;
    int barX = 40;
    int barY = Gfx::SCREEN_HEIGHT - 140;
    
    SDL_Rect bgRect = {barX, barY, barWidth, barHeight};
    SDL_SetRenderDrawColor(Gfx::GetRenderer(), 60, 60, 60, 255);
    SDL_RenderFillRect(Gfx::GetRenderer(), &bgRect);
    
    if (mDecoder.GetDuration() > 0) {
        int progressWidth = (int)(barWidth * (mDecoder.GetCurrentTime() / mDecoder.GetDuration()));
        SDL_Rect progressRect = {barX, barY, progressWidth, barHeight};
        SDL_SetRenderDrawColor(Gfx::GetRenderer(), 0, 150, 255, 255);
        SDL_RenderFillRect(Gfx::GetRenderer(), &progressRect);
    }
    
    if (!mIsPlaying) {
        Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2, 48,
                   Gfx::COLOR_WHITE, "Press A to Play", Gfx::ALIGN_CENTER);
    }
}

void VideoPlayerScreen::CalculateDisplayRect(SDL_Rect& rect) {
    float videoAspect = static_cast<float>(mVideoWidth) / static_cast<float>(mVideoHeight);
    
    int viewportHeight = mShowUI ? (Gfx::SCREEN_HEIGHT - 120) : Gfx::SCREEN_HEIGHT;
    int viewportWidth = Gfx::SCREEN_WIDTH;
    int viewportY = mShowUI ? 60 : 0;
    float viewportAspect = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
    
    if (videoAspect > viewportAspect) {
        rect.w = viewportWidth;
        rect.h = static_cast<int>(viewportWidth / videoAspect);
        rect.x = 0;
        rect.y = viewportY + (viewportHeight - rect.h) / 2;
    } else {
        rect.h = viewportHeight;
        rect.w = static_cast<int>(viewportHeight * videoAspect);
        rect.x = (viewportWidth - rect.w) / 2;
        rect.y = viewportY;
    }
}

void VideoPlayerScreen::UpdatePlayback() {
    static Uint32 lastLogTime = 0;
    static int framesDecoded = 0;
    static Uint32 lastFrameTime = 0;
    static Uint32 totalFrameTime = 0;
    static int frameTimings = 0;
    static int framesSkipped = 0;
    static int framesDropped = 0;
    static int framesCatchup = 0;
    
    Uint32 updateStartTime = SDL_GetTicks();
    
    double videoPTS = mDecoder.GetCurrentTime();
    double audioPTS = mDecoder.GetAudioTime();
    double avDrift = videoPTS - audioPTS;
    
    Uint32 currentTime = SDL_GetTicks();
    Uint32 timeSinceLastFrame = currentTime - lastFrameTime;
    
    bool shouldDecode = false;
    bool dropFrame = false;
    int framesToDecode = 1;
    
    if (avDrift < -0.1) {
        shouldDecode = true;
        framesCatchup++;
        
        if (avDrift < -0.2) {
            framesToDecode = (avDrift < -0.3) ? 3 : 2;
            dropFrame = true;
        }
    } else if (avDrift > 0.1) {
        double compensatedDelay = mFrameDelay + (avDrift * 1000.0);
        if (timeSinceLastFrame >= (Uint32)compensatedDelay) {
            shouldDecode = true;
        } else {
            framesSkipped++;
        }
    } else {
        if (timeSinceLastFrame >= (Uint32)mFrameDelay) {
            shouldDecode = true;
        } else {
            framesSkipped++;
        }
    }
    
    if (shouldDecode) {
        Uint32 decodeStartTime = SDL_GetTicks();
        
        for (int i = 0; i < framesToDecode; i++) {
            bool isLastFrame = (i == framesToDecode - 1);
            SDL_Texture* targetTexture = (dropFrame && !isLastFrame) ? nullptr : mVideoTexture;
            
            if (!mDecoder.ReadFrame(targetTexture)) {
                mIsPlaying = false;
                mDecoder.StopAudio();
                WHBLogPrintf("[SYNC] End of video");
                return;
            }
            
            framesDecoded++;
            if (dropFrame && !isLastFrame) {
                framesDropped++;
            }
            
            if (i < framesToDecode - 1) {
                videoPTS = mDecoder.GetCurrentTime();
                audioPTS = mDecoder.GetAudioTime();
                avDrift = videoPTS - audioPTS;
            }
        }
        
        Uint32 decodeEndTime = SDL_GetTicks();
        Uint32 frameTime = decodeEndTime - decodeStartTime;
        totalFrameTime += frameTime;
        frameTimings++;
        
        lastFrameTime = currentTime;
    }
    
    Uint32 updateEndTime = SDL_GetTicks();
    Uint32 updateDuration = updateEndTime - updateStartTime;
    
    Uint32 now = SDL_GetTicks();
    if ((now - lastLogTime) > 2000) {
        double avgFrameTime = frameTimings > 0 ? (double)totalFrameTime / frameTimings : 0;
        
        WHBLogPrintf("[SYNC] vPTS=%.2f aPTS=%.2f drift=%.0fms decoded=%d skipped=%d dropped=%d catchup=%d", 
                     videoPTS, audioPTS, avDrift * 1000.0, framesDecoded, framesSkipped, framesDropped, framesCatchup);
        WHBLogPrintf("[SYNC] Timing: update=%ums avgFrame=%.1fms targetDelay=%.1fms", 
                     updateDuration, avgFrameTime, mFrameDelay);
        
        lastLogTime = now;
        framesDecoded = 0;
        framesSkipped = 0;
        framesDropped = 0;
        framesCatchup = 0;
        totalFrameTime = 0;
        frameTimings = 0;
    }
}

void VideoPlayerScreen::InitializeVideo() {
    Uint32 initStart = SDL_GetTicks();
    WHBLogPrintf("[PERF] InitializeVideo: Starting initialization");
    
    mInitialized = true;
    
    char pathMsg[256];
    snprintf(pathMsg, sizeof(pathMsg), "Opening: %s", mVideoPath.c_str());
    
    Uint32 openStart = SDL_GetTicks();
    if (!mDecoder.Open(mVideoPath)) {
        mLoadError = true;
        mVideoWidth = mDecoder.GetWidth();
        mVideoHeight = mDecoder.GetHeight();
        WHBLogPrintf("[PERF] InitializeVideo: Failed to open video");
        return;
    }
    Uint32 openEnd = SDL_GetTicks();
    WHBLogPrintf("[PERF] InitializeVideo: Decoder.Open took %u ms", openEnd - openStart);
    
    mVideoWidth = mDecoder.GetWidth();
    mVideoHeight = mDecoder.GetHeight();
    
    if (mVideoWidth <= 0 || mVideoHeight <= 0) {
        mLoadError = true;
        mDecoder.Close();
        WHBLogPrintf("[PERF] InitializeVideo: Invalid dimensions %dx%d", mVideoWidth, mVideoHeight);
        return;
    }
    
    WHBLogPrintf("[PERF] InitializeVideo: Video dimensions %dx%d", mVideoWidth, mVideoHeight);
    
    if (mDecoder.IsRawVideo()) {
        WHBLogPrintf("[PERF] InitializeVideo: RAW VIDEO DETECTED - Not supported");
        mIsRawVideo = true;
        mShowRawVideoWarning = true;
        mDecoder.Close();
        Uint32 initEnd = SDL_GetTicks();
        WHBLogPrintf("[PERF] InitializeVideo: Raw video not supported (took %u ms)", initEnd - initStart);
        return;
    }
    
    Uint32 textureStart = SDL_GetTicks();
    mVideoTexture = SDL_CreateTexture(Gfx::GetRenderer(),
                                      SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_STREAMING,
                                      mVideoWidth, mVideoHeight);
    Uint32 textureEnd = SDL_GetTicks();
    WHBLogPrintf("[PERF] InitializeVideo: Texture creation took %u ms", textureEnd - textureStart);
    
    if (!mVideoTexture) {
        mLoadError = true;
        mDecoder.Close();
        WHBLogPrintf("[PERF] InitializeVideo: Failed to create texture");
        return;
    }
    
    SDL_SetRenderTarget(Gfx::GetRenderer(), mVideoTexture);
    SDL_SetRenderDrawColor(Gfx::GetRenderer(), 0, 0, 0, 255);
    SDL_RenderClear(Gfx::GetRenderer());
    SDL_SetRenderTarget(Gfx::GetRenderer(), nullptr);
    
    double fps = mDecoder.GetFrameRate();
    mFrameDelay = 1000.0 / fps;
    
    Uint32 waitStart = SDL_GetTicks();
    if (mDecoder.HasVideo()) {
        WHBLogPrintf("[PERF] InitializeVideo: Waiting for first frame to decode...");
        double initialTime = mDecoder.GetCurrentTime();
        int waitAttempts = 0;
        bool frameDecoded = false;
        
        while (waitAttempts < 100 && !frameDecoded) {
            SDL_Delay(10);
            waitAttempts++;
            
            mDecoder.ReadFrame(mVideoTexture);
            
            double newTime = mDecoder.GetCurrentTime();
            if (newTime > initialTime) {
                frameDecoded = true;
                WHBLogPrintf("[PERF] InitializeVideo: First frame decoded successfully after %d attempts (%.3fs)", 
                             waitAttempts, newTime);
                break;
            }
        }
        
        if (!frameDecoded) {
            WHBLogPrintf("[PERF] InitializeVideo: Warning - No frame decoded after timeout");
        }
    }
    Uint32 waitEnd = SDL_GetTicks();
    WHBLogPrintf("[PERF] InitializeVideo: First frame decode took %u ms", waitEnd - waitStart);
    
    Uint32 initEnd = SDL_GetTicks();
    WHBLogPrintf("[PERF] InitializeVideo: Total initialization took %u ms (FPS=%.2f, delay=%.2fms)", 
                 initEnd - initStart, fps, mFrameDelay);
}
