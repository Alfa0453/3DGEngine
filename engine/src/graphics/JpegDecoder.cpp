// From-scratch baseline JPEG decoder: file -> RGBA. Supports baseline
// (sequential DCT, Huffman) JFIF images, grayscale or YCbCr, with chroma
// subsampling and restart markers. Progressive and arithmetic JPEGs are not
// supported. No dependencies — in the spirit of the engine's other loaders.
#include "engine/graphics/ImageDecode.h"

#include <algorithm>
#include <cmath>

constexpr double M_PI = 3.14159265358979323846;
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace engine::image {
namespace {

// Natural-order index for each zig-zag position.
const int kZig[64] = {
     0, 1, 8,16, 9, 2, 3,10, 17,24,32,25,18,11, 4, 5,
    12,19,26,33,40,48,41,34, 27,20,13, 6, 7,14,21,28,
    35,42,49,56,57,50,43,36, 29,22,15,23,30,37,44,51,
    58,59,52,45,38,31,39,46, 53,60,61,54,47,55,62,63
};

struct Huff {
    std::uint8_t              counts[17] = {0};   // counts[1..16]
    std::vector<std::uint8_t> values;
    int mincode[17], maxcode[17], valptr[17];
    void build() {
        int code = 0, k = 0;
        for (int len = 1; len <= 16; ++len) {
            valptr[len]  = k;
            mincode[len] = code;
            code += counts[len];
            maxcode[len] = counts[len] ? code - 1 : -1;
            k    += counts[len];
            code <<= 1;
        }
    }
};

struct Component {
    int id = 0, h = 1, v = 1, quant = 0;
    int dcTable = 0, acTable = 0;
    int pred = 0;                       // DC predictor
    std::vector<float> plane;           // decoded samples (subsampled res)
    int planeW = 0, planeH = 0;
};

struct BitReader {
    const std::uint8_t* d;
    std::size_t n, pos;
    std::uint32_t buf = 0;
    int cnt = 0;
    bool hitMarker = false;

    int bit() {
        if (cnt == 0) {
            if (pos >= n) { hitMarker = true; return 0; }
            std::uint8_t b = d[pos++];
            if (b == 0xFF) {
                std::uint8_t b2 = (pos < n) ? d[pos] : 0;
                if (b2 == 0x00) { pos++; }            // stuffed byte
                else { hitMarker = true; pos--; return 0; }  // real marker ahead
            }
            buf = b; cnt = 8;
        }
        --cnt;
        return (buf >> cnt) & 1;
    }
    int bits(int k) { int v = 0; for (int i = 0; i < k; ++i) v = (v << 1) | bit(); return v; }
    void restart() { cnt = 0; hitMarker = false; }
};

int Receive(BitReader& br, int s) {
    if (s == 0) return 0;
    int v = br.bits(s);
    if (v < (1 << (s - 1))) v += (-(1 << s)) + 1;   // extend sign
    return v;
}

int DecodeHuff(BitReader& br, const Huff& h) {
    int code = 0;
    for (int len = 1; len <= 16; ++len) {
        code = (code << 1) | br.bit();
        if (h.maxcode[len] >= 0 && code <= h.maxcode[len])
            return h.values[static_cast<std::size_t>(h.valptr[len] + (code - h.mincode[len]))];
    }
    return 0;
}

void IDCT(const int in[64], float out[64]) {
    static double c[8][8];
    static bool init = false;
    if (!init) {
        for (int u = 0; u < 8; ++u)
            for (int x = 0; x < 8; ++x) {
                const double cu = (u == 0) ? std::sqrt(1.0 / 8.0) : std::sqrt(2.0 / 8.0);
                c[u][x] = cu * std::cos((2 * x + 1) * u * M_PI / 16.0);
            }
        init = true;
    }
    double tmp[64];
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            double s = 0.0;
            for (int u = 0; u < 8; ++u) s += c[u][x] * in[y * 8 + u];
            tmp[y * 8 + x] = s;
        }
    for (int x = 0; x < 8; ++x)
        for (int y = 0; y < 8; ++y) {
            double s = 0.0;
            for (int v = 0; v < 8; ++v) s += c[v][y] * tmp[v * 8 + x];
            out[y * 8 + x] = static_cast<float>(s);
        }
}

std::uint16_t be16(const std::uint8_t* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }
inline std::uint8_t clamp8(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<std::uint8_t>(v + 0.5f)); }

} // namespace

Image DecodeJPEGFromMemory(const unsigned char* data, std::size_t size) {
    const std::vector<std::uint8_t> b(data, data + size);
    if (b.size() < 2 || b[0] != 0xFF || b[1] != 0xD8) throw std::runtime_error("JPEG: bad SOI");

    std::uint16_t qt[4][64] = {{0}};
    Huff dcH[4], acH[4];
    std::vector<Component> comp;
    int width = 0, height = 0, restartInterval = 0;

    std::size_t p = 2;
    while (p + 4 <= b.size()) {
        if (b[p] != 0xFF) { ++p; continue; }
        const std::uint8_t marker = b[p + 1];
        p += 2;
        if (marker == 0xD9) break;                              // EOI
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) continue;  // standalone
        const std::uint16_t len = be16(&b[p]);
        const std::size_t seg = p + 2;
        const std::size_t segEnd = p + len;
        if (segEnd > b.size()) throw std::runtime_error("JPEG: truncated segment");

        if (marker == 0xDB) {                                   // DQT
            std::size_t q = seg;
            while (q < segEnd) {
                const int prec = b[q] >> 4, id = b[q] & 15; ++q;
                if (id > 3) throw std::runtime_error("JPEG: bad quant id");
                for (int i = 0; i < 64; ++i) {
                    qt[id][i] = prec ? be16(&b[q]) : b[q];
                    q += prec ? 2 : 1;
                }
            }
        } else if (marker == 0xC0 || marker == 0xC1) {          // SOF0/1 (baseline)
            height = be16(&b[seg + 1]);
            width  = be16(&b[seg + 3]);
            const int nc = b[seg + 5];
            comp.resize(static_cast<std::size_t>(nc));
            for (int i = 0; i < nc; ++i) {
                const std::size_t o = seg + 6 + static_cast<std::size_t>(i) * 3;
                comp[i].id    = b[o];
                comp[i].h     = b[o + 1] >> 4;
                comp[i].v     = b[o + 1] & 15;
                comp[i].quant = b[o + 2];
            }
        } else if (marker == 0xC2) {
            throw std::runtime_error("JPEG: progressive JPEG not supported");
        } else if (marker == 0xC4) {                            // DHT
            std::size_t q = seg;
            while (q < segEnd) {
                const int cls = b[q] >> 4, id = b[q] & 15; ++q;
                Huff& h = cls ? acH[id] : dcH[id];
                int total = 0;
                for (int i = 1; i <= 16; ++i) { h.counts[i] = b[q++]; total += h.counts[i]; }
                h.values.assign(b.begin() + static_cast<std::ptrdiff_t>(q),
                                b.begin() + static_cast<std::ptrdiff_t>(q + total));
                q += static_cast<std::size_t>(total);
                h.build();
            }
        } else if (marker == 0xDD) {                            // DRI
            restartInterval = be16(&b[seg]);
        } else if (marker == 0xDA) {                            // SOS -> entropy data
            const int ns = b[seg];
            for (int i = 0; i < ns; ++i) {
                const int cid = b[seg + 1 + static_cast<std::size_t>(i) * 2];
                const int tbl = b[seg + 2 + static_cast<std::size_t>(i) * 2];
                for (auto& c : comp) if (c.id == cid) { c.dcTable = tbl >> 4; c.acTable = tbl & 15; }
            }
            std::size_t scan = segEnd;   // entropy-coded data starts after the SOS header

            // Sampling factors and per-component plane geometry.
            int Hmax = 1, Vmax = 1;
            for (const auto& c : comp) { Hmax = std::max(Hmax, c.h); Vmax = std::max(Vmax, c.v); }
            const int mcuW = 8 * Hmax, mcuH = 8 * Vmax;
            const int mcusX = (width + mcuW - 1) / mcuW;
            const int mcusY = (height + mcuH - 1) / mcuH;
            for (auto& c : comp) {
                c.planeW = mcusX * c.h * 8;
                c.planeH = mcusY * c.v * 8;
                c.plane.assign(static_cast<std::size_t>(c.planeW) * c.planeH, 0.0f);
                c.pred = 0;
            }

            BitReader br{b.data(), b.size(), scan, 0, 0, false};
            int mcuCount = 0;
            for (int my = 0; my < mcusY; ++my) {
                for (int mx = 0; mx < mcusX; ++mx) {
                    for (auto& c : comp) {
                        for (int by = 0; by < c.v; ++by) {
                            for (int bx = 0; bx < c.h; ++bx) {
                                int block[64] = {0};
                                const int t = DecodeHuff(br, dcH[c.dcTable]);
                                c.pred += Receive(br, t);
                                block[0] = c.pred * qt[c.quant][0];
                                int k = 1;
                                while (k < 64) {
                                    const int rs = DecodeHuff(br, acH[c.acTable]);
                                    const int r = rs >> 4, s = rs & 15;
                                    if (s == 0) { if (r == 15) { k += 16; continue; } break; }
                                    k += r;
                                    if (k > 63) break;
                                    block[kZig[k]] = Receive(br, s) * qt[c.quant][k];
                                    ++k;
                                }
                                float out[64];
                                IDCT(block, out);
                                const int px0 = (mx * c.h + bx) * 8;
                                const int py0 = (my * c.v + by) * 8;
                                for (int yy = 0; yy < 8; ++yy)
                                    for (int xx = 0; xx < 8; ++xx)
                                        c.plane[static_cast<std::size_t>(py0 + yy) * c.planeW + (px0 + xx)]
                                            = out[yy * 8 + xx] + 128.0f;
                            }
                        }
                    }
                    if (restartInterval && ++mcuCount % restartInterval == 0
                        && !(my == mcusY - 1 && mx == mcusX - 1)) {
                        br.restart();
                        // skip the RSTn marker in the byte stream
                        while (br.pos + 1 < b.size() && !(b[br.pos] == 0xFF &&
                               b[br.pos + 1] >= 0xD0 && b[br.pos + 1] <= 0xD7)) ++br.pos;
                        br.pos += 2;
                        for (auto& c : comp) c.pred = 0;
                    }
                }
            }

            // Assemble RGBA (upsample chroma by replication).
            Image img;
            img.width = width; img.height = height;
            img.rgba.resize(static_cast<std::size_t>(width) * height * 4);
            const bool gray = comp.size() == 1;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    std::uint8_t* o = &img.rgba[(static_cast<std::size_t>(y) * width + x) * 4];
                    // Centre-aligned bilinear upsample (smooth chroma, matching
                    // libjpeg's "fancy" upsampling); exact for full-res components.
                    auto sample = [&](const Component& c) -> float {
                        const float fx = (x + 0.5f) * c.h / Hmax - 0.5f;
                        const float fy = (y + 0.5f) * c.v / Vmax - 0.5f;
                        const int x0 = static_cast<int>(std::floor(fx));
                        const int y0 = static_cast<int>(std::floor(fy));
                        const float tx = fx - x0, ty = fy - y0;
                        auto at = [&](int xx, int yy) -> float {
                            xx = std::min(std::max(xx, 0), c.planeW - 1);
                            yy = std::min(std::max(yy, 0), c.planeH - 1);
                            return c.plane[static_cast<std::size_t>(yy) * c.planeW + xx];
                        };
                        const float a = at(x0, y0),     b = at(x0 + 1, y0);
                        const float d = at(x0, y0 + 1), e = at(x0 + 1, y0 + 1);
                        return a * (1 - tx) * (1 - ty) + b * tx * (1 - ty)
                             + d * (1 - tx) * ty       + e * tx * ty;
                    };
                    if (gray) {
                        const std::uint8_t g = clamp8(sample(comp[0]));
                        o[0] = o[1] = o[2] = g; o[3] = 255;
                    } else {
                        const float Y  = sample(comp[0]);
                        const float Cb = sample(comp[1]) - 128.0f;
                        const float Cr = sample(comp[2]) - 128.0f;
                        o[0] = clamp8(Y + 1.402f * Cr);
                        o[1] = clamp8(Y - 0.344136f * Cb - 0.714136f * Cr);
                        o[2] = clamp8(Y + 1.772f * Cb);
                        o[3] = 255;
                    }
                }
            }
            return img;
        }
        p = segEnd;
    }
    throw std::runtime_error("JPEG: no scan data found");
}

Image DecodeJPEG(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("JPEG: cannot open '" + path + "'");
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                           std::istreambuf_iterator<char>());
    return DecodeJPEGFromMemory(bytes.data(), bytes.size());
}

} // namespace engine::image
