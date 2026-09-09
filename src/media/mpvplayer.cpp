#include "media/mpvplayer.h"
#include "app/async.h"
#include "app/settings.h"
#include <QDir>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrentRun>
#include <QCoreApplication>
#include <QMetaType>
#include <QOpenGLContext>
#include <QStandardPaths>
#include <clocale>
#include <cmath>
#include <stdexcept>
#include <limits>
#include <QStringList>
#include <QQuickOpenGLUtils>
#include <QtOpenGL/QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QScreen>
#include "ui/appshell.h"
#include "app/logger.h"
#include "media/danmaku.h"
#include <windows.h>   // SetThreadExecutionState; must follow the Qt headers

namespace {
// Idempotent: calling either twice is harmless.
void keepDisplayAwake() { SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED); }
void allowDisplaySleep() { SetThreadExecutionState(ES_CONTINUOUS); }
}

// Sharing Qt's GL context: a private thread and second context makes the driver sync every shader pass.
class MpvRenderer : public QQuickFramebufferObject::Renderer {
    MpvPlayer *m_obj;
    bool m_visible = true;

public:
    MpvRenderer(MpvPlayer *obj) : m_obj(obj) {}
    ~MpvRenderer() override { m_obj->freeMpvRenderContext(); }

    void synchronize(QQuickFramebufferObject *item) override { m_visible = item->isVisible(); }

    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override {
        QOpenGLFramebufferObjectFormat fmt;
        fmt.setAttachment(QOpenGLFramebufferObject::NoAttachment);
        fmt.setSamples(0);
        fmt.setMipmap(false);
        return new QOpenGLFramebufferObject(m_obj->clampRenderSize(size), fmt);
    }

    void render() override {
        QOpenGLFramebufferObject *target = framebufferObject();
        if (!target || !m_obj->ensureMpvRenderContext()) return;

        QQuickOpenGLUtils::resetOpenGLState();
        if (!m_visible) {
            auto *gl = QOpenGLContext::currentContext()->functions();
            gl->glClearColor(0.f, 0.f, 0.f, 1.f);
            gl->glClear(GL_COLOR_BUFFER_BIT);
            QQuickOpenGLUtils::resetOpenGLState();
            return;
        }

        mpv_opengl_fbo mpfbo{static_cast<int>(target->handle()), target->width(), target->height(), 0};
        int flip_y = 0;
        // The scene graph presents, not mpv; at its default it hands frames over late and drops the next.
        int blockForTargetTime = 0;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &mpfbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
            {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &blockForTargetTime},
            {MPV_RENDER_PARAM_INVALID, nullptr}
        };
        m_obj->handle().render(params);
        QQuickOpenGLUtils::resetOpenGLState();
    }
};

MpvPlayer::MpvPlayer(QQuickItem *parent) : QQuickFramebufferObject(parent) {
    s_instance.store(this, std::memory_order_release);
    // Cap the render size to the display (GUI thread - QScreen isn't safe off it).
    QSize cap;
    if (QScreen *s = QGuiApplication::primaryScreen())
        cap = (QSizeF(s->size()) * s->devicePixelRatio()).toSize();
    if (cap.isEmpty()) cap = QSize(1920, 1080);
    m_maxRenderSize.store(cap.boundedTo(QSize(3840, 3840)), std::memory_order_relaxed);
    m_time.store(0, std::memory_order_relaxed);
    m_duration.store(0, std::memory_order_relaxed);
    int vol = Settings::instance().get(Config::Volume);
    m_volume = (vol >= 0 && vol <= 200) ? vol : 100;
    double speed = Settings::instance().get(Config::Speed);
    m_speed = (speed > 0.0 && speed <= 10.0) ? static_cast<float>(speed) : 1.0f;

    connect(this, &QQuickItem::windowChanged, this, [this]() { recomputeMaxRenderSize(); });
    // Bundled mpv folder next to the exe; fall back to %APPDATA%/mpv for user overrides.
    QDir mpvDir(QCoreApplication::applicationDirPath() + "/mpv");
    if (!mpvDir.exists())
        mpvDir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/../mpv");
    if (mpvDir.exists()) {
        m_mpv.set_option("config-dir", mpvDir.absolutePath().toLocal8Bit().constData());
        m_mpv.set_option("config", "yes");
    }

    m_mpv.set_option("ytdl", Settings::instance().mpvYtdlEnabled());
    m_mpv.set_option("pause", false);
    m_mpv.set_option("softvol", true);
    m_mpv.set_option("vo", "libmpv");
    m_mpv.set_option("keep-open", true);
    m_mpv.set_option("screenshot-directory",
                     QStandardPaths::writableLocation(QStandardPaths::PicturesLocation).toUtf8().constData());

    m_mpv.set_option("reset-on-next-file", "video-aspect-override,af,audio-delay,pause");

    m_mpv.set_option("hwdec", "auto");

    m_mpv.set_option("cache", "yes");
    m_mpv.set_option("cache-secs", "100");
    m_mpv.set_option("cache-unlink-files", "whendone");
    const bool mpvLogOn = Settings::instance().mpvLogEnabled();
    m_mpv.set_option("msg-level", mpvLogOn ? "all=v" : "all=error");
    m_mpv.set_option("force-seekable", "yes");
    m_mpv.set_option("cookies", "no");
    // Some CDNs serve valid streams behind certs the OS distrusts (Client ignores them too).
    m_mpv.set_option("tls-verify", "no");
    // Let sub-scale also resize ASS/styled subs (sub-font-size alone is ignored for those).
    m_mpv.set_option("sub-ass-override", "scale");

    m_mpv.observe_property("duration");
    m_mpv.observe_property("playback-time");
    m_mpv.observe_property("paused-for-cache");
    m_mpv.observe_property("base-idle");
    m_mpv.observe_property("pause");
    m_mpv.observe_property("track-list");
    m_mpv.observe_property("glsl-shaders");
    m_mpv.observe_property("aid");
    m_mpv.observe_property("sid");
    m_mpv.observe_property("secondary-sid");
    m_mpv.observe_property("secondary-sub-text");
    m_mpv.observe_property("sub-scale");
    m_mpv.observe_property("sub-delay");
    m_mpv.observe_property("vid");
    m_mpv.request_log_messages(mpvLogOn ? "info" : "error");

    QObject::connect(&Settings::instance(), &Settings::mpvYtdlEnabledChanged, this, [this]() {
        bool enabled = Settings::instance().mpvYtdlEnabled();
        m_mpv.set_property_async("ytdl", enabled);
        showText(QString("ytdl: %1").arg(enabled ? "on" : "off"));
    });

    m_danmakuRefresh.setSingleShot(true);
    m_danmakuRefresh.setInterval(100);
    connect(&m_danmakuWriter, &QFutureWatcher<QString>::finished, this, [this]() {
        if (m_danmakuWriter.future().resultCount() == 0 || m_danmakuWriter.result().isEmpty()) return;
        const int index = danmakuTrackIndex();
        if (index < 0) return;
        // Same path mpv already has open, so the id and selection survive.
        const QByteArray id = QByteArray::number(qlonglong(m_subtitleListModel.idForIndex(index)));
        const char *args[] = {"sub-reload", id.constData(), nullptr};
        if (m_mpv.command(args) < 0)
            logWarn() << "Danmaku" << "sub-reload failed for track" << id;
    });
    connect(&m_danmakuRefresh, &QTimer::timeout, this, &MpvPlayer::refreshDanmaku);
    QObject::connect(&Settings::instance(), &Settings::danmakuStyleChanged, this,
                     [this]() { m_danmakuRefresh.start(); });

    QObject::connect(&Settings::instance(), &Settings::danmakuEnabledChanged, this, [this]() {
        const int index = danmakuTrackIndex();
        if (index < 0) return;
        const qint64 id = m_subtitleListModel.idForIndex(index);
        if (Settings::instance().danmakuEnabled()) setPrimarySub(id);
        else if (m_primarySubId == id) setPrimarySub(0);
    });

    QObject::connect(&Settings::instance(), &Settings::mpvLogEnabledChanged, this, [this]() {
        bool enabled = Settings::instance().mpvLogEnabled();
        m_mpv.set_property_async("msg-level", enabled ? "all=v" : "all=error");
        m_mpv.request_log_messages(enabled ? "info" : "error");
    });

    if (Settings::instance().get(Config::LimitCache)) {
        int64_t forwardBytes  = Settings::instance().get(Config::ForwardCache)  << 20;
        int64_t backwardBytes = Settings::instance().get(Config::BackwardCache) << 20;
        m_mpv.set_option("demuxer-max-bytes", forwardBytes);
        m_mpv.set_option("demuxer-max-back-bytes", backwardBytes);
    }

    if (m_mpv.initialize() < 0)
        throw std::runtime_error("could not initialize mpv context");

    m_mpv.set_property("volume", static_cast<double>(m_volume));
    m_mpv.set_property("speed",  static_cast<double>(m_speed));

    m_mpv.set_wakeup_callback(
        [](void *ctx) {
            MpvPlayer *obj = static_cast<MpvPlayer *>(ctx);
            QMetaObject::invokeMethod(obj, "onMpvEvent", Qt::QueuedConnection);
        },
        this);

}

MpvPlayer::~MpvPlayer() {
    s_instance.store(nullptr, std::memory_order_release);
    waitFor(m_danmakuWriter, "MpvPlayer danmaku writer");
    const char *stopCmd[] = {"stop", nullptr};
    m_mpv.command(stopCmd);   // kill decode + audio now; terminate_destroy alone lets it play on
}

void MpvPlayer::recomputeMaxRenderSize() {
    QScreen *s = window() ? window()->screen() : QGuiApplication::primaryScreen();
    QSize sz = s ? (QSizeF(s->size()) * s->devicePixelRatio()).toSize() : QSize();
    if (sz.isEmpty()) sz = QSize(1920, 1080);
    sz = sz.boundedTo(QSize(3840, 3840));
    if (sz == m_maxRenderSize.load(std::memory_order_relaxed)) return;
    m_maxRenderSize.store(sz, std::memory_order_relaxed);
    if (window()) connect(window(), &QWindow::screenChanged, this,
                          [this]() { recomputeMaxRenderSize(); update(); }, Qt::UniqueConnection);
    update();
}

bool MpvPlayer::ensureMpvRenderContext() {
    if (m_renderCtxInited) return true;

    mpv_opengl_init_params gl_init {
        [](void *, const char *name) -> void * {
            QOpenGLContext *c = QOpenGLContext::currentContext();
            return c ? reinterpret_cast<void *>(c->getProcAddress(QByteArray(name))) : nullptr;
        }
#if MPV_CLIENT_API_VERSION < MPV_MAKE_VERSION(2, 0)
        , nullptr, nullptr
#endif
    };
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };
    if (m_mpv.renderer_initialize(params) < 0) {
        logError() << "Mpv" << "renderer_initialize failed";
        return false;
    }
    m_mpv.set_render_callback([](void *ctx) {
        auto *self = static_cast<MpvPlayer *>(ctx);
        QMetaObject::invokeMethod(self, [self]() { self->update(); }, Qt::QueuedConnection);
    }, this);
    m_renderCtxInited = true;
    return true;
}

void MpvPlayer::freeMpvRenderContext() {
    if (!m_renderCtxInited) return;
    m_renderCtxInited = false;
    m_mpv.set_render_callback(nullptr, nullptr);
    m_mpv.free_renderer();
}

QSize MpvPlayer::clampRenderSize(QSize itemPx) const {
    const QSize cap = m_maxRenderSize.load(std::memory_order_relaxed);
    if (itemPx.isEmpty()) return cap;

    QSize sz = itemPx;
    if (sz.width() > cap.width() || sz.height() > cap.height())
        sz = sz.scaled(cap, Qt::KeepAspectRatio);

    // Anime4K AutoDownscalePre only fires at OUTPUT.w/NATIVE.w in (1.2, 2.0) or (2.4, 4.0);
    // outside those the CNN pass runs at 16x. Nudge up into the nearest band.
    const int nativeW = m_videoWidth.load(std::memory_order_relaxed);
    if (m_bandClamp.load(std::memory_order_relaxed) && nativeW > 0) {
        const double r = double(sz.width()) / double(nativeW);
        const double target = (r < 1.26) ? 1.26 : (r >= 1.99 && r < 2.46 ? 2.46 : 0.0);
        if (target > r && target <= r * 2.0) {
            sz = (QSizeF(sz) * (target / r)).toSize();
            if (sz.width() > 4096 || sz.height() > 4096)
                sz = sz.scaled(QSize(4096, 4096), Qt::KeepAspectRatio);
        }
    }

    if (sz.width() < 64 || sz.height() < 64) {
        const double s = qMax(64.0 / qMax(1, sz.width()), 64.0 / qMax(1, sz.height()));
        sz = (QSizeF(sz) * s).toSize();
    }
    return sz;
}

void MpvPlayer::open(PlayInfo &playItem) {
    if (playItem.videos.isEmpty()) return;

    setLoading(true);

    m_state = Stopped;
    emit mpvStateChanged();

    m_seekFraction = playItem.progress;
    m_danmaku = playItem.danmaku;
    m_danmakuKey = playItem.danmakuKey;

    std::stable_sort(playItem.videos.begin(), playItem.videos.end(),
                     [](const Video &a, const Video &b) {
                         if (a.height != b.height) return a.height > b.height;
                         return a.bitrate > b.bitrate;
                     });

    std::stable_sort(playItem.audios.begin(), playItem.audios.end(),
                     [](const Track &a, const Track &b) {
                         return a.bitrate > b.bitrate;
                     });

    setHeaders(playItem.headers);

    QByteArray videoUrlData = (playItem.videos[0].url.isLocalFile()
                                   ? playItem.videos[0].url.toLocalFile()
                                   : playItem.videos[0].url.toString()).toUtf8();
    const char *args[] = {"loadfile", videoUrlData.constData(), nullptr};
    m_mpv.command_async(args);

    m_audiosToBeAdded = playItem.audios;
    m_subtitlesToBeAdded = playItem.subtitles;
    m_videosToBeAdded = playItem.videos;

    m_currentVideoUrl = playItem.videos[0].url;
    AppShell::instance().navigateTo(AppShell::Page::Player);
}

void MpvPlayer::play() {
    if (m_state == Paused)
        m_mpv.set_property_async("pause", false);
}

void MpvPlayer::pause() {
    if (m_state == Playing)
        m_mpv.set_property_async("pause", true);
}

void MpvPlayer::togglePlayPause() {
    if (m_state == Playing) pause();
    else                          play();
}

void MpvPlayer::stop() {
    const char *args[] = {"stop", nullptr};
    m_mpv.command_async(args);
}

void MpvPlayer::setSubDelay(double seconds) {
    seconds = qBound(-60.0, seconds, 60.0);
    seconds = qRound(seconds * 10.0) / 10.0;
    if (qFuzzyCompare(m_subDelay + 1.0, seconds + 1.0)) return;
    m_subDelay = seconds;
    m_mpv.set_property_async("sub-delay", seconds);
    showText(QStringLiteral("Subtitle delay: %1s").arg(seconds, 0, 'f', 1));
    emit subDelayChanged();
}

void MpvPlayer::setSpeed(float speed) {
    speed = qBound(0.1f, speed, 10.0f);
    if (m_speed == speed) return;
    m_speed = speed;
    m_mpv.set_property_async("speed", static_cast<double>(speed));
    showText(QString("Speed: %1x").arg(speed));
    emit speedChanged();
    Settings::instance().set(Config::Speed, static_cast<double>(speed));
}

void MpvPlayer::seek(qint64 time, bool absolute) {
    if (m_state == Stopped) return;
    if (absolute && time == m_time.load(std::memory_order_relaxed)) return;
    if (absolute && time < 0) time = 0;
    QByteArray time_str = QByteArray::number(time);
    const char *args[] = {"seek", time_str.constData(),
                          absolute ? "absolute" : "relative", nullptr};
    m_mpv.command_async(args);
}

void MpvPlayer::setVolume(int volume) {
    volume = qBound(0, volume, 200);
    if (m_volume == volume) return;
    m_volume = volume;
    m_mpv.set_property_async("volume", static_cast<double>(volume));
    showText(QString("Volume: %1%").arg(volume));
    emit volumeChanged();
    if (volume > 0)
        Settings::instance().set(Config::Volume, volume);
}

void MpvPlayer::setSubVisible(bool subVisible) {
    // Switching them on with nothing chosen would show nothing at all.
    if (subVisible && m_primarySubId == 0 && m_secondarySubId == 0
                   && m_subtitleListModel.count() > 0) {
        setSubIndex(0);
        return;
    }
    if (m_subVisible == subVisible) return;
    m_subVisible = subVisible;
    m_mpv.set_property_async("sub-visibility", m_subVisible);
    m_mpv.set_property_async("secondary-sub-visibility", m_subVisible);
    emit subVisibleChanged();
    if (!m_applyingTrackPrefs) saveTrackPrefs();
}

bool MpvPlayer::addVideo(const Track &video) {
    if (m_state == Stopped) return false;
    if (!m_videoListModel.append(video.url, video.title, video.lang, video.height)) return true;
    QByteArray url = (video.url.isLocalFile() ? video.url.toLocalFile() : video.url.toString()).toUtf8();
    const char *args[] = {"video-add", url.constData(), "auto", "", nullptr};
    m_mpv.command_async(args);
    return true;
}

void MpvPlayer::sendKeyPress(const QString &key) {
    if (key.isEmpty() || key.endsWith('+')) return;
    QByteArray cmd = key.toUtf8();
    const char *args[] = {"keypress", cmd.constData(), nullptr};
    m_mpv.command_async(args);
}

bool MpvPlayer::addAudio(const Track &audio, bool select) {
    if (m_state == Stopped) return false;
    if (!m_audioListModel.append(audio.url, audio.title, audio.lang)) return true;
    QByteArray url = (audio.url.isLocalFile() ? audio.url.toLocalFile() : audio.url.toString()).toUtf8();
    QByteArray title = audio.title.toUtf8();
    const char *mode = select ? "select" : "auto";
    const char *args[] = {"audio-add", url.constData(), mode, title.constData(), "", nullptr};
    m_mpv.command_async(args);
    return true;
}

bool MpvPlayer::addSubtitle(const Track &subtitle) {
    if (m_state == Stopped) return false;
    if (!m_subtitleListModel.append(subtitle.url, subtitle.title, subtitle.lang)) return true;
    QByteArray url = (subtitle.url.isLocalFile() ? subtitle.url.toLocalFile() : subtitle.url.toString()).toUtf8();
    QByteArray title = subtitle.title.toUtf8();
    QByteArray lang = subtitle.lang.toUtf8();
    const QString l = subtitle.lang.toLower();
    const bool isEnglish = l == "en" || l == "eng" || l.startsWith("en-") || l.startsWith("en_")
                           || subtitle.title.contains("english", Qt::CaseInsensitive);
    const bool isDanmaku = l == "danmaku";
    const char *args[] = {"sub-add", url.constData(),
                          (isEnglish || isDanmaku) ? "select" : "auto",
                          title.constData(), lang.constData(), nullptr};
    m_mpv.command_async(args);
    return true;
}

int MpvPlayer::danmakuTrackIndex() const {
    for (int i = 0; i < m_subtitleListModel.count(); ++i) {
        const Track *track = m_subtitleListModel.at(i);
        if (track && track->lang == QLatin1String("danmaku")) return i;
    }
    return -1;
}

void MpvPlayer::refreshDanmaku() {
    if (m_state == Stopped || m_danmaku.isEmpty() || m_danmakuKey.isEmpty()) return;
    if (danmakuTrackIndex() < 0) return;
    // A rebuild is tens of ms on a busy episode, so re-arm rather than queue them up.
    if (m_danmakuWriter.isRunning()) { m_danmakuRefresh.start(); return; }

    m_danmakuWriter.setFuture(QtConcurrent::run(
        [comments = m_danmaku, key = m_danmakuKey, opt = DanmakuOptions::current(),
         dir = Settings::tempDir() + QStringLiteral("/danmaku")] {
            return DanmakuAss::writeFile(comments, key, opt, dir);
        }));
}

void MpvPlayer::screenshot() {
    if (m_state == Stopped) return;
    const char *args[] = {"osd-msg", "screenshot", nullptr};
    m_mpv.command_async(args);
}

void MpvPlayer::onMpvEvent() {
    while (true) {
        const mpv_event *event = m_mpv.wait_event();
        if (!event || event->event_id == MPV_EVENT_NONE) break;
        switch (event->event_id) {
        case MPV_EVENT_START_FILE:      onStartFile(); break;
        case MPV_EVENT_FILE_LOADED:     onFileLoaded(); break;
        case MPV_EVENT_END_FILE:        onEndFile(event); break;
        case MPV_EVENT_IDLE:            onIdle(); break;
        case MPV_EVENT_VIDEO_RECONFIG:  onVideoReconfig(); break;
        case MPV_EVENT_LOG_MESSAGE:     onLogMessage(event); break;
        case MPV_EVENT_PROPERTY_CHANGE: onPropertyChange(event); break;
        default: break;
        }
    }
}

void MpvPlayer::onStartFile() {
    m_playNextEmitted = false;
    m_subRestored = false;
    m_videoPrefApplied = false;
    m_audioPrefApplied = false;
    m_videoWidth.store(0, std::memory_order_relaxed);
    m_videoHeight.store(0, std::memory_order_relaxed);
    m_time.store(0, std::memory_order_relaxed);
    m_subVisible = true;
    emit timeChanged();
    emit subVisibleChanged();
}

// duration can arrive either side of file-loaded; whichever is second does the seek.
void MpvPlayer::applyPendingSeek() {
    if (m_seekFraction <= 0) return;
    const int64_t total = m_duration.load(std::memory_order_relaxed);
    if (total <= 0) return;
    seek(static_cast<qint64>(m_seekFraction * double(total)), true);
    m_seekFraction = 0;
}

void MpvPlayer::onFileLoaded() {
    m_state = Playing;
    keepDisplayAwake();

    applyPendingSeek();

    m_videoListModel.clear();
    // A lone entry is the file mpv already loaded. mpv reports that itself - as one track per HLS
    // variant, labelled with real resolutions - and the row added here could never bind to any of
    // those ids, leaving a dead duplicate named after the server.
    if (m_videosToBeAdded.count() > 1) {
        m_videoListModel.append(m_videosToBeAdded[0].url,
                                m_videosToBeAdded[0].title,
                                m_videosToBeAdded[0].lang,
                                m_videosToBeAdded[0].height);
        for (int i = 1; i < m_videosToBeAdded.count(); i++)
            addVideo(m_videosToBeAdded[i]);
    }
    m_videosToBeAdded.clear();
    m_audioListModel.clear();
    for (int i = 0; i < m_audiosToBeAdded.size(); ++i)
        addAudio(m_audiosToBeAdded[i], /*select=*/i == 0);
    m_audiosToBeAdded.clear();

    m_subtitleListModel.clear();
    // A new file means new mpv tracks; anything fetched for the last one is gone with it.
    if (!m_externalSubIds.isEmpty()) { m_externalSubIds.clear(); emit externalSubsChanged(); }
    m_pendingSubPath.clear();
    if (m_primarySubId != 0)   { m_primarySubId = 0;   emit primarySubIdChanged(); }
    if (m_secondarySubId != 0) { m_secondarySubId = 0; emit secondarySubIdChanged(); }
    for (const auto &sub : std::as_const(m_subtitlesToBeAdded))
        addSubtitle(sub);
    m_subtitlesToBeAdded.clear();

    setLoading(false);
    emit mpvStateChanged();
}

void MpvPlayer::onEndFile(const mpv_event *event) {
    auto *ef = static_cast<mpv_event_end_file *>(event->data);
    handleMpvError(ef->error);
    m_endFileReason = static_cast<mpv_end_file_reason>(ef->reason);
    setLoading(false);
    if (m_endFileReason == MPV_END_FILE_REASON_ERROR)
        emit playbackError();
    // Files with no duration never reach the time-based check below, so advance on real EOF.
    else if (m_endFileReason == MPV_END_FILE_REASON_EOF && !m_playNextEmitted) {
        m_playNextEmitted = true;
        emit playNext();
    }
}

void MpvPlayer::onIdle() {
    m_state = Stopped;
    emit mpvStateChanged();
}

void MpvPlayer::onVideoReconfig() {
    Mpv::Node width = m_mpv.get_property("dwidth");
    Mpv::Node height = m_mpv.get_property("dheight");
    // operator int64_t() asserts on the format, and dheight can lag dwidth.
    if (width.type() != MPV_FORMAT_INT64 || height.type() != MPV_FORMAT_INT64) return;

    const int w = int(int64_t(width));
    const int h = int(int64_t(height));
    m_videoWidth.store(w, std::memory_order_relaxed);
    m_videoHeight.store(h, std::memory_order_relaxed);
    update();

    const QSizeF item = size();
    if (h > 0 && item.height() > 0)
        logInfo() << "Video" << QStringLiteral("%1x%2 (%3) into %4x%5 (%6)")
                              .arg(w).arg(h).arg(double(w) / h, 0, 'f', 3)
                              .arg(int(item.width())).arg(int(item.height()))
                              .arg(item.width() / item.height(), 0, 'f', 3);
}

// Decoder chatter with nothing actionable in it; HE-AACv2 throws these every audio frame.
static bool isDecoderNoise(const QString &text) {
    static const char *ignored[] = {
        "Reserved SBR extensions is not implemented",
        "upload a sample of this file",
        "ffmpeg-devel",
        "illegal icc",
        "illegal iid",
        "border_position non monotone",
        "may have been wrapped",
    };
    for (const char *needle : ignored)
        if (text.contains(QLatin1String(needle), Qt::CaseInsensitive)) return true;
    return false;
}

void MpvPlayer::onLogMessage(const mpv_event *event) {
    if (!Settings::instance().mpvLogEnabled()) return;
    auto *msg = static_cast<mpv_event_log_message *>(event->data);
    static QString lastMsgText;
    auto msgText = QString::fromUtf8(msg->text).trimmed();
    if (msgText.isEmpty() || msgText == lastMsgText || isDecoderNoise(msgText)) return;
    lastMsgText = msgText;
    logRaw() << "MPV" << msgText;
}

void MpvPlayer::onPropertyChange(const mpv_event *event) {
    auto *prop = static_cast<mpv_event_property *>(event->data);
    if (prop->data == nullptr) return;

    const Mpv::Node &propValue = *static_cast<Mpv::Node *>(prop->data);
    if (propValue.type() == MPV_FORMAT_NONE) return;

    if (strcmp(prop->name, "playback-time") == 0) {
        int64_t newTime = static_cast<double>(propValue);
        if (newTime == m_time.load(std::memory_order_relaxed)) return;
        m_time.store(newTime, std::memory_order_relaxed);
        emit timeChanged();

        int64_t curTime = newTime;
        int64_t curDuration = m_duration.load(std::memory_order_relaxed);
        const int64_t edLen = hasED() ? m_aniEDLength : m_EDLength;
        const bool edWindow = edLen > 0 && edLen < curDuration && curTime > curDuration - edLen;
        // curDuration is 0 for live/duration-less streams, where this would fire on the first tick.
        if (!m_playNextEmitted && ((curDuration > 0 && curTime >= curDuration) ||
                (edWindow && (hasED() ? Settings::instance().get(Config::AniSkipAuto) : m_skipED)))) {
            m_playNextEmitted = true;
            emit playNext();
        } else {
            const int64_t opStart = hasOP() ? m_aniOPStart : m_OPStart;
            const int64_t opLen   = hasOP() ? m_aniOPLength : m_OPLength;
            if (opLen > 0 && curTime >= opStart && curTime < opStart + opLen && opStart + opLen <= curDuration
                    && (hasOP() ? Settings::instance().get(Config::AniSkipAuto) : m_skipOP)) {
                seek(opStart + opLen, true);
            }
        }
    }
    else if (strcmp(prop->name, "sub-delay") == 0) {
        // mpv's z/Z bindings move this too. The type check matters: operator double() asserts.
        if (propValue.type() == MPV_FORMAT_DOUBLE) {
            const double v = static_cast<double>(propValue);
            if (!qFuzzyCompare(m_subDelay + 1.0, v + 1.0)) { m_subDelay = v; emit subDelayChanged(); }
        }
    }
    else if (strcmp(prop->name, "duration") == 0) {
        m_duration.store(static_cast<int64_t>(static_cast<double>(propValue)), std::memory_order_relaxed);
        emit durationChanged();
        applyPendingSeek();
    }
    else if (strcmp(prop->name, "pause") == 0) {
        if (propValue && m_state == Playing) {
            m_state = Paused;
            allowDisplaySleep();
        } else if (!propValue && m_state == Paused) {
            m_state = Playing;
            keepDisplayAwake();
        }
        emit mpvStateChanged();
    }
    else if (strcmp(prop->name, "paused-for-cache") == 0) {
        if (propValue && m_state != Stopped)
            showText("Network is slow...");
    }
    else if (strcmp(prop->name, "base-idle") == 0) {
        if (propValue && m_state == Playing)
            showText("Pausing...");
    }
    else if (strcmp(prop->name, "aid") == 0) {
        if (propValue.type() == MPV_FORMAT_INT64)
            m_audioListModel.setCurrentId(static_cast<int64_t>(propValue));
    }
    else if (strcmp(prop->name, "sid") == 0) {
        const qint64 id = propValue.type() == MPV_FORMAT_INT64 ? static_cast<int64_t>(propValue) : 0;
        if (id != 0) m_subtitleListModel.setCurrentId(id);
        else m_subtitleListModel.setCurrentIndex(-1);
        if (m_primarySubId != id) { m_primarySubId = id; emit primarySubIdChanged(); applySubLayout(); }
    }
    else if (strcmp(prop->name, "secondary-sub-text") == 0) {
        const QString text = propValue.type() == MPV_FORMAT_STRING ? QString(propValue) : QString();
        if (const int lines = text.isEmpty() ? 0 : text.count(QChar(0x0a)) + 1; lines != m_secondarySubLines) {
            m_secondarySubLines = lines;
            applySubLayout();
        }
    }
    else if (strcmp(prop->name, "sub-scale") == 0) {
        if (propValue.type() == MPV_FORMAT_DOUBLE) {
            m_subScale = double(propValue);
            applySubLayout();
        }
    }
    else if (strcmp(prop->name, "secondary-sid") == 0) {
        const qint64 id = propValue.type() == MPV_FORMAT_INT64 ? static_cast<int64_t>(propValue) : 0;
        m_subtitleListModel.setSecondaryIndex(id != 0 ? m_subtitleListModel.indexForId(id) : -1);
        if (m_secondarySubId != id) { m_secondarySubId = id; emit secondarySubIdChanged(); applySubLayout(); }
    }
    else if (strcmp(prop->name, "vid") == 0) {
        if (propValue.type() == MPV_FORMAT_INT64)
            m_videoListModel.setCurrentId(static_cast<int64_t>(propValue));
    }
    else if (strcmp(prop->name, "track-list") == 0) {
        parseTrackList(propValue);
    }
    else if (strcmp(prop->name, "glsl-shaders") == 0) {
        bool clamp = false;
        if (propValue.type() == MPV_FORMAT_NODE_ARRAY) {
            for (int i = 0; i < propValue.size() && !clamp; ++i) {
                const Mpv::Node &s = propValue[i];
                if (s.type() == MPV_FORMAT_STRING)
                    clamp = strstr(static_cast<const char *>(s), "AutoDownscalePre") != nullptr;
            }
        }
        if (clamp != m_bandClamp.load(std::memory_order_relaxed)) {
            m_bandClamp.store(clamp, std::memory_order_relaxed);
            update();
        }
    }
}

// A native path, whose drive letter QUrl reads as a scheme - external tracks then keep a stale id.
static QUrl externalTrackUrl(const QString &raw) {
    const QUrl parsed(raw);
    if (parsed.scheme().size() > 1) return parsed;
    return QUrl::fromLocalFile(QDir::fromNativeSeparators(raw));
}

struct TrackInfo {
    QString type;                 // "video" | "audio" | "sub"
    int64_t id = -1;
    QString title, lang, codec;
    QUrl    url;                  // external-filename, when mpv autoloaded a sidecar
    int64_t w = 0, h = 0;
    int64_t channels = 0, bitrate = 0;
    double  fps = 0.0;
};

QString prettyCodec(const QString &codec) {
    if (codec.isEmpty()) return {};
    static const QMap<QString, QString> names{
        {"aac", "AAC"},   {"ac3", "AC3"},   {"eac3", "E-AC3"}, {"truehd", "TrueHD"},
        {"dts", "DTS"},   {"opus", "Opus"}, {"vorbis", "Vorbis"}, {"flac", "FLAC"},
        {"mp3", "MP3"},   {"subrip", "SRT"}, {"ass", "ASS"},   {"ssa", "SSA"},
        {"webvtt", "WebVTT"}, {"hdmv_pgs_subtitle", "PGS"}, {"dvd_subtitle", "VobSub"},
        {"mov_text", "Text"},
    };
    if (const QString name = names.value(codec.toLower()); !name.isEmpty()) return name;
    if (codec.startsWith(QLatin1String("pcm"), Qt::CaseInsensitive)) return QStringLiteral("PCM");
    return codec.toUpper();
}

QString channelLayout(int64_t channels) {
    switch (channels) {
    case 0:  return {};
    case 1:  return QStringLiteral("mono");
    case 2:  return QStringLiteral("stereo");
    case 6:  return QStringLiteral("5.1");
    case 8:  return QStringLiteral("7.1");
    default: return QString("%1 ch").arg(channels);
    }
}

// sub-auto=fuzzy and audio-file-auto=fuzzy pull in sidecar files that carry no title and no
// language, so their filename is the only thing that identifies them.
QString externalBaseName(const QUrl &url) {
    if (url.isEmpty()) return {};
    return QFileInfo(url.isLocalFile() ? url.toLocalFile() : url.path()).completeBaseName();
}

QString trackLabel(const TrackInfo &t) {
    const bool isVideo = t.type == QLatin1String("video");
    const QString name = !t.title.isEmpty() ? t.title : externalBaseName(t.url);
    QString label;

    if (isVideo) {
        QString resolution;
        if (t.w > 0 && t.h > 0) {
            resolution = QString("%1x%2").arg(t.w).arg(t.h);
            if (t.fps > 0) resolution += QString(" %1FPS").arg(t.fps);
        }
        if (resolution.isEmpty()) resolution = prettyCodec(t.codec);
        label = (!name.isEmpty() && !resolution.isEmpty()) ? QString("%1 [%2]").arg(name, resolution)
              : name.isEmpty()                             ? resolution
                                                           : name;
        if (!t.lang.isEmpty())
            label = label.isEmpty() ? t.lang : QString("%1 (%2)").arg(label, t.lang);
        if (t.bitrate > 0) {
            if (!label.isEmpty()) label += " - ";
            label += QString("%1 kbps").arg(t.bitrate / 1000);
        }
        return label.isEmpty() ? QString("Video %1").arg(t.id) : label;
    }

    if (!name.isEmpty()) {
        label = (t.lang.isEmpty() || name.compare(t.lang, Qt::CaseInsensitive) == 0)
                    ? name : QString("%1 [%2]").arg(name, t.lang);
    } else {
        // Nothing named it, so say what it is: "AAC stereo", "WebVTT".
        QStringList parts;
        if (const QString codec = prettyCodec(t.codec); !codec.isEmpty()) parts << codec;
        if (t.type == QLatin1String("audio"))
            if (const QString channels = channelLayout(t.channels); !channels.isEmpty()) parts << channels;
        const QString description = parts.join(QChar(' '));
        label = t.lang.isEmpty()       ? description
              : description.isEmpty()  ? t.lang
                                       : QString("%1 - %2").arg(t.lang, description);
    }

    // Subtitle bitrates are noise; only audio gets one.
    if (t.type == QLatin1String("audio") && t.bitrate > 0) {
        if (!label.isEmpty()) label += " - ";
        label += QString("%1 kbps").arg(t.bitrate / 1000);
    }
    if (!label.isEmpty()) return label;
    return t.type == QLatin1String("audio") ? QString("Audio %1").arg(t.id)
                                            : QString("Subtitle %1").arg(t.id);
}

void MpvPlayer::parseTrackList(const Mpv::Node &trackList) {
    for (const Mpv::Node &track : trackList) {
        try {
            QString trackType = static_cast<const char *>(track["type"]);
            TrackListModel *listModel = nullptr;

            if (trackType == "sub")        listModel = &m_subtitleListModel;
            else if (trackType == "audio") listModel = &m_audioListModel;
            else if (trackType == "video") listModel = &m_videoListModel;
            else continue;

            int64_t id = -1;
            QString title, lang, codec;
            QUrl url;
            double fps = 0.0;
            int64_t bitrate = 0, w = 0, h = 0, channels = 0;

            auto map = track.list();
            for (int i = 0; i < map->num; i++) {
                const char *key = map->keys[i];
                mpv_node &v = map->values[i];

                if (strcmp(key, "id") == 0 && v.format == MPV_FORMAT_INT64)
                    id = v.u.int64;
                else if (strcmp(key, "title") == 0 && v.format == MPV_FORMAT_STRING)
                    title = QString::fromUtf8(v.u.string);
                else if (strcmp(key, "lang") == 0 && v.format == MPV_FORMAT_STRING)
                    lang = QString::fromUtf8(v.u.string);
                else if (strcmp(key, "external-filename") == 0 && v.format == MPV_FORMAT_STRING)
                    url = externalTrackUrl(QString::fromUtf8(v.u.string));
                else if (strcmp(key, "demux-fps") == 0 && v.format == MPV_FORMAT_DOUBLE)
                    fps = v.u.double_;
                else if ((strcmp(key, "demux-bitrate") == 0 || strcmp(key, "hls-bitrate") == 0) && v.format == MPV_FORMAT_INT64) {
                    if (bitrate == 0) bitrate = v.u.int64;
                }
                else if (strcmp(key, "demux-w") == 0 && v.format == MPV_FORMAT_INT64)
                    w = v.u.int64;
                else if (strcmp(key, "demux-h") == 0 && v.format == MPV_FORMAT_INT64)
                    h = v.u.int64;
                else if (strcmp(key, "codec") == 0 && v.format == MPV_FORMAT_STRING)
                    codec = QString::fromUtf8(v.u.string);
                else if (strcmp(key, "demux-channel-count") == 0 && v.format == MPV_FORMAT_INT64)
                    channels = v.u.int64;
            }

            if (id <= 0) continue;

            if (listModel == &m_subtitleListModel && url.isLocalFile()) {
                const QString path = QDir::cleanPath(url.toLocalFile());
                if (auto it = m_externalSubIds.find(path); it != m_externalSubIds.end()) {
                    if (*it != id) {
                        *it = id;
                        emit externalSubsChanged();
                        if (m_pendingSubPath == path) {
                            setSubSlot(!m_pendingSubSecondary, id);
                            m_pendingSubPath.clear();
                        }
                    }
                    continue;
                }
            }

            if (!url.isEmpty())
                listModel->setId(url, id);

            listModel->setStats(id, static_cast<int>(h), fps, static_cast<int>(bitrate));

            // mpv names an untitled sidecar after the part of its filename the video does not
            // share - "srt" for Clip.srt, "eng.srt" for Clip.eng.srt. The extension carries no
            // meaning, so drop it; a title that was only the extension leaves nothing, and the
            // label then falls back to the filename and stays refreshable.
            if (!url.isEmpty() && !title.isEmpty()) {
                const QString suffix =
                    QFileInfo(url.isLocalFile() ? url.toLocalFile() : url.path()).suffix();
                if (!suffix.isEmpty()) {
                    if (title.compare(suffix, Qt::CaseInsensitive) == 0)
                        title.clear();
                    else if (title.endsWith(QLatin1Char('.') + suffix, Qt::CaseInsensitive))
                        title.chop(suffix.size() + 1);
                }
            }

            const TrackInfo info{trackType, id, title, lang, codec, url, w, h, channels, bitrate, fps};
            const bool derived = title.isEmpty();   // nothing authoritative to preserve
            const QString label = trackLabel(info);

            const int row = listModel->indexForId(id);
            if (row < 0) {
                listModel->append(id, label, lang, derived);
                listModel->setStats(id, static_cast<int>(h), fps, static_cast<int>(bitrate));
            } else if (!listModel->hasFinalTitle(id)) {
                // Derived: refresh it, since mpv may have measured the bitrate since.
                listModel->updateById(id, label, derived);
            }

        } catch (const std::exception &e) {
            logRaw() << "MPV" << e.what();
        }
    }
    m_videoListModel.sortByQuality(true);
    m_audioListModel.sortByQuality(false);

    // mpv can report vid/aid/sid before the tracks exist, and that pending id is lost on clear.
    auto syncSelection = [this](const char *prop, TrackListModel &model) {
        Mpv::Node v = m_mpv.get_property(prop);
        if (v.type() == MPV_FORMAT_INT64) model.setCurrentId(static_cast<int64_t>(v));
    };
    syncSelection("vid", m_videoListModel);
    syncSelection("aid", m_audioListModel);
    syncSelection("sid", m_subtitleListModel);

    restoreTrackPrefs();
}

void MpvPlayer::setMpvProperty(const QString &name, const QVariant &value) {
    QByteArray nameData = name.toLatin1();
    switch (value.typeId()) {
    case QMetaType::Bool:
        m_mpv.set_property_async(nameData.constData(), value.toBool());
        break;
    case QMetaType::Int:
    case QMetaType::Long:
    case QMetaType::LongLong:
        m_mpv.set_property_async(nameData.constData(), value.toLongLong());
        break;
    case QMetaType::Float:
    case QMetaType::Double:
        m_mpv.set_property_async(nameData.constData(), value.toDouble());
        break;
    case QMetaType::QByteArray: {
        QByteArray v = value.toByteArray();
        m_mpv.set_property_async(nameData.constData(), v.constData());
        break;
    }
    case QMetaType::QString: {
        QByteArray v = value.toString().toUtf8();
        m_mpv.set_property_async(nameData.constData(), v.constData());
        break;
    }
    }
}

void MpvPlayer::handleMpvError(int code) {
    if (code < 0) {
        if (m_lastMpvError == code) {
            stop();
            m_lastMpvError = MPV_ERROR_SUCCESS;
            return;
        }
        m_lastMpvError = code;
        AppShell::instance().reportError(mpv_error_string(code),
                                         QString("Mpv Error %1").arg(code));
    }
}

void MpvPlayer::showText(const QString &text) {
    QByteArray data = text.toUtf8();
    const char *args[] = {"show-text", data.constData(), nullptr};
    m_mpv.command_async(args);
}

QQuickFramebufferObject::Renderer *MpvPlayer::createRenderer() const {
    QQuickWindow *win = window();
    Q_ASSERT(win != nullptr);
    win->setPersistentGraphics(true);
    win->setPersistentSceneGraph(true);
    return new MpvRenderer(const_cast<MpvPlayer *>(this));
}

void MpvPlayer::setHeaders(const QMap<QString, QString> &headers) {
    m_mpv.set_property("referrer", "");
    m_mpv.set_property("user-agent", "");
    m_mpv.set_property("http-header-fields", "");
    m_mpv.set_property("stream-lavf-o", "");
    if (headers.isEmpty()) return;

    QStringList extra;
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        const QString key = it.key().toLower();
        if (key == "user-agent")
            m_mpv.set_property("user-agent", it.value().toUtf8().constData());
        else if (key == "referrer" || key == "referer")
            m_mpv.set_property("referrer", it.value().toUtf8().constData());
        // Pass headers via http-header-fields too - the UA/referrer options miss ffmpeg's HLS segments.
        extra << QStringLiteral("%1: %2").arg(it.key(), it.value());
    }
    if (!extra.isEmpty())
        m_mpv.set_property("http-header-fields", extra.join(",").toUtf8().constData());
}

void MpvPlayer::setSkipOP(bool skip) {
    m_skipOP = skip;
    emit skipOPChanged();
}

void MpvPlayer::setSkipED(bool skip) {
    m_skipED = skip;
    emit skipEDChanged();
}

void MpvPlayer::setOPStart(qint64 start) {
    m_OPStart = start;
    emit skipOPStartChanged();
}

void MpvPlayer::setOPLength(qint64 length) {
    m_OPLength = length;
    emit skipOPLengthChanged();
}

void MpvPlayer::setEDLength(qint64 length) {
    m_EDLength = length;
    emit skipEDLengthChanged();
}

void MpvPlayer::setAudioIndex(int index) {
    if (index < 0 || index >= m_audioListModel.count()) return;
    m_mpv.set_property_async("aid", m_audioListModel.idForIndex(index));
    m_audioListModel.setCurrentIndex(index);
    if (!m_applyingTrackPrefs) { m_audioPrefApplied = true; saveTrackPrefs(); }  // user took control
}

void MpvPlayer::setSubSlot(bool primary, qint64 id) {
    qint64 &slot = primary ? m_primarySubId : m_secondarySubId;
    qint64 &other = primary ? m_secondarySubId : m_primarySubId;
    if (slot == id) return;
    const qint64 previous = slot;

    // One track cannot fill both slots; taking it for one frees the other.
    if (id != 0 && other == id) setSubSlot(!primary, 0);

    slot = id;
    const char *property = primary ? "sid" : "secondary-sid";
    if (id == 0) m_mpv.set_property_async(property, "no");
    else         m_mpv.set_property_async(property, int64_t(id));

    if (primary) {
        m_subtitleListModel.setCurrentIndex(m_subtitleListModel.indexForId(id));
        emit primarySubIdChanged();
    } else {
        m_subtitleListModel.setSecondaryIndex(m_subtitleListModel.indexForId(id));
        emit secondarySubIdChanged();
    }

    if (id != 0 && !m_subVisible) setSubVisible(true);
    else if (m_primarySubId == 0 && m_secondarySubId == 0 && m_subVisible) setSubVisible(false);

    applySubLayout();
    if (m_applyingTrackPrefs) return;

    const QString path = pathForSubId(id);
    if (!path.isEmpty()) rememberEpisodeSub(path);
    if (primary) {
        // An explicit pick, so stop restoreTrackPrefs() nudging the slot on later track-list events.
        if (id != 0) m_subRestored = true;
        else if (!pathForSubId(previous).isEmpty()) rememberEpisodeSub({});
    }
    saveTrackPrefs();
}

void MpvPlayer::setPrimarySub(qint64 id)   { setSubSlot(true, id); }
void MpvPlayer::setSecondarySub(qint64 id) { setSubSlot(false, id); }

qint64 MpvPlayer::externalSubId(const QString &path) const {
    return m_externalSubIds.value(QDir::cleanPath(path), 0);
}

QString MpvPlayer::subNameForId(qint64 id) const {
    if (const int row = m_subtitleListModel.indexForId(id); row >= 0)
        if (const Track *track = m_subtitleListModel.at(row))
            return track->title.isEmpty() ? track->lang : track->title;
    const QString path = pathForSubId(id);
    return path.isEmpty() ? QString() : QFileInfo(path).completeBaseName();
}

void MpvPlayer::setSubIndex(int index, bool secondary) {
    if (index < 0 || index >= m_subtitleListModel.count()) return;
    setSubSlot(!secondary, m_subtitleListModel.idForIndex(index));
}

void MpvPlayer::useExternalSubtitle(const QString &path, const QString &title,
                                    const QString &lang, bool secondary) {
    if (path.isEmpty() || m_state == Stopped) return;
    const QString key = QDir::cleanPath(path);
    if (QFileInfo(key).size() <= 0) return;   // no placeholder for a file mpv cannot load
    if (const qint64 known = m_externalSubIds.value(key, 0); known != 0) {
        setSubSlot(!secondary, known);
        return;
    }
    m_externalSubIds.insert(key, 0);   // filled in when mpv reports the track
    m_pendingSubPath = key;
    m_pendingSubSecondary = secondary;
    emit externalSubsChanged();

    const QByteArray file = QDir::toNativeSeparators(key).toUtf8();
    const QByteArray t = title.toUtf8(), l = lang.toUtf8();
    // "auto", not "select": the slot is applied on the id, so the secondary never steals primary.
    const char *args[] = {"sub-add", file.constData(), "auto", t.constData(), l.constData(), nullptr};
    m_mpv.command_async(args);
}

QString MpvPlayer::pathForSubId(qint64 id) const {
    if (id == 0) return {};
    for (auto it = m_externalSubIds.constBegin(); it != m_externalSubIds.constEnd(); ++it)
        if (it.value() == id) return it.key();
    return {};
}

void MpvPlayer::rememberEpisodeSub(const QString &path) const {
    if (m_episodeKey.isEmpty()) return;
    Settings::instance().setValue(Config::episodeSub(m_episodeKey), path);
}

// Both grow upward, so the primary lifts clear of the secondary's whole block. Measured against
// libmpv, one line spans 5.3% of frame height per unit of sub-scale.
void MpvPlayer::applySubLayout() {
    int gap = 0;
    if (m_primarySubId != 0 && m_secondarySubId != 0) {
        const int lines = qMax(1, m_secondarySubLines);
        gap = int(std::ceil(m_subScale * 5.3 * lines));
    }
    m_mpv.set_property_async("sub-pos", int64_t(qMax(0, m_subPos - gap)));
    m_mpv.set_property_async("secondary-sub-pos", int64_t(m_subPos));
}

void MpvPlayer::setSubPos(int pos) {
    m_subPos = pos;
    applySubLayout();
}

void MpvPlayer::saveTrackPrefs() {
    if (m_applyingTrackPrefs || m_showKey.isEmpty()) return;
    const QString key = QStringLiteral("tracks/") + m_showKey;
    const QStringList saved = Settings::instance().value(key).toString().split(QChar(0x1f));
    // A fetched subtitle is not in the track model; its empty title would wipe the saved one.
    // A derived label is not a key either - it changes as mpv measures the stream - so it is
    // stored empty and the index below carries the selection.
    auto subTitle = [&](int field, int index, qint64 id) {
        if (!pathForSubId(id).isEmpty()) return saved.value(field);
        const Track *track = m_subtitleListModel.at(index);
        if (!track) return QString();
        return m_subtitleListModel.isDerivedTitle(m_subtitleListModel.idForIndex(index))
                   ? QString() : track->title;
    };
    const int audIdx = m_audioListModel.currentIndex();
    const Track *aud = m_audioListModel.at(audIdx);
    const QString audTitle =
        (aud && !m_audioListModel.isDerivedTitle(m_audioListModel.idForIndex(audIdx)))
            ? aud->title : QString();

    const QList<int> heights = m_videoListModel.heights();
    int vidIdx = m_videoListModel.currentIndex();
    int vidRes = (vidIdx >= 0 && vidIdx < heights.size()) ? heights[vidIdx] : -1;
    int vidWithin = 0;
    for (int i = 0; i < vidIdx && i < heights.size(); ++i)
        if (heights[i] == vidRes) vidWithin++;

    const QStringList parts = {
        subTitle(0, m_subtitleListModel.currentIndex(), m_primarySubId),
        audTitle,
        m_subVisible ? QStringLiteral("1") : QStringLiteral("0"),
        QString::number(vidRes),
        QString::number(vidWithin),
        QString::number(audIdx),
        subTitle(6, m_subtitleListModel.secondaryIndex(), m_secondarySubId),
        // Field 7: the subtitle's index, so an untitled track still restores. Readers guard on
        // size, so appending stays compatible with strings written by older builds.
        QString::number(m_subtitleListModel.currentIndex()),
    };
    Settings::instance().setValue(key, parts.join(QChar(0x1f)));
}

int MpvPlayer::pickVideoForPrefs(int savedRes, int savedWithin) const {
    const QList<int> heights = m_videoListModel.heights();
    if (heights.isEmpty() || savedRes < 0) return -1;
    int start = -1, count = 0;
    for (int i = 0; i < heights.size(); ++i)
        if (heights[i] == savedRes) { if (start < 0) start = i; ++count; }
    if (start >= 0) return start + qMin(savedWithin, count - 1);

    int best = 0, bestDiff = std::numeric_limits<int>::max();
    for (int i = 0; i < heights.size(); ++i) {
        int d = qAbs(heights[i] - savedRes);
        if (d < bestDiff) { bestDiff = d; best = i; }
    }
    return best;
}

// Exact title match (language dubs) first, then the saved rank (bitrate variants).
int MpvPlayer::pickAudioForPrefs(const QString &savedTitle, int savedRank) {
    if (!savedTitle.isEmpty())
        for (int i = 0; i < m_audioListModel.count(); ++i)
            if (m_audioListModel.at(i)->title == savedTitle) return i;
    if (savedRank >= 0 && savedRank < m_audioListModel.count()) return savedRank;
    return -1;
}

// On every track-list update, nudge mpv toward the saved selection until it sticks.
void MpvPlayer::restoreTrackPrefs() {
    if (!m_episodeKey.isEmpty() && !m_subRestored) {
        const QString saved = Settings::instance().value(Config::episodeSub(m_episodeKey)).toString();
        if (!saved.isEmpty() && QFileInfo(saved).size() > 0 && !m_externalSubIds.contains(saved)) {
            m_subRestored = true;
            useExternalSubtitle(saved, QFileInfo(saved).completeBaseName(), {});
        }
    }

    if (m_showKey.isEmpty() || (m_subRestored && m_videoPrefApplied && m_audioPrefApplied)) return;
    const QString pref = Settings::instance().value("tracks/" + m_showKey).toString();
    if (pref.isEmpty()) { m_subRestored = m_videoPrefApplied = m_audioPrefApplied = true; return; }
    const QStringList p = pref.split(QChar(0x1f));

    if (!m_videoPrefApplied) {
        int target = (p.size() >= 5) ? pickVideoForPrefs(p[3].toInt(), p[4].toInt()) : -1;
        if (target < 0 || m_videoListModel.currentIndex() == target) {
            m_videoPrefApplied = true;
        } else {
            int64_t id = m_videoListModel.idForIndex(target);
            if (id > 0) m_mpv.set_property_async("vid", id);
        }
    }

    if (!m_audioPrefApplied) {
        int target = (p.size() >= 6) ? pickAudioForPrefs(p[1], p[5].toInt())
                   : (p.size() >= 2) ? pickAudioForPrefs(p[1], -1) : -1;
        if (target < 0 || m_audioListModel.currentIndex() == target) {
            m_audioPrefApplied = true;
        } else {
            int64_t id = m_audioListModel.idForIndex(target);
            if (id > 0) m_mpv.set_property_async("aid", id);
        }
    }

    if (!m_subRestored && p.size() >= 3) {
        m_applyingTrackPrefs = true;
        bool subFound = false;
        if (!p[0].isEmpty()) {
            for (int i = 0; i < m_subtitleListModel.count(); ++i)
                if (m_subtitleListModel.at(i)->title == p[0]) { setSubIndex(i); subFound = true; break; }
        } else if (p.size() >= 8) {
            // No title to match on, so the index is the selection.
            const int idx = p[7].toInt();
            if (idx >= 0 && idx < m_subtitleListModel.count()) { setSubIndex(idx); }
            subFound = true;
        } else {
            subFound = true;   // nothing was saved
        }
        if (p.size() >= 7 && !p[6].isEmpty())
            for (int i = 0; i < m_subtitleListModel.count(); ++i)
                if (m_subtitleListModel.at(i)->title == p[6]) { setSecondarySub(m_subtitleListModel.idForIndex(i)); break; }
        setSubVisible(p[2] == "1");
        m_applyingTrackPrefs = false;
        if (subFound) m_subRestored = true;
    } else if (p.size() < 3) {
        m_subRestored = true;
    }
}

void MpvPlayer::setVideoIndex(int index) {
    if (index < 0 || index >= m_videoListModel.count()) return;
    m_mpv.set_property_async("vid", m_videoListModel.idForIndex(index));
    m_videoListModel.setCurrentIndex(index);
    auto *track = m_videoListModel.at(index);
    if (track && !track->url.isEmpty())
        m_currentVideoUrl = track->url;
    if (!m_applyingTrackPrefs) { m_videoPrefApplied = true; saveTrackPrefs(); }  // user took control
}

void MpvPlayer::setMuted(bool muted) {
    if (m_muted == muted) return;
    if (muted) {
        m_lastVolume = m_volume;
        setVolume(0);
    } else {
        setVolume(m_lastVolume);
    }
    m_muted = muted;
    emit mutedChanged();
}
