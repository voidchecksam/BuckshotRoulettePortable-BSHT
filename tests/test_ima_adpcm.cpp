// IMA ADPCM, the sound bank's sample format: 4 bits a sample at the source's
// rate. A 16-bit bank at 22.05 kHz cut every SFX above 11 kHz.
#include "core/ima_adpcm.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

std::vector<int16_t> Sine(int n, double hz, double rate, double amp) {
    std::vector<int16_t> s(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        s[static_cast<std::size_t>(i)] = static_cast<int16_t>(amp * std::sin(2.0 * 3.14159265358979 * hz * i / rate));
    }
    return s;
}

double SnrDb(const std::vector<int16_t>& a, const std::vector<int16_t>& b) {
    double sig = 0.0, err = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        sig += static_cast<double>(a[i]) * a[i];
        const double d = static_cast<double>(a[i]) - b[i];
        err += d * d;
    }
    return 10.0 * std::log10(sig / (err > 0.0 ? err : 1e-9));
}

}  // namespace

TEST(ImaAdpcm, TwoSamplesPackIntoOneByte) {
    const std::vector<int16_t> pcm = Sine(1001, 440.0, 44100.0, 12000.0);
    const core::AdpcmEncoded e = core::EncodeImaAdpcm(pcm.data(), pcm.size());
    EXPECT_EQ(e.bytes.size(), (pcm.size() + 1) / 2);
}

TEST(ImaAdpcm, DecodingRestoresAToneAtUsableQuality) {
    // A 6 kHz tone: high enough that the old 22 kHz bank barely held it.
    const std::vector<int16_t> pcm = Sine(44100, 6000.0, 44100.0, 12000.0);
    const core::AdpcmEncoded e = core::EncodeImaAdpcm(pcm.data(), pcm.size());
    core::AdpcmDecoder d(e.bytes.data(), e.predictor, e.stepIndex);
    std::vector<int16_t> out(pcm.size());
    for (int16_t& s : out) s = d.Next();
    EXPECT_GT(SnrDb(pcm, out), 20.0);
}

TEST(ImaAdpcm, FullScaleSquareDoesNotWrap) {
    std::vector<int16_t> pcm(2000);
    for (std::size_t i = 0; i < pcm.size(); ++i) pcm[i] = (i / 50) % 2 ? 32767 : -32768;
    const core::AdpcmEncoded e = core::EncodeImaAdpcm(pcm.data(), pcm.size());
    core::AdpcmDecoder d(e.bytes.data(), e.predictor, e.stepIndex);
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        const int s = d.Next();
        ASSERT_GE(s, -32768);
        ASSERT_LE(s, 32767);
        // Once settled, the decoded wave keeps the input's sign (no wrap-around).
        if (i % 50 > 25) EXPECT_EQ(s > 0, pcm[i] > 0) << i;
    }
}
