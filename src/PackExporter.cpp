#include "PackExporter.h"
#include "ConflictDetector.h"
#include "CacheManager.h"
#include "AppVersion.h"

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
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QEventLoop>
#include <QJsonParseError>
#include <QDirIterator>
#include <QUrl>
#include <QJsonArray>
#include <QObject>

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

/** Recursively pack `localRoot` into ZIP paths prefixed with `archiveRoot` using forward slashes. */
bool addDirectoryTreeToZip(mz_zip_archive* zip, const QString& localRoot, const QString& archiveRoot) {
    QDir root(localRoot);
    if (!root.exists()) return true;
    QString normRoot = root.canonicalPath();
    QDirIterator it(normRoot,
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QFileInfo fi = it.fileInfo();
        if (fi.isDir()) continue;
        QString rel = QDir(normRoot).relativeFilePath(fi.absoluteFilePath());
        rel.replace(QChar(QLatin1Char('\\')), QLatin1Char('/'));
        if (rel.contains(QLatin1String("..")) || rel.startsWith(QLatin1Char('/'))) {
            qWarning().noquote() << "Skipping unsafe overrides path:" << rel;
            continue;
        }
        const QString arc = archiveRoot + rel;
        if (!addLocalFileToZip(zip, arc, fi.absoluteFilePath())) return false;
    }
    return true;
}

QString mrpackDepsKey(const QString& loader) {
    const QString l = loader.trimmed().toLower();
    if (l == QLatin1String("fabric")) return QStringLiteral("fabric-loader");
    if (l == QLatin1String("quilt")) return QStringLiteral("quilt-loader");
    if (l == QLatin1String("forge")) return QStringLiteral("forge");
    if (l == QLatin1String("neoforge")) return QStringLiteral("neoforge");
    return {};
}

QString normalizedEnvSide(const QString& raw, const QString& fallback) {
    QString s = raw.trimmed().toLower();
    if (s.isEmpty())
        return fallback;
    if (s == QLatin1String("required") || s == QLatin1String("optional") || s == QLatin1String("unsupported"))
        return s;
    return fallback;
}

QString curseForgeModLoaderManifestId(const QString& loader,
                                     const QString& mcVersion,
                                     const QString& loaderVersion) {
    const QString lv = loaderVersion.trimmed();
    if (lv.isEmpty())
        return {};
    const QString l = loader.trimmed().toLower();
    if (l == QLatin1String("fabric"))
        return QStringLiteral("fabric-%1").arg(lv);
    if (l == QLatin1String("quilt"))
        return QStringLiteral("quilt-%1").arg(lv);
    if (l == QLatin1String("forge"))
        return QStringLiteral("forge-%1").arg(lv);
    if (l == QLatin1String("neoforge"))
        return QStringLiteral("neoforge-%1-%2").arg(mcVersion.trimmed(), lv);
    return {};
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
        return QString::fromLatin1(hash.result().toHex()).toLower();
    }
    return {};
}

QString PackExporter::sha512Hash(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha512);
    if (hash.addData(&file)) {
        return QString::fromLatin1(hash.result().toHex()).toLower();
    }
    return {};
}

// ============================================================
// MODRINTH .mrpack GENERATION
// ============================================================
QJsonObject PackExporter::generateMrpackIndex(const QVector<ModInfo>& mods,
                                               const ExportOptions& options) {
    QJsonObject index;
    index.insert(QStringLiteral("formatVersion"), 1);
    index.insert(QStringLiteral("game"), QStringLiteral("minecraft"));
    index.insert(QStringLiteral("versionId"), options.packVersion);
    index.insert(QStringLiteral("name"), options.packName);
    index.insert(QStringLiteral("summary"), options.description);
    index.insert(QStringLiteral("author"), options.author);
    index.insert(QStringLiteral("date"),
                 QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    const QString dk = mrpackDepsKey(options.loader);
    QJsonObject deps;
    deps.insert(QStringLiteral("minecraft"), options.mcVersion.trimmed());
    if (!dk.isEmpty() && !options.loaderVersion.trimmed().isEmpty())
        deps.insert(dk, options.loaderVersion.trimmed());
    index.insert(QStringLiteral("dependencies"), deps);

    QJsonArray fileArr;
    for (const auto& mod : mods) {
        if (mod.filename.trimmed().isEmpty())
            continue;

        QString rel = QStringLiteral("mods/%1").arg(mod.filename);
        rel.replace(QChar(QLatin1Char('\\')), QLatin1Char('/'));
        if (rel.contains(QLatin1String(".."))) {
            qWarning().noquote() << ".mrpack: skipping unsafe path" << rel;
            continue;
        }

        QString sha1h = mod.sha1.trimmed().toLower();
        QString sha512h = mod.sha512.trimmed().toLower();

        if (sha1h.isEmpty() || sha512h.isEmpty()) {
            if (!options.localModsDirectory.trimmed().isEmpty()) {
                const QString jar =
                    QDir::cleanPath(options.localModsDirectory.trimmed() + QLatin1Char('/') + mod.filename);
                if (QFile::exists(jar)) {
                    if (sha1h.isEmpty()) sha1h = PackExporter::sha1Hash(jar);
                    if (sha512h.isEmpty()) sha512h = PackExporter::sha512Hash(jar);
                }
            }
        }

        if (sha1h.isEmpty() || sha512h.isEmpty())
            continue;

        const QUrl url(mod.downloadUrl);
        if (!url.isValid() || url.scheme().toLower() != QLatin1String("https"))
            continue;

        QJsonObject fileObj;
        fileObj.insert(QStringLiteral("path"), rel);
        QJsonObject hashesObj;
        hashesObj.insert(QStringLiteral("sha1"), sha1h);
        hashesObj.insert(QStringLiteral("sha512"), sha512h);
        fileObj.insert(QStringLiteral("hashes"), hashesObj);
        fileObj.insert(QStringLiteral("downloads"),
                        QJsonArray{ QString::fromUtf8(url.toEncoded(QUrl::FullyEncoded)) });

        qint64 declaredSize = mod.fileSize;
        if (declaredSize <= 0 && !options.localModsDirectory.trimmed().isEmpty()) {
            QFileInfo lf(QDir(options.localModsDirectory.trimmed()).filePath(mod.filename));
            if (lf.exists() && lf.isFile())
                declaredSize = lf.size();
        }
        if (declaredSize > 0)
            fileObj.insert(QStringLiteral("fileSize"), declaredSize);

        QJsonObject env;
        env.insert(QStringLiteral("client"),
            normalizedEnvSide(mod.clientSide, QStringLiteral("required")));
        env.insert(QStringLiteral("server"),
            normalizedEnvSide(mod.serverSide, QStringLiteral("required")));
        fileObj.insert(QStringLiteral("env"), env);

        fileArr.append(fileObj);
    }
    index.insert(QStringLiteral("files"), fileArr);
    return index;
}

bool PackExporter::exportToMrpack(const QVector<ModInfo>& mods,
                                   const ExportOptions& options,
                                   QString* errorOut) {
    emit exportProgress(0, QStringLiteral("Preparing .mrpack export..."));

    const QString dk = mrpackDepsKey(options.loader);
    if (dk.isEmpty()) {
        const QString msg = QStringLiteral(
            "Unsupported loader for .mrpack. Use Fabric, Quilt, Forge, or NeoForge.");
        if (errorOut) *errorOut = msg;
        emit exportFinished(false, {}, msg);
        return false;
    }
    if (options.loaderVersion.trimmed().isEmpty()) {
        const QString msg = QStringLiteral(
            "Loader library version missing (required in modrinth.index.json dependencies).");
        if (errorOut) *errorOut = msg;
        emit exportFinished(false, {}, msg);
        return false;
    }

    QString outputPath = options.outputPath;
    if (outputPath.isEmpty()) {
        outputPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
                     + QStringLiteral("/") + options.packName + QStringLiteral(".mrpack");
    }
    if (!outputPath.endsWith(QStringLiteral(".mrpack"), Qt::CaseInsensitive))
        outputPath += QStringLiteral(".mrpack");

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    mz_bool status = mz_zip_writer_init_file(&zip, outputPath.toUtf8().constData(), 0);
    if (!status) {
        if (errorOut)
            *errorOut = QStringLiteral("Failed to create zip file: ") + outputPath;
        emit exportFinished(false, outputPath, QStringLiteral("Failed to create zip"));
        return false;
    }

    emit exportProgress(18, QStringLiteral("Building modrinth.index.json..."));
    QJsonObject index = generateMrpackIndex(mods, options);

    QJsonArray fileArr = index.value(QStringLiteral("files")).toArray();
    if (fileArr.isEmpty()) {
        mz_zip_writer_end(&zip);
        QFile::remove(outputPath);
        const QString msg = QStringLiteral(
            "Cannot build .mrpack: each mod entry needs HTTPS download URL plus sha1 and sha512 "
            "(from Modrinth API or from hashing local jars in your download folder).");
        if (errorOut) *errorOut = msg;
        emit exportFinished(false, outputPath, msg);
        return false;
    }

    QByteArray indexData = QJsonDocument(index).toJson(QJsonDocument::Indented);
    if (!addFileToZip(&zip, QStringLiteral("modrinth.index.json"), indexData)) {
        mz_zip_writer_end(&zip);
        QFile::remove(outputPath);
        if (errorOut) *errorOut = QStringLiteral("Failed to embed modrinth.index.json");
        emit exportFinished(false, outputPath, QStringLiteral("Index zip write failed"));
        return false;
    }

    emit exportProgress(42, QStringLiteral("Adding overrides..."));
    if (options.includeConfigs && !options.configOverridesPath.trimmed().isEmpty()
        && QFileInfo::exists(options.configOverridesPath.trimmed())) {
        if (!addDirectoryTreeToZip(&zip,
                options.configOverridesPath.trimmed(),
                QStringLiteral("overrides/"))) {
            mz_zip_writer_end(&zip);
            QFile::remove(outputPath);
            if (errorOut) *errorOut = QStringLiteral("Failed to zip overrides/");
            emit exportFinished(false, outputPath, QStringLiteral("overrides zip failed"));
            return false;
        }
    }

    emit exportProgress(85, QStringLiteral("Finalizing .mrpack archive..."));
    mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);

    emit exportProgress(100, QStringLiteral("Export complete."));
    emit exportFinished(true, outputPath, QString());
    return true;
}

// ============================================================
// CURSEFORGE .zip GENERATION
// ============================================================
QJsonObject PackExporter::generateCFManifest(const QVector<ModInfo>& mods,
                                              const ExportOptions& options,
                                              QString* errorMessage) {
    QJsonObject manifest;
    manifest.insert(QStringLiteral("manifestType"), QStringLiteral("minecraftModpack"));
    manifest.insert(QStringLiteral("manifestVersion"), 1);
    manifest.insert(QStringLiteral("name"), options.packName);
    manifest.insert(QStringLiteral("version"), options.packVersion);
    manifest.insert(QStringLiteral("author"), options.author);
    manifest.insert(QStringLiteral("overrides"), QStringLiteral("overrides"));

    const QString loaderManifestId =
        curseForgeModLoaderManifestId(options.loader, options.mcVersion, options.loaderVersion);

    QJsonObject mc;
    mc.insert(QStringLiteral("version"), options.mcVersion.trimmed());
    QJsonObject loaderObj;
    loaderObj.insert(QStringLiteral("primary"), true);

    if (loaderManifestId.isEmpty()) {
        if (errorMessage) {
            *errorMessage =
                QStringLiteral(
                    "CurseForge manifest needs a loader id (e.g. fabric-0.15.11, forge-47.2.0, "
                    "neoforge-{mc}-{version}). Specify loader version.");
        }
        return {};
    }
    loaderObj.insert(QStringLiteral("id"), loaderManifestId);
    mc.insert(QStringLiteral("modLoaders"), QJsonArray({loaderObj}));
    manifest.insert(QStringLiteral("minecraft"), mc);

    QJsonArray filesArr;
    for (const auto& mod : mods) {
        bool okPid = false;
        const qint64 pid = mod.projectId.trimmed().toLongLong(&okPid);
        if (!okPid || pid <= 0)
            continue;
        if (mod.curseforgeFileId <= 0)
            continue;
        QJsonObject fileObj;
        fileObj.insert(QStringLiteral("projectID"), pid);
        fileObj.insert(QStringLiteral("fileID"), mod.curseforgeFileId);
        fileObj.insert(QStringLiteral("required"), true);
        filesArr.append(fileObj);
    }
    manifest.insert(QStringLiteral("files"), filesArr);

    if (filesArr.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Every CurseForge pack entry requires both CurseForge projectID and fileID. "
                "Mods resolved only via Modrinth cannot be exported in this format — export .mrpack instead.");
        }
        return {};
    }

    if (errorMessage)
        errorMessage->clear();
    return manifest;
}

QString PackExporter::generateModListHtml(const QVector<ModInfo>& mods) {
    QString html;
    html += QStringLiteral("<ul>\n");
    for (const auto& m : mods) {
        bool ok = false;
        m.projectId.trimmed().toLongLong(&ok);
        QString href;
        if (ok)
            href = QStringLiteral("https://www.curseforge.com/minecraft/mc-mods/%1").arg(m.projectId);
        else if (!m.slug.trimmed().isEmpty())
            href = QStringLiteral("https://modrinth.com/mod/%1").arg(m.slug);
        else
            href = QStringLiteral("https://modrinth.com/mod/%1").arg(m.projectId);
        html +=
            QStringLiteral("    <li><a href=\"%1\">%2</a></li>\n").arg(href, m.name.toHtmlEscaped());
    }
    html += QStringLiteral("</ul>\n");
    return html;
}

bool PackExporter::exportToCurseForge(const QVector<ModInfo>& mods,
                                      const ExportOptions& options,
                                      QString* errorOut) {
    emit exportProgress(0, QStringLiteral("Preparing CurseForge modpack.zip..."));

    QString outputPath = options.outputPath;
    if (outputPath.isEmpty()) {
        outputPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
                     + QStringLiteral("/") + options.packName + QStringLiteral("-cf.zip");
    }
    if (!outputPath.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive))
        outputPath += QStringLiteral(".zip");

    QString mfErr;
    QJsonObject manifest = generateCFManifest(mods, options, &mfErr);
    if (manifest.isEmpty()) {
        if (errorOut && !mfErr.isEmpty())
            *errorOut = mfErr;
        emit exportFinished(false, outputPath, mfErr);
        return false;
    }

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    mz_bool mzOk = mz_zip_writer_init_file(&zip, outputPath.toUtf8().constData(), 0);
    if (!mzOk) {
        if (errorOut) *errorOut = QStringLiteral("Failed to create zip file");
        emit exportFinished(false, outputPath, QStringLiteral("Zip init failed"));
        return false;
    }

    emit exportProgress(25, QStringLiteral("Writing manifest.json..."));
    QByteArray mj = QJsonDocument(manifest).toJson(QJsonDocument::Indented);
    if (!addFileToZip(&zip, QStringLiteral("manifest.json"), mj)) {
        mz_zip_writer_end(&zip);
        QFile::remove(outputPath);
        if (errorOut) *errorOut = QStringLiteral("manifest.json zip write failed");
        emit exportFinished(false, outputPath, QStringLiteral("manifest write failed"));
        return false;
    }

    emit exportProgress(50, QStringLiteral("Writing modlist.html..."));
    const QString html = QStringLiteral("<html><body><h1>")
        + options.packName.toHtmlEscaped() + QStringLiteral("</h1>\n") + generateModListHtml(mods)
        + QStringLiteral("</body></html>");
    if (!addFileToZip(&zip, QStringLiteral("modlist.html"), html.toUtf8())) {
        mz_zip_writer_end(&zip);
        QFile::remove(outputPath);
        if (errorOut) *errorOut = QStringLiteral("modlist.html zip write failed");
        emit exportFinished(false, outputPath, QStringLiteral("modlist write failed"));
        return false;
    }

    if (options.includeConfigs && !options.configOverridesPath.trimmed().isEmpty()
        && QFileInfo::exists(options.configOverridesPath.trimmed())) {
        if (!addDirectoryTreeToZip(&zip,
                options.configOverridesPath.trimmed(),
                QStringLiteral("overrides/"))) {
            mz_zip_writer_end(&zip);
            QFile::remove(outputPath);
            if (errorOut) *errorOut = QStringLiteral("overrides zip failed");
            emit exportFinished(false, outputPath, QStringLiteral("overrides failed"));
            return false;
        }
    }

    mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    emit exportProgress(100, QStringLiteral("Done."));
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
        addFileToZip(&zip,
            QStringLiteral("mods/%1.meta.json").arg(mod.filename),
            metaData);

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
                     "Generated by CraftPacker " + craftPackerVersionString() + "\n"
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

QString PackExporter::suggestedLoaderVersion(const QString& loader, const QString& mcVersion) {
    const QString lw = loader.trimmed().toLower();
    const QString mc = mcVersion.trimmed();
    if (mc.isEmpty())
        return {};

    if (lw == QLatin1String("fabric") || lw == QLatin1String("quilt")) {
        const QUrl metaUrl(
            lw == QLatin1String("fabric")
                ? QStringLiteral("https://meta.fabricmc.net/v2/versions/loader/%1").arg(mc)
                : QStringLiteral("https://meta.quiltmc.org/v3/versions/loader/%1").arg(mc));

        QNetworkAccessManager nam;
        QNetworkRequest req(metaUrl);
        req.setHeader(QNetworkRequest::UserAgentHeader,
            craftPackerModrinthUserAgent());
        QNetworkReply* reply = nam.get(req);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        if (reply->error() != QNetworkReply::NoError) {
            reply->deleteLater();
            return {};
        }
        const QByteArray body = reply->readAll();
        reply->deleteLater();

        QJsonParseError parseErr{};
        QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isArray())
            return {};

        const QJsonArray arr = doc.array();
        if (lw == QLatin1String("fabric")) {
            for (const auto& entry : arr) {
                QJsonObject lo = entry.toObject().value(QStringLiteral("loader")).toObject();
                if (!lo.value(QStringLiteral("stable")).toBool(false))
                    continue;
                const QString v = lo.value(QStringLiteral("version")).toString();
                if (!v.isEmpty())
                    return v;
            }
            for (const auto& entry : arr) {
                const QString v =
                    entry.toObject().value(QStringLiteral("loader")).toObject().value(QStringLiteral("version")).toString();
                if (!v.isEmpty())
                    return v;
            }
        } else {
            // Quilt: skip obvious pre-release tags when possible
            for (const auto& entry : arr) {
                const QString v =
                    entry.toObject().value(QStringLiteral("loader")).toObject().value(QStringLiteral("version")).toString();
                if (v.isEmpty())
                    continue;
                if (v.contains(QLatin1String("beta"), Qt::CaseInsensitive)
                    || v.contains(QLatin1String("alpha"), Qt::CaseInsensitive))
                    continue;
                return v;
            }
            for (const auto& entry : arr) {
                const QString v =
                    entry.toObject().value(QStringLiteral("loader")).toObject().value(QStringLiteral("version")).toString();
                if (!v.isEmpty())
                    return v;
            }
        }
    }
    return {};
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