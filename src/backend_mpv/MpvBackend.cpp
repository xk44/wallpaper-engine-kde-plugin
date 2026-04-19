#include "MpvBackend.hpp"

#include <QtGlobal>
#include <QtCore/QObject>
#include <QtCore/QDir>
#include <QtQuick/QQuickWindow>
#include <rhi/qrhi.h>

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <mpv/render_gl.h>

#include <memory>
#include <vector>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>

Q_LOGGING_CATEGORY(wekdeMpv, "wekde.mpv")

#define _Q_DEBUG() qCDebug(wekdeMpv)

using namespace mpv;

// GL proc address callback for mpv OpenGL render API
static void* mpv_gl_get_proc_address(void* /*ctx*/, const char* name) {
    QOpenGLContext* glctx = QOpenGLContext::currentContext();
    if (! glctx) return nullptr;
    return reinterpret_cast<void*>(glctx->getProcAddress(QByteArray(name)));
}

// ── MpvObject property/command methods ──────────────────────────────────────

bool MpvObject::command(const QVariant& params) {
    auto* mpv       = m_mpv;
    int   errorCode = mpv::qt::get_error(mpv::qt::command(mpv, params));
    return (errorCode >= 0);
}

bool MpvObject::setProperty(const QString& name, const QVariant& value) {
    auto* mpv       = m_mpv;
    int   errorCode = mpv::qt::get_error(mpv::qt::set_property(mpv, name, value));
    _Q_DEBUG() << "Setting property" << name << "to" << value;
    return (errorCode >= 0);
}

QVariant MpvObject::getProperty(const QString& name, bool* ok) const {
    auto* mpv = m_mpv;
    if (ok) *ok = false;

    if (name.isEmpty()) {
        return QVariant();
    }
    QVariant  result    = mpv::qt::get_property(mpv, name);
    const int errorCode = mpv::qt::get_error(result);
    if (errorCode >= 0) {
        if (ok) {
            *ok = true;
        }
    } else {
        _Q_DEBUG() << "Failed to query property: " << name << "code" << errorCode << " result"
                   << result;
    }
    return result;
}

void MpvObject::initCallback() {
    QUrl temp(m_source.toString());
    m_source.clear();
    inited = true;
    setSource(temp);
    Q_EMIT initFinished();
}

void MpvObject::play() {
    if (status() != Paused) return;
    this->setProperty("pause", false);
}

void MpvObject::pause() {
    if (status() != Playing) return;
    this->setProperty("pause", true);
}

void MpvObject::stop() {
    if (status() == Stopped) return;
    bool result = this->command(QVariantList { "stop" });
    if (result) {
        m_source.clear();
        Q_EMIT sourceChanged();
    }
}

MpvObject::Status MpvObject::status() const {
    const bool stopped = getProperty("idle-active").toBool();
    const bool paused  = getProperty("pause").toBool();
    return stopped ? Stopped : (paused ? Paused : Playing);
}

QUrl MpvObject::source() const { return m_source; }

bool MpvObject::mute() const {
    QString aid = getProperty("aid").toString();
    return aid == "no";
}

QString MpvObject::logfile() const { return getProperty("log-file").toString(); }

int MpvObject::volume() const { return getProperty("volume").toInt(); }

void MpvObject::setMute(const bool& mute) { setProperty("aid", mute ? "no" : "auto"); }

void MpvObject::setVolume(const int& volume) { setProperty("volume", volume); }

QString MpvObject::hwdec() const { return m_hwdec; }

void MpvObject::setHwdec(const QString& hwdec) {
    if (m_hwdec == hwdec) return;
    m_hwdec = hwdec;
    mpv_set_property_string(m_mpv, "hwdec", hwdec.toUtf8().constData());
}

void MpvObject::setLogfile(const QString& logfile) { setProperty("log-file", logfile); }

int MpvObject::fpsLimit() const { return m_fpsLimit; }

void MpvObject::setFpsLimit(int limit) {
    if (m_fpsLimit == limit) return;
    m_fpsLimit = limit;
    // Use mpv's native fps filter for frame-accurate limiting
    if (limit > 0) {
        QString vf = QStringLiteral("fps=%1").arg(limit);
        mpv_set_property_string(m_mpv, "vf", vf.toUtf8().constData());
    } else {
        mpv_set_property_string(m_mpv, "vf", "");
    }
    Q_EMIT fpsLimitChanged();
}

QString MpvObject::renderPath() const { return m_renderPath; }

double MpvObject::renderScale() const { return m_renderScale; }

void MpvObject::setRenderScale(double scale) {
    scale = std::clamp(scale, 0.25, 1.0);
    if (m_renderScale == scale) return;
    m_renderScale = scale;
    Q_EMIT renderScaleChanged();
}

QVariantMap MpvObject::debugMetrics() const {
    QVariantMap m;
    m["renderPath"]    = m_renderPath;
    m["renderScale"]   = m_renderScale;
    m["frameCount"]    = (qulonglong)m_metrics->frameCount.load();
    m["droppedFrames"] = (qulonglong)m_metrics->droppedFrames.load();
    m["dirtyUpdates"]  = (qulonglong)m_metrics->dirtyUpdates.load();
    m["lastRenderMs"]  = m_metrics->lastRenderNs.load() / 1e6;
    m["lastUploadMs"]  = m_metrics->lastUploadNs.load() / 1e6;
    m["lastAlphaFixMs"] = m_metrics->lastAlphaFixNs.load() / 1e6;
    return m;
}

void MpvObject::setSource(const QUrl& source) {
    if (source.isEmpty()) {
        stop();
        return;
    }
    if (! source.isValid() || (source == m_source)) {
        return;
    }
    if (! inited) {
        m_source = source;
        return;
    }
    const QString file =
        source.isLocalFile() ? QDir::toNativeSeparators(source.toLocalFile()) : source.url();
    qCInfo(wekdeMpv) << "loadfile:" << file;
    bool result = this->command(QVariantList { "loadfile", file });
    if (result) {
        m_source = source;
        Q_EMIT sourceChanged();
        m_first_frame = false;
        // Force scene-graph update ticks for the next few seconds so the
        // first video frame renders even if mpv's update_callback is
        // delayed by hwdec init on problematic drivers.
        if (m_kickstartTimer) {
            m_kickstartTicks = 0;
            m_kickstartTimer->start();
        }
    } else {
        qCWarning(wekdeMpv) << "loadfile FAILED for:" << file;
    }
}

// ── MpvRender (render thread) ───────────────────────────────────────────────

namespace mpv
{

class MpvRender : public QObject, public QQuickRhiItemRenderer {
    Q_OBJECT
public:
    MpvRender(std::shared_ptr<MpvHandle> mpv, std::shared_ptr<MpvObject::Metrics> metrics)
        : m_shared_mpv(mpv), m_mpv(mpv->handle), m_metrics(metrics) {}

    virtual ~MpvRender() {
        _Q_DEBUG() << "destroyed";
        mpv::qt::command(m_mpv, QVariantList { "stop" });

        if (m_render_ctx) {
            mpv_render_context_set_update_callback(m_render_ctx, nullptr, nullptr);
            mpv_render_context_free(m_render_ctx);
        }
        m_render_ctx = nullptr;

        if (m_fbo) {
            QOpenGLContext* glctx = QOpenGLContext::currentContext();
            if (glctx) {
                glctx->functions()->glDeleteFramebuffers(1, &m_fbo);
            }
            m_fbo = 0;
        }
    }

    bool Dirty() const { return m_dirty.load(); }
    bool setDirty(bool v) { return m_dirty.exchange(v); }

signals:
    void mpvRedraw();
    void inited();

protected:
    void initialize(QRhiCommandBuffer* cb) override {
        Q_UNUSED(cb)
        // NOTE: Qt calls initialize() every frame from QQuickRhiItemNode::sync().
        // Do NOT log here — it would spam the journal. One-shot init logging
        // lives in synchronize() where we create the render context.
        m_hasGL = (rhi()->backend() == QRhi::OpenGLES2);
    }

    void synchronize(QQuickRhiItem* item) override {
        MpvObject* mpv_obj = static_cast<MpvObject*>(item);

        if (! m_render_ctx) {
            // GL FBO path only when hwdec=no (software decode). With any
            // hwdec active, mpv's GL renderer corrupts Qt's GL state
            // (shader/texture/VAO bindings) even with beginExternal().
            // SW render API avoids all GL conflicts while still benefiting
            // from GPU decode — mpv copies decoded frames to CPU internally.
            bool wantGL = m_hasGL && (mpv_obj->hwdec() == "no");

            qCInfo(wekdeMpv) << "=== MPV render context setup ===";
            qCInfo(wekdeMpv) << "  RHI backend:" << rhi()->backendName();
            qCInfo(wekdeMpv) << "  hwdec:" << mpv_obj->hwdec();
            qCInfo(wekdeMpv) << "  render path:" << (wantGL ? "gpu-gl (zero-copy)" : "sw (QRhi upload)");
            qCInfo(wekdeMpv) << "  driver info:" << rhi()->driverInfo().deviceName;
            bool created = false;

            if (wantGL) {
                mpv_opengl_init_params gl_init {};
                gl_init.get_proc_address = mpv_gl_get_proc_address;

                mpv_render_param params[] = {
                    { MPV_RENDER_PARAM_API_TYPE, (void*)MPV_RENDER_API_TYPE_OPENGL },
                    { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init },
                    { MPV_RENDER_PARAM_INVALID, nullptr },
                };

                int err = mpv_render_context_create(&m_render_ctx, m_mpv, params);
                if (err >= 0) {
                    m_useGLPath = true;
                    created     = true;
                } else {
                    _Q_DEBUG() << "GL context creation failed (err" << err
                               << "), falling back to SW";
                }
            }

            if (! created) {
                m_useGLPath = false;
                mpv_render_param params[] = {
                    { MPV_RENDER_PARAM_API_TYPE, (void*)MPV_RENDER_API_TYPE_SW },
                    { MPV_RENDER_PARAM_INVALID, nullptr },
                };
                if (mpv_render_context_create(&m_render_ctx, m_mpv, params) >= 0) {
                    _Q_DEBUG() << "SW render context created";
                } else {
                    _Q_DEBUG() << "failed to create any render context";
                    return;
                }
            }

            mpv_render_context_set_update_callback(m_render_ctx, on_mpv_redraw, this);

            // Push render path info back to QML thread
            QString path = m_useGLPath ? "gpu-gl" : "sw";
            if (mpv_obj->m_renderPath != path) {
                mpv_obj->m_renderPath = path;
                Q_EMIT mpv_obj->renderPathChanged();
            }

            qCInfo(wekdeMpv) << "  actual path:" << path;
            Q_EMIT this->inited();
        }

        // Resolution scaling: set fixed buffer size when scale < 1.0.
        //
        // IMPORTANT: setFixedColorBufferWidth/Height MUST be guarded against
        // same-value writes. Qt's QQuickRhiItem re-creates the scene graph
        // node whenever these change, which in turn re-calls initialize().
        // Calling them unconditionally every sync creates an infinite
        // init→sync→setFixed→init loop and the video never actually renders.
        double scale = mpv_obj->renderScale();
        int    wantW = 0, wantH = 0;
        if (scale < 1.0) {
            wantW = std::max(1, (int)(mpv_obj->width() * scale));
            wantH = std::max(1, (int)(mpv_obj->height() * scale));
        }
        if (wantW != m_lastFixedW) {
            mpv_obj->setFixedColorBufferWidth(wantW);
            m_lastFixedW = wantW;
        }
        if (wantH != m_lastFixedH) {
            mpv_obj->setFixedColorBufferHeight(wantH);
            m_lastFixedH = wantH;
        }

        if (Dirty()) {
            mpv_obj->checkAndEmitFirstFrame();
        }
    }

    void render(QRhiCommandBuffer* cb) override {
        QRhiTexture* tex = colorTexture();
        if (! tex || ! m_render_ctx) return;

        QSize texSize = tex->pixelSize();
        int   w       = texSize.width();
        int   h       = texSize.height();
        if (w <= 0 || h <= 0) return;

        bool sizeChanged = (m_bufWidth != w || m_bufHeight != h);
        if (! (setDirty(false) || sizeChanged)) return;

        using clock = std::chrono::steady_clock;
        auto t0     = clock::now();

        if (m_useGLPath) {
            renderGL(cb, tex, w, h);
        } else {
            renderSW(cb, tex, w, h, sizeChanged);
        }

        auto elapsed = clock::now() - t0;
        m_metrics->lastRenderNs.store(
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
        m_metrics->frameCount.fetch_add(1);

        m_bufWidth  = w;
        m_bufHeight = h;
    }

private:
    // ── GPU render path: mpv renders directly to FBO → zero CPU copy ───────
    void renderGL(QRhiCommandBuffer* cb, QRhiTexture* tex, int w, int h) {
        QOpenGLContext* glctx = QOpenGLContext::currentContext();
        if (! glctx) return;
        QOpenGLFunctions* f = glctx->functions();

        if (! m_fbo) {
            f->glGenFramebuffers(1, &m_fbo);
        }

        // Attach QRhi's GL texture to our FBO
        auto   native = tex->nativeTexture();
        GLuint glTex  = (GLuint)(quintptr)native.object;

        // Tell Qt's RHI that external GL operations follow. This makes
        // QRhi save its tracked GL state and re-sync afterward, preventing
        // mpv's shader/texture/FBO changes from corrupting Qt's scene graph.
        cb->beginExternal();

        GLint prevFbo = 0;
        f->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

        f->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        f->glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, glTex, 0);

        // mpv renders directly to FBO on GPU — no CPU copy, no alpha fix
        mpv_opengl_fbo fbo_param {};
        fbo_param.fbo = (int)m_fbo;
        fbo_param.w   = w;
        fbo_param.h   = h;

        int              flip_y = 1; // GPU flip (replaces vf=vflip to avoid CUDA interop)
        mpv_render_param params[] = {
            { MPV_RENDER_PARAM_OPENGL_FBO, &fbo_param },
            { MPV_RENDER_PARAM_FLIP_Y, &flip_y },
            { MPV_RENDER_PARAM_INVALID, nullptr },
        };

        mpv_render_context_render(m_render_ctx, params);

        f->glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);

        cb->endExternal();
    }

    // ── SW fallback: CPU render + alpha fix + texture upload ───────────────
    void renderSW(QRhiCommandBuffer* cb, QRhiTexture* tex, int w, int h, bool sizeChanged) {
        size_t stride  = ((size_t)w * 4 + 63) & ~(size_t)63;
        size_t bufSize = stride * h;

        if (sizeChanged) {
            size_t alignedBufSize = (bufSize + 63) & ~(size_t)63;
            m_buffer.resize(alignedBufSize);
        }

        int              size[] = { w, h };
        mpv_render_param params[] = { { MPV_RENDER_PARAM_SW_SIZE, size },
                                      { MPV_RENDER_PARAM_SW_FORMAT, (void*)"rgb0" },
                                      { MPV_RENDER_PARAM_SW_STRIDE, &stride },
                                      { MPV_RENDER_PARAM_SW_POINTER, m_buffer.data },
                                      { MPV_RENDER_PARAM_INVALID, nullptr } };

        if (mpv_render_context_render(m_render_ctx, params) < 0) {
            _Q_DEBUG() << "SW render failed";
            return;
        }

        // rgb0 has alpha=0; force to 255 for opaque display.
        // Also flip vertically (replaces vf=vflip to avoid CUDA interop).
        using clock = std::chrono::steady_clock;
        auto alphaT0 = clock::now();

        static_assert(std::endian::native == std::endian::little,
                      "Alpha fix assumes little-endian byte order");

        size_t flippedSize = stride * h;
        if (m_flipped.size != flippedSize) {
            m_flipped.resize((flippedSize + 63) & ~(size_t)63);
        }
        for (int y = 0; y < h; y++) {
            const uint32_t* srcRow =
                reinterpret_cast<const uint32_t*>(m_buffer.data + y * stride);
            uint32_t* dstRow =
                reinterpret_cast<uint32_t*>(m_flipped.data + (h - 1 - y) * stride);
            for (int x = 0; x < w; x++) {
                dstRow[x] = srcRow[x] | 0xFF000000u;
            }
        }

        m_metrics->lastAlphaFixNs.store(
            std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - alphaT0).count());

        auto uploadT0 = clock::now();

        QByteArray data = QByteArray::fromRawData(
            reinterpret_cast<const char*>(m_flipped.data),
            static_cast<qsizetype>(flippedSize));

        QRhiTextureSubresourceUploadDescription desc(data);
        desc.setDataStride((quint32)stride);

        QRhiResourceUpdateBatch* batch = rhi()->nextResourceUpdateBatch();
        batch->uploadTexture(
            tex, QRhiTextureUploadDescription({ QRhiTextureUploadEntry(0, 0, desc) }));
        cb->resourceUpdate(batch);

        m_metrics->lastUploadNs.store(
            std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - uploadT0).count());
    }

    static void on_mpv_redraw(void* ctx) {
        auto* render = static_cast<MpvRender*>(ctx);

        // Track dirty updates; if already dirty, previous frame was dropped
        bool wasDirty = render->setDirty(true);
        render->m_metrics->dirtyUpdates.fetch_add(1);
        if (wasDirty) {
            render->m_metrics->droppedFrames.fetch_add(1);
        }

        // Always signal scene graph — FPS throttling happens in render()
        Q_EMIT render->mpvRedraw();
    }

    mpv_handle*                          m_mpv { nullptr };
    mpv_render_context*                  m_render_ctx { nullptr };
    std::shared_ptr<MpvHandle>           m_shared_mpv;
    std::shared_ptr<MpvObject::Metrics>  m_metrics;

    bool   m_hasGL { false };      // RHI backend is OpenGL
    bool   m_useGLPath { false };  // actually using GL FBO (only when hwdec=no + GL)
    GLuint m_fbo { 0 };
    int    m_lastFixedW { -1 };
    int    m_lastFixedH { -1 };

    struct AlignedBuffer {
        uint8_t* data { nullptr };
        size_t   size { 0 };
        void     resize(size_t newSize) {
            if (size == newSize) return;
            std::free(data);
            data = static_cast<uint8_t*>(std::aligned_alloc(64, newSize));
            size = data ? newSize : 0;
            if (data) std::memset(data, 0, size);
        }
        ~AlignedBuffer() { std::free(data); }
        AlignedBuffer()                                = default;
        AlignedBuffer(const AlignedBuffer&)            = delete;
        AlignedBuffer& operator=(const AlignedBuffer&) = delete;
    };

    AlignedBuffer     m_buffer;   // only used by SW path
    AlignedBuffer     m_flipped;  // only used by SW path (flipped + alpha-fixed)
    int               m_bufWidth { 0 };
    int               m_bufHeight { 0 };
    std::atomic<bool> m_dirty { false };
};

} // namespace mpv

// ── MpvObject construction and renderer creation ────────────────────────────

MpvObject::MpvObject(QQuickItem* parent)
    : QQuickRhiItem(parent), m_shared_mpv(std::make_shared<MpvHandle>(mpv_create())) {
    m_mpv = m_shared_mpv->handle;

    if (! m_mpv) {
        _Q_DEBUG() << "could not create mpv context";
        return;
    }

    // All options MUST be set before mpv_initialize
    mpv_set_option_string(m_mpv, "terminal", "no");
    mpv_set_option_string(m_mpv, "msg-level", "all=info");
    mpv_set_option_string(m_mpv, "config", "no");
    mpv_set_option_string(m_mpv, "vo", "libmpv");
    mpv_set_option_string(m_mpv, "hwdec", m_hwdec.toUtf8().constData());
    // No vf=vflip: it forces CUDA interop on Vulkan hw-decoded frames.
    // Flipping is handled per-path at zero cost:
    //   GL: MPV_RENDER_PARAM_FLIP_Y=1 (GPU texture coord flip)
    //   SW: reverse row order during alpha-fix pass
    mpv_set_option_string(m_mpv, "loop", "inf");

    // Route mpv internal logs to a file for diagnostics. Uses mpv's built-in
    // logger — no wakeup callback, no event pump, no threading races.
    mpv_set_option_string(m_mpv, "log-file", "/tmp/wekde-mpv.log");

    if (mpv_initialize(m_mpv) < 0) {
        _Q_DEBUG() << "could not initialize mpv context";
        m_mpv = nullptr;
        return;
    }

    // Blank-screen mitigation: mpv's on_mpv_redraw callback is unreliable on
    // some drivers (NVIDIA 595 + hwdec-auto), so after loadfile the first
    // frame never arrives. Force periodic update() ticks for the first few
    // seconds after setSource — by then mpv has either started decoding or
    // we've exhausted our grace window and rely on its callback.
    m_kickstartTimer = new QTimer(this);
    m_kickstartTimer->setInterval(100);
    connect(m_kickstartTimer, &QTimer::timeout, this, [this]() {
        update();
        if (++m_kickstartTicks >= 30) { // 3 seconds of forced ticks
            m_kickstartTimer->stop();
            m_kickstartTicks = 0;
        }
    });
}

MpvObject::~MpvObject() {}

void MpvObject::checkAndEmitFirstFrame() {
    if (! m_first_frame) {
        m_first_frame = true;
        Q_EMIT firstFrame();
    }
}

QQuickRhiItemRenderer* MpvObject::createRenderer() {
    window()->setPersistentSceneGraph(true);

    auto* render = new MpvRender(m_shared_mpv, m_metrics);

    connect(render, &MpvRender::mpvRedraw, this, &MpvObject::update, Qt::QueuedConnection);
    connect(render, &MpvRender::inited, this, &MpvObject::initCallback, Qt::QueuedConnection);
    return render;
}

#include "MpvBackend.moc"
