#pragma once

#include "../Screen.hpp"
#include <SDL.h>
#include <string>

class ImageViewerScreen : public Screen {
public:
    explicit ImageViewerScreen(const std::string& imagePath);
    ~ImageViewerScreen() override;
    void Draw() override;
    bool Update(Input &input) override;
    bool ShouldClose() const { return mShouldClose; }

private:
    std::string mImagePath;
    SDL_Texture* mTexture;
    int mImageWidth;
    int mImageHeight;
    float mZoom;
    float mOffsetX;
    float mOffsetY;
    float mFitScale;

    static constexpr float ZOOM_MIN  = 1.0f;
    static constexpr float ZOOM_MAX  = 8.0f;
    static constexpr float ZOOM_STEP = 0.05f;
    static constexpr float PAN_SPEED = 8.0f;
    bool mShouldClose;
    bool mLoadError;
    bool mBarsHidden;

    void CalculateDisplayRect(SDL_Rect& rect);
};
