#pragma once

#include "../Screen.hpp"
#include <string>
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

class AudioPlayerScreen : public Screen {
public:
    AudioPlayerScreen(const std::string& audioPath);
    ~AudioPlayerScreen();
    
    void Draw() override;
    bool Update(Input &input) override;
    
    bool ShouldClose() const { return mShouldClose; }
    
private:
    void DrawPlaybackControls();
    void InitializeAudio();
    
    std::string mAudioPath;
    Mix_Music* mMusic;
    
    bool mShouldClose;
    bool mLoadError;
    bool mIsPlaying;
    bool mIsPaused;
    bool mInitialized;
    
    double mDuration;
    
    std::string mErrorMessage;
    int mErrorCode;
};
