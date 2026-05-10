#include "PdfViewerScreen.hpp"
#include "../Gfx.hpp"
#include "../filemanager/PathConverter.hpp"
#include <whb/log.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>

static constexpr int BAR_HEIGHT = 60;
static constexpr int VIEWPORT_Y = BAR_HEIGHT;
static constexpr int VIEWPORT_H = Gfx::SCREEN_HEIGHT - BAR_HEIGHT * 2;
static constexpr int VIEWPORT_W = Gfx::SCREEN_WIDTH;

int PdfViewerScreen::RenderThreadEntry(int /*argc*/, const char** argv)
{
    PdfViewerScreen* self = reinterpret_cast<PdfViewerScreen*>(argv);
    self->RenderWorker();
    return 0;
}

PdfViewerScreen::PdfViewerScreen(const std::string& pdfPath)
    : mPdfPath(pdfPath),
      mCtx(nullptr), mDoc(nullptr),
      mPageCount(0), mCurrentPage(0),
      mTexture(nullptr), mTexWidth(0), mTexHeight(0),
      mPendingW(0), mPendingH(0), mPendingStride(0), mResultReady(false),
      mRequestPage(0), mRequestZoom(1.0f), mRequestRotation(0),
      mRenderThreadRunning(false), mStopThread(false),
      mThreadStack(nullptr),
      mZoom(1.0f), mOffsetX(0), mOffsetY(0), mRotation(0),
      mLastRequestedZoom(-1.0f), mLastRequestedPage(-1), mLastRequestedRotation(0),
      mZoomSettleFrames(0), mRendering(false),
      mShouldClose(false), mLoadError(false)
{
    OSInitMutex(&mResultMutex);
    OSInitMutex(&mRequestMutex);
    OSInitSemaphore(&mRequestSem, 0);

    std::string realPath = PathConverter::ToRealPath(pdfPath);
    WHBLogPrintf("PdfViewerScreen: Opening %s", realPath.c_str());

    mCtx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!mCtx) {
        mLoadError    = true;
        mErrorMessage = "Failed to create MuPDF context";
        return;
    }

    fz_register_document_handlers(mCtx);

    fz_try(mCtx) {
        mDoc = fz_open_document(mCtx, realPath.c_str());
    }
    fz_catch(mCtx) {
        mLoadError    = true;
        mErrorMessage = std::string("Failed to open: ") + fz_caught_message(mCtx);
        WHBLogPrintf("PdfViewerScreen: open failed: %s", fz_caught_message(mCtx));
        return;
    }

    fz_try(mCtx) {
        mPageCount = fz_count_pages(mCtx, mDoc);
    }
    fz_catch(mCtx) {
        mPageCount = 0;
    }

    WHBLogPrintf("PdfViewerScreen: %d pages", mPageCount);
    if (mPageCount <= 0) {
        mLoadError    = true;
        mErrorMessage = "Document has no pages";
        return;
    }

    // Start the render thread
    mThreadStack = static_cast<uint8_t*>(std::malloc(THREAD_STACK_SIZE));
    if (!mThreadStack) {
        mLoadError    = true;
        mErrorMessage = "Out of memory for render thread";
        return;
    }

    OSCreateThread(&mThread,
                   RenderThreadEntry,
                   0, reinterpret_cast<char*>(this),
                   mThreadStack + THREAD_STACK_SIZE,
                   THREAD_STACK_SIZE,
                   16,
                   OS_THREAD_ATTRIB_AFFINITY_ANY);
    OSResumeThread(&mThread);
    mRenderThreadRunning = true;

    RequestRender(0, 1.0f, 0);
}

PdfViewerScreen::~PdfViewerScreen()
{
    // Tells the render thread to stop and wake it
    if (mRenderThreadRunning) {
        OSLockMutex(&mRequestMutex);
        mStopThread = true;
        OSUnlockMutex(&mRequestMutex);
        OSSignalSemaphore(&mRequestSem);
        OSJoinThread(&mThread, nullptr);
        mRenderThreadRunning = false;
    }

    if (mThreadStack) {
        std::free(mThreadStack);
        mThreadStack = nullptr;
    }

    if (mTexture) {
        SDL_DestroyTexture(mTexture);
        mTexture = nullptr;
    }

    if (mDoc && mCtx) {
        fz_drop_document(mCtx, mDoc);
        mDoc = nullptr;
    }
    if (mCtx) {
        fz_drop_context(mCtx);
        mCtx = nullptr;
    }
}

void PdfViewerScreen::RequestRender(int page, float zoom, int rotation)
{
    OSLockMutex(&mRequestMutex);
    mRequestPage     = page;
    mRequestZoom     = zoom;
    mRequestRotation = rotation;
    OSUnlockMutex(&mRequestMutex);

    while (OSTryWaitSemaphore(&mRequestSem) > 0) { /* drain */ }
    OSSignalSemaphore(&mRequestSem);

    mRendering = true;
}

void PdfViewerScreen::RenderWorker()
{
    while (true) {
        OSWaitSemaphore(&mRequestSem);

        OSLockMutex(&mRequestMutex);
        bool stop  = mStopThread;
        int  page  = mRequestPage;
        float zoom = mRequestZoom;
        int   rot  = mRequestRotation;
        OSUnlockMutex(&mRequestMutex);

        if (stop) break;

        if (!mCtx || !mDoc || page < 0 || page >= mPageCount) continue;

        fz_page*   fzpage  = nullptr;
        fz_pixmap* pixmap  = nullptr;

        fz_try(mCtx) {
            fzpage = fz_load_page(mCtx, mDoc, page);

            fz_rect bounds = fz_bound_page(mCtx, fzpage);
            float pageW = bounds.x1 - bounds.x0;
            float pageH = bounds.y1 - bounds.y0;

            float scaleX   = (float)VIEWPORT_W / pageW;
            float scaleY   = (float)VIEWPORT_H / pageH;
            float fitScale = std::min(scaleX, scaleY) * zoom;

            fz_matrix matrix = fz_scale(fitScale, fitScale);
            matrix = fz_pre_rotate(matrix, (float)rot);

            fz_colorspace* cs = fz_device_rgb(mCtx);
            pixmap = fz_new_pixmap_from_page(mCtx, fzpage, matrix, cs, 0);

            int w      = fz_pixmap_width(mCtx, pixmap);
            int h      = fz_pixmap_height(mCtx, pixmap);
            int stride = fz_pixmap_stride(mCtx, pixmap);
            unsigned char* samples = fz_pixmap_samples(mCtx, pixmap);

            WHBLogPrintf("PdfViewerScreen: rendered page %d at %.2fx -> %dx%d",
                         page + 1, zoom, w, h);

            std::vector<uint8_t> pixels(samples, samples + (size_t)stride * h);

            // Hand the result to the main thread
            OSLockMutex(&mResultMutex);
            mPendingPixels = std::move(pixels);
            mPendingW      = w;
            mPendingH      = h;
            mPendingStride = stride;
            mResultReady   = true;
            OSUnlockMutex(&mResultMutex);
        }
        fz_always(mCtx) {
            if (pixmap) fz_drop_pixmap(mCtx, pixmap);
            if (fzpage) fz_drop_page(mCtx, fzpage);
        }
        fz_catch(mCtx) {
            WHBLogPrintf("PdfViewerScreen: render error: %s", fz_caught_message(mCtx));
        }
    }
}

void PdfViewerScreen::UploadPendingTexture()
{
    OSLockMutex(&mResultMutex);
    if (!mResultReady) {
        OSUnlockMutex(&mResultMutex);
        return;
    }

    std::vector<uint8_t> pixels = std::move(mPendingPixels);
    int w      = mPendingW;
    int h      = mPendingH;
    int stride = mPendingStride;
    mResultReady = false;
    OSUnlockMutex(&mResultMutex);

    if (mTexture) {
        SDL_DestroyTexture(mTexture);
        mTexture = nullptr;
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
        pixels.data(), w, h,
        24, stride,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0);

    if (surface) {
        mTexture   = SDL_CreateTextureFromSurface(Gfx::GetRenderer(), surface);
        mTexWidth  = w;
        mTexHeight = h;
        SDL_FreeSurface(surface);
    } else {
        WHBLogPrintf("PdfViewerScreen: SDL_CreateRGBSurfaceFrom failed: %s", SDL_GetError());
    }

    mRendering = false;
}

void PdfViewerScreen::Draw()
{
    // Upload any finished render before drawing
    UploadPendingTexture();

    Gfx::Clear(Gfx::COLOR_BLACK);

    // Title bar
    char title[256];
    size_t slash = mPdfPath.find_last_of('/');
    std::string filename = (slash != std::string::npos)
                           ? mPdfPath.substr(slash + 1) : mPdfPath;
    snprintf(title, sizeof(title), "%s", filename.c_str());

    DrawTopBar(title);
    DrawBottomBar("B: Back  L/R/Up/Down: Page", "Stick: Pan  ZL/ZR: Rotate", "A/Y: Zoom  X: Reset");

    if (mLoadError) {
        Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2 - 40, 48,
                   Gfx::COLOR_WHITE, "Failed to load PDF", Gfx::ALIGN_CENTER);
        if (!mErrorMessage.empty())
            Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2 + 20, 32,
                       Gfx::COLOR_ALT_TEXT, mErrorMessage.c_str(), Gfx::ALIGN_CENTER);
        return;
    }

    if (!mTexture) {
        Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2, 40,
                   Gfx::COLOR_ALT_TEXT, "Rendering...", Gfx::ALIGN_CENTER);
        return;
    }

    SDL_Rect dst;
    CalculateDisplayRect(dst);
    SDL_RenderCopy(Gfx::GetRenderer(), mTexture, nullptr, &dst);

    // Top-right bar page indicator/zoom
    char info[64];
    if (mPageCount > 0) {
        if (mRendering)
            snprintf(info, sizeof(info), "%d / %d  |  %.0f%% ...",
                     mCurrentPage + 1, mPageCount, mZoom * 100.0f);
        else
            snprintf(info, sizeof(info), "%d / %d  |  %.0f%%",
                     mCurrentPage + 1, mPageCount, mZoom * 100.0f);
    } else {
        snprintf(info, sizeof(info), "%.0f%%", mZoom * 100.0f);
    }
    Gfx::Print(Gfx::SCREEN_WIDTH - 20, VIEWPORT_Y + 10, 32,
               Gfx::COLOR_WHITE, info, Gfx::ALIGN_RIGHT);
}

bool PdfViewerScreen::Update(Input& input)
{
    if (input.data.buttons_d & Input::BUTTON_B) {
        mShouldClose = true;
        return false;
    }

    bool pageChanged = false;
    bool zoomChanged = false;
    const float deadzone = 0.2f;

    if ((input.data.buttons_d & Input::BUTTON_R) ||
        (input.data.buttons_d & Input::BUTTON_DOWN)) {
        if (mCurrentPage < mPageCount - 1) {
            mCurrentPage++;
            mOffsetX = 0;
            mOffsetY = 0;
            pageChanged = true;
        }
    }
    if ((input.data.buttons_d & Input::BUTTON_L) ||
        (input.data.buttons_d & Input::BUTTON_UP)) {
        if (mCurrentPage > 0) {
            mCurrentPage--;
            mOffsetX = 0;
            mOffsetY = 0;
            pageChanged = true;
        }
    }

    if (input.data.buttons_d & Input::BUTTON_A) {
        mZoom = std::min(mZoom * 1.25f, 8.0f);
        zoomChanged = true;
    }
    if (input.data.buttons_d & Input::BUTTON_Y) {
        mZoom = std::max(mZoom / 1.25f, 0.25f);
        zoomChanged = true;
    }

    // --- Right stick zoom
    if (std::abs(input.data.rightStickY) > deadzone) {
        mZoom += input.data.rightStickY * 0.02f;
        mZoom  = std::max(0.25f, std::min(8.0f, mZoom));
        zoomChanged = true;
    }

    // Reset
    if (input.data.buttons_d & Input::BUTTON_X) {
        mZoom = 1.0f;
        zoomChanged = true;
    }

    // Pan (only when zoomed in)
    if (mZoom > 1.0f) {
        if (std::abs(input.data.leftStickX) > deadzone ||
            std::abs(input.data.leftStickY) > deadzone) {
            mOffsetX += static_cast<int>(input.data.leftStickX * 12.0f);
            mOffsetY -= static_cast<int>(input.data.leftStickY * 12.0f);
        }
        const int panSpeed = 20;
        if (input.data.buttons_h & Input::BUTTON_LEFT)  mOffsetX += panSpeed;
        if (input.data.buttons_h & Input::BUTTON_RIGHT) mOffsetX -= panSpeed;
    } else {
        // Not zoomed in — keep centred
        mOffsetX = 0;
        mOffsetY = 0;
    }

    // Rotation (ZL = counter-clockwise, ZR = clockwise)
    bool rotChanged = false;
    if (input.data.buttons_d & Input::BUTTON_ZR) {
        mRotation = (mRotation + 90) % 360;
        mOffsetX = 0;
        mOffsetY = 0;
        rotChanged = true;
    }
    if (input.data.buttons_d & Input::BUTTON_ZL) {
        mRotation = (mRotation + 270) % 360;
        mOffsetX = 0;
        mOffsetY = 0;
        rotChanged = true;
    }

    if (pageChanged) {
        mZoomSettleFrames = 0;
        mLastRequestedPage = mCurrentPage;
        mLastRequestedZoom = mZoom;
        mLastRequestedRotation = mRotation;
        RequestRender(mCurrentPage, mZoom, mRotation);
    } else if (rotChanged) {
        mZoomSettleFrames = 0;
        mLastRequestedPage = mCurrentPage;
        mLastRequestedZoom = mZoom;
        mLastRequestedRotation = mRotation;
        RequestRender(mCurrentPage, mZoom, mRotation);
    } else if (zoomChanged) {
        bool discreteZoom = (input.data.buttons_d & Input::BUTTON_A) ||
                            (input.data.buttons_d & Input::BUTTON_Y) ||
                            (input.data.buttons_d & Input::BUTTON_X);

        if (discreteZoom) {
            mZoomSettleFrames = 0;
            mLastRequestedZoom = mZoom;
            mLastRequestedPage = mCurrentPage;
            mLastRequestedRotation = mRotation;
            RequestRender(mCurrentPage, mZoom, mRotation);
        } else {
            mZoomSettleFrames = 0;
        }
    } else if (mZoomSettleFrames >= 0 &&
               mZoomSettleFrames < ZOOM_SETTLE_FRAMES) {
        mZoomSettleFrames++;
        if (mZoomSettleFrames >= ZOOM_SETTLE_FRAMES) {
            if (mZoom != mLastRequestedZoom ||
                mCurrentPage != mLastRequestedPage ||
                mRotation != mLastRequestedRotation) {
                mLastRequestedZoom     = mZoom;
                mLastRequestedPage     = mCurrentPage;
                mLastRequestedRotation = mRotation;
                RequestRender(mCurrentPage, mZoom, mRotation);
            }
        }
    }

    return true;
}

void PdfViewerScreen::CalculateDisplayRect(SDL_Rect& rect)
{
    rect.w = mTexWidth;
    rect.h = mTexHeight;

    // Centred position (no pan)
    int centreX = (VIEWPORT_W - mTexWidth)  / 2;
    int centreY = VIEWPORT_Y + (VIEWPORT_H - mTexHeight) / 2;

    // Clamp pan so the texture never leaves the viewport.
    // When the texture is smaller than the viewport it stays centred (offset = 0).
    int maxOffsetX = std::max(0, (mTexWidth  - VIEWPORT_W) / 2);
    int maxOffsetY = std::max(0, (mTexHeight - VIEWPORT_H) / 2);

    mOffsetX = std::max(-maxOffsetX, std::min(maxOffsetX, mOffsetX));
    mOffsetY = std::max(-maxOffsetY, std::min(maxOffsetY, mOffsetY));

    rect.x = centreX + mOffsetX;
    rect.y = centreY + mOffsetY;
}
