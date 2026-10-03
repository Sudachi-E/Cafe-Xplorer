#include "Gfx.hpp"
#include <SDL_ttf.h>
#include <coreinit/memory.h>
#include <whb/log.h>
#include <cmath>
#include <map>
#include <vector>

namespace Gfx {
    static SDL_Window *sWindow = nullptr;
    static SDL_Renderer *sRenderer = nullptr;
    static TTF_Font *sFont = nullptr;
    static std::map<int, TTF_Font*> sIconFontBySize;
    static std::map<std::pair<int, int>, SDL_Texture*> sShapeTextures;
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
    static std::map<int, TTF_Font*> sFontBySize;

    static TTF_Font* GetFontForSize(int size) {
        auto it = sFontBySize.find(size);
        if (it != sFontBySize.end()) return it->second;
        if (!sFontData) return nullptr;

        TTF_Font* font = TTF_OpenFontRW(SDL_RWFromMem(sFontData, sFontSize), 0, size);
        if (!font) return nullptr;

        sFontBySize[size] = font;
        return font;
    }

    static const TextCacheValue* GetCachedText(int size, const std::string& text, SDL_Color color) {
        TextCacheKey key{size, text, color};
        auto it = sTextCache.find(key);
        if (it != sTextCache.end()) {
            return &it->second;
        }

        if (text.empty()) {
            return nullptr;
        }

        TTF_Font* font = GetFontForSize(size);
        if (!font) {
            return nullptr;
        }

        SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
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
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

        if (OSGetSharedData(OS_SHAREDDATATYPE_FONT_STANDARD, 0, &sFontData, &sFontSize)) {
            sFont = GetFontForSize(32);
        }

        return true;
    }

    void Shutdown() {
        for (auto& [key, fnt] : sFontBySize) TTF_CloseFont(fnt);
        sFontBySize.clear();
        sFont = nullptr;

        for (auto& [key, fnt] : sIconFontBySize) TTF_CloseFont(fnt);
        sIconFontBySize.clear();
        for (auto& [key, tex] : sShapeTextures) SDL_DestroyTexture(tex);
        sShapeTextures.clear();
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

    static constexpr int ICON_SS = 4;

    enum class ShapeKind { Folder, File, Disc, Music, Play };

    static bool InRect(float u, float v, float x0, float y0, float x1, float y1) {
        return u >= x0 && u <= x1 && v >= y0 && v <= y1;
    }

    static bool InEllipse(float u, float v, float cx, float cy, float rx, float ry) {
        const float dx = (u - cx) / rx;
        const float dy = (v - cy) / ry;
        return dx * dx + dy * dy <= 1.0f;
    }

    static bool FolderShape(float u, float v) {
        if (InRect(u, v, 0.10f, 0.12f, 0.44f, 0.30f)) return true; // tab
        if (InRect(u, v, 0.10f, 0.26f, 0.90f, 0.36f)) return true; // back
        if (InRect(u, v, 0.10f, 0.40f, 0.90f, 0.88f)) return true; // front
        return false;
    }

    static bool FileShape(float u, float v) {
        if (!InRect(u, v, 0.24f, 0.10f, 0.76f, 0.90f)) return false;
        if ((v - 0.10f) / 0.24f < (u - 0.56f) / 0.20f) return false;
        if (InRect(u, v, 0.34f, 0.44f, 0.66f, 0.50f)) return false;
        if (InRect(u, v, 0.34f, 0.57f, 0.66f, 0.63f)) return false;
        if (InRect(u, v, 0.34f, 0.70f, 0.56f, 0.76f)) return false;
        return true;
    }

    static bool DiscShape(float u, float v) {
        const float dx = u - 0.5f;
        const float dy = v - 0.5f;
        const float radius = std::sqrt(dx * dx + dy * dy);
        if (radius > 0.47f) return false;
        if (radius < 0.13f) return false;
        if (radius > 0.38f && radius < 0.42f) return false;
        return true;
    }

    static bool MusicShape(float u, float v) {
        if (InEllipse(u, v, 0.30f, 0.72f, 0.22f, 0.16f)) return true;
        if (InRect(u, v, 0.44f, 0.12f, 0.53f, 0.78f)) return true;
        if (u >= 0.53f && u <= 0.88f) {
            const float top = 0.12f + (u - 0.53f) * 0.571f;
            const float bottom = 0.46f - (u - 0.53f) * 0.400f;
            if (v >= top && v <= bottom) return true;
        }
        return false;
    }

    static bool PlayShape(float u, float v) {
        constexpr float LEFT = 0.22f;
        constexpr float RIGHT = 0.86f;
        constexpr float TIP_Y = 0.50f;
        if (u < LEFT || u > RIGHT) return false;

        const float reach = (u - LEFT) / (RIGHT - LEFT);
        const float halfHeight = 0.36f * (1.0f - reach);
        return v >= TIP_Y - halfHeight && v <= TIP_Y + halfHeight;
    }

    static bool ShapeContains(ShapeKind kind, float u, float v) {
        switch (kind) {
            case ShapeKind::Folder: return FolderShape(u, v);
            case ShapeKind::File:   return FileShape(u, v);
            case ShapeKind::Disc:   return DiscShape(u, v);
            case ShapeKind::Music:  return MusicShape(u, v);
            case ShapeKind::Play:   return PlayShape(u, v);
        }
        return false;
    }

    static SDL_Texture* BuildShapeTexture(int size, ShapeKind kind) {
        const int res = size * ICON_SS;
        const int samples = ICON_SS * ICON_SS;

        std::vector<uint8_t> coverage(res * res, 0);
        for (int py = 0; py < res; py++) {
            for (int px = 0; px < res; px++) {
                float u = ((float)px + 0.5f) / (float)res;
                float v = ((float)py + 0.5f) / (float)res;
                if (ShapeContains(kind, u, v)) {
                    coverage[py * res + px] = 0xff;
                }
            }
        }

        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_ARGB8888);
        if (!surface) return nullptr;

        Uint32* pixels = (Uint32*)surface->pixels;
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                int sum = 0;
                for (int sy = 0; sy < ICON_SS; sy++) {
                    for (int sx = 0; sx < ICON_SS; sx++) {
                        sum += coverage[(y * ICON_SS + sy) * res + (x * ICON_SS + sx)];
                    }
                }
                pixels[y * size + x] = 0x00ffffffu | ((Uint32)(sum / samples) << 24);
            }
        }

        SDL_Texture* texture = SDL_CreateTextureFromSurface(sRenderer, surface);
        SDL_FreeSurface(surface);
        if (!texture) return nullptr;

        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        return texture;
    }

    static void DrawShapeIcon(int x, int y, int size, SDL_Color color, ShapeKind kind, AlignFlags align) {
        if (size <= 0) return;

        auto key = std::make_pair(size, static_cast<int>(kind));
        auto it = sShapeTextures.find(key);
        if (it == sShapeTextures.end()) {
            SDL_Texture* texture = BuildShapeTexture(size, kind);
            if (!texture) return;
            it = sShapeTextures.emplace(key, texture).first;
        }

        SDL_Texture* texture = it->second;
        SDL_SetTextureColorMod(texture, color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(texture, color.a);

        if (align & ALIGN_HORIZONTAL) x -= size / 2;
        else if (align & ALIGN_RIGHT) x -= size;
        if (align & ALIGN_VERTICAL) y -= size / 2;
        else if (align & ALIGN_BOTTOM) y -= size;

        SDL_Rect dstRect = {x, y, size, size};
        SDL_RenderCopy(sRenderer, texture, nullptr, &dstRect);
    }

    void DrawFolderIcon(int x, int y, int size, SDL_Color color, AlignFlags align) {
        DrawShapeIcon(x, y, size, color, ShapeKind::Folder, align);
    }

    void DrawFileIcon(int x, int y, int size, SDL_Color color, AlignFlags align) {
        DrawShapeIcon(x, y, size, color, ShapeKind::File, align);
    }

    void DrawDiscIcon(int x, int y, int size, SDL_Color color, AlignFlags align) {
        DrawShapeIcon(x, y, size, color, ShapeKind::Disc, align);
    }

    void DrawMusicIcon(int x, int y, int size, SDL_Color color, AlignFlags align) {
        DrawShapeIcon(x, y, size, color, ShapeKind::Music, align);
    }

    void DrawPlayIcon(int x, int y, int size, SDL_Color color, AlignFlags align) {
        DrawShapeIcon(x, y, size, color, ShapeKind::Play, align);
    }

    void DrawPanel(int x, int y, int w, int h, int radius, int borderWidth) {
        DrawRectRounded(x, y, w, h, radius, COLOR_HIGHLIGHTED);

        const int innerRadius = radius - borderWidth;
        DrawRectRounded(x + borderWidth, y + borderWidth,
                        w - 2 * borderWidth, h - 2 * borderWidth,
                        innerRadius > 0 ? innerRadius : 0, COLOR_ALT_BACKGROUND);
    }

    void DrawRoundedOutline(int x, int y, int w, int h, int radius, SDL_Color color,
                            int thickness, SDL_Color innerColor) {
        DrawRectRounded(x, y, w, h, radius, color);

        const int innerRadius = radius - thickness;
        DrawRectRounded(x + thickness, y + thickness,
                        w - 2 * thickness, h - 2 * thickness,
                        innerRadius > 0 ? innerRadius : 0, innerColor);
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
        TTF_Font* font = GetFontForSize(size);
        if (!font || text.empty()) return 0;
        int w = 0;
        TTF_SizeUTF8(font, text.c_str(), &w, nullptr);
        return w;
    }

    int GetTextHeight(int size, const std::string& text) {
        TTF_Font* font = GetFontForSize(size);
        if (!font || text.empty()) return 0;
        int h = 0;
        TTF_SizeUTF8(font, text.c_str(), nullptr, &h);
        return h;
    }

    std::string TruncateToWidth(const std::string& text, int size, int maxWidth) {
        if (maxWidth <= 0 || text.empty() || GetTextWidth(size, text) <= maxWidth) {
            return text;
        }

        std::string truncated;
        for (size_t i = 0; i < text.size(); i++) {
            truncated += text[i];
            if (GetTextWidth(size, truncated + "...") > maxWidth) {
                truncated.pop_back();
                return truncated + "...";
            }
        }
        return text;
    }

    SDL_Renderer* GetRenderer() {
        return sRenderer;
    }

    SDL_Window* GetWindow() {
        return sWindow;
    }
}

