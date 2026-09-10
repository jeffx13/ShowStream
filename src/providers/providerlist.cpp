#include "providers/providerlist.h"
#include "providers/showprovider.h"

void ProviderList::setProviders(QList<ShowProvider *> &&providers) {
    beginResetModel();
    m_providers = std::move(providers);
    s_byName.clear();
    for (ShowProvider *provider : std::as_const(m_providers)) {
        provider->setParent(this);
        s_byName.insert(provider->name(), provider);
    }
    m_index = -1;
    m_typeIndex = 0;
    m_current = nullptr;
    m_types.clear();
    endResetModel();

    if (!m_providers.isEmpty())
        setCurrentIndex(0);
}

void ProviderList::setCurrentIndex(int index) {
    if (index == m_index || index < 0 || index >= m_providers.size()) return;

    // Keep the same show type across providers that offer it.
    const QString previousType = (m_typeIndex >= 0 && m_typeIndex < m_types.size())
                                     ? m_types.at(m_typeIndex) : QString();

    m_index = index;
    m_current = m_providers.at(index);
    m_types = m_current->availableTypes();
    emit currentIndexChanged();

    const int carried = previousType.isEmpty() ? -1 : m_types.indexOf(previousType);
    m_typeIndex = qMax(carried, 0);
    emit currentTypeIndexChanged();
}

void ProviderList::setCurrentTypeIndex(int index) {
    if (index == m_typeIndex || index < 0 || index >= m_types.size()) return;
    m_typeIndex = index;
    emit currentTypeIndexChanged();
}

void ProviderList::cycle() {
    if (m_providers.isEmpty()) return;
    setCurrentIndex((m_index + 1) % m_providers.size());
}

int ProviderList::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_providers.size();
}

QVariant ProviderList::data(const QModelIndex &index, int role) const {
    if (role != NameRole || index.row() < 0 || index.row() >= m_providers.size())
        return {};
    return m_providers.at(index.row())->name();
}

QHash<int, QByteArray> ProviderList::roleNames() const {
    return {{NameRole, "text"}};
}
