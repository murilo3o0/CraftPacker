#pragma once
#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QReadWriteLock>
#include <QMap>
#include <optional>

// Phase 1: Lightweight local caching layer
// Stores API responses in a JSON file to provide instant results for repeated queries.
// If SQLite is available (USE_SYSTEM_SQLITE), uses that instead for better performance.
class CacheManager {
public:
    static CacheManager& instance();

    // Check if a cached result exists and is still fresh (default: 30 min TTL)
    bool has(const QString& key, int ttlSeconds = 1800) const;

    // Retrieve cached JSON data
    std::optional<QJsonDocument> get(const QString& key) const;

    // Store data in cache
    void put(const QString& key, const QJsonDocument& data);

    // Search cache (by mod name / query substring)
    QJsonArray search(const QString& query, int maxResults = 10) const;

    // Clear expired entries
    void cleanExpired();

    // Clear entire cache
    void clear();

private:
    CacheManager();
    ~CacheManager() = default;
    CacheManager(const CacheManager&) = delete;
    CacheManager& operator=(const CacheManager&) = delete;

    QString cacheFilePath() const;
    void load();
    void save();

    struct CacheEntry {
        QString key;
        QJsonDocument data;
        qint64 timestamp; // epoch seconds
    };

    QMap<QString, CacheEntry> m_cache;
    mutable QReadWriteLock m_lock;
    bool m_dirty = false;
};