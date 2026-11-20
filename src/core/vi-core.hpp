#pragma once

#include <QObject>
#include <QSslSocket>
#include <QTimer>
#include <QMutex>
#include <QColor>
#include <QMap>
#include <QVector>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <cstdint>

struct HotEntry {
    qint64 ts;
    int xp;
};

struct ViewerRecord {
    QString key;
    QString displayName;
    int lifetimeXp = 0;
    int sessionXp = 0;
    int messages = 0;
    qint64 lastSeen = 0;
    qint64 lastDeltaTs = 0;
    int lastDeltaXp = 0;
    QVector<HotEntry> hotLog;
};

struct LeaderboardItem {
    QString name;
    int score = 0;
    int deltaXp = 0;
    bool showDelta = false;
};

struct ActiveEventSnapshot {
    bool active = false;
    QString targetKey;
    QString displayName;
    QString categoryLabel;
    QString rarity;
    QString message;
    bool replied = false;
    QString replyText;
    float progress = 0.0f;
    QColor rarityColor = QColor(239, 92, 155);
};

class VICore : public QObject {
    Q_OBJECT

public:
    static VICore *instance();

    void initialize();
    void shutdown();

    void connectChannel(const QString &channel);
    QString channel() const;
    bool isConnected() const;

    void setRankingMode(const QString &mode);
    QString rankingMode() const;

    void setDemoMode(bool enabled);
    bool isDemoMode() const;

    void setEventsPaused(bool paused);
    bool isEventsPaused() const;

    void triggerRandomEvent(const QString &forcedCategory = QString());
    void simulateTargetReply();
    void resetSessionXp();

    QVector<LeaderboardItem> getTop5Snapshot(int &activeUsersOut, int &nextScanSecOut);
    ActiveEventSnapshot getActiveEventSnapshot();
    QColor getCurrentFlashColor(float &intensityOut);

signals:
    void statusChanged(bool connected, const QString &text);
    void dataUpdated();

private slots:
    void onSocketEncrypted();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onTick();
    void onDemoTick();

private:
    explicit VICore(QObject *parent = nullptr);
    ~VICore() override;

    void handleIrcLine(const QString &line);
    void processChatMessage(const QString &login, const QString &displayName, const QString &text, bool isDemo);
    void handleEventReply(const QString &userKey, const QString &text);
    int computeHotXp(ViewerRecord &rec, qint64 now);
    void loadPools();
    void loadState();
    void saveState();
    void scheduleNextScan();

    mutable QMutex m_mutex;
    QSslSocket *m_socket = nullptr;
    QByteArray m_readBuf;
    QTimer *m_tickTimer = nullptr;
    QTimer *m_demoTimer = nullptr;

    QString m_channel = "ktotakoyben";
    QString m_rankingMode = "hot30";
    bool m_connected = false;
    bool m_demoMode = false;
    bool m_eventsPaused = false;

    QMap<QString, ViewerRecord> m_users;
    QMap<QString, QStringList> m_pools;
    QString m_lastEventUser;
    qint64 m_nextScanAt = 0;

    bool m_eventActive = false;
    QString m_evTargetKey;
    QString m_evDisplayName;
    QString m_evCategoryLabel;
    QString m_evRarity;
    QString m_evMessage;
    bool m_evReplied = false;
    QString m_evReplyText;
    qint64 m_evStartedAt = 0;
    qint64 m_evExpiresAt = 0;
    qint64 m_evCloseAt = 0;
    QColor m_evColor = QColor(239, 92, 155);
};
