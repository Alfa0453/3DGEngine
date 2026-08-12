#pragma once

// -----------------------------------------------------------------------------
// From-scratch TrueType (.ttf) glyph rasterizer.
//
// In the same spirit as the engine's hand-written PNG/JPEG decoders, this is a
// dependency-free loader: no stb, no FreeType. It parses the essential tables
// (head, maxp, cmap, loca, glyf, hhea, hmtx), flattens the quadratic-bezier
// glyph outlines to line segments, and rasterizes them into an anti-aliased
// coverage atlas plus per-glyph layout metrics.
//
// It is HEADER-ONLY (everything inline) so no new translation unit / CMake
// reconfigure is needed -- include it from one .cpp (TextRenderer.cpp).
//
// Scope: enough to render UI/HUD text in Latin fonts. It covers cmap formats
// 0/4/6/12, simple and (basic) composite glyphs, and ASCII 32..126 by default.
// It does NOT do hinting, kerning, or bidi -- it is a renderer, not a shaper.
//
//     engine::truetype::BakedFont f =
//         engine::truetype::BakeTrueTypeFont("assets/fonts/Roboto.ttf", 48);
//     if (f.ok) { /* f.atlasRGBA is width*height*4, f.glyphs has metrics */ }
//
// SECURITY: like stb_truetype, this does little bounds validation. Only load
// font files you trust.
// -----------------------------------------------------------------------------

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::truetype {

// Per-glyph placement + atlas location. All pixel values are at the baked
// pixelHeight; the text renderer scales them to the requested draw size.
struct Glyph {
    float advance  = 0.0f;   // pen advance in px
    float drawXOff = 0.0f;   // quad left  = penX + drawXOff
    float drawYOff = 0.0f;   // quad top   = baselineY + drawYOff  (negative = above baseline)
    int   w = 0, h = 0;      // glyph bitmap size in px (includes 1px pad on each side)
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;  // atlas UVs (v0 = top row)
    bool  hasBitmap = false; // false for whitespace (advance only)
};

struct BakedFont {
    bool  ok = false;
    int   pixelHeight = 0;
    float ascent  = 0.0f;    // px above baseline (positive)
    float descent = 0.0f;    // px below baseline (negative)
    float lineHeight = 0.0f; // recommended line spacing in px
    int   atlasW = 0, atlasH = 0;
    std::vector<unsigned char> atlasRGBA;      // atlasW*atlasH*4, white with alpha = coverage
    std::unordered_map<int, Glyph> glyphs;     // keyed by unicode codepoint
};

// ---- internal helpers -------------------------------------------------------
namespace detail {

inline std::uint16_t RdU16(const unsigned char* p) {
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}
inline std::int16_t RdS16(const unsigned char* p) {
    return static_cast<std::int16_t>((p[0] << 8) | p[1]);
}
inline std::uint32_t RdU32(const unsigned char* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

struct Pt {
    float x = 0.0f, y = 0.0f;
    bool  on = false;   // on-curve?
};

struct Reader {
    const unsigned char* data = nullptr;
    std::size_t          size = 0;

    // Table offsets (absolute, in bytes from the file start). 0 = missing.
    std::uint32_t head = 0, maxp = 0, cmap = 0, loca = 0, glyf = 0, hhea = 0, hmtx = 0;

    int   unitsPerEm      = 1000;
    int   indexToLocFmt   = 0;      // 0 = short (u16*2), 1 = long (u32)
    int   numGlyphs       = 0;
    int   numHMetrics     = 0;
    int   ascentFU        = 0;      // font units
    int   descentFU       = 0;
    std::uint32_t cmapSub = 0;      // absolute offset of the chosen cmap subtable
    int   cmapFmt         = -1;

    bool InBounds(std::uint32_t off, std::size_t n) const {
        return data && off + n <= size;
    }

    bool ParseDirectory() {
        if (!data || size < 12) return false;
        const std::uint16_t numTables = RdU16(data + 4);
        const std::uint32_t dir = 12;
        if (!InBounds(dir, static_cast<std::size_t>(numTables) * 16)) return false;
        for (std::uint16_t i = 0; i < numTables; ++i) {
            const unsigned char* rec = data + dir + i * 16;
            const std::uint32_t off = RdU32(rec + 8);
            const char t0 = static_cast<char>(rec[0]), t1 = static_cast<char>(rec[1]),
                       t2 = static_cast<char>(rec[2]), t3 = static_cast<char>(rec[3]);
            auto tag = [&](char a, char b, char c, char d) { return t0 == a && t1 == b && t2 == c && t3 == d; };
            if      (tag('h','e','a','d')) head = off;
            else if (tag('m','a','x','p')) maxp = off;
            else if (tag('c','m','a','p')) cmap = off;
            else if (tag('l','o','c','a')) loca = off;
            else if (tag('g','l','y','f')) glyf = off;
            else if (tag('h','h','e','a')) hhea = off;
            else if (tag('h','m','t','x')) hmtx = off;
        }
        return head && maxp && cmap && loca && glyf && hhea && hmtx;
    }

    bool ParseHeaders() {
        if (!InBounds(head, 54) || !InBounds(maxp, 6) || !InBounds(hhea, 36)) return false;
        unitsPerEm    = RdU16(data + head + 18);
        indexToLocFmt = RdS16(data + head + 50);
        if (unitsPerEm <= 0) unitsPerEm = 1000;
        numGlyphs   = RdU16(data + maxp + 4);
        ascentFU    = RdS16(data + hhea + 4);
        descentFU   = RdS16(data + hhea + 6);
        numHMetrics = RdU16(data + hhea + 34);
        if (numHMetrics <= 0) numHMetrics = 1;
        return true;
    }

    // Pick a usable cmap subtable (prefer Windows Unicode, then any Unicode).
    bool ParseCmap() {
        if (!InBounds(cmap, 4)) return false;
        const std::uint16_t numSub = RdU16(data + cmap + 2);
        if (!InBounds(cmap + 4, static_cast<std::size_t>(numSub) * 8)) return false;
        std::uint32_t best = 0; int bestScore = -1;
        for (std::uint16_t i = 0; i < numSub; ++i) {
            const unsigned char* rec = data + cmap + 4 + i * 8;
            const std::uint16_t plat = RdU16(rec + 0);
            const std::uint16_t enc  = RdU16(rec + 2);
            const std::uint32_t off  = RdU32(rec + 4);
            int score = 0;
            if      (plat == 3 && enc == 10) score = 5;   // Windows UCS-4
            else if (plat == 3 && enc == 1)  score = 4;   // Windows BMP
            else if (plat == 0)              score = 3;   // Unicode
            else if (plat == 3 && enc == 0)  score = 2;   // Windows Symbol
            else                             score = 1;
            if (score > bestScore) { bestScore = score; best = cmap + off; }
        }
        if (!best || !InBounds(best, 2)) return false;
        cmapSub = best;
        cmapFmt = RdU16(data + best);
        return true;
    }

    // Map a unicode codepoint to a glyph index using the chosen subtable.
    int GlyphIndex(int cp) const {
        const unsigned char* s = data + cmapSub;
        if (cmapFmt == 0) {
            if (cp < 0 || cp > 255) return 0;
            if (!InBounds(cmapSub + 6 + cp, 1)) return 0;
            return s[6 + cp];
        }
        if (cmapFmt == 6) {
            const int first = RdU16(s + 6);
            const int count = RdU16(s + 8);
            if (cp < first || cp >= first + count) return 0;
            return RdU16(s + 10 + 2 * (cp - first));
        }
        if (cmapFmt == 12) {
            const std::uint32_t nGroups = RdU32(s + 12);
            const unsigned char* g = s + 16;
            for (std::uint32_t i = 0; i < nGroups; ++i, g += 12) {
                const std::uint32_t start = RdU32(g + 0);
                const std::uint32_t end   = RdU32(g + 4);
                const std::uint32_t sg    = RdU32(g + 8);
                if (static_cast<std::uint32_t>(cp) >= start && static_cast<std::uint32_t>(cp) <= end)
                    return static_cast<int>(sg + (static_cast<std::uint32_t>(cp) - start));
            }
            return 0;
        }
        if (cmapFmt == 4) {
            const int segCount = RdU16(s + 6) / 2;
            const unsigned char* endCode   = s + 14;
            const unsigned char* startCode = s + 16 + 2 * segCount;
            const unsigned char* idDelta   = s + 16 + 4 * segCount;
            const unsigned char* idRange   = s + 16 + 6 * segCount;
            for (int i = 0; i < segCount; ++i) {
                const int end = RdU16(endCode + 2 * i);
                if (cp > end) continue;
                const int start = RdU16(startCode + 2 * i);
                if (cp < start) return 0;
                const int delta = RdS16(idDelta + 2 * i);
                const int ro    = RdU16(idRange + 2 * i);
                if (ro == 0) return (cp + delta) & 0xFFFF;
                const unsigned char* addr = idRange + 2 * i + ro + 2 * (cp - start);
                if (!InBounds(static_cast<std::uint32_t>(addr - data), 2)) return 0;
                const int g = RdU16(addr);
                return g == 0 ? 0 : (g + delta) & 0xFFFF;
            }
            return 0;
        }
        return 0;
    }

    // Byte offset + length of glyph `g`'s outline in the glyf table.
    bool GlyphRange(int g, std::uint32_t& off, std::uint32_t& len) const {
        if (g < 0 || g >= numGlyphs) return false;
        std::uint32_t a, b;
        if (indexToLocFmt == 0) {
            if (!InBounds(loca + 2 * (g + 1), 2)) return false;
            a = static_cast<std::uint32_t>(RdU16(data + loca + 2 * g)) * 2;
            b = static_cast<std::uint32_t>(RdU16(data + loca + 2 * (g + 1))) * 2;
        } else {
            if (!InBounds(loca + 4 * (g + 1), 4)) return false;
            a = RdU32(data + loca + 4 * g);
            b = RdU32(data + loca + 4 * (g + 1));
        }
        if (b < a) return false;
        off = glyf + a;
        len = b - a;
        return true;
    }

    float AdvanceFU(int g) const {
        const int idx = (g < numHMetrics) ? g : (numHMetrics - 1);
        if (!InBounds(hmtx + 4 * idx, 4)) return static_cast<float>(unitsPerEm);
        return static_cast<float>(RdU16(data + hmtx + 4 * idx));
    }

    // Collect a glyph's contours as flattened line-segment loops in FONT UNITS.
    // Handles simple glyphs and (translation/scale) composite glyphs.
    bool LoadContours(int g, std::vector<std::vector<Pt>>& contours,
                      int& xMin, int& yMin, int& xMax, int& yMax, int depth = 0) const {
        if (depth > 5) return false;
        std::uint32_t off, len;
        if (!GlyphRange(g, off, len)) return false;
        if (len == 0) { xMin = yMin = xMax = yMax = 0; return true; }   // empty (space)
        if (!InBounds(off, 10)) return false;
        const int numberOfContours = RdS16(data + off);
        xMin = RdS16(data + off + 2);
        yMin = RdS16(data + off + 4);
        xMax = RdS16(data + off + 6);
        yMax = RdS16(data + off + 8);

        if (numberOfContours < 0) {
            // Composite glyph: accumulate component outlines with offset/scale.
            std::uint32_t p = off + 10;
            for (;;) {
                if (!InBounds(p, 4)) break;
                const std::uint16_t flags = RdU16(data + p);
                const std::uint16_t comp  = RdU16(data + p + 2);
                p += 4;
                float dx = 0, dy = 0;
                if (flags & 0x0001) { // ARG_1_AND_2_ARE_WORDS
                    dx = RdS16(data + p); dy = RdS16(data + p + 2); p += 4;
                } else {
                    dx = static_cast<std::int8_t>(data[p]); dy = static_cast<std::int8_t>(data[p + 1]); p += 2;
                }
                float a = 1, b = 0, c = 0, d = 1;
                if (flags & 0x0008) {            // WE_HAVE_A_SCALE
                    a = d = RdS16(data + p) / 16384.0f; p += 2;
                } else if (flags & 0x0040) {     // WE_HAVE_AN_X_AND_Y_SCALE
                    a = RdS16(data + p) / 16384.0f; d = RdS16(data + p + 2) / 16384.0f; p += 4;
                } else if (flags & 0x0080) {     // WE_HAVE_A_TWO_BY_TWO
                    a = RdS16(data + p) / 16384.0f; b = RdS16(data + p + 2) / 16384.0f;
                    c = RdS16(data + p + 4) / 16384.0f; d = RdS16(data + p + 6) / 16384.0f; p += 8;
                }
                std::vector<std::vector<Pt>> sub;
                int sx0, sy0, sx1, sy1;
                if (LoadContours(comp, sub, sx0, sy0, sx1, sy1, depth + 1)) {
                    for (auto& cont : sub) {
                        for (auto& pt : cont) {
                            const float nx = a * pt.x + c * pt.y + dx;
                            const float ny = b * pt.x + d * pt.y + dy;
                            pt.x = nx; pt.y = ny;
                        }
                        contours.push_back(std::move(cont));
                    }
                }
                if (!(flags & 0x0020)) break;    // MORE_COMPONENTS
            }
            return true;
        }

        // Simple glyph.
        std::uint32_t p = off + 10;
        if (!InBounds(p, static_cast<std::size_t>(numberOfContours) * 2 + 2)) return false;
        std::vector<int> endPts(numberOfContours);
        for (int i = 0; i < numberOfContours; ++i) endPts[i] = RdU16(data + p + 2 * i);
        p += static_cast<std::uint32_t>(numberOfContours) * 2;
        const int numPts = numberOfContours ? endPts.back() + 1 : 0;
        const int insLen = RdU16(data + p); p += 2 + insLen;

        // Flags (with repeat).
        std::vector<std::uint8_t> flags;
        flags.reserve(numPts);
        while (static_cast<int>(flags.size()) < numPts) {
            if (!InBounds(p, 1)) return false;
            const std::uint8_t f = data[p++];
            flags.push_back(f);
            if (f & 0x08) {                       // REPEAT
                if (!InBounds(p, 1)) return false;
                int r = data[p++];
                while (r-- > 0 && static_cast<int>(flags.size()) < numPts) flags.push_back(f);
            }
        }

        // X coordinates (delta encoded).
        std::vector<int> xs(numPts), ys(numPts);
        int acc = 0;
        for (int i = 0; i < numPts; ++i) {
            const std::uint8_t f = flags[i];
            if (f & 0x02) {                       // X_SHORT
                if (!InBounds(p, 1)) return false;
                const int dx = data[p++];
                acc += (f & 0x10) ? dx : -dx;
            } else if (!(f & 0x10)) {             // not X_SAME -> signed word
                if (!InBounds(p, 2)) return false;
                acc += RdS16(data + p); p += 2;
            }
            xs[i] = acc;
        }
        acc = 0;
        for (int i = 0; i < numPts; ++i) {
            const std::uint8_t f = flags[i];
            if (f & 0x04) {                       // Y_SHORT
                if (!InBounds(p, 1)) return false;
                const int dy = data[p++];
                acc += (f & 0x20) ? dy : -dy;
            } else if (!(f & 0x20)) {             // not Y_SAME -> signed word
                if (!InBounds(p, 2)) return false;
                acc += RdS16(data + p); p += 2;
            }
            ys[i] = acc;
        }

        // Split into contours and flatten quadratic beziers.
        int startPt = 0;
        for (int c = 0; c < numberOfContours; ++c) {
            const int endPt = endPts[c];
            const int n = endPt - startPt + 1;
            if (n <= 0) { startPt = endPt + 1; continue; }
            std::vector<Pt> raw(n);
            for (int i = 0; i < n; ++i) {
                raw[i].x  = static_cast<float>(xs[startPt + i]);
                raw[i].y  = static_cast<float>(ys[startPt + i]);
                raw[i].on = (flags[startPt + i] & 0x01) != 0;
            }
            std::vector<Pt> poly;
            FlattenContour(raw, poly);
            if (poly.size() >= 2) contours.push_back(std::move(poly));
            startPt = endPt + 1;
        }
        return true;
    }

    static Pt Mid(const Pt& a, const Pt& b) {
        Pt m; m.x = 0.5f * (a.x + b.x); m.y = 0.5f * (a.y + b.y); m.on = true; return m;
    }

    // Flatten one contour of on/off-curve points into a closed line-segment loop.
    static void FlattenContour(const std::vector<Pt>& raw, std::vector<Pt>& out) {
        const int n = static_cast<int>(raw.size());
        if (n == 0) return;

        // Find an on-curve start; if none, synthesize one at a midpoint.
        int startIdx = -1;
        for (int i = 0; i < n; ++i) if (raw[i].on) { startIdx = i; break; }

        std::vector<Pt> pts;      // reordered so pts[0] is on-curve
        if (startIdx >= 0) {
            pts.reserve(n);
            for (int k = 0; k < n; ++k) pts.push_back(raw[(startIdx + k) % n]);
        } else {
            // All off-curve: start at midpoint of last & first, weave midpoints in.
            pts.push_back(Mid(raw[n - 1], raw[0]));
            for (int k = 0; k < n; ++k) {
                pts.push_back(raw[k]);
                if (k < n - 1) pts.push_back(Mid(raw[k], raw[k + 1]));
            }
        }

        const int m = static_cast<int>(pts.size());
        const Pt start = pts[0];
        Pt prev = start;
        bool haveCtrl = false;
        Pt ctrl;
        auto emitLine = [&](const Pt& a, const Pt& b) {
            if (out.empty()) out.push_back(a);
            out.push_back(b);
        };
        auto emitQuad = [&](const Pt& p0, const Pt& c0, const Pt& p1) {
            const float dist = std::fabs(p0.x - c0.x) + std::fabs(p0.y - c0.y) +
                               std::fabs(c0.x - p1.x) + std::fabs(c0.y - p1.y);
            int steps = static_cast<int>(dist / 8.0f);
            if (steps < 2) steps = 2;
            if (steps > 32) steps = 32;
            Pt last = p0;
            for (int s = 1; s <= steps; ++s) {
                const float t = static_cast<float>(s) / steps;
                const float u = 1.0f - t;
                Pt q;
                q.x = u * u * p0.x + 2 * u * t * c0.x + t * t * p1.x;
                q.y = u * u * p0.y + 2 * u * t * c0.y + t * t * p1.y;
                emitLine(last, q);
                last = q;
            }
        };
        for (int k = 1; k <= m; ++k) {
            const Pt& pt = (k == m) ? start : pts[k];
            if (pt.on) {
                if (haveCtrl) { emitQuad(prev, ctrl, pt); haveCtrl = false; }
                else          { emitLine(prev, pt); }
                prev = pt;
            } else {
                if (haveCtrl) { const Pt mid = Mid(ctrl, pt); emitQuad(prev, ctrl, mid); prev = mid; ctrl = pt; }
                else          { ctrl = pt; haveCtrl = true; }
            }
        }
    }
};

// Rasterize flattened contours into an 8-bit coverage bitmap via supersampling.
// Contours are in font units; `scale` converts to px. `pad` px border is added.
inline std::vector<unsigned char> RasterizeGlyph(
    const std::vector<std::vector<Pt>>& contours, int xMin, int yMax,
    float scale, int outW, int outH, int pad, int ss) {
    std::vector<unsigned char> cov(static_cast<std::size_t>(outW) * outH, 0);
    if (outW <= 0 || outH <= 0) return cov;

    const int HW = outW * ss;
    const int HH = outH * ss;

    // Build hi-res edges (y downward). Skip horizontal edges.
    struct Edge { float x0, y0, x1, y1; };
    std::vector<Edge> edges;
    for (const auto& c : contours) {
        for (std::size_t i = 0; i + 1 < c.size(); ++i) {
            const float ax = (c[i].x   - xMin) * scale * ss + pad * ss;
            const float ay = (yMax - c[i].y)   * scale * ss + pad * ss;
            const float bx = (c[i + 1].x - xMin) * scale * ss + pad * ss;
            const float by = (yMax - c[i + 1].y) * scale * ss + pad * ss;
            if (ay != by) edges.push_back({ ax, ay, bx, by });
        }
    }
    if (edges.empty()) return cov;

    std::vector<unsigned char> hires(static_cast<std::size_t>(HW) * HH, 0);
    std::vector<std::pair<float, int>> xs;   // (crossing x, winding dir)
    for (int y = 0; y < HH; ++y) {
        const float sy = y + 0.5f;
        xs.clear();
        for (const auto& e : edges) {
            const float y0 = e.y0, y1 = e.y1;
            const bool up = y1 > y0;
            const float lo = up ? y0 : y1;
            const float hi = up ? y1 : y0;
            if (sy < lo || sy >= hi) continue;
            const float t = (sy - e.y0) / (e.y1 - e.y0);
            const float x = e.x0 + t * (e.x1 - e.x0);
            xs.push_back({ x, up ? 1 : -1 });
        }
        if (xs.size() < 2) continue;
        std::sort(xs.begin(), xs.end(),
                  [](const std::pair<float,int>& a, const std::pair<float,int>& b) { return a.first < b.first; });
        int winding = 0;
        for (std::size_t i = 0; i + 1 < xs.size(); ++i) {
            winding += xs[i].second;
            if (winding == 0) continue;               // non-zero winding rule
            int xa = static_cast<int>(std::ceil(xs[i].first - 0.5f));
            int xb = static_cast<int>(std::ceil(xs[i + 1].first - 0.5f));
            if (xa < 0) xa = 0;
            if (xb > HW) xb = HW;
            unsigned char* row = &hires[static_cast<std::size_t>(y) * HW];
            for (int x = xa; x < xb; ++x) row[x] = 1;
        }
    }

    // Box-downsample the hi-res mask into 0..255 coverage.
    const int area = ss * ss;
    for (int py = 0; py < outH; ++py) {
        for (int px = 0; px < outW; ++px) {
            int sum = 0;
            for (int sy = 0; sy < ss; ++sy) {
                const unsigned char* row = &hires[static_cast<std::size_t>(py * ss + sy) * HW + px * ss];
                for (int sx = 0; sx < ss; ++sx) sum += row[sx];
            }
            cov[static_cast<std::size_t>(py) * outW + px] =
                static_cast<unsigned char>((sum * 255) / area);
        }
    }
    return cov;
}

} // namespace detail

// Bake `pixelHeight`-tall glyphs for ASCII 32..126 from a font in memory.
inline BakedFont BakeTrueTypeFontFromMemory(const unsigned char* data, std::size_t size, int pixelHeight) {
    using namespace detail;
    BakedFont font;
    if (!data || size < 12 || pixelHeight < 4) return font;

    Reader r;
    r.data = data;
    r.size = size;
    if (!r.ParseDirectory() || !r.ParseHeaders() || !r.ParseCmap()) return font;

    const float scale = static_cast<float>(pixelHeight) / static_cast<float>(r.unitsPerEm);
    const int   pad   = 1;
    const int   ss    = 4;   // 4x4 supersampling

    font.pixelHeight = pixelHeight;
    font.ascent      = r.ascentFU  * scale;
    font.descent     = r.descentFU * scale;
    font.lineHeight  = (r.ascentFU - r.descentFU) * scale;
    if (font.lineHeight < static_cast<float>(pixelHeight)) font.lineHeight = static_cast<float>(pixelHeight);

    // Rasterize each glyph, remembering its coverage bitmap for a later atlas pack.
    struct Baked { int cp; int w, h; float advance, drawXOff, drawYOff; std::vector<unsigned char> cov; };
    std::vector<Baked> baked;
    baked.reserve(95);

    for (int cp = 32; cp <= 126; ++cp) {
        const int g = r.GlyphIndex(cp);
        std::vector<std::vector<Pt>> contours;
        int xMin = 0, yMin = 0, xMax = 0, yMax = 0;
        r.LoadContours(g, contours, xMin, yMin, xMax, yMax);
        const float advance = r.AdvanceFU(g) * scale;

        Baked b;
        b.cp = cp;
        b.advance = advance;

        const int wPx = static_cast<int>(std::ceil((xMax - xMin) * scale)) + 2 * pad;
        const int hPx = static_cast<int>(std::ceil((yMax - yMin) * scale)) + 2 * pad;
        if (contours.empty() || wPx <= 2 * pad || hPx <= 2 * pad) {
            b.w = 0; b.h = 0; b.drawXOff = 0; b.drawYOff = 0;   // whitespace
        } else {
            b.w = wPx; b.h = hPx;
            b.drawXOff = xMin * scale - pad;                     // pen -> quad left
            b.drawYOff = -(yMax * scale) - pad;                  // baseline -> quad top (up = negative)
            b.cov = RasterizeGlyph(contours, xMin, yMax, scale, wPx, hPx, pad, ss);
        }
        baked.push_back(std::move(b));
    }

    // Shelf-pack the glyph bitmaps into a single atlas.
    const int atlasW = 512;
    int penX = 1, penY = 1, rowH = 0;
    for (const auto& b : baked) {
        if (b.w == 0) continue;
        if (penX + b.w + 1 > atlasW) { penX = 1; penY += rowH + 1; rowH = 0; }
        penX += b.w + 1;
        if (b.h > rowH) rowH = b.h;
    }
    int atlasH = penY + rowH + 1;
    // Round height up to a small power-ish size (keeps some GL drivers happy).
    int pot = 1; while (pot < atlasH) pot <<= 1;
    atlasH = pot;

    font.atlasW = atlasW;
    font.atlasH = atlasH;
    font.atlasRGBA.assign(static_cast<std::size_t>(atlasW) * atlasH * 4, 0);

    penX = 1; penY = 1; rowH = 0;
    for (const auto& b : baked) {
        Glyph gm;
        gm.advance  = b.advance;
        gm.drawXOff = b.drawXOff;
        gm.drawYOff = b.drawYOff;
        if (b.w == 0) {
            gm.hasBitmap = false;
            font.glyphs[b.cp] = gm;
            continue;
        }
        if (penX + b.w + 1 > atlasW) { penX = 1; penY += rowH + 1; rowH = 0; }
        // Blit coverage as white RGBA with alpha = coverage.
        for (int y = 0; y < b.h; ++y) {
            for (int x = 0; x < b.w; ++x) {
                const unsigned char a = b.cov[static_cast<std::size_t>(y) * b.w + x];
                const std::size_t dst = (static_cast<std::size_t>(penY + y) * atlasW + (penX + x)) * 4;
                font.atlasRGBA[dst + 0] = 255;
                font.atlasRGBA[dst + 1] = 255;
                font.atlasRGBA[dst + 2] = 255;
                font.atlasRGBA[dst + 3] = a;
            }
        }
        gm.w = b.w; gm.h = b.h; gm.hasBitmap = true;
        gm.u0 = static_cast<float>(penX)         / atlasW;
        gm.v0 = static_cast<float>(penY)         / atlasH;
        gm.u1 = static_cast<float>(penX + b.w)   / atlasW;
        gm.v1 = static_cast<float>(penY + b.h)   / atlasH;
        font.glyphs[b.cp] = gm;

        penX += b.w + 1;
        if (b.h > rowH) rowH = b.h;
    }

    font.ok = true;
    return font;
}

// Bake from a .ttf file on disk.
inline BakedFont BakeTrueTypeFont(const std::string& path, int pixelHeight) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return BakedFont{};
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
    return BakeTrueTypeFontFromMemory(bytes.data(), bytes.size(), pixelHeight);
}

} // namespace engine::truetype
