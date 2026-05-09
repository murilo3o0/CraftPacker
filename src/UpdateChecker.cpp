#include "UpdateChecker.h"
#include "AsyncWorker.h"

UpdateChecker& UpdateChecker::instance() {
    static UpdateChecker inst;
    return inst;
}

UpdateChecker::UpdateChecker()
    : QObject(nullptr)
{
}

void UpdateChecker::checkForUpdates(const QVector<ModInfo>& mods) {
    m_results.clear();
    m_totalChecks = mods.size();
    m_checksPending = m_totalChecks;

    if (mods.isEmpty()) {
        emit updateCheckComplete(m_results);
        return;
    }

    auto& api = ModrinthAPI::instance();

    for (const auto& mod : mods) {
        // Connect to the API's updateCheckResult signal
        QMetaObject::Connection* conn = new QMetaObject::Connection();
        *conn = connect(&api, &ModrinthAPI::updateCheckResult,
            this, [this, conn, modId = mod.projectId, modName = mod.name, modVersionId = mod.versionId]
            (const QString& projectId, bool hasUpdate, const QString& newVersionId) {
            if (projectId != modId) return;

            // Disconnect this specific handler
            disconnect(*conn);
            delete conn;

            UpdateStatus status;
            status.projectId = projectId;
            status.modName = modName;
            status.currentVersionId = modVersionId;

            if (hasUpdate) {
                status.status = UpdateStatus::UpdateAvailable;
                status.latestVersionId = newVersionId;
                status.newVersionName = newVersionId;
            } else {
                status.status = UpdateStatus::UpToDate;
            }

            m_results.append(status);
            m_checksPending--;

            emit updateCheckProgress(m_totalChecks - m_checksPending, m_totalChecks);

            if (m_checksPending == 0) {
                emit updateCheckComplete(m_results);
            }
        });

        // Initiate the check
        api.checkForUpdate(mod.projectId, mod.versionId);
    }
}

void UpdateChecker::migrateToVersion(const QVector<ModInfo>& currentMods,
                                      const QString& newLoader,
                                      const QString& newGameVersion) {
    m_migrationResults.clear();
    m_totalChecks = currentMods.size();
    m_checksPending = m_totalChecks;

    if (currentMods.isEmpty()) {
        emit migrationComplete(m_migrationResults);
        return;
    }

    auto& api = ModrinthAPI::instance();

    for (const auto& mod : currentMods) {
        QMetaObject::Connection* conn = new QMetaObject::Connection();
        *conn = connect(&api,
            static_cast<void(ModrinthAPI::*)(const QString&, const ModInfo&)>(&ModrinthAPI::modVersionResult),
            this, [this, conn, modId = mod.projectId, modName = mod.name]
            (const QString& projectId, const ModInfo& info) {
            if (projectId != modId) return;

            disconnect(*conn);
            delete conn;

            MigrationResult result;
            result.projectId = projectId;
            result.modName = modName;

            if (info.isValid()) {
                result.foundForNewVersion = true;
                result.newVersionInfo = info;
            } else {
                result.foundForNewVersion = false;
                result.error = "No version found for target MC version";
            }

            m_migrationResults.append(result);
            m_checksPending--;

            emit migrationProgress(m_totalChecks - m_checksPending, m_totalChecks);

            if (m_checksPending == 0) {
                emit migrationComplete(m_migrationResults);
            }
        });

        // Query for new version
        api.findVersionsForGameVersion(mod.projectId, newLoader, newGameVersion);
    }
}