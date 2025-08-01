// Copyright (C) 2020 ~ 2021 Uniontech Software Technology Co., Ltd.
// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cda.h"
#include <vlc/vlc.h>
#include <vlc_common.h>
#include <vlc_variables.h>
#include <vlc_access.h>
#include <vlc_stream.h>
#include <vlc_input_item.h>
#include <libvlc_media.h>
#include <vlc_playlist.h>
#include <vlc_interface.h>
#include <vlc_input.h>
#include <libvlc.h>
#include <vlc_plugin.h>
#include <vlc_modules.h>

#include <QDebug>
#include <QDir>
#include <QScopedPointer>
#include <QMap>
#include <QTimer>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusMessage>

#include "dynamiclibraries.h"
#include "utils.h"
#include "util/log.h"

typedef input_item_t *(*input_item_NewExt_func)(const char *,
                                                const char *,
                                                mtime_t, int,
                                                enum input_item_net_type);
typedef stream_t *(*vlc_stream_NewURL_func)(vlc_object_t *, const char *);
typedef input_item_node_t *(*input_item_node_Create_func)(input_item_t *);
typedef void (*input_item_Release_func)(input_item_t *);
typedef int (*vlc_stream_ReadDir_func)(stream_t *, input_item_node_t *);
typedef void (*input_item_node_Delete_func)(input_item_node_t *);
typedef void (*vlc_stream_Delete_func)(stream_t *);

QStringList getCDADirectory()
{
    // qCDebug(dmMusic) << "Getting CDA directory paths";
    return QStringList() << "cdda:///dev/sr0"; //暂时只考虑sr0,原装光驱
}

QString queryIdTypeFormDbus()
{
    // qCDebug(dmMusic) << "Querying ID type from DBus";
    QVariant vartype = Utils::readDBusProperty("org.freedesktop.UDisks2",
                                               "/org/freedesktop/UDisks2/block_devices/sr0",
                                               "org.freedesktop.UDisks2.Block",
                                               "IdType",
                                               QDBusConnection::systemBus());
    QString result = vartype.isValid() ? vartype.toString() : "";
    // qCDebug(dmMusic) << "DBus ID type query result:" << result;
    return result;
}

CdaThread::CdaThread(QObject *parent) : QThread(parent), m_cdaStat(CDROM_INVALID)
{
    // qCDebug(dmMusic) << "Creating CDA thread";
}

QList<DMusic::MediaMeta> CdaThread::getCdaMetaInfo()
{
    // qCDebug(dmMusic) << "Getting CDA meta info, track count:" << m_mediaList.size();
    return m_mediaList;
}

void CdaThread::doQuery()
{
    // qCDebug(dmMusic) << "Starting CDA query thread";
    start();
}

input_item_node_t *CdaThread::getInputNode()
{
    // qCDebug(dmMusic) << "Getting input node for CDA";
    input_item_NewExt_func input_item_NewExt_fc = (input_item_NewExt_func)DynamicLibraries::instance()->resolve("input_item_NewExt");
    vlc_stream_NewURL_func vlc_stream_NewURL_fc = (vlc_stream_NewURL_func)DynamicLibraries::instance()->resolve("vlc_stream_NewURL");
    input_item_node_Create_func input_item_node_Create_fc = (input_item_node_Create_func)DynamicLibraries::instance()->resolve("input_item_node_Create");
    input_item_Release_func input_item_Release_fc = (input_item_Release_func)DynamicLibraries::instance()->resolve("input_item_Release");
    vlc_stream_ReadDir_func vlc_stream_ReadDir_fc = (vlc_stream_ReadDir_func)DynamicLibraries::instance()->resolve("vlc_stream_ReadDir");
    vlc_stream_Delete_func vlc_stream_Delete_fc = (vlc_stream_Delete_func)DynamicLibraries::instance()->resolve("vlc_stream_Delete");

    input_item_node_t *p_items = nullptr;
    QStringList strcdalist = getCDADirectory();

    if (strcdalist.size() < 0) {
        qCWarning(dmMusic) << "No CDA directories found";
        return p_items;
    }

    QString strcda = strcdalist.at(0);
    input_item_t *p_input = input_item_NewExt_fc(strcda.toUtf8().data(), "access_demux", 0, ITEM_TYPE_DISC, ITEM_LOCAL);
    if (!p_input) {
        qCWarning(dmMusic) << "Failed to create input item for CD drive";
        return p_items;
    }

    Q_ASSERT(m_play_t);

    stream_t *pstream = vlc_stream_NewURL_fc((vlc_object_t *)m_play_t, strcda.toUtf8().data()); //打开CD，读取流，该操作较耗时
    if (!pstream) {
        qCWarning(dmMusic) << "Failed to create stream for CD";
        return p_items;
    }
    p_items = input_item_node_Create_fc(p_input);
    input_item_Release_fc(p_input);
    int ret = vlc_stream_ReadDir_fc(pstream, p_items);//读取CD中的节点信息
    // qCDebug(dmMusic) << "Read CD directory result:" << ret;
    //释放stream流
    vlc_stream_Delete_fc(pstream);
    return p_items;
}

QString CdaThread::GetCdRomString()
{
    // qCDebug(dmMusic) << "Getting CDROM string";
    QString strcda = "sr0"; //cdrom关键字
    QDBusInterface blockinterface("org.freedesktop.UDisks2", "/org/freedesktop/UDisks2/Manager",
                                  "org.freedesktop.UDisks2.Manager",
                                  QDBusConnection::systemBus());
    QDBusReply<QVariant> reply = blockinterface.call(QLatin1String("GetBlockDevices"), QVariantMap());

    if (reply.isValid()) {
        qCDebug(dmMusic) << "DBus reply is valid";
        QVariantList objectPathList = reply.value().toList();

        for (const QVariant &variant : objectPathList) {
            QString tmp = variant.value<QDBusObjectPath>().path();
            QString strdev = tmp.mid(tmp.lastIndexOf("/") + 1, tmp.size() - tmp.lastIndexOf("/"));
            if (strdev.compare(strcda) == 0) {
                // qCDebug(dmMusic) << "Found CDROM device:" << tmp;
                return tmp;
            }
        }
    }
    return QString();
}

void CdaThread::setCdaState(CdaThread::CdromState stat)
{
    // qCDebug(dmMusic) << "Setting CDA state from" << m_cdaStat << "to" << stat;
    if (stat != CDROM_MOUNT_WITH_CD) {
        // qCDebug(dmMusic) << "CDA state is not CDROM_MOUNT_WITH_CD, setting to CDROM_INVALID";
        stat = CDROM_INVALID;
    }

    if (m_cdaStat == stat) {
        // qCDebug(dmMusic) << "CDA state unchanged, sleeping thread";
        // 线程中做休眠
        QThread::sleep(1); //状态一致时，统一休眠
        return;
    }
    
    m_cdaStat = stat;
    /**
     * 状态更改后再发送cda状态
     **/
    emit sigSendCdaStatus(m_cdaStat);
    /**
     * 非CDROM_MOUNT_WITH_CD清空缓存
     **/
    if (stat != CDROM_MOUNT_WITH_CD) {
        // qCDebug(dmMusic) << "Clearing media list for non-CD state";
        m_mediaList.clear();
    }
}

void CdaThread::run()
{
    // qCDebug(dmMusic) << "Starting CDA thread";
    while (m_needRun) {
        QString strcdrom = GetCdRomString();
        /**
         * 没有cdrom，说明光驱已移出或损坏
         * */
        if (strcdrom.isEmpty()) {
            setCdaState(CDROM_INVALID);
            continue;
        }

        QDBusInterface udiskInterface("org.freedesktop.UDisks2", strcdrom, "org.freedesktop.UDisks2.Block");
        qulonglong blocksize = 0;
        QString type;
        if (udiskInterface.isValid()) {
            blocksize = udiskInterface.property("Size").toULongLong();
            type = udiskInterface.property("IdType").toString();
        }
        /**
         * 过滤空的光盘和文件类型为udf、iso9660格式的光盘
         **/
        if (blocksize == 0 || type == "iso9660" || queryIdTypeFormDbus().toLower() == "udf") {
            setCdaState(CDROM_MOUNT_WITHOUT_CD);
            continue;
        }

        /**
         * 当状态不一致时，现在去读取节点信息并发送相关信号
         * */
        if (m_cdaStat != CDROM_MOUNT_WITH_CD) {
            /**
             * 当blocksize有效时，可以开始读取cda节点名
             * */
            input_item_node_t *p_items = getInputNode();
            /**
             * 防止线程正在读取cd数据流时，用户弹出cd盘
             * */
            if (!p_items) {
                setCdaState(CDROM_INVALID);
                continue;
            }

            m_mediaList.clear();
            for (int i = 0; i < p_items->i_children; i++) {
                input_item_node_t *child = p_items->pp_children[i];
                if (child != nullptr) {
                    // qCDebug(dmMusic) << __FUNCTION__ << "thread id:" << QThread::currentThread() \
                    //          << "name:" << child->p_item->psz_name << "duration:" << child->p_item->i_duration;
                    //空光盘有可能节点不为空，检测持续时间
                    if (child->p_item->i_duration == 0)
                        continue;
                    DMusic::MediaMeta meta;
                    // cd的hash值添加编号防止psz_name相同
                    meta.hash = Utils::filePathHash(QString(child->p_item->psz_name) + QString::number(i + 1));
                    QString strnum = QString::number(i + 1);
                    if (strnum.length() == 1) {
                        strnum.insert(0, '0'); //前置0显示，如01,02，
                    }
                    meta.title = QString("Audio CD - Track %1").arg(strnum); /*child->p_item->psz_name;*/
                    meta.localPath = child->p_item->psz_uri;
                    meta.length = child->p_item->i_duration / 1000; //microseconds to milliseconds
                    meta.track = i + 1; //cd的歌曲索引值，从1开始，用于歌曲切歌
                    meta.filetype = "cdda";
                    meta.album = DmGlobal::unknownAlbumText();
                    meta.artist = DmGlobal::unknownArtistText();
                    meta.mmType = DmGlobal::MimeTypeCDA; //mimetype用于识别cd
                    m_mediaList << meta;
                    // qCDebug(dmMusic) << "Added CD track to media list -" << "Title:" << meta.title 
                    //                << "Track:" << meta.track;
                }
            }

            input_item_node_Delete_func input_item_node_Delete_fc = (input_item_node_Delete_func)DynamicLibraries::instance()->resolve("input_item_node_Delete");
            /**
             * 释放查询的节点input_item_node_t，input_item_node_Delete为遍历释放，无需考虑子节点的释放
             * */
            input_item_node_Delete_fc(p_items);
            /**
             * 发送添加歌单消息
             * */
            if (m_mediaList.size() > 0) {
                setCdaState(CDROM_MOUNT_WITH_CD);
            }
        }
        sleep(1);
    }
}

