#include "mpvObject.h"
#include <QDebug>
#include <QDir>
#include <QMetaType>
#include <QOpenGLContext>
#include <QSettings>
#include <QStandardPaths>
#include <clocale>
#include <stdexcept>

#include <QStringList>
#include <windows.h>
#include "utils/errorhandler.h"
#include <QQuickOpenGLUtils>
#include <QtOpenGL/QOpenGLFramebufferObject>
#include <stdlib.h>

/* MPV Renderer */
class MpvRenderer : public QQuickFramebufferObject::Renderer {
    MpvObject *m_obj;

public:
    MpvRenderer(MpvObject *obj) : m_obj(obj) {}

    // This function is called when a new FBO is needed.
    // This happens on the initial frame.
    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) {
        Q_ASSERT(m_obj != nullptr);
        // init mpv_gl
        if (!m_obj->m_mpv.renderer_initialized()) {
            mpv_opengl_init_params gl_init_params {
                [](void *, const char *name) -> void * {
                    QOpenGLContext *glctx = QOpenGLContext::currentContext();
                    return glctx ? reinterpret_cast<void *>(
                                       glctx->getProcAddress(QByteArray(name)))
                                 : nullptr;
                }
#if MPV_CLIENT_API_VERSION < MPV_MAKE_VERSION(2, 0)
                ,
                nullptr, nullptr
#endif
            };

            mpv_render_param params[]{
                                      {MPV_RENDER_PARAM_API_TYPE,
                                       const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
                                      {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
                                      {MPV_RENDER_PARAM_INVALID, nullptr}};

            if (m_obj->m_mpv.renderer_initialize(params) < 0)
                throw std::runtime_error("failed to initialize mpv GL context");

            m_obj->m_mpv.set_render_callback(
                [](void *ctx) {
                    MpvObject *obj = static_cast<MpvObject *>(ctx);
                    QMetaObject::invokeMethod(obj, "update", Qt::QueuedConnection);
                },
                m_obj);
        }
        return QQuickFramebufferObject::Renderer::createFramebufferObject(size);
    }

    void render() {
        if (!m_obj->isVisible() || m_obj->isResizing()) return;
        Q_ASSERT(m_obj != nullptr);
        Q_ASSERT(m_obj->window() != nullptr);

        QQuickOpenGLUtils::resetOpenGLState();

        QOpenGLFramebufferObject *fbo = framebufferObject();
        Q_ASSERT(fbo != nullptr);

        mpv_opengl_fbo mpfbo{static_cast<int>(fbo->handle()), fbo->width(),
                             fbo->height(), 0};
        int flip_y = 0;

        mpv_render_param params[] = {{MPV_RENDER_PARAM_OPENGL_FBO, &mpfbo},
                                     {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
                                     {MPV_RENDER_PARAM_INVALID, nullptr}};
        m_obj->m_mpv.render(params);

        QQuickOpenGLUtils::resetOpenGLState();
    }
};

MpvObject *MpvObject::s_instance = nullptr;

MpvObject::MpvObject(QQuickItem *parent) : QQuickFramebufferObject(parent) {
    Q_ASSERT(s_instance == nullptr);
    s_instance = this;

    m_time = m_duration = 0;
    m_volume = 100;

    // Access settings
    QSettings settings;

    // set mpv options
    // m_mpv.set_option("ytdl", false);     // We handle video url parsing
    m_mpv.set_option("pause", false);    // Always play when a new file is opened
    m_mpv.set_option("softvol", true);   // mpv handles the volume
    m_mpv.set_option("vo", "libmpv");    // Force to use libmpv
    m_mpv.set_option("keep-open", true); // Keeps the video open after EOF
    m_mpv.set_option("screenshot-directory", QStandardPaths::writableLocation(
                                                 QStandardPaths::PicturesLocation)
                                                 .toUtf8()
                                                 .constData());
    m_mpv.set_option("reset-on-next-file",
                     "video-aspect-override,af,audio-delay,pause");
    m_mpv.set_option("hwdec", "auto"); // Hardware acceleration
    m_mpv.set_option("cache", "yes");
    m_mpv.set_option("cache-secs", "100");
    m_mpv.set_option("cache-unlink-files", "whendone");
    m_mpv.set_option("config", "yes");
    m_mpv.set_option ("msg-level", "all=error");
    m_mpv.set_option ("osd-font-size", "40");

    m_mpv.observe_property("duration");
    m_mpv.observe_property("playback-time");
    m_mpv.observe_property("paused-for-cache");
    m_mpv.observe_property("core-idle");
    m_mpv.observe_property("pause");
    m_mpv.observe_property("track-list");
    m_mpv.request_log_messages("warn");

    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QDir mpvDir(appDataPath + "/mpv");

    if (mpvDir.exists()) {
        m_mpv.set_option("config-dir", mpvDir.absolutePath().toLocal8Bit().constData());
        auto inputConfPath = mpvDir.absoluteFilePath("input.conf");
        if (QFileInfo::exists(inputConfPath)) {
            m_mpv.set_option("input-conf", inputConfPath.toLocal8Bit().constData());
        }
    }


    // Configure cache
    if (settings.value(QStringLiteral("network/limit_cache"), false).toBool()) {
        int64_t forwardBytes =
            settings.value(QStringLiteral("network/forward_cache")).toLongLong()
            << 20;
        int64_t backwardBytes =
            settings.value(QStringLiteral("network/backward_cache")).toLongLong()
            << 20;
        m_mpv.set_option("demuxer-max-bytes", forwardBytes);
        m_mpv.set_option("demuxer-max-back-bytes", backwardBytes);
    }

    if (QSysInfo::productVersion() == QStringLiteral("8.1") ||
        QSysInfo::productVersion() == QStringLiteral("10") ||
        QSysInfo::productVersion() == QStringLiteral("11")) {
        m_mpv.set_option("hwdec", "d3d11va");
        m_mpv.set_option("gpu-context", "d3d11");
    } else {
        m_mpv.set_option("hwdec", "dxva2");
        m_mpv.set_option("gpu-context", "dxinterop");
    }

    if (m_mpv.initialize() < 0)
        throw std::runtime_error("could not initialize mpv context");

    // Set update callback
    m_mpv.set_wakeup_callback(
        [](void *ctx) {
            MpvObject *obj = static_cast<MpvObject *>(ctx);
            QMetaObject::invokeMethod(obj, "onMpvEvent", Qt::QueuedConnection);
        },
        this);

    loadAnime4K(4);
}

// Open file
void MpvObject::open(const Video &video, int time) {

    m_isLoading = true;
    emit isLoadingChanged();

    m_state = STOPPED;
    emit mpvStateChanged();

    auto headers = video.getHeaders();
    if (!headers.isEmpty()) {
        for(auto it = headers.begin(); it != headers.end(); ++it) {
            if (it.key().toLower()=="user-agent") {
                m_mpv.set_property_async("user-agent", it.value().toUtf8().constData());
            } else {
                m_mpv.set_property_async("http-header-fields", it.value().toUtf8().constData());
            }
        }
    } else {
        m_mpv.set_property_async("http-header-fields", "");
    }
    m_seekTime = time;
    QByteArray fileUrl = (video.videoUrl.isLocalFile() ? video.videoUrl.toLocalFile() : video.videoUrl.toString()).toUtf8();
    const char *args[] = {"loadfile", fileUrl.constData(), nullptr};
    m_mpv.command_async(args);


    if (video.videoUrl != m_currentVideo.videoUrl){
        m_currentVideo = video;
    }


}

// Play, Pause, Stop & Get state
void MpvObject::play() {
    if (m_state == VIDEO_PAUSED) {
        m_mpv.set_property_async("pause", false);
    }
}

void MpvObject::pause() {
    if (m_state == VIDEO_PLAYING) {
        m_mpv.set_property_async("pause", true);
    }
}

void MpvObject::stop() {
    if (m_state != STOPPED) {
        const char *args[] = {"stop", nullptr};
        m_mpv.command_async(args);
    }
}

void MpvObject::mute() {
    if (m_volume > 0) {
        m_lastVolume = m_volume;
        setVolume(0);
    } else {
        setVolume(m_lastVolume);
    }
}

void MpvObject::setSpeed(float speed) {
    if (m_speed == speed)
        return;
    m_speed = speed;
    m_mpv.set_property_async("speed", static_cast<double>(speed));
    showText(QByteArrayLiteral("speed: ") + QByteArray::number(speed));
    emit speedChanged();
}

// Seek
void MpvObject::seek(qint64 time, bool absolute) {
    if (m_state != STOPPED && time != m_time) {
        if (absolute && time <= 0)
            time = 0;
        QByteArray time_str = QByteArray::number(time);
        const char *args[] = {"seek", time_str.constData(),
                              (absolute ? "absolute" : "relative"), nullptr};
        m_mpv.command_async(args);
    }
}

// Set volume
void MpvObject::setVolume(int volume) {
    if (m_volume == volume)
        return;
    m_volume = volume;
    m_mpv.set_property_async("volume", static_cast<double>(volume));
    showText(QByteArrayLiteral("Volume: ") + QByteArray::number(volume));
    emit volumeChanged();
}

// Set subtitle visibility
void MpvObject::setSubVisible(bool subVisible) {
    if (m_subVisible == subVisible)
        return;
    m_subVisible = subVisible;
    m_mpv.set_property_async("sub-visibility", m_subVisible);

    emit subVisibleChanged();
}

// Add audio track
void MpvObject::addAudioTrack(const QUrl &url) {
    if (m_state == STOPPED)
        return;
    QByteArray uri_str =
        (url.isLocalFile() ? url.toLocalFile() : url.toString()).toUtf8();
    const char *args[] = {"audio-add", uri_str.constData(), "select", nullptr};
    m_mpv.command_async(args);
}

// Add subtitle
void MpvObject::addSubtitle(const QUrl &url) {
    if (m_state == STOPPED)
        return;
    QByteArray uri_str =
        (url.isLocalFile() ? url.toLocalFile() : url.toString()).toUtf8();
    const char *args[] = {"sub-add", uri_str.constData(), "select", nullptr};
    m_mpv.command_async(args);
}

// Take screenshot
void MpvObject::screenshot() {
    if (m_state == STOPPED)
        return;
    const char *args[] = {"osd-msg", "screenshot", nullptr};
    m_mpv.command_async(args);
}

void MpvObject::onMpvEvent() {

    while (true) {
        const mpv_event *event = m_mpv.wait_event();
        if (event == NULL)
            break;
        if (event->event_id == MPV_EVENT_NONE)
            break;

        switch (event->event_id) {
        case MPV_EVENT_START_FILE:
            m_videoWidth = m_videoHeight = 0; // Set videoSize invalid
            m_time = 0;
            m_subVisible = true;
            emit timeChanged();
            emit subVisibleChanged();
            break;

        case MPV_EVENT_FILE_LOADED:
            m_state = VIDEO_PLAYING;
            SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED);
            if (m_seekTime > 0) {
                seek(m_seekTime, true);
                m_seekTime = 0;
            }
            m_isLoading = false;
            emit isLoadingChanged();
            emit mpvStateChanged();
            break;

        case MPV_EVENT_END_FILE: {
            mpv_event_end_file *ef = static_cast<mpv_event_end_file *>(event->data);
            handleMpvError(ef->error);
            m_endFileReason = static_cast<mpv_end_file_reason>(ef->reason);
            if (m_isLoading){
                m_isLoading = false;
                emit isLoadingChanged();
            }
            break;
        }

        case MPV_EVENT_IDLE: {
            m_state = STOPPED;
            emit mpvStateChanged();
            break;
        }

        case MPV_EVENT_VIDEO_RECONFIG: {
            Mpv::Node width = m_mpv.get_property("dwidth");
            Mpv::Node height = m_mpv.get_property("dheight");

            if (width.type() != MPV_FORMAT_NONE) {
                m_videoWidth = width;
                m_videoHeight = height;
                emit videoSizeChanged();

                // Load audio track
                if (!m_audioToBeAdded.isEmpty()) {
                    addAudioTrack(m_audioToBeAdded);
                    m_audioToBeAdded = QUrl();
                }
            }
            break;
        }

        case MPV_EVENT_LOG_MESSAGE: {
            // mpv_event_log_message *msg = static_cast<mpv_event_log_message *>(event->data);
            // fprintf(stderr, "[%s] %s", msg->prefix, msg->text); //TODO
            break;
        }

        case MPV_EVENT_PROPERTY_CHANGE: {
            mpv_event_property *prop = (mpv_event_property *)event->data;

            if (prop->data == nullptr) {
                break;
            }

            const Mpv::Node &propValue = *static_cast<Mpv::Node *>(prop->data);
            if (propValue.type() == MPV_FORMAT_NONE) {
                break;
            }

            if (strcmp(prop->name, "playback-time") == 0) {
                int64_t newTime = static_cast<double>(propValue);
                if (newTime != m_time) {
                    m_time = newTime;
                    emit timeChanged();
                    if (m_time == m_duration){
                        emit playNext();
                    } else if (m_shouldSkipOP && m_time < m_OPEnd && m_time >= m_OPStart){
                        seek(m_OPEnd,true);
                    } else if (m_shouldSkipED && m_time < m_EDEnd && m_time >= m_EDStart){
                        seek(m_EDEnd, true);
                    }

                }
            }

            else if (strcmp(prop->name, "duration") == 0) {
                m_duration = static_cast<double>(propValue);
                emit durationChanged();
            }

            else if (strcmp(prop->name, "pause") == 0) {
                if (propValue && m_state == VIDEO_PLAYING) {
                    m_state = VIDEO_PAUSED;
                    SetThreadExecutionState(ES_CONTINUOUS);
                } else if (!propValue && m_state == VIDEO_PAUSED) {
                    m_state = VIDEO_PLAYING;
                    SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED);
                }
                emit mpvStateChanged();
            }

            else if (strcmp(prop->name, "paused-for-cache") == 0) {
                if (propValue && m_state != STOPPED) {
                    showText(QByteArrayLiteral("Network is slow..."));
                } else {
                    showText(QByteArrayLiteral(""));
                }
            }

            else if (strcmp(prop->name, "core-idle") == 0) {
                if (propValue && m_state == VIDEO_PLAYING) {
                    showText(QByteArrayLiteral("Pausing..."));
                } else {
                    showText(QByteArrayLiteral(""));
                }
            }

            else if (strcmp(prop->name, "track-list") == 0) // Read tracks info
            {
                m_subtitles.clear();
                m_audioTracks.clear();
                for (const auto &track : propValue) {
                    try {
                        if (track["type"] == "sub") // Subtitles
                        {
                            int64_t id = track["id"];
                            QString title;
                            try {
                                title = QString::fromUtf8(
                                    static_cast<const char *>(track["title"]));
                            } catch (std::exception &) {
                                title = tr("Untitled ") + QString::number(id);
                            }

                            if (m_subtitles.count() <= id) {
                                for (int j = m_subtitles.count(); j < id; j++)
                                    m_subtitles.append(QLatin1Char('#') + QString::number(j));
                                m_subtitles.append(title.isEmpty()
                                                       ? QLatin1Char('#') + QString::number(id)
                                                       : title);
                            } else {
                                m_subtitles[id] = title.isEmpty()
                                ? QLatin1Char('#') + QString::number(id)
                                : title;
                            }
                        }

                        else if (track["type"] == "audio") // Audio tracks
                        {
                            int64_t id = track["id"];
                            QString title;
                            try {
                                title = QString::fromUtf8(
                                    static_cast<const char *>(track["title"]));
                            } catch (std::exception &) {
                                title = tr("Untitled ") + QString::number(id);
                            }

                            if (m_audioTracks.count() <= id) {
                                for (int j = m_audioTracks.count(); j < id; j++)
                                    m_audioTracks.append(QLatin1Char('#') + QString::number(j));
                                m_audioTracks.append(title.isEmpty() ? QLatin1Char('#') +
                                                                           QString::number(id)
                                                                     : title);
                            } else {
                                m_audioTracks[id] = title.isEmpty()
                                ? QLatin1Char('#') + QString::number(id)
                                : title;
                            }
                        }
                    } catch (const std::exception &) {
                        continue;
                    }
                }
                emit subtitlesChanged();
                emit audioTracksChanged();
            }
            break;
        }

        default:
            break;
        }
    }
}

// setProperty() exposed to QML
void MpvObject::setProperty(const QString &name, const QVariant &value) {
    switch ((int)value.typeId()) {
    case (int)QMetaType::Bool: {
        bool v = value.toBool();
        m_mpv.set_property_async(name.toLatin1().constData(), v);
        break;
    }
    case (int)QMetaType::Int:
    case (int)QMetaType::Long:
    case (int)QMetaType::LongLong: {
        int64_t v = value.toLongLong();
        m_mpv.set_property_async(name.toLatin1().constData(), v);
        break;
    }
    case (int)QMetaType::Float:
    case (int)QMetaType::Double: {
        double v = value.toDouble();
        m_mpv.set_property_async(name.toLatin1().constData(), v);
        break;
    }
    case (int)QMetaType::QByteArray: {
        QByteArray v = value.toByteArray();
        m_mpv.set_property_async(name.toLatin1().constData(), v.constData());
        break;
    }
    case (int)QMetaType::QString: {
        QByteArray v = value.toString().toUtf8();
        m_mpv.set_property_async(name.toLatin1().constData(), v.constData());
        break;
    }
    }
}

void MpvObject::handleMpvError(int code) {
    if (code < 0) {
        QString errorString = mpv_error_string(code);
        static bool wasLoadingFailed = false;
        if (wasLoadingFailed && code == MPV_ERROR_LOADING_FAILED){
            return;
        }
        wasLoadingFailed = code == MPV_ERROR_LOADING_FAILED;
        ErrorHandler::instance().show(errorString, QString("Mpv Error"));
    }
}

void MpvObject::showText(const QByteArray &text) {
    const char *args[] = {"show-text", text.constData(), nullptr};
    m_mpv.command_async(args);
}

QQuickFramebufferObject::Renderer *MpvObject::createRenderer() const {
    QQuickWindow *win = window();
    Q_ASSERT(win != nullptr);
    win->setPersistentGraphics(true);
    win->setPersistentSceneGraph(true);
    return new MpvRenderer(const_cast<MpvObject *>(this));
}
