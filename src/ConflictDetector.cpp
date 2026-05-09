#include "ConflictDetector.h"
#include "StringCleaner.h"
#include <QMap>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

ConflictDetector& ConflictDetector::instance() {
    static ConflictDetector inst;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        inst.loadKnownConflicts();
    }
    return inst;
}

void ConflictDetector::loadKnownConflicts() {
    // Phase 8.1: Load known_compatibles allowlist to prevent false positives
    // for mods like Fabric API + Cloth Config + Architectury API

    auto loadFromDoc = [this](const QJsonDocument& doc) {
        if (!doc.isObject()) return false;
        QJsonObject root = doc.object();
        
        // Load conflict groups
        if (root.contains("conflicts")) {
            QJsonArray conflicts = root["conflicts"].toArray();
            for (const auto& conflictVal : conflicts) {
                QJsonObject conflictObj = conflictVal.toObject();
                QJsonArray mods = conflictObj["mods"].toArray();
                QVector<QString> orderedGroup;
                for (const auto& modVal : mods) {
                    orderedGroup.append(StringCleaner::normalizeForConflict(modVal.toString()));
                }
                QSet<QString> normalizedNames;
                for (const auto& modVal : mods) {
                    QString rawName = modVal.toString();
                    normalizedNames.insert(StringCleaner::normalizeForConflict(rawName));
                }
                m_knownConflictGroups.append({
                    normalizedNames, orderedGroup,
                    conflictObj["reason"].toString(),
                    conflictObj["severity"].toString()
                });
            }
        }
        
        // Phase 8.1: Load compatible groups (allowlist)
        if (root.contains("known_compatibles")) {
            QJsonArray compatibles = root["known_compatibles"].toArray();
            for (const auto& compatVal : compatibles) {
                QJsonObject compatObj = compatVal.toObject();
                QJsonArray mods = compatObj["mods"].toArray();
                QSet<QString> normalizedNames;
                for (const auto& modVal : mods) {
                    QString rawName = modVal.toString();
                    normalizedNames.insert(StringCleaner::normalizeForConflict(rawName));
                }
                m_knownCompatibleGroups.append({
                    normalizedNames,
                    compatObj["reason"].toString()
                });
            }
            qDebug() << "[ConflictDetector] Loaded" << m_knownCompatibleGroups.size()
                     << "compatible groups";
        }
        
        return true;
    };

    // Try Qt resource system first (for standalone .exe)
    QFile resFile(":/known_conflicts.json");
    if (resFile.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(resFile.readAll());
        resFile.close();
        if (loadFromDoc(doc)) {
            qDebug().noquote() << "[ConflictDetector] Loaded" << m_knownConflictGroups.size()
                               << "conflict groups from Qt resource";
            return;
        }
    }

    // Fallback: try filesystem paths
    QStringList searchPaths;
    searchPaths << QDir::currentPath() + "/resources";
    searchPaths << QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    searchPaths << QDir::currentPath();
    
    for (const auto& dirPath : searchPaths) {
        QDir dir(dirPath);
        QString filePath = dir.absoluteFilePath("known_conflicts.json");
        QString resourcesPath = dirPath + "/resources/known_conflicts.json";
        if (QFile::exists(resourcesPath)) filePath = resourcesPath;

        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            file.close();
            if (doc.isObject() && doc.object().contains("conflicts")) {
                QJsonArray conflicts = doc.object()["conflicts"].toArray();
                for (const auto& conflictVal : conflicts) {
                    QJsonObject conflictObj = conflictVal.toObject();
                    QJsonArray mods = conflictObj["mods"].toArray();
                    QVector<QString> orderedGroup;
                    for (const auto& modVal : mods) {
                        orderedGroup.append(StringCleaner::normalizeForConflict(modVal.toString()));
                    }
                    QSet<QString> normalizedNames;
                    for (const auto& modVal : mods) {
                        QString rawName = modVal.toString();
                        for (const auto& form : StringCleaner::allNormalizedForms(rawName)) {
                            normalizedNames.insert(form);
                        }
                        normalizedNames.insert(StringCleaner::normalizeForConflict(rawName));
                    }
                    m_knownConflictGroups.append({
                        normalizedNames, orderedGroup,
                        conflictObj["reason"].toString(),
                        conflictObj["severity"].toString()
                    });
                }
                qDebug().noquote() << "[ConflictDetector] Loaded" << m_knownConflictGroups.size()
                                   << "conflict groups from" << filePath;
                return;
            }
        }
    }
    
    qDebug() << "[ConflictDetector] No known_conflicts.json found, using API-only detection";
}

// ============================================================
// Phase 10.0: Build dependency link graph from resolved mods
// Returns a set of (projectIdA, projectIdB) pairs that are
// linked by required or optional dependencies.
// ============================================================
QSet<QPair<QString, QString>> ConflictDetector::buildDependencyLinks(const QVector<ModInfo>& mods) {
    QSet<QPair<QString, QString>> links;
    QMap<QString, const ModInfo*> modMap;
    for (const auto& m : mods) {
        modMap[m.projectId] = &m;
    }
    
    for (const auto& m : mods) {
        auto deps = parseDependencies(m.dependencies);
        for (const auto& dep : deps) {
            if (dep.dependencyType == "required" || dep.dependencyType == "optional") {
                // Check if the dependency is in our mod set
                if (modMap.contains(dep.projectId)) {
                    QString a = m.projectId;
                    QString b = dep.projectId;
                    if (a > b) std::swap(a, b);
                    links.insert(qMakePair(a, b));
                    qDebug() << "[ConflictDetector:DependencyLink]"
                             << m.name << "(" << m.projectId << ")"
                             << dep.dependencyType.toStdString().c_str()
                             << "depends on"
                             << modMap[dep.projectId]->name << "(" << dep.projectId << ")";
                }
                // Also try slug matching for dependencies not directly in the mod set
                // (e.g., when dependency uses project slug instead of ID)
                bool foundBySlug = false;
                for (const auto& mod : mods) {
                    QString normSlug = StringCleaner::normalizeForConflict(mod.slug);
                    QString normDepId = StringCleaner::normalizeForConflict(dep.projectId);
                    if (normSlug == normDepId && !modMap.contains(dep.projectId)) {
                        QString a = m.projectId;
                        QString b = mod.projectId;
                        if (a > b) std::swap(a, b);
                        links.insert(qMakePair(a, b));
                        qDebug() << "[ConflictDetector:DependencyLink (slug)]"
                                 << m.name << "depends on" << mod.name;
                        foundBySlug = true;
                        break;
                    }
                }
                if (!foundBySlug) {
                    qDebug() << "[ConflictDetector:DependencyLink] Skipped external dep:"
                             << dep.projectId << "(not in mod set)";
                }
            }
            if (dep.dependencyType == "incompatible") {
                qDebug() << "[ConflictDetector:IncompatibleDep]"
                         << m.name << "is incompatible with" << dep.projectId;
            }
        }
    }
    
    qDebug() << "[ConflictDetector] Built" << links.size() << "dependency links";
    return links;
}

QSet<QPair<QString, QString>> ConflictDetector::getIncompatiblePairs(const QVector<ModInfo>& mods, const QVector<QString>& rawInputNames) {
    QSet<QPair<QString, QString>> incompatible;

    // Build a lookup map: projectId -> ModInfo
    QMap<QString, const ModInfo*> modMap;
    QMap<QString, const ModInfo*> nameMap;
    QMap<QString, const ModInfo*> slugMap;
    for (const auto& m : mods) {
        modMap[m.projectId] = &m;
        for (const auto& form : StringCleaner::allNormalizedForms(m.name)) {
            nameMap[form] = &m;
        }
        QString normSlug = StringCleaner::normalizeForConflict(m.slug);
        nameMap[normSlug] = &m;
        slugMap[normSlug] = &m;
    }

    // Phase 10.0: Build dependency links BEFORE checking conflicts
    // This ensures we suppress false positives for mods that are designed to work together
    auto depLinks = buildDependencyLinks(mods);

    // Build name → projectId mapping from the known_conflicts JSON database
    QMap<QString, QSet<QString>> knownNameToProjectIds;
    for (const auto& group : m_knownConflictGroups) {
        for (const auto& normName : group.mods) {
            knownNameToProjectIds[normName].insert(normName);
        }
    }

    // Pre-compute normalized raw input names for matching
    QSet<QString> normalizedRawNames;
    for (const auto& raw : rawInputNames) {
        QString stripped = StringCleaner::sanitizeModName(raw);
        for (const auto& form : StringCleaner::allNormalizedForms(raw)) {
            normalizedRawNames.insert(form);
        }
        normalizedRawNames.insert(StringCleaner::normalizeForConflict(stripped));
        QStringList words = raw.toLower().split(QRegularExpression(R"(\s+)"), Qt::SkipEmptyParts);
        for (const auto& w : words) {
            QString nw = StringCleaner::normalizeForConflict(w);
            if (nw.length() >= 2) normalizedRawNames.insert(nw);
        }
    }

    // Helper: check if a pair is dependency-linked (suppresses conflict)
    auto isDependencyLinked = [&](const QString& a, const QString& b) -> bool {
        QString x = a, y = b;
        if (x > y) std::swap(x, y);
        return depLinks.contains(qMakePair(x, y));
    };

    // Check each mod's dependencies for "incompatible" entries (API-level)
    for (const auto& mod : mods) {
        auto deps = parseDependencies(mod.dependencies);
        for (const auto& dep : deps) {
            if (dep.dependencyType == "incompatible") {
                if (modMap.contains(dep.projectId)) {
                    QString a = mod.projectId;
                    QString b = dep.projectId;
                    // Phase 10.0: Skip if dependency-linked (soft incompatible from API)
                    if (isDependencyLinked(a, b)) {
                        qDebug() << "[ConflictDetector] Suppressed API incompatible (dependency-linked):"
                                 << mod.name << "vs" << modMap[dep.projectId]->name;
                        continue;
                    }
                    if (a > b) std::swap(a, b);
                    incompatible.insert(qMakePair(a, b));
                } else if (slugMap.contains(dep.projectId)) {
                    QString a = mod.projectId;
                    QString b = slugMap[dep.projectId]->projectId;
                    if (isDependencyLinked(a, b)) {
                        qDebug() << "[ConflictDetector] Suppressed API incompatible (dependency-linked slug):"
                                 << mod.name << "vs" << slugMap[dep.projectId]->name;
                        continue;
                    }
                    if (a > b) std::swap(a, b);
                    incompatible.insert(qMakePair(a, b));
                } else {
                    QString normDepId = StringCleaner::normalizeForConflict(dep.projectId);
                    bool foundByName = false;
                    if (knownNameToProjectIds.contains(normDepId)) {
                        for (const auto& knownName : knownNameToProjectIds[normDepId]) {
                            if (normalizedRawNames.contains(knownName)) {
                                foundByName = true;
                                break;
                            }
                        }
                    }
                    if (!foundByName) {
                        if (normalizedRawNames.contains(normDepId)) {
                            foundByName = true;
                        }
                    }
                    if (foundByName) {
                        incompatible.insert(qMakePair(mod.projectId, mod.projectId));
                    }
                }
            }
        }
    }

    // Check local known_conflicts.json database
    for (const auto& group : m_knownConflictGroups) {
        QVector<const ModInfo*> matchedMods;
        QSet<QString> matchedIds;

        for (const auto& m : mods) {
            bool matched = false;
            if (!m.slug.isEmpty()) {
                QString normSlug = StringCleaner::normalizeForConflict(m.slug);
                if (group.mods.contains(normSlug)) {
                    matched = true;
                }
            }
            if (!matched && m.slug.isEmpty()) {
                QString normName = StringCleaner::normalizeForConflict(m.name);
                if (group.mods.contains(normName)) {
                    matched = true;
                }
            }
            
            if (matched && !matchedIds.contains(m.projectId)) {
                matchedMods.append(&m);
                matchedIds.insert(m.projectId);
            }
        }
        
        if (matchedMods.size() < 2) continue;

        // Phase 10.0: Filter out dependency-linked pairs
        // Example: Sodium + Sodium Extra — if Sodium Extra depends on Sodium,
        // they should NOT conflict even if both match a group
        QVector<QPair<QString, QString>> pairsToCheck;
        if (group.orderedNorm.size() >= 3) {
            const QString& firstNorm = group.orderedNorm.first();
            QVector<int> hubIndices;
            QVector<int> spokeIndices;
            
            for (int i = 0; i < matchedMods.size(); i++) {
                const auto& m = matchedMods[i];
                bool isHub = false;
                for (const auto& form : StringCleaner::allNormalizedForms(m->name)) {
                    if (form == firstNorm) {
                        isHub = true;
                        break;
                    }
                }
                if (!isHub && !m->slug.isEmpty()) {
                    if (StringCleaner::normalizeForConflict(m->slug) == firstNorm) {
                        isHub = true;
                    }
                }
                if (isHub) hubIndices.append(i);
                else spokeIndices.append(i);
            }
            
            for (int hIdx : hubIndices) {
                for (int sIdx : spokeIndices) {
                    QString a = matchedMods[hIdx]->projectId;
                    QString b = matchedMods[sIdx]->projectId;
                    if (a > b) std::swap(a, b);
                    pairsToCheck.append(qMakePair(a, b));
                }
            }
        } else {
            for (int i = 0; i < matchedMods.size(); i++) {
                for (int j = i + 1; j < matchedMods.size(); j++) {
                    QString a = matchedMods[i]->projectId;
                    QString b = matchedMods[j]->projectId;
                    if (a > b) std::swap(a, b);
                    pairsToCheck.append(qMakePair(a, b));
                }
            }
        }
        
        // Phase 10.0: Apply dependency-aware suppression
        for (const auto& pair : pairsToCheck) {
            if (isDependencyLinked(pair.first, pair.second)) {
                QString nameA = modMap.contains(pair.first) ? modMap[pair.first]->name : pair.first;
                QString nameB = modMap.contains(pair.second) ? modMap[pair.second]->name : pair.second;
                qDebug() << "[ConflictDetector] Suppressed group conflict (dependency-linked):"
                         << nameA << "vs" << nameB << "in group" << group.reason;
                continue;
            }
            incompatible.insert(pair);
        }
    }

    return incompatible;
}

QVector<ConflictWarning> ConflictDetector::detectConflicts(const QVector<ModInfo>& mods) {
    QVector<ConflictWarning> warnings;
    auto incompatiblePairs = getIncompatiblePairs(mods);

    // Build name lookup
    QMap<QString, QString> idToName;
    for (const auto& m : mods) {
        idToName[m.projectId] = m.name;
    }

    auto findReason = [&](const QString& projA, const QString& projB) -> QString {
        QString nameA, nameB;
        for (const auto& m : mods) {
            if (m.projectId == projA) nameA = m.name;
            if (m.projectId == projB) nameB = m.name;
        }
        
        QStringList formsA = StringCleaner::allNormalizedForms(nameA);
        formsA.append(StringCleaner::normalizeForConflict(nameA));
        QStringList formsB = StringCleaner::allNormalizedForms(nameB);
        formsB.append(StringCleaner::normalizeForConflict(nameB));
        
        for (const auto& group : m_knownConflictGroups) {
            bool hasA = false, hasB = false;
            for (const auto& f : formsA) { if (group.mods.contains(f)) { hasA = true; break; } }
            for (const auto& f : formsB) { if (group.mods.contains(f)) { hasB = true; break; } }
            if (hasA && hasB) {
                return group.reason;
            }
        }
        return "Modrinth API reports this mod is incompatible";
    };

    // Phase 8.1: Filter out false positives using the known_compatibles allowlist
    auto isKnownCompatible = [&](const ConflictWarning& cw) -> bool {
        QString normA = StringCleaner::normalizeForConflict(cw.modA);
        QString normB = StringCleaner::normalizeForConflict(cw.modB);
        if (normA.isEmpty() || normB.isEmpty()) return false;
        
        for (const auto& compatGroup : m_knownCompatibleGroups) {
            if (compatGroup.mods.contains(normA) && compatGroup.mods.contains(normB)) {
                qDebug() << "[ConflictDetector] Suppressed false conflict between"
                         << cw.modA << "and" << cw.modB
                         << "Reason:" << compatGroup.reason;
                return true;
            }
        }
        return false;
    };

    for (const auto& pair : incompatiblePairs) {
        ConflictWarning w;
        w.modA = idToName.value(pair.first, pair.first);
        w.modB = idToName.value(pair.second, pair.second);
        w.reason = findReason(pair.first, pair.second);
        
        // Skip if this pair is known to be compatible
        if (isKnownCompatible(w)) continue;
        
        // Skip self-conflicts (modA == modB) unless it's an API mismatch
        if (w.modA == w.modB && !w.reason.contains("loader", Qt::CaseInsensitive)) continue;
        
        warnings.append(w);
    }

    // Check for missing required dependencies
    auto missing = findMissingDependencies(mods);
    for (const auto& miss : missing) {
        ConflictWarning w;
        w.modA = miss;
        w.modB = "";
        w.reason = "missing_dependency";
        warnings.append(w);
    }

    return warnings;
}

auto ConflictDetector::parseDependencies(const QJsonArray& deps) const -> QVector<Dependency> {
    QVector<Dependency> result;
    for (const auto& depVal : deps) {
        QJsonObject dep = depVal.toObject();
        Dependency d;
        d.projectId = dep["project_id"].toString();
        d.dependencyType = dep["dependency_type"].toString();
        d.versionId = dep["version_id"].toString();
        result.append(d);
    }
    return result;
}

QStringList ConflictDetector::findDuplicates(const QVector<ModInfo>& mods) {
    QStringList duplicates;
    QMap<QString, int> slugCount;
    QMap<QString, QString> slugName;

    for (const auto& m : mods) {
        QString key = m.slug.isEmpty() ? m.name.toLower() : m.slug.toLower();
        slugCount[key]++;
        slugName[key] = m.name;
    }

    for (auto it = slugCount.begin(); it != slugCount.end(); ++it) {
        if (it.value() > 1) {
            duplicates.append(slugName[it.key()]);
        }
    }

    return duplicates;
}

QVector<QString> ConflictDetector::findMissingDependencies(const QVector<ModInfo>& mods) {
    QVector<QString> missing;
    QSet<QString> availableIds;

    for (const auto& m : mods) {
        availableIds.insert(m.projectId);
    }

    for (const auto& mod : mods) {
        auto deps = parseDependencies(mod.dependencies);
        for (const auto& dep : deps) {
            if (dep.dependencyType == "required") {
                if (!availableIds.contains(dep.projectId)) {
                    missing.append(QString("'%1' requires: %2").arg(mod.name, dep.projectId));
                }
            }
        }
    }

    return missing;
}

ServerSplitResult ConflictDetector::splitForServer(const QVector<ModInfo>& mods) {
    ServerSplitResult result;

    for (const auto& m : mods) {
        bool clientRequired = (m.clientSide == "required");
        bool clientUnsupported = (m.clientSide == "unsupported");
        bool serverRequired = (m.serverSide == "required");
        bool serverUnsupported = (m.serverSide == "unsupported");

        if (clientRequired && serverUnsupported) {
            result.clientOnlyMods.append(m);
            result.warnings.append(QString("%1 is client-only (shaders, minimaps, etc.)").arg(m.name));
        }
        else if (serverRequired && clientUnsupported) {
            result.serverMods.append(m);
            result.warnings.append(QString("%1 is server-only").arg(m.name));
        }
        else if (!clientUnsupported && !serverUnsupported) {
            result.bothMods.append(m);
        }
        else {
            result.bothMods.append(m);
        }
    }

    return result;
}