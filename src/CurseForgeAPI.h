#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <optional>
#include "ModrinthAPI.h" // Reuse ModInfo struct

// Phase 2: CurseForge Integration
// Uses build-time injected XOR-obfuscated API key with runtime decryption.
// Supports user-provided key fallback via Settings UI.
// Gracefully disables if no key available.
class CurseForgeAPI : public QObject {
    Q_OBJECT
public:
    static CurseForgeAPI& instance();

    // Returns true if we have a valid API key (injected OR user-provided)
    bool hasApiKey() const;

    // Set a user-provided API key (takes precedence over injected key)
    void setUserApiKey(const QString& key);

    // Get the active API key (user key > injected key > empty)
    QString activeApiKey() const;

    // Search CurseForge for mods
    void searchMods(const QString& query, int limit = 10);

    // Get mod description/version info
    void getMod(const QString& modId);

    // Get matching file for specific loader/game version
    void getModFile(const QString& modId, const QString& loader, const QString& gameVersion);

    static const QString API_BASE;
    static const QString API_KEY_HEADER;

signals:
    void searchResult(const QString& query, const QJsonArray& results);
    void modResult(const QString& modId, const QJsonObject& mod);
    void fileResult(const QString& modId, std::optional<ModInfo> info);
    void apiKeyMissing();

private:
    CurseForgeAPI();
    ~CurseForgeAPI() = default;
    CurseForgeAPI(const CurseForgeAPI&) = delete;
    CurseForgeAPI& operator=(const CurseForgeAPI&) = delete;

    QNetworkReply* get(const QUrl& url);
    QByteArray decryptInjectedKey() const;

    QNetworkAccessManager* m_nam;
    QString m_userApiKey;
    bool m_hasInjectedKey;
};