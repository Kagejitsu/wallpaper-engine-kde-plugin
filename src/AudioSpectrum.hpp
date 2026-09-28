#pragma once
// QML access to the shared system-audio spectrum (see Audio/AudioCapture.h),
// for backends that are not the scene renderer: the web backend hands the
// frames to window.wallpaperRegisterAudioListener() as Wallpaper Engine does
// (128 values: 64 left bands, then 64 right, each 0..1).
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <memory>

namespace wallpaper::audio
{
class AudioCapture;
}

namespace wekde
{

class AudioSpectrum : public QObject {
    Q_OBJECT
    // capture runs only while active (a page registered a listener and is playing)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(int interval READ interval WRITE setInterval NOTIFY intervalChanged)
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    explicit AudioSpectrum(QObject* parent = nullptr);
    ~AudioSpectrum() override;

    bool active() const { return m_active; }
    void setActive(bool v);
    int  interval() const { return m_timer.interval(); }
    void setInterval(int ms);
    bool available() const;

signals:
    void activeChanged();
    void intervalChanged();
    // 128 floats: left[0..63], right[0..63]
    void frame(const QVariantList& audioArray);

private:
    void tick();

    bool                                            m_active { false };
    QTimer                                          m_timer;
    std::shared_ptr<wallpaper::audio::AudioCapture> m_capture;
};

} // namespace wekde
