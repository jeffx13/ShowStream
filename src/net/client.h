#pragma once
#include <QString>
#include <QByteArray>
#include <QMap>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include "net/html.h"
#include "net/canceltoken.h"

// Bump here, not per provider. A host needing a different build keeps its own literal.
inline constexpr char kFirefoxUserAgent[] =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:143.0) Gecko/20100101 Firefox/143.0";

class Client {
public:
    Client(CancelToken cancel = {}, bool verbose = true)
        : m_cancel(std::move(cancel)), m_verbose(verbose) {}

    Client(const Client &other) = default;
    Client &operator=(const Client &other) = default;

    bool isCancelled() const { return m_cancel.isCancelled(); }

    static QString urlWithParams(const QString &url, const QMap<QString, QString> &params);

    // A copy of this client that also aborts when `secondary` fires (race losers).
    Client withCancel(const CancelToken &secondary) const {
        Client c = *this;
        c.m_cancel = m_cancel.composeWith(secondary);
        return c;
    }

    // Off for endpoints that must not recurse (the solver's own calls) or where 403 is the answer.
    Client &setBypassEnabled(bool enabled) { m_bypass = enabled; return *this; }

    Client &setVerbose(bool verbose) { m_verbose = verbose; return *this; }

    // Idle-transfer limit, not a deadline - raise it for a host that builds a playlist on demand.
    Client &setTimeout(int ms) { m_timeoutMs = ms; return *this; }


    struct Response {
        int code = -1;
        QMap<QString, QString> headers;
        QString body;
        QByteArray bytes;   // getBytes() only; body stays empty there

        // HTTP header names are case-insensitive; HTTP/2 lowercases them.
        QString header(const QString &name) const {
            for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
                if (it.key().compare(name, Qt::CaseInsensitive) == 0)
                    return it.value();
            return {};
        }

        QJsonDocument toJson() const {
            if (body.isEmpty()) return {};
            QJsonParseError error;
            const auto doc = QJsonDocument::fromJson(body.toUtf8(), &error);
            return error.error == QJsonParseError::NoError ? doc : QJsonDocument();
        }

        QJsonObject toJsonObject() const { return toJson().object(); }
        QJsonArray  toJsonArray()  const { return toJson().array(); }

        Html toHtml() const { return Html::parse(body); }
    };

    Response get(const QString &url,
                 const QMap<QString, QString> &headers = {},
                 const QMap<QString, QString> &params = {});

    // For binary bodies, which QString::fromUtf8 would fill with U+FFFD.
    Response getBytes(const QString &url,
                      const QMap<QString, QString> &headers = {},
                      const QMap<QString, QString> &params = {});

    Response post(const QString &url,
                  const QMap<QString, QString> &data,
                  const QMap<QString, QString> &headers = {});

    Response post(const QString &url,
                  const QByteArray &data,
                  const QMap<QString, QString> &headers = {}) {
        return request(POST, url, headers, data);
    }

    Response head(const QString &url,
                  const QMap<QString, QString> &headers = {}) {
        return request(HEAD, url, headers);
    }

private:
    enum RequestType { GET, POST, HEAD };
    Response request(int type, const QString &url,
                     const QMap<QString, QString> &headers,
                     const QByteArray &postData = {},
                     bool binary = false);

    CancelToken m_cancel;
    int         m_timeoutMs = 10000;
    bool        m_verbose = true;
    bool        m_bypass = true;
};
