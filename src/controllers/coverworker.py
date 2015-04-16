#!/usr/bin/python
# -*- coding: utf-8 -*-


import os
import sys
import copy
import json
from PyQt5.QtCore import QObject, pyqtSignal, pyqtSlot, QThreadPool, QRunnable
from PyQt5.QtGui import QImage
import requests
from log import logger
from .utils import registerContext
from dwidgets import dthread, CoverRunnable
from config.constants import ArtistCoverPath, AlbumCoverPath, SongCoverPath, OnlineSongCoverPath
import urllib


class Cover360Runnable(QRunnable):

    def __init__(self, worker, artist, title, url):
        super(Cover360Runnable, self).__init__()
        self.worker = worker
        self.artist = artist
        self.title = title
        self.url = url

    def run(self):
        localUrl = CoverWorker.onlineSongCoverPath(self.artist, self.title)
        try:
            r = requests.get(self.url)
            with open(localUrl, "wb") as f:
                f.write(r.content)
            self.worker.download360SongCoverSuccessed.emit(self.artist, self.title, localUrl)
        except Exception, e:
            raise e

class CoverWorker(QObject):

    __contextName__ = "CoverWorker"


    downloadArtistCoverSuccessed = pyqtSignal('QString', 'QString')
    downloadAlbumCoverSuccessed = pyqtSignal('QString', 'QString', 'QString')
    download360SongCoverSuccessed = pyqtSignal('QString', 'QString', 'QString')

    allTaskFinished = pyqtSignal()

    updateArtistCover = pyqtSignal('QString', 'QString')
    updateAlbumCover = pyqtSignal('QString', 'QString', 'QString')
    updateOnlineSongCover = pyqtSignal('QString', 'QString', 'QString')


    defaultArtistCover = os.path.join(os.getcwd(), 'skin', 'images','bg1.jpg')
    defaultAlbumCover = os.path.join(os.getcwd(), 'skin', 'images','bg2.jpg')
    defaultSongCover = os.path.join(os.getcwd(), 'skin', 'images','bg3.jpg')
    defaultFolderCover = os.path.join(os.getcwd(), 'skin', 'images','bg4.jpg')

    @registerContext
    def __init__(self, parent=None):
        super(CoverWorker, self).__init__(parent)
        QThreadPool.globalInstance().setMaxThreadCount(4)
        self.artistCovers = {}
        self.albumCovers = {}
        self.onlineSongCovers = {}
        self.taskNumber = 0
        self._artists = set()
        self._albums = set()
        self.initConnect()

    def initConnect(self):
        self.downloadArtistCoverSuccessed.connect(self.cacheArtistCover)
        self.downloadAlbumCoverSuccessed.connect(self.cacheAlbumCover)
        self.download360SongCoverSuccessed.connect(self.cancheSongCover)

    def downloadCoverByUrl(self, mediaUrl, coverUrl):
        filepath = self.getCoverPathByMediaUrl(mediaUrl)
        try:
            r = requests.get(coverUrl)
            with open(filepath, "wb") as f:
                f.write(r.content)
            self.downloadCoverSuccessed.emit(mediaUrl, filepath)
        except:
            pass

    def cacheArtistCover(self, artist, url):
        self.artistCovers[artist]  = url
        self.updateArtistCover.emit(artist, url)

    def cacheAlbumCover(self, artist, album, url):
        self.albumCovers[album] = url
        self.updateAlbumCover.emit(artist, album, url)
        self.taskNumber -= 1

    def cancheSongCover(self, artist, title, url):
        self.onlineSongCovers[title] = url
        self.updateOnlineSongCover.emit(artist, title, url)

    def downloadArtistCover(self, artist):
        f = self.artistCoverPath(artist)
        if os.path.exists(f):
            return
        if ',' in artist:
            artist = artist.split(',')
            for item in artist:
                if item not in self._artists:
                    self._artists.add(item)
                    self.taskNumber += 1
                    d = CoverRunnable(self, item, qtype="artist")
                    QThreadPool.globalInstance().start(d)
        else:
            if artist not in self._artists:
                self._artists.add(artist)
                self.taskNumber += 1
                d = CoverRunnable(self, artist, qtype="artist")
                QThreadPool.globalInstance().start(d)

    def downloadAlbumCover(self, artist, album):
        f = self.albumCoverPath(artist, album)
        if os.path.exists(f):
            return
        key = (artist, album)
        if key not in self._albums:
            self._albums.add(key)
            self.taskNumber += 1
            d = CoverRunnable(self, artist, album, qtype="album")
            QThreadPool.globalInstance().start(d)

    def downloadOnlineSongCover(self, artist, title, url):
        f = self.onlineSongCoverPath(artist, title)
        if os.path.exists(f):
            return
        if url:
            d = Cover360Runnable(self, artist, title, url)
            QThreadPool.globalInstance().start(d)

    @classmethod
    def getCoverPathByArtist(cls, artist):
        if ',' in artist:
            artist = artist.split(',')[0]
        filepath = os.path.join(ArtistCoverPath, artist)
        if isinstance(artist, unicode):
            artist = artist.encode('utf-8')
        encodefilepath = os.path.join(ArtistCoverPath, urllib.quote(artist))
        if os.path.exists(filepath):
            image = QImage()
            if image.load(filepath):
                return encodefilepath
            else:
                return cls.defaultArtistCover
        else:
            return cls.defaultArtistCover

    @classmethod
    def getCoverPathByArtistAlbum(cls, artist, album):
        filename = '%s-%s' % (artist, album)
        filepath = os.path.join(AlbumCoverPath, filename)
        if isinstance(filename, unicode):
            filename = filename.encode('utf-8')
        encodefilepath = os.path.join(AlbumCoverPath, urllib.quote(filename))
        if os.path.exists(filepath):
            image = QImage()
            if image.load(filepath):
                return encodefilepath
            else:
                return cls.defaultAlbumCover
        else:
            return cls.defaultAlbumCover

    @classmethod
    def getCoverPathByArtistSong(cls, artist, title):
        filepath = os.path.join(SongCoverPath, '%s-%s' % (artist, title))
        if os.path.exists(filepath):
            image = QImage()
            if image.load(filepath):
                return filepath
            else:
                return cls.defaultSongCover
        else:
            return cls.defaultSongCover

    @classmethod
    def getOnlineCoverPathByArtistSong(cls, artist, title):
        filepath = os.path.join(OnlineSongCoverPath, '%s-%s' % (artist, title))
        if os.path.exists(filepath):
            image = QImage()
            if image.load(filepath):
                return filepath
            else:
                return cls.defaultSongCover
        else:
            return cls.defaultSongCover

    @classmethod
    def getFolderCover(cls):
        return cls.defaultFolderCover

    @classmethod
    def artistCoverPath(cls, artist):
        filepath = os.path.join(ArtistCoverPath, artist)
        return filepath

    @classmethod
    def isArtistCoverExisted(cls, artist):
        return os.path.exists(cls.artistCoverPath(artist))

    @classmethod
    def albumCoverPath(cls, artist, album):
        filepath = os.path.join(AlbumCoverPath, '%s-%s' % (artist, album))
        return filepath
    @classmethod
    def isAlbumCoverExisted(cls, artist, album):
        return os.path.exists(cls.albumCoverPath(artist, album))

    @classmethod
    def songCoverPath(cls, artist, title):
        filepath = os.path.join(SongCoverPath, '%s-%s' % (artist, title))
        return filepath

    @classmethod
    def isSongCoverExisted(cls, artist, title):
        return os.path.exists(cls.songCoverPath(artist, title))

    @classmethod
    def onlineSongCoverPath(cls, artist, title):
        filepath = os.path.join(OnlineSongCoverPath, '%s-%s' % (artist, title))
        return filepath

    @classmethod
    def isOnlineSongCoverExisted(cls, artist, title):
        return os.path.exists(cls.onlineSongCoverPath(artist, title))
