#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include "ModrinthAPI.h"

// Phase 3: Exporting Modpacks (.mrpack & CF .zip)
// Phase 4: Client/Server splitting
struct ExportOptions {
    QString packName;
    QString packVersion;
    QString mcVersion;
    QString loader;
    QString loaderVersion;
    QString author;
    QString description;
    bool generateServerPack = false;
    bool includeConfigs = false;
    QString configOverridesPath;
    QString outputPath;
    /** Folder where mod .jar files may already exist (for hashing when exporting .mrpack). */
    QString localModsDirectory;
};

class PackExporter : public QObject {
    Q_OBJECT
public:
    static PackExporter& instance();

    // Export to Modrinth .mrpack format
    // Generates: modrinth.index.json + overrides/ folder -> .mrpack.zip
    bool exportToMrpack(const QVector<ModInfo>& mods,
                         const ExportOptions& options,
                         QString* errorOut = nullptr);

    // Export to CurseForge .zip format
    // Generates: manifest.json + modlist.html + overrides/ -> .zip
    bool exportToCurseForge(const QVector<ModInfo>& mods,
                             const ExportOptions& options,
                             QString* errorOut = nullptr);

    // Generate server pack (filters client-only mods)
    bool exportServerPack(const QVector<ModInfo>& allMods,
                           const ExportOptions& options,
                           QString* errorOut = nullptr);

    // Test if a path is writable before export
    static bool canWriteTo(const QString& path);

    /** Best-effort stable loader/library version string for manifests (Fabric/Quilt from meta APIs). Empty if unknown. */
    static QString suggestedLoaderVersion(const QString& loader, const QString& mcVersion);

signals:
    void exportProgress(int percent, const QString& stage);
    void exportFinished(bool success, const QString& path, const QString& error);

private:
    PackExporter();
    ~PackExporter() = default;
    PackExporter(const PackExporter&) = delete;
    PackExporter& operator=(const PackExporter&) = delete;

    // Generate Modrinth index.json content
    QJsonObject generateMrpackIndex(const QVector<ModInfo>& mods,
                                     const ExportOptions& options);

    // Generate CurseForge manifest.json content
    QJsonObject generateCFManifest(const QVector<ModInfo>& mods,
                                    const ExportOptions& options,
                                    QString* errorMessage = nullptr);

    // Generate modlist.html for CF packs
    QString generateModListHtml(const QVector<ModInfo>& mods);

    // Hash a file using SHA-1
    static QString sha1Hash(const QString& filePath);

    // Get SHA-512 hash
    static QString sha512Hash(const QString& filePath);

    // Staging directory for building packs
    QString createStagingDir(const QString& packName);
    bool cleanupStagingDir(const QString& dir);
};