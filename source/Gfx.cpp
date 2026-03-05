#include "Gfx.hpp"
#include <SDL_ttf.h>
#include <coreinit/memory.h>
#include <whb/log.h>
#include <cmath>

namespace Gfx {
    static SDL_Window *sWindow = nullptr;
    static SDL_Renderer *sRenderer = nullptr;
    static TTF_Font *sFont = nullptr;
    static void *sFontData = nullptr;
    static uint32_t sFontSize = 0;

    bool Init() {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
            return false;
        }

        if (TTF_Init() < 0) {
            SDL_Quit();
            return false;
        }

        sWindow = SDL_CreateWindow("WiiUXplorer", 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0);
        if (!sWindow) {
            TTF_Quit();
            SDL_Quit();
            return false;
        }

        sRenderer = SDL_CreateRenderer(sWindow, -1, SDL_RENDERER_ACCELERATED);
        if (!sRenderer) {
            SDL_DestroyWindow(sWindow);
            TTF_Quit();
            SDL_Quit();
            return false;
        }

        SDL_SetRenderDrawBlendMode(sRenderer, SDL_BLENDMODE_BLEND);

        if (OSGetSharedData(OS_SHAREDDATATYPE_FONT_STANDARD, 0, &sFontData, &sFontSize)) {
            sFont = TTF_OpenFontRW(SDL_RWFromMem(sFontData, sFontSize), 0, 32);
        }

        return true;
    }

    void Shutdown() {
        if (sFont) {
            TTF_CloseFont(sFont);
            sFont = nullptr;
        }

        if (sRenderer) {
            SDL_DestroyRenderer(sRenderer);
            sRenderer = nullptr;
        }

        if (sWindow) {
            SDL_DestroyWindow(sWindow);
            sWindow = nullptr;
        }

        TTF_Quit();
        
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        SDL_Quit();
    }

    void Clear(SDL_Color color) {
        SDL_SetRenderDrawColor(sRenderer, color.r, color.g, color.b, color.a);
        SDL_RenderClear(sRenderer);
    }

    void Render() {
        SDL_RenderPresent(sRenderer);
    }

    void DrawRectFilled(int x, int y, int w, int h, SDL_Color color) {
        SDL_Rect rect = {x, y, w, h};
        SDL_SetRenderDrawColor(sRenderer, color.r, color.g, color.b, color.a);
        SDL_RenderFillRect(sRenderer, &rect);
    }

    void Print(int x, int y, int size, SDL_Color color, const std::string& text, AlignFlags align) {
        if (!sFont || text.empty()) return;

        SDL_Surface *surface = TTF_RenderText_Blended(sFont, text.c_str(), color);
        if (!surface) return;

        SDL_Texture *texture = SDL_CreateTextureFromSurface(sRenderer, surface);
        if (!texture) {
            SDL_FreeSurface(surface);
            return;
        }

        int w = surface->w;
        int h = surface->h;
        SDL_FreeSurface(surface);

        // Apply alignment
        if (align & ALIGN_HORIZONTAL) {
            x -= w / 2;
        } else if (align & ALIGN_RIGHT) {
            x -= w;
        }

        if (align & ALIGN_VERTICAL) {
            y -= h / 2;
        } else if (align & ALIGN_BOTTOM) {
            y -= h;
        }

        SDL_Rect dstRect = {x, y, w, h};
        SDL_RenderCopy(sRenderer, texture, nullptr, &dstRect);
        SDL_DestroyTexture(texture);
    }

    int GetTextWidth(int size, const std::string& text) {
        if (!sFont || text.empty()) return 0;
        int w = 0;
        TTF_SizeText(sFont, text.c_str(), &w, nullptr);
        return w;
    }

    int GetTextHeight(int size, const std::string& text) {
        if (!sFont || text.empty()) return 0;
        int h = 0;
        TTF_SizeText(sFont, text.c_str(), nullptr, &h);
        return h;
    }

    SDL_Renderer* GetRenderer() {
        return sRenderer;
    }
}

