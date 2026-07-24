#include <QApplication>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QProgressBar>
#include <QFont>
#include <QFontDatabase>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QDebug>
#include <QMetaType>
#include <QCloseEvent>

#include <optional>
#include <cmath>
#include <utility>
#include <algorithm>

// ------------------------------------------------------------
// Configuration
// ------------------------------------------------------------

static const QString RECITER = "ar.abdurrahmaansudais";
static const QString FONT_FILE = "/usr/share/fonts/opentype/fonts-hosny-amiri/AmiriQuran.ttf";

static const int REQUEST_TIMEOUT_MS = 10000;   // 10 seconds
static const int MAX_RETRIES = 2;
static const double RETRY_BACKOFF_BASE_MS = 500.0; // doubles each retry

// Bismillah is just Surah 1, Ayah 1's own recitation -- no separate asset or
// download needed, it rides along with the normal per-ayah cache/download path.
static const int BISMILLAH_SURAH = 1;
static const int BISMILLAH_AYAH = 1;

// Cache lives next to the executable, so it's easy to find/inspect/delete
// while developing. (Note: unlike the Python script -- which used the
// *source file's* directory -- this uses the binary's directory, since a
// compiled app has no meaningful "script location".)
static QString scriptDir() { return QCoreApplication::applicationDirPath(); }
static QString cacheDir() { return scriptDir() + "/.cache"; }
static QString metadataCacheDir() { return cacheDir() + "/metadata"; }
static QString audioCacheDir() { return cacheDir() + "/audio"; }

// Hardcoded extra local mp3/m4a files that get appended to the END of the
// playlist (e.g. duas, other reciters, etc). Edit this JSON file -- no code
// changes needed. Paths can be absolute or relative to the executable's folder.
static QString customPlaylistJsonPath() { return scriptDir() + "/custom_playlist.json"; }

// Label shown on the separator row inserted before the custom/dua entries.
static const QString CUSTOM_PLAYLIST_SEPARATOR_LABEL = "Duas";

// Playlist group: surah, start ayah, end ayah, description
struct PlaylistGroup {
    int surah;
    int startAyah;
    int endAyah;
    QString description;
};

// Every group opens with Bismillah except At-Tawbah (9), by convention.
static const QVector<PlaylistGroup> PLAYLIST = {
    {1, 1, 7, "Surah Fatiha (Full)"},
    {2, 1, 5, "Surah Al Baqarah 1-5"},
    {2, 102, 103, "Surah Al Baqarah 102"},
    {2, 163, 164, "Surah Al Baqarah 163"},
    {2, 255, 257, "Ayatul Kursi & following"},
    {2, 285, 286, "Surah Al Baqarah 285"},
    {3, 18, 19, "Surah Al Imran 18-19"},
    {3, 26, 27, "Surah Al Imran 26-27"},
    {3, 160, 160, "Surah Al Imran 160"},
    {4, 76, 76, "Surah Al Nisaa 76"},
    {5, 118, 118, "Surah Al Maaidah 118"},
    {6, 17, 18, "Surah Al Anaam 17-18"},
    {7, 23, 23, "Surah Al Araaf 23"},
    {7, 115, 122, "Surah Al Araaf 115-122"},
    {7, 179, 179, "Surah Al Araaf 179"},
    {9, 51, 51, "Surah Al Tawbah 51"},  // no Bismillah -- see needsBismillah()
    {10, 76, 82, "Surah Al Yunus 76-82"},
    {10, 107, 107, "Surah Al Yunus 107"},
    {13, 28, 29, "Surah Raad 28-29"},
    {17, 32, 32, "Surah Al Isra 32"},
    {17, 81, 82, "Surah Al Isra 81-82"},
    {17, 97, 97, "Surah Al Isra 97"},
    {20, 25, 28, "Surah Al Taha 25-28"},
    {20, 65, 76, "Surah Al Taha 65-76"},
    {21, 87, 87, "Surah Al Anbiya 87"},
    {23, 115, 118, "Surah Al Muminun 115-118"},
    {28, 24, 24, "Surah Al Qasas 24"},
    {31, 27, 27, "Surah Al Luqman 27"},
    {32, 13, 14, "Surah Al Sajdah 13-14"},
    {35, 2, 2, "Surah Al Fatir 2"},
    {35, 36, 37, "Surah Al Fatir 36-37"},
    {36, 1, 10, "Surah Al Yaseen 1-10"},
    {37, 1, 10, "Surah Al Saffat 1-10"},
    {40, 59, 60, "Surah Al Ghafir 59-60"},
    {54, 10, 10, "Surah Al Qamar 10"},
    {55, 33, 36, "Surah Al Rahman 33-36"},
    {58, 19, 21, "Surah Al Mujadila 19-21"},
    {59, 18, 24, "Surah Al Hashr 18-24"},
    {72, 1, 28, "Surah Al Jinn (Full)"},
    {85, 1, 22, "Surah Al Burooj (Full)"},
    {99, 1, 8, "Surah Al Zalzalah (Full)"},
    {109, 1, 6, "Surah Al-Kafirun (Full)"},
    {112, 1, 4, "Surah Ikhlas (Full)"},
    {113, 1, 5, "Surah Falaq (Full)"},
    {114, 1, 6, "Surah An-Nas (Full)"},
};

static bool needsBismillah(int surah) { return surah != 1; }

// ------------------------------------------------------------
// Data model for a flattened playback queue item
// ------------------------------------------------------------

struct QueueItem {
    QString kind; // "bismillah" or "ayah"
    int surah = 0;
    int ayah = 0;
    QString description;
    QString groupDesc;
};

struct LoadedEntry {
    QString kind; // "bismillah", "ayah", "custom", or "separator"
    int surah = 0;
    int ayah = 0;
    QString description;
    QString audioFile;
    QJsonObject data;
    QJsonObject surahData;
    QString error; // empty == no error
};

Q_DECLARE_METATYPE(LoadedEntry)
using LoadedEntryBatch = QVector<QPair<int, LoadedEntry>>;
Q_DECLARE_METATYPE(LoadedEntryBatch)

static QVector<QueueItem> buildQueue() {
    QVector<QueueItem> queue;
    for (const auto &g : PLAYLIST) {
        if (needsBismillah(g.surah)) {
            QueueItem b;
            b.kind = "bismillah";
            b.surah = BISMILLAH_SURAH;
            b.ayah = BISMILLAH_AYAH;
            b.groupDesc = g.description;
            queue.append(b);
        }
        for (int ayah = g.startAyah; ayah <= g.endAyah; ++ayah) {
            QueueItem it;
            it.kind = "ayah";
            it.surah = g.surah;
            it.ayah = ayah;
            it.description = g.description;
            queue.append(it);
        }
    }
    return queue;
}

// ------------------------------------------------------------
// Caching + networking helpers
// ------------------------------------------------------------

static void ensureDirs() {
    QDir().mkpath(metadataCacheDir());
    QDir().mkpath(audioCacheDir());
}

static QString audioCachePath(int surah, int ayah) {
    return QString("%1/%2_%3.mp3").arg(audioCacheDir()).arg(surah).arg(ayah);
}

static QString metadataCachePath(int surah) {
    return QString("%1/surah_%2_%3.json").arg(metadataCacheDir()).arg(surah).arg(RECITER);
}

// GET with a timeout and a couple of retries with backoff. Sets *error on
// final failure so the caller can handle/log it per-item.
static QByteArray requestWithRetry(QNetworkAccessManager *nam, const QString &urlStr, QString *error) {
    QString lastError;
    for (int attempt = 0; attempt <= MAX_RETRIES; ++attempt) {
        QNetworkRequest req{QUrl(urlStr)};
        QNetworkReply *reply = nam->get(req);

        QEventLoop loop;
        QTimer timeoutTimer;
        timeoutTimer.setSingleShot(true);
        QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        timeoutTimer.start(REQUEST_TIMEOUT_MS);
        loop.exec();

        bool timedOut = !reply->isFinished();
        if (timedOut) {
            reply->abort();
        }

        if (!timedOut && reply->error() == QNetworkReply::NoError) {
            QByteArray bytes = reply->readAll();
            reply->deleteLater();
            if (error) error->clear();
            return bytes;
        }

        lastError = timedOut ? QStringLiteral("Request timed out") : reply->errorString();
        reply->deleteLater();

        if (attempt < MAX_RETRIES) {
            QThread::msleep(static_cast<unsigned long>(RETRY_BACKOFF_BASE_MS * std::pow(2, attempt)));
        }
    }
    if (error) *error = lastError;
    return QByteArray();
}

static QJsonObject getCachedSurahMetadata(int surah, bool *found) {
    *found = false;
    QFile f(metadataCachePath(surah));
    if (!f.exists()) return {};
    if (!f.open(QIODevice::ReadOnly)) {
        // Narrow catch equivalent: file exists but couldn't be read -> refetch.
        qWarning() << "Corrupt metadata cache for surah" << surah << "(unreadable), refetching";
        return {};
    }
    QByteArray bytes = f.readAll();
    f.close();
    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(bytes, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "Corrupt metadata cache for surah" << surah << "refetching:" << perr.errorString();
        return {};
    }
    *found = true;
    return doc.object();
}

static void saveCachedSurahMetadata(int surah, const QJsonObject &data) {
    ensureDirs();
    QFile f(metadataCachePath(surah));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(data).toJson(QJsonDocument::Indented));
    }
}

// Get surah data - checks cache first, then downloads entire surah at once.
static QJsonObject getSurahData(QNetworkAccessManager *nam, int surah, QString *error) {
    bool found = false;
    QJsonObject cached = getCachedSurahMetadata(surah, &found);
    if (found) {
        qInfo() << "Using cached surah" << surah << "metadata";
        return cached;
    }

    qInfo() << "Downloading surah" << surah << "metadata...";
    QString url = QString("https://api.alquran.cloud/v1/surah/%1/%2").arg(surah).arg(RECITER);
    QString err;
    QByteArray bytes = requestWithRetry(nam, url, &err);
    if (!err.isEmpty()) {
        *error = err;
        return {};
    }

    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(bytes, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = "Invalid JSON response for surah metadata";
        return {};
    }
    QJsonObject data = doc.object().value("data").toObject();
    saveCachedSurahMetadata(surah, data);
    return data;
}

// Download audio only if not already cached. Returns the path; sets
// *wasCached and *error appropriately.
static QString ensureAudioCached(QNetworkAccessManager *nam, int surah, int ayah,
                                  const QString &audioUrl, bool *wasCached, QString *error) {
    QString audioFile = audioCachePath(surah, ayah);
    if (QFile::exists(audioFile)) {
        *wasCached = true;
        return audioFile;
    }

    ensureDirs();
    qInfo() << "Downloading audio for" << surah << ":" << ayah << "...";
    QString err;
    QByteArray bytes = requestWithRetry(nam, audioUrl, &err);
    if (!err.isEmpty()) {
        *error = err;
        return {};
    }

    QFile f(audioFile);
    if (!f.open(QIODevice::WriteOnly)) {
        *error = "Failed to write audio file: " + audioFile;
        return {};
    }
    f.write(bytes);
    f.close();
    *wasCached = false;
    return audioFile;
}

// O(1) lookup instead of scanning surahData["ayahs"] per ayah.
static QHash<int, QJsonObject> buildAyahLookup(const QJsonObject &surahData) {
    QHash<int, QJsonObject> lookup;
    const QJsonArray ayahs = surahData.value("ayahs").toArray();
    for (const QJsonValue &v : ayahs) {
        QJsonObject a = v.toObject();
        lookup.insert(a.value("numberInSurah").toInt(), a);
    }
    return lookup;
}

// Load the hardcoded extra local mp3/m4a files (defined in
// custom_playlist.json) that play at the end of the playlist.
//
// Expected JSON shape:
// [
//   {
//     "path": "extras/dua1.mp3",
//     "title": "...",
//     "body": "..."
//   },
//   {"path": "/absolute/path/to/file.m4a", "title": "Some Title", "body": "..."}
// ]
//
// `path` may be relative (resolved against the executable's folder) or
// absolute. `title` is optional and defaults to the filename. `body` is
// optional and holds the Arabic text shown the same way ayahs are -- leave
// it out or empty if you don't have text for that file.
static QVector<LoadedEntry> loadCustomEntries() {
    QVector<LoadedEntry> entries;
    QFile f(customPlaylistJsonPath());
    if (!f.exists()) return entries;

    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "Corrupt custom playlist json, skipping (unreadable)";
        return entries;
    }
    QByteArray bytes = f.readAll();
    f.close();

    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(bytes, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isArray()) {
        qWarning() << "Corrupt custom playlist json, skipping:" << perr.errorString();
        return entries;
    }

    for (const QJsonValue &v : doc.array()) {
        QJsonObject item = v.toObject();
        QString rawPath = item.value("path").toString();
        QString path = QDir::isAbsolutePath(rawPath) ? rawPath : QDir(scriptDir()).filePath(rawPath);
        QString title = item.value("title").toString();
        if (title.isEmpty()) title = QFileInfo(path).fileName();
        QString body = item.value("body").toString();
        bool exists = QFileInfo::exists(path) && QFileInfo(path).isFile();

        LoadedEntry entry;
        entry.kind = "custom";
        entry.surah = 0;
        entry.ayah = 0;
        entry.description = title;
        entry.audioFile = path;
        QJsonObject data;
        data["text"] = body;
        entry.data = data;
        entry.error = exists ? QString() : QString("File not found: %1").arg(path);
        entries.append(entry);

        if (!exists) qWarning() << "Custom playlist file missing:" << path;
    }
    return entries;
}

// ------------------------------------------------------------
// Background loader (QThread, so networking never blocks the UI)
// ------------------------------------------------------------

class PlaylistLoader : public QThread {
    Q_OBJECT
public:
    using QThread::QThread;

signals:
    void progress(int loaded, int total, int cached, int downloaded);
    void itemsLoaded(LoadedEntryBatch batch);
    void finishedLoading(int loaded, int cached, int downloaded);

protected:
    void run() override {
        QNetworkAccessManager nam; // thread-affine: created inside run()

        QVector<QueueItem> queue = buildQueue();
        const int total = queue.size();
        int loaded = 0, cachedCount = 0, downloadedCount = 0;

        QHash<int, QJsonObject> surahDataCache;
        QHash<int, QHash<int, QJsonObject>> ayahLookupCache;
        LoadedEntryBatch batch;
        const int BATCH_SIZE = 10;

        for (int index = 0; index < queue.size(); ++index) {
            const QueueItem &item = queue.at(index);

            LoadedEntry entry;
            entry.kind = item.kind;
            entry.surah = item.surah;
            entry.ayah = item.ayah;
            entry.description = !item.description.isEmpty() ? item.description : item.groupDesc;

            QString err;

            if (!surahDataCache.contains(item.surah)) {
                QString surahErr;
                QJsonObject sd = getSurahData(&nam, item.surah, &surahErr);
                if (!surahErr.isEmpty()) {
                    err = surahErr;
                } else {
                    surahDataCache.insert(item.surah, sd);
                    ayahLookupCache.insert(item.surah, buildAyahLookup(sd));
                }
            }

            if (err.isEmpty()) {
                const QJsonObject &surahData = surahDataCache[item.surah];
                const QHash<int, QJsonObject> &lookup = ayahLookupCache[item.surah];
                if (!lookup.contains(item.ayah)) {
                    err = QString("Ayah %1 not found in surah %2").arg(item.ayah).arg(item.surah);
                } else {
                    QJsonObject ayahData = lookup.value(item.ayah);
                    QString audioUrl = ayahData.value("audio").toString();
                    if (audioUrl.isEmpty()) {
                        audioUrl = QString("https://cdn.alquran.cloud/media/audio/ayah/%1/%2_%3.mp3")
                                       .arg(RECITER).arg(item.surah).arg(item.ayah);
                    }
                    bool wasCached = false;
                    QString audioErr;
                    QString audioFile = ensureAudioCached(&nam, item.surah, item.ayah, audioUrl,
                                                           &wasCached, &audioErr);
                    if (!audioErr.isEmpty()) {
                        err = audioErr;
                    } else {
                        if (wasCached) ++cachedCount; else ++downloadedCount;
                        entry.audioFile = audioFile;
                        entry.data = ayahData;
                        entry.surahData = surahData;
                    }
                }
            }

            // Per-item failure only -- a bad ayah no longer marks its whole
            // surah/group as errored.
            if (!err.isEmpty()) {
                entry.error = err;
                qWarning() << "Failed to load" << item.surah << ":" << item.ayah << "-" << err;
            } else {
                qInfo() << "Loaded:" << entry.description;
            }

            ++loaded;
            batch.append(qMakePair(index, entry));

            if (batch.size() >= BATCH_SIZE || loaded == total) {
                emit itemsLoaded(batch);
                batch.clear();
            }
            emit progress(loaded, total, cachedCount, downloadedCount);
        }

        emit finishedLoading(loaded, cachedCount, downloadedCount);
    }
};

// ------------------------------------------------------------
// Font loading
// ------------------------------------------------------------

static QString loadQuranFont() {
    QString fontFamily = "Arial";
    if (QFile::exists(FONT_FILE)) {
        int fontId = QFontDatabase::addApplicationFont(FONT_FILE);
        if (fontId != -1) {
            const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
            if (!families.isEmpty()) fontFamily = families.first();
        }
    }
    qInfo() << "Using font:" << fontFamily;
    return fontFamily;
}

// ------------------------------------------------------------
// Main Window
// ------------------------------------------------------------

class QuranPlayer : public QWidget {
    Q_OBJECT
public:
    explicit QuranPlayer(const QString &fontFamily, QWidget *parent = nullptr)
        : QWidget(parent), m_fontFamily(fontFamily) {
        m_player = new QMediaPlayer(this);
        m_audioOutput = new QAudioOutput(this);
        m_audioOutput->setVolume(1.0);
        m_player->setAudioOutput(m_audioOutput);

        setupUi();

        connect(m_player, &QMediaPlayer::mediaStatusChanged, this, &QuranPlayer::onMediaStatusChanged);
        connect(m_player, &QMediaPlayer::errorOccurred, this, &QuranPlayer::onPlayerError);
        connect(m_playlistWidget, &QListWidget::itemDoubleClicked, this, &QuranPlayer::onItemDoubleClicked);

        startLoading();
    }

protected:
    void closeEvent(QCloseEvent *event) override {
        if (m_loader && m_loader->isRunning()) {
            m_loader->quit();
            m_loader->wait(2000);
        }
        QWidget::closeEvent(event);
    }

private:
    // ---------------- UI setup ----------------

    void setupUi() {
        setWindowTitle("Ruqya Quran");
        resize(1400, 800);

        auto *mainLayout = new QHBoxLayout(this);

        auto *leftPanel = new QWidget();
        auto *leftLayout = new QVBoxLayout(leftPanel);

        auto *playlistLabel = new QLabel(" Playlist");
        playlistLabel->setFont(QFont("Arial", 14, QFont::Bold));
        leftLayout->addWidget(playlistLabel);

        m_playlistWidget = new QListWidget();
        m_playlistWidget->setFont(QFont("Arial", 12));
        leftLayout->addWidget(m_playlistWidget);

        m_progressBar = new QProgressBar();
        m_progressBar->setVisible(false);
        leftLayout->addWidget(m_progressBar);

        m_progressLabel = new QLabel("Ready");
        leftLayout->addWidget(m_progressLabel);

        mainLayout->addWidget(leftPanel, 1);

        auto *rightPanel = new QWidget();
        auto *rightLayout = new QVBoxLayout(rightPanel);

        m_titleLabel = new QLabel("Select a verse from the playlist");
        m_titleLabel->setAlignment(Qt::AlignCenter);
        m_titleLabel->setFont(QFont("Arial", 16));
        rightLayout->addWidget(m_titleLabel);

        m_ayahLabel = new QLabel("");
        m_ayahLabel->setAlignment(Qt::AlignCenter);
        m_ayahLabel->setLayoutDirection(Qt::RightToLeft);
        m_ayahLabel->setWordWrap(true);
        m_ayahLabel->setFont(QFont(m_fontFamily, 30));
        rightLayout->addWidget(m_ayahLabel, 1);

        m_statusLabel = new QLabel("Ready");
        m_statusLabel->setAlignment(Qt::AlignCenter);
        rightLayout->addWidget(m_statusLabel);

        auto *buttonLayout = new QHBoxLayout();
        m_playBtn = new QPushButton("\u25B6 Play");
        m_pauseBtn = new QPushButton("\u23F8 Pause");
        m_stopBtn = new QPushButton("\u25A0 Stop");
        m_prevBtn = new QPushButton("\u23EE Previous");
        m_nextBtn = new QPushButton("Next \u23ED");

        buttonLayout->addWidget(m_prevBtn);
        buttonLayout->addWidget(m_playBtn);
        buttonLayout->addWidget(m_pauseBtn);
        buttonLayout->addWidget(m_stopBtn);
        buttonLayout->addWidget(m_nextBtn);
        rightLayout->addLayout(buttonLayout);

        auto *navLayout = new QHBoxLayout();
        m_autoPlayCheck = new QPushButton("\U0001F501 Auto-play: ON");
        m_autoPlayCheck->setCheckable(true);
        m_autoPlayCheck->setChecked(true);
        navLayout->addWidget(m_autoPlayCheck);
        rightLayout->addLayout(navLayout);

        mainLayout->addWidget(rightPanel, 2);

        connect(m_playBtn, &QPushButton::clicked, this, &QuranPlayer::playCurrent);
        connect(m_pauseBtn, &QPushButton::clicked, m_player, &QMediaPlayer::pause);
        connect(m_stopBtn, &QPushButton::clicked, this, &QuranPlayer::stopPlayback);
        connect(m_prevBtn, &QPushButton::clicked, this, &QuranPlayer::playPrevious);
        connect(m_nextBtn, &QPushButton::clicked, this, &QuranPlayer::playNext);
        connect(m_autoPlayCheck, &QPushButton::toggled, this, &QuranPlayer::toggleAutoPlay);
    }

    // ---------------- Loading ----------------

    void startLoading() {
        qInfo() << QString("=").repeated(60);
        qInfo() << "Loading playlist (threaded, cache dir:" << cacheDir() << ")...";
        qInfo() << QString("=").repeated(60);

        m_playlistWidget->clear();
        m_playlistData.clear();
        m_progressBar->setVisible(true);
        m_progressBar->setValue(0);

        m_loader = new PlaylistLoader(this);
        connect(m_loader, &PlaylistLoader::progress, this, &QuranPlayer::onLoadProgress);
        connect(m_loader, &PlaylistLoader::itemsLoaded, this, &QuranPlayer::onItemsLoaded);
        connect(m_loader, &PlaylistLoader::finishedLoading, this, &QuranPlayer::onLoadFinished);
        m_loader->start();
    }

private slots:
    void onLoadProgress(int loaded, int total, int cached, int downloaded) {
        m_progressBar->setMaximum(total);
        m_progressBar->setValue(loaded);
        m_progressLabel->setText(
            QString("Loading %1/%2... (cached: %3, downloaded: %4)").arg(loaded).arg(total).arg(cached).arg(downloaded));
    }

    void onItemsLoaded(LoadedEntryBatch batch) {
        if (batch.isEmpty()) return;

        int maxIndex = 0;
        for (const auto &pr : batch) maxIndex = std::max(maxIndex, pr.first);

        while (m_playlistData.size() <= maxIndex) {
            m_playlistData.append(std::nullopt);
            m_playlistWidget->addItem("Loading\u2026");
        }

        for (const auto &pr : batch) {
            int index = pr.first;
            const LoadedEntry &entry = pr.second;
            m_playlistData[index] = entry;

            QString itemText;
            if (!entry.error.isEmpty()) {
                itemText = QString("\u274C %1 (Error)").arg(entry.description);
            } else if (entry.kind == "bismillah") {
                itemText = "\u2026\u2026\u2026";
            } else {
                QString surahName = entry.surahData.value("englishName").toString();
                itemText = QString("%1 - %2 %3").arg(entry.description, surahName).arg(entry.ayah);
            }
            m_playlistWidget->item(index)->setText(itemText);
        }

        if (m_currentIndex == -1) {
            auto first = firstValidIndex();
            if (first.has_value()) {
                m_playlistWidget->setCurrentRow(*first);
                showAyah(*first);
            }
        }
    }

    void onLoadFinished(int loaded, int cached, int downloaded) {
        m_progressBar->setVisible(false);
        m_progressLabel->setText(QString("\u2713 Loaded %1 items (cached: %2, downloaded: %3)").arg(loaded).arg(cached).arg(downloaded));
        qInfo() << QString("=").repeated(60);
        qInfo() << "Loaded" << loaded << "items total (cached:" << cached << ", downloaded:" << downloaded << ")";
        qInfo() << QString("=").repeated(60);

        appendCustomEntries();
    }

    void onItemDoubleClicked(QListWidgetItem *item) {
        playSelected(m_playlistWidget->row(item));
    }

    void onMediaStatusChanged(QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia) {
            m_statusLabel->setText("Finished");
            if (m_autoPlayCheck->isChecked()) {
                QTimer::singleShot(0, this, &QuranPlayer::playNext);
            }
        } else if (status == QMediaPlayer::LoadedMedia) {
            m_statusLabel->setText("Loaded, ready to play");
        } else if (status == QMediaPlayer::BufferingMedia) {
            m_statusLabel->setText("Buffering...");
        }
    }

    void onPlayerError(QMediaPlayer::Error error, const QString &errorString) {
        Q_UNUSED(errorString);
        static const QHash<QMediaPlayer::Error, QString> errorMessages = {
            {QMediaPlayer::NoError, "No error"},
            {QMediaPlayer::ResourceError, "Resource error"},
            {QMediaPlayer::FormatError, "Format error"},
            {QMediaPlayer::NetworkError, "Network error"},
            {QMediaPlayer::AccessDeniedError, "Access denied"},
        };
        m_statusLabel->setText("Player Error: " + errorMessages.value(error, QString::number(error)));
        qWarning() << "Player error:" << error;
    }

    void toggleAutoPlay(bool checked) {
        m_autoPlayCheck->setText(QString("\U0001F501 Auto-play: %1").arg(checked ? "ON" : "OFF"));
    }

private:
    void appendCustomEntries() {
        QVector<LoadedEntry> customEntries = loadCustomEntries();
        if (customEntries.isEmpty()) return;

        addSeparator(CUSTOM_PLAYLIST_SEPARATOR_LABEL);

        for (const LoadedEntry &entry : customEntries) {
            m_playlistData.append(entry);
            QString itemText = !entry.error.isEmpty()
                                    ? QString("\u274C %1 (Error)").arg(entry.description)
                                    : QString("\U0001F3B5 %1").arg(entry.description);
            m_playlistWidget->addItem(itemText);
        }

        qInfo() << "Appended" << customEntries.size() << "custom entries to playlist";
    }

    // Insert a non-selectable, non-playable divider row into both the list
    // widget and playlistData. Kept generic (label-based) so it can be
    // reused anywhere a visual break between sections is needed.
    void addSeparator(const QString &label) {
        LoadedEntry separatorEntry;
        separatorEntry.kind = "separator";
        separatorEntry.description = label;
        m_playlistData.append(separatorEntry);

        auto *item = new QListWidgetItem(QString("\u2500\u2500\u2500\u2500\u2500 %1 \u2500\u2500\u2500\u2500\u2500").arg(label));
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable & ~Qt::ItemIsEnabled);
        QFont font = item->font();
        font.setItalic(true);
        item->setFont(font);
        m_playlistWidget->addItem(item);
    }

    // ---------------- Playability ----------------

    // Single source of truth for "can this row actually be played". Every
    // navigation/playback method below routes through this instead of
    // re-deriving the rule.
    bool isPlayable(const std::optional<LoadedEntry> &entry) const {
        return entry.has_value() && entry->error.isEmpty() && entry->kind != "separator";
    }

    // ---------------- Display ----------------

    void showAyah(int index) {
        if (index < 0 || index >= m_playlistData.size()) return;
        const auto &entryOpt = m_playlistData.at(index);
        if (!isPlayable(entryOpt)) {
            m_titleLabel->setText("Error: Failed to load this item");
            m_ayahLabel->setText("");
            return;
        }
        const LoadedEntry &entry = *entryOpt;

        if (entry.kind == "bismillah") {
            m_titleLabel->setText(QString("Bismillah\n%1").arg(entry.description));
            m_ayahLabel->setText(entry.data.value("text").toString());
            m_statusLabel->setText(QString("Bismillah \u2014 %1").arg(entry.description));
        } else if (entry.kind == "custom") {
            m_titleLabel->setText(entry.description);
            m_ayahLabel->setText(entry.data.value("text").toString());
            m_statusLabel->setText(QString("Loaded: %1").arg(entry.description));
        } else {
            const QJsonObject &surahData = entry.surahData;
            QString englishName = surahData.value("englishName").toString("Unknown");
            QString arabicName = surahData.value("name").toString();
            m_titleLabel->setText(QString("%1 (%2)\nAyah %3").arg(englishName, arabicName).arg(entry.ayah));
            m_ayahLabel->setText(entry.data.value("text").toString());
            m_statusLabel->setText(QString("Loaded: %1").arg(entry.description));
        }

        selectRowNoScroll(index);
    }

    // Highlight the currently-playing row without moving the list's scroll
    // position -- the user's manual scroll position is preserved.
    void selectRowNoScroll(int index) {
        m_playlistWidget->setAutoScroll(false);
        m_playlistWidget->setCurrentRow(index);
        m_playlistWidget->setAutoScroll(true);
    }

    // ---------------- Playback ----------------

    void playCurrent() {
        if (m_currentIndex < 0 || m_currentIndex >= m_playlistData.size()) {
            auto first = firstValidIndex();
            if (first.has_value()) {
                playSelected(*first);
            } else {
                m_statusLabel->setText("No valid item to play");
            }
        } else {
            playSelected(m_currentIndex);
        }
    }

    void playSelected(int index) {
        if (index < 0 || index >= m_playlistData.size()) return;
        const auto &entryOpt = m_playlistData.at(index);
        if (!isPlayable(entryOpt)) {
            m_statusLabel->setText("Error: This item could not be loaded");
            return;
        }
        const LoadedEntry &entry = *entryOpt;

        m_currentIndex = index;
        if (!QFile::exists(entry.audioFile)) {
            m_statusLabel->setText(QString("Error: Audio file not found: %1").arg(entry.audioFile));
            return;
        }

        qInfo() << "Playing:" << entry.description << "-" << entry.audioFile;
        m_player->setSource(QUrl::fromLocalFile(entry.audioFile));
        showAyah(index);
        m_player->play();
        QString label = entry.kind == "bismillah" ? "Bismillah" : entry.description;
        m_statusLabel->setText(QString("\u25B6 Playing: %1").arg(label));
    }

    std::optional<int> firstValidIndex() const {
        for (int i = 0; i < m_playlistData.size(); ++i) {
            if (isPlayable(m_playlistData.at(i))) return i;
        }
        return std::nullopt;
    }

    std::optional<int> nextValidIndex(int fromIndex) const {
        for (int i = fromIndex + 1; i < m_playlistData.size(); ++i) {
            if (isPlayable(m_playlistData.at(i))) return i;
        }
        return std::nullopt;
    }

    std::optional<int> prevValidIndex(int fromIndex) const {
        for (int i = fromIndex - 1; i >= 0; --i) {
            if (isPlayable(m_playlistData.at(i))) return i;
        }
        return std::nullopt;
    }

    void playNext() {
        if (m_currentIndex < 0) {
            auto first = firstValidIndex();
            if (first.has_value()) playSelected(*first);
            return;
        }

        auto nxt = nextValidIndex(m_currentIndex);
        if (nxt.has_value()) {
            playSelected(*nxt);
            return;
        }

        if (m_autoPlayCheck->isChecked()) {
            auto first = firstValidIndex();
            if (first.has_value()) {
                playSelected(*first);
                return;
            }
        }

        m_statusLabel->setText("End of playlist");
    }

    void playPrevious() {
        if (m_currentIndex < 0) {
            auto first = firstValidIndex();
            if (first.has_value()) playSelected(*first);
            return;
        }

        auto prev = prevValidIndex(m_currentIndex);
        if (prev.has_value()) {
            playSelected(*prev);
        } else {
            m_statusLabel->setText("Beginning of playlist");
        }
    }

    void stopPlayback() {
        m_player->stop();
        m_statusLabel->setText("Stopped");
    }

private:
    QString m_fontFamily;
    int m_currentIndex = -1;
    QVector<std::optional<LoadedEntry>> m_playlistData;

    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    PlaylistLoader *m_loader = nullptr;

    QListWidget *m_playlistWidget = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_progressLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_ayahLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_playBtn = nullptr;
    QPushButton *m_pauseBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QPushButton *m_prevBtn = nullptr;
    QPushButton *m_nextBtn = nullptr;
    QPushButton *m_autoPlayCheck = nullptr;
};

// ------------------------------------------------------------
// Main
// ------------------------------------------------------------

int main(int argc, char *argv[]) {
    qRegisterMetaType<LoadedEntry>("LoadedEntry");
    qRegisterMetaType<LoadedEntryBatch>("LoadedEntryBatch");

    QApplication app(argc, argv);
    QString fontFamily = loadQuranFont();

    QuranPlayer window(fontFamily);
    window.show();

    return app.exec();
}

#include "main.moc"