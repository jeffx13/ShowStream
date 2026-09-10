#include "ui/showdetails.h"
#include "media/playlistitem.h"
#include "providers/showprovider.h"
#include "ui/appshell.h"
#include "app/logger.h"
#include "app/settings.h"

ShowDetails::ShowDetails(QObject *parent) : QObject(parent) {
    connect(&m_watcher, &QFutureWatcher<void>::finished, this, &ShowDetails::onLoadFinished);
    connect(&m_watcher, &QFutureWatcher<void>::started,  this, &ShowDetails::isLoadingChanged);
    connect(&m_watcher, &QFutureWatcher<void>::finished, this, &ShowDetails::isLoadingChanged);
}

ShowDetails::~ShowDetails() {
    m_cancel.cancel();
    waitFor(m_watcher, "ShowDetails load");
}

int ShowDetails::lastWatchedIndex() const {
    auto list = playlist();
    return list ? list->currentIndex() : -1;
}

void ShowDetails::setLastWatchedIndex(int index) {
    auto list = playlist();
    if (!list) return;

    if (list->parent() && list->parent()->currentIndex() == list->row())
        return;

    if (!list->setCurrentIndex(index)) return;
    updateContinueEpisode();
    emit lastWatchedIndexChanged();
}

void ShowDetails::updateContinueEpisode() {
    auto list = playlist();
    if (!list) { m_continueText.clear(); return; }

    int idx = qMax(list->currentIndex(), 0);

    // Past the threshold the episode is done with; point at the next one.
    if (auto watched = list->at(idx);
        watched && watched->progress() >= Settings::instance().watchedFraction()
        && idx + 1 < list->count())
        ++idx;

    m_continueIndex = idx;
    auto episode = list->at(m_continueIndex);
    if (!episode) { m_continueText.clear(); return; }

    m_continueText = (m_continueIndex == 0 ? QStringLiteral("Play ")
                                           : QStringLiteral("Continue from "))
                     + episode->displayName.simplified();
}

void ShowDetails::cancel() {
    if (m_watcher.isRunning())
        m_cancel.cancel();
}

void ShowDetails::setShow(const ShowData &show, const ShowData::WatchState &watchState, bool navigate) {
    if (m_watcher.isRunning()) {
        m_pendingShow = show;
        m_pendingInfo = watchState;
        m_pendingNavigate = navigate;
        m_hasPending = true;
        m_cancel.cancel();
        return;
    }
    if (m_show.link == show.link) {
        if (navigate) AppShell::instance().navigateTo(AppShell::Page::Info);
        return;
    }
    // Fresh, not reset(): a reset would un-cancel the load this one supersedes and let its
    // queued result overwrite the show.
    m_cancel = CancelToken{};
    m_watcher.setFuture(QtConcurrent::run(&ShowDetails::load, this, show, watchState, navigate, m_cancel));
}

void ShowDetails::reload(const ShowData &show, const ShowData::WatchState &watchState) {
    if (m_watcher.isRunning()) return;
    m_cancel = CancelToken{};
    m_watcher.setFuture(QtConcurrent::run(&ShowDetails::load, this, show, watchState, false, m_cancel));
}

void ShowDetails::onLoadFinished() {
    if (!m_hasPending) return;
    m_hasPending = false;
    setShow(m_pendingShow, m_pendingInfo, m_pendingNavigate);
}

// Worker thread; the arguments are by-value copies, the token included - a superseded load
// must not test whichever token the load that replaced it has since installed.
void ShowDetails::load(ShowData show, ShowData::WatchState watchState, bool navigate, CancelToken cancel) {
    auto list = watchState.playlist;
    const bool usingExistingPlaylist = (list != nullptr);

    bool success = false;
    if (show.provider) {
        logInfo() << show.provider->name() << "Loading" << show.title << "using" << show.link;
        try {
            Client client(cancel);
            success = show.provider->loadShow(&client, show);
        } catch (const std::exception &ex) {
            AppShell::instance().reportError(QString::fromUtf8(ex.what()), show.provider->name() + " Error");
        } catch (...) {
            AppShell::instance().reportError("An unknown error occurred", show.provider->name() + " Error");
        }
    }

    if (!success || cancel.isCancelled()) {
        if (!success) {
            // A provider returning false (rather than throwing) otherwise looks like a dead click.
            logWarn() << "ShowDetails" << "Failed to load" << show.title;
            const QString title = show.title;
            QMetaObject::invokeMethod(&AppShell::instance(), [title]() {
                AppShell::instance().reportError("Could not load " + title + ".", "Show Error");
            }, Qt::QueuedConnection);
        }
        return;
    }

    if (!list)
        list = show.playlist();

    bool shouldReverse = false;
    if (usingExistingPlaylist) {
        shouldReverse = list->currentIndex() > 0;
        show.setPlaylist(list);
    } else if (list && list->isValidIndex(watchState.lastWatchedIndex)) {
        list->setCurrentIndex(watchState.lastWatchedIndex);
        if (auto item = list->currentItem())
            item->setProgress(watchState.progress);
        shouldReverse = watchState.lastWatchedIndex > 0;
    }

    logInfo() << "ShowDetails" << "Loaded" << show.title;

    QMetaObject::invokeMethod(this, [this, show = std::move(show), list, shouldReverse, navigate, cancel]() {
        // A newer request arrived - drop this stale result.
        if (cancel.isCancelled()) return;
        m_show = show;
        m_episodes.setPlaylist(list);
        // Unconditional: setting it only when true keeps the previous show's order.
        m_episodes.setReversed(shouldReverse);
        updateContinueEpisode();
        if (navigate) AppShell::instance().navigateTo(AppShell::Page::Info);
        emit showChanged();
        emit lastWatchedIndexChanged();
    }, Qt::QueuedConnection);
}
