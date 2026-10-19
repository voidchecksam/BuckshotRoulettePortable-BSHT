#include "assets/dxt1.h"

namespace assets {
namespace {

uint16_t To565(uint8_t r, uint8_t g, uint8_t b) {
    // Rounded, not truncated: the nearest representable level.
    const unsigned r5 = (static_cast<unsigned>(r) * 31u + 127u) / 255u;
    const unsigned g6 = (static_cast<unsigned>(g) * 63u + 127u) / 255u;
    const unsigned b5 = (static_cast<unsigned>(b) * 31u + 127u) / 255u;
    return static_cast<uint16_t>((r5 << 11) | (g6 << 5) | b5);
}

// Bit replication, as the hardware expands 5 and 6 bits to 8.
void From565(uint16_t c, uint8_t out[3]) {
    const unsigned r5 = (c >> 11) & 31u;
    const unsigned g6 = (c >> 5) & 63u;
    const unsigned b5 = c & 31u;
    out[0] = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
    out[1] = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
    out[2] = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
}

}  // namespace

Dxt1Block EncodeDxt1Solid(uint8_t r, uint8_t g, uint8_t b) {
    const uint16_t c = To565(r, g, b);
    // c0 == c1 is three-colour mode, where index 0 is still c0: all-zero
    // indices give a solid block in either mode.
    return Dxt1Block{static_cast<uint8_t>(c & 0xFF), static_cast<uint8_t>(c >> 8),
                     static_cast<uint8_t>(c & 0xFF), static_cast<uint8_t>(c >> 8),
                     0, 0, 0, 0};
}

void DecodeDxt1Block(const uint8_t block[8], uint8_t outRgba[16 * 4]) {
    const uint16_t c0 = static_cast<uint16_t>(block[0] | (block[1] << 8));
    const uint16_t c1 = static_cast<uint16_t>(block[2] | (block[3] << 8));
    uint8_t palette[4][4];
    From565(c0, palette[0]);
    From565(c1, palette[1]);
    palette[0][3] = palette[1][3] = 255;
    for (int ch = 0; ch < 3; ++ch) {
        const int a = palette[0][ch];
        const int b = palette[1][ch];
        if (c0 > c1) {
            palette[2][ch] = static_cast<uint8_t>((2 * a + b) / 3);
            palette[3][ch] = static_cast<uint8_t>((a + 2 * b) / 3);
        } else {
            palette[2][ch] = static_cast<uint8_t>((a + b) / 2);
            palette[3][ch] = 0;
        }
    }
    palette[2][3] = 255;
    palette[3][3] = c0 > c1 ? 255 : 0;

    for (int i = 0; i < 16; ++i) {
        const int index = (block[4 + i / 4] >> ((i % 4) * 2)) & 3;
        for (int ch = 0; ch < 4; ++ch) outRgba[i * 4 + ch] = palette[index][ch];
    }
}


}  // namespace assets
