#pragma once

#include "../Screen.hpp"
#include "../audio/AudioDecoder.hpp"
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

    // Format helpers
    bool IsWavFile()  const;
    bool IsOggFile()  const;
    bool IsFlacFile() const;
    bool UseFFmpeg()  const;

    std::string mAudioPath;

    Mix_Music* mMusic;

    AudioDecoder* mAudioDecoder;

    bool mShouldClose;
    bool mLoadError;
    bool mIsPlaying;
    bool mIsPaused;
    bool mInitialized;
    bool mSeekSupported;

    double mDuration;

    std::string mErrorMessage;
    int         mErrorCode;
};
