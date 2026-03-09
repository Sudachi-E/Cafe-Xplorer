#pragma once

#include "../Screen.hpp"
#include "../video/VideoDecoder.hpp"
#include <SDL2/SDL.h>
#include <string>

class VideoPlayerScreen : public Screen {
public:
    VideoPlayerScreen(const std::string& videoPath);
    ~VideoPlayerScreen();
    
    void Draw() override;
    bool Update(Input &input) override;
    
    bool ShouldClose() const { return mShouldClose; }
    
private:
    void DrawPlaybackControls();
    void CalculateDisplayRect(SDL_Rect& rect);
    void UpdatePlayback();
    void InitializeVideo();
    
    std::string mVideoPath;
    VideoDecoder mDecoder;
    SDL_Texture* mVideoTexture;
    
    bool mShouldClose;
    bool mLoadError;
    bool mIsPlaying;
    bool mIsPaused;
    bool mInitialized;
    bool mShowUI;
    bool mIsRawVideo;
    bool mShowRawVideoWarning;
    
    int mVideoWidth;
    int mVideoHeight;
    
    Uint32 mPlaybackStartTime;
    double mPlaybackStartPTS;
    double mFrameDelay;
};
