/**
 * Copyright (C) 2016 Deepin Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 **/

#include "musiclistview.h"

#include <QDebug>
#include <QResizeEvent>
#include <QStandardItemModel>
#include <QHeaderView>
#include <QScrollBar>
#include <QAction>
#include <QMenu>
#include <QLabel>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QDir>
#include <QStyle>
#include <QUrl>
#include <QProcess>

#include <dthememanager.h>

#include "../../musicapp.h"
#include "../../core/music.h"
#include "../../core/playlist.h"
#include "../../core/lyricservice.h"
#include "../helper/widgethellper.h"

#include "musicitemdelegate.h"
#include "infodialog.h"

DWIDGET_USE_NAMESPACE

class MusicListViewPrivate
{
public:
    MusicListViewPrivate(MusicListView *parent): q_ptr(parent) {}

    PlaylistPtr         m_playlist;
    QStandardItemModel  *m_model        = nullptr;
    MusicItemDelegate   *m_delegate     = nullptr;
    QScrollBar          *m_scrollBar    = nullptr;

    MusicListView *q_ptr;
    Q_DECLARE_PUBLIC(MusicListView);
};

MusicListView::MusicListView(QWidget *parent)
    : QTableView(parent), d_ptr(new MusicListViewPrivate(this))
{
    Q_D(MusicListView);

    setObjectName("MusicListView");

    d->m_model = new QStandardItemModel(0, 5, this);
    setModel(d->m_model);

    d->m_delegate = new MusicItemDelegate;
    setItemDelegate(d->m_delegate);

    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DropOnly);
    viewport()->setAcceptDrops(true);
    setDropIndicatorShown(false);

    setSelectionMode(QTableView::ExtendedSelection);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    this->horizontalHeader()->hide();
    this->verticalHeader()->hide();
    this->setEditTriggers(QAbstractItemView::NoEditTriggers);
    this->setSelectionBehavior(QAbstractItemView::SelectRows);
    this->setShowGrid(false);

    QHeaderView *verticalHeader = this->verticalHeader();
    verticalHeader->setSectionResizeMode(QHeaderView::Fixed);
    verticalHeader->setDefaultSectionSize(36);

    QHeaderView *headerView = this->horizontalHeader();
    headerView->setSectionResizeMode(QHeaderView::Stretch);

    for (int i = +1; i < MusicItemDelegate::ColumnButt; ++i) {
        headerView->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    }
    headerView->setSectionResizeMode(MusicItemDelegate::Number, QHeaderView::ResizeToContents);
    headerView->setSectionResizeMode(MusicItemDelegate::Title, QHeaderView::Stretch);
    headerView->setSectionResizeMode(MusicItemDelegate::Artist, QHeaderView::ResizeToContents);
    headerView->setSectionResizeMode(MusicItemDelegate::Album, QHeaderView::ResizeToContents);
    headerView->setSectionResizeMode(MusicItemDelegate::Length, QHeaderView::ResizeToContents);

    this->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &MusicListView::customContextMenuRequested,
            this, &MusicListView::requestCustomContextMenu);

    d->m_scrollBar = new QScrollBar(this);
    d->m_scrollBar->setObjectName("MusicViewScrollBar");
    d->m_scrollBar->setOrientation(Qt::Vertical);
    d->m_scrollBar->raise();

    connect(d->m_scrollBar, &QScrollBar::valueChanged,
    this, [ = ](int value) {
        verticalScrollBar()->setValue(value);
    });
    D_THEME_INIT_WIDGET(MusicListView);
}

MusicListView::~MusicListView()
{

}

PlaylistPtr MusicListView::playlist()
{
    Q_D(MusicListView);
    return d->m_playlist;
}

void MusicListView::onMusicPlayed(PlaylistPtr playlist, const MusicMeta &meta)
{
    Q_D(MusicListView);
    if (playlist != d->m_playlist) {
        qWarning() << "check playlist failed!"
                   << "m_playlist:" << d->m_playlist
                   << "playlist:" << d->m_playlist;
        return;
    }


    QModelIndex index;
    QStandardItem *item = nullptr;
    for (int i = 0; i < d->m_model->rowCount(); ++i) {
        index = d->m_model->index(i, 0);
        item = d->m_model->item(i, 0);
        MusicMeta itemmeta = qvariant_cast<MusicMeta>(item->data());
        if (itemmeta.hash == meta.hash) {
            break;
        }
    }

    if (nullptr == item) {
        return;
    }
    clearSelection();
    setCurrentIndex(index);
    scrollTo(index);

    // TODO
    d->m_delegate->setPlayingIndex(index);
}

void MusicListView::onMusicRemoved(PlaylistPtr playlist, const MusicMeta &meta)
{
    Q_D(MusicListView);
    if (playlist != d->m_playlist) {
        qWarning() << "check playlist failed!"
                   << "m_playlist:" << d->m_playlist
                   << "playlist:" << d->m_playlist;
        return;
    }

    for (int i = 0; i < d->m_model->rowCount(); ++i) {
        auto item = d->m_model->item(i, 0);
        MusicMeta itemmeta = qvariant_cast<MusicMeta>(item->data());
        if (itemmeta.hash == meta.hash) {
            d->m_model->removeRow(i);
            break;
        }
    }
}

void MusicListView::onMusicAdded(PlaylistPtr playlist, const MusicMeta &meta)
{
    Q_D(MusicListView);
    if (playlist != d->m_playlist) {
        qWarning() << "check playlist failed!"
                   << "m_playlist:" << d->m_playlist
                   << "playlist:" << d->m_playlist;
        return;
    }

    QStandardItem *newItem = new QStandardItem;
    newItem->setData(QVariant::fromValue<MusicMeta>(meta));
    d->m_model->appendRow(newItem);
    auto row = d->m_model->rowCount() - 1;
    QModelIndex index = d->m_model->index(row, 0, QModelIndex());
    d->m_model->setData(index, row);
    index = d->m_model->index(row, 1, QModelIndex());
    d->m_model->setData(index, meta.title);
    index = d->m_model->index(row, 2, QModelIndex());
    auto artist = meta.artist.isEmpty() ? tr("Unknow Artist") : meta.artist;
    d->m_model->setData(index, artist);
    index = d->m_model->index(row, 3, QModelIndex());
    auto album = meta.album.isEmpty() ? tr("Unknow Album") : meta.album;
    d->m_model->setData(index, album);

    index = d->m_model->index(row, 4, QModelIndex());
    d->m_model->setData(index, lengthString(meta.length));
}

void MusicListView::onMusicListAdded(PlaylistPtr playlist, const MusicMetaList &metalist)
{
    for (auto meta : metalist) {
        onMusicAdded(playlist, meta);
    }
}

void MusicListView::onLocate(PlaylistPtr playlist, const MusicMeta &meta)
{
    Q_D(MusicListView);
    if (playlist != d->m_playlist) {
        onMusiclistChanged(playlist);
    }

    QModelIndex index;
    QStandardItem *item = nullptr;
    for (int i = 0; i < d->m_model->rowCount(); ++i) {
        index = d->m_model->index(i, 0);
        item = d->m_model->item(i, 0);
        MusicMeta itemmeta = qvariant_cast<MusicMeta>(item->data());
        if (itemmeta.hash == meta.hash) {
            break;
        }
    }
    if (nullptr == item) {
        return;
    }
    clearSelection();
    setCurrentIndex(index);

    auto viewRect = QRect(QPoint(0, 0), size());
    if (!viewRect.intersects(visualRect(index))) {
        scrollTo(index, MusicListView::PositionAtCenter);
    }
}

void MusicListView::onMusiclistChanged(PlaylistPtr playlist)
{
    Q_D(MusicListView);

    if (playlist.isNull()) {
        qWarning() << "can not change to emptry playlist";
        return;
    }

    if (d->m_playlist) {
        d->m_playlist.data()->disconnect(this);
    }
    d->m_playlist = playlist;
    d->m_model->removeRows(0, d->m_model->rowCount());
    for (auto &info : playlist->allmusic()) {
        onMusicAdded(d->m_playlist, info);
    }
}

void MusicListView::wheelEvent(QWheelEvent *event)
{
    Q_D(MusicListView);
    QTableView::wheelEvent(event);
    d->m_scrollBar->setSliderPosition(verticalScrollBar()->sliderPosition());
}

void MusicListView::resizeEvent(QResizeEvent *event)
{
    Q_D(MusicListView);
    QTableView::resizeEvent(event);

    auto size = event->size();
    auto scrollBarWidth = 8;
    d->m_scrollBar->resize(scrollBarWidth, size.height());
    d->m_scrollBar->move(size.width() - scrollBarWidth - 3, 0);
    d->m_scrollBar->setMaximum(verticalScrollBar()->maximum());
    d->m_scrollBar->setMinimum(verticalScrollBar()->minimum());
    d->m_scrollBar->setPageStep(verticalScrollBar()->pageStep());
}


void MusicListView::showContextMenu(const QPoint &pos,
                                    PlaylistPtr selectedPlaylist,
                                    PlaylistPtr favPlaylist,
                                    QList<PlaylistPtr > newPlaylists)
{
    Q_D(MusicListView);
    QItemSelectionModel *selection = this->selectionModel();

    QPoint globalPos = this->mapToGlobal(pos);

    QMenu playlistMenu;
    bool hasAction = false;

    if (selectedPlaylist != favPlaylist) {
        hasAction = true;
        auto act = playlistMenu.addAction(favPlaylist->displayName());
        act->setData(QVariant::fromValue(favPlaylist));
    }

    for (auto playlist : newPlaylists) {
        auto act = playlistMenu.addAction(playlist->displayName());
        act->setData(QVariant::fromValue(playlist));
        hasAction = true;
    }

    if (hasAction) {
        playlistMenu.addSeparator();
    }
    auto newvar = QVariant::fromValue(PlaylistPtr());
    playlistMenu.addAction(tr("New playlist"))->setData(newvar);

    connect(&playlistMenu, &QMenu::triggered, this, [ = ](QAction * action) {
        auto playlist = action->data().value<PlaylistPtr >();
        MusicMetaList metalist;
        for (auto &index : selection->selectedRows()) {
            auto item = d->m_model->item(index.row(), index.column());
            if (item) {
                metalist << qvariant_cast<MusicMeta>(item->data());
            }
        }
        emit addToPlaylist(playlist, metalist);
    });

    bool singleSelect = (1 == selection->selectedRows().length());

    QMenu myMenu;

    if (singleSelect) {
        myMenu.addAction(tr("Play"));
    }
    myMenu.addAction(tr("Add to playlist"))->setMenu(&playlistMenu);
    myMenu.addSeparator();

    if (singleSelect) {
        myMenu.addAction(tr("Display in file manager"));
    }
    myMenu.addAction(tr("Remove from list"));
    myMenu.addAction(tr("Delete"));

    if (singleSelect) {
        myMenu.addSeparator();
        myMenu.addAction(tr("Song info"));
    }

    connect(&myMenu, &QMenu::triggered, this, [ = ](QAction * action) {
        if (action->text() == tr("Play")) {
            auto index = selection->selectedRows().first();
            auto item = d->m_model->item(index.row(), index.column());
            MusicMeta meta = qvariant_cast<MusicMeta>(item->data());
            emit play(meta);
        }

        if (action->text() == tr("Display in file manager")) {
            auto index = selection->selectedRows().first();
            auto item = d->m_model->item(index.row(), index.column());
            MusicMeta meta = qvariant_cast<MusicMeta>(item->data());
            auto dirUrl = QUrl::fromLocalFile(QFileInfo(meta.localPath).absoluteDir().absolutePath());
            QFileInfo ddefilemanger("/usr/bin/dde-file-manager");
            if (ddefilemanger.exists()) {
                auto dirFile = QUrl::fromLocalFile(QFileInfo(meta.localPath).absoluteFilePath());
                auto url = QString("%1?selectUrl=%2").arg(dirUrl.toString()).arg(dirFile.toString());
                QProcess::startDetached("dde-file-manager" , QStringList() << url);
            } else {
                QProcess::startDetached("gvfs-open" , QStringList() << dirUrl.toString());
            }
        }

        if (action->text() == tr("Remove from list")) {
            MusicMetaList metalist;
            for (auto index : selection->selectedRows()) {
                auto item = d->m_model->item(index.row(), index.column());
                MusicMeta meta = qvariant_cast<MusicMeta>(item->data());
                metalist << meta;
            }
            emit removeMusicList(metalist);
        }

        if (action->text() == tr("Delete")) {
            MusicMetaList metalist;
            for (auto index : selection->selectedRows()) {
                auto item = d->m_model->item(index.row(), index.column());
                MusicMeta meta = qvariant_cast<MusicMeta>(item->data());
                metalist << meta;
            }

            DDialog warnDlg;
            warnDlg.setTextFormat(Qt::AutoText);
            warnDlg.addButtons(QStringList() << tr("Cancel") << tr("Delete"));

            auto coverPath = QString(":/image/cover_max.png");
            if (1 == metalist.length()) {
                auto meta = metalist.first();
                QFileInfo coverfi(LyricService::coverPath(meta));
                if (coverfi.exists()) {
                    coverPath = coverfi.absoluteFilePath();
                }
                warnDlg.setMessage(
                    QString(tr("Are you sure to delete %1?")).arg(meta.title));
            } else {
                warnDlg.setMessage(
                    QString(tr("TODO: Are you sure to delete %1 songs?")).arg(metalist.length()));
            }

            auto cover = WidgetHelper::coverPixmap(coverPath, QSize(64, 64));

            warnDlg.setIcon(QIcon(cover));
            if (0 == warnDlg.exec()) {
                return;
            }
            emit deleteMusicList(metalist);
        }

        if (action->text() == tr("Song info")) {
            auto index = selection->selectedRows().first();
            auto item = d->m_model->item(index.row(), index.column());
            MusicMeta meta = qvariant_cast<MusicMeta>(item->data());
            auto coverPath = QString(":/image/info_cover.png");
            QFileInfo coverfi(LyricService::coverPath(meta));
            if (coverfi.exists()) {
                coverPath = coverfi.absoluteFilePath();
            }
            auto cover = WidgetHelper::coverPixmap(coverPath, QSize(140, 140));
            InfoDialog dlg(meta, cover, this);
            dlg.exec();
        }
    });

    myMenu.exec(globalPos);
}
