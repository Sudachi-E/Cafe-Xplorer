#include "Gfx.hpp"
#include <SDL_ttf.h>
#include <coreinit/memory.h>
#include <whb/log.h>
#include <cmath>
#include <map>

namespace Gfx {
    static SDL_Window *sWindow = nullptr;
    static SDL_Renderer *sRenderer = nullptr;
    static TTF_Font *sFont = nullptr;
    static std::map<int, TTF_Font*> sIconFontBySize;
    static void *sFontData = nullptr;
    static uint32_t sFontSize = 0;

    struct TextCacheValue {
        SDL_Texture* texture = nullptr;
        int width = 0;
        int height = 0;
    };

    struct TextCacheKey {
        int size = 0;
        std::string text;
        SDL_Color color;

        bool operator<(const TextCacheKey& other) const {
            if (size != other.size) return size < other.size;
            if (text != other.text) return text < other.text;
            if (color.r != other.color.r) return color.r < other.color.r;
            if (color.g != other.color.g) return color.g < other.color.g;
            if (color.b != other.color.b) return color.b < other.color.b;
            return color.a < other.color.a;
        }
    };

    static std::map<TextCacheKey, TextCacheValue> sTextCache;

    static const TextCacheValue* GetCachedText(int size, const std::string& text, SDL_Color color) {
        TextCacheKey key{size, text, color};
        auto it = sTextCache.find(key);
        if (it != sTextCache.end()) {
            return &it->second;
        }

        if (!sFont || text.empty()) {
            return nullptr;
        }

        TTF_Font* font = sFont;
        if (size != 32) {
            font = TTF_OpenFontRW(SDL_RWFromMem(sFontData, sFontSize), 0, size);
            if (!font) {
                return nullptr;
            }
        }

        SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
        if (size != 32) {
            TTF_CloseFont(font);
        }
        if (!surface) {
            return nullptr;
        }

        SDL_Texture *texture = SDL_CreateTextureFromSurface(sRenderer, surface);
        if (!texture) {
            SDL_FreeSurface(surface);
            return nullptr;
        }

        TextCacheValue value;
        value.texture = texture;
        value.width = surface->w;
        value.height = surface->h;
        SDL_FreeSurface(surface);

        auto [insertIt, _] = sTextCache.emplace(std::move(key), value);
        return &insertIt->second;
    }

    static void ClearTextCache() {
        for (auto& [_, value] : sTextCache) {
            SDL_DestroyTexture(value.texture);
        }
        sTextCache.clear();
    }


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

        sRenderer = SDL_CreateRenderer(sWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
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

        for (auto& [key, fnt] : sIconFontBySize) TTF_CloseFont(fnt);
        sIconFontBySize.clear();
        ClearTextCache();

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

    void DrawRectGradient(int x, int y, int w, int h, SDL_Color topColor, SDL_Color bottomColor) {
        for (int i = 0; i < h; i++) {
            float r = (float)i / h;
            SDL_Color c = {
                (uint8_t)(topColor.r + (bottomColor.r - topColor.r) * r),
                (uint8_t)(topColor.g + (bottomColor.g - topColor.g) * r),
                (uint8_t)(topColor.b + (bottomColor.b - topColor.b) * r),
                (uint8_t)(topColor.a + (bottomColor.a - topColor.a) * r)
            };
            SDL_SetRenderDrawColor(sRenderer, c.r, c.g, c.b, c.a);
            SDL_RenderDrawLine(sRenderer, x, y + i, x + w, y + i);
        }
    }

    void DrawRectRounded(int x, int y, int w, int h, int radius, SDL_Color color) {
        SDL_SetRenderDrawColor(sRenderer, color.r, color.g, color.b, color.a);
        SDL_Rect rects[3] = {
            {x + radius, y,              w - 2*radius, h             },
            {x,          y + radius,     radius,       h - 2*radius  },
            {x + w - radius, y + radius, radius,       h - 2*radius  }
        };
        SDL_RenderFillRects(sRenderer, rects, 3);
        for (int dy = 0; dy < radius; dy++) {
            int dx = (int)std::sqrt((float)(radius*radius - dy*dy));
            SDL_RenderDrawLine(sRenderer, x + radius - dx,     y + radius - dy,     x + radius,         y + radius - dy);
            SDL_RenderDrawLine(sRenderer, x + w - radius,      y + radius - dy,     x + w - radius + dx, y + radius - dy);
            SDL_RenderDrawLine(sRenderer, x + radius - dx,     y + h - radius + dy, x + radius,          y + h - radius + dy);
            SDL_RenderDrawLine(sRenderer, x + w - radius,      y + h - radius + dy, x + w - radius + dx, y + h - radius + dy);
        }
    }

    void Print(int x, int y, int size, SDL_Color color, const std::string& text, AlignFlags align) {
        if (!sFont || text.empty()) return;

        const TextCacheValue* cached = GetCachedText(size, text, color);
        if (!cached || !cached->texture) return;

        int w = cached->width;
        int h = cached->height;

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
        SDL_RenderCopy(sRenderer, cached->texture, nullptr, &dstRect);
    }

    static TTF_Font* GetIconFontForSize(int size) {
        auto it = sIconFontBySize.find(size);
        if (it != sIconFontBySize.end()) return it->second;
        if (!sFontData) return nullptr;
        TTF_Font* f = TTF_OpenFontRW(SDL_RWFromMem(sFontData, sFontSize), 0, size);
        if (f) sIconFontBySize[size] = f;
        return f;
    }

    void PrintIcon(int x, int y, int size, SDL_Color color, const std::string& text, AlignFlags align) {
        TTF_Font* font = GetIconFontForSize(size);
        if (!font || text.empty()) return;

        SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
        if (!surface) return;

        SDL_Texture* texture = SDL_CreateTextureFromSurface(sRenderer, surface);
        int w = surface->w, h = surface->h;
        SDL_FreeSurface(surface);
        if (!texture) return;

        if (align & ALIGN_HORIZONTAL) x -= w / 2;
        else if (align & ALIGN_RIGHT) x -= w;
        if (align & ALIGN_VERTICAL)   y -= h / 2;
        else if (align & ALIGN_BOTTOM) y -= h;

        SDL_Rect dst = {x, y, w, h};
        SDL_RenderCopy(sRenderer, texture, nullptr, &dst);
        SDL_DestroyTexture(texture);
    }

    int GetIconTextWidth(int size, const std::string& text) {
        TTF_Font* font = GetIconFontForSize(size);
        if (!font || text.empty()) return 0;
        int w = 0;
        TTF_SizeUTF8(font, text.c_str(), &w, nullptr);
        return w;
    }

    int GetTextWidth(int size, const std::string& text) {
        if (!sFont || text.empty()) return 0;
        int w = 0;
        TTF_SizeUTF8(sFont, text.c_str(), &w, nullptr);
        return w;
    }

    int GetTextHeight(int size, const std::string& text) {
        if (!sFont || text.empty()) return 0;
        int h = 0;
        TTF_SizeUTF8(sFont, text.c_str(), nullptr, &h);
        return h;
    }

    SDL_Renderer* GetRenderer() {
        return sRenderer;
    }

    SDL_Window* GetWindow() {
        return sWindow;
    }
}

