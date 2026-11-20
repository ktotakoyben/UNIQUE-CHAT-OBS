#include "vi-core.hpp"
#include <obs-module.h>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QSet>
#include <algorithm>

static VICore *g_core = nullptr;

static const QSet<QString> kBots = {
    "nightbot", "streamelements", "moobot", "fossabot", "wizebot", "soundalerts"
};

VICore *VICore::instance()
{
    if (!g_core) {
        g_core = new VICore();
    }
    return g_core;
}

VICore::VICore(QObject *parent) : QObject(parent) {}

VICore::~VICore()
{
    shutdown();
}

void VICore::initialize()
{
    loadPools();
    loadState();
    scheduleNextScan();

    m_socket = new QSslSocket(this);
    connect(m_socket, &QSslSocket::encrypted, this, &VICore::onSocketEncrypted);
    connect(m_socket, &QSslSocket::disconnected, this, &VICore::onSocketDisconnected);
    connect(m_socket, &QSslSocket::readyRead, this, &VICore::onSocketReadyRead);

    m_tickTimer = new QTimer(this);
    connect(m_tickTimer, &QTimer::timeout, this, &VICore::onTick);
    m_tickTimer->start(250);

    m_demoTimer = new QTimer(this);
    connect(m_demoTimer, &QTimer::timeout, this, &VICore::onDemoTick);

    if (!m_channel.isEmpty()) {
        connectChannel(m_channel);
    }
}

void VICore::shutdown()
{
    saveState();
    if (m_tickTimer) m_tickTimer->stop();
    if (m_demoTimer) m_demoTimer->stop();
    if (m_socket) {
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

void VICore::connectChannel(const QString &channel)
{
    QString clean = channel.trimmed().toLower();
    if (clean.startsWith('#')) clean.remove(0, 1);
    if (clean.isEmpty()) return;

    {
        QMutexLocker lock(&m_mutex);
        m_channel = clean;
    }
    saveState();

    if (m_socket) {
        m_readBuf.clear();
        m_socket->abort();
        emit statusChanged(false, "CONNECTING...");
        m_socket->connectToHostEncrypted("irc.chat.twitch.tv", 6697);
    }
}

QString VICore::channel() const
{
    QMutexLocker lock(&m_mutex);
    return m_channel;
}

bool VICore::isConnected() const
{
    QMutexLocker lock(&m_mutex);
    return m_connected;
}

void VICore::setRankingMode(const QString &mode)
{
    {
        QMutexLocker lock(&m_mutex);
        m_rankingMode = mode;
    }
    saveState();
    emit dataUpdated();
}

QString VICore::rankingMode() const
{
    QMutexLocker lock(&m_mutex);
    return m_rankingMode;
}

void VICore::setDemoMode(bool enabled)
{
    {
        QMutexLocker lock(&m_mutex);
        m_demoMode = enabled;
    }
    if (enabled) {
        m_demoTimer->start(2200);
        onDemoTick();
    } else {
        m_demoTimer->stop();
    }
    emit dataUpdated();
}

bool VICore::isDemoMode() const
{
    QMutexLocker lock(&m_mutex);
    return m_demoMode;
}

void VICore::setEventsPaused(bool paused)
{
    QMutexLocker lock(&m_mutex);
    m_eventsPaused = paused;
}

bool VICore::isEventsPaused() const
{
    QMutexLocker lock(&m_mutex);
    return m_eventsPaused;
}

void VICore::resetSessionXp()
{
    {
        QMutexLocker lock(&m_mutex);
        for (auto &u : m_users) {
            u.sessionXp = 0;
            u.hotLog.clear();
        }
    }
    saveState();
    emit dataUpdated();
}

void VICore::onSocketEncrypted()
{
    QString ch;
    {
        QMutexLocker lock(&m_mutex);
        m_connected = true;
        ch = m_channel;
    }
    int rnd = QRandomGenerator::global()->bounded(10000, 99999);
    m_socket->write("CAP REQ :twitch.tv/tags\r\n");
    m_socket->write("PASS SCHMOOPIIE\r\n");
    m_socket->write(QString("NICK justinfan%1\r\n").arg(rnd).toUtf8());
    m_socket->write(QString("JOIN #%1\r\n").arg(ch).toUtf8());
    m_socket->flush();
    emit statusChanged(true, "LIVE // #" + ch);
}

void VICore::onSocketDisconnected()
{
    {
        QMutexLocker lock(&m_mutex);
        m_connected = false;
    }
    emit statusChanged(false, "RECONNECTING...");
    QTimer::singleShot(4000, this, [this]() {
        if (m_socket && !m_connected && !m_channel.isEmpty()) {
            m_socket->connectToHostEncrypted("irc.chat.twitch.tv", 6697);
        }
    });
}

void VICore::onSocketReadyRead()
{
    if (!m_socket) return;
    m_readBuf.append(m_socket->readAll());

    while (true) {
        int idx = m_readBuf.indexOf("\r\n");
        if (idx < 0) break;
        QByteArray rawLine = m_readBuf.left(idx);
        m_readBuf.remove(0, idx + 2);
        handleIrcLine(QString::fromUtf8(rawLine));
    }
}

void VICore::handleIrcLine(const QString &line)
{
    if (line.startsWith("PING")) {
        if (m_socket) {
            m_socket->write("PONG :tmi.twitch.tv\r\n");
            m_socket->flush();
        }
        return;
    }

    if (!line.contains(" PRIVMSG #")) return;

    static const QRegularExpression reMsg(R"(PRIVMSG #[^\s]+ :(.+)$)");
    static const QRegularExpression reUser(R"(:([^!]+)![^@]+@[^\s]+\.tmi\.twitch\.tv PRIVMSG)");
    static const QRegularExpression reDisp(R"(display-name=([^;]*))");

    auto mText = reMsg.match(line);
    auto mUser = reUser.match(line);
    if (mText.hasMatch() && mUser.hasMatch()) {
        QString login = mUser.captured(1);
        auto mDisp = reDisp.match(line);
        QString disp = (mDisp.hasMatch() && !mDisp.captured(1).isEmpty()) ? mDisp.captured(1) : login;
        processChatMessage(login, disp, mText.captured(1), false);
    }
}

void VICore::processChatMessage(const QString &login, const QString &displayName, const QString &text, bool isDemo)
{
    QString key = login.trimmed().toLower();
    if (key.isEmpty() || kBots.contains(key)) return;
    if (text.trimmed().startsWith('!')) return;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool replyTriggered = false;

    {
        QMutexLocker lock(&m_mutex);
        auto &u = m_users[key];
        u.key = key;
        u.displayName = displayName.isEmpty() ? key : displayName;

        if (m_eventActive && !m_evReplied && m_evTargetKey == key) {
            replyTriggered = true;
        }

        if (now - u.lastSeen >= 4000) {
            int words = text.trimmed().split(QRegularExpression(R"(\s+)"), Qt::SkipEmptyParts).size();
            int earned = std::clamp(words, 1, 10);
            u.lifetimeXp += earned;
            u.sessionXp += earned;
            u.messages += 1;
            u.lastSeen = now;
            u.lastDeltaTs = now;
            u.lastDeltaXp = earned;
            u.hotLog.append({now, earned});
        } else {
            u.lastSeen = now;
        }
    }

    if (replyTriggered) {
        handleEventReply(key, text);
    }

    if (!isDemo) saveState();
    emit dataUpdated();
}

void VICore::handleEventReply(const QString &userKey, const QString &text)
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    QMutexLocker lock(&m_mutex);
    if (!m_eventActive || m_evReplied) return;

    m_evReplied = true;
    m_evReplyText = text.left(100);
    m_evCloseAt = now + 5500;

    if (m_users.contains(userKey)) {
        auto &u = m_users[userKey];
        u.lifetimeXp += 25;
        u.sessionXp += 25;
        u.lastDeltaTs = now;
        u.lastDeltaXp = 25;
        u.hotLog.append({now, 25});
    }
}

void VICore::triggerRandomEvent(const QString &forcedCategory)
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    QMutexLocker lock(&m_mutex);

    QVector<QPair<QString, QString>> activePool;
    for (const auto &u : m_users) {
        if (now - u.lastSeen <= 10 * 60 * 1000) {
            activePool.append({u.key, u.displayName});
        }
    }

    if (activePool.isEmpty()) {
        if (forcedCategory.isEmpty()) {
            scheduleNextScan();
            return;
        }
        activePool.append({"viewer_one", "VIEWER_ONE"});
    }

    QVector<QPair<QString, QString>> candidates;
    for (const auto &p : activePool) {
        if (p.first != m_lastEventUser) candidates.append(p);
    }
    if (candidates.isEmpty()) candidates = activePool;

    const auto chosen = candidates[QRandomGenerator::global()->bounded(candidates.size())];
    m_lastEventUser = chosen.first;

    QString cat = forcedCategory;
    if (cat.isEmpty() || !m_pools.contains(cat)) {
        int roll = QRandomGenerator::global()->bounded(100);
        if (roll < 2) cat = "SECRET_LEGENDARY";
        else if (roll < 10) cat = "QUESTIONS";
        else if (roll < 30) cat = "BRAINROT";
        else cat = (QRandomGenerator::global()->bounded(2) == 0) ? "AFFECTION" : "BULLSHIT";
    }

    const QStringList &list = m_pools[cat];
    QString msg = list.isEmpty() ? "{user}, система тебя видит." : list[QRandomGenerator::global()->bounded(list.size())];
    msg.replace("{user}", chosen.second);

    m_eventActive = true;
    m_evTargetKey = chosen.first;
    m_evDisplayName = chosen.second;
    m_evMessage = msg;
    m_evReplied = false;
    m_evReplyText.clear();
    m_evStartedAt = now;
    m_evExpiresAt = now + 35000;
    m_evCloseAt = m_evExpiresAt + 1800;

    if (cat == "SECRET_LEGENDARY") {
        m_evRarity = "LEGENDARY";
        m_evCategoryLabel = "OVERRIDE // CHOSEN ONE";
        m_evColor = QColor(245, 158, 11);
        if (m_users.contains(chosen.first)) {
            m_users[chosen.first].lifetimeXp += 50;
            m_users[chosen.first].sessionXp += 50;
            m_users[chosen.first].hotLog.append({now, 50});
        }
    } else if (cat == "QUESTIONS") {
        m_evRarity = "RARE";
        m_evCategoryLabel = "INTERROGATION // QUERY";
        m_evColor = QColor(239, 92, 155);
    } else if (cat == "BRAINROT") {
        m_evRarity = "UNCOMMON";
        m_evCategoryLabel = "ANOMALY // BRAINROT";
        m_evColor = QColor(56, 189, 248);
    } else {
        m_evRarity = "COMMON";
        m_evCategoryLabel = (cat == "AFFECTION") ? "AFFECTION // SIGNAL" : "DIAGNOSTIC // TROLL";
        m_evColor = QColor(156, 163, 175);
    }

    scheduleNextScan();
}

void VICore::simulateTargetReply()
{
    QString target;
    {
        QMutexLocker lock(&m_mutex);
        if (!m_eventActive || m_evReplied) return;
        target = m_evTargetKey;
    }
    handleEventReply(target, "Я тут, система! Всё под контролем.");
    emit dataUpdated();
}

void VICore::onTick()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool needTrigger = false;

    {
        QMutexLocker lock(&m_mutex);
        if (m_eventActive && now >= m_evCloseAt) {
            m_eventActive = false;
        }
        if (!m_eventActive && !m_eventsPaused && now >= m_nextScanAt) {
            needTrigger = true;
        }
    }

    if (needTrigger) {
        triggerRandomEvent();
    }
}

void VICore::onDemoTick()
{
    static const QVector<QPair<QString, QString>> demoUsers = {
        {"artas_tv", "АРТАС"}, {"neko_byte", "NekoByte"}, {"kuro_dev", "kuro_dev"},
        {"ghost_walker", "GhostWalker"}, {"mira_flux", "MiraFlux"}, {"ben_enjoyer", "BenEnjoyer"}
    };
    static const QStringList demoMsgs = {
        "Привет Бен как дела на стриме",
        "Жесткий клатч был только что",
        "Система работает идеально",
        "Кто сегодня топ один по опыту",
        "Чат живой погнали дальше"
    };

    const auto &u = demoUsers[QRandomGenerator::global()->bounded(demoUsers.size())];
    const auto &m = demoMsgs[QRandomGenerator::global()->bounded(demoMsgs.size())];
    processChatMessage(u.first, u.second, m, true);
}

int VICore::computeHotXp(ViewerRecord &rec, qint64 now)
{
    qint64 cutoff = now - 30 * 60 * 1000;
    int sum = 0;
    QVector<HotEntry> filtered;
    for (const auto &e : rec.hotLog) {
        if (e.ts >= cutoff) {
            filtered.append(e);
            sum += e.xp;
        }
    }
    rec.hotLog = filtered;
    return sum;
}

QVector<LeaderboardItem> VICore::getTop5Snapshot(int &activeUsersOut, int &nextScanSecOut)
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    QMutexLocker lock(&m_mutex);

    activeUsersOut = 0;
    nextScanSecOut = std::max(0, static_cast<int>((m_nextScanAt - now) / 1000));

    QVector<LeaderboardItem> all;
    for (auto &u : m_users) {
        if (now - u.lastSeen <= 10 * 60 * 1000) activeUsersOut++;

        int score = 0;
        if (m_rankingMode == "hot30") score = computeHotXp(u, now);
        else if (m_rankingMode == "session") score = u.sessionXp;
        else score = u.lifetimeXp;

        if (score > 0) {
            LeaderboardItem item;
            item.name = u.displayName;
            item.score = score;
            item.deltaXp = u.lastDeltaXp;
            item.showDelta = (now - u.lastDeltaTs < 1600);
            all.append(item);
        }
    }

    std::sort(all.begin(), all.end(), [](const LeaderboardItem &a, const LeaderboardItem &b) {
        return a.score > b.score;
    });

    if (all.size() > 5) all.resize(5);
    return all;
}

ActiveEventSnapshot VICore::getActiveEventSnapshot()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    QMutexLocker lock(&m_mutex);

    ActiveEventSnapshot snap;
    snap.active = m_eventActive;
    if (!m_eventActive) return snap;

    snap.targetKey = m_evTargetKey;
    snap.displayName = m_evDisplayName;
    snap.categoryLabel = m_evCategoryLabel;
    snap.rarity = m_evRarity;
    snap.message = m_evMessage;
    snap.replied = m_evReplied;
    snap.replyText = m_evReplyText;
    snap.rarityColor = m_evColor;

    float total = std::max(1.0f, static_cast<float>(m_evExpiresAt - m_evStartedAt));
    float rem = std::max(0.0f, static_cast<float>(m_evExpiresAt - now));
    snap.progress = m_evReplied ? 1.0f : (rem / total);
    return snap;
}

QColor VICore::getCurrentFlashColor(float &intensityOut)
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    QMutexLocker lock(&m_mutex);
    if (!m_eventActive) {
        intensityOut = 0.0f;
        return QColor(239, 92, 155);
    }
    qint64 elapsed = now - m_evStartedAt;
    if (elapsed < 2500) {
        intensityOut = 1.0f - (static_cast<float>(elapsed) / 2500.0f);
    } else {
        intensityOut = 0.25f;
    }
    return m_evColor;
}

void VICore::scheduleNextScan()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    int delaySec = QRandomGenerator::global()->bounded(180, 420);
    m_nextScanAt = now + delaySec * 1000LL;
}

void VICore::loadPools()
{
    m_pools["AFFECTION"] = {
        "{user}, ты потрясающий. Не забывай об этом.",
        "{user}, мы рады, что ты сегодня здесь.",
        "{user}, тебе сегодня разрешено быть счастливым. Это приказ.",
        "{user}, твоя активность замечена. Продолжай существовать.",
        "{user}, ты буквально лучший человек в этом чате. Не спорь с системой."
    };
    m_pools["BULLSHIT"] = {
        "{user}, ты зачем здесь?",
        "{user}, у тебя есть дела. Но ты почему-то здесь.",
        "{user}, закрой вкладку. Ладно, оставайся.",
        "{user}, система не может подтвердить твою полезность.",
        "{user}, поздравляем. Ты снова ничего не сделал."
    };
    m_pools["BRAINROT"] = {
        "{user}, aura level: -9000.",
        "{user}, bro is NOT cooking.",
        "{user}, detected: unemployed behavior.",
        "{user}, touch grass challenge: FAILED.",
        "{user}, ты сигма или обычный человек?"
    };
    m_pools["QUESTIONS"] = {
        "{user}, ты бы выжил в зомби-апокалипсисе? Отвечай быстро.",
        "{user}, назови свою самую бесполезную суперспособность.",
        "{user}, ты здесь добровольно или тебя удерживает алгоритм?",
        "{user}, чай или кофе — ошибка в ответе карается осуждением."
    };
    m_pools["SECRET_LEGENDARY"] = {
        "КРИТИЧЕСКАЯ АНОМАЛИЯ: {user} получил статус «Избранный зритель этой сессии». +50 XP.",
        "СИСТЕМНЫЙ СБОЙ В ПОЛЬЗУ {user}: алгоритм признал тебя легендой чата."
    };

    char *path = obs_module_file("pools.json");
    if (path) {
        QFile f(QString::fromUtf8(path));
        bfree(path);
        if (f.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                for (auto it = obj.begin(); it != obj.end(); ++it) {
                    if (it.value().isArray()) {
                        QStringList list;
                        for (const auto &v : it.value().toArray()) {
                            list.append(v.toString());
                        }
                        if (!list.isEmpty()) m_pools[it.key()] = list;
                    }
                }
            }
        }
    }
}

void VICore::loadState()
{
    char *path = obs_module_config_path("state.json");
    if (!path) return;
    QFile f(QString::fromUtf8(path));
    bfree(path);
    if (!f.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;
    QJsonObject root = doc.object();

    if (root.contains("channel")) m_channel = root["channel"].toString();
    if (root.contains("rankingMode")) m_rankingMode = root["rankingMode"].toString();

    QJsonObject usersObj = root["users"].toObject();
    for (auto it = usersObj.begin(); it != usersObj.end(); ++it) {
        QJsonObject uo = it.value().toObject();
        ViewerRecord r;
        r.key = it.key();
        r.displayName = uo["displayName"].toString(r.key);
        r.lifetimeXp = uo["lifetimeXp"].toInt();
        r.messages = uo["messages"].toInt();
        m_users[r.key] = r;
    }
}

void VICore::saveState()
{
    char *dirPath = obs_module_config_path("");
    if (dirPath) {
        QDir().mkpath(QString::fromUtf8(dirPath));
        bfree(dirPath);
    }
    char *path = obs_module_config_path("state.json");
    if (!path) return;

    QJsonObject root;
    {
        QMutexLocker lock(&m_mutex);
        root["channel"] = m_channel;
        root["rankingMode"] = m_rankingMode;
        QJsonObject usersObj;
        for (const auto &u : m_users) {
            QJsonObject uo;
            uo["displayName"] = u.displayName;
            uo["lifetimeXp"] = u.lifetimeXp;
            uo["messages"] = u.messages;
            usersObj[u.key] = uo;
        }
        root["users"] = usersObj;
    }

    QFile f(QString::fromUtf8(path));
    bfree(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }
}
