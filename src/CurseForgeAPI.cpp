#include "CurseForgeAPI.h"
#include "CacheManager.h"

// Generated at build time by obfuscate_key.py
#include "api_keys.h"

#include <QDebug>
#include <QUrlQuery>

const QString CurseForgeAPI::API_BASE = "https://api.curseforge.com/v1";
const QString CurseForgeAPI::API_KEY_HEADER = "x-api-key";

CurseForgeAPI& CurseForgeAPI::instance() {
    static CurseForgeAPI inst;
    return inst;
}

CurseForgeAPI::CurseForgeAPI()
    : QObject(nullptr)
    , m_nam(new QNetworkAccessManager(this))
    , m_hasInjectedKey(ApiKeys::HasCurseForgeKey())
{
}

QByteArray CurseForgeAPI::decryptInjectedKey() const {
    if (!m_hasInjectedKey) return {};
    const auto& encrypted = ApiKeys::kCurseForgeKeyEncrypted;
    const auto& salt = ApiKeys::kCurseForgeSalt;

    QByteArray decrypted;
    decrypted.resize(static_cast<int>(encrypted.size()));
    for (size_t i = 0; i < encrypted.size(); ++i) {
        decrypted[static_cast<int>(i)] = static_cast<char>(
            encrypted[i] ^ salt[i % salt.size()]
        );
    }
    return decrypted;
}

bool CurseForgeAPI::hasApiKey() const {
    return !m_userApiKey.isEmpty() || m_hasInjectedKey;
}

void CurseForgeAPI::setUserApiKey(const QString& key) {
    m_userApiKey = key;
}

QString CurseForgeAPI::activeApiKey() const {
    if (!m_userApiKey.isEmpty()) return m_userApiKey;
    if (m_hasInjectedKey) return QString::fromUtf8(decryptInjectedKey());
    return QString();
}

QNetworkReply* CurseForgeAPI::get(const QUrl& url) {
    if (!hasApiKey()) {
        emit apiKeyMissing();
        return nullptr;
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
        "helloworldx64/CraftPacker/3.0.0");
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader(API_KEY_HEADER.toUtf8(), activeApiKey().toUtf8());
    return m_nam->get(request);
}

void CurseForgeAPI::searchMods(const QString& query, int limit) {
    // Check cache
    QString cacheKey = "cf_search:" + query;
    auto cached = CacheManager::instance().get(cacheKey);
    if (cached) {
        emit searchResult(query, cached->array());
        return;
    }

    QUrlQuery params;
    params.addQueryItem("searchFilter", query);
    params.addQueryItem("pageSize", QString::number(limit));
    params.addQueryItem("gameId", "432"); // Minecraft
    params.addQueryItem("classId", "447454"); // Mods class

    QUrl url(API_BASE + "/mods/search");
    url.setQuery(params);

    QNetworkReply* reply = get(url);
    if (!reply) return;

    connect(reply, &QNetworkReply::finished, this, [this, reply, query]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit searchResult(query, QJsonArray());
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonArray data = doc.object()["data"].toArray();
        CacheManager::instance().put("cf_search:" + query, QJsonDocument(data));
        emit searchResult(query, data);
    });
}

void CurseForgeAPI::getMod(const QString& modId) {
    QNetworkReply* reply = get(QUrl(API_BASE + "/mods/" + modId));
    if (!reply) return;

    connect(reply, &QNetworkReply::finished, this, [this, reply, modId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit modResult(modId, QJsonObject());
            return;
        }
        QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object()["data"].toObject();
        emit modResult(modId, data);
    });
}

void CurseForgeAPI::getModFile(const QString& modId, const QString& loader, const QString& gameVersion) {
    // First get the mod to find matching files
    QNetworkReply* reply = get(QUrl(API_BASE + "/mods/" + modId + "/files"));
    if (!reply) return;

    connect(reply, &QNetworkReply::finished, this, [this, reply, modId, loader, gameVersion]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit fileResult(modId, std::nullopt);
            return;
        }

        QJsonArray files = QJsonDocument::fromJson(reply->readAll()).object()["data"].toArray();
        // Find the latest file that matches our loader and game version
        for (const auto& fileVal : files) {
            QJsonObject file = fileVal.toObject();
            QJsonArray gameVersions = file["gameVersions"].toArray();

            bool matchesGameVersion = false;
            bool matchesLoader = false;
            for (const auto& gv : gameVersions) {
                QString gvStr = gv.toString();
                if (gvStr == gameVersion) matchesGameVersion = true;
                if (gvStr.toLower().contains(loader.toLower())) matchesLoader = true;
            }

            if (matchesGameVersion) {
                // Build ModInfo from CF file data
                ModInfo info;
                info.projectId = modId;
                info.name = file["displayName"].toString();
                info.filename = file["fileName"].toString();
                info.downloadUrl = file["downloadUrl"].toString();
                info.fileSize = file["fileLength"].toVariant().toLongLong();
                info.versionType = file["releaseType"] == 1 ? "release" :
                                    file["releaseType"] == 2 ? "beta" : "alpha";
                info.loader = loader;
                info.gameVersion = gameVersion;

                // Parse dependencies
                QJsonArray deps = file["dependencies"].toArray();
                info.dependencies = deps;

                emit fileResult(modId, info);
                return;
            }
        }
        emit fileResult(modId, std::nullopt);
    });
}