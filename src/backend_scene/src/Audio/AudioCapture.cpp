#include "Audio/AudioCapture.h"
#include "Utils/Logging.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <numbers>
#include <vector>

#ifdef WP_HAVE_PIPEWIRE
    #include <pipewire/pipewire.h>
    #include <spa/param/audio/format-utils.h>
#endif

using namespace wallpaper::audio;

namespace
{
constexpr int    kRate        = 48000;
constexpr int    kChannels    = 2;
constexpr int    kFftSize     = 2048;             // ~43 ms window
constexpr int    kRingFrames  = kFftSize * 4;
constexpr double kMinHz       = 40.0;
constexpr double kMaxHz       = 16000.0;
constexpr double kRefreshSec  = 0.008;            // spectrum recompute interval
constexpr double kAttack      = 0.55;             // per update, towards louder
constexpr double kDecayPerSec = 9.0;              // exponential fall-off
constexpr float  kSilence     = 1e-5f;            // RMS below this = no audio

double NowSec() {
    using namespace std::chrono;
    return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
}

// in-place iterative radix-2 FFT
void Fft(std::vector<std::complex<float>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const float               ang = -2.0f * std::numbers::pi_v<float> / (float)len;
        const std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; k++) {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k]           = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// FFT bin range [lo, hi) for each of the 64 log-spaced bands
struct BandTable {
    std::array<int, kSpectrumBands + 1> edges {};
    BandTable() {
        const double lo = std::log(kMinHz), hi = std::log(kMaxHz);
        for (int i = 0; i <= kSpectrumBands; i++) {
            const double f = std::exp(lo + (hi - lo) * i / kSpectrumBands);
            edges[(size_t)i] = std::clamp((int)std::lround(f * kFftSize / kRate), 1, kFftSize / 2);
        }
    }
};
} // namespace

struct AudioCapture::Impl {
    std::mutex         ring_mtx;
    std::vector<float> ring;       // interleaved stereo
    size_t             ring_pos { 0 };
    size_t             ring_filled { 0 };
    bool               streaming { false };
    int                channels { kChannels };

    AudioSpectrum spectrum;
    double        last_compute { 0.0 };
    BandTable     bands;
    std::vector<std::complex<float>> fft_l, fft_r;
    std::vector<float>               window;

#ifdef WP_HAVE_PIPEWIRE
    pw_thread_loop* loop { nullptr };
    pw_stream*      stream { nullptr };
    spa_hook        listener {};
#endif

    Impl(): ring((size_t)kRingFrames * kChannels, 0.0f), fft_l(kFftSize), fft_r(kFftSize) {
        window.resize(kFftSize);
        for (int i = 0; i < kFftSize; i++)
            window[(size_t)i] =
                0.5f - 0.5f * std::cos(2.0f * std::numbers::pi_v<float> * (float)i / (kFftSize - 1));
    }

    // audio thread
    void push(const float* samples, size_t frames, int nch) {
        std::lock_guard<std::mutex> lk(ring_mtx);
        for (size_t f = 0; f < frames; f++) {
            const float l = samples[f * (size_t)nch];
            const float r = nch > 1 ? samples[f * (size_t)nch + 1] : l;
            ring[ring_pos * kChannels]     = l;
            ring[ring_pos * kChannels + 1] = r;
            ring_pos                       = (ring_pos + 1) % kRingFrames;
        }
        ring_filled = std::min<size_t>(ring_filled + frames, kRingFrames);
        streaming   = true;
    }

    void compute(double dt) {
        // latest kFftSize frames, windowed
        float rms = 0.0f;
        {
            std::lock_guard<std::mutex> lk(ring_mtx);
            spectrum.active = streaming && ring_filled >= (size_t)kFftSize;
            if (! spectrum.active) {
                decay(dt, true);
                return;
            }
            size_t start = (ring_pos + kRingFrames - kFftSize) % kRingFrames;
            for (int i = 0; i < kFftSize; i++) {
                const size_t p = (start + (size_t)i) % kRingFrames;
                const float  l = ring[p * kChannels], r = ring[p * kChannels + 1];
                fft_l[(size_t)i] = { l * window[(size_t)i], 0.0f };
                fft_r[(size_t)i] = { r * window[(size_t)i], 0.0f };
                rms += l * l + r * r;
            }
        }
        rms = std::sqrt(rms / (2.0f * kFftSize));
        if (rms < kSilence) {
            decay(dt, true);
            return;
        }
        Fft(fft_l);
        Fft(fft_r);

        auto bandsOf = [&](const std::vector<std::complex<float>>& x,
                           std::array<float, kSpectrumBands>&      out) {
            for (int b = 0; b < kSpectrumBands; b++) {
                int lo = bands.edges[(size_t)b], hi = std::max(bands.edges[(size_t)b + 1], lo + 1);
                float peak = 0.0f;
                for (int k = lo; k < hi && k < kFftSize / 2; k++)
                    peak = std::max(peak, std::abs(x[(size_t)k]));
                // amplitude of a full-scale sine ≈ 1 (Hann window gain 0.5)
                const float amp = peak * 4.0f / kFftSize;
                // perceptual curve + gentle treble lift, like WE's bars
                float v = std::sqrt(amp) * (1.2f + 0.8f * (float)b / kSpectrumBands);
                v       = std::clamp(v, 0.0f, 1.0f);
                float& cur = out[(size_t)b];
                if (v > cur)
                    cur += (v - cur) * (float)kAttack;
                else
                    cur += (v - cur) * (float)(1.0 - std::exp(-kDecayPerSec * dt));
            }
        };
        bandsOf(fft_l, spectrum.left);
        bandsOf(fft_r, spectrum.right);
        for (int b = 0; b < kSpectrumBands; b++)
            spectrum.average[(size_t)b] = 0.5f * (spectrum.left[(size_t)b] + spectrum.right[(size_t)b]);
    }

    void decay(double dt, bool silent) {
        const float k = (float)(1.0 - std::exp(-kDecayPerSec * dt));
        for (int b = 0; b < kSpectrumBands; b++) {
            spectrum.left[(size_t)b] *= 1.0f - k;
            spectrum.right[(size_t)b] *= 1.0f - k;
            spectrum.average[(size_t)b] = 0.5f * (spectrum.left[(size_t)b] + spectrum.right[(size_t)b]);
        }
        (void)silent;
    }

#ifdef WP_HAVE_PIPEWIRE
    static void onProcess(void* data) {
        auto* self = static_cast<Impl*>(data);
        pw_buffer* b = pw_stream_dequeue_buffer(self->stream);
        if (b == nullptr) return;
        spa_buffer* buf = b->buffer;
        auto*       samples = static_cast<float*>(buf->datas[0].data);
        if (samples != nullptr && buf->datas[0].chunk != nullptr) {
            const int    nch    = std::max(self->channels, 1);
            const size_t frames = buf->datas[0].chunk->size / (sizeof(float) * (size_t)nch);
            self->push(samples, frames, nch);
        }
        pw_stream_queue_buffer(self->stream, b);
    }
    static void onParamChanged(void* data, uint32_t id, const spa_pod* param) {
        auto* self = static_cast<Impl*>(data);
        if (param == nullptr || id != SPA_PARAM_Format) return;
        spa_audio_info_raw info {};
        if (spa_format_audio_raw_parse(param, &info) >= 0 && info.channels > 0)
            self->channels = (int)info.channels;
    }
    static void onStateChanged(void* data, pw_stream_state, pw_stream_state state, const char* err) {
        auto* self = static_cast<Impl*>(data);
        LOG_INFO("audio capture stream: %s", pw_stream_state_as_string(state));
        if (state == pw_stream_state::PW_STREAM_STATE_ERROR)
            LOG_ERROR("audio capture stream error: %s", err ? err : "?");
        if (state != pw_stream_state::PW_STREAM_STATE_STREAMING) {
            std::lock_guard<std::mutex> lk(self->ring_mtx);
            self->streaming = false;
        }
    }

    bool start() {
        static std::once_flag once;
        std::call_once(once, [] {
            pw_init(nullptr, nullptr);
        });
        loop = pw_thread_loop_new("we-audio-capture", nullptr);
        if (loop == nullptr) return false;

        pw_properties* props = pw_properties_new(PW_KEY_MEDIA_TYPE,
                                                 "Audio",
                                                 PW_KEY_MEDIA_CATEGORY,
                                                 "Capture",
                                                 PW_KEY_MEDIA_ROLE,
                                                 "Music",
                                                 PW_KEY_STREAM_CAPTURE_SINK,
                                                 "true",
                                                 PW_KEY_NODE_NAME,
                                                 "wallpaper-engine-kde",
                                                 PW_KEY_APP_NAME,
                                                 "Wallpaper Engine KDE",
                                                 nullptr);
        static const pw_stream_events events = [] {
            pw_stream_events e {};
            e.version       = PW_VERSION_STREAM_EVENTS;
            e.process       = &Impl::onProcess;
            e.param_changed = &Impl::onParamChanged;
            e.state_changed = &Impl::onStateChanged;
            return e;
        }();

        pw_thread_loop_lock(loop);
        stream = pw_stream_new_simple(
            pw_thread_loop_get_loop(loop), "wallpaper-engine-kde capture", props, &events, this);
        if (stream == nullptr) {
            pw_thread_loop_unlock(loop);
            return false;
        }
        uint8_t         buffer[1024];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        spa_audio_info_raw info {};
        info.format   = SPA_AUDIO_FORMAT_F32;
        info.rate     = kRate;
        info.channels = kChannels;
        const spa_pod* params[1] = { spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info) };
        int rc = pw_stream_connect(stream,
                                   PW_DIRECTION_INPUT,
                                   PW_ID_ANY,
                                   (pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT |
                                                     PW_STREAM_FLAG_MAP_BUFFERS |
                                                     PW_STREAM_FLAG_RT_PROCESS),
                                   params,
                                   1);
        pw_thread_loop_unlock(loop);
        if (rc < 0) {
            LOG_ERROR("audio capture: pw_stream_connect failed (%d)", rc);
            return false;
        }
        if (pw_thread_loop_start(loop) < 0) return false;
        LOG_INFO("audio capture started (PipeWire, default sink monitor)");
        return true;
    }
    void stop() {
        if (loop == nullptr) return;
        pw_thread_loop_lock(loop);
        if (stream != nullptr) pw_stream_destroy(stream);
        stream = nullptr;
        pw_thread_loop_unlock(loop);
        pw_thread_loop_stop(loop);
        pw_thread_loop_destroy(loop);
        loop = nullptr;
        LOG_INFO("audio capture stopped");
    }
#else
    bool start() {
        LOG_INFO("audio capture unavailable: built without PipeWire");
        return false;
    }
    void stop() {}
#endif
};

namespace
{
std::mutex                  g_instance_mtx;
std::weak_ptr<AudioCapture> g_instance;
} // namespace

std::shared_ptr<AudioCapture> AudioCapture::Acquire() {
    std::lock_guard<std::mutex> lk(g_instance_mtx);
    if (auto p = g_instance.lock()) return p;
    std::shared_ptr<AudioCapture> p(new AudioCapture());
    g_instance = p;
    return p;
}

bool AudioCapture::Available() {
#ifdef WP_HAVE_PIPEWIRE
    return true;
#else
    return false;
#endif
}

AudioCapture::AudioCapture(): m_impl(new Impl()) { m_impl->start(); }
AudioCapture::~AudioCapture() { m_impl->stop(); }

const AudioSpectrum& AudioCapture::Spectrum() {
    auto&        I   = *m_impl;
    const double now = NowSec();
    if (I.last_compute == 0.0) I.last_compute = now;
    const double dt = now - I.last_compute;
    if (dt >= kRefreshSec) {
        I.compute(dt);
        I.last_compute = now;
        // WP_AUDIO_DEBUG=1: a spectrum summary every second
        static const bool debug = std::getenv("WP_AUDIO_DEBUG") != nullptr;
        static double     last_log = 0.0;
        if (debug && now - last_log > 1.0) {
            last_log = now;
            int   peak_band = 0;
            float peak      = 0.0f;
            for (int b = 0; b < kSpectrumBands; b++)
                if (I.spectrum.average[(size_t)b] > peak) {
                    peak      = I.spectrum.average[(size_t)b];
                    peak_band = b;
                }
            LOG_INFO("audio: active=%d filled=%zu peak=%.3f@band%d L0=%.3f R0=%.3f",
                     (int)I.spectrum.active,
                     I.ring_filled,
                     peak,
                     peak_band,
                     I.spectrum.left[0],
                     I.spectrum.right[0]);
        }
    }
    return I.spectrum;
}

void wallpaper::audio::ResampleBands(const std::array<float, kSpectrumBands>& in, float* out,
                                     int bands) {
    if (bands <= 0) return;
    const int per = std::max(1, kSpectrumBands / bands);
    for (int b = 0; b < bands; b++) {
        float sum = 0.0f;
        int   n   = 0;
        for (int k = b * per; k < (b + 1) * per && k < kSpectrumBands; k++, n++)
            sum += in[(size_t)k];
        out[b] = n > 0 ? sum / (float)n : 0.0f;
    }
}
