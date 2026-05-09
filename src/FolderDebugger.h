#ifndef FOLDER_DEBUGGER_H
#define FOLDER_DEBUGGER_H

#include <QObject>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

// ============================================================
// Finding severity model (Phase 8.1)
// ============================================================
enum class DebuggerSeverity {
    Critical,   // confirmed incompatible class collision, loader mismatch
    Warning,    // duplicate mod, missing dependency, suspicious overlap
    Info        // not on Modrinth API, unknown metadata
};

// ============================================================
// Finding category (Phase 8.1)
// ============================================================
enum class FindingCategory {
    DuplicateMod,           // same mod identity, multiple copies
    RealClassCollision,     // different mods, same non-ignored class
    DerivedOverlap,         // duplicate-derived overlap (collapsed)
    LoaderMismatch,
    MissingDependency,
    ConfirmedIncompatibility,
    SuspiciousOverlap,      // different mods, uncertain identity
    Informational
};

// ============================================================
// A single finding (Phase 8.1)
// ============================================================
struct DebuggerFinding {
    FindingCategory category;
    DebuggerSeverity severity;

    QString title;              // "Duplicate Mod: Sodium"
    QString summary;            // "Two copies of Sodium found with 173 overlapping classes"
    QString explanation;        // "These JARs appear to be duplicate versions... Keep only one."
    QString recommendation;     // "Remove the older copy: sodium-accidental-duplicate.jar"

    // Affected mods/jars
    QStringList affectedJars;   // JAR filenames
    QStringList affectedProjectIds; // Modrinth project IDs

    // Derived data (for expandable details)
    QStringList classPaths;     // Overlapping class paths (if applicable)

    // For sorting: Critical > Warning > Info
    int sortPriority() const {
        return static_cast<int>(severity);
    }
};

// ============================================================
// Legacy structs (kept for Dashboard compatibility)
// ============================================================
struct DebuggerModResult {
    QString jarFileName;
    QString jarFilePath;
    QString sha1Hash;
    QString detectedLoader;
    QString apiProjectId;
    QString apiProjectName;
    enum Issue {
        NoIssue = 0,
        DuplicateMod = 1,
        LoaderMismatch = 2,
        ClassCollision = 4,
        MissingApi = 8,
        ApiNotFound = 16
    };
    Q_DECLARE_FLAGS(Issues, Issue)
    Issues issues = NoIssue;
    QString reasonText;
    int duplicateCount = 0;
    qint64 fileSize = 0;

    QString severityText() const {
        if (issues == NoIssue) return "OK";
        if (issues == ApiNotFound) return "Info";
        if (issues & (DuplicateMod | LoaderMismatch | ClassCollision)) return "Error";
        if (issues & MissingApi) return "Warning";
        return "Info";
    }

    bool isErrorSeverity() const {
        if (issues == NoIssue) return false;
        if (issues == ApiNotFound) return false;
        if (issues & (DuplicateMod | LoaderMismatch | ClassCollision)) return true;
        if (issues & MissingApi) return false;
        return false;
    }
};
Q_DECLARE_OPERATORS_FOR_FLAGS(DebuggerModResult::Issues)

struct ClassCollision {
    QString classPath;
    QStringList conflictingJars;
};

struct DebuggerSummary {
    int totalJars = 0;
    int fabricCount = 0;
    int forgeCount = 0;
    int neoforgeCount = 0;
    int quiltCount = 0;
    int unknownCount = 0;
    int issueCount = 0;
    int duplicateCount = 0;
    int collisionCount = 0;
    bool hasFabricApi = false;
    bool missingFabricApi = false;
    QString dominantLoader;

    // Phase 8.1: New verdict fields
    int criticalCount = 0;
    int warningCount = 0;
    int infoCount = 0;
    QString verdictText;        // e.g. "Likely safe after cleanup"
    QString verdictDetail;      // One-sentence explanation
};

// ============================================================
// FolderDebugger runs in BACKGROUND thread (QThreadPool)
// ============================================================
class FolderDebugger : public QObject {
    Q_OBJECT

public:
    explicit FolderDebugger(QObject *parent = nullptr);

    void debugFolder(const QString& folderPath);
    void cancel();

    // Public for Dashboard access
    struct JarIdentity {
        QString jarName;
        QString jarPath;
        QString projectId;      // Modrinth project ID (empty if unknown)
        QString projectName;    // API name (empty if unknown)
        QString detectedLoader; // "Fabric", "Forge", etc.
        qint64 fileSize = 0;
        QByteArray jarData;     // Raw bytes for class scanning
        bool isDuplicate = false; // Another jar has same projectId
    };

signals:
    void scanProgress(int current, int total, const QString& currentFile);
    void modResultReady(const DebuggerModResult& result);
    void collisionFound(const ClassCollision& collision);
    void findingReady(const DebuggerFinding& finding);  // Phase 8.1
    void summaryReady(const DebuggerSummary& summary);
    void scanFinished();
    void scanError(const QString& error);

private:
    QString hashFile(const QString& filePath);
    QString lookupHashOnModrinth(const QString& sha1Hash, QString& outProjectName);
    QString detectLoader(const QByteArray& jarData);
    QVector<ClassCollision> scanClassCollisions(
        const QHash<QString, QByteArray>& jarDataMap,
        const QSet<QString>& duplicateJarNames);  // NEW: exclude duplicates
    bool shouldIgnoreClassPath(const QString& path);

    // Phase 8.1: Triage layer
    void generateFindings(
        const QVector<JarIdentity>& identities,
        const QHash<QString, QByteArray>& jarDataMap,
        const QVector<ClassCollision>& rawCollisions,
        const QString& dominantLoader);

    // Phase 8.1: Compute verdict
    QString computeVerdict(int critical, int warning, int info);

    volatile bool m_cancelled = false;
    QVector<DebuggerModResult> m_results;
    QHash<QString, int> m_projectIdCounts;
    QString m_loaderMode;
    int m_fabricCount = 0;
    int m_forgeCount = 0;
    int m_neoforgeCount = 0;
    int m_quiltCount = 0;
};

#endif // FOLDER_DEBUGGER_H