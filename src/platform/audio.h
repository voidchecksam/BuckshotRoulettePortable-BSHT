// Audio output: SFX mixing and streaming music.
//
// SFX come from the resident ADPCM bank, so a gunshot starts on the exact frame
// it is asked for. Music and long sounds stream from .ogg through stb_vorbis,
// decoded a chunk at a time on a loader thread; decoded whole they would not
// fit in the Vita's free RAM.
//
// The mix follows Godot 4.1.4's AudioServer, measured against its own playback
// (tools/compare_cues.py): each player's 2D/3D gain per channel
// (godot::OutputGain), every playback's start ramp and per-buffer gain ramps,
// a 3D player's attenuation shelf, the music bus's low-pass, and Godot's
// interpolation (cubic for an .ogg, linear for a .wav) from each sound's own
// rate (bank v4: 44.1 or 48 kHz, stereo where heard) to the device's 48 kHz
// (core/godot_mix.h, core/godot_filter.h).
//
// Threading: SDL runs Mix() on its own audio thread, under the device lock. The
// game thread takes that lock only to claim a voice (PlaySound) and to install
// or stop a stream; everything per frame (a voice's gain, pitch and shelf, a
// stop, a stream's gain, the music cutoff) is published through atomics the
// callback drains once per buffer, so the frame never waits on a mix. Handles
// carry their slot's generation: a stale one is dropped on the game thread
// (counted), never applied to the voice that reused the slot.
#pragma once

#include "core/sfx_voice.h"

#include <atomic>

#include "core/gain_ramp.h"
#include "core/load_stats.h"
#include "core/godot_filter.h"
#include "core/slot_requests.h"
#include "core/stream_resampler.h"

#include "assets/sound_bank.h"
#include "platform/gl_compat.h"
#include "platform/sound_play.h"

#include <cstdint>
#include <string>
#include <vector>

struct stb_vorbis;

namespace platform {

// The device's own rate, as the hardware probe measured it: 48 kHz stereo.
inline constexpr int kOutputRate     = 48000;
inline constexpr int kOutputChannels = 2;

// Enough for the busiest moment in a round (a shot, a shell eject, hands, the
// health counter and dialogue) with room to spare. An idle voice costs
// nothing; the mixer skips it.
inline constexpr int kMaxVoices = 24;

// Streamed .ogg voices: slot 0 is the music (it alone carries the low-pass),
// the others are every other player of a file the bank does not hold
// (ambience, the CRT boot loop, the death and win themes).
inline constexpr int kStreamVoices = 4;   // = 1 + game::StreamRouter::kVoices

class Audio {
public:
    ~Audio();

    Audio() = default;
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    // `bank` must outlive the Audio. Returns false when the device could not be
    // opened, in which case every later call is a safe no-op: a silent game is
    // better than a crashed one.
    bool Open(const assets::SoundBank* bank);
    void Close();

    bool IsOpen() const { return device_ != 0; }

    // Plays a banked effect. Unknown names are COUNTED, not ignored, so a
    // missing cue reads as "no sound called X" rather than as silence. Returns
    // the voice's handle for StopVoice, or -1 (missing, or no free voice).
    int PlaySound(const SoundPlay& play);
    // Godot's stop of one playback: a fade to 0 across one mix buffer. A
    // handle whose voice has since ended or been reused is ignored.
    void StopVoice(int handle);
    // Lock-free: posted, applied by the callback from its next buffer.
    // Every voice fades out: a scene change frees every player (PREDELETE).
    void StopAllVoices();
    // A playing voice's new gain and pitch, lock-free (as SetStreamGain): the
    // callback applies it from its next buffer. A stale handle is ignored.
    void UpdateVoice(int handle, float gainL, float gainR, float pitch, float shelf);
    // True when the bank holds `name` (anything else streams).
    bool HasSound(const char* name) const { return bank_ && name && bank_->Find(name) >= 0; }

    // Streams an .ogg. Replaces whatever is playing. Empty path stops music.
    bool PlayMusic(const char* path, bool loop, float gainL, float gainR) {
        return PlayStream(0, path, loop, gainL, gainR);
    }
    void StopMusic() { StopStream(0); }

    // Streams an .ogg on voice `slot` (0 .. kStreamVoices-1), replacing what it
    // played. False when the file cannot be opened (logged). The file is read
    // and its decoder opened on the loader thread, so the call returns at once
    // and the stream starts a moment later (reading a 3 MB track on the frame
    // thread froze the Vita for up to 1.3 s). A later PlayStream or StopStream
    // on the same voice drops a load still in flight.
    // `musicBus`: the stream plays on Godot's "music" bus, through the table's
    // low-pass like the music voice (slot 0 always is).
    bool PlayStream(int slot, const char* path, bool loop, float gainL, float gainR, float pitch = 1.0f,
                    bool musicBus = false);
    void StopStream(int slot);
    // Lock-free: the game thread publishes the target, and the
    // callback ramps to it from its next buffer, as Godot reads bus_details
    // once per mix buffer. A torn L/R pair lasts one buffer of the ramp.
    void SetStreamGain(int slot, float gainL, float gainR);

    void SetMusicGain(float gainL, float gainR) { SetStreamGain(0, gainL, gainR); }
    // The music bus's low-pass, as FilterController drives it: 156 Hz muffles
    // the track at the table, wide open is the lobby (IntroManager.gd:306,315).
    // 0 turns it off. Applied to the OUTPUT rate, after resampling.
    // Godot's AudioEffectLowPassFilter (core::GodotLowPass, resonance 0.5, one
    // stage). Lock-free: the callback picks the cutoff up per buffer and
    // computes its own coefficients, so a half-written set is never heard.
    void SetMusicCutoff(float hz) { musicCutoffTarget_.store(hz, std::memory_order_relaxed); }
    void SetSfxGain(float g)   { sfxGain_ = g; }
    // The master bus (Godot's bus 0), linear. 1 = unchanged.
    void SetMasterGain(float g) { masterGain_ = g; }

    // Diagnostics, reported in the hardware log.
    int  ActiveVoices() const;
    int  MissingCues() const { return missingCues_; }
    const std::vector<std::string>& MissingNames() const { return missingNames_; }
    bool MusicPlaying() const { return streams_[0].vorbis != nullptr; }

    // Microseconds spent in the last mix callback. This is the number that
    // decides whether streaming Vorbis on the CPU is affordable, or whether the
    // music has to move to the hardware ATRAC9 decoder.
    double LastMixMicros() const { return lastMixMicros_; }
    // What the stream loader did ("streams: N loaded, ... peak X ms off the
    // frame"), for the device report, which its printf never reaches.
    std::string LoaderSummary() const;
    // "clipped N samples | " + LoaderSummary(): the one audio line the device
    // report and the desktop log print.
    std::string Summary() const;
    unsigned ClippedSamples() const { return clippedSamples_.load(std::memory_order_relaxed); }
    double PeakMixMicros() const { return peakMixMicros_; }

private:
    // A banked effect playing (core::SfxVoice: ADPCM decoded as it plays).
    using Voice = core::SfxVoice;

    // One streamed file: the compressed .ogg in memory, decoded a chunk at a
    // time, resampled to the device rate.
    struct Stream {
        stb_vorbis* vorbis = nullptr;
        std::vector<uint8_t> data;
        core::StreamResampler resampler;
        int channels = 2;
        bool loop = true;
        // Godot's per-channel gain, ramped across a mix buffer on a change
        // (the callback's own), the target the game thread publishes, and the
        // frames played since the stream was installed (core::StartRamp).
        core::GainRamp gain;
        std::atomic<float> targetL{1.0f};
        std::atomic<float> targetR{1.0f};
        uint32_t played = 0;
        bool musicBus = false;          // through the music bus's low-pass
        core::Biquad lowPassL;          // its own history, the bus's coefficients
        core::Biquad lowPassR;
    };

    // What the loader thread needs to open a stream; the gain lives in the
    // Stream itself, so a SetStreamGain during the load is kept.
    struct StreamJob {
        std::string path;
        bool loop = true;
        float pitch = 1.0f;
    };
    using StreamRequests = core::SlotRequests<StreamJob>;

    static void SDLCALL Callback(void* userdata, uint8_t* stream, int len);
    static int SDLCALL LoaderMain(void* self);
    void StartLoader();
    void StopLoader();
    // On the loader thread: reads and opens the file, then installs it if the
    // request is still current. Lock order: loaderLock_, then the device.
    void LoadStream(const StreamRequests::Request& request);
    void RecordFailure(const std::string& path);   // on the loader thread
    void Mix(int16_t* out, int frames);
    // Both add into the 32-bit bus; one clamp happens at the end of Mix, so a
    // loud stack of sounds is never clipped voice by voice.
    void MixStream(int slot, int32_t* bus, int frames);
    void CloseStream(Stream& s);   // caller holds the device lock

    const assets::SoundBank* bank_ = nullptr;
    SDL_AudioDeviceID device_ = 0;

    Voice voices_[kMaxVoices];
    uint32_t voiceGeneration_[kMaxVoices] = {};   // bumped per Start: a handle's reuse check
    // UpdateVoice's mailbox per slot: the values, then the handle they are for
    // (released last, taken by the callback).
    struct VoiceUpdate {
        std::atomic<float> gainL{1.0f};
        std::atomic<float> gainR{1.0f};
        std::atomic<float> pitch{1.0f};
        std::atomic<float> shelf{1.0f};
        std::atomic<int> handle{-1};
        std::atomic<int> stop{-1};      // StopVoice's post
    };
    VoiceUpdate voiceUpdates_[kMaxVoices];
    static_assert(std::atomic<float>::is_always_lock_free && std::atomic<int>::is_always_lock_free,
                  "the audio callback's mailboxes must not take a lock");
    // What the voices did, for the device report (Summary).
    unsigned voicesStarted_ = 0;
    unsigned voicesDropped_ = 0;   // no free voice
    unsigned voicesStopped_ = 0;
    unsigned staleHandles_ = 0;    // an update or stop for a voice that was gone

    Stream streams_[kStreamVoices];
    StreamRequests requests_{kStreamVoices};   // guarded by loaderLock_
    core::LoadStats loads_;                    // guarded by loaderLock_
    SDL_mutex* loaderLock_ = nullptr;
    SDL_cond* loaderWake_ = nullptr;
    SDL_Thread* loader_ = nullptr;
    bool loaderQuit_ = false;                  // guarded by loaderLock_
    std::vector<float> streamScratch_;   // sized in Open, reused every callback
    std::vector<int32_t> bus_;           // the 32-bit mix, sized in Open
    std::atomic<float> musicCutoffTarget_{0.0f};   // written by the game thread
    float musicCutoffHz_ = 0.0f;                   // what the callback applied
    float sfxGain_ = 1.0f;
    std::atomic<float> masterGain_{1.0f};   // set by the game thread, read by the mixer

    int missingCues_ = 0;
    std::vector<std::string> missingNames_;

    std::atomic<unsigned> clippedSamples_{0};   // written by the mixer, read by the game
    double lastMixMicros_ = 0.0;
    double peakMixMicros_ = 0.0;
};

}  // namespace platform
