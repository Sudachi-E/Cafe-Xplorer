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
    int mOffsetX;
    int mOffsetY;
    bool mShouldClose;
    bool mLoadError;
    
    void CalculateDisplayRect(SDL_Rect& rect);
};
