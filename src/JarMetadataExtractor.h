#pragma once
#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QByteArray>

// ============================================================
// Canonical mod metadata extracted from JAR internals
// Production-grade: reads from fabric.mod.json, META-INF/mods.toml,
// MANIFEST.MF, and only falls back to filename heuristics.
// ============================================================
struct ExtractedModMetadata {
    // Identity
    QString modId;              // Canonical mod identifier (e.g. "fabric-api", "sodium")
    QString displayName;        // Human-readable name (e.g. "Fabric API", "Sodium")
    QString version;            // Version string (e.g. "0.92.8+1.20.1")
    QString description;        // Brief description from metadata
    QString environment;        // "client", "server", "*" (both)
    QString detectedLoader;     // "Fabric", "Forge", "NeoForge", "Quilt", "Unknown"

    // Source tracking
    QString sourceFileName;     // Original JAR filename
    QString sourceFilePath;     // Full path to JAR
    QString extractionMethod;   // "fabric.mod.json", "mods.toml", "MANIFEST.MF", "filename heuristic"
    qint64 fileSize = 0;

    // Resolution
    bool isEmpty() const { return modId.isEmpty() && displayName.isEmpty(); }

    // Get the best display name: prefer displayName, then modId, then filename
    QString bestName() const {
        if (!displayName.isEmpty()) return displayName;
        if (!modId.isEmpty()) return modId;
        // Strip version from filename as last resort
        QString base = sourceFileName;
        if (base.endsWith(".jar")) base.chop(4);
        // Remove trailing version patterns
        base.remove(QRegularExpression(R"([-_]?\d+(\.\d+)+.*$)"));
        return base.trimmed();
    }
};

// ============================================================
// JarMetadataExtractor
// Single canonical pipeline for extracting mod metadata from JAR files.
// Used by: import-from-folder, debugger, resolver
// ============================================================
class JarMetadataExtractor {
public:
    // Extract metadata from a JAR file's internal contents
    // Reads the file, inspects fabric.mod.json, mods.toml, MANIFEST.MF in order
    static ExtractedModMetadata extract(const QString& jarFilePath);

    // Extract from already-loaded bytes (for batch/threaded usage)
    static ExtractedModMetadata extractFromBytes(const QByteArray& jarData,
                                                  const QString& jarFilePath,
                                                  const QString& jarFileName);

private:
    // Attempt to parse fabric.mod.json
    static bool tryFabricModJson(const QByteArray& jarData,
                                  ExtractedModMetadata& meta);

    // Attempt to parse META-INF/mods.toml or META-INF/neoforge.mods.toml
    static bool tryModsToml(const QByteArray& jarData,
                             ExtractedModMetadata& meta);

    // Attempt to parse META-INF/MANIFEST.MF
    static bool tryManifestMf(const QByteArray& jarData,
                               ExtractedModMetadata& meta);

    // Filename fallback: strip version patterns from filename
    static void applyFilenameFallback(const QString& jarFileName,
                                       ExtractedModMetadata& meta);

    // Detect loader from binary markers in JAR data
    static QString detectLoaderFromBytes(const QByteArray& jarData);

    // Helper: extract file from ZIP archive in memory
    static QByteArray extractFileFromZip(const QByteArray& jarData,
                                          const QString& filename);
};

#include <QRegularExpression>