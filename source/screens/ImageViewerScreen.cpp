#include "ImageViewerScreen.hpp"
#include "../Gfx.hpp"
#include <SDL_image.h>
#include <whb/log.h>
#include <algorithm>

ImageViewerScreen::ImageViewerScreen(const std::string& imagePath)
    : mImagePath(imagePath), mTexture(nullptr), mImageWidth(0), mImageHeight(0),
      mZoom(1.0f), mOffsetX(0), mOffsetY(0), mShouldClose(false), mLoadError(false) {
    
    WHBLogPrintf("Loading image: %s", imagePath.c_str());
    
    int imgFlags = IMG_INIT_PNG | IMG_INIT_JPG;
    if (!(IMG_Init(imgFlags) & imgFlags)) {
        WHBLogPrintf("SDL_image init failed: %s", IMG_GetError());
        mLoadError = true;
        return;
    }
    
    SDL_Surface* surface = IMG_Load(imagePath.c_str());
    if (!surface) {
        WHBLogPrintf("Failed to load image: %s", IMG_GetError());
        mLoadError = true;
        return;
    }
    
    mImageWidth = surface->w;
    mImageHeight = surface->h;
    
    mTexture = SDL_CreateTextureFromSurface(Gfx::GetRenderer(), surface);
    SDL_FreeSurface(surface);
    
    if (!mTexture) {
        WHBLogPrintf("Failed to create texture: %s", SDL_GetError());
        mLoadError = true;
        return;
    }
    
    WHBLogPrintf("Image loaded successfully: %dx%d", mImageWidth, mImageHeight);
}

ImageViewerScreen::~ImageViewerScreen() {
    if (mTexture) {
        SDL_DestroyTexture(mTexture);
    }
    IMG_Quit();
}

void ImageViewerScreen::Draw() {
    Gfx::Clear(Gfx::COLOR_BLACK);
    
    DrawTopBar(mImagePath.c_str());
    DrawBottomBar("B: Back", "L/R: Zoom", "X: Reset");
    
    if (mLoadError) {
        Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2, 48,
                   Gfx::COLOR_WHITE, "Failed to load image", Gfx::ALIGN_CENTER);
        return;
    }
    
    if (!mTexture) {
        return;
    }
    
    SDL_Rect dstRect;
    CalculateDisplayRect(dstRect);
    
    SDL_RenderCopy(Gfx::GetRenderer(), mTexture, nullptr, &dstRect);
    
    char zoomText[32];
    snprintf(zoomText, sizeof(zoomText), "%.0f%%", mZoom * 100.0f);
    Gfx::Print(Gfx::SCREEN_WIDTH - 20, 80, 32, Gfx::COLOR_WHITE, zoomText, Gfx::ALIGN_RIGHT);
}

bool ImageViewerScreen::Update(Input &input) {
    if (input.data.buttons_d & Input::BUTTON_B) {
        mShouldClose = true;
        return false;
    }
    
    if (input.data.buttons_d & Input::BUTTON_R) {
        mZoom *= 1.25f;
        if (mZoom > 5.0f) mZoom = 5.0f;
    }
    
    if (input.data.buttons_d & Input::BUTTON_L) {
        mZoom /= 1.25f;
        if (mZoom < 0.1f) mZoom = 0.1f;
    }
    
    if (mZoom > 1.0f) {
        int panSpeed = 20;
        if (input.data.buttons_h & Input::BUTTON_LEFT) {
            mOffsetX += panSpeed;
        }
        if (input.data.buttons_h & Input::BUTTON_RIGHT) {
            mOffsetX -= panSpeed;
        }
        if (input.data.buttons_h & Input::BUTTON_UP) {
            mOffsetY += panSpeed;
        }
        if (input.data.buttons_h & Input::BUTTON_DOWN) {
            mOffsetY -= panSpeed;
        }
    }
    
    // Reset view
    if (input.data.buttons_d & Input::BUTTON_X) {
        mZoom = 1.0f;
        mOffsetX = 0;
        mOffsetY = 0;
    }
    
    return true;
}

void ImageViewerScreen::CalculateDisplayRect(SDL_Rect& rect) {
    int scaledWidth = static_cast<int>(mImageWidth * mZoom);
    int scaledHeight = static_cast<int>(mImageHeight * mZoom);
    
    int viewportHeight = Gfx::SCREEN_HEIGHT - 120; // Account for bars
    rect.x = (Gfx::SCREEN_WIDTH - scaledWidth) / 2 + mOffsetX;
    rect.y = 60 + (viewportHeight - scaledHeight) / 2 + mOffsetY;
    rect.w = scaledWidth;
    rect.h = scaledHeight;
}
