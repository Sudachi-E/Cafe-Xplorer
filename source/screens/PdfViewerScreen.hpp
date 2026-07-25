#pragma once

#include "../Screen.hpp"
#include <SDL.h>
#include <string>
#include <vector>
#include <coreinit/thread.h>
#include <coreinit/mutex.h>
#include <coreinit/semaphore.h>

extern "C" {
#include <mupdf/fitz.h>
}

class PdfViewerScreen : public Screen {
public:
    explicit PdfViewerScreen(const std::string& pdfPath);
    ~PdfViewerScreen() override;

    void Draw() override;
    bool Update(Input& input) override;

    bool ShouldClose() const { return mShouldClose; }

private:
    void RequestRender(int page, float zoom, int rotation);

    void RenderWorker();
    static int RenderThreadEntry(int argc, const char** argv);

    void CalculateDisplayRect(SDL_Rect& rect);
    void UploadPendingTexture();

    std::string mPdfPath;

    fz_context*  mCtx;
    fz_document* mDoc;

    int mPageCount;
    int mCurrentPage;

    SDL_Texture* mTexture;
    int          mTexWidth;
    int          mTexHeight;

    OSMutex              mResultMutex;
    std::vector<uint8_t> mPendingPixels; // RGB24
    int                  mPendingW;
    int                  mPendingH;
    int                  mPendingStride;
    bool                 mResultReady;

    OSMutex     mRequestMutex;
    OSSemaphore mRequestSem;
    int         mRequestPage;
    float       mRequestZoom;
    int         mRequestRotation;
    bool        mRenderThreadRunning;
    bool        mStopThread;

    OSThread    mThread;
    uint8_t*    mThreadStack;
    static constexpr uint32_t THREAD_STACK_SIZE = 256 * 1024;

    float mZoom;
    float mOffsetX;
    float mOffsetY;
    int   mRotation;

    float    mLastRequestedZoom;
    int      mLastRequestedPage;
    int      mLastRequestedRotation;
    int      mZoomSettleFrames;
    static constexpr int ZOOM_SETTLE_FRAMES = 6;

    static constexpr float ZOOM_MIN  = 1.0f;
    static constexpr float ZOOM_MAX  = 8.0f;
    static constexpr float ZOOM_STEP = 0.05f;
    static constexpr float PAN_SPEED = 8.0f;

    bool mRendering;
    bool mShouldClose;
    bool mLoadError;
    bool mBarsHidden;
    std::string mErrorMessage;
};
