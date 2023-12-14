#pragma once
#include <fstream>
#include <iostream>
#include <QAbstractListModel>
#include <QStandardItemModel>
#include <nlohmann/json.hpp>
#include <QDir>
#include <QCoreApplication>
#include <QtConcurrent>
#include "Data/showdata.h"

class WatchListModel: public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int listType READ getCurrentListType WRITE setDisplayingListType NOTIFY layoutChanged)
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)
private:
    enum ListType
    {
        WATCHING,
        PLANNED,
        ON_HOLD,
        DROPPED,
        COMPLETED
    };

    nlohmann::json m_jsonList;
    std::unordered_map<std::string, int> jsonHashmap;
    std::unordered_map<std::string, int> totalEpisodeMap;
    int m_currentListType = WATCHING;
    int getCurrentListType();
    void setDisplayingListType(int listType);
    bool isLoading()
    {
        return loading;
    }
    bool loading = false;
    QFutureWatcher<void>* m_watcher;
private:
    // keeps track of the list type of each show
    int findShowInJsonList(int listType, const std::string& showLink)
    {
        int index = 0;
        bool found = false;
        for (const auto& item : m_jsonList[listType]) {
            if (item["link"] == showLink) {
                found = true;
                break;
            }
            ++index;
        }
        if (found) {
            qDebug() << "found" << index;
            return index;
        } else {
            return -1;
        }

    }
    void loadWatchList(QString filePath = "");
    void changeListType(const ShowData& show, int listType);
    void fetchUnwatchedEpisodes(int listType);
    QString watchListFilePath;
    QMutex mutex;
public:
    explicit WatchListModel(QObject *parent = nullptr): QAbstractListModel(parent){
        loadWatchList ();
        //m_watcher->setFuture (QtConcurrent::run(&WatchListModel::fetchUnwatchedEpisodes, this, WATCHING));
    };
signals:
    void loadingChanged(void);
    void syncedCurrentShow(void);
public slots:
    void syncCurrentShow();
    void save();
    void add(const ShowData& show, int listType);
    void addCurrentShow(int listType);
    void remove(const ShowData& show);
    void removeCurrentShow();
    void move(int from, int to);
    void moveEnded();
    void loadShow(int index);

private:
    enum{
        TitleRole = Qt::UserRole,
        CoverRole,
        UnwatchedEpisodesRole
    };
    int rowCount(const QModelIndex &parent) const override ;
    QVariant data(const QModelIndex &index, int role) const override ;
    QHash<int, QByteArray> roleNames() const override ;
};

