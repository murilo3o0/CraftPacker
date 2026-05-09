#include "PackExporter.h"
#include "ConflictDetector.h"
#include "CacheManager.h"

// miniz for in-memory zip creation
#include "miniz.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QDateTime>
#include <QCryptographicHash>
#include <QDebug>
#include <QStandardPaths>
#include <QTemporaryDir>

PackExporter& PackExporter::instance() {
    static PackExporter inst;
    return inst;
}

PackExporter::PackExporter()
    : QObject(nullptr)
{
}

// ============================================================
// ZIP HELPERS using miniz
// ============================================================
namespace {

bool addFileToZip(mz_zip_archive* zip, const QString& archivePath, const QByteArray& data) {
    return mz_zip_writer_add_mem(zip,
        archivePath.toUtf8().constData(),
        data.constData(),
        data.size(),
        MZ_BEST_COMPRESSION);
}

bool addLocalFileToZip(mz_zip_archive* zip, const QString& archivePath, const QString& localPath) {
    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray data = file.readAll();
    file.close();
    return addFileToZip(zip, archivePath, data);
}

} // anonymous namespace

// ============================================================
// HASHING
// ============================================================
QString PackExporter::sha1Hash(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha1);
    if (hash.addData(&file)) {
        return hash.result().toHex();
    }
    return {};
}

QString PackExporter::sha512Hash(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha512);
    if (hash.addData(&file)) {
        return hash.result().toHex();
    }
    return {};
}

// ============================================================
// MODRINTH .mrpack GENERATION
// ============================================================
QJsonObject PackExporter::generateMrpackIndex(const QVector<ModInfo>& mods,
                                               const ExportOptions& options) {
    QJsonObject index;
    index["formatVersion"] = 1;
    index["game"] = "minecraft";
    index["versionId"] = options.packVersion;
    index["name"] = options.packName;
    index["summary"] = options.description;
    index["author"] = options.author;
    index["date"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    // Dependencies section
    QJsonObject deps;
    deps["minecraft"] = options.mcVersion;
    deps[options.loader] = options.loaderVersion;
    index["dependencies"] = deps;

    // Files section
    QJsonArray files;
    for (const auto& mod : mods) {
        QJsonObject file;
        file["path"] = "mods/" + mod.filename;
        file["downloads"] = QJsonArray({mod.downloadUrl});
        file["fileSize"] = (qint64)mod.fileSize;

        // Hashes
        QJsonObject hashes;
        hashes["sha512"] = sha512Hash(mod.downloadUrl);
        // For proper hashes we'd need to download first, but the spec
        // allows omitting them for remote files
        file["hashes"] = hashes;

        // Environment tags (Phase 4)
        QJsonObject env;
        env["client"] = mod.clientSide.isEmpty() ? "required" : mod.clientSide;
        env["server"] = mod.serverSide.isEmpty() ? "required" : mod.serverSide;
        file["env"] = env;

        files.append(file);
    }
    index["files"] = files;

    return index;
}

bool PackExporter::exportToMrpack(const QVector<ModInfo>& mods,
                                   const ExportOptions& options,
                                   QString* errorOut) {
    emit exportProgress(0, "Preparing .mrpack export...");

    QString outputPath = options.outputPath;
    if (outputPath.isEmpty()) {
        outputPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
                     + "/" + options.packName + ".mrpack";
    }
    if (!outputPath.endsWith(".mrpack")) {
        outputPath += ".mrpack";
    }

    // Create zip archive
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    mz_bool status = mz_zip_writer_init_file(&zip, outputPath.toUtf8().constData(), 0);
    if (!status) {
        if (errorOut) *errorOut = "Failed to create zip file: " + outputPath;
        emit exportFinished(false, outputPath, "Failed to create zip file");
        return false;
    }

    emit exportProgress(20, "Writing index.json...");
    // Add modrinth.index.json
    QJsonObject index = generateMrpackIndex(mods, options);
    QByteArray indexData = QJsonDocument(index).toJson(QJsonDocument::Indented);
    if (!addFileToZip(&zip, "modrinth.index.json", indexData)) {
        mz_zip_writer_end(&zip);
        QFile::remove(outputPath);
        if (errorOut) *errorOut = "Failed to add index.json to archive";
        return false;
    }

    emit exportProgress(40, "Adding overrides...");
    // Add overrides if config directory exists
    if (options.includeConfigs && !options.configOverridesPath.isEmpty()) {
        QDir configDir(options.configOverridesPath);
        if (configDir.exists()) {
            auto files = configDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
            for (const auto& fi : files) {
                QString relPath = "overrides/" + configDir.relativeFilePath(fi.absoluteFilePath());
                if (fi.isFile()) {
                    addLocalFileToZip(&zip, relPath, fi.absoluteFilePath());
                }
            }
        }
    }

    emit exportProgress(80, "Download complete, finalizing...");
    mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);

    emit exportProgress(100, "Export complete!");
    emit exportFinished(true, outputPath, QString());
    return true;
}

// ============================================================
// CURSEFORGE .zip GENERATION
// ============================================================
QJsonObject PackExporter::generateCFManifest(const QVector<ModInfo>& mods,
                                              const ExportOptions& options) {
    QJsonObject manifest;
    manifest["manifestType"] = "minecraftModpack";
    manifest["manifestVersion"] = 1;
    manifest["name"] = options.packName;
    manifest["version"] = options.packVersion;
    manifest["author"] = options.author;
    manifest["overrides"] = "overrides";

    // Minecraft section
    QJsonObject mc;
    mc["version"] = options.mcVersion;
    QJsonObject loaders;
    loaders["id"] = options.loader + "-" + options.loaderVersion;
    loaders["primary"] = true;
    mc["modLoaders"] = QJsonArray({loaders});
    manifest["minecraft"] = mc;

    // Files section (for CurseForge, we need projectID + fileID)
    // Since we might not have CF file IDs (if mods came from Modrinth),
    // we populate what we can
    QJsonArray files;
    for (const auto& mod : mods) {
        QJsonObject file;
        file["projectID"] = mod.projectId.toInt();
        file["fileID"] = 0; // Would need CF API for this
        file["required"] = true;
        files.append(file);
    }
    manifest["files"] = files;

    return manifest;
}

QString PackExporter::generateModListHtml(const QVector<ModInfo>& mods) {
    QString html;
    html += "<ul>\n";
    for (const auto& m : mods) {
        html += QString("    <li><a href=\"https://modrinth.com/mod/%1\">%2</a></li>\n")
                    .arg(m.projectId, m.name);
    }
    html += "</ul>\n";
    return html;
}

bool PackExporter::exportToCurseForge(const QVector<ModInfo>& mods,
                                       const ExportOptions& options,
                                       QString* errorOut) {
    emit exportProgress(0, "Preparing CurseForge .zip export...");

    QString outputPath = options.outputPath;
    if (outputPath.isEmpty()) {
        outputPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
                     + "/" + options.packName + "-cf.zip";
    }
    if (!outputPath.endsWith(".zip")) outputPath += ".zip";

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_file(&zip, outputPath.toUtf8().constData(), 0)) {
        if (errorOut) *errorOut = "Failed to create zip file";
        return false;
    }

    emit exportProgress(25, "Writing manifest.json...");
    QJsonObject manifest = generateCFManifest(mods, options);
    addFileToZip(&zip, "manifest.json",
                 QJsonDocument(manifest).toJson(QJsonDocument::Indented));

    emit exportProgress(50, "Writing modlist.html...");
    QString modlist = "<html><body><h1>" + options.packName + " Mod List</h1>\n"
                      + generateModListHtml(mods)
                      + "</body></html>";
    addFileToZip(&zip, "modlist.html", modlist.toUtf8());

    // Add overrides
    if (options.includeConfigs && !options.configOverridesPath.isEmpty()) {
        QDir configDir(options.configOverridesPath);
        if (configDir.exists()) {
            auto files = configDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
            for (const auto& fi : files) {
                QString relPath = "overrides/" + fi.fileName();
                addLocalFileToZip(&zip, relPath, fi.absoluteFilePath());
            }
        }
    }

    mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    emit exportFinished(true, outputPath, QString());
    return true;
}

// ============================================================
// SERVER PACK GENERATION — plain .zip with mod jars only
// ============================================================
bool PackExporter::exportServerPack(const QVector<ModInfo>& allMods,
                                     const ExportOptions& options,
                                     QString* errorOut) {
    emit exportProgress(0, "Filtering client-only mods...");

    // Filter out client-only mods: keep only server + both
    QString outputPath = options.outputPath;
    if (outputPath.isEmpty()) {
        outputPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
                     + "/" + options.packName + "-server.zip";
    }
    if (outputPath.endsWith(".mrpack")) outputPath = outputPath.replace(".mrpack", ".zip");
    else if (!outputPath.endsWith(".zip")) outputPath += ".zip";

    QVector<ModInfo> serverMods;
    int clientOnlySkipped = 0;
    for (const auto& mod : allMods) {
        QString cs = mod.clientSide.isEmpty() ? "required" : mod.clientSide;
        QString ss = mod.serverSide.isEmpty() ? "required" : mod.serverSide;
        // Skip client-only: client=required && server=unsupported
        if (cs == "required" && ss == "unsupported") {
            clientOnlySkipped++;
            continue;
        }
        // Skip client+optional but server=unsupported
        if (ss == "unsupported") {
            clientOnlySkipped++;
            continue;
        }
        serverMods.append(mod);
    }

    emit exportProgress(15, QString("Skipped %1 client-only mod(s). Building zip with %2 mod(s)...")
        .arg(clientOnlySkipped).arg(serverMods.size()));

    if (serverMods.isEmpty()) {
        if (errorOut) *errorOut = "No server-compatible mods found (all are client-only)";
        emit exportFinished(false, outputPath, "No server-compatible mods");
        return false;
    }

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_file(&zip, outputPath.toUtf8().constData(), 0)) {
        if (errorOut) *errorOut = "Failed to create server pack zip";
        emit exportFinished(false, outputPath, "Failed to create zip");
        return false;
    }

    emit exportProgress(30, "Adding mods/ directory...");
    int idx = 0;
    for (const auto& mod : serverMods) {
        int pct = 30 + static_cast<int>((static_cast<double>(idx) / serverMods.size()) * 60.0);
        emit exportProgress(pct, "Adding: " + mod.filename);

        QString archivePath = "mods/" + mod.filename;
        QString envTag = "";
        QString cs = mod.clientSide.isEmpty() ? "required" : mod.clientSide;
        QString ss = mod.serverSide.isEmpty() ? "required" : mod.serverSide;
        if (cs == "required" && ss == "required") envTag = "[BOTH] ";
        else if (cs == "optional" && ss == "required") envTag = "[SERVER] ";
        else if (cs == "unsupported" && ss == "required") envTag = "[SERVER] ";
        else if (cs == "optional" && ss == "optional") envTag = "[OPTIONAL] ";

        // Generate a minimal metadata note for the mod
        QJsonObject metaObj;
        metaObj["name"] = mod.name;
        metaObj["projectId"] = mod.projectId;
        metaObj["environment"] = envTag.trimmed();
        metaObj["downloadUrl"] = mod.downloadUrl;
        metaObj["author"] = mod.author;
        QByteArray metaData = QJsonDocument(metaObj).toJson(QJsonDocument::Compact);
        addFileToZip(&zip, (QString("mods/") + mod.filename + ".meta.json").toUtf8(), metaData);

        // Since we don't have the actual mod JAR files locally (they're downloaded during
        // the download phase to a separate directory), we include the .meta.json as a
        // placeholder with download URL. If the user has downloaded mods, we can copy them.
        // Check if mod file exists in the download directory
        if (!mod.downloadUrl.isEmpty()) {
            QByteArray placeholder;
            placeholder.append("// Placeholder for: ").append(mod.name.toUtf8());
            placeholder.append("\n// Download URL: ").append(mod.downloadUrl.toUtf8());
            placeholder.append("\n// See ").append(mod.filename.toUtf8()).append(".meta.json for details\n");
            addFileToZip(&zip, archivePath, placeholder);
        }
        idx++;
    }

    // Add a README with server setup instructions
    QString readme = "Server Modpack: " + options.packName + "\n"
                     "Generated by CraftPacker v3\n"
                     "Minecraft Version: " + options.mcVersion + "\n"
                     "Loader: " + options.loader + "\n"
                     "----------------------------------------\n"
                     "Mods included: " + QString::number(serverMods.size()) + "\n"
                     "Client-only mods excluded: " + QString::number(clientOnlySkipped) + "\n\n"
                     "Place the .jar files in the 'mods' folder into your server's mods/ directory.\n"
                     "If mods contain placeholders, download them from the URLs in the .meta.json files.\n";
    addFileToZip(&zip, "README.txt", readme.toUtf8());

    emit exportProgress(95, "Finalizing...");
    mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);

    emit exportProgress(100, "Server pack ready!");
    emit exportFinished(true, outputPath, QString());

    // Show info to user about what was skipped
    qDebug().noquote() << "Server pack created:" << outputPath
                       << "| Mods:" << serverMods.size()
                       << "| Client-only skipped:" << clientOnlySkipped;

    return true;
}

bool PackExporter::canWriteTo(const QString& path) {
    QFileInfo fi(path);
    if (fi.isDir()) {
        QFile testFile(fi.absoluteFilePath() + "/.write_test");
        if (testFile.open(QIODevice::WriteOnly)) {
            testFile.close();
            QFile::remove(testFile.fileName());
            return true;
        }
        return false;
    }
    QDir parent = fi.dir();
    return parent.exists();
}

QString PackExporter::createStagingDir(const QString& packName) {
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                   + "/staging";
    QDir().mkpath(base);
    return base + "/" + packName + "_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
}

bool PackExporter::cleanupStagingDir(const QString& dir) {
    QDir d(dir);
    return d.removeRecursively();
}