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
      mZoom(1.0f), mOffsetX(0), mOffsetY(0),
      mIsPlaying(true), mShouldClose(false), mLoadError(false) {

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
}

GifViewerScreen::~GifViewerScreen() {
    for (auto* tex : mFrames) {
        SDL_DestroyTexture(tex);
    }
}

void GifViewerScreen::Draw() {
    Gfx::Clear(Gfx::COLOR_BLACK);

    DrawTopBar(mGifPath.c_str());
    DrawBottomBar("B: Back", "A: Play/Pause  X: Reset", "Zoom: L/R");

    if (mLoadError) {
        Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2, 48,
                   Gfx::COLOR_WHITE, "Failed to load GIF", Gfx::ALIGN_CENTER);
        return;
    }

    if (mFrames.empty()) return;

    SDL_Rect dst;
    CalculateDisplayRect(dst);
    SDL_RenderCopy(Gfx::GetRenderer(), mFrames[mCurrentFrame], nullptr, &dst);

    // Frame counter + zoom
    char info[64];
    snprintf(info, sizeof(info), "%d/%d  %.0f%%",
             mCurrentFrame + 1, mFrameCount, mZoom * 100.0f);
    Gfx::Print(Gfx::SCREEN_WIDTH - 20, 80, 32, Gfx::COLOR_WHITE, info, Gfx::ALIGN_RIGHT);

    if (!mIsPlaying) {
        Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT - 80, 36,
                   Gfx::COLOR_ALT_TEXT, "Paused", Gfx::ALIGN_CENTER);
    }
}

bool GifViewerScreen::Update(Input& input) {
    if (input.data.buttons_d & Input::BUTTON_B) {
        mShouldClose = true;
        return false;
    }

    // Play / pause toggle
    if (input.data.buttons_d & Input::BUTTON_A) {
        mIsPlaying = !mIsPlaying;
        if (mIsPlaying) {
            // Reset timer so we don't skip frames after a long pause
            mLastFrameTime = SDL_GetTicks();
        }
    }

    // Reset view
    if (input.data.buttons_d & Input::BUTTON_X) {
        mZoom    = 1.0f;
        mOffsetX = 0;
        mOffsetY = 0;
    }

    // Zoom with L/R buttons
    if (input.data.buttons_d & Input::BUTTON_R) {
        mZoom *= 1.25f;
        if (mZoom > 5.0f) mZoom = 5.0f;
    }
    if (input.data.buttons_d & Input::BUTTON_L) {
        mZoom /= 1.25f;
        if (mZoom < 0.1f) mZoom = 0.1f;
    }

    // Right stick zoom
    const float deadzone = 0.2f;
    if (std::abs(input.data.rightStickY) > deadzone) {
        mZoom += input.data.rightStickY * 0.02f;
        if (mZoom > 5.0f) mZoom = 5.0f;
        if (mZoom < 0.1f) mZoom = 0.1f;
    }

    // Left stick pan
    if (std::abs(input.data.leftStickX) > deadzone || std::abs(input.data.leftStickY) > deadzone) {
        mOffsetX += static_cast<int>(input.data.leftStickX * 10.0f);
        mOffsetY -= static_cast<int>(input.data.leftStickY * 10.0f);
    }

    // D-pad pan when zoomed in
    if (mZoom > 1.0f) {
        const int panSpeed = 20;
        if (input.data.buttons_h & Input::BUTTON_LEFT)  mOffsetX += panSpeed;
        if (input.data.buttons_h & Input::BUTTON_RIGHT) mOffsetX -= panSpeed;
        if (input.data.buttons_h & Input::BUTTON_UP)    mOffsetY += panSpeed;
        if (input.data.buttons_h & Input::BUTTON_DOWN)  mOffsetY -= panSpeed;
    }

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
    int scaledW = static_cast<int>(mImageWidth  * mZoom);
    int scaledH = static_cast<int>(mImageHeight * mZoom);

    int viewportH = Gfx::SCREEN_HEIGHT - 120; // top + bottom bars
    rect.x = (Gfx::SCREEN_WIDTH  - scaledW) / 2 + mOffsetX;
    rect.y = 60 + (viewportH - scaledH) / 2   + mOffsetY;
    rect.w = scaledW;
    rect.h = scaledH;
}
