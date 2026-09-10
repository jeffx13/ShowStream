#pragma once
#include <QString>

namespace Config {

template <typename T>
struct Key {
    const char *path;
    T defaultValue;
};

inline const Key<int>     Volume      {"player/volume", 100};
inline const Key<double>  Speed       {"player/speed", 1.0};
inline const Key<bool>    Ytdl        {"player/ytdl", false};
inline const Key<int>     SubFontSize {"player/subFontSize", 40};
inline const Key<int>     SubPos      {"player/subPos", 100};
inline const Key<bool>    PreferDub   {"player/preferDub", false};
inline const Key<bool>    AniSkip     {"player/aniskip", true};
inline const Key<bool>    AniSkipAuto {"player/aniskipAuto", false};
inline const Key<int>     WatchedPercent {"player/watchedPercent", 80};

inline const Key<bool>    DanmakuEnabled     {"danmaku/enabled", true};
inline const Key<int>     DanmakuOpacity     {"danmaku/opacity", 80};        // %
inline const Key<int>     DanmakuFontScale   {"danmaku/fontScale", 100};     // % of the 48px@1080p base
inline const Key<int>     DanmakuSpeed       {"danmaku/speed", 100};         // % - higher crosses faster
inline const Key<int>     DanmakuArea        {"danmaku/area", 85};           // % of frame height usable
inline const Key<int>     DanmakuMaxLines    {"danmaku/maxLines", 0};        // 0 = derive from area
inline const Key<int>     DanmakuMinWeight   {"danmaku/minWeight", 0};       // 0 = off; bilibili weights run 1..11
inline const Key<int>     DanmakuMaxOnScreen {"danmaku/maxOnScreen", 60};    // 0 = unlimited
inline const Key<QString> DanmakuFont        {"danmaku/font", QStringLiteral("Microsoft YaHei")};
inline const Key<bool>    DanmakuBold        {"danmaku/bold", false};
inline const Key<int>     DanmakuOutline     {"danmaku/outline", 1};         // 0 none, 1 outline, 2 outline + shadow
inline const Key<bool>    DanmakuBlockScroll {"danmaku/blockScroll", false};
inline const Key<bool>    DanmakuBlockTop    {"danmaku/blockTop", false};
inline const Key<bool>    DanmakuBlockBottom {"danmaku/blockBottom", false};
inline const Key<bool>    DanmakuBlockColour {"danmaku/blockColour", false}; // force coloured comments to white
inline const Key<bool>    DanmakuBlockRepeat {"danmaku/blockRepeat", true};

inline const Key<int>     SkipOPStart  {"skip/fallbackOPStart", 0};
inline const Key<int>     SkipOPLength {"skip/fallbackOPLength", 90};
inline const Key<int>     SkipEDLength {"skip/fallbackEDLength", 90};

inline const Key<QString> ThemeName   {"ui/theme", QStringLiteral("obsidian")};
inline const Key<QString> AccentColor {"ui/accent", QString()};   // empty = use the theme's own accent
inline const Key<double>  UiScale     {"ui/scale", 1.0};

inline const Key<bool>    MpvLog      {"logging/mpv", true};

inline const Key<QString> Proxy         {"network/proxy", QString()};
inline const Key<bool>    LimitCache    {"network/limit_cache", false};
inline const Key<qint64>  ForwardCache  {"network/forward_cache", 0};
inline const Key<qint64>  BackwardCache {"network/backward_cache", 0};

inline const Key<QString> MaxSpeed      {"download/maxSpeed", QString()};

inline const Key<QString> SubdlApiKey {"subtitles/subdlApiKey",
                                       QStringLiteral("subdl_MfPsaHHiw3qYRJZhOaqFtypiXTJB7XqrMSFz1lvrRvk")};
inline const Key<QString> SubdlLanguages {"subtitles/subdlLanguages", QStringLiteral("EN")};

// The Client ID is a public app identifier, safe to ship.
inline const Key<bool>    DiscordEnabled  {"discord/enabled", true};
inline const Key<QString> DiscordClientId {"discord/clientId", QStringLiteral("1518260245552566283")};

inline QString skipProfile(const QString &showLink) {
    return QStringLiteral("skip/") + QString::number(qHash(showLink));
}

inline QString episodeSub(const QString &episodeLink) {
    return QStringLiteral("subtitles/ep") + QString::number(qHash(episodeLink));
}

inline QString skipMal(const QString &showLink) {
    return QStringLiteral("skipmal/") + QString::number(qHash(showLink));
}

}
