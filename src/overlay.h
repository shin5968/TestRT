#pragma once

#include "util.h"

#include <string>
#include <vector>

// CPU rasterized text overlay (GDI into a DIB section), uploaded as BGRA8.
class Overlay {
public:
    Overlay() = default;
    ~Overlay();

    bool resize(int w, int h);
    void render(const std::vector<std::string>& lines);
    // Draws a full-screen BGRA background (nearest-neighbor scaled), then text.
    void render(const std::vector<std::string>& lines, const uint8_t* bgBGRA,
                int bgWidth, int bgHeight);

    void setButtons(const std::vector<std::string>& labels);
    int buttonAt(int x, int y) const;  // -1 if none

    const uint8_t* pixels() const { return (const uint8_t*)bits_; }
    int width() const { return width_; }
    int height() const { return height_; }
    uint32_t version() const { return version_; }

private:
    void release();

    HDC screen_ = nullptr;
    HDC memdc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ oldBitmap_ = nullptr;
    HGDIOBJ oldFont_ = nullptr;
    HFONT font_ = nullptr;
    void* bits_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int fontHeight_ = 0;
    uint32_t version_ = 0;
    std::vector<std::string> buttonLabels_;
    std::vector<RECT> buttonRects_;
};
