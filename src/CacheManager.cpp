#include "CacheManager.h"
#include <QDebug>
#include <QJsonParseError>

CacheManager::CacheManager() {
    load();
}

CacheManager& CacheManager::instance() {
    static CacheManager inst;
    return inst;
}

QString CacheManager::cacheFilePath() const {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(path);
    return path + "/craftpacker_cache.json";
}

void CacheManager::load() {
    QWriteLocker locker(&m_lock);
    QFile file(cacheFilePath());
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;

    QJsonObject root = doc.object();
    QJsonObject entries = root["entries"].toObject();
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        QJsonObject entry = it.value().toObject();
        CacheEntry ce;
        ce.key = it.key();
        ce.timestamp = entry["ts"].toVariant().toLongLong();
        ce.data = QJsonDocument(entry["data"].toObject());
        m_cache.insert(ce.key, ce);
    }
    m_dirty = false;
}

void CacheManager::save() {
    QJsonObject root;
    QJsonObject entries;
    auto now = QDateTime::currentSecsSinceEpoch();

    for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
        // Skip entries older than 24 hours during save
        if (now - it.value().timestamp > 86400) continue;
        QJsonObject entry;
        entry["ts"] = it.value().timestamp;
        entry["data"] = it.value().data.object();
        entries.insert(it.key(), entry);
    }

    root["entries"] = entries;
    root["version"] = 2;

    QFile file(cacheFilePath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
}

bool CacheManager::has(const QString& key, int ttlSeconds) const {
    QReadLocker locker(&m_lock);
    auto it = m_cache.find(key);
    if (it == m_cache.end()) return false;

    auto now = QDateTime::currentSecsSinceEpoch();
    return (now - it.value().timestamp) < ttlSeconds;
}

std::optional<QJsonDocument> CacheManager::get(const QString& key) const {
    QReadLocker locker(&m_lock);
    auto it = m_cache.find(key);
    if (it == m_cache.end()) return std::nullopt;

    auto now = QDateTime::currentSecsSinceEpoch();
    if ((now - it.value().timestamp) > 1800) return std::nullopt; // 30 min default TTL
    return it.value().data;
}

void CacheManager::put(const QString& key, const QJsonDocument& data) {
    QWriteLocker locker(&m_lock);
    CacheEntry ce;
    ce.key = key;
    ce.data = data;
    ce.timestamp = QDateTime::currentSecsSinceEpoch();
    m_cache.insert(key, ce);
    m_dirty = true;
    // Auto-save periodically (every 10 writes)
    static int writeCount = 0;
    if (++writeCount % 10 == 0) {
        locker.unlock();
        save();
    }
}

QJsonArray CacheManager::search(const QString& query, int maxResults) const {
    QReadLocker locker(&m_lock);
    QJsonArray results;
    QString lowerQuery = query.toLower();
    int count = 0;

    for (auto it = m_cache.begin(); it != m_cache.end() && count < maxResults; ++it) {
        if (it.key().toLower().contains(lowerQuery)) {
            QJsonObject result;
            result["key"] = it.key();
            result["data"] = it.value().data.object();
            results.append(result);
            ++count;
        }
    }
    return results;
}

void CacheManager::cleanExpired() {
    QWriteLocker locker(&m_lock);
    auto now = QDateTime::currentSecsSinceEpoch();
    QMutableMapIterator<QString, CacheEntry> it(m_cache);
    int removed = 0;
    while (it.hasNext()) {
        it.next();
        if (now - it.value().timestamp > 86400) { // 24 hours
            it.remove();
            ++removed;
        }
    }
    if (removed > 0) {
        m_dirty = true;
        save();
    }
}

void CacheManager::clear() {
    QWriteLocker locker(&m_lock);
    m_cache.clear();
    m_dirty = true;
    save();
    QFile::remove(cacheFilePath());
}