#pragma once
#include <QHash>
#include <QList>
#include "net/client.h"
#include "media/playinfo.h"

class ShowProvider;

class ServerSelector {
public:
    struct Result {
        int index = -1;
        PlayInfo playInfo;
        QHash<QString, PlayInfo> cachedSources;
        bool found() const { return index >= 0; }
    };

    // Unknown is not a verdict: the probe never reached one, so the server stays unchecked.
    enum class Playability { Playable, Broken, Unknown };

    static Playability playability(Client *client, PlayInfo &playItem);
    static Result findWorkingServer(Client *client, ShowProvider *provider, QList<VideoServer> &servers);
};