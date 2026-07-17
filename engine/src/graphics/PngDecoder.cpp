// From-scratch PNG decoder: file -> RGBA. Includes a hand-written DEFLATE
// (RFC 1951) inflate and PNG scanline un-filtering (RFC 2083). No dependencies,
// in the same spirit as the engine's TGA loader and bitmap font.
#include "engine/graphics/ImageDecode.h"

#include <cstdint>
#include <fstream>
#include <stdexcept>

namespace engine::image {
namespace {

// ---- Bit reader (DEFLATE is least-significant-bit-first) --------------------
struct BitReader {
    const unsigned char* d;
    std::size_t n;
    std::size_t pos = 0;
    unsigned    bitbuf = 0;
    int         bitcnt = 0;

    int getbit() {
        if (bitcnt == 0) {
            if (pos >= n) throw std::runtime_error("PNG: deflate stream underrun");
            bitbuf = d[pos++];
            bitcnt = 8;
        }
        const int b = bitbuf & 1;
        bitbuf >>= 1;
        --bitcnt;
        return b;
    }
    unsigned getbits(int count) {
        unsigned v = 0;
        for (int i = 0; i < count; ++i) v |= static_cast<unsigned>(getbit()) << i;
        return v;
    }
    void align() { bitcnt = 0; }
};

// ---- Canonical Huffman decoder (puff-style) --------------------------------
struct Huffman {
    std::vector<int> counts;    // number of codes of each length
    std::vector<int> symbols;   // symbols sorted by (length, value)
    int maxbits = 0;

    void build(const std::vector<int>& lengths) {
        maxbits = 0;
        for (int l : lengths) if (l > maxbits) maxbits = l;
        counts.assign(maxbits + 1, 0);
        for (int l : lengths) if (l) ++ counts[l];
        std::vector<int> offset(maxbits + 2, 0);
        for (int len = 1; len <= maxbits; ++len) offset[len + 1] = offset[len] + counts[len];
        symbols.assign(lengths.size(), 0);
        for (std::size_t s = 0; s < lengths.size(); ++s)
            if (lengths[s]) symbols[offset[lengths[s]]++] = static_cast<int>(s);
    }
    int decode(BitReader& br) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= maxbits; ++len) {
            code |= br.getbit();
            const int count = counts[len];
            if (code - first < count) return symbols[index + (code - first)];
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        throw std::runtime_error("PNG: bad huffman code");
    }
};

std::vector<unsigned char> Inflate(const unsigned char* data, std::size_t len) {
    static const int lbase[] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const int lext[]  = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const int dbase[] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const int dext[]  = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

    BitReader br{data, len};
    std::vector<unsigned char> out;
    bool final = false;
    while (!final) {
        final = br.getbit() != 0;
        const int type = static_cast<int>(br.getbits(2));
        if (type == 0) {                                    // stored
            br.align();
            if (br.pos + 4 > br.n) throw std::runtime_error("PNG: stored block truncated");
            const int blen = br.d[br.pos] | (br.d[br.pos + 1] << 8);
            br.pos += 4;                                    // skip LEN + NLEN
            for (int i = 0; i < blen; ++i) {
                if (br.pos >= br.n) throw std::runtime_error("PNG: stored block underrun");
                out.push_back(br.d[br.pos++]);
            }
            continue;
        }
        Huffman lit, dist;
        if (type == 1) {                                    // fixed Huffman
            std::vector<int> ll(288);
            for (int i = 0;   i < 144; ++i) ll[i] = 8;
            for (int i = 144; i < 256; ++i) ll[i] = 9;
            for (int i = 256; i < 280; ++i) ll[i] = 7;
            for (int i = 280; i < 288; ++i) ll[i] = 8;
            lit.build(ll);
            dist.build(std::vector<int>(30, 5));
        } else if (type == 2) {                             // dynamic Huffman
            const int hlit  = static_cast<int>(br.getbits(5)) + 257;
            const int hdist = static_cast<int>(br.getbits(5)) + 1;
            const int hclen = static_cast<int>(br.getbits(4)) + 4;
            static const int ord[] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            std::vector<int> cll(19, 0);
            for (int i = 0; i < hclen; ++i) cll[ord[i]] = static_cast<int>(br.getbits(3));
            Huffman cl; cl.build(cll);
            std::vector<int> lengths;
            lengths.reserve(static_cast<std::size_t>(hlit + hdist));
            while (static_cast<int>(lengths.size()) < hlit + hdist) {
                const int sym = cl.decode(br);
                if (sym < 16) {
                    lengths.push_back(sym);
                } else if (sym == 16) {
                    if (lengths.empty()) throw std::runtime_error("PNG: bad repeat");
                    const int prev = lengths.back();
                    for (int r = br.getbits(2) + 3; r > 0; --r) lengths.push_back(prev);
                } else if (sym == 17) {
                    for (int r = br.getbits(3) + 3; r  > 0; --r) lengths.push_back(0);
                } else {
                    for (int r = br.getbits(7) + 11; r > 0; --r) lengths.push_back(0);
                }
            }
            lit.build(std::vector<int>(lengths.begin(), lengths.begin() + hlit));
            dist.build(std::vector<int>(lengths.begin() + hlit, lengths.end()));
        } else {
            throw std::runtime_error("PNG: bad deflate block type");
        }
        for (;;) {
            const int sym = lit.decode(br);
            if (sym == 256) break;
            if (sym < 256) {
                out.push_back(static_cast<unsigned char>(sym));
            } else {
                const int s = sym - 257;
                if (s >= 29) throw std::runtime_error("PNG: bad length symbol");
                const int length   = lbase[s] + static_cast<int>(br.getbits(lext[s]));
                const int ds       = dist.decode(br);
                const int distance = dbase[ds] + static_cast<int>(br.getbits(dext[ds]));
                if (static_cast<std::size_t>(distance) > out.size())
                    throw std::runtime_error("PNG: distnace too far back");
                const std::size_t start = out.size() - static_cast<std::size_t>(distance);
                for (int i = 0; i < length; ++i) out.push_back(out[start + i]);
            }
        }
    }
    return out;
}

// ---- big-endian helpers / Paeth -------------------------------------------
std::uint32_t be32(const unsigned char* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8)  | std::uint32_t(p[3]);
}
int Paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = p > a ? p - a : a - p;
    const int pb = p > b ? p - b : b - p;
    const int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    return (pb <= pc) ? b : c;
}

} // namespace

Image DecodePNGFromMemeory(const unsigned char* data, std::size_t size) {
    const std::vector<unsigned char> buf(data, data + size);

    static const unsigned char sig[8] = {137,80,78,71,13,10,26,10};
    if (buf.size() < 8) throw std::runtime_error("PNG: too small");
    for (int i = 0; i < 8; ++i)
        if (buf[i] != sig[i]) throw std::runtime_error("PNG: bad signature");

    int width = 0, height = 0, bitDepth = 0, colorType = 0, interlace = 0;
    std::vector<unsigned char> idat, palette, trns;

    std::size_t pos = 8;
    while (pos + 8 <= buf.size()) {
        const std::uint32_t len = be32(&buf[pos]);
        const char* type = reinterpret_cast<const char*>(&buf[pos + 4]);
        const std::size_t chunkData = pos + 8;
        if (chunkData + len > buf.size()) throw std::runtime_error("PNG: truncated chunk");

        if (std::string(type, 4) == "IHDR") {
            width     = static_cast<int>(be32(&buf[chunkData]));
            height    = static_cast<int>(be32(&buf[chunkData + 4]));
            bitDepth  = buf[chunkData + 8];
            colorType = buf[chunkData + 9];
            interlace = buf[chunkData + 12];
        } else if (std::string(type, 4) == "PLTE") {
            palette.assign(buf.begin() + chunkData, buf.begin() + chunkData + len);
        } else if (std::string(type, 4) == "tRNS") {
            trns.assign(buf.begin() + chunkData, buf.begin() + chunkData + len);
        } else if (std::string(type, 4) == "IDAT") {
            idat.insert(idat.end(), buf.begin() + chunkData, buf.begin() + chunkData + len);
        } else if (std::string(type, 4) == "IEND") {
            break;
        }
        pos = chunkData + len + 4;   // skip data + CRC
    }

    if (width <= 0 || height <= 0) throw std::runtime_error("PNG: bad dimensions");
    const bool packedOk = (colorType == 0 || colorType == 3) &&
                          (bitDepth == 1 || bitDepth == 2 || bitDepth == 4);
    const bool sixteenBitOk = bitDepth == 16 && colorType != 3;
    if (!(bitDepth == 8 || sixteenBitOk || packedOk))
        throw std::runtime_error("PNG: unsupported bit depth (supports 16/8-bit, or 1/2/4-bit gray/palette)");
    if (interlace != 0) throw std::runtime_error("PNG: interlaced PNG not supported");
    if (idat.size() < 2) throw std::runtime_error("PNG: missing image data");

    int channels;
    switch (colorType) {
        case 0: channels = 1; break;   // grayscale
        case 2: channels = 3; break;   // RGB
        case 3: channels = 1; break;   // palette index
        case 4: channels = 2; break;   // grayscale + alpha
        case 6: channels = 4; break;   // RGBA
        default: throw std::runtime_error("PNG: unsupported colour type");
    }

    // Inflate the zlib stream (skip the 2-byte zlib header).
    const std::vector<unsigned char> raw = Inflate(idat.data() + 2, idat.size() - 2);

    const int bitsPrevPixel = channels * bitDepth;
    const std::size_t stride = (static_cast<std::size_t>(width) * bitsPrevPixel + 7) / 8;
    if (raw.size() < (stride + 1) * static_cast<std::size_t>(height))
        throw std::runtime_error("PNG: not enough decompressed data");

    // Un-filter scanlines into `recon`. Filtering is byte-wise; for sub-byte
    // depths the filter "bytes-per-pixel" is 1 (per the PNG spec).
    std::vector<unsigned char> recon(stride * static_cast<std::size_t>(height));
    const int bpp = (bitsPrevPixel >= 8) ? bitsPrevPixel / 8 : 1;
    std::size_t src = 0;
    for (int y = 0; y < height; ++y) {
        const int filter = raw[src++];
        unsigned char* row = &recon[static_cast<std::size_t>(y) * stride];
        const unsigned char* prev = (y > 0) ? &recon[(static_cast<std::size_t>(y) - 1) * stride] : nullptr;
        for (std::size_t i = 0; i < stride; ++i) {
            const int x = raw[src++];
            const int a = (i >= static_cast<std::size_t>(bpp)) ? row[i - bpp] : 0;
            const int b = prev ? prev[i] : 0;
            const int c = (prev && i >= static_cast<std::size_t>(bpp)) ? prev[i - bpp] : 0;
            int v;
            switch (filter) {
                case 0: v = x;                       break;  // None
                case 1: v = x + a;                   break;  // Sub
                case 2: v = x + b;                   break;  // Up
                case 3: v = x + ((a + b) >> 1);      break;  // Average
                case 4: v = x + Paeth(a, b, c);      break;  // Paeth
                default: throw std::runtime_error("PNG: bad filter type");
            }
            row[i] = static_cast<unsigned char>(v & 0xFF);
        }
    }

    // Expand to RGBA. PNG stores 16-bit samples most-significant byte first.
    // Filtering has already reconstructed both bytes, so sampleAt combines them
    // before the value is rounded down to the engine's 8-bit texture format.
    auto sampleAt = [&](const unsigned char* row, int x, int ch) -> int {
        if (bitDepth == 8) return row[static_cast<std::size_t>(x) * channels + ch];
        if (bitDepth == 16) {
            const std::size_t offset = (static_cast<std::size_t>(x) * channels + ch) * 2;
            return (static_cast<int>(row[offset]) << 8) | row[offset + 1];
        }
        const int bitPos = x * bitDepth;                 // channels == 1 when packed
        const int byte   = row[bitPos / 8];
        const int shift  = 8 - bitDepth - (bitPos % 8);
        return (byte >> shift) & ((1 << bitDepth) - 1);
    };
    const int maxVal = (1 << bitDepth) - 1;
    auto toByte = [&](int sample) -> unsigned char {
        if (bitDepth == 8) return static_cast<unsigned char>(sample);
        return static_cast<unsigned char>((sample * 255 + maxVal / 2) / maxVal);
    };

    Image img;
    img.width  = width;
    img.height = height;
    img.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        const unsigned char* row = &recon[static_cast<std::size_t>(y) * stride];
        for (int x = 0; x < width; ++x) {
            unsigned char* o = &img.rgba[(static_cast<std::size_t>(y) * width + x) * 4];
            unsigned char r, g, b, a = 255;
            switch (colorType) {
            case 0: {   // grayscale (any supported depth)
                const int s0 = sampleAt(row, x, 0);
                r = g = b = toByte(s0);
                break;
            }
            case 2: {   // RGB (8/16-bit)
                r = toByte(sampleAt(row, x, 0));
                g = toByte(sampleAt(row, x, 1));
                b = toByte(sampleAt(row, x, 2));
                break;
            }
            case 4: {   // grayscale + alpha (8/16-bit)
                r = g = b = toByte(sampleAt(row, x, 0));
                a = toByte(sampleAt(row, x, 1));
                break;
            }
            case 6: {   // RGBA (8/16-bit)
                r = toByte(sampleAt(row, x, 0)); g = toByte(sampleAt(row, x, 1));
                b = toByte(sampleAt(row, x, 2)); a = toByte(sampleAt(row, x, 3));
                break;
            }
            case 3: {   // palette index (any supported depth)
                const std::size_t idx = static_cast<std::size_t>(sampleAt(row, x, 0));
                if (idx * 3 + 2 >= palette.size()) throw std::runtime_error("PNG: palette index out of range");
                r = palette[idx * 3]; g = palette[idx * 3 + 1]; b = palette[idx * 3 + 2];
                a = (idx < trns.size()) ? trns[idx] : 255;
                break;
            }
            default: r = g = b = 0; break;
            }
            o[0] = r; o[1] = g; o[2] = b; o[3] = a;
        }
    }
    return img;
}

Image DecodePNG(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("PNG: cannot open '" + path + "'");
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),
                                            std::istreambuf_iterator<char>());
    return DecodePNGFromMemeory(bytes.data(), bytes.size());
}

} // namespace engine::image
