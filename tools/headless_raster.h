// headless_raster.h - tiny software rasterizer for Dear ImGui draw data (dev tool, not part of lumen::core).
// Lets you render a Lumen frame to a PNG with no window, GPU or display, so visual work can be checked
// by looking at pictures. Header-only; depends on imgui.h only.
//
//   HeadlessRaster raster(900, 520);
//   raster.Clear(0.96f, 0.97f, 0.99f);
//   raster.Draw(ImGui::GetDrawData(), font_pixels_rgba, font_w, font_h);   // after ImGui::Render()
//   raster.SavePng("shot.png");
//
// Notes: bilinear texture sampling (ImGui's anti-aliased line textures need it), straight-alpha
// "over" blending, per-command clip rects. Close to a real GL backend but not pixel-identical.
#pragma once

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

class HeadlessRaster {
public:
    HeadlessRaster(int width, int height) : w_(width), h_(height), rgb_(static_cast<size_t>(width) * height * 3u, 0) {}

    void Clear(float r, float g, float b) {
        for (size_t i = 0; i < rgb_.size(); i += 3) {
            rgb_[i] = ToByte(r); rgb_[i + 1] = ToByte(g); rgb_[i + 2] = ToByte(b);
        }
    }

    // font_rgba: the atlas from io.Fonts->GetTexDataAsRGBA32(). One texture is assumed (the font atlas).
    void Draw(const ImDrawData* data, const unsigned char* font_rgba, int tex_w, int tex_h) {
        if (data == nullptr) return;
        for (int n = 0; n < data->CmdListsCount; ++n) {
            const ImDrawList* list = data->CmdLists[n];
            for (const ImDrawCmd& cmd : list->CmdBuffer) {
                if (cmd.UserCallback != nullptr) continue;
                const int cx0 = std::max(0, static_cast<int>(cmd.ClipRect.x));
                const int cy0 = std::max(0, static_cast<int>(cmd.ClipRect.y));
                const int cx1 = std::min(w_, static_cast<int>(std::ceil(cmd.ClipRect.z)));
                const int cy1 = std::min(h_, static_cast<int>(std::ceil(cmd.ClipRect.w)));
                for (unsigned i = 0; i + 2 < cmd.ElemCount; i += 3) {
                    const ImDrawVert* v[3];
                    for (int k = 0; k < 3; ++k) v[k] = &list->VtxBuffer[list->IdxBuffer[cmd.IdxOffset + i + k] + cmd.VtxOffset];
                    Triangle(v, cx0, cy0, cx1, cy1, font_rgba, tex_w, tex_h);
                }
            }
        }
    }

    bool SavePng(const char* path) const {
        std::FILE* f = std::fopen(path, "wb");
        if (f == nullptr) return false;
        // Raw scanlines with filter byte 0, wrapped in stored (uncompressed) deflate blocks.
        std::vector<unsigned char> raw;
        raw.reserve((static_cast<size_t>(w_) * 3u + 1u) * h_);
        for (int y = 0; y < h_; ++y) {
            raw.push_back(0);
            raw.insert(raw.end(), rgb_.begin() + static_cast<size_t>(y) * w_ * 3u, rgb_.begin() + static_cast<size_t>(y + 1) * w_ * 3u);
        }
        std::vector<unsigned char> z{0x78, 0x01};
        for (size_t pos = 0; pos < raw.size();) {
            const size_t n = std::min<size_t>(65535u, raw.size() - pos);
            z.push_back(pos + n >= raw.size() ? 1 : 0);
            z.push_back(n & 255u); z.push_back((n >> 8) & 255u);
            z.push_back(~n & 255u); z.push_back((~n >> 8) & 255u);
            z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
            pos += n;
        }
        uint32_t a = 1, b = 0;
        for (unsigned char c : raw) { a = (a + c) % 65521u; b = (b + a) % 65521u; }
        const uint32_t adler = (b << 16) | a;
        for (int s = 24; s >= 0; s -= 8) z.push_back((adler >> s) & 255u);

        const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
        std::fwrite(sig, 1, 8, f);
        unsigned char ihdr[13];
        Put32(ihdr, static_cast<uint32_t>(w_)); Put32(ihdr + 4, static_cast<uint32_t>(h_));
        ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
        Chunk(f, "IHDR", ihdr, 13);
        Chunk(f, "IDAT", z.data(), z.size());
        Chunk(f, "IEND", nullptr, 0);
        std::fclose(f);
        return true;
    }

    int Width() const { return w_; }
    int Height() const { return h_; }
    const std::vector<unsigned char>& Rgb() const { return rgb_; }

private:
    static unsigned char ToByte(float v) { return static_cast<unsigned char>(std::min(1.0f, std::max(0.0f, v)) * 255.0f + 0.5f); }
    static void Put32(unsigned char* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

    static uint32_t Crc(const unsigned char* data, size_t n, uint32_t crc = 0xFFFFFFFFu) {
        static uint32_t table[256];
        static bool ready = false;
        if (!ready) {
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                table[i] = c;
            }
            ready = true;
        }
        for (size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 255u] ^ (crc >> 8);
        return crc;
    }

    static void Chunk(std::FILE* f, const char* type, const unsigned char* data, size_t n) {
        unsigned char len[4]; Put32(len, static_cast<uint32_t>(n));
        std::fwrite(len, 1, 4, f);
        std::fwrite(type, 1, 4, f);
        if (n != 0) std::fwrite(data, 1, n, f);
        uint32_t crc = Crc(reinterpret_cast<const unsigned char*>(type), 4);
        crc = Crc(data, n, crc) ^ 0xFFFFFFFFu;
        unsigned char c[4]; Put32(c, crc);
        std::fwrite(c, 1, 4, f);
    }

    void Blend(int x, int y, float r, float g, float b, float a) {
        unsigned char* p = &rgb_[(static_cast<size_t>(y) * w_ + x) * 3u];
        p[0] = static_cast<unsigned char>(p[0] + (r * 255.0f - p[0]) * a + 0.5f);
        p[1] = static_cast<unsigned char>(p[1] + (g * 255.0f - p[1]) * a + 0.5f);
        p[2] = static_cast<unsigned char>(p[2] + (b * 255.0f - p[2]) * a + 0.5f);
    }

    void Triangle(const ImDrawVert* v[3], int cx0, int cy0, int cx1, int cy1,
                  const unsigned char* tex, int tw, int th) {
        const float minx = std::min({v[0]->pos.x, v[1]->pos.x, v[2]->pos.x}), maxx = std::max({v[0]->pos.x, v[1]->pos.x, v[2]->pos.x});
        const float miny = std::min({v[0]->pos.y, v[1]->pos.y, v[2]->pos.y}), maxy = std::max({v[0]->pos.y, v[1]->pos.y, v[2]->pos.y});
        const int x0 = std::max(cx0, static_cast<int>(std::floor(minx))), x1 = std::min(cx1, static_cast<int>(std::ceil(maxx)));
        const int y0 = std::max(cy0, static_cast<int>(std::floor(miny))), y1 = std::min(cy1, static_cast<int>(std::ceil(maxy)));
        const float area = (v[1]->pos.x - v[0]->pos.x) * (v[2]->pos.y - v[0]->pos.y) - (v[2]->pos.x - v[0]->pos.x) * (v[1]->pos.y - v[0]->pos.y);
        if (std::fabs(area) < 1e-6f) return;
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const float px = x + 0.5f, py = y + 0.5f;
                const float w0 = ((v[1]->pos.x - px) * (v[2]->pos.y - py) - (v[2]->pos.x - px) * (v[1]->pos.y - py)) / area;
                const float w1 = ((v[2]->pos.x - px) * (v[0]->pos.y - py) - (v[0]->pos.x - px) * (v[2]->pos.y - py)) / area;
                const float w2 = 1.0f - w0 - w1;
                if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;
                const float u = w0 * v[0]->uv.x + w1 * v[1]->uv.x + w2 * v[2]->uv.x;
                const float vv = w0 * v[0]->uv.y + w1 * v[1]->uv.y + w2 * v[2]->uv.y;
                const float fu = u * tw - 0.5f, fv = vv * th - 0.5f;
                const int ix = static_cast<int>(std::floor(fu)), iy = static_cast<int>(std::floor(fv));
                const float ax = fu - ix, ay = fv - iy;
                auto T = [&](int xx, int yy, int c) {
                    xx = std::min(tw - 1, std::max(0, xx)); yy = std::min(th - 1, std::max(0, yy));
                    return static_cast<float>(tex[(static_cast<size_t>(yy) * tw + xx) * 4u + c]);
                };
                float t[4];
                for (int c = 0; c < 4; ++c)
                    t[c] = (T(ix, iy, c) * (1 - ax) + T(ix + 1, iy, c) * ax) * (1 - ay) + (T(ix, iy + 1, c) * (1 - ax) + T(ix + 1, iy + 1, c) * ax) * ay;
                auto ch = [&](int sh) { return (((v[0]->col >> sh) & 255u) * w0 + ((v[1]->col >> sh) & 255u) * w1 + ((v[2]->col >> sh) & 255u) * w2) / 255.0f; };
                const float a = ch(24) * t[3] / 255.0f;
                if (a > 0.0f) Blend(x, y, ch(0) * t[0] / 255.0f, ch(8) * t[1] / 255.0f, ch(16) * t[2] / 255.0f, a);
            }
        }
    }

    int w_, h_;
    std::vector<unsigned char> rgb_;
};
