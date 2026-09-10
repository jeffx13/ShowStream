#pragma once
#include <QAbstractListModel>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <qqmlintegration.h>
#include <string>

class LogListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
public:
    enum Role { TimeRole = Qt::UserRole, TypeRole, MessageRole, ColourRole };

    explicit LogListModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    // Every network call logs a line, so an unbounded list is a session-long leak.
    static constexpr int kMaxEntries = 5000;

    Q_INVOKABLE void append(const QString &type, const QString &message, const QString &colour) {
        if (m_entries.size() >= kMaxEntries) {
            beginRemoveRows({}, 0, 0);
            m_entries.removeFirst();
            endRemoveRows();
        }
        beginInsertRows({}, m_entries.size(), m_entries.size());
        m_entries.append({QDateTime::currentDateTime().toString("hh:mm:ss"), type, message, colour});
        endInsertRows();
    }

    Q_INVOKABLE void clear() {
        beginResetModel();
        m_entries.clear();
        endResetModel();
    }

    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : m_entries.size();
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override {
        if (index.row() < 0 || index.row() >= m_entries.size()) return {};
        const Entry &entry = m_entries.at(index.row());
        switch (role) {
        case TimeRole:    return entry.time;
        case TypeRole:    return entry.type;
        case ColourRole:  return entry.colour;
        case MessageRole: return entry.message;
        default:          return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override {
        return {{TimeRole, "time"}, {TypeRole, "type"},
                {MessageRole, "message"}, {ColourRole, "colour"}};
    }

private:
    struct Entry { QString time, type, message, colour; };
    QList<Entry> m_entries;
};

class QLog {
public:
    enum Colour { Red = 31, Green = 32, Orange = 33, Magenta = 35, Cyan = 36, Yellow = 93 };

    inline static LogListModel logListModel{};

    explicit QLog(Colour colour)
        : m_debug(qDebug().noquote().nospace())
        , m_colour(QString::fromLatin1(colourName(colour)))
    {
        m_debug << QString("\033[%1m[").arg(colour);
    }

    ~QLog() {
        m_debug << " \033[0m";
        const QString plain = m_fields.join(' ');
        // A request line is just a url; the log page renders it as a link, the file keeps it plain.
        const bool isRequest = m_type.startsWith("GET") || m_type.startsWith("POST");
        QMetaObject::invokeMethod(&logListModel, "append", Q_ARG(QString, m_type),
                                  Q_ARG(QString, isRequest
                                      ? QStringLiteral("<a href='%1'>%1</a>").arg(m_fields.value(0))
                                      : plain),
                                  Q_ARG(QString, m_colour));
        writeToFile(m_type, plain);
    }

    QLog(const QLog &) = delete;
    QLog &operator=(const QLog &) = delete;

    template<typename T>
    QLog &operator<<(const T &value) {
        const QString str = toString(value);
        if (!m_haveType) {
            m_haveType = true;
            m_type = str;
            m_debug << centred(str, kLabelWidth) << "]";
        } else {
            m_fields << str;
            m_debug << " " << str;
        }
        return *this;
    }

    QLog &operator<<(const char *v)        { return *this << QString::fromUtf8(v); }
    QLog &operator<<(QStringView v)        { return *this << v.toString(); }
    QLog &operator<<(const QByteArray &v)  { return *this << QString::fromUtf8(v); }
    QLog &operator<<(const std::string &v) { return *this << QString::fromStdString(v); }

private:
    static constexpr int kLabelWidth = 14;

    static void writeToFile(const QString &type, const QString &message) {
        static QMutex mutex;
        static QFile file;
        QMutexLocker lock(&mutex);
        if (!file.isOpen()) {
            file.setFileName(QCoreApplication::applicationDirPath() + "/aonami.log");
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
                return;
        }
        file.write((QDateTime::currentDateTime().toString("hh:mm:ss") + " " +
                    type + " " + message + "\n").toUtf8());
        file.flush();
    }

    static const char *colourName(Colour c) {
        switch (c) {
        case Green:   return "green";
        case Red:     return "red";
        case Yellow:  return "yellow";
        case Orange:  return "orange";
        case Magenta: return "magenta";
        case Cyan:    return "cyan";
        }
        return "white";
    }

    static QString centred(const QString &text, int width) {
        const int left = qMax(0, (width - int(text.size())) / 2);
        const int right = qMax(0, width - left - int(text.size()));
        return QString(left, ' ') + text + QString(right, ' ');
    }

    template<typename T>
    static QString toString(const T &value) {
        QString s;
        QDebug(&s).noquote().nospace() << value;
        return s;
    }

    QDebug      m_debug;
    QString     m_colour;
    QString     m_type;
    QStringList m_fields;
    bool        m_haveType = false;
};

#define logError() QLog(QLog::Red)
#define logWarn()  QLog(QLog::Orange)
#define logInfo()  QLog(QLog::Cyan)
#define logOk()    QLog(QLog::Green)
#define logStep()  QLog(QLog::Yellow)   // a stage of a slow external workaround
#define logRaw()   QLog(QLog::Magenta)  // relayed verbatim from an external process
