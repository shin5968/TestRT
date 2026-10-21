#include "overlay.h"

#include <algorithm>
#include <cstring>

Overlay::~Overlay() { release(); }

void Overlay::release() {
    if (memdc_ && oldBitmap_) {
        SelectObject(memdc_, oldBitmap_);
        oldBitmap_ = nullptr;
    }
    if (memdc_ && oldFont_) {
        SelectObject(memdc_, oldFont_);
        oldFont_ = nullptr;
    }
    if (bitmap_) {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
    }
    if (font_) {
        DeleteObject(font_);
        font_ = nullptr;
    }
    if (memdc_) {
        DeleteDC(memdc_);
        memdc_ = nullptr;
    }
    if (screen_) {
        ReleaseDC(nullptr, screen_);
        screen_ = nullptr;
    }
    bits_ = nullptr;
    width_ = height_ = 0;
    fontHeight_ = 0;
}

bool Overlay::resize(int w, int h) {
    if (w <= 0 || h <= 0) return false;
    if (w == width_ && h == height_ && memdc_) return true;

    release();
    width_ = w;
    height_ = h;

    screen_ = GetDC(nullptr);
    if (!screen_) return false;
    memdc_ = CreateCompatibleDC(screen_);
    if (!memdc_) return false;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    bitmap_ = CreateDIBSection(memdc_, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap_ || !bits) return false;
    bits_ = bits;
    oldBitmap_ = SelectObject(memdc_, bitmap_);

    fontHeight_ = std::max(14, (h * 22) / 720);
    font_ = CreateFontA(-fontHeight_, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    if (!font_) {
        font_ = CreateFontA(-fontHeight_, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Courier New");
    }
    if (font_) oldFont_ = SelectObject(memdc_, font_);

    logf("overlay: %dx%d font=%dpx", w, h, fontHeight_);
    return true;
}

void Overlay::render(const std::vector<std::string>& lines) {
    render(lines, nullptr, 0, 0);
}

void Overlay::render(const std::vector<std::string>& lines, const uint8_t* bgBGRA,
                     int bgWidth, int bgHeight) {
    if (!bits_ || !memdc_) return;

    const bool hasBg = bgBGRA && bgWidth > 0 && bgHeight > 0;
    uint8_t* base = (uint8_t*)bits_;
    if (hasBg) {
        if (bgWidth == width_ && bgHeight == height_) {
            memcpy(bits_, bgBGRA, (size_t)width_ * height_ * 4);
        } else {
            for (int y = 0; y < height_; ++y) {
                int sy = (int)((int64_t)y * bgHeight / height_);
                sy = std::min(bgHeight - 1, std::max(0, sy));
                const uint8_t* src = bgBGRA + (size_t)sy * bgWidth * 4;
                uint8_t* dst = base + (size_t)y * width_ * 4;
                for (int x = 0; x < width_; ++x) {
                    int sx = (int)((int64_t)x * bgWidth / width_);
                    sx = std::min(bgWidth - 1, std::max(0, sx));
                    memcpy(dst + (size_t)x * 4, src + (size_t)sx * 4, 4);
                }
            }
        }
    } else {
        memset(bits_, 0, (size_t)width_ * height_ * 4);
    }

    SetBkMode(memdc_, TRANSPARENT);
    SetTextAlign(memdc_, TA_LEFT | TA_TOP);

    int y = 10;
    int lineHeight = fontHeight_ + 4;
    for (const auto& line : lines) {
        if (line.empty()) {
            y += lineHeight / 2;
            continue;
        }
        if (y + lineHeight > height_) break;
        std::wstring wline = widen(line);

        SetTextColor(memdc_, RGB(0, 0, 0));
        TextOutW(memdc_, 13, y + 1, wline.c_str(), (int)wline.size());
        SetTextColor(memdc_, RGB(255, 255, 255));
        TextOutW(memdc_, 12, y, wline.c_str(), (int)wline.size());
        y += lineHeight;
    }

    // Text is drawn as white (with a dark shadow). With a background image the
    // overlay is fully opaque; otherwise luminance becomes alpha.
    uint8_t* p = (uint8_t*)bits_;
    size_t count = (size_t)width_ * height_;
    if (!hasBg) {
        for (size_t i = 0; i < count; ++i) {
            uint8_t* px = p + i * 4;  // B,G,R,A
            px[3] = std::max(px[0], std::max(px[1], px[2]));
        }
    }
    ++version_;
}
