#pragma once

#include "../Screen.hpp"
#include <SDL.h>
#include <SDL_image.h>
#include <string>
#include <vector>

class GifViewerScreen : public Screen {
public:
    explicit GifViewerScreen(const std::string& gifPath);
    ~GifViewerScreen() override;

    void Draw() override;
    bool Update(Input& input) override;

    bool ShouldClose() const { return mShouldClose; }

private:
    void CalculateDisplayRect(SDL_Rect& rect);

    std::string mGifPath;

    std::vector<SDL_Texture*> mFrames;
    std::vector<int>          mDelays;

    int    mFrameCount;
    int    mCurrentFrame;
    Uint32 mLastFrameTime;

    int   mImageWidth;
    int   mImageHeight;
    float mZoom;
    float mOffsetX;
    float mOffsetY;
    float mFitScale;

    static constexpr float ZOOM_MIN  = 1.0f;
    static constexpr float ZOOM_MAX  = 8.0f;
    static constexpr float ZOOM_STEP = 0.05f;
    static constexpr float PAN_SPEED = 8.0f;

    bool mIsPlaying;
    bool mShouldClose;
    bool mLoadError;
    bool mBarsHidden;
};
