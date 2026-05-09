#include "JarMetadataExtractor.h"
#include "miniz.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QDebug>
#include <cstring>

// ============================================================
// Public API
// ============================================================
ExtractedModMetadata JarMetadataExtractor::extract(const QString& jarFilePath) {
    QFileInfo fi(jarFilePath);
    QFile file(jarFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        ExtractedModMetadata meta;
        meta.sourceFileName = fi.fileName();
        meta.sourceFilePath = jarFilePath;
        meta.extractionMethod = "error: cannot read";
        return meta;
    }
    QByteArray data = file.readAll();
    file.close();
    return extractFromBytes(data, jarFilePath, fi.fileName());
}

ExtractedModMetadata JarMetadataExtractor::extractFromBytes(
    const QByteArray& jarData,
    const QString& jarFilePath,
    const QString& jarFileName)
{
    ExtractedModMetadata meta;
    meta.sourceFileName = jarFileName;
    meta.sourceFilePath = jarFilePath;
    meta.fileSize = jarData.size();

    // Step 1: fabric.mod.json (Fabric/Quilt)
    if (tryFabricModJson(jarData, meta)) {
        meta.extractionMethod = "fabric.mod.json";
        meta.detectedLoader = detectLoaderFromBytes(jarData);
        return meta;
    }

    // Step 2: META-INF/mods.toml (Forge/NeoForge)
    if (tryModsToml(jarData, meta)) {
        meta.extractionMethod = "mods.toml";
        meta.detectedLoader = detectLoaderFromBytes(jarData);
        return meta;
    }

    // Step 3: MANIFEST.MF (fallback)
    if (tryManifestMf(jarData, meta)) {
        meta.extractionMethod = "MANIFEST.MF";
        meta.detectedLoader = detectLoaderFromBytes(jarData);
        return meta;
    }

    // Step 4: Filename heuristics (last resort)
    meta.extractionMethod = "filename heuristic";
    meta.detectedLoader = detectLoaderFromBytes(jarData);
    applyFilenameFallback(jarFileName, meta);
    return meta;
}

// ============================================================
// Step 1: fabric.mod.json
// ============================================================
bool JarMetadataExtractor::tryFabricModJson(const QByteArray& jarData,
                                              ExtractedModMetadata& meta) {
    QByteArray content = extractFileFromZip(jarData, "fabric.mod.json");
    if (content.isEmpty()) return false;

    QJsonDocument doc = QJsonDocument::fromJson(content);
    if (!doc.isObject()) return false;

    QJsonObject obj = doc.object();
    meta.modId = obj["id"].toString();
    meta.displayName = obj["name"].toString();
    if (meta.displayName.isEmpty()) meta.displayName = meta.modId;
    meta.version = obj["version"].toString();
    meta.description = obj["description"].toString();

    // Parse environment from "environment" or "side" field
    QString env = obj["environment"].toString();
    if (env.isEmpty()) env = obj["side"].toString();
    meta.environment = env;

    // If there's no name, use modId as display name
    if (meta.displayName.isEmpty()) meta.displayName = meta.modId;

    return !meta.modId.isEmpty();
}

// ============================================================
// Step 2: META-INF/mods.toml (Forge/NeoForge)
// ============================================================
bool JarMetadataExtractor::tryModsToml(const QByteArray& jarData,
                                         ExtractedModMetadata& meta) {
    QByteArray content = extractFileFromZip(jarData, "META-INF/neoforge.mods.toml");
    if (content.isEmpty()) {
        content = extractFileFromZip(jarData, "META-INF/mods.toml");
    }
    if (content.isEmpty()) return false;

    // Parse TOML-like format manually (no TOML parser dependency)
    QString tomlText = QString::fromUtf8(content);

    // Search markers directly to avoid raw string literal issues
    // The patterns: modId = "value", displayName = "value", etc.
    auto extractFromToml = [&](const QString& key) -> QString {
        QString marker = key + "=\"";
        int pos = tomlText.indexOf(marker, 0, Qt::CaseInsensitive);
        if (pos < 0) {
            marker = key + " = \"";
            pos = tomlText.indexOf(marker, 0, Qt::CaseInsensitive);
        }
        if (pos < 0) return {};
        int start = pos + marker.length();
        int end = tomlText.indexOf('"', start);
        if (end < 0) return {};
        return tomlText.mid(start, end - start);
    };

    meta.modId = extractFromToml("modId");
    if (meta.displayName.isEmpty())
        meta.displayName = extractFromToml("displayName");
    if (meta.version.isEmpty())
        meta.version = extractFromToml("version");
    if (meta.description.isEmpty())
        meta.description = extractFromToml("description");

    // If no displayName, use modId
    if (meta.displayName.isEmpty()) meta.displayName = meta.modId;

    return !meta.modId.isEmpty();
}

// ============================================================
// Step 3: META-INF/MANIFEST.MF
// ============================================================
bool JarMetadataExtractor::tryManifestMf(const QByteArray& jarData,
                                           ExtractedModMetadata& meta) {
    QByteArray content = extractFileFromZip(jarData, "META-INF/MANIFEST.MF");
    if (content.isEmpty()) return false;

    QString text = QString::fromUtf8(content);

    // Look for Implementation-Title and Implementation-Version
    static const QRegularExpression titleRe(R"(Implementation-Title:\s*(.+)\s*)");
    static const QRegularExpression versionRe(R"(Implementation-Version:\s*(.+)\s*)");
    static const QRegularExpression nameRe(R"(Implementation-Vendor:\s*(.+)\s*)");

    auto match = titleRe.match(text);
    if (match.hasMatch()) meta.displayName = match.captured(1).trimmed();

    match = versionRe.match(text);
    if (match.hasMatch()) meta.version = match.captured(1).trimmed();

    // Try to extract modId from Automatic-Module-Name
    static const QRegularExpression moduleRe(R"(Automatic-Module-Name:\s*([^\s]+))");
    match = moduleRe.match(text);
    if (match.hasMatch()) {
        meta.modId = match.captured(1).trimmed();
        if (meta.modId.contains('.')) {
            // Module names like "me.shedaniel.cloth" -> extract last part
            QStringList parts = meta.modId.split('.');
            if (parts.size() > 1) meta.displayName = parts.last();
        }
    }

    return !meta.displayName.isEmpty() || !meta.modId.isEmpty();
}

// ============================================================
// Step 4: Filename fallback
// ============================================================
void JarMetadataExtractor::applyFilenameFallback(const QString& jarFileName,
                                                   ExtractedModMetadata& meta) {
    if (!meta.isEmpty()) return; // Already have metadata

    QString base = jarFileName;
    if (base.endsWith(".jar")) base.chop(4);

    // Remove Fabric/Forge/NeoForge markers
    static const QRegularExpression loaderTagRe(
        R"([-_])?((fabric|forge|neoforge|quilt)[-_]?(mc)?\d?)",
        QRegularExpression::CaseInsensitiveOption);
    base = base.remove(loaderTagRe);

    // Remove version patterns like -0.92.8+1.20.1, v0.5.3, [1.20.1]
    static const QRegularExpression versionRe(R"([-_]?[vV]?\d+\.\d+[^ ]*)");
    base = base.remove(versionRe);

    // Remove trailing brackets
    base = base.remove(QRegularExpression(R"(\s*\(.*\)\s*$)"));
    base = base.remove(QRegularExpression(R"(\s*\[.*\]\s*$)"));

    base = base.replace(QRegularExpression("[-_]"), " ");
    base = base.simplified().trimmed();

    meta.modId = base.toLower().replace(' ', '-');
    meta.displayName = base;
    meta.version.clear(); // Version couldn't be extracted
}

// ============================================================
// Detect loader from binary markers
// ============================================================
QString JarMetadataExtractor::detectLoaderFromBytes(const QByteArray& jarData) {
    if (jarData.isEmpty()) return "Unknown";
    if (jarData.contains("fabric.mod.json")) return "Fabric";
    if (jarData.contains("quilt.mod.json")) return "Quilt";
    if (jarData.contains("neoforge.mods.toml")) return "NeoForge";
    if (jarData.contains("META-INF/mods.toml")) return "Forge";

    // Try miniz as fallback
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (mz_zip_reader_init_mem(&zip, jarData.constData(), jarData.size(), 0)) {
        QString loader;
        if (mz_zip_reader_locate_file(&zip, "fabric.mod.json", nullptr, 0) >= 0)
            loader = "Fabric";
        else if (mz_zip_reader_locate_file(&zip, "quilt.mod.json", nullptr, 0) >= 0)
            loader = "Quilt";
        else if (mz_zip_reader_locate_file(&zip, "META-INF/neoforge.mods.toml", nullptr, 0) >= 0)
            loader = "NeoForge";
        else if (mz_zip_reader_locate_file(&zip, "META-INF/mods.toml", nullptr, 0) >= 0)
            loader = "Forge";
        mz_zip_reader_end(&zip);
        return loader.isEmpty() ? "Unknown" : loader;
    }
    return "Unknown";
}

// ============================================================
// Helper: extract file from ZIP archive in memory using miniz
// ============================================================
QByteArray JarMetadataExtractor::extractFileFromZip(const QByteArray& jarData,
                                                      const QString& filename) {
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_mem(&zip, jarData.constData(), jarData.size(), 0)) {
        return {};
    }

    int idx = mz_zip_reader_locate_file(&zip, filename.toUtf8().constData(), nullptr, 0);
    if (idx < 0) {
        mz_zip_reader_end(&zip);
        return {};
    }

    size_t size;
    char* data = (char*)mz_zip_reader_extract_to_heap(&zip, idx, &size, 0);
    if (!data || size == 0) {
        mz_zip_reader_end(&zip);
        return {};
    }

    QByteArray result(data, (int)size);
    mz_free(data);
    mz_zip_reader_end(&zip);
    return result;
}