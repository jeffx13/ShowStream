#pragma once
#include <QObject>
#include <QSettings>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QVariant>
#include <QMap>
#include <QString>
#include <QCoreApplication>
#include <atomic>
#include "app/qmlsingleton.h"
#include "app/config.h"

class Settings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool    mpvLogEnabled   READ mpvLogEnabled   WRITE setMpvLogEnabled   NOTIFY mpvLogEnabledChanged)
    Q_PROPERTY(bool    mpvYtdlEnabled  READ mpvYtdlEnabled  WRITE setMpvYtdlEnabled  NOTIFY mpvYtdlEnabledChanged)
    Q_PROPERTY(QString proxy           READ proxy           WRITE setProxy           NOTIFY proxyChanged)
    Q_PROPERTY(QString downloadDir     READ downloadDir     WRITE setDownloadDir     NOTIFY downloadDirChanged)
    Q_PROPERTY(QString maxSpeed        READ maxSpeed        WRITE setMaxSpeed        NOTIFY maxSpeedChanged)
    Q_PROPERTY(int     subFontSize     READ subFontSize     WRITE setSubFontSize     NOTIFY subFontSizeChanged)
    Q_PROPERTY(int     subPos          READ subPos          WRITE setSubPos          NOTIFY subPosChanged)
    Q_PROPERTY(bool    preferDub       READ preferDub       WRITE setPreferDub       NOTIFY preferDubChanged)
    Q_PROPERTY(bool    aniskipEnabled  READ aniskipEnabled  WRITE setAniskipEnabled  NOTIFY aniskipEnabledChanged)
    Q_PROPERTY(bool    aniskipAuto     READ aniskipAuto     WRITE setAniskipAuto     NOTIFY aniskipAutoChanged)
    Q_PROPERTY(int     watchedPercent  READ watchedPercent  WRITE setWatchedPercent  NOTIFY watchedPercentChanged)
    Q_PROPERTY(bool    danmakuEnabled     READ danmakuEnabled     WRITE setDanmakuEnabled     NOTIFY danmakuEnabledChanged)
    Q_PROPERTY(int     danmakuOpacity     READ danmakuOpacity     WRITE setDanmakuOpacity     NOTIFY danmakuOpacityChanged)
    Q_PROPERTY(int     danmakuFontScale   READ danmakuFontScale   WRITE setDanmakuFontScale   NOTIFY danmakuFontScaleChanged)
    Q_PROPERTY(int     danmakuSpeed       READ danmakuSpeed       WRITE setDanmakuSpeed       NOTIFY danmakuSpeedChanged)
    Q_PROPERTY(int     danmakuArea        READ danmakuArea        WRITE setDanmakuArea        NOTIFY danmakuAreaChanged)
    Q_PROPERTY(int     danmakuMinWeight   READ danmakuMinWeight   WRITE setDanmakuMinWeight   NOTIFY danmakuMinWeightChanged)
    Q_PROPERTY(int     danmakuMaxOnScreen READ danmakuMaxOnScreen WRITE setDanmakuMaxOnScreen NOTIFY danmakuMaxOnScreenChanged)
    Q_PROPERTY(bool    danmakuBold        READ danmakuBold        WRITE setDanmakuBold        NOTIFY danmakuBoldChanged)
    Q_PROPERTY(int     danmakuOutline     READ danmakuOutline     WRITE setDanmakuOutline     NOTIFY danmakuOutlineChanged)
    Q_PROPERTY(bool    danmakuBlockScroll READ danmakuBlockScroll WRITE setDanmakuBlockScroll NOTIFY danmakuBlockScrollChanged)
    Q_PROPERTY(bool    danmakuBlockTop    READ danmakuBlockTop    WRITE setDanmakuBlockTop    NOTIFY danmakuBlockTopChanged)
    Q_PROPERTY(bool    danmakuBlockBottom READ danmakuBlockBottom WRITE setDanmakuBlockBottom NOTIFY danmakuBlockBottomChanged)
    Q_PROPERTY(bool    danmakuBlockColour READ danmakuBlockColour WRITE setDanmakuBlockColour NOTIFY danmakuBlockColourChanged)
    Q_PROPERTY(bool    danmakuBlockRepeat READ danmakuBlockRepeat WRITE setDanmakuBlockRepeat NOTIFY danmakuBlockRepeatChanged)
    Q_PROPERTY(bool    discordEnabled  READ discordEnabled  WRITE setDiscordEnabled  NOTIFY discordEnabledChanged)
    Q_PROPERTY(QString themeName       READ themeName       WRITE setThemeName       NOTIFY themeNameChanged)
    Q_PROPERTY(QString accentColor     READ accentColor     WRITE setAccentColor     NOTIFY accentColorChanged)
    Q_PROPERTY(double  uiScale         READ uiScale         WRITE setUiScale         NOTIFY uiScaleChanged)
    Q_PROPERTY(QString path            READ iniPath         CONSTANT)
    Q_PROPERTY(QString appDir          READ appDir          CONSTANT)

public:
    static Settings &instance();

    template <typename T>
    T get(const Config::Key<T> &key) const {
        return m_settings.value(QLatin1String(key.path), QVariant::fromValue(key.defaultValue)).template value<T>();
    }
    template <typename T>
    void set(const Config::Key<T> &key, const T &value) {
        m_settings.setValue(QLatin1String(key.path), QVariant::fromValue(value));
        scheduleSync();
    }

    static QString iniPath();
    static QString appDir() { return QCoreApplication::applicationDirPath(); }

    Q_INVOKABLE QVariant value(const QString &key, const QVariant &defaultValue = {}) const {
        return m_settings.value(key, defaultValue);
    }
    Q_INVOKABLE void setValue(const QString &key, const QVariant &value) {
        m_settings.setValue(key, value);
        scheduleSync();
    }

    Q_INVOKABLE void prependToHistory(const QString &key, const QString &value, int maxCount = 10);
    Q_INVOKABLE void removeFromHistory(const QString &key, const QString &value);
    Q_INVOKABLE void clearHistory(const QString &key);

    bool mpvLogEnabled() const  { return get(Config::MpvLog); }
    bool mpvYtdlEnabled() const { return get(Config::Ytdl); }
    QString proxy() const       { return get(Config::Proxy); }
    QString maxSpeed() const    { return get(Config::MaxSpeed); }
    int subFontSize() const     { return get(Config::SubFontSize); }
    int subPos() const          { return get(Config::SubPos); }
    bool aniskipEnabled() const { return get(Config::AniSkip); }
    bool aniskipAuto() const    { return get(Config::AniSkipAuto); }
    int watchedPercent() const  { return get(Config::WatchedPercent); }
    bool discordEnabled() const { return get(Config::DiscordEnabled); }
    QString themeName() const   { return get(Config::ThemeName); }
    QString accentColor() const { return get(Config::AccentColor); }
    double uiScale() const      { return get(Config::UiScale); }
    QString downloadDir() const;

    // Cached in an atomic so the server selector worker can read it off the main thread.
    bool preferDub() const      { return s_preferDub.load(std::memory_order_relaxed); }

    double watchedFraction() const { return qBound(1, watchedPercent(), 100) / 100.0; }

    QString subdlApiKey() const    { return get(Config::SubdlApiKey); }
    QString subdlLanguages() const { return get(Config::SubdlLanguages); }

    bool danmakuEnabled() const     { return get(Config::DanmakuEnabled); }
    int  danmakuOpacity() const     { return get(Config::DanmakuOpacity); }
    int  danmakuFontScale() const   { return get(Config::DanmakuFontScale); }
    int  danmakuSpeed() const       { return get(Config::DanmakuSpeed); }
    int  danmakuArea() const        { return get(Config::DanmakuArea); }
    int  danmakuMinWeight() const   { return get(Config::DanmakuMinWeight); }
    int  danmakuMaxOnScreen() const { return get(Config::DanmakuMaxOnScreen); }
    bool danmakuBold() const        { return get(Config::DanmakuBold); }
    int  danmakuOutline() const     { return get(Config::DanmakuOutline); }
    bool danmakuBlockScroll() const { return get(Config::DanmakuBlockScroll); }
    bool danmakuBlockTop() const    { return get(Config::DanmakuBlockTop); }
    bool danmakuBlockBottom() const { return get(Config::DanmakuBlockBottom); }
    bool danmakuBlockColour() const { return get(Config::DanmakuBlockColour); }
    bool danmakuBlockRepeat() const { return get(Config::DanmakuBlockRepeat); }

    void setMpvLogEnabled(bool v)  { apply(Config::MpvLog, v, &Settings::mpvLogEnabledChanged); }
    void setMpvYtdlEnabled(bool v) { apply(Config::Ytdl, v, &Settings::mpvYtdlEnabledChanged); }
    void setMaxSpeed(const QString &v) { apply(Config::MaxSpeed, v, &Settings::maxSpeedChanged); }
    void setSubPos(int v)          { apply(Config::SubPos, v, &Settings::subPosChanged); }
    void setAniskipEnabled(bool v) { apply(Config::AniSkip, v, &Settings::aniskipEnabledChanged); }
    void setAniskipAuto(bool v)    { apply(Config::AniSkipAuto, v, &Settings::aniskipAutoChanged); }
    void setWatchedPercent(int v)  { apply(Config::WatchedPercent, qBound(0, v, 100), &Settings::watchedPercentChanged); }
    void setDiscordEnabled(bool v) { apply(Config::DiscordEnabled, v, &Settings::discordEnabledChanged); }
    void setThemeName(const QString &v)   { apply(Config::ThemeName, v, &Settings::themeNameChanged); }
    void setAccentColor(const QString &v) { apply(Config::AccentColor, v, &Settings::accentColorChanged); }
    void setProxy(const QString &v);
    void setDownloadDir(const QString &dir);
    void setUiScale(double v);
    void setPreferDub(bool v);

    void setSubFontSize(int v)        { applyDanmakuStyle(Config::SubFontSize, v, &Settings::subFontSizeChanged); }
    void setDanmakuOpacity(int v)     { applyDanmakuStyle(Config::DanmakuOpacity, qBound(10, v, 100), &Settings::danmakuOpacityChanged); }
    void setDanmakuFontScale(int v)   { applyDanmakuStyle(Config::DanmakuFontScale, qBound(50, v, 200), &Settings::danmakuFontScaleChanged); }
    void setDanmakuSpeed(int v)       { applyDanmakuStyle(Config::DanmakuSpeed, qBound(25, v, 400), &Settings::danmakuSpeedChanged); }
    void setDanmakuArea(int v)        { applyDanmakuStyle(Config::DanmakuArea, qBound(10, v, 100), &Settings::danmakuAreaChanged); }
    void setDanmakuMinWeight(int v)   { applyDanmakuStyle(Config::DanmakuMinWeight, qBound(0, v, 11), &Settings::danmakuMinWeightChanged); }
    void setDanmakuMaxOnScreen(int v) { applyDanmakuStyle(Config::DanmakuMaxOnScreen, qBound(0, v, 500), &Settings::danmakuMaxOnScreenChanged); }
    void setDanmakuBold(bool v)       { applyDanmakuStyle(Config::DanmakuBold, v, &Settings::danmakuBoldChanged); }
    void setDanmakuOutline(int v)     { applyDanmakuStyle(Config::DanmakuOutline, qBound(0, v, 2), &Settings::danmakuOutlineChanged); }
    void setDanmakuBlockScroll(bool v){ applyDanmakuStyle(Config::DanmakuBlockScroll, v, &Settings::danmakuBlockScrollChanged); }
    void setDanmakuBlockTop(bool v)   { applyDanmakuStyle(Config::DanmakuBlockTop, v, &Settings::danmakuBlockTopChanged); }
    void setDanmakuBlockBottom(bool v){ applyDanmakuStyle(Config::DanmakuBlockBottom, v, &Settings::danmakuBlockBottomChanged); }
    void setDanmakuBlockColour(bool v){ applyDanmakuStyle(Config::DanmakuBlockColour, v, &Settings::danmakuBlockColourChanged); }
    void setDanmakuBlockRepeat(bool v){ applyDanmakuStyle(Config::DanmakuBlockRepeat, v, &Settings::danmakuBlockRepeatChanged); }

    // Not in the appearance group: MpvPlayer swaps the track on this, it must not rebuild the ASS.
    void setDanmakuEnabled(bool v) {
        if (apply(Config::DanmakuEnabled, v, &Settings::danmakuEnabledChanged)) syncDanmakuOptions();
    }

    Q_INVOKABLE void resetDanmakuAppearance();

    static QString tempDir();
    QMap<QString, QString> groupValues(const QString &group) const;

signals:
    void mpvLogEnabledChanged();
    void mpvYtdlEnabledChanged();
    void proxyChanged();
    void downloadDirChanged();
    void maxSpeedChanged();
    void subFontSizeChanged();
    void subPosChanged();
    void preferDubChanged();
    void aniskipEnabledChanged();
    void aniskipAutoChanged();
    void watchedPercentChanged();
    void danmakuEnabledChanged();
    void danmakuOpacityChanged();
    void danmakuFontScaleChanged();
    void danmakuSpeedChanged();
    void danmakuAreaChanged();
    void danmakuMinWeightChanged();
    void danmakuMaxOnScreenChanged();
    void danmakuBoldChanged();
    void danmakuOutlineChanged();
    void danmakuBlockScrollChanged();
    void danmakuBlockTopChanged();
    void danmakuBlockBottomChanged();
    void danmakuBlockColourChanged();
    void danmakuBlockRepeatChanged();
    void discordEnabledChanged();
    void themeNameChanged();
    void accentColorChanged();
    void uiScaleChanged();

    void danmakuStyleChanged();
    // Also emitted for an external edit to settings.ini, which fires no per-property signal.
    void settingsChanged();

private:
    template <typename T, typename Signal>
    bool apply(const Config::Key<T> &key, const T &value, Signal changed) {
        if (get(key) == value) return false;
        set(key, value);
        emit (this->*changed)();
        emit settingsChanged();
        return true;
    }

    template <typename T, typename Signal>
    void applyDanmakuStyle(const Config::Key<T> &key, const T &value, Signal changed) {
        if (!apply(key, value, changed)) return;
        syncDanmakuOptions();
        emit danmakuStyleChanged();
    }

    // Danmaku extraction runs off the GUI thread, where QSettings must not be touched.
    void syncDanmakuOptions() const;
    void applyProxySettings(const QString &proxyString);
    void scheduleSync();

    Settings();

    mutable QSettings m_settings;
    QFileSystemWatcher m_fileWatcher;
    QTimer m_syncTimer;
    static inline std::atomic<bool> s_preferDub{false};
};

DECLARE_QML_SINGLETON(Settings);
