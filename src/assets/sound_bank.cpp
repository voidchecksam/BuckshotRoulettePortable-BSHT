#include "assets/sound_bank.h"

#include "core/ima_adpcm.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace assets {

namespace {

constexpr char kMagic[4] = {'B', 'S', 'S', 'B'};

struct FileHeader {
    char     magic[4];
    uint32_t version;
    uint32_t reserved0;     // v1/v2 held the bank's one rate and channel count
    uint32_t reserved1;
    uint32_t soundCount;
    uint32_t totalBytes;    // the ADPCM pool
};

struct ChannelRecord {
    uint32_t offset;        // bytes into the pool
    int16_t  predictor;     // ADPCM start state
    uint8_t  stepIndex;
    uint8_t  pad;
};

struct SoundRecord {
    uint32_t frames;        // per channel
    uint32_t rate;
    uint16_t channels;      // 1 or 2
    uint16_t flags;         // kFlagLinear
    ChannelRecord channel[2];
    uint32_t nameLength;
};

constexpr uint16_t kFlagLinear = 1;   // a .wav: linear interpolation

static_assert(sizeof(FileHeader) == 24, "sound bank header layout changed");
static_assert(sizeof(SoundRecord) == 32, "sound record layout changed");

}  // namespace

const char* SoundBankErrorText(SoundBankError e) {
    switch (e) {
    case SoundBankError::None:         return "ok";
    case SoundBankError::FileNotFound: return "file not found";
    case SoundBankError::TooSmall:     return "file smaller than a header";
    case SoundBankError::BadMagic:     return "not a sound bank";
    case SoundBankError::BadVersion:   return "wrong bank version; re-bake";
    case SoundBankError::Truncated:    return "file ends mid-record";
    case SoundBankError::Inconsistent: return "header counts disagree with contents";
    }
    return "unknown error";
}

bool IsAudiblyStereo(const std::vector<int16_t>& left, const std::vector<int16_t>& right) {
    const std::size_t n = left.size() < right.size() ? left.size() : right.size();
    double mid = 0.0, side = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double m = (static_cast<double>(left[i]) + right[i]) * 0.5;
        const double d = (static_cast<double>(left[i]) - right[i]) * 0.5;
        mid += m * m;
        side += d * d;
    }
    if (side <= 0.0) return false;
    if (mid <= 0.0) return true;
    return 10.0 * std::log10(side / mid) > kStereoSideThresholdDb;
}

void SoundBank::AddForBake(const std::string& name, const std::vector<int16_t>& left,
                           const std::vector<int16_t>& right, uint32_t rate, bool linear) {
    Sound s;
    s.name = name;
    s.frames = static_cast<uint32_t>(left.size());
    s.rate = rate;
    s.channels = right.empty() ? 1 : 2;
    s.linear = linear;
    for (int c = 0; c < s.channels; ++c) {
        const std::vector<int16_t>& pcm = c == 0 ? left : right;
        const core::AdpcmEncoded e = core::EncodeImaAdpcm(pcm.data(), s.frames);
        s.channel[c].offset = static_cast<uint32_t>(data_.size());
        s.channel[c].predictor = e.predictor;
        s.channel[c].stepIndex = e.stepIndex;
        data_.insert(data_.end(), e.bytes.begin(), e.bytes.end());
    }
    sounds_.push_back(std::move(s));
}

int SoundBank::Find(const char* name) const {
    if (!name) return -1;
    for (std::size_t i = 0; i < sounds_.size(); ++i) {
        if (sounds_[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

bool SoundBank::WriteToFile(const char* path) const {
    if (!path || sounds_.empty()) return false;

    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;

    FileHeader h{};
    std::memcpy(h.magic, kMagic, 4);
    h.version = kSoundBankVersion;
    h.soundCount = static_cast<uint32_t>(sounds_.size());
    h.totalBytes = static_cast<uint32_t>(data_.size());
    std::fwrite(&h, sizeof(h), 1, f);

    for (const Sound& s : sounds_) {
        SoundRecord r{};
        r.frames = s.frames;
        r.rate = s.rate;
        r.channels = static_cast<uint16_t>(s.channels);
        r.flags = s.linear ? kFlagLinear : 0;
        for (int c = 0; c < 2; ++c) {
            r.channel[c].offset = s.channel[c].offset;
            r.channel[c].predictor = s.channel[c].predictor;
            r.channel[c].stepIndex = s.channel[c].stepIndex;
        }
        r.nameLength = static_cast<uint32_t>(s.name.size());
        std::fwrite(&r, sizeof(r), 1, f);
        std::fwrite(s.name.data(), 1, s.name.size(), f);
    }

    std::fwrite(data_.data(), 1, data_.size(), f);

    const bool ok = std::ferror(f) == 0;
    std::fclose(f);
    return ok;
}

bool SoundBank::LoadFromFile(const char* path) {
    Clear();
    std::FILE* f = path ? std::fopen(path, "rb") : nullptr;
    if (!f) {
        lastError_ = SoundBankError::FileNotFound;
        return false;
    }
    // Every count in the file is checked against its size before anything is
    // sized by it: a corrupt bank must be refused, not throw or read past.
    const auto fail = [this, f](SoundBankError e) {
        std::fclose(f);
        Clear();
        lastError_ = e;
        return false;
    };
    std::fseek(f, 0, SEEK_END);
    const long sizeLong = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    const uint64_t fileSize = sizeLong > 0 ? static_cast<uint64_t>(sizeLong) : 0;

    FileHeader h{};
    if (std::fread(&h, sizeof(h), 1, f) != 1) return fail(SoundBankError::TooSmall);
    if (std::memcmp(h.magic, kMagic, 4) != 0) return fail(SoundBankError::BadMagic);
    // Rejected, not reinterpreted: a stale bank would play the wrong sounds at
    // the wrong offsets, which is far harder to diagnose than silence.
    if (h.version != kSoundBankVersion) return fail(SoundBankError::BadVersion);
    if (static_cast<uint64_t>(h.soundCount) * sizeof(SoundRecord) > fileSize ||
        static_cast<uint64_t>(h.totalBytes) > fileSize) {
        return fail(SoundBankError::Inconsistent);
    }

    sounds_.reserve(h.soundCount);
    for (uint32_t i = 0; i < h.soundCount; ++i) {
        SoundRecord r{};
        if (std::fread(&r, sizeof(r), 1, f) != 1) return fail(SoundBankError::Truncated);
        // A channel count other than 1 or 2 is refused, never read as mono.
        if ((r.channels != 1 && r.channels != 2) || r.rate == 0 || r.nameLength > fileSize) {
            return fail(SoundBankError::Inconsistent);
        }
        Sound s;
        s.frames = r.frames;
        s.rate = r.rate;
        s.channels = r.channels;
        s.linear = (r.flags & kFlagLinear) != 0;
        for (int c = 0; c < 2; ++c) {
            s.channel[c].offset = r.channel[c].offset;
            s.channel[c].predictor = r.channel[c].predictor;
            s.channel[c].stepIndex = r.channel[c].stepIndex > 88 ? 88 : r.channel[c].stepIndex;
        }
        s.name.resize(r.nameLength);
        if (r.nameLength && std::fread(&s.name[0], 1, r.nameLength, f) != r.nameLength) {
            return fail(SoundBankError::Truncated);
        }
        sounds_.push_back(std::move(s));
    }

    data_.resize(h.totalBytes);
    if (h.totalBytes && std::fread(data_.data(), 1, h.totalBytes, f) != h.totalBytes) {
        return fail(SoundBankError::Truncated);
    }

    // Every sound must sit inside the pool, checked in 64 bits so no frame
    // count wraps. A bad offset would read past the buffer on the first play,
    // on a device with no debugger.
    for (const Sound& snd : sounds_) {
        for (int c = 0; c < snd.channels; ++c) {
            if (static_cast<uint64_t>(snd.channel[c].offset) + (static_cast<uint64_t>(snd.frames) + 1) / 2 >
                data_.size()) {
                return fail(SoundBankError::Inconsistent);
            }
        }
    }
    std::fclose(f);
    lastError_ = SoundBankError::None;
    return true;
}

}  // namespace assets
