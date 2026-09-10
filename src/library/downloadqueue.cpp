#include "library/downloadqueue.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QDateTime>
#include <QRegularExpression>
#include "media/playlistitem.h"
#include "providers/showprovider.h"
#include "ui/appshell.h"
#include "app/logger.h"
#include "media/serverselector.h"
#include "app/settings.h"
#include "app/exception.h"
#include "net/client.h"
#include <QCryptographicHash>
#include <QSaveFile>

DownloadTask::DownloadTask(const QString &videoName, const QString &folder, const QString &link,
                           const QString &displayName, const QMap<QString, QString> &headers)
    : videoName(videoName), folder(folder), link(link), headers(headers), displayName(displayName)
{}

DownloadTask::DownloadTask(QSharedPointer<PlaylistItem> episode, ShowProvider *provider, const QString &workDir)
    : m_episode(episode), m_provider(provider)
{
    if (episode && episode->parent()) {
        QString showName = episode->parent()->name;
        videoName = DownloadQueue::cleanFolderName(episode->displayName.trimmed().replace("\n", ". "));
        displayName = showName + " : " + videoName;
        folder = workDir;
    }
}

bool DownloadTask::checkDependencies() {
    ensurePaths();
    return QFile::exists(s_m3u8dlPath) && QFile::exists(s_ffmpegPath);
}

// ':' separates the option fields, so a Windows drive letter has to be escaped.
static QString muxEscape(const QString &value) {
    QString escaped = value;
    escaped.replace(QLatin1Char(':'), QLatin1String("\\:"));
    return escaped;
}

QStringList DownloadTask::toolArguments() const {
    QStringList args {
        link,
        "--save-dir", folder,
        "--tmp-dir", tmpDir(),
        "--save-name", videoName,
        "--ffmpeg-binary-path", s_ffmpegPath,
        "--del-after-done", "--no-date-info", "--no-log",
        "--auto-select", "--no-ansi-color"
    };
    if (!maxSpeed.isEmpty())
        args << "--max-speed" << maxSpeed;
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
        args << "-H" << (it.key() + ": " + it.value());

    if (!subtitleFiles.isEmpty()) {
        // bin_path explicitly: the bundled ffmpeg is not on PATH.
        args << "-M" << ("format=mkv:muxer=ffmpeg:bin_path="
                         + muxEscape(QDir::toNativeSeparators(s_ffmpegPath)));
        for (const SubtitleFile &sub : subtitleFiles) {
            QString option = "path=" + muxEscape(QDir::toNativeSeparators(sub.path));
            if (!sub.lang.isEmpty()) option += ":lang=" + sub.lang;
            if (!sub.name.isEmpty()) option += ":name=" + sub.name;
            args << "--mux-import" << option;
        }
    }
    return args;
}

QStringList DownloadTask::ffmpegArguments() const {
    // ffmpeg -headers applies to the input that follows it, so emit it before each.
    QString headerBlock;
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
        headerBlock += it.key() + ": " + it.value() + "\r\n";

    QStringList args { "-y", "-hide_banner" };
    if (!headerBlock.isEmpty()) args << "-headers" << headerBlock;
    args << "-i" << link;
    if (!headerBlock.isEmpty()) args << "-headers" << headerBlock;
    args << "-i" << audioLink;
    for (const SubtitleFile &sub : subtitleFiles)
        args << "-i" << QDir::toNativeSeparators(sub.path);

    args << "-map" << "0:v:0" << "-map" << "1:a:0";
    // Whole input, not <n>:s:0 - a mis-sniffed file would otherwise abort the mux.
    for (int i = 0; i < subtitleFiles.size(); ++i)
        args << "-map" << QString::number(2 + i);

    args << "-c" << "copy";
    for (int i = 0; i < subtitleFiles.size(); ++i) {
        const SubtitleFile &sub = subtitleFiles[i];
        const bool styled = sub.path.endsWith(".ass", Qt::CaseInsensitive)
                         || sub.path.endsWith(".ssa", Qt::CaseInsensitive);
        // WebVTT cannot be copied into Matroska; SubRip is the lossless-enough target.
        args << QStringLiteral("-c:s:%1").arg(i) << (styled ? "copy" : "srt");
        if (!sub.lang.isEmpty())
            args << QStringLiteral("-metadata:s:s:%1").arg(i) << ("language=" + sub.lang);
        if (!sub.name.isEmpty())
            args << QStringLiteral("-metadata:s:s:%1").arg(i) << ("title=" + sub.name);
    }
    if (subtitleFiles.isEmpty()) args << "-movflags" << "+faststart";   // mp4 only
    args << partPath();
    return args;
}

// Runs inside a QThreadPool runnable, and providers throw - an escaping exception is std::terminate.
QString DownloadTask::extractLink() {
    try {
        return extractLinkInner();
    } catch (AppException &e) {
        e.log();
    } catch (const std::exception &e) {
        logWarn() << "Downloader" << displayName << e.what();
    } catch (...) {
        logWarn() << "Downloader" << displayName << "unknown extraction error";
    }
    return {};
}

QString DownloadTask::extractLinkInner() {
    if (!m_provider || !m_episode || m_cancel.isCancelled())
        return {};

    setProgressText("Extracting source...");
    Client client(m_cancel);

    auto servers = m_provider->loadServers(&client, m_episode.data());
    if (m_cancel.isCancelled()) return {};

    auto res = ServerSelector::findWorkingServer(&client, m_provider, servers);
    if (!res.found() || m_cancel.isCancelled()) return {};

    link = res.playInfo.videos.first().url.toString();
    if (!res.playInfo.audios.isEmpty())
        audioLink = res.playInfo.audios.first().url.toString();
    headers = res.playInfo.headers;

    if (subtitleFiles.isEmpty() && !res.playInfo.subtitles.isEmpty()) {
        setProgressText("Fetching subtitles...");
        fetchSubtitles(client, res.playInfo.subtitles);
    }
    setProgressText("Extracted source successfully!");

    m_episode = nullptr;
    m_provider = nullptr;
    return link;
}

namespace {

// Sniffed, never trusted from the url. Every HTML comment ends in "-->", so a bare "-->" check
// would let an error page served as a 200 reach the muxer; match a real timecode instead.
QString sniffSubtitleExtension(const QByteArray &data) {
    const QByteArray head = data.left(4096);
    if (head.startsWith("WEBVTT"))      return QStringLiteral(".vtt");
    if (head.contains("[Script Info]")) return QStringLiteral(".ass");
    static const QRegularExpression cue(
        QStringLiteral(R"(\d{1,2}:\d{2}:\d{2}[,.]\d{1,3}\s*-->\s*\d{1,2}:\d{2}:\d{2})"));
    if (cue.match(QString::fromUtf8(head)).hasMatch()) return QStringLiteral(".srt");
    return {};
}

bool isLangCode(const QString &text) {
    if (text.size() < 2 || text.size() > 3) return false;
    for (const QChar c : text)
        if (!c.isLetter() || c.unicode() > 0x7F) return false;
    // Shaped like a code but never one, and these turn up in subtitle filenames.
    static const QSet<QString> notCodes{"sub", "dub", "cc", "sdh", "srt", "vtt", "ass"};
    return !notCodes.contains(text.toLower());
}

// These hosts put the language in the filename - "ara-9.vtt", "..._sub_eng-0.vtt" - which covers
// far more of them than a label table ever would.
QString langFromFileName(const QUrl &url) {
    QString stem = QFileInfo(url.path()).completeBaseName();
    static const QRegularExpression trailingIndex(QStringLiteral(R"(-\d+$)"));
    stem.remove(trailingIndex);
    const QString tail = stem.section(QLatin1Char('_'), -1);
    return isLangCode(tail) ? tail.toLower() : QString();
}

QString subtitleLang(const Track &track) {
    if (const QString lang = track.lang.trimmed(); isLangCode(lang)) return lang.toLower();
    if (const QString fromName = langFromFileName(track.url); !fromName.isEmpty()) return fromName;

    static const QMap<QString, QString> byLabel{
        {"english", "eng"}, {"spanish", "spa"}, {"portuguese", "por"}, {"french", "fra"},
        {"german", "deu"},  {"italian", "ita"}, {"arabic", "ara"},     {"russian", "rus"},
        {"japanese", "jpn"},{"korean", "kor"},  {"chinese", "zho"},    {"indonesian", "ind"},
    };
    return byLabel.value(track.title.trimmed().toLower());
}

// Feeds --mux-import's name= field, where ':' would split the option.
QString subtitleName(const Track &track) {
    QString name = track.title;
    name.remove(QRegularExpression(QStringLiteral("[\\x00-\\x1F:\"]")));
    name = name.simplified();
    return name.left(40);
}

}

void DownloadTask::fetchSubtitles(Client &client, const QList<Track> &tracks) {
    const QString dir = Settings::tempDir() + QStringLiteral("/downloadsubs");
    QDir().mkpath(dir);
    const QString key = QString::fromLatin1(
        QCryptographicHash::hash(basePath().toUtf8(), QCryptographicHash::Md5).toHex().left(12));

    int index = 0;
    for (const Track &track : tracks) {
        if (m_cancel.isCancelled()) return;
        // The danmaku overlay is generated from the current style settings, not a source track.
        if (track.lang == QLatin1String("danmaku")) continue;
        ++index;

        if (track.url.isLocalFile()) {
            const QString local = track.url.toLocalFile();
            if (QFileInfo(local).size() > 0)
                subtitleFiles.append({local, subtitleLang(track), subtitleName(track), false});
            continue;
        }
        const QString scheme = track.url.scheme();
        if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) continue;

        // A subtitle that will not come down must never fail the video.
        const auto response = client.getBytes(track.url.toString(), headers);
        if (response.code != 200 || response.bytes.isEmpty()) {
            logWarn() << "Downloader" << "Subtitle fetch failed" << response.code << track.url.toString();
            continue;
        }
        const QString extension = sniffSubtitleExtension(response.bytes);
        if (extension.isEmpty()) {
            logWarn() << "Downloader" << "Unrecognised subtitle format" << track.url.toString();
            continue;
        }

        const QString file = QDir::cleanPath(
            QStringLiteral("%1/%2_%3%4").arg(dir, key).arg(index).arg(extension));
        QSaveFile out(file);
        if (!out.open(QIODevice::WriteOnly)) continue;
        out.write(response.bytes);
        if (!out.commit()) continue;
        subtitleFiles.append({file, subtitleLang(track), subtitleName(track), true});
    }
}

void DownloadTask::prepareSubtitles() {
    for (int i = subtitleFiles.size() - 1; i >= 0; --i)
        if (QFileInfo(subtitleFiles[i].path).size() <= 0) subtitleFiles.removeAt(i);
    m_useMkv.store(!subtitleFiles.isEmpty(), std::memory_order_release);
}

void DownloadTask::discardSubtitles() {
    for (const SubtitleFile &sub : subtitleFiles)
        if (sub.owned) QFile::remove(sub.path);
    subtitleFiles.clear();
}

void DownloadTask::setProgressValue(int value) {
    // Marshal onto the object's thread - main-thread Q_PROPERTY reads race otherwise.
    QMetaObject::invokeMethod(this, [this, value]() {
        if (m_progressValue == value) return;
        m_progressValue = value;

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        // N_m3u8DL-RE resumes from existing segments, so a restart does not begin at 0%.
        if (m_startTimeMs == 0) { m_startTimeMs = now; m_startProgress = value; }
        const double elapsed = (now - m_startTimeMs) / 1000.0;
        const int done = value - m_startProgress;
        if (done > 0 && value < 100 && elapsed > 2.0)
            m_etaText = formatEta(int((100 - value) * elapsed / done));
        else
            m_etaText.clear();
        rebuildStats();
    }, Qt::AutoConnection);
}

void DownloadTask::setProgressText(const QString &text) {
    QMetaObject::invokeMethod(this, [this, text]() {
        if (m_progressText == text) return;
        m_progressText = text;
    }, Qt::AutoConnection);
}

void DownloadTask::setSpeed(const QString &speed) {
    QMetaObject::invokeMethod(this, [this, speed]() {
        if (m_speed == speed) return;
        m_speed = speed;
        rebuildStats();
    }, Qt::AutoConnection);
}

void DownloadTask::resetStats() {
    QMetaObject::invokeMethod(this, [this]() {
        m_startTimeMs = 0;
        m_speed.clear();
        m_etaText.clear();
        m_stats.clear();
    }, Qt::AutoConnection);
}

void DownloadTask::rebuildStats() {
    QStringList parts;
    if (!m_speed.isEmpty())   parts << m_speed;
    if (!m_etaText.isEmpty()) parts << ("ETA " + m_etaText);
    m_stats = parts.join(QStringLiteral("  •  "));
}

QString DownloadTask::formatEta(int seconds) {
    if (seconds < 0) seconds = 0;
    int h = seconds / 3600, m = (seconds % 3600) / 60, s = seconds % 60;
    if (h > 0)
        return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    return QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
}

// Either container counts: a subtitled download lands as .mkv, everything else as .mp4.
static bool alreadyDownloaded(const QString &basePath) {
    return QFile::exists(basePath + QStringLiteral(".mp4"))
        || QFile::exists(basePath + QStringLiteral(".mkv"));
}

QString DownloadQueue::cleanFolderName(const QString &name) {
    static const QList<QPair<QChar, QChar>> replacements = {
        {':', u'꞉'}, {'"', '\''}, {'?', u'？'}, {'*', u'∗'},
        {'|', u'｜'}, {'<', u'≺'}, {'>', u'≻'}, {'/', u'∕'}, {'\\', u'⧵'}
    };
    QString result = name;
    for (const auto &[from, to] : replacements)
        result.replace(from, to);
    result.remove(QRegularExpression(QStringLiteral("[\\x00-\\x1F]")));
    // Windows rejects names ending in a space or dot (-> "Access denied").
    while (!result.isEmpty() && (result.endsWith(' ') || result.endsWith('.')))
        result.chop(1);
    result = result.trimmed();
    return result.isEmpty() ? QStringLiteral("download") : result;
}

DownloadQueue::DownloadQueue(QObject *parent)
    : QAbstractListModel(parent)
{
    m_threadPool.setMaxThreadCount(m_maxDownloads);
}

int DownloadQueue::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_tasks.count();
}

QVariant DownloadQueue::data(const QModelIndex &index, int role) const {
    auto *task = taskAt(index.row());
    if (!task) return {};
    switch (role) {
    case NameRole:          return task->displayName;
    case PathRole:          return task->path();
    case ProgressValueRole: return task->progressValue();
    case ProgressTextRole:  return task->progressText();
    case StatusRole:        return task->status();
    case StatsRole:         return task->stats();
    default:                return {};
    }
}

QHash<int, QByteArray> DownloadQueue::roleNames() const {
    return {
        {NameRole, "downloadName"}, {PathRole, "downloadPath"},
        {ProgressValueRole, "progressValue"}, {ProgressTextRole, "progressText"},
        {StatusRole, "status"}, {StatsRole, "stats"},
    };
}

void DownloadQueue::emitRowChanged(int row) {
    if (row < 0) return;
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, row]() {
            if (row < m_tasks.size()) { auto i = index(row); emit dataChanged(i, i); }
        }, Qt::QueuedConnection);
    } else if (row < m_tasks.size()) {
        auto i = index(row);
        emit dataChanged(i, i);
    }
}

int DownloadQueue::rowOf(const QSharedPointer<DownloadTask> &task) const {
    QMutexLocker locker(&m_mutex);
    return m_tasks.indexOf(task);
}

void DownloadQueue::downloadLink(const QString &name, const QString &link) {
    if (!DownloadTask::checkDependencies()) {
        AppShell::instance().reportError(
            "N_m3u8DL-RE.exe and ffmpeg.exe must sit next to AoNami.exe.", "Download");
        return;
    }

    QString cleanedName = cleanFolderName(name);
    auto task = QSharedPointer<DownloadTask>::create(cleanedName, Settings::instance().downloadDir(),
                                                     link, cleanedName);
    if (alreadyDownloaded(task->basePath()) || m_ongoingPaths.contains(task->basePath())) {
        logWarn() << "Downloader" << "Already exists or downloading" << task->path();
        return;
    }
    // Model signals must stay outside the lock - the workers take it too.
    beginInsertRows(QModelIndex(), m_tasks.size(), m_tasks.size());
    {
        QMutexLocker locker(&m_mutex);
        m_ongoingPaths.insert(task->basePath());
        m_tasks.push_back(task);
        m_taskQueue.append(task);
    }
    endInsertRows();
    startTasks();
}

void DownloadQueue::downloadShow(const ShowData &show, int startIndex, int endIndex) {
    if (!DownloadTask::checkDependencies()) {
        AppShell::instance().reportError(
            "N_m3u8DL-RE.exe and ffmpeg.exe must sit next to AoNami.exe.", "Download");
        return;
    }

    auto playlist = show.playlist();
    if (!playlist || !playlist->isValidIndex(startIndex)) return;

    if (endIndex < startIndex) std::swap(startIndex, endIndex);
    endIndex = qMin(endIndex, playlist->count() - 1);

    QString showName = cleanFolderName(show.title);
    QString workDir = Settings::instance().downloadDir() + "/" + showName;
    logInfo() << "Downloader" << showName << "from" << startIndex << "to" << endIndex;

    for (int i = startIndex; i <= endIndex; ++i) {
        auto task = QSharedPointer<DownloadTask>::create(playlist->at(i), show.provider, workDir);
        if (alreadyDownloaded(task->basePath()) || m_ongoingPaths.contains(task->basePath())) {
            logInfo() << "Downloader" << "Already exists or downloading" << task->path();
            continue;
        }
        // Model signals must stay outside the lock - the workers take it too.
        beginInsertRows(QModelIndex(), m_tasks.size(), m_tasks.size());
        {
            QMutexLocker locker(&m_mutex);
            m_ongoingPaths.insert(task->basePath());
            m_tasks.push_back(task);
            m_taskQueue.append(task);
        }
        endInsertRows();
    }
    startTasks();
}

void DownloadQueue::runTask(QSharedPointer<DownloadTask> task) {
    if (!DownloadTask::checkDependencies()) {
        // startTasks() already counted this slot - release it so downloads don't stall.
        { QMutexLocker locker(&m_mutex); m_currentConcurrentDownloads--; }
        QMetaObject::invokeMethod(this, [this]() { startTasks(); }, Qt::QueuedConnection);
        return;
    }

    if (task->link.isEmpty()) {
        task->link = task->extractLink();
        if (task->link.isEmpty()) {
            { QMutexLocker locker(&m_mutex); m_currentConcurrentDownloads--; }
            QMetaObject::invokeMethod(this, [this, task]() {
                if (task->isCancelled()) {
                    removeTask(task, true);
                } else {
                    task->setStatus(DownloadTask::Failed);
                    task->setProgressText("Extraction failed - press Retry");
                    emitRowChanged(rowOf(task));
                }
                startTasks();
            }, Qt::QueuedConnection);
            return;
        }
    }

    // Sets the container, so it must run before either argument builder reads subtitleFiles.
    task->prepareSubtitles();
    emitRowChanged(rowOf(task));

    const bool ffmpeg = task->usesFfmpeg();
    if (ffmpeg) QDir().mkpath(task->folder);   // N_m3u8DL-RE makes its save-dir; ffmpeg won't
    else        QDir().mkpath(task->tmpDir());

    auto *process = new QProcess(nullptr);
    process->setProgram(task->program());
    process->setArguments(ffmpeg ? task->ffmpegArguments() : task->toolArguments());
    process->setProcessChannelMode(QProcess::MergedChannels);
    task->setProcess(process);
    process->start();

    static QRegularExpression percentRegex(R"((\d+\.\d+)%)");
    static QRegularExpression speedRegex(R"(([\d.]+\s*[KMGTP]?i?B(?:ps|/s)))");
    // ffmpeg has no %, so derive it from `time=` vs the `Duration:` it prints.
    static QRegularExpression ffDurRegex(R"(Duration:\s*(\d+):(\d+):(\d+)\.(\d+))");
    static QRegularExpression ffTimeRegex(R"(time=\s*(\d+):(\d+):(\d+)\.(\d+))");
    auto ffSeconds = [](const QRegularExpressionMatch &m) {
        return m.captured(1).toInt() * 3600 + m.captured(2).toInt() * 60
             + m.captured(3).toInt() + m.captured(4).toInt() / 100.0;
    };
    double ffTotal = -1;
    bool reportedError = false;

    // Drain stdout continuously (a full pipe blocks the child); also re-checks cancel/pause.
    while (!task->isCancelled() && !task->isPaused()) {
        bool ready = process->waitForReadyRead(1000);
        if (process->bytesAvailable() > 0) {
            auto line = process->readAll().trimmed();
            line.replace("━", "");
            if (ffmpeg) {
                if (ffTotal < 0) {
                    auto dm = ffDurRegex.match(line);
                    if (dm.hasMatch()) ffTotal = ffSeconds(dm);
                }
                double cur = -1;
                for (auto it = ffTimeRegex.globalMatch(line); it.hasNext(); )
                    cur = ffSeconds(it.next());
                if (cur >= 0 && ffTotal > 0) {
                    task->setProgressValue(qBound(0, int(cur / ffTotal * 100), 100));
                    task->setProgressText("Downloading...");
                }
            } else {
                auto match = percentRegex.match(line);
                if (match.hasMatch())
                    task->setProgressValue(static_cast<int>(match.captured(1).toFloat()));
                else if (line.contains("ERROR:") && !reportedError) {
                    // One popup per task: N_m3u8DL-RE emits an ERROR line per failed segment.
                    reportedError = true;
                    QString msg = QString("%1\n%2").arg(task->displayName, line);
                    QMetaObject::invokeMethod(&AppShell::instance(), [msg]() {
                        AppShell::instance().reportError(msg, "Download Error");
                    }, Qt::QueuedConnection);
                }
                auto sm = speedRegex.match(line);
                if (sm.hasMatch()) task->setSpeed(sm.captured(1).simplified());
                task->setProgressText(line);
            }
            const int i = rowOf(task);
            emitRowChanged(i);
        }
        if (!ready && process->state() != QProcess::Running)
            break;
    }

    const bool cancelled = task->isCancelled();
    const bool paused    = task->isPaused();
    if (cancelled || paused) process->kill();
    process->waitForFinished(-1);

    const bool startFailed = process->error() == QProcess::FailedToStart;
    bool succeeded = !cancelled && !paused && !startFailed && process->exitCode() == 0;

    // Promote the part file before anyone is told the download finished.
    if (ffmpeg) {
        if (succeeded) {
            QFile::remove(task->path());
            if (!QFile::rename(task->partPath(), task->path())) {
                // The part file is the only copy now, so it outlives the failure.
                logWarn() << "Downloader" << "Could not rename" << task->partPath() << "to" << task->path();
                task->keepPart = true;
                succeeded = false;
            }
        }
        if (!succeeded && !paused && !task->keepPart) QFile::remove(task->partPath());
    } else if (succeeded) {
        QDir(task->tmpDir()).removeRecursively();   // --del-after-done leaves the wrapper behind
    }
    if (succeeded) task->discardSubtitles();

    {
        QMutexLocker locker(&m_mutex);
        m_currentConcurrentDownloads--;
        task->setProcess(nullptr);
        delete process;
    }

    QMetaObject::invokeMethod(this, [this, task, succeeded, cancelled, paused]() {
        if (cancelled) {
            removeTask(task, true);
        } else if (paused) {
            task->setPaused(false);
            task->setStatus(DownloadTask::Paused);
            task->setProgressText("Paused");
            emitRowChanged(rowOf(task));
        } else if (succeeded) {
            AppShell::instance().reportInfo(task->displayName, "Download Complete");
            removeTask(task, true);
        } else {
            task->setStatus(DownloadTask::Failed);
            task->setProgressText("Failed - press Retry to resume");
            emitRowChanged(rowOf(task));
        }
        startTasks();
    }, Qt::QueuedConnection);
}

void DownloadQueue::removeTask(const QSharedPointer<DownloadTask> &task, bool force) {
    // Main thread only.
    int idx;
    bool workerOwnsIt;
    {
        QMutexLocker locker(&m_mutex);
        idx = m_tasks.indexOf(task);
        if (idx == -1) return;
        // A queued task never reaches a worker, so dropping the row has to drop it here too.
        m_taskQueue.removeOne(task);
        // Running covers the extraction window, where there is no process to ask about yet.
        workerOwnsIt = !force && task->status() == DownloadTask::Running;
        if (workerOwnsIt) {
            logInfo() << "Downloader" << "Cancelling" << task->displayName;
            task->cancel();
            task->setProgressText("Cancelling");
        }
    }
    // runTask calls removeTask again when it unwinds.
    if (workerOwnsIt) { emitRowChanged(idx); return; }

    // Reached only once no worker owns the task, so touching its files is safe here.
    if (task->usesFfmpeg()) {
        if (!task->keepPart) QFile::remove(task->partPath());
    } else {
        QDir(task->tmpDir()).removeRecursively();
    }
    task->discardSubtitles();

    beginRemoveRows(QModelIndex(), idx, idx);
    {
        QMutexLocker locker(&m_mutex);
        m_ongoingPaths.remove(task->basePath());
        m_tasks.removeAt(idx);
    }
    endRemoveRows();
}

void DownloadQueue::cancelTask(int index) {
    QSharedPointer<DownloadTask> task;
    { QMutexLocker locker(&m_mutex); if (index >= 0 && index < m_tasks.size()) task = m_tasks[index]; }
    if (task) removeTask(task);
}

void DownloadQueue::cancelAllTasks() {
    QList<QSharedPointer<DownloadTask>> copy;
    { QMutexLocker locker(&m_mutex); m_taskQueue.clear(); copy = m_tasks; }
    for (int i = copy.size() - 1; i >= 0; --i)
        removeTask(copy[i]);
}

void DownloadQueue::pauseTask(int index) {
    QSharedPointer<DownloadTask> task;
    bool wasQueued = false;
    {
        QMutexLocker locker(&m_mutex);
        if (index < 0 || index >= m_tasks.size()) return;
        task = m_tasks[index];
        if (task->status() == DownloadTask::Queued) {
            m_taskQueue.removeOne(task);
            wasQueued = true;
        }
    }
    if (wasQueued) {
        task->setStatus(DownloadTask::Paused);
        task->setProgressText("Paused");
        emitRowChanged(index);
    } else if (task->status() == DownloadTask::Running) {
        // Worker loop sees isPaused(), kills the process (keeping temp segments); resumes next run.
        task->setPaused(true);
        task->setProgressText("Pausing...");
        emitRowChanged(index);
    }
}

void DownloadQueue::resumeTask(int index) {
    QSharedPointer<DownloadTask> task;
    {
        QMutexLocker locker(&m_mutex);
        if (index < 0 || index >= m_tasks.size()) return;
        task = m_tasks[index];
        if (task->status() != DownloadTask::Paused && task->status() != DownloadTask::Failed) return;
        task->setPaused(false);
        task->keepPart = false;
        task->setStatus(DownloadTask::Queued);
        if (!m_taskQueue.contains(task)) m_taskQueue.append(task);
    }
    task->setProgressText("Queued");
    emitRowChanged(index);
    startTasks();
}

void DownloadQueue::pauseAll() {
    int n;
    { QMutexLocker locker(&m_mutex); n = m_tasks.size(); }
    for (int i = 0; i < n; ++i) pauseTask(i);
}

void DownloadQueue::resumeAll() {
    int n;
    { QMutexLocker locker(&m_mutex); n = m_tasks.size(); }
    for (int i = 0; i < n; ++i) resumeTask(i);
}

void DownloadQueue::startTasks() {
    QList<int> startedRows;
    {
        QMutexLocker locker(&m_mutex);
        while (!m_taskQueue.isEmpty() && m_currentConcurrentDownloads < m_maxDownloads) {
            auto task = m_taskQueue.takeFirst();
            m_currentConcurrentDownloads++;
            task->maxSpeed = Settings::instance().get(Config::MaxSpeed);
            task->setStatus(DownloadTask::Running);
            task->resetStats();
            task->setProgressText("Starting...");
            int row = m_tasks.indexOf(task);
            if (row >= 0) startedRows.append(row);
            m_threadPool.start([this, task]() { runTask(task); });
        }
    }
    for (int row : startedRows) emitRowChanged(row);
}

int DownloadQueue::maxDownloads() const { return m_maxDownloads; }

void DownloadQueue::setMaxDownloads(int n) {
    if (m_maxDownloads == n) return;
    m_maxDownloads = n;
    m_threadPool.setMaxThreadCount(n);
    emit maxDownloadsChanged();
    startTasks();
}
