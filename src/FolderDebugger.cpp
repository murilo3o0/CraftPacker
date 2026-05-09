#include "FolderDebugger.h"
#include "miniz.h"

#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QEventLoop>
#include <QUrlQuery>
#include <QDebug>
#include <cstring>

FolderDebugger::FolderDebugger(QObject *parent) : QObject(parent) {}

void FolderDebugger::cancel() {
    m_cancelled = true;
}

// ============================================================
// SHA-1 Hash a file
// ============================================================
QString FolderDebugger::hashFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return {};

    QCryptographicHash hasher(QCryptographicHash::Sha1);
    const qint64 bufferSize = 64 * 1024;
    while (!file.atEnd() && !m_cancelled) {
        hasher.addData(file.read(bufferSize));
    }
    file.close();
    return hasher.result().toHex().toLower();
}

// ============================================================
// Look up SHA-1 hash on Modrinth API
// ============================================================
QString FolderDebugger::lookupHashOnModrinth(const QString& sha1Hash,
                                               QString& outProjectName) {
    if (sha1Hash.isEmpty() || m_cancelled) return {};

    QNetworkAccessManager manager;
    QEventLoop loop;

    QString urlStr = "https://api.modrinth.com/v2/version_file/" + sha1Hash;
    QUrlQuery query;
    query.addQueryItem("algorithm", "sha1");
    QUrl url(urlStr);
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
        "helloworldx64/CraftPacker/3.0.0");
    req.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = manager.get(req);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return {};
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return {};

    QJsonObject obj = doc.object();
    outProjectName = obj["title"].toString();
    if (outProjectName.isEmpty()) outProjectName = obj["name"].toString();

    QString projectId = obj["project_id"].toString();
    if (projectId.isEmpty()) {
        projectId = obj["id"].toString();
    }

    return projectId;
}

// ============================================================
// Detect loader from JAR metadata
// ============================================================
QString FolderDebugger::detectLoader(const QByteArray& jarData) {
    if (jarData.isEmpty()) return "Unknown";

    if (jarData.contains("fabric.mod.json")) return "Fabric";
    if (jarData.contains("quilt.mod.json")) return "Quilt";
    if (jarData.contains("neoforge.mods.toml")) return "NeoForge";
    if (jarData.contains("META-INF/mods.toml")) return "Forge";

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (mz_zip_reader_init_mem(&zip, jarData.constData(), jarData.size(), 0)) {
        if (mz_zip_reader_locate_file(&zip, "fabric.mod.json", nullptr, 0) >= 0) {
            mz_zip_reader_end(&zip);
            return "Fabric";
        }
        if (mz_zip_reader_locate_file(&zip, "quilt.mod.json", nullptr, 0) >= 0) {
            mz_zip_reader_end(&zip);
            return "Quilt";
        }
        if (mz_zip_reader_locate_file(&zip, "META-INF/neoforge.mods.toml", nullptr, 0) >= 0) {
            mz_zip_reader_end(&zip);
            return "NeoForge";
        }
        if (mz_zip_reader_locate_file(&zip, "META-INF/mods.toml", nullptr, 0) >= 0) {
            mz_zip_reader_end(&zip);
            return "Forge";
        }
        mz_zip_reader_end(&zip);
    }

    return "Unknown";
}

// ============================================================
// Check if a class path should be ignored (shared libraries)
// ============================================================
bool FolderDebugger::shouldIgnoreClassPath(const QString& path) {
    if (!path.endsWith(".class")) return true;

    if (path.startsWith("kotlin/") ||
        path.startsWith("kotlinx/") ||
        path.startsWith("org/spongepowered/asm/") ||
        path.startsWith("org/apache/") ||
        path.startsWith("com/google/") ||
        path.startsWith("javax/") ||
        path.startsWith("javassist/") ||
        path.startsWith("me/shedaniel/cloth/") ||
        path.startsWith("dev/architectury/") ||
        path.startsWith("org/slf4j/") ||
        path.startsWith("com/mojang/") ||  // Mojang interfaces
        path.startsWith("io/netty/") ||
        path.startsWith("it/unimi/dsi/fastutil/") ||
        path.startsWith("META-INF/") ||
        path.contains("module-info") ||
        path.contains("package-info")) {
        return true;
    }

    return false;
}

// ============================================================
// Extract .class filenames from raw JAR bytes (EOCD-based)
// ============================================================
static QStringList extractClassPathsFromJarBytes(const QByteArray& jarData) {
    QStringList classPaths;
    if (jarData.size() < 22) return classPaths;

    static const QByteArray eocdSig("\x50\x4b\x05\x06", 4);
    int eocdPos = -1;
    int searchStart = (jarData.size() < 65557) ? 0 : (jarData.size() - 65557);
    for (int i = jarData.size() - 22; i >= searchStart; i--) {
        if (jarData[i] == 0x50 && jarData[i+1] == 0x4b &&
            jarData[i+2] == 0x05 && jarData[i+3] == 0x06) {
            eocdPos = i;
            break;
        }
    }
    if (eocdPos < 0) return classPaths;

    unsigned int cdOffset = (unsigned char)jarData[eocdPos + 16] |
                           ((unsigned char)jarData[eocdPos + 17] << 8) |
                           ((unsigned char)jarData[eocdPos + 18] << 16) |
                           ((unsigned char)jarData[eocdPos + 19] << 24);
    unsigned int cdSize = (unsigned char)jarData[eocdPos + 12] |
                          ((unsigned char)jarData[eocdPos + 13] << 8) |
                          ((unsigned char)jarData[eocdPos + 14] << 16) |
                          ((unsigned char)jarData[eocdPos + 15] << 24);
    unsigned int numEntries = (unsigned char)jarData[eocdPos + 10] |
                             ((unsigned char)jarData[eocdPos + 11] << 8);

    if (numEntries == 0 || numEntries > 65535) return classPaths;
    if (cdOffset + cdSize > (unsigned int)jarData.size()) return classPaths;

    QSet<QString> seen;
    int cdPos = (int)cdOffset;
    for (unsigned int i = 0; i < numEntries && cdPos + 46 <= jarData.size(); i++) {
        if (jarData[cdPos] != 0x50 || jarData[cdPos+1] != 0x4b ||
            jarData[cdPos+2] != 0x01 || jarData[cdPos+3] != 0x02) {
            break;
        }

        int filenameLen = (unsigned char)jarData[cdPos + 28] |
                         ((unsigned char)jarData[cdPos + 29] << 8);
        int extraLen = (unsigned char)jarData[cdPos + 30] |
                      ((unsigned char)jarData[cdPos + 31] << 8);
        int commentLen = (unsigned char)jarData[cdPos + 32] |
                        ((unsigned char)jarData[cdPos + 33] << 8);

        int totalEntrySize = 46 + filenameLen + extraLen + commentLen;
        if (totalEntrySize < 46 || cdPos + totalEntrySize > jarData.size()) break;

        if (filenameLen > 0 && filenameLen < 512) {
            QString name = QString::fromUtf8(
                jarData.constData() + cdPos + 46, filenameLen);
            name.replace('\\', '/');
            if (name.startsWith('/')) name = name.mid(1);

            if (name.endsWith(".class")) {
                if (!seen.contains(name)) {
                    seen.insert(name);
                    classPaths.append(name);
                }
            }
        }

        cdPos += totalEntrySize;
    }

    return classPaths;
}

// ============================================================
// Scan for class collisions — with DUPLICATE EXCLUSION (Phase 8.1)
// ============================================================
QVector<ClassCollision> FolderDebugger::scanClassCollisions(
    const QHash<QString, QByteArray>& jarDataMap,
    const QSet<QString>& duplicateJarNames)
{
    // Phase 8.1: Group class paths by mod identity first
    // jarName -> list of class paths
    QHash<QString, QSet<QString>> jarClassSets;
    QHash<QString, QStringList> pathToJars;
    int totalClassPaths = 0;
    int ignoredClassPaths = 0;

    for (auto it = jarDataMap.constBegin(); it != jarDataMap.constEnd(); ++it) {
        const QString& jarName = it.key();
        const QByteArray& data = it.value();

        // Skip duplicate jars entirely for class collision purposes
        // Their overlaps will be reported as "DerivedOverlap" findings
        if (duplicateJarNames.contains(jarName)) {
            qDebug() << "[CollisionScan] Skipping duplicate:" << jarName;
            continue;
        }

        QStringList classPaths = extractClassPathsFromJarBytes(data);

        for (const auto& entryPath : classPaths) {
            totalClassPaths++;

            if (shouldIgnoreClassPath(entryPath)) {
                ignoredClassPaths++;
                continue;
            }

            pathToJars[entryPath].append(jarName);
        }
    }

    // Extract collisions (class paths shared by >1 NON-duplicate jar)
    QVector<ClassCollision> collisions;
    for (auto it = pathToJars.constBegin(); it != pathToJars.constEnd(); ++it) {
        if (it.value().size() > 1) {
            QSet<QString> uniqueSet = QSet<QString>(it.value().begin(), it.value().end());
            QStringList uniqueJars = uniqueSet.values();
            if (uniqueJars.size() > 1) {
                ClassCollision cc;
                cc.classPath = it.key();
                cc.conflictingJars = uniqueJars;
                collisions.append(cc);
            }
        }
    }

    std::sort(collisions.begin(), collisions.end(),
        [](const ClassCollision& a, const ClassCollision& b) {
            return a.conflictingJars.size() > b.conflictingJars.size();
        });

    return collisions;
}

// ============================================================
// TRIAGE: Generate human-readable findings (Phase 8.1)
// ============================================================
void FolderDebugger::generateFindings(
    const QVector<JarIdentity>& identities,
    const QHash<QString, QByteArray>& jarDataMap,
    const QVector<ClassCollision>& rawCollisions,
    const QString& dominantLoader)
{
    // Build projectId -> jars map for duplicate detection
    QHash<QString, QVector<const JarIdentity*>> projectIdToJars;
    QSet<QString> duplicateProjectIds;
    for (const auto& id : identities) {
        if (!id.projectId.isEmpty()) {
            projectIdToJars[id.projectId].append(&id);
            if (projectIdToJars[id.projectId].size() > 1) {
                duplicateProjectIds.insert(id.projectId);
            }
        }
    }

    // Build jarName -> JarIdentity map
    QHash<QString, const JarIdentity*> jarNameToIdentity;
    for (const auto& id : identities) {
        jarNameToIdentity[id.jarName] = &id;
    }

    // ================================================================
    // FINDING TYPE A: Duplicate Mod
    // ================================================================
    for (const auto& projectId : duplicateProjectIds) {
        const auto& jars = projectIdToJars[projectId];
        if (jars.size() < 2) continue;

        // Get project name
        QString projName = jars[0]->projectName;
        if (projName.isEmpty()) projName = jars[0]->jarName;

        // Collect all JAR filenames
        QStringList jarNames;
        for (const auto* j : jars) {
            jarNames.append(j->jarName);
        }

        // Calculate how many class paths overlap between duplicates
        // (informational, not used for severity)
        QSet<QString> mergedPaths;
        QSet<QString> overlapPaths;
        for (const auto* j : jars) {
            QSet<QString> currentPaths;
            for (const auto& p : extractClassPathsFromJarBytes(j->jarData)) {
                if (!shouldIgnoreClassPath(p)) {
                    currentPaths.insert(p);
                }
            }
            if (mergedPaths.isEmpty()) {
                mergedPaths = currentPaths;
            } else {
                overlapPaths = mergedPaths & currentPaths;
                mergedPaths = currentPaths;
            }
        }

        DebuggerFinding f;
        f.category = FindingCategory::DuplicateMod;
        f.severity = DebuggerSeverity::Warning;
        f.affectedJars = jarNames;
        f.affectedProjectIds = {projectId};

        f.title = QString("Duplicate Mod: %1").arg(projName);
        f.summary = QString("%1 copies of \"%2\" detected with %3 overlapping classes. "
                            "These appear to be different versions of the same mod.")
                        .arg(jars.size()).arg(projName).arg(overlapPaths.size());
        f.explanation = QString("The following JARs all resolve to Modrinth project \"%1\": %2. "
                                "Their class overlap is expected — they are not two different mods "
                                "that happen to share classes, but duplicates of the same mod.")
                            .arg(projName, jarNames.join(", "));
        f.recommendation = QString("Remove all but one version. Keep the newest or most stable version.");
        f.classPaths = overlapPaths.values();
        emit findingReady(f);
    }

    // ================================================================
    // FINDING TYPE B/C: Real Class Collisions (non-duplicate jars only)
    // ================================================================
    QHash<QPair<QString, QString>, QVector<QString>> pairToPaths; // (jarA, jarB) -> paths
    for (const auto& coll : rawCollisions) {
        QSet<QString> uniqueJars(coll.conflictingJars.begin(), coll.conflictingJars.end());
        QStringList jarList = uniqueJars.values();
        for (int i = 0; i < jarList.size(); i++) {
            for (int j = i + 1; j < jarList.size(); j++) {
                // Only report if jars are from DIFFERENT projects
                auto* idA = jarNameToIdentity.value(jarList[i]);
                auto* idB = jarNameToIdentity.value(jarList[j]);

                if (!idA || !idB) continue;

                // Skip if both are the same project (duplicate-derived overlap)
                if (!idA->projectId.isEmpty() && !idB->projectId.isEmpty() &&
                    idA->projectId == idB->projectId) {
                    continue;
                }

                // Check if we have a direct API dependency/incompatibility link
                bool hasApiLink = false;

                // Use sorted pair as key
                QPair<QString, QString> key =
                    qMakePair(qMin(jarList[i], jarList[j]), qMax(jarList[i], jarList[j]));
                pairToPaths[key].append(coll.classPath);
            }
        }
    }

    // Emit findings for real collisions
    for (auto it = pairToPaths.constBegin(); it != pairToPaths.constEnd(); ++it) {
        const auto& jarA = it.key().first;
        const auto& jarB = it.key().second;
        const auto& paths = it.value();

        DebuggerFinding f;
        f.affectedJars = {jarA, jarB};
        f.classPaths = paths;

        auto* idA = jarNameToIdentity.value(jarA);
        auto* idB = jarNameToIdentity.value(jarB);

        // Determine severity based on identity confidence
        bool idA_confident = idA && !idA->projectId.isEmpty();
        bool idB_confident = idB && !idB->projectId.isEmpty();

        if (idA_confident && idB_confident) {
            // Both known: this is a real collision between different mods
            f.category = FindingCategory::RealClassCollision;
            f.severity = DebuggerSeverity::Critical;

            QString nameA = idA->projectName.isEmpty() ? jarA : idA->projectName;
            QString nameB = idB->projectName.isEmpty() ? jarB : idB->projectName;

            f.title = QString("Class Collision: %1 vs %2").arg(nameA, nameB);
            f.summary = QString("Two different mods (%1 and %2) ship %3 overlapping classes. "
                                "This is a real incompatibility.")
                            .arg(nameA, nameB).arg(paths.size());
            f.explanation = QString("The JARs \"%1\" and \"%2\" both contain the same %3 classes. "
                                    "Since they resolve to different Modrinth projects (%4 and %5), "
                                    "this is a genuine conflict that may cause crashes or undefined behavior.")
                                .arg(jarA, jarB).arg(paths.size())
                                .arg(idA->projectName, idB->projectName);
            f.recommendation = QString("These mods are likely incompatible with each other. "
                                       "Remove one of them or check for a compatibility patch.");
        } else if (!idA_confident && !idB_confident) {
            // Both unknown: suspicious but low confidence
            f.category = FindingCategory::SuspiciousOverlap;
            f.severity = DebuggerSeverity::Warning;

            f.title = QString("Suspicious Class Overlap: %1 vs %2").arg(jarA, jarB);
            f.summary = QString("Two JARs with unknown API identity share %1 classes.").arg(paths.size());
            f.explanation = QString("Both \"%1\" and \"%2\" could not be identified via the Modrinth API. "
                                    "They may be the same mod under different filenames, or genuinely conflicting.")
                                .arg(jarA, jarB);
            f.recommendation = QString("Review these files manually. If they are the same mod, keep one. "
                                       "If they are different mods, they may conflict.");
        } else {
            // One known, one unknown: suspicious
            f.category = FindingCategory::SuspiciousOverlap;
            f.severity = DebuggerSeverity::Warning;

            QString knownName = idA_confident ?
                (idA->projectName.isEmpty() ? jarA : idA->projectName) : jarA;
            QString unknownName = idA_confident ? jarB : jarA;

            f.title = QString("Suspicious Overlap: %1 vs %2").arg(knownName, unknownName);
            f.summary = QString("Known mod \"%1\" shares %2 classes with unidentified JAR \"%3\".")
                            .arg(knownName).arg(paths.size()).arg(unknownName);
            f.explanation = QString("This could be a duplicate copy with a different filename, "
                                    "or a genuinely conflicting mod that isn't on Modrinth.");
            f.recommendation = QString("Check if \"%1\" is a renamed copy of \"%2\". "
                                       "If so, remove the duplicate.").arg(unknownName, knownName);
        }

        emit findingReady(f);
    }

    // ================================================================
    // FINDING TYPE D: Loader Mismatch
    // ================================================================
    for (const auto& id : identities) {
        if (id.detectedLoader != "Unknown" && id.detectedLoader != dominantLoader &&
            dominantLoader != "Mixed" && id.detectedLoader != "Quilt") {
            DebuggerFinding f;
            f.category = FindingCategory::LoaderMismatch;
            f.severity = DebuggerSeverity::Critical;
            f.affectedJars = {id.jarName};
            f.title = QString("Loader Mismatch: %1").arg(id.jarName);
            f.summary = QString("Pack targets \"%1\" but \"%2\" is a \"%3\" mod.")
                            .arg(dominantLoader, id.jarName, id.detectedLoader);
            f.explanation = QString("Mod loader types (Fabric, Forge, NeoForge) are incompatible. "
                                    "A mod built for one loader cannot run under a different one.");
            f.recommendation = QString("Remove this mod or choose the correct version for %1.")
                                .arg(dominantLoader);
            emit findingReady(f);
        }
    }

    // ================================================================
    // FINDING TYPE E: Missing Fabric API
    // ================================================================
    int fabricMods = 0;
    bool hasFabricApi = false;
    for (const auto& id : identities) {
        if (id.detectedLoader == "Fabric") fabricMods++;
        if (id.projectName.contains("Fabric API", Qt::CaseInsensitive) ||
            id.projectId == "fabric-api") {
            hasFabricApi = true;
        }
    }
    if (fabricMods >= 3 && !hasFabricApi) {
        DebuggerFinding f;
        f.category = FindingCategory::MissingDependency;
        f.severity = DebuggerSeverity::Warning;
        f.title = "Missing Fabric API";
        f.summary = QString("This pack contains %1 Fabric mods but is missing Fabric API.")
                        .arg(fabricMods);
        f.explanation = "Fabric API is a required library for most Fabric mods. "
                        "Without it, many mods will fail to load or cause errors.";
        f.recommendation = "Search for and add \"Fabric API\" to your mod list.";
        emit findingReady(f);
    }

    // ================================================================
    // FINDING TYPE F: Informational — not on API
    // ================================================================
    for (const auto& id : identities) {
        if (id.projectId.isEmpty()) {
            DebuggerFinding f;
            f.category = FindingCategory::Informational;
            f.severity = DebuggerSeverity::Info;
            f.affectedJars = {id.jarName};
            f.title = QString("Not on Modrinth: %1").arg(id.jarName);
            f.summary = QString("\"%1\" could not be identified via the Modrinth API.").arg(id.jarName);
            f.explanation = "This may be a mod from CurseForge, a private/local mod, "
                            "or a file that has been removed from Modrinth.";
            f.recommendation = "Check manually. If it's a known mod, search for it on CurseForge.";
            emit findingReady(f);
        }
    }
}

// ============================================================
// Compute verdict text (Phase 8.1)
// ============================================================
QString FolderDebugger::computeVerdict(int critical, int warning, int info) {
    if (critical > 0) {
        return "Pack has serious incompatibilities — review required";
    }
    if (warning > 0) {
        return "Likely safe after addressing warnings";
    }
    if (info > 0) {
        return "Mostly clean with informational notes";
    }
    return "Pack appears clean — no issues detected";
}

// ============================================================
// MAIN ENTRY: debugFolder() — runs in background thread
// ============================================================
void FolderDebugger::debugFolder(const QString& folderPath) {
    m_cancelled = false;
    m_results.clear();
    m_projectIdCounts.clear();
    m_fabricCount = 0;
    m_forgeCount = 0;
    m_neoforgeCount = 0;
    m_quiltCount = 0;

    // Step 1: Collect all .jar files
    QStringList jarFiles;
    QDirIterator it(folderPath, {"*.jar"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        jarFiles.append(it.filePath());
    }

    int total = jarFiles.size();
    if (total == 0) {
        emit scanError("No .jar files found in the selected folder.");
        return;
    }

    emit scanProgress(0, total, "Scanning...");

    // Phase 8.1: Collect identities for triage
    QVector<JarIdentity> identities;
    QSet<QString> duplicateJarNames;
    QHash<QString, QByteArray> jarDataMap;

    // Step 2: Process each jar
    for (int i = 0; i < total; ++i) {
        if (m_cancelled) {
            emit scanError("Scan cancelled by user.");
            return;
        }

        const QString& jarPath = jarFiles[i];
        QFileInfo fi(jarPath);
        QString jarName = fi.fileName();

        emit scanProgress(i + 1, total, jarName);

        DebuggerModResult result;
        result.jarFileName = jarName;
        result.jarFilePath = jarPath;
        result.fileSize = fi.size();

        QFile file(jarPath);
        if (!file.open(QIODevice::ReadOnly)) {
            result.detectedLoader = "Unknown";
            result.reasonText = "Cannot read file";
            result.issues |= DebuggerModResult::ApiNotFound;
            m_results.append(result);
            emit modResultReady(result);
            continue;
        }
        QByteArray jarData = file.readAll();
        file.close();

        jarDataMap[jarName] = jarData;

        QString sha1 = hashFile(jarPath);

        QString projectName;
        QString projectId = lookupHashOnModrinth(sha1, projectName);
        if (!projectId.isEmpty()) {
            result.apiProjectId = projectId;
            result.apiProjectName = projectName;
            m_projectIdCounts[projectId]++;
            result.duplicateCount = m_projectIdCounts[projectId];
            if (m_projectIdCounts[projectId] > 1) {
                duplicateJarNames.insert(jarName);
            }
        } else {
            result.issues |= DebuggerModResult::ApiNotFound;
            result.reasonText = "Not found on Modrinth API";
        }

        result.detectedLoader = detectLoader(jarData);
        if (result.detectedLoader == "Fabric") m_fabricCount++;
        else if (result.detectedLoader == "Forge") m_forgeCount++;
        else if (result.detectedLoader == "NeoForge") m_neoforgeCount++;
        else if (result.detectedLoader == "Quilt") m_quiltCount++;

        // Build identity
        JarIdentity identity;
        identity.jarName = jarName;
        identity.jarPath = jarPath;
        identity.projectId = projectId;
        identity.projectName = projectName;
        identity.detectedLoader = result.detectedLoader;
        identity.fileSize = fi.size();
        identity.jarData = jarData;
        identity.isDuplicate = (m_projectIdCounts.value(projectId, 0) > 1);
        identities.append(identity);

        m_results.append(result);
        emit modResultReady(result);
    }

    // Step 3: Determine dominant loader
    int maxCount = m_fabricCount;
    if (m_forgeCount > maxCount) maxCount = m_forgeCount;
    if (m_neoforgeCount > maxCount) maxCount = m_neoforgeCount;
    if (m_quiltCount > maxCount) maxCount = m_quiltCount;
    if (m_fabricCount == maxCount && maxCount > 0) m_loaderMode = "Fabric";
    else if (m_forgeCount == maxCount && maxCount > 0) m_loaderMode = "Forge";
    else if (m_neoforgeCount == maxCount && maxCount > 0) m_loaderMode = "NeoForge";
    else if (m_quiltCount == maxCount && maxCount > 0) m_loaderMode = "Quilt";
    else m_loaderMode = "Mixed";

    // Step 4: Class collision scan (WITH duplicate exclusion)
    QVector<ClassCollision> collisions = scanClassCollisions(jarDataMap, duplicateJarNames);
    for (const auto& coll : collisions) {
        emit collisionFound(coll);
    }

    // Step 5: TRIAGE — generate human-readable findings (Phase 8.1)
    // This replaces the raw collision dump with grouped, meaningful findings
    generateFindings(identities, jarDataMap, collisions, m_loaderMode);

    // Step 6: Compute severity counts and verdict
    int criticalCount = 0, warningCount = 0, infoCount = 0;

    // Count from legacy issues
    for (const auto& r : m_results) {
        if (r.issues == DebuggerModResult::ApiNotFound) {
            infoCount++;
        } else if (r.issues & (DebuggerModResult::DuplicateMod |
                               DebuggerModResult::LoaderMismatch |
                               DebuggerModResult::ClassCollision)) {
            criticalCount++;
        } else if (r.issues & DebuggerModResult::MissingApi) {
            warningCount++;
        }
    }

    // Count non-duplicate collisions as critical
    for (const auto& coll : collisions) {
        // Don't double-count — if these jars are from different projects, it's critical
        criticalCount++;
    }

    QString verdict = computeVerdict(criticalCount, warningCount, infoCount);

    // Emit summary
    DebuggerSummary summary;
    summary.totalJars = total;
    summary.fabricCount = m_fabricCount;
    summary.forgeCount = m_forgeCount;
    summary.neoforgeCount = m_neoforgeCount;
    summary.quiltCount = m_quiltCount;
    summary.unknownCount = total - m_fabricCount - m_forgeCount - m_neoforgeCount - m_quiltCount;
    summary.dominantLoader = m_loaderMode;
    summary.duplicateCount = 0;
    for (auto it = m_projectIdCounts.constBegin(); it != m_projectIdCounts.constEnd(); ++it) {
        if (it.value() > 1) summary.duplicateCount++;
    }
    summary.collisionCount = collisions.size();
    summary.issueCount = criticalCount + warningCount;
    summary.criticalCount = criticalCount;
    summary.warningCount = warningCount;
    summary.infoCount = infoCount;
    summary.verdictText = verdict;
    summary.verdictDetail = (criticalCount > 0) ?
        QString("Critical: %1, Warning: %2, Info: %3")
            .arg(criticalCount).arg(warningCount).arg(infoCount) :
        (warningCount > 0) ?
            QString("No critical issues. Warnings: %1, Info: %2")
                .arg(warningCount).arg(infoCount) :
        QString("All clear. Info: %1").arg(infoCount);

    emit summaryReady(summary);
    emit scanFinished();
}