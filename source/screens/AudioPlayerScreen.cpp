#include "AudioPlayerScreen.hpp"
#include "../Gfx.hpp"
#include <whb/log.h>
#include <SDL2/SDL_mixer.h>

AudioPlayerScreen::AudioPlayerScreen(const std::string& audioPath)
    : mAudioPath(audioPath), mMusic(nullptr), mShouldClose(false), mLoadError(false),
      mIsPlaying(false), mIsPaused(false), mInitialized(false),
      mDuration(0.0), mErrorCode(0) {
    
    WHBLogPrintf("AudioPlayerScreen: Initializing SDL_mixer");
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        WHBLogPrintf("AudioPlayerScreen: Failed to initialize SDL_mixer: %s", Mix_GetError());
        mLoadError = true;
        mErrorMessage = std::string("Failed to open audio device: ") + Mix_GetError();
    } else {
        WHBLogPrintf("AudioPlayerScreen: SDL_mixer initialized successfully");
    }
}

AudioPlayerScreen::~AudioPlayerScreen() {
    if (mMusic) {
        Mix_HaltMusic();
        Mix_FreeMusic(mMusic);
        mMusic = nullptr;
    }
    
    Mix_CloseAudio();
    WHBLogPrintf("AudioPlayerScreen: Closed SDL_mixer audio device");
}

void AudioPlayerScreen::Draw() {
    Gfx::Clear(Gfx::COLOR_BLACK);
    
    DrawTopBar(mAudioPath.c_str());
    DrawBottomBar("B: Back", "A: Play/Pause", "L/R: Seek");
    
    if (!mInitialized && !mLoadError) {
        InitializeAudio();
    }
    
    if (mLoadError) {
        Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2 - 100, 48,
                   Gfx::COLOR_WHITE, "Failed to load audio", Gfx::ALIGN_CENTER);
        
        if (!mErrorMessage.empty()) {
            Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2 - 40, 32,
                       Gfx::COLOR_ALT_TEXT, mErrorMessage.c_str(), Gfx::ALIGN_CENTER);
        }
        
        Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2 + 80, 24,
                   Gfx::COLOR_ALT_TEXT, mAudioPath.c_str(), Gfx::ALIGN_CENTER);
        
        return;
    }
    
    int centerX = Gfx::SCREEN_WIDTH / 2;
    int centerY = Gfx::SCREEN_HEIGHT / 2 - 50;
    
    SDL_Renderer* renderer = Gfx::GetRenderer();
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    
    SDL_Rect stem = {centerX - 5, centerY - 80, 10, 100};
    SDL_RenderFillRect(renderer, &stem);
    
    SDL_Rect head = {centerX - 25, centerY + 10, 40, 30};
    SDL_RenderFillRect(renderer, &head);
    
    DrawPlaybackControls();
}

bool AudioPlayerScreen::Update(Input &input) {
    if (input.data.buttons_d & Input::BUTTON_B) {
        mShouldClose = true;
        return false;
    }
    
    if (input.data.buttons_d & Input::BUTTON_A) {
        if (!mIsPlaying) {
            WHBLogPrintf("AudioPlayerScreen: Starting playback");
            if (Mix_PlayMusic(mMusic, 0) == -1) {
                WHBLogPrintf("AudioPlayerScreen: Failed to play: %s", Mix_GetError());
            } else {
                mIsPlaying = true;
                mIsPaused = false;
            }
        } else {
            if (mIsPaused) {
                WHBLogPrintf("AudioPlayerScreen: Resuming playback");
                Mix_ResumeMusic();
                mIsPaused = false;
            } else {
                WHBLogPrintf("AudioPlayerScreen: Pausing playback");
                Mix_PauseMusic();
                mIsPaused = true;
            }
        }
    }
    
    if (input.data.buttons_d & Input::BUTTON_L) {
        WHBLogPrintf("AudioPlayerScreen: Seeking backward");
        double currentPos = Mix_GetMusicPosition(mMusic);
        double newPos = currentPos - 10.0;
        if (newPos < 0) newPos = 0;
        Mix_SetMusicPosition(newPos);
    }
    
    if (input.data.buttons_d & Input::BUTTON_R) {
        WHBLogPrintf("AudioPlayerScreen: Seeking forward");
        double currentPos = Mix_GetMusicPosition(mMusic);
        double newPos = currentPos + 10.0;
        Mix_SetMusicPosition(newPos);
    }
    
    if (mIsPlaying && !Mix_PlayingMusic()) {
        WHBLogPrintf("AudioPlayerScreen: Playback ended");
        mIsPlaying = false;
        mIsPaused = false;
    }
    
    return true;
}

void AudioPlayerScreen::DrawPlaybackControls() {
    const char* statusText = "Not Playing";
    if (mIsPlaying) {
        statusText = mIsPaused ? "Paused" : "Playing";
    }
    
    Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2 + 100, 48,
               Gfx::COLOR_WHITE, statusText, Gfx::ALIGN_CENTER);
    
    char timeStr[64];
    double currentTime = 0.0;
    if (mMusic) {
        currentTime = Mix_GetMusicPosition(mMusic);
    }
    
    snprintf(timeStr, sizeof(timeStr), "%.1f / %.1f s", currentTime, mDuration);
    Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2 + 160, 36,
               Gfx::COLOR_WHITE, timeStr, Gfx::ALIGN_CENTER);
    
    int barWidth = Gfx::SCREEN_WIDTH - 200;
    int barHeight = 10;
    int barX = (Gfx::SCREEN_WIDTH - barWidth) / 2;
    int barY = Gfx::SCREEN_HEIGHT - 150;
    
    SDL_Rect bgRect = {barX, barY, barWidth, barHeight};
    SDL_SetRenderDrawColor(Gfx::GetRenderer(), 60, 60, 60, 255);
    SDL_RenderFillRect(Gfx::GetRenderer(), &bgRect);
    
    if (mDuration > 0) {
        int progressWidth = (int)(barWidth * (currentTime / mDuration));
        SDL_Rect progressRect = {barX, barY, progressWidth, barHeight};
        SDL_SetRenderDrawColor(Gfx::GetRenderer(), 0, 150, 255, 255);
        SDL_RenderFillRect(Gfx::GetRenderer(), &progressRect);
    }
    
    if (!mIsPlaying) {
        Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2 + 220, 40,
                   Gfx::COLOR_ALT_TEXT, "Press A to Play", Gfx::ALIGN_CENTER);
    }
}

void AudioPlayerScreen::InitializeAudio() {
    mInitialized = true;
    
    WHBLogPrintf("AudioPlayerScreen: Loading music file: %s", mAudioPath.c_str());
    
    mMusic = Mix_LoadMUS(mAudioPath.c_str());
    if (!mMusic) {
        mLoadError = true;
        mErrorMessage = std::string("SDL_mixer error: ") + Mix_GetError();
        WHBLogPrintf("AudioPlayerScreen: Failed to load music: %s", Mix_GetError());
        return;
    }
    
    WHBLogPrintf("AudioPlayerScreen: Music loaded successfully");
    
    mDuration = Mix_MusicDuration(mMusic);
    if (mDuration <= 0) {
        WHBLogPrintf("AudioPlayerScreen: Duration not available, using estimate");
        mDuration = 180.0; // Default 3 minutes if not available
    } else {
        WHBLogPrintf("AudioPlayerScreen: Duration: %.2f seconds", mDuration);
    }
}
