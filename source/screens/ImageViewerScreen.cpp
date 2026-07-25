#include "ImageViewerScreen.hpp"
#include "../Gfx.hpp"
#include "../filemanager/PathConverter.hpp"
#include <SDL_image.h>
#include <whb/log.h>
#include <algorithm>

ImageViewerScreen::ImageViewerScreen(const std::string& imagePath)
    : mImagePath(imagePath), mTexture(nullptr), mImageWidth(0), mImageHeight(0),
      mZoom(1.0f), mOffsetX(0), mOffsetY(0), mShouldClose(false), mLoadError(false), mBarsHidden(false) {
    
    WHBLogPrintf("Loading image: %s", imagePath.c_str());
    
    int imgFlags = IMG_INIT_PNG | IMG_INIT_JPG;
    if (!(IMG_Init(imgFlags) & imgFlags)) {
        WHBLogPrintf("SDL_image init failed: %s", IMG_GetError());
        mLoadError = true;
        return;
    }
    
    std::string realPath = PathConverter::ToRealPath(imagePath);
    SDL_Surface* surface = IMG_Load(realPath.c_str());
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

    int topBar = 60;
    int bottomBar = 60;
    int viewportW = Gfx::SCREEN_WIDTH;
    int viewportH = Gfx::SCREEN_HEIGHT - topBar - bottomBar;
    float scaleX = (float)viewportW / mImageWidth;
    float scaleY = (float)viewportH / mImageHeight;
    mFitScale = (scaleX < scaleY) ? scaleX : scaleY;
}

ImageViewerScreen::~ImageViewerScreen() {
    if (mTexture) {
        SDL_DestroyTexture(mTexture);
    }
    IMG_Quit();
}

void ImageViewerScreen::Draw() {
    Gfx::Clear(Gfx::COLOR_BLACK);

    if (!mBarsHidden) {
        size_t slash = mImagePath.find_last_of('/');
        std::string filename = (slash != std::string::npos) ? mImagePath.substr(slash + 1) : mImagePath;
        DrawTopBar(filename.c_str());
        DrawBottomBar("B: Back/Reset", "Stick: Pan  Zoom: Right Stick", "X: Reset  +: Toggle UI");
    }
    
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

    int topBar = mBarsHidden ? 0 : 60;
    int bottomBar = mBarsHidden ? 0 : 60;
    SDL_Rect clip = {0, topBar, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT - topBar - bottomBar};
    SDL_RenderSetClipRect(Gfx::GetRenderer(), &clip);

    SDL_RenderCopy(Gfx::GetRenderer(), mTexture, nullptr, &dstRect);

    SDL_RenderSetClipRect(Gfx::GetRenderer(), nullptr);
    
    char zoomText[32];
    snprintf(zoomText, sizeof(zoomText), "%.0f%%", mZoom * 100.0f);
}

bool ImageViewerScreen::Update(Input &input) {
    if (input.data.buttons_d & Input::BUTTON_B) {
        if (mZoom > ZOOM_MIN + 0.01f) {
            mZoom = ZOOM_MIN;
            mOffsetX = 0.f;
            mOffsetY = 0.f;
        } else {
            mShouldClose = true;
            return false;
        }
    }

    if (input.data.buttons_d & Input::BUTTON_PLUS) {
        mBarsHidden = !mBarsHidden;
    }

    // Button zoom
    if (input.data.buttons_d & Input::BUTTON_R) {
        mZoom += ZOOM_STEP;
        if (mZoom > ZOOM_MAX) mZoom = ZOOM_MAX;
    }

    if (input.data.buttons_d & Input::BUTTON_L) {
        mZoom -= ZOOM_STEP;
        if (mZoom < ZOOM_MIN) mZoom = ZOOM_MIN;
    }

    // Right stick zoom (vertical axis)
    const float deadzone = 0.2f;
    if (std::abs(input.data.rightStickY) > deadzone) {
        mZoom += input.data.rightStickY * ZOOM_STEP;
        if (mZoom > ZOOM_MAX) mZoom = ZOOM_MAX;
        if (mZoom < ZOOM_MIN) mZoom = ZOOM_MIN;
    }

    // Left stick panning (only when zoomed in)
    if (mZoom > ZOOM_MIN + 0.01f) {
        if (std::abs(input.data.leftStickX) > deadzone || std::abs(input.data.leftStickY) > deadzone) {
            mOffsetX -= input.data.leftStickX * PAN_SPEED * mZoom;
            mOffsetY += input.data.leftStickY * PAN_SPEED * mZoom;
        }

        int dpadPan = 20;
        if (input.data.buttons_h & Input::BUTTON_LEFT) {
            mOffsetX += dpadPan;
        }
        if (input.data.buttons_h & Input::BUTTON_RIGHT) {
            mOffsetX -= dpadPan;
        }
        if (input.data.buttons_h & Input::BUTTON_UP) {
            mOffsetY += dpadPan;
        }
        if (input.data.buttons_h & Input::BUTTON_DOWN) {
            mOffsetY -= dpadPan;
        }
    }

    // Clamp pan to image bounds
    int topBar = mBarsHidden ? 0 : 60;
    int bottomBar = mBarsHidden ? 0 : 60;
    int viewportH = Gfx::SCREEN_HEIGHT - topBar - bottomBar;
    int drawW = (int)(mImageWidth * mFitScale * mZoom);
    int drawH = (int)(mImageHeight * mFitScale * mZoom);
    float maxPanX = (drawW > (int)Gfx::SCREEN_WIDTH) ? (drawW - Gfx::SCREEN_WIDTH) * 0.5f : 0.f;
    float maxPanY = (drawH > viewportH) ? (drawH - viewportH) * 0.5f : 0.f;
    if (mOffsetX >  maxPanX) mOffsetX =  maxPanX;
    if (mOffsetX < -maxPanX) mOffsetX = -maxPanX;
    if (mOffsetY >  maxPanY) mOffsetY =  maxPanY;
    if (mOffsetY < -maxPanY) mOffsetY = -maxPanY;

    // Reset view
    if (input.data.buttons_d & Input::BUTTON_X) {
        mZoom = ZOOM_MIN;
        mOffsetX = 0.f;
        mOffsetY = 0.f;
    }

    return true;
}

void ImageViewerScreen::CalculateDisplayRect(SDL_Rect& rect) {
    float scale = mFitScale * mZoom;
    int drawW = (int)(mImageWidth * scale);
    int drawH = (int)(mImageHeight * scale);

    int topBar = mBarsHidden ? 0 : 60;
    int bottomBar = mBarsHidden ? 0 : 60;
    int viewportH = Gfx::SCREEN_HEIGHT - topBar - bottomBar;
    rect.x = ((int)Gfx::SCREEN_WIDTH - drawW) / 2 + (int)mOffsetX;
    rect.y = topBar + (viewportH - drawH) / 2 + (int)mOffsetY;
    rect.w = drawW;
    rect.h = drawH;
}
