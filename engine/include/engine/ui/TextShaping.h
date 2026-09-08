#pragma once

// Font system upgrade -- UTF-8 decoding, text metrics, multiline measurement, word wrap and
// alignment (Phases 17/18/19/21/24/25).
//
// Renderer-agnostic on purpose: everything works from a TextMetricsSource callback that returns a
// codepoint's advance at the desired size, so the SAME code measures the built-in bitmap font and an
// imported proportional TrueType font identically. No GL, no font parsing here -- pure layout math,
// harness-testable. Byte-based `for (char c : text)` iteration is replaced by proper UTF-8 decoding;
// malformed bytes decode to U+FFFD rather than being misinterpreted as separate characters.

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace engine {
namespace text {

inline constexpr std::uint32_t kReplacementCodepoint = 0xFFFD;   // U+FFFD REPLACEMENT CHARACTER

// Decode the next UTF-8 codepoint from [p,end); advances p. Returns U+FFFD for malformed/overlong/
// truncated sequences (consuming exactly one byte so decoding always makes progress).
inline std::uint32_t NextCodepoint(const char*& p, const char* end) {
    if (p >= end) return 0;
    const unsigned char b0 = static_cast<unsigned char>(*p);
    if (b0 < 0x80) { ++p; return b0; }                                   // ASCII
    int extra; std::uint32_t cp;
    if ((b0 & 0xE0) == 0xC0) { extra = 1; cp = b0 & 0x1F; }
    else if ((b0 & 0xF0) == 0xE0) { extra = 2; cp = b0 & 0x0F; }
    else if ((b0 & 0xF8) == 0xF0) { extra = 3; cp = b0 & 0x07; }
    else { ++p; return kReplacementCodepoint; }                          // invalid lead byte
    if (p + 1 + extra > end) { ++p; return kReplacementCodepoint; }      // truncated
    for (int i = 1; i <= extra; ++i) {
        const unsigned char bn = static_cast<unsigned char>(p[i]);
        if ((bn & 0xC0) != 0x80) { ++p; return kReplacementCodepoint; }  // bad continuation
        cp = (cp << 6) | (bn & 0x3F);
    }
    // reject overlong encodings and surrogates
    static const std::uint32_t mins[4] = {0, 0x80, 0x800, 0x10000};
    if (cp < mins[extra] || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) { ++p; return kReplacementCodepoint; }
    p += 1 + extra;
    return cp;
}

inline std::vector<std::uint32_t> DecodeUtf8(const std::string& s) {
    std::vector<std::uint32_t> out;
    const char* p = s.data(); const char* end = p + s.size();
    while (p < end) out.push_back(NextCodepoint(p, end));
    return out;
}

// Advance width for a codepoint at the target size, plus vertical metrics at that size.
struct TextMetricsSource {
    std::function<float(std::uint32_t)> advance;   // px advance for a codepoint (already at target size)
    float lineHeight = 0.0f;
    float ascent = 0.0f;
    float descent = 0.0f;
};

struct TextMetrics {
    float width = 0.0f;       // max line advance
    float height = 0.0f;      // lineCount * lineHeight
    float ascent = 0.0f;
    float descent = 0.0f;
    float lineHeight = 0.0f;
    int   lineCount = 0;
};

// Measure multi-line text (splits on '\n'). Width is the widest line's advance sum.
inline TextMetrics MeasureText(const std::string& utf8, const TextMetricsSource& m) {
    TextMetrics r; r.lineHeight = m.lineHeight; r.ascent = m.ascent; r.descent = m.descent;
    float lineW = 0.0f; int lines = 1;
    const char* p = utf8.data(); const char* end = p + utf8.size();
    while (p < end) {
        const std::uint32_t cp = NextCodepoint(p, end);
        if (cp == '\n') { if (lineW > r.width) r.width = lineW; lineW = 0.0f; ++lines; continue; }
        lineW += m.advance ? m.advance(cp) : 0.0f;
    }
    if (lineW > r.width) r.width = lineW;
    r.lineCount = lines;
    r.height = static_cast<float>(lines) * m.lineHeight;
    return r;
}

// Greedy word wrap to maxWidth (px), preserving explicit '\n'. Words are split on spaces; a single
// word longer than maxWidth is left intact (no mid-word breaking in this pass). Returns the lines.
inline std::vector<std::string> WrapText(const std::string& utf8, float maxWidth,
                                         const TextMetricsSource& m) {
    std::vector<std::string> lines;
    if (maxWidth <= 0.0f || !m.advance) { lines.push_back(utf8); return lines; }
    auto measure = [&](const std::string& s) {
        float w = 0.0f; const char* p = s.data(); const char* e = p + s.size();
        while (p < e) w += m.advance(NextCodepoint(p, e)); return w;
    };
    std::string paragraphAccum;
    const char* p = utf8.data(); const char* end = p + utf8.size();
    std::string current;      // current output line
    std::string word;         // current word being built
    auto flushWord = [&]() {
        if (word.empty()) return;
        const std::string candidate = current.empty() ? word : current + " " + word;
        if (measure(candidate) <= maxWidth || current.empty()) current = candidate;
        else { lines.push_back(current); current = word; }
        word.clear();
    };
    while (p < end) {
        const char* charStart = p;
        const std::uint32_t cp = NextCodepoint(p, end);
        if (cp == '\n') { flushWord(); lines.push_back(current); current.clear(); continue; }
        if (cp == ' ') { flushWord(); continue; }
        word.append(charStart, p);   // keep original UTF-8 bytes
    }
    flushWord();
    lines.push_back(current);
    return lines;
}

enum class HAlign { Left, Center, Right };
enum class VAlign { Top, Middle, Bottom };

// Compute the top-left draw origin for `metrics` inside the rect, honoring alignment. Left/Top match
// the legacy HUD behavior, so it stays the default.
inline glm::vec2 AlignedOrigin(HAlign h, VAlign v,
                               float rectX, float rectY, float rectW, float rectH,
                               const TextMetrics& metrics) {
    float x = rectX, y = rectY;
    switch (h) {
        case HAlign::Left:   x = rectX; break;
        case HAlign::Center: x = rectX + (rectW - metrics.width) * 0.5f; break;
        case HAlign::Right:  x = rectX + (rectW - metrics.width); break;
    }
    switch (v) {
        case VAlign::Top:    y = rectY; break;
        case VAlign::Middle: y = rectY + (rectH - metrics.height) * 0.5f; break;
        case VAlign::Bottom: y = rectY + (rectH - metrics.height); break;
    }
    return glm::vec2(x, y);
}

} // namespace text
} // namespace engine
