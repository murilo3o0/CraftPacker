#include "ModrinthAPI.h"
#include "CacheManager.h"
#include "AsyncWorker.h"
#include "StringCleaner.h"
#include <QEventLoop>
#include <QDebug>

const QString ModrinthAPI::API_BASE = "https://api.modrinth.com/v2";
const QString ModrinthAPI::API_SEARCH = API_BASE + "/search";

ModrinthAPI& ModrinthAPI::instance() {
    static ModrinthAPI inst;
    return inst;
}

ModrinthAPI::ModrinthAPI()
    : QObject(nullptr)
    , m_nam(new QNetworkAccessManager(this))
    , m_lastCall(std::chrono::steady_clock::now())
{
    qRegisterMetaType<ModInfo>();
    qRegisterMetaType<std::optional<ModInfo>>();
}

void ModrinthAPI::rateLimitWait() {
    QMutexLocker locker(&m_rateMutex);
    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - m_lastCall;
    if (elapsed < m_minInterval) {
        auto waitTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            m_minInterval - elapsed).count();
        QThread::msleep(waitTime);
    }
    m_lastCall = std::chrono::steady_clock::now();
}

QNetworkReply* ModrinthAPI::get(const QUrl& url) {
    rateLimitWait();
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
        "helloworldx64/CraftPacker/3.0.0");
    request.setRawHeader("Accept", "application/json");
    return m_nam->get(request);
}

void ModrinthAPI::searchMods(const QString& query, int limit) {
    // Check cache first
    QString cacheKey = "search:" + query;
    auto cached = CacheManager::instance().get(cacheKey);
    if (cached) {
        emit modSearchResult(query, cached->array());
        return;
    }

    QUrlQuery params;
    params.addQueryItem("query", query);
    params.addQueryItem("limit", QString::number(limit));
    params.addQueryItem("facets", R"([["project_type:mod"]])");

    QUrl url(API_SEARCH);
    url.setQuery(params);

    QNetworkReply* reply = get(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, query]() {
        handleSearchReply(reply, query);
    });
}

void ModrinthAPI::handleSearchReply(QNetworkReply* reply, const QString& query) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        emit apiError("search", reply->errorString());
        emit modSearchResult(query, QJsonArray());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject()) {
        emit modSearchResult(query, QJsonArray());
        return;
    }

    QJsonArray hits = doc.object()["hits"].toArray();
    // Cache the result
    CacheManager::instance().put("search:" + query, QJsonDocument(hits));
    emit modSearchResult(query, hits);
}

void ModrinthAPI::getVersion(const QString& projectId, const QString& loader, const QString& gameVersion) {
    QString cacheKey = QString("version:%1:%2:%3").arg(projectId, loader, gameVersion);
    auto cached = CacheManager::instance().get(cacheKey);
    if (cached) {
        // Parse cached ModInfo
        QJsonObject obj = cached->object();
        ModInfo info;
        info.projectId = obj["project_id"].toString();
        info.name = obj["name"].toString();
        info.versionId = obj["version_id"].toString();
        info.downloadUrl = obj["download_url"].toString();
        info.filename = obj["filename"].toString();
        info.versionType = obj["version_type"].toString();
        info.clientSide = obj["client_side"].toString();
        info.serverSide = obj["server_side"].toString();
        info.loader = obj["loader"].toString();
        info.gameVersion = obj["game_version"].toString();
        info.fileSize = obj["file_size"].toVariant().toLongLong();
        info.dependencies = obj["dependencies"].toArray();
        info.sha1 = obj["sha1"].toString().toLower();
        info.sha512 = obj["sha512"].toString().toLower();
        emit modVersionResult(projectId, info);
        return;
    }

    // First get project to get slug
    QUrl projUrl(API_BASE + "/project/" + projectId);
    QNetworkReply* projReply = get(projUrl);

    connect(projReply, &QNetworkReply::finished, this,
        [this, projReply, projectId, loader, gameVersion]() {
        projReply->deleteLater();
        if (projReply->error() != QNetworkReply::NoError) {
            emit modVersionResult(projectId, std::nullopt);
            return;
        }

        QJsonObject project = QJsonDocument::fromJson(projReply->readAll()).object();
        QString slug = project["slug"].toString();
        QString title = project["title"].toString();
        QString author = project["author"].toString();
        QString description = project["description"].toString();
        QString iconUrl = project["icon_url"].toString();
        QString clientSide = project["client_side"].toString();
        QString serverSide = project["server_side"].toString();

        // Now get version
        QUrlQuery params;
        params.addQueryItem("loaders", QJsonDocument(QJsonArray({loader})).toJson(QJsonDocument::Compact));
        params.addQueryItem("game_versions", QJsonDocument(QJsonArray({gameVersion})).toJson(QJsonDocument::Compact));

        QUrl verUrl(API_BASE + "/project/" + slug + "/version");
        verUrl.setQuery(params);

        QNetworkReply* verReply = get(verUrl);
        connect(verReply, &QNetworkReply::finished, this,
            [this, verReply, projectId, title, author, description, iconUrl,
             clientSide, serverSide, slug, loader, gameVersion]() {
            verReply->deleteLater();
            if (verReply->error() != QNetworkReply::NoError) {
                emit modVersionResult(projectId, std::nullopt);
                return;
            }

            QJsonArray versions = QJsonDocument::fromJson(verReply->readAll()).array();
            for (const QString& type : {"release", "beta", "alpha"}) {
                for (const auto& v : versions) {
                    QJsonObject verObj = v.toObject();
                    if (verObj["version_type"].toString() == type) {
                        QJsonArray files = verObj["files"].toArray();
                        if (files.isEmpty()) continue;

                        QJsonObject file = files[0].toObject();
                        ModInfo info;
                        info.name = title;
                        info.slug = slug;
                        info.projectId = projectId;
                        info.versionId = verObj["id"].toString();
                        info.downloadUrl = file["url"].toString();
                        info.filename = file["filename"].toString();
                        info.versionType = type;
                        info.loader = loader;
                        info.gameVersion = gameVersion;
                        info.author = author;
                        info.description = description;
                        info.iconUrl = iconUrl;
                        info.clientSide = clientSide;
                        info.serverSide = serverSide;
                        info.fileSize = file["size"].toVariant().toLongLong();
                        info.dependencies = verObj["dependencies"].toArray();
                        {
                            QJsonObject fh = file.value("hashes").toObject();
                            info.sha1 = fh.value("sha1").toString().toLower();
                            info.sha512 = fh.value("sha512").toString().toLower();
                        }

                        // Cache the result
                        QJsonObject cacheObj;
                        cacheObj["project_id"] = info.projectId;
                        cacheObj["name"] = info.name;
                        cacheObj["version_id"] = info.versionId;
                        cacheObj["download_url"] = info.downloadUrl;
                        cacheObj["filename"] = info.filename;
                        cacheObj["version_type"] = info.versionType;
                        cacheObj["loader"] = info.loader;
                        cacheObj["game_version"] = info.gameVersion;
                        cacheObj["client_side"] = info.clientSide;
                        cacheObj["server_side"] = info.serverSide;
                        cacheObj["file_size"] = (qint64)info.fileSize;
                        cacheObj["dependencies"] = info.dependencies;
                        cacheObj["sha1"] = info.sha1;
                        cacheObj["sha512"] = info.sha512;
                        CacheManager::instance().put(
                            "version:" + projectId + ":" + loader + ":" + gameVersion,
                            QJsonDocument(cacheObj)
                        );

                        emit modVersionResult(projectId, info);
                        return;
                    }
                }
            }
            emit modVersionResult(projectId, std::nullopt);
        });
    });
}

void ModrinthAPI::getProject(const QString& projectId) {
    QString cacheKey = "project:" + projectId;
    auto cached = CacheManager::instance().get(cacheKey);
    if (cached) {
        emit modProjectResult(projectId, cached->object());
        return;
    }

    QNetworkReply* reply = get(QUrl(API_BASE + "/project/" + projectId));
    connect(reply, &QNetworkReply::finished, this, [this, reply, projectId]() {
        handleProjectReply(reply, projectId);
    });
}

void ModrinthAPI::handleProjectReply(QNetworkReply* reply, const QString& projectId) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        emit apiError("project:" + projectId, reply->errorString());
        emit modProjectResult(projectId, QJsonObject());
        return;
    }

    QJsonObject proj = QJsonDocument::fromJson(reply->readAll()).object();
    CacheManager::instance().put("project:" + projectId, QJsonDocument(proj));
    emit modProjectResult(projectId, proj);
}

void ModrinthAPI::getAllVersions(const QString& projectId) {
    QString cacheKey = "allversions:" + projectId;
    auto cached = CacheManager::instance().get(cacheKey); // 10 min TTL handled by has()
    if (cached) {
        emit allVersionsResult(projectId, cached->array());
        return;
    }
    
    // No cache, use a fresh request with shorter timeout
    // Actually just call get() which uses default 30min TTL
    cached = CacheManager::instance().get(cacheKey);
    if (cached) {
        emit allVersionsResult(projectId, cached->array());
        return;
    }

    QNetworkReply* reply = get(QUrl(API_BASE + "/project/" + projectId + "/version"));
    connect(reply, &QNetworkReply::finished, this, [this, reply, projectId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit allVersionsResult(projectId, QJsonArray());
            return;
        }
        QJsonArray versions = QJsonDocument::fromJson(reply->readAll()).array();
        CacheManager::instance().put("allversions:" + projectId, QJsonDocument(versions));
        emit allVersionsResult(projectId, versions);
    });
}

void ModrinthAPI::checkForUpdate(const QString& projectId, const QString& currentVersionId) {
    QNetworkReply* reply = get(QUrl(API_BASE + "/version/" + currentVersionId));
    connect(reply, &QNetworkReply::finished, this, [this, reply, projectId, currentVersionId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit updateCheckResult(projectId, false, QString());
            return;
        }

        QJsonObject currentVer = QJsonDocument::fromJson(reply->readAll()).object();
        QString currentDate = currentVer["date_published"].toString();
        QStringList loaders;
        QStringList gameVersions;
        for (const auto& l : currentVer["loaders"].toArray())
            loaders << l.toString();
        for (const auto& gv : currentVer["game_versions"].toArray())
            gameVersions << gv.toString();

        // Check if there's a newer version for the same MC version
        QUrlQuery params;
        params.addQueryItem("game_versions",
            QJsonDocument(QJsonArray::fromStringList(gameVersions)).toJson(QJsonDocument::Compact));
        params.addQueryItem("loaders",
            QJsonDocument(QJsonArray::fromStringList(loaders)).toJson(QJsonDocument::Compact));

        QUrl url(API_BASE + "/project/" + projectId + "/version");
        url.setQuery(params);

        QNetworkReply* verReply = get(url);
        connect(verReply, &QNetworkReply::finished, this,
            [this, verReply, projectId, currentDate]() {
            verReply->deleteLater();
            if (verReply->error() != QNetworkReply::NoError) {
                emit updateCheckResult(projectId, false, QString());
                return;
            }

            QJsonArray versions = QJsonDocument::fromJson(verReply->readAll()).array();
            for (const auto& v : versions) {
                QJsonObject ver = v.toObject();
                QString pubDate = ver["date_published"].toString();
                if (pubDate > currentDate) {
                    emit updateCheckResult(projectId, true, ver["id"].toString());
                    return;
                }
            }
            emit updateCheckResult(projectId, false, QString());
        });
    });
}

void ModrinthAPI::findVersionsForGameVersion(const QString& projectId,
                                              const QString& newLoader,
                                              const QString& newGameVersion) {
    getVersion(projectId, newLoader, newGameVersion);
}