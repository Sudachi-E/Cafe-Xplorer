#include "GifViewerScreen.hpp"
#include "Gfx.hpp"
#include "../filemanager/PathConverter.hpp"
#include <whb/log.h>
#include <algorithm>
#include <cstring>
#include <cstdio>

static constexpr int MIN_FRAME_DELAY = 10;

GifViewerScreen::GifViewerScreen(const std::string& gifPath)
    : mGifPath(gifPath),
      mFrameCount(0), mCurrentFrame(0), mLastFrameTime(0),
      mImageWidth(0), mImageHeight(0),
      mZoom(1.0f), mOffsetX(0.f), mOffsetY(0.f), mFitScale(1.0f),
      mIsPlaying(true), mShouldClose(false), mLoadError(false), mBarsHidden(false) {

    WHBLogPrintf("GifViewerScreen: Loading %s", gifPath.c_str());

    std::string realPath = PathConverter::ToRealPath(gifPath);
    WHBLogPrintf("GifViewerScreen: Real path: %s", realPath.c_str());

    FILE* f = fopen(realPath.c_str(), "rb");
    if (!f) {
        WHBLogPrintf("GifViewerScreen: fopen failed for: %s", realPath.c_str());
        mLoadError = true;
        return;
    }

    SDL_RWops* rw = SDL_RWFromFP(f, SDL_TRUE);
    if (!rw) {
        WHBLogPrintf("GifViewerScreen: SDL_RWFromFP failed: %s", SDL_GetError());
        fclose(f);
        mLoadError = true;
        return;
    }

    IMG_Animation* anim = IMG_LoadAnimation_RW(rw, 1);
    if (!anim) {
        WHBLogPrintf("GifViewerScreen: IMG_LoadAnimation_RW failed: %s", IMG_GetError());
        mLoadError = true;
        return;
    }

    mImageWidth  = anim->w;
    mImageHeight = anim->h;
    mFrameCount  = anim->count;

    WHBLogPrintf("GifViewerScreen: %d frames, %dx%d", mFrameCount, mImageWidth, mImageHeight);

    SDL_Renderer* renderer = Gfx::GetRenderer();

    for (int i = 0; i < mFrameCount; i++) {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, anim->frames[i]);
        if (!tex) {
            WHBLogPrintf("GifViewerScreen: SDL_CreateTextureFromSurface failed frame %d: %s",
                         i, SDL_GetError());
            // Clean up already-created textures and bail
            for (auto* t : mFrames) SDL_DestroyTexture(t);
            mFrames.clear();
            IMG_FreeAnimation(anim);
            mLoadError = true;
            return;
        }
        mFrames.push_back(tex);

        int delay = anim->delays[i];
        if (delay < MIN_FRAME_DELAY) delay = MIN_FRAME_DELAY;
        mDelays.push_back(delay);
    }

    IMG_FreeAnimation(anim);

    mLastFrameTime = SDL_GetTicks();
    WHBLogPrintf("GifViewerScreen: Loaded successfully");

    int viewportW = Gfx::SCREEN_WIDTH;
    int viewportH = Gfx::SCREEN_HEIGHT - 120;
    float scaleX = (float)viewportW / mImageWidth;
    float scaleY = (float)viewportH / mImageHeight;
    mFitScale = (scaleX < scaleY) ? scaleX : scaleY;
}

GifViewerScreen::~GifViewerScreen() {
    for (auto* tex : mFrames) {
        SDL_DestroyTexture(tex);
    }
}

void GifViewerScreen::Draw() {
    Gfx::Clear(Gfx::COLOR_BLACK);

    if (!mBarsHidden) {
        size_t slash = mGifPath.find_last_of('/');
        std::string filename = (slash != std::string::npos) ? mGifPath.substr(slash + 1) : mGifPath;
        DrawTopBar(filename.c_str());
        DrawBottomBar("B: Back/Reset", "A: Play/Pause  X: Reset  +: Toggle UI", "Stick: Pan  Zoom: Zoom");
    }

    if (mLoadError) {
        Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2, 48,
                   Gfx::COLOR_WHITE, "Failed to load GIF", Gfx::ALIGN_CENTER);
        return;
    }

    if (mFrames.empty()) return;

    SDL_Rect dst;
    CalculateDisplayRect(dst);

    int topBar = mBarsHidden ? 0 : 60;
    int bottomBar = mBarsHidden ? 0 : 60;
    SDL_Rect clip = {0, topBar, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT - topBar - bottomBar};
    SDL_RenderSetClipRect(Gfx::GetRenderer(), &clip);

    SDL_RenderCopy(Gfx::GetRenderer(), mFrames[mCurrentFrame], nullptr, &dst);

    SDL_RenderSetClipRect(Gfx::GetRenderer(), nullptr);

    // Frame counter + zoom
    char info[64];
    snprintf(info, sizeof(info), "%d/%d  %.0f%%",
             mCurrentFrame + 1, mFrameCount, mZoom * 100.0f);
}

bool GifViewerScreen::Update(Input& input) {
    if (input.data.buttons_d & Input::BUTTON_PLUS) {
        mBarsHidden = !mBarsHidden;
    }

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

    // Play / pause toggle
    if (input.data.buttons_d & Input::BUTTON_A) {
        mIsPlaying = !mIsPlaying;
        if (mIsPlaying) {
            mLastFrameTime = SDL_GetTicks();
        }
    }

    // Reset view
    if (input.data.buttons_d & Input::BUTTON_X) {
        mZoom    = ZOOM_MIN;
        mOffsetX = 0.f;
        mOffsetY = 0.f;
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

    // Right stick zoom
    const float deadzone = 0.2f;
    if (std::abs(input.data.rightStickY) > deadzone) {
        mZoom += input.data.rightStickY * ZOOM_STEP;
        if (mZoom > ZOOM_MAX) mZoom = ZOOM_MAX;
        if (mZoom < ZOOM_MIN) mZoom = ZOOM_MIN;
    }

    // Left stick pan (only when zoomed in)
    if (mZoom > ZOOM_MIN + 0.01f) {
        if (std::abs(input.data.leftStickX) > deadzone || std::abs(input.data.leftStickY) > deadzone) {
            mOffsetX -= input.data.leftStickX * PAN_SPEED * mZoom;
            mOffsetY += input.data.leftStickY * PAN_SPEED * mZoom;
        }

        const int dpadPan = 20;
        if (input.data.buttons_h & Input::BUTTON_LEFT)  mOffsetX -= dpadPan;
        if (input.data.buttons_h & Input::BUTTON_RIGHT) mOffsetX += dpadPan;
        if (input.data.buttons_h & Input::BUTTON_UP)    mOffsetY -= dpadPan;
        if (input.data.buttons_h & Input::BUTTON_DOWN)  mOffsetY += dpadPan;
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

    // Advance animation frame
    if (mIsPlaying && mFrameCount > 1) {
        Uint32 now = SDL_GetTicks();
        if (now - mLastFrameTime >= static_cast<Uint32>(mDelays[mCurrentFrame])) {
            mCurrentFrame  = (mCurrentFrame + 1) % mFrameCount;
            mLastFrameTime = now;
        }
    }

    return true;
}

void GifViewerScreen::CalculateDisplayRect(SDL_Rect& rect) {
    float scale = mFitScale * mZoom;
    int drawW = (int)(mImageWidth  * scale);
    int drawH = (int)(mImageHeight * scale);

    int topBar = mBarsHidden ? 0 : 60;
    int bottomBar = mBarsHidden ? 0 : 60;
    int viewportH = Gfx::SCREEN_HEIGHT - topBar - bottomBar;
    rect.x = ((int)Gfx::SCREEN_WIDTH  - drawW) / 2 + (int)mOffsetX;
    rect.y = topBar + (viewportH - drawH) / 2 + (int)mOffsetY;
    rect.w = drawW;
    rect.h = drawH;
}
