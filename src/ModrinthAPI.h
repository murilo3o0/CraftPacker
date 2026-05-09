#pragma once
#include <QObject>
#include <QString>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QThread>
#include <QMutex>
#include <QVector>
#include <QPair>
#include <QUrl>
#include <QUrlQuery>
#include <optional>
#include <chrono>

// Forward declare
struct ModInfo {
    QString projectId;
    QString name;
    QString slug;
    QString description;
    QString iconUrl;
    QString downloadUrl;
    QString fileName;
    QString sha1;
    QString sha512;
    QString clientSide;
    QString serverSide;
    QString projectType;
    QString versionId;
    QString filename;
    QString versionType;
    QString loader;
    QString gameVersion;
    QString author;
    qint64 fileSize = 0;
    qint64 curseforgeFileId = 0;
    bool isDependency = false;
    QString originalQuery;
    QJsonArray dependencies;
    bool isValid() const { return !projectId.isEmpty(); }
    QString id() const { return projectId; }
};

// ============================================================
// ModrinthAPI — Singleton for all Modrinth API calls
// ============================================================
class ModrinthAPI : public QObject {
    Q_OBJECT

public:
    static ModrinthAPI& instance();

    // Search mods
    void searchMods(const QString& query, int limit = 5);
    void searchModsByHash(const QString& sha1Hash);

    // Fetch project by ID (slug)
    void getProject(const QString& projectId);
    void getProjectVersions(const QString& projectId,
                            const QString& gameVersion = {},
                            const QString& loader = {});
    void getVersion(const QString& versionId);
    void getVersion(const QString& projectId,
                    const QString& loader,
                    const QString& gameVersion);

    // Get ALL versions of a project
    void getAllVersions(const QString& projectId);

    // Updates: check if a specific mod version is outdated
    void checkForUpdate(const QString& projectId,
                        const QString& currentVersionId);
    void findVersionsForGameVersion(const QString& projectId,
                                     const QString& gameVersion,
                                     const QString& loader);

    // Constants
    static const QString API_BASE;
    static const QString API_SEARCH;

    // Rate limit (ms between calls)
    void setRateLimit(int ms) { m_minInterval = std::chrono::milliseconds(ms); }

signals:
    // Search
    void modSearchResult(const QString& query, const QJsonArray& results);
    void searchError(const QString& error);
    void hashSearchResult(const QJsonObject& result);
    void hashSearchError(const QString& error);

    // Project
    void modProjectResult(const QString& projectId, const QJsonObject& project);
    void projectError(const QString& error);

    // Versions
    void modVersionResult(const QString& projectId, const ModInfo& version);
    void modVersionResult(const QString& projectId, const std::nullopt_t);
    void allVersionsResult(const QString& projectId, const QJsonArray& versions);
    void versionError(const QString& error);

    // Updates
    void updateCheckResult(const QString& projectId, bool hasUpdate, const QString& newVersion);
    void updateCheckError(const QString& error);

    // General
    void apiError(const QString& context, const QString& error);
    void rateLimited();

private:
    explicit ModrinthAPI();
    ~ModrinthAPI() override = default;
    ModrinthAPI(const ModrinthAPI&) = delete;
    ModrinthAPI& operator=(const ModrinthAPI&) = delete;

    void rateLimitWait();
    QNetworkReply* get(const QUrl& url);

    // Internal handler wrappers
    void handleSearchReply(QNetworkReply* reply, const QString& query);
    void handleProjectReply(QNetworkReply* reply, const QString& projectId);
    void handleVersionsReply(QNetworkReply* reply, const QString& projectId,
                             const QString& gameVersion, const QString& loader);
    void handleAllVersionsReply(QNetworkReply* reply, const QString& projectId);
    void handleUpdateCheckReply(QNetworkReply* reply, const QString& projectId,
                                const QString& currentVersionId,
                                const QString& gameVersion,
                                const QString& loader);
    void handleFindVersionsReply(QNetworkReply* reply,
                                 const QString& projectId,
                                 const QString& gameVersion,
                                 const QString& loader);

    QNetworkAccessManager* m_nam;
    std::chrono::steady_clock::time_point m_lastCall;
    std::chrono::milliseconds m_minInterval{80};
    QMutex m_rateMutex;
};

class API_RateLimiter {
public:
    explicit API_RateLimiter(int callsPerSecond);
    void wait();
private:
    QMutex m_mtx;
    std::chrono::steady_clock::time_point m_last;
    std::chrono::milliseconds m_interval;
};