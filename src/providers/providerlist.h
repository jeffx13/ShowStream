#pragma once
#include <QAbstractListModel>
#include <QStringList>
#include <qqmlintegration.h>

class ShowProvider;

class ProviderList : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(int      currentIndex     READ currentIndex     WRITE setCurrentIndex     NOTIFY currentIndexChanged)
    Q_PROPERTY(int      currentTypeIndex READ currentTypeIndex WRITE setCurrentTypeIndex NOTIFY currentTypeIndexChanged)
    Q_PROPERTY(QVariant showTypes        READ showTypes                                  NOTIFY currentIndexChanged)
public:
    explicit ProviderList(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    // Takes ownership: each provider is reparented here.
    void setProviders(QList<ShowProvider *> &&providers);

    Q_INVOKABLE void cycle();

    ShowProvider *currentProvider() const { return m_current; }
    int currentTypeIndex() const { return m_typeIndex; }

    static ShowProvider *byName(const QString &providerName) {
        return s_byName.value(providerName, nullptr);
    }

signals:
    void currentIndexChanged();
    void currentTypeIndexChanged();

private:
    int  currentIndex() const { return m_index; }
    void setCurrentIndex(int index);
    void setCurrentTypeIndex(int index);
    QVariant showTypes() const {
        return QVariant::fromValue(m_types.isEmpty() ? QStringList{"All"} : m_types);
    }

    enum { NameRole = Qt::UserRole };
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QList<ShowProvider *> m_providers;
    inline static QHash<QString, ShowProvider *> s_byName;
    ShowProvider *m_current = nullptr;
    int m_index = -1;
    int m_typeIndex = 0;
    QStringList m_types;
};
