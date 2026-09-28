#include "AudioSpectrum.hpp"
#include "Audio/AudioCapture.h"
#include <algorithm>

using namespace wekde;
using wallpaper::audio::AudioCapture;
using wallpaper::audio::kSpectrumBands;

AudioSpectrum::AudioSpectrum(QObject* parent): QObject(parent) {
    m_timer.setInterval(33); // ~30 frames/s, plenty for bars
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &AudioSpectrum::tick);
}

AudioSpectrum::~AudioSpectrum() = default;

bool AudioSpectrum::available() const { return AudioCapture::Available(); }

void AudioSpectrum::setInterval(int ms) {
    ms = std::max(10, ms);
    if (ms == m_timer.interval()) return;
    m_timer.setInterval(ms);
    emit intervalChanged();
}

void AudioSpectrum::setActive(bool v) {
    if (v == m_active) return;
    m_active = v;
    if (m_active) {
        if (! m_capture) m_capture = AudioCapture::Acquire();
        m_timer.start();
    } else {
        m_timer.stop();
        m_capture.reset(); // releases the shared capturer when nobody else holds it
    }
    emit activeChanged();
}

void AudioSpectrum::tick() {
    if (! m_capture) return;
    const auto&  sp = m_capture->Spectrum();
    QVariantList out;
    out.reserve(2 * kSpectrumBands);
    for (float v : sp.left) out.append((double)v);
    for (float v : sp.right) out.append((double)v);
    emit frame(out);
}
