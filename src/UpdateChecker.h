#pragma once
#include <QObject>
#include <QHash>
#include <QSet>
#include <QVector>
#include "ModrinthAPI.h"

// Phase 5: Version Migration Wizard & Updates
// Update Dashboard: Pings APIs to check for newer versions.
// Version Retargeting: Allows changing MC version and finding matching mods.
struct UpdateStatus {
    QString projectId;
    QString modName;
    QString currentVersionId;
    QString latestVersionId;
    enum Status { UpToDate, UpdateAvailable, NotFound, Error };
    Status status = UpToDate;
    QString newVersionName;
};

struct MigrationResult {
    QString projectId;
    QString modName;
    bool foundForNewVersion = false;
    ModInfo newVersionInfo;
    QString error;
};

class UpdateChecker : public QObject {
    Q_OBJECT
public:
    static UpdateChecker& instance();

    // Check a list of mods for updates (Phase 5: Update Dashboard)
    void checkForUpdates(const QVector<ModInfo>& mods);

    // Migrate a list of mods to a new MC version (Phase 5: Version Retargeting)
    void migrateToVersion(const QVector<ModInfo>& currentMods,
                           const QString& newLoader,
                           const QString& newGameVersion);

signals:
    void updateCheckProgress(int checked, int total);
    void updateCheckComplete(const QVector<UpdateStatus>& results);
    void migrationProgress(int processed, int total);
    void migrationComplete(const QVector<MigrationResult>& results);

private:
    UpdateChecker();
    ~UpdateChecker() = default;
    UpdateChecker(const UpdateChecker&) = delete;
    UpdateChecker& operator=(const UpdateChecker&) = delete;

    QVector<UpdateStatus> m_results;
    QVector<MigrationResult> m_migrationResults;
    int m_checksPending = 0;
    int m_totalChecks = 0;
};