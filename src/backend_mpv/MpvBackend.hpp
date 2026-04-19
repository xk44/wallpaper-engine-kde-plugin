#ifndef MPVRENDERER_H_
#define MPVRENDERER_H_

#include <mpv/client.h>
#include <mpv/render.h>

#include <QtQuick/QQuickRhiItem>
#include <QtCore/QLoggingCategory>
#include <QtCore/QVariantMap>
#include <QtCore/QTimer>
#include <memory>
#include <atomic>

#include "qthelper.hpp"

Q_DECLARE_LOGGING_CATEGORY(wekdeMpv)

namespace mpv
{

struct MpvHandle {
    MpvHandle(mpv_handle* mpv): handle(mpv) {}
    ~MpvHandle() { mpv_terminate_destroy(handle); }
    mpv_handle* handle;
};

class MpvRender;

class MpvObject : public QQuickRhiItem {
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool mute READ mute WRITE setMute)
    Q_PROPERTY(QString logfile READ logfile WRITE setLogfile)
    Q_PROPERTY(int volume READ volume WRITE setVolume)
    Q_PROPERTY(QString hwdec READ hwdec WRITE setHwdec)
    Q_PROPERTY(int fpsLimit READ fpsLimit WRITE setFpsLimit NOTIFY fpsLimitChanged)
    Q_PROPERTY(QString renderPath READ renderPath NOTIFY renderPathChanged)
    Q_PROPERTY(double renderScale READ renderScale WRITE setRenderScale NOTIFY renderScaleChanged)

    friend class MpvRender;

public:
    explicit MpvObject(QQuickItem* parent = nullptr);
    virtual ~MpvObject();
    QQuickRhiItemRenderer* createRenderer() override;

    enum Status
    {
        Stopped,
        Playing,
        Paused,
    };
    Q_ENUM(Status)
    Status  status() const;
    QUrl    source() const;
    bool    mute() const;
    QString logfile() const;
    int     volume() const;

    void    setSource(const QUrl& source);
    void    setMute(const bool& mute);
    void    setLogfile(const QString& logfile);
    void    setVolume(const int& volume);
    QString hwdec() const;
    void    setHwdec(const QString& hwdec);
    int     fpsLimit() const;
    void    setFpsLimit(int limit);
    QString renderPath() const;
    double  renderScale() const;
    void    setRenderScale(double scale);

    Q_INVOKABLE QVariantMap debugMetrics() const;

public slots:
    void play();
    void pause();
    void stop();

    bool     command(const QVariant& params);
    bool     setProperty(const QString& name, const QVariant& value);
    QVariant getProperty(const QString& name, bool* ok = nullptr) const;
    void     initCallback();
    void     checkAndEmitFirstFrame();

signals:
    void initFinished();
    void statusChanged();
    void sourceChanged();
    void firstFrame();
    void fpsLimitChanged();
    void renderPathChanged();
    void renderScaleChanged();

private:
    bool    inited = false;
    QUrl    m_source;
    Status  m_status = Stopped;
    QString m_hwdec { "auto-copy" };
    int     m_fpsLimit { 0 };      // 0 = no limit (render at source fps)
    QString m_renderPath { "none" };
    double  m_renderScale { 1.0 }; // 1.0 = native, 0.5 = half res

    // Metrics shared from render thread (atomics for lock-free access)
    struct Metrics {
        std::atomic<uint64_t> frameCount { 0 };
        std::atomic<uint64_t> droppedFrames { 0 };
        std::atomic<uint64_t> dirtyUpdates { 0 };
        std::atomic<int64_t>  lastRenderNs { 0 };
        std::atomic<int64_t>  lastUploadNs { 0 };     // SW only
        std::atomic<int64_t>  lastAlphaFixNs { 0 };   // SW only
    };
    std::shared_ptr<Metrics> m_metrics { std::make_shared<Metrics>() };

private:
    mpv_handle*                m_mpv { nullptr };
    std::shared_ptr<MpvHandle> m_shared_mpv { nullptr };
    bool                       m_first_frame { true };
    QTimer*                    m_kickstartTimer { nullptr };
    int                        m_kickstartTicks { 0 };
};
} // namespace mpv

#endif
