#include <engine/graphics/ImageDecode.h>

#include <cstdlib>
#include <iostream>

namespace {

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

} // namespace

int main() {
    // 2x2 grayscale PNG16 containing 0, 1, 32768, 65535. The first two values
    // intentionally collapse to the same RGBA8 byte but must remain distinct in
    // luminance16 for terrain heightfield import.
    static const unsigned char png[] = {
        0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,
        0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x02,0x10,0x00,0x00,0x00,0x00,0x07,0x4D,0x8E,
        0xBB,0x00,0x00,0x00,0x12,0x49,0x44,0x41,0x54,0x78,0x9C,0x63,0x60,0x60,0x60,0x60,
        0x64,0x68,0x60,0xF8,0xFF,0x1F,0x00,0x05,0x0D,0x02,0x80,0x01,0x70,0xCA,0x8C,0x00,
        0x00,0x00,0x00,0x49,0x45,0x4E,0x44,0xAE,0x42,0x60,0x82
    };
    const engine::image::Image image =
        engine::image::DecodePNGFromMemeory(png, sizeof(png));
    Check(image.width == 2 && image.height == 2, "PNG16 dimensions");
    Check(image.sourceBitDepth == 16, "PNG16 source depth");
    Check(image.luminance16.size() == 4, "PNG16 luminance payload");
    Check(image.luminance16[0] == 0 && image.luminance16[1] == 1,
          "PNG16 low-value precision");
    Check(image.luminance16[2] == 32768 && image.luminance16[3] == 65535,
          "PNG16 full-range precision");
    Check(image.rgba[0] == 0 && image.rgba[4] == 0,
          "texture payload remains RGBA8");
    std::cout << "image decode tests passed\n";
    return 0;
}
