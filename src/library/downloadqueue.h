#pragma once

#include <QAbstractListModel>
#include <QDir>
#include <QProcess>
#include <QCoreApplication>
#include <QMutex>
#include <QThreadPool>
#include <QSharedPointer>
#include <atomic>

#include "net/canceltoken.h"
#include "media/playinfo.h"
#include <qqmlintegration.h>

class ShowData;
class PlaylistItem;
class ShowProvider;
class Client;

class DownloadTask : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by DownloadQueue; QML uses it for Status.")
public:
    DownloadTask(const QString &videoName, const QString &folder, const QString &link,
                 const QString &displayName, const QMap<QString, QString> &headers = {});

    DownloadTask(QSharedPointer<PlaylistItem> episode, ShowProvider *provider, const QString &workDir);

    ~DownloadTask() override = default;

    enum Status { Queued, Running, Paused, Failed };
    Q_ENUM(Status)

    struct SubtitleFile {
        QString path;
        QString lang;    // ISO code, or empty when the provider gave none
        QString name;
        bool    owned;   // we fetched it, so we delete it; a provider's own local file we never touch
    };

    QString videoName;
    QString folder;
    QString link;
    QString audioLink;   // set when the source has a separate audio stream (e.g. Bilibili DASH)
    QMap<QString, QString> headers;
    QString displayName;
    QString maxSpeed;   // read on the GUI thread in startTasks(); QSettings is not thread-safe
    QList<SubtitleFile> subtitleFiles;

    // The dedup key: extension-less, because the container is only known once extraction has run.
    QString basePath() const { return QDir::cleanPath(folder + "/" + videoName); }
    QString path() const     { return basePath() + (useMkv() ? ".mkv" : ".mp4"); }
    // ffmpeg picks the container from this suffix, so it has to track path().
    QString partPath() const { return basePath() + (useMkv() ? ".part.mkv" : ".part.mp4"); }
    // Private scratch, so a cancelled download leaves nothing in the show folder.
    QString tmpDir() const   { return basePath() + QStringLiteral(".tmp"); }

    QStringList toolArguments() const;
    // Separate video+audio (Bilibili) isn't a manifest N_m3u8DL-RE can take - ffmpeg muxes both.
    bool usesFfmpeg() const { return !audioLink.isEmpty(); }
    bool useMkv() const { return m_useMkv.load(std::memory_order_acquire); }
    QString program() const { return usesFfmpeg() ? ffmpegPath() : toolPath(); }
    QStringList ffmpegArguments() const;
    QString extractLink();   // empty on failure
    QString extractLinkInner();
    // Worker thread, once per run, before the arguments are built: fixes the container so the
    // argument list and path() can never disagree.
    void prepareSubtitles();
    void discardSubtitles();

    // A rename that failed left the only copy in the .part file - don't delete it.
    std::atomic<bool> keepPart{false};

    int progressValue() const { return m_progressValue; }
    QString progressText() const { return m_progressText; }
    QString stats() const { return m_stats; }
    void setProgressValue(int value);
    void setProgressText(const QString &text);
    void setSpeed(const QString &speed);
    void resetStats();

    int  status() const { return m_status.load(); }
    void setStatus(int s) { m_status.store(s); }

    bool isCancelled() const { return m_cancel.isCancelled(); }
    void cancel() { m_cancel.cancel(); }
    bool isPaused() const { return m_isPaused.load(); }
    void setPaused(bool p) { m_isPaused = p; }
    QProcess *process() const { return m_process.load(std::memory_order_acquire); }
    void setProcess(QProcess *proc) { m_process.store(proc, std::memory_order_release); }

    static bool checkDependencies();
    static QString toolPath()   { ensurePaths(); return s_m3u8dlPath; }
    static QString ffmpegPath() { ensurePaths(); return s_ffmpegPath; }

private:
    void rebuildStats();
    void fetchSubtitles(Client &client, const QList<Track> &tracks);
    static QString formatEta(int seconds);

    static void ensurePaths() {
        if (!s_m3u8dlPath.isEmpty()) return;
        QString appDir = QCoreApplication::applicationDirPath();
        s_m3u8dlPath = QDir::cleanPath(appDir + "/N_m3u8DL-RE.exe");
        s_ffmpegPath = QDir::cleanPath(appDir + "/ffmpeg.exe");
    }

    static inline QString s_m3u8dlPath;
    static inline QString s_ffmpegPath;

    CancelToken       m_cancel;
    std::atomic<bool> m_isPaused{false};
    std::atomic<bool> m_useMkv{false};
    std::atomic<int>  m_status{Queued};
    std::atomic<QProcess*> m_process{nullptr};
    int m_progressValue = 0;
    QString m_progressText = QStringLiteral("Awaiting to start...");

    QString m_stats;
    QString m_speed;
    QString m_etaText;
    qint64  m_startTimeMs = 0;
    int     m_startProgress = 0;   // progress when the clock started; a resume does not begin at 0

    // Only used for episode downloads (cleared after extractLink)
    QSharedPointer<PlaylistItem> m_episode;
    ShowProvider *m_provider = nullptr;
};

class DownloadQueue : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(int maxDownloads READ maxDownloads WRITE setMaxDownloads NOTIFY maxDownloadsChanged)
public:
    enum Role { NameRole = Qt::UserRole, PathRole, ProgressValueRole, ProgressTextRole, StatusRole, StatsRole };

    explicit DownloadQueue(QObject *parent = nullptr);
    ~DownloadQueue() override { cancelAllTasks(); m_threadPool.waitForDone(); }

    Q_INVOKABLE void downloadLink(const QString &name, const QString &link);
    void downloadShow(const ShowData &show, int startIndex, int endIndex);
    static QString cleanFolderName(const QString &name);
    Q_INVOKABLE void cancelTask(int index);
    Q_INVOKABLE void pauseTask(int index);
    Q_INVOKABLE void resumeTask(int index);   // also used to retry a failed task
    Q_INVOKABLE void pauseAll();
    Q_INVOKABLE void resumeAll();
    Q_INVOKABLE void cancelAll() { cancelAllTasks(); }
    void cancelAllTasks();

    int maxDownloads() const;
    void setMaxDownloads(int newMaxDownloads);

    int count() const { return m_tasks.count(); }
    DownloadTask *taskAt(int i) const {
        return (i >= 0 && i < m_tasks.size()) ? m_tasks.at(i).data() : nullptr;
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void maxDownloadsChanged();

private:
    void startTasks();
    void runTask(QSharedPointer<DownloadTask> task);
    // force: the worker has finished with the task, so drop the row instead of asking it to stop.
    void removeTask(const QSharedPointer<DownloadTask> &task, bool force = false);
    void emitRowChanged(int row);   // safe from any thread
    int  rowOf(const QSharedPointer<DownloadTask> &task) const;

    int m_maxDownloads = 4;
    std::atomic<int> m_currentConcurrentDownloads{0};
    mutable QMutex m_mutex;   // guards the three containers below against the worker threads
    QSet<QString> m_ongoingPaths;
    QList<QSharedPointer<DownloadTask>> m_taskQueue;
    QList<QSharedPointer<DownloadTask>> m_tasks;
    QThreadPool m_threadPool;
};
