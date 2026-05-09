#pragma once
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QHash>
#include <QSet>
#include <QPair>
#include <QVector>
#include "ModrinthAPI.h" // for ModInfo

// Phase 4: Smart Conflict Detection & Client/Server Splitting
struct ConflictWarning {
    QString modA;
    QString modB;
    QString reason; // "incompatible", "duplicate_function", "missing_dependency"
};

struct ServerSplitResult {
    QVector<ModInfo> serverMods;      // Mods that work on server
    QVector<ModInfo> clientOnlyMods;  // client_side:required && server_side:unsupported
    QVector<ModInfo> bothMods;        // Mods that work on both
    QStringList warnings;
};

struct KnownConflictGroup {
    QSet<QString> mods;           // Normalized mod names in this conflict group
    QVector<QString> orderedNorm; // Normalized forms preserving JSON insertion order (for star-pattern logic)
    QString reason;               // Human-readable explanation
    QString severity;             // "high", "medium", "low"
};

class ConflictDetector {
public:
    static ConflictDetector& instance();

    // Analyze a list of mods for conflicts
    // Combines local known_conflicts.json + Modrinth API incompatible deps
    QVector<ConflictWarning> detectConflicts(const QVector<ModInfo>& mods);

    // Split mods into server/client categories based on client_side/server_side fields
    ServerSplitResult splitForServer(const QVector<ModInfo>& mods);

    // Check if any mods are missing required dependencies
    QVector<QString> findMissingDependencies(const QVector<ModInfo>& mods);

    // Get all unique incompatible pairs from a dependency tree + local conflict DB
    // rawInputNames: the user's original typed input names (including not-found mods)
    QSet<QPair<QString, QString>> getIncompatiblePairs(const QVector<ModInfo>& mods,
                                                         const QVector<QString>& rawInputNames = {});

    // Build dependency link graph from resolved mods using their real Modrinth dependency metadata
    // Returns a set of (projectIdA, projectIdB) pairs linked by required/optional dependencies
    QSet<QPair<QString, QString>> buildDependencyLinks(const QVector<ModInfo>& mods);

    // Check for duplicate functionality (same slug, different versions)
    QStringList findDuplicates(const QVector<ModInfo>& mods);

    // Load known conflicts from the local JSON file
    void loadKnownConflicts();

private:
    ConflictDetector() = default;
    ~ConflictDetector() = default;
    ConflictDetector(const ConflictDetector&) = delete;
    ConflictDetector& operator=(const ConflictDetector&) = delete;

    // Parse QJsonArray of dependencies into a structured format
    struct Dependency {
        QString projectId;
        QString dependencyType; // "required", "optional", "incompatible"
        QString versionId;
    };
    QVector<Dependency> parseDependencies(const QJsonArray& deps) const;

    // Known conflict groups loaded from known_conflicts.json
    QVector<KnownConflictGroup> m_knownConflictGroups;

    // Known compatible groups (allowlist overrides false conflict detection)
    struct KnownCompatibleGroup {
        QSet<QString> mods; // Normalized mod names in this compatible group
        QString reason;
    };
    QVector<KnownCompatibleGroup> m_knownCompatibleGroups;
};
