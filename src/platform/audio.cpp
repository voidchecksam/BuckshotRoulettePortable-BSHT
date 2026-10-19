#include "platform/audio.h"

#include "core/bus_clamp.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

extern "C" {
#include "stb_vorbis.h"
}

namespace platform {

namespace {


}  // namespace

Audio::~Audio() { Close(); }

void SDLCALL Audio::Callback(void* userdata, uint8_t* stream, int len) {
    Audio* self = static_cast<Audio*>(userdata);
    const int frames = len / (kOutputChannels * static_cast<int>(sizeof(int16_t)));
    self->Mix(reinterpret_cast<int16_t*>(stream), frames);
}

bool Audio::Open(const assets::SoundBank* bank) {
    Close();
    bank_ = bank;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::printf("audio: SDL_InitSubSystem failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_AudioSpec want;
    std::memset(&want, 0, sizeof(want));
    want.freq = kOutputRate;
    want.format = AUDIO_S16SYS;
    want.channels = static_cast<Uint8>(kOutputChannels);
    // 1024 frames is ~21 ms at 48 kHz: short enough that a gunshot is not
    // audibly late, long enough that the callback is not thrashing.
    want.samples = 1024;
    want.callback = &Audio::Callback;
    want.userdata = this;

    SDL_AudioSpec have;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (device_ == 0) {
        // A silent game is better than a crashed one, so this is reported and
        // then every later call becomes a no-op.
        std::printf("audio: could not open device: %s\n", SDL_GetError());
        return false;
    }

    std::printf("audio: %d Hz, %d ch, %d-frame buffer\n",
                have.freq, have.channels, have.samples);
    // Every callback renders streams into this; sized once, never in the callback.
    streamScratch_.assign(static_cast<std::size_t>(have.samples > 0 ? have.samples : 1024) * 4 * 2, 0.0f);
    bus_.assign(static_cast<std::size_t>(have.samples > 0 ? have.samples : 1024) * 4 * kOutputChannels, 0);
    SDL_PauseAudioDevice(device_, 0);
    StartLoader();
    return true;
}

void Audio::Close() {
    // Voices first, while loaderLock_ exists: StopStream cancels a queued or
    // in-flight load, which a later re-Open must never install.
    for (int i = 0; i < kStreamVoices; ++i) StopStream(i);
    StopLoader();
    if (device_) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
    for (Voice& v : voices_) v.Stop();
}

namespace {
// A handle is the slot plus kMaxVoices times its generation, so a stale handle
// (its voice ended and was reused) never matches.
int VoiceHandle(int slot, uint32_t generation) {
    return slot + kMaxVoices * static_cast<int>(generation % (0x7FFFFFFFu / kMaxVoices));
}
}  // namespace

int Audio::PlaySound(const SoundPlay& play) {
    if (!device_ || !bank_ || !play.name) return -1;

    const int index = bank_->Find(play.name);
    if (index < 0) {
        // Counted and named once. Silence is indistinguishable from a bug
        // anywhere else in the audio path, so the miss has to say so itself.
        ++missingCues_;
        const std::string key(play.name);
        if (std::find(missingNames_.begin(), missingNames_.end(), key) == missingNames_.end()) {
            missingNames_.push_back(key);
        }
        return -1;
    }

    const assets::Sound& s = bank_->At(index);
    core::SfxStart start;
    const auto run = [this, &s](int c) {
        return core::AdpcmChannel{bank_->Data() + s.channel[c].offset, s.channel[c].predictor,
                                  s.channel[c].stepIndex};
    };
    start.left = run(0);
    if (s.channels == 2) start.right = run(1);
    start.frames = s.frames;
    // The sound's own rate (44.1 or 48 kHz) to the device's, at its pitch.
    start.step = static_cast<double>(s.rate) / static_cast<double>(kOutputRate);
    start.pitch = play.pitch;
    start.gainL = play.gainL;
    start.gainR = play.gainR;
    const auto frameAt = [&s](float seconds) {
        const double f = static_cast<double>(seconds) * s.rate;
        return f <= 0.0 ? 0u : (f >= s.frames ? s.frames : static_cast<uint32_t>(f));
    };
    start.startFrame = frameAt(play.fromSeconds);
    if (play.endOffsetSeconds > 0.0f) start.endFrame = s.frames - frameAt(play.endOffsetSeconds);
    start.rampIn = play.rampIn;
    start.linear = s.linear;
    start.positional = play.positional;
    start.shelf = play.shelf;
    start.shelfCutoffHz = play.shelfCutoffHz;
    start.deviceRate = static_cast<float>(kOutputRate);

    int handle = -1;
    SDL_LockAudioDevice(device_);
    for (int i = 0; i < kMaxVoices; ++i) {
        if (voices_[i].Active()) continue;
        voices_[i].Start(start);
        handle = VoiceHandle(i, ++voiceGeneration_[i]);
        // A post left for the slot's previous voice is not this one's.
        voiceUpdates_[i].handle.store(-1, std::memory_order_relaxed);
        voiceUpdates_[i].stop.store(-1, std::memory_order_relaxed);
        break;
    }
    if (handle >= 0) ++voicesStarted_;
    else ++voicesDropped_;
    // No free voice: the sound is dropped rather than stealing an in-flight
    // one. Cutting off a gunshot to start another is more noticeable than
    // missing the quieter of two overlapping cues.
    SDL_UnlockAudioDevice(device_);
    return handle;
}

void Audio::UpdateVoice(int handle, float gainL, float gainR, float pitch, float shelf) {
    if (!device_ || handle < 0) return;
    const int slot = handle % kMaxVoices;
    // Only this thread claims slots (PlaySound), so the generation it reads is
    // current: an update for a voice whose slot was reused would overwrite the
    // new voice's post, and is dropped here instead.
    if (VoiceHandle(slot, voiceGeneration_[slot]) != handle) {
        ++staleHandles_;
        return;
    }
    VoiceUpdate& u = voiceUpdates_[slot];
    u.gainL.store(gainL, std::memory_order_relaxed);
    u.gainR.store(gainR, std::memory_order_relaxed);
    u.pitch.store(pitch, std::memory_order_relaxed);
    u.shelf.store(shelf, std::memory_order_relaxed);
    u.handle.store(handle, std::memory_order_release);
}

void Audio::StopAllVoices() {
    if (!device_) return;
    for (int i = 0; i < kMaxVoices; ++i) {
        voiceUpdates_[i].stop.store(VoiceHandle(i, voiceGeneration_[i]), std::memory_order_release);
    }
}

void Audio::StopVoice(int handle) {
    if (!device_ || handle < 0) return;
    const int slot = handle % kMaxVoices;
    if (VoiceHandle(slot, voiceGeneration_[slot]) != handle) {
        ++staleHandles_;
        return;
    }
    ++voicesStopped_;
    voiceUpdates_[slot].stop.store(handle, std::memory_order_release);
}

bool Audio::PlayStream(int slot, const char* path, bool loop, float gainL, float gainR, float pitch,
                       bool musicBus) {
    if (!device_ || slot < 0 || slot >= kStreamVoices) return false;
    StopStream(slot);
    if (slot == 0) {
    }
    if (!path || !*path) return false;
    // Only whether it exists, here: opening a file is cheap, reading 3 MB is not.
    std::FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::printf("audio: stream not found: %s\n", path);
        return false;
    }
    std::fclose(f);
    if (!loader_) return false;

    SDL_LockAudioDevice(device_);
    streams_[slot].gain.Reset(gainL, gainR);
    streams_[slot].musicBus = slot == 0 || musicBus;
    streams_[slot].lowPassL.Clear();
    streams_[slot].lowPassR.Clear();
    streams_[slot].targetL.store(gainL, std::memory_order_relaxed);
    streams_[slot].targetR.store(gainR, std::memory_order_relaxed);
    SDL_UnlockAudioDevice(device_);
    SDL_LockMutex(loaderLock_);
    requests_.Post(slot, StreamJob{path, loop, pitch});
    SDL_CondSignal(loaderWake_);
    SDL_UnlockMutex(loaderLock_);
    return true;
}

void Audio::StartLoader() {
    loaderLock_ = SDL_CreateMutex();
    loaderWake_ = SDL_CreateCond();
    loaderQuit_ = false;
    // 256 KB: stb_vorbis allocates on the heap, but the Vita's default thread
    // stack is small.
    if (loaderLock_ && loaderWake_) {
        loader_ = SDL_CreateThreadWithStackSize(&Audio::LoaderMain, "bsht stream loader", 256 * 1024, this);
    }
    if (!loader_) std::printf("audio: no stream loader thread (%s) -- streams are off\n", SDL_GetError());
}

void Audio::StopLoader() {
    if (loader_) {
        SDL_LockMutex(loaderLock_);
        loaderQuit_ = true;
        SDL_CondSignal(loaderWake_);
        SDL_UnlockMutex(loaderLock_);
        SDL_WaitThread(loader_, nullptr);
        loader_ = nullptr;
    }
    if (loaderWake_) SDL_DestroyCond(loaderWake_);
    if (loaderLock_) SDL_DestroyMutex(loaderLock_);
    loaderWake_ = nullptr;
    loaderLock_ = nullptr;
}

void Audio::RecordFailure(const std::string& path) {
    SDL_LockMutex(loaderLock_);
    loads_.Failed(path);
    SDL_UnlockMutex(loaderLock_);
}

std::string Audio::LoaderSummary() const {
    if (!loaderLock_) return loader_ ? std::string() : std::string("streams: no loader thread");
    SDL_LockMutex(loaderLock_);
    std::string s = loads_.Format();
    SDL_UnlockMutex(loaderLock_);
    return s;
}

std::string Audio::Summary() const {
    return "voices: " + std::to_string(voicesStarted_) + " started, " + std::to_string(voicesDropped_) +
           " dropped (none free), " + std::to_string(voicesStopped_) + " stopped, " + std::to_string(staleHandles_) +
           " stale | clipped " + std::to_string(ClippedSamples()) + " samples | " + LoaderSummary();
}

int SDLCALL Audio::LoaderMain(void* self) {
    Audio* a = static_cast<Audio*>(self);
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
    SDL_LockMutex(a->loaderLock_);
    for (;;) {
        StreamRequests::Request r;
        while (!a->loaderQuit_ && !a->requests_.Take(r)) SDL_CondWait(a->loaderWake_, a->loaderLock_);
        if (a->loaderQuit_) break;
        SDL_UnlockMutex(a->loaderLock_);
        a->LoadStream(r);
        SDL_LockMutex(a->loaderLock_);
    }
    SDL_UnlockMutex(a->loaderLock_);
    return 0;
}

void Audio::LoadStream(const StreamRequests::Request& request) {
    const StreamJob& job = request.payload;
    const uint64_t t0 = SDL_GetPerformanceCounter();
    std::FILE* f = std::fopen(job.path.c_str(), "rb");
    if (!f) {
        std::printf("audio: stream not found: %s\n", job.path.c_str());
        RecordFailure(job.path);
        return;
    }
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        std::fclose(f);
        RecordFailure(job.path);
        return;
    }
    std::vector<uint8_t> data(static_cast<std::size_t>(n));
    const std::size_t got = std::fread(data.data(), 1, data.size(), f);
    std::fclose(f);
    data.resize(got);

    // The COMPRESSED file is held in memory (a few MB) and decoded a chunk at a
    // time. Decoding it whole would cost tens of MB for a single track. The
    // decoder points into `data`; moving the vector keeps its buffer.
    int err = 0;
    stb_vorbis* v = stb_vorbis_open_memory(data.data(), static_cast<int>(data.size()), &err, nullptr);
    if (!v) {
        std::printf("audio: could not open %s (stb_vorbis error %d)\n", job.path.c_str(), err);
        RecordFailure(job.path);
        return;
    }
    const stb_vorbis_info info = stb_vorbis_get_info(v);
    const double ms = static_cast<double>(SDL_GetPerformanceCounter() - t0) * 1000.0 /
                      static_cast<double>(SDL_GetPerformanceFrequency());
    const double mb = static_cast<double>(data.size()) / (1024.0 * 1024.0);

    SDL_LockMutex(loaderLock_);
    const bool current = requests_.IsCurrent(request);
    if (current) {
        SDL_LockAudioDevice(device_);
        Stream& s = streams_[request.slot];
        CloseStream(s);
        s.data = std::move(data);
        s.vorbis = v;
        s.channels = info.channels;
        s.loop = job.loop;
        s.played = 0;
        s.resampler.Reset(info.channels, static_cast<double>(info.sample_rate) / static_cast<double>(kOutputRate) *
                                             (job.pitch > 0.01f ? job.pitch : 0.01f));
        SDL_UnlockAudioDevice(device_);
        loads_.Loaded(ms);
    } else {
        loads_.Dropped();
    }
    SDL_UnlockMutex(loaderLock_);
    if (!current) {
        stb_vorbis_close(v);   // superseded or stopped while it loaded
        std::printf("audio: stream %d %s dropped (superseded while loading)\n", request.slot, job.path.c_str());
        return;
    }
    std::printf("audio: stream %d %s (%d ch, %d Hz, %.1f MB compressed, loaded in %.0f ms off the frame)\n",
                request.slot, job.path.c_str(), info.channels, static_cast<int>(info.sample_rate), mb, ms);
}

void Audio::CloseStream(Stream& s) {
    if (s.vorbis) stb_vorbis_close(s.vorbis);
    s.vorbis = nullptr;
    s.data.clear();
}

void Audio::StopStream(int slot) {
    if (slot < 0 || slot >= kStreamVoices) return;
    if (loaderLock_) {
        // A load in flight for this voice is now stale: it is not installed.
        SDL_LockMutex(loaderLock_);
        requests_.Cancel(slot);
        SDL_UnlockMutex(loaderLock_);
    }
    if (!streams_[slot].vorbis) return;
    if (device_) SDL_LockAudioDevice(device_);
    CloseStream(streams_[slot]);
    if (device_) SDL_UnlockAudioDevice(device_);
}

void Audio::SetStreamGain(int slot, float gainL, float gainR) {
    if (slot < 0 || slot >= kStreamVoices) return;
    streams_[slot].targetL.store(gainL, std::memory_order_relaxed);
    streams_[slot].targetR.store(gainR, std::memory_order_relaxed);
}

// Runs on the audio thread, under SDL's callback lock.
void Audio::MixStream(int slot, int32_t* out, int frames) {
    Stream& s = streams_[slot];
    if (!s.vorbis) return;
    if (static_cast<int>(streamScratch_.size()) < frames * 2) return;   // never allocate here
    stb_vorbis* v = s.vorbis;
    const int channels = s.channels;
    const bool loop = s.loop;
    const int rendered = s.resampler.Render(
        frames,
        [v, channels, loop](int16_t* dst, int maxFrames) {
            int got = stb_vorbis_get_samples_short_interleaved(v, channels, dst, maxFrames * channels);
            if (got <= 0 && loop) {
                // Rewind and keep reading so the loop has no gap.
                stb_vorbis_seek_start(v);
                got = stb_vorbis_get_samples_short_interleaved(v, channels, dst, maxFrames * channels);
            }
            return got;
        },
        streamScratch_.data());
    // The game thread's latest target, read once per buffer.
    s.gain.SetTarget(s.targetL.load(std::memory_order_relaxed), s.targetR.load(std::memory_order_relaxed));
    const bool filter = s.musicBus && musicCutoffHz_ > 0.0f;
    for (int i = 0; i < rendered; ++i) {
        float l = streamScratch_[static_cast<std::size_t>(i) * 2];
        float r = streamScratch_[static_cast<std::size_t>(i) * 2 + 1];
        // The music bus's low-pass. Inactive by default, and then this is a
        // branch and nothing else.
        if (filter) {
            l = s.lowPassL.Process(l);
            r = s.lowPassR.Process(r);
        }
        float gl = 1.0f, gr = 1.0f;
        s.gain.Next(gl, gr);
        const float ramp = core::StartRamp(s.played);
        if (s.played < core::kGodotMixBuffer) ++s.played;   // saturates (a uint32 wraps in 24.8 h)
        out[i * 2 + 0] += static_cast<int32_t>(l * gl * ramp);
        out[i * 2 + 1] += static_cast<int32_t>(r * gr * ramp);
    }
    if (rendered < frames) CloseStream(s);   // a one-shot played out
}

void Audio::Mix(int16_t* out, int frames) {
    const Uint64 start = SDL_GetPerformanceCounter();
    const int n = frames * kOutputChannels;
    if (static_cast<int>(bus_.size()) < n) {   // never allocate here
        std::memset(out, 0, static_cast<std::size_t>(n) * sizeof(int16_t));
        return;
    }
    std::fill(bus_.begin(), bus_.begin() + n, 0);

    // Streams first (music, then the rest), then the banked effects.
    // The music bus's low-pass, at the cutoff the game last posted: every
    // stream on the bus takes the coefficients (a filter is linear, so each
    // stream through its own copy is the bus's sum through one).
    const float hz = musicCutoffTarget_.load(std::memory_order_relaxed);
    if (hz != musicCutoffHz_) {
        const bool reenabled = musicCutoffHz_ <= 0.0f && hz > 0.0f;
        musicCutoffHz_ = hz;
        if (hz > 0.0f) {
            const core::BiquadCoeffs c = core::GodotLowPass(hz, 0.5f, static_cast<float>(kOutputRate));
            for (Stream& st : streams_) {
                st.lowPassL.Set(c);
                st.lowPassR.Set(c);
                if (reenabled) {   // no stale history from before it was off
                    st.lowPassL.Clear();
                    st.lowPassR.Clear();
                }
            }
        }
    }
    for (int i = 0; i < kStreamVoices; ++i) MixStream(i, bus_.data(), frames);
    for (int i = 0; i < kMaxVoices; ++i) {
        Voice& v = voices_[i];
        // A live gain/pitch the game thread posted for THIS voice (not a slot
        // reused since): applied from this buffer on.
        const int current = VoiceHandle(i, voiceGeneration_[i]);
        if (voiceUpdates_[i].stop.exchange(-1, std::memory_order_acquire) == current && v.Active()) v.Release();
        const int posted = voiceUpdates_[i].handle.exchange(-1, std::memory_order_acquire);
        if (posted >= 0 && posted == current && v.Active()) {
            v.SetTarget(voiceUpdates_[i].gainL.load(std::memory_order_relaxed),
                        voiceUpdates_[i].gainR.load(std::memory_order_relaxed),
                        voiceUpdates_[i].pitch.load(std::memory_order_relaxed),
                        voiceUpdates_[i].shelf.load(std::memory_order_relaxed));
        }
        if (v.Active()) v.Mix(bus_.data(), frames, sfxGain_);
    }

    // Bus 0, Master (AudioServer.set_bus_volume_db(0, ...)), then the one
    // clamp to 16 bits.
    const int clipped = core::ClampBus(bus_.data(), out, n, masterGain_);
    if (clipped > 0) clippedSamples_.fetch_add(static_cast<unsigned>(clipped), std::memory_order_relaxed);

    const Uint64 end = SDL_GetPerformanceCounter();
    lastMixMicros_ = static_cast<double>(end - start) * 1e6 /
                     static_cast<double>(SDL_GetPerformanceFrequency());
    if (lastMixMicros_ > peakMixMicros_) peakMixMicros_ = lastMixMicros_;
}

int Audio::ActiveVoices() const {
    int n = 0;
    for (const Voice& v : voices_) {
        if (v.Active()) ++n;
    }
    return n;
}

}  // namespace platform
