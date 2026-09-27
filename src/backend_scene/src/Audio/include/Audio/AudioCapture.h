#pragma once
// System-audio spectrum for audio-responsive wallpapers.
//
// Captures the default output's monitor (what is playing — never a
// microphone) through PipeWire and turns it into the 64-band left/right
// spectrum Wallpaper Engine exposes to shaders (g_AudioSpectrum{16,32,64}
// {Left,Right}) and scripts (engine.registerAudioBuffers). One capturer is
// shared by every scene that needs it and stops when the last one lets go.
//
// Built without PipeWire, Acquire() still works and the spectrum stays silent.
#include <array>
#include <memory>

namespace wallpaper::audio
{

constexpr int kSpectrumBands = 64;

struct AudioSpectrum {
    std::array<float, kSpectrumBands> left {};
    std::array<float, kSpectrumBands> right {};
    std::array<float, kSpectrumBands> average {};
    bool                              active { false }; // capture running and receiving data
};

class AudioCapture {
public:
    // Shared instance; the capture thread starts with the first holder.
    static std::shared_ptr<AudioCapture> Acquire();
    static bool                          Available();

    ~AudioCapture();
    AudioCapture(const AudioCapture&)            = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    // Current smoothed spectrum. Recomputed at most every few milliseconds, so
    // several callers per frame share the work.
    const AudioSpectrum& Spectrum();

private:
    AudioCapture();
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// Downmix a 64-band spectrum to 16 or 32 bands (band-averaged), for the
// lower-resolution uniforms and script buffers.
void ResampleBands(const std::array<float, kSpectrumBands>& in, float* out, int bands);

} // namespace wallpaper::audio
