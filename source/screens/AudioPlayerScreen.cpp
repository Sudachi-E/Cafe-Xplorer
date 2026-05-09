#include "AudioPlayerScreen.hpp"
#include "../Gfx.hpp"
#include "../filemanager/PathConverter.hpp"
#include <whb/log.h>
#include <SDL2/SDL_mixer.h>
#include <algorithm>

AudioPlayerScreen::AudioPlayerScreen(const std::string& audioPath)
    : mAudioPath(PathConverter::ToRealPath(audioPath)),
      mMusic(nullptr), mAudioDecoder(nullptr),
      mShouldClose(false), mLoadError(false),
      mIsPlaying(false), mIsPaused(false), mInitialized(false),
      mSeekSupported(true), mDuration(0.0), mErrorCode(0) {

    if (!UseFFmpeg()) {
        WHBLogPrintf("AudioPlayerScreen: Initializing SDL_mixer");
        if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
            WHBLogPrintf("AudioPlayerScreen: SDL_mixer init failed: %s", Mix_GetError());
            mLoadError    = true;
            mErrorMessage = std::string("Failed to open audio device: ") + Mix_GetError();
        } else {
            WHBLogPrintf("AudioPlayerScreen: SDL_mixer initialized");
        }
    }
}

AudioPlayerScreen::~AudioPlayerScreen() {
    if (mAudioDecoder) {
        delete mAudioDecoder;
        mAudioDecoder = nullptr;
    }

    if (mMusic) {
        Mix_HaltMusic();
        Mix_FreeMusic(mMusic);
        mMusic = nullptr;
    }

    if (!UseFFmpeg()) {
        Mix_CloseAudio();
        WHBLogPrintf("AudioPlayerScreen: Closed SDL_mixer");
    }
}

static std::string ToLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), ::tolower);
    return out;
}

bool AudioPlayerScreen::IsWavFile()  const { return ToLower(mAudioPath).ends_with(".wav");  }
bool AudioPlayerScreen::IsOggFile()  const { return ToLower(mAudioPath).ends_with(".ogg");  }
bool AudioPlayerScreen::IsFlacFile() const { return ToLower(mAudioPath).ends_with(".flac"); }
bool AudioPlayerScreen::UseFFmpeg()  const { return IsOggFile() || IsFlacFile() || IsWavFile(); }

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

    // Speaker icon
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

bool AudioPlayerScreen::Update(Input& input) {
    if (input.data.buttons_d & Input::BUTTON_B) {
        mShouldClose = true;
        return false;
    }

    if (input.data.buttons_d & Input::BUTTON_A) {
        if (UseFFmpeg()) {
            // FFmpeg path
            if (!mIsPlaying) {
                WHBLogPrintf("AudioPlayerScreen: Starting FFmpeg playback");
                mAudioDecoder->StartAudio();
                mIsPlaying = true;
                mIsPaused  = false;
            } else {
                if (mIsPaused) {
                    WHBLogPrintf("AudioPlayerScreen: Resuming FFmpeg playback");
                    mAudioDecoder->PauseAudio(false);
                    mIsPaused = false;
                } else {
                    WHBLogPrintf("AudioPlayerScreen: Pausing FFmpeg playback");
                    mAudioDecoder->PauseAudio(true);
                    mIsPaused = true;
                }
            }
        } else {
            // SDL_mixer path
            if (!mIsPlaying) {
                WHBLogPrintf("AudioPlayerScreen: Starting SDL_mixer playback");
                if (Mix_PlayMusic(mMusic, 0) == -1) {
                    WHBLogPrintf("AudioPlayerScreen: Mix_PlayMusic failed: %s", Mix_GetError());
                } else {
                    mIsPlaying = true;
                    mIsPaused  = false;
                }
            } else {
                if (mIsPaused) {
                    Mix_ResumeMusic();
                    mIsPaused = false;
                } else {
                    Mix_PauseMusic();
                    mIsPaused = true;
                }
            }
        }
    }

    if (input.data.buttons_d & Input::BUTTON_L) {
        if (mSeekSupported) {
            WHBLogPrintf("AudioPlayerScreen: Seeking backward");
            if (UseFFmpeg()) {
                double newPos = mAudioDecoder->GetCurrentTime() - 10.0;
                if (newPos < 0) newPos = 0;
                mAudioDecoder->Seek(newPos);
            } else if (mIsPlaying) {
                double cur = Mix_GetMusicPosition(mMusic);
                if (cur < 0) cur = 0;
                double newPos = cur - 10.0;
                if (newPos < 0) newPos = 0;
                Mix_SetMusicPosition(newPos);
            }
        } else {
            WHBLogPrintf("AudioPlayerScreen: Seek not supported for this format");
        }
    }

    if (input.data.buttons_d & Input::BUTTON_R) {
        if (mSeekSupported) {
            WHBLogPrintf("AudioPlayerScreen: Seeking forward");
            if (UseFFmpeg()) {
                double newPos = mAudioDecoder->GetCurrentTime() + 10.0;
                mAudioDecoder->Seek(newPos);
            } else if (mIsPlaying) {
                double cur = Mix_GetMusicPosition(mMusic);
                if (cur < 0) cur = 0;
                double newPos = cur + 10.0;
                Mix_SetMusicPosition(newPos);
            }
        } else {
            WHBLogPrintf("AudioPlayerScreen: Seek not supported for this format");
        }
    }

    // Checks for end of playback
    if (mIsPlaying && !mIsPaused) {
        if (UseFFmpeg()) {
            if (!mAudioDecoder->IsPlaying() && !mAudioDecoder->IsPaused()) {
                WHBLogPrintf("AudioPlayerScreen: FFmpeg playback ended");
                mIsPlaying = false;
            }
        } else {
            if (!Mix_PlayingMusic()) {
                WHBLogPrintf("AudioPlayerScreen: SDL_mixer playback ended");
                mIsPlaying = false;
            }
        }
    }

    return true;
}

void AudioPlayerScreen::DrawPlaybackControls() {
    const char* statusText = "Not Playing";
    if (mIsPlaying) statusText = mIsPaused ? "Paused" : "Playing";

    Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2 + 100, 48,
               Gfx::COLOR_WHITE, statusText, Gfx::ALIGN_CENTER);

    double currentTime = 0.0;
    if (UseFFmpeg() && mAudioDecoder) {
        currentTime = mAudioDecoder->GetCurrentTime();
    } else if (mMusic) {
        currentTime = Mix_GetMusicPosition(mMusic);
    }

    char timeStr[64];
    snprintf(timeStr, sizeof(timeStr), "%.1f / %.1f s", currentTime, mDuration);
    Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2 + 160, 36,
               Gfx::COLOR_WHITE, timeStr, Gfx::ALIGN_CENTER);

    // Progress bar
    int barWidth  = Gfx::SCREEN_WIDTH - 200;
    int barHeight = 10;
    int barX      = (Gfx::SCREEN_WIDTH - barWidth) / 2;
    int barY      = Gfx::SCREEN_HEIGHT - 150;

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

    WHBLogPrintf("AudioPlayerScreen: Loading: %s", mAudioPath.c_str());

    mSeekSupported = true;

    if (UseFFmpeg()) {
        mAudioDecoder = new AudioDecoder();
        if (!mAudioDecoder->Open(mAudioPath)) {
            mLoadError    = true;
            mErrorMessage = "Failed to open audio file (FFmpeg)";
            WHBLogPrintf("AudioPlayerScreen: AudioDecoder::Open failed");
            delete mAudioDecoder;
            mAudioDecoder = nullptr;
            return;
        }
        mDuration = mAudioDecoder->GetDuration();
        WHBLogPrintf("AudioPlayerScreen: FFmpeg decoder ready. Duration=%.2fs", mDuration);
    } else {
        mMusic = Mix_LoadMUS(mAudioPath.c_str());
        if (!mMusic) {
            mLoadError    = true;
            mErrorMessage = std::string("SDL_mixer error: ") + Mix_GetError();
            WHBLogPrintf("AudioPlayerScreen: Mix_LoadMUS failed: %s", Mix_GetError());
            return;
        }
        mDuration = Mix_MusicDuration(mMusic);
        if (mDuration <= 0) {
            WHBLogPrintf("AudioPlayerScreen: Duration unavailable, using 180s estimate");
            mDuration = 180.0;
        }
        WHBLogPrintf("AudioPlayerScreen: SDL_mixer ready. Duration=%.2fs", mDuration);
    }
}
