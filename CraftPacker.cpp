#include "CraftPacker.h"
#include "src/ThemeManager.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProgressBar>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextEdit>
#include <QThread>
#include <QThreadPool>
#include <QTabWidget>
#include <QTimer>
#include <QUrlQuery>
#include <QSplitter>
#include <QSettings>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QMenuBar>
#include <QMenu>
#include <QDirIterator>
#include <QDebug>
#include <QDate>
#include <QLabel>
#include <QFileInfo>
#include <cstring>

#include "src/miniz.h"
#include "src/DebuggerDashboard.h"
#include "src/JarMetadataExtractor.h"

// ============================================================
// RateLimiter
// ============================================================
RateLimiter::RateLimiter(int callsPerMinute)
    : m_minInterval(60000 / callsPerMinute)
{
    m_lastCall = std::chrono::steady_clock::now() - m_minInterval;
}

void RateLimiter::wait() {
    QMutexLocker locker(&m_mutex);
    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - m_lastCall;
    if (elapsed < m_minInterval) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            m_minInterval - elapsed).count();
        QThread::msleep(ms);
    }
    m_lastCall = std::chrono::steady_clock::now();
}

// ============================================================
// DownloadWorker
// ============================================================
DownloadWorker::DownloadWorker(ModInfo mod, QString dir)
    : m_modInfo(std::move(mod)), m_downloadDir(std::move(dir)) {}

void DownloadWorker::process() {
    QString iid = m_modInfo.isDependency ? m_modInfo.projectId : m_modInfo.originalQuery;
    QNetworkAccessManager manager;
    QEventLoop loop;

    QNetworkRequest request(m_modInfo.downloadUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader,
        "helloworldx64/CraftPacker/3.0.0");

    QNetworkReply *reply = manager.get(request);
    if (!reply) { emit finished(iid, "Network Error"); return; }

    QString filePath = m_downloadDir + "/" + m_modInfo.filename;
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit finished(iid, "File Error");
        reply->abort();
        return;
    }

    QObject::connect(reply, &QNetworkReply::downloadProgress,
        [&](qint64 r, qint64 t) { emit progress(iid, r, t); });
    QObject::connect(reply, &QNetworkReply::readyRead, [&]() {
        if (file.isOpen()) file.write(reply->readAll());
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    file.close();
    QString errorStr = (reply->error() != QNetworkReply::NoError)
        ? reply->errorString() : "";
    if (!errorStr.isEmpty()) QFile::remove(filePath);
    reply->deleteLater();
    emit finished(iid, errorStr);
}

// ============================================================
// ImageSearchWorker
// ============================================================
ImageSearchWorker::ImageSearchWorker(QString modName, QString projectId)
    : m_modName(std::move(modName)), m_projectId(std::move(projectId)) {}

void ImageSearchWorker::process() {
    QNetworkAccessManager manager;
    QEventLoop loop;

    QString query = m_modName + " mod minecraft icon";
    QUrl url("https://duckduckgo.com/i.js");
    QUrlQuery urlQuery;
    urlQuery.addQueryItem("l", "us-en");
    urlQuery.addQueryItem("o", "json");
    urlQuery.addQueryItem("q", query);
    url.setQuery(urlQuery);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0");
    QNetworkReply *reply = manager.get(request);

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonArray results = doc.object()["results"].toArray();
        if (!results.isEmpty()) {
            QString imageUrl = results[0].toObject()["image"].toString();
            QNetworkReply *imgReply = manager.get(QNetworkRequest(QUrl(imageUrl)));
            QObject::connect(imgReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            loop.exec();

            if (imgReply->error() == QNetworkReply::NoError) {
                QPixmap pixmap;
                pixmap.loadFromData(imgReply->readAll());
                emit imageFound(m_projectId, pixmap);
            } else {
                emit imageFound(m_projectId, QPixmap());
            }
            imgReply->deleteLater();
        } else {
            emit imageFound(m_projectId, QPixmap());
        }
    } else {
        emit imageFound(m_projectId, QPixmap());
    }
    reply->deleteLater();
}

// ============================================================
// CraftPacker Constructor / Destructor
// ============================================================
CraftPacker::CraftPacker(QWidget *parent)
    : QMainWindow(parent)
{
    m_settings = new QSettings(this);
    setupUi();
    applySettings();

    qRegisterMetaType<ModInfo>();
    qRegisterMetaType<QList<ModInfo>>();
    qRegisterMetaType<qint64>();
    qRegisterMetaType<QVector<UpdateStatus>>();
    qRegisterMetaType<UpdateStatus::Status>();

    // Cross-thread signal relay — signal names DIFFER from slot names to avoid MOC crashes
    connect(this, &CraftPacker::sigModFound, this, &CraftPacker::onModFound, Qt::QueuedConnection);
    connect(this, &CraftPacker::sigModNotFound, this, &CraftPacker::onModNotFound, Qt::QueuedConnection);
    connect(this, &CraftPacker::sigSearchFinished, this, &CraftPacker::onSearchFinished, Qt::QueuedConnection);
    connect(this, &CraftPacker::sigUpdateStatusBar, this, &CraftPacker::updateStatusBar, Qt::QueuedConnection);
    connect(this, &CraftPacker::sigAllDownloadsFinished, this, &CraftPacker::onAllDownloadsFinished, Qt::QueuedConnection);
    connect(this, &CraftPacker::sigDependencyResolutionFinished, this, &CraftPacker::onDependencyResolutionFinished, Qt::QueuedConnection);
    connect(this, &CraftPacker::sigImageFound, this, &CraftPacker::onImageFound, Qt::QueuedConnection);

    // Fade-in animation
    auto *animation = new QPropertyAnimation(this, "windowOpacity");
    animation->setDuration(400);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setEasingCurve(QEasingCurve::InQuad);
    animation->start(QAbstractAnimation::DeleteWhenStopped);

    m_profilePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(m_profilePath);
    loadProfileList();
    setAcceptDrops(true);
}

CraftPacker::~CraftPacker() = default;

// ============================================================
// UI Setup — SINGLE unified results table with QSplitter
// ============================================================
void CraftPacker::setupUi() {
    setWindowTitle("CraftPacker v3 - Modpack Creation Studio");
    setMinimumSize(1400, 850);
    setObjectName("mainWindow");

    // Menu bar
    QMenuBar* menuBar = new QMenuBar();
    QMenu* fileMenu = menuBar->addMenu("&File");
    fileMenu->addAction("Import from Folder...", this, &CraftPacker::importFromFolder);
    fileMenu->addSeparator();
    QMenu* exportMenu = fileMenu->addMenu("&Export Pack");
    exportMenu->addAction("Export as .mrpack...", this, &CraftPacker::exportToMrpack);
    exportMenu->addAction("Export as CurseForge .zip...", this, &CraftPacker::exportToCurseForge);
    exportMenu->addAction("Generate Server Pack...", this, &CraftPacker::generateServerPack);
    fileMenu->addSeparator();
    fileMenu->addAction("&Settings...", this, &CraftPacker::openSettingsDialog);
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", qApp, &QApplication::quit);

    QMenu* toolsMenu = menuBar->addMenu("&Tools");
    toolsMenu->addAction("Debug Local Folder...", this, &CraftPacker::openDebugger);
    toolsMenu->addSeparator();
    toolsMenu->addAction("Check for &Updates", this, &CraftPacker::checkForUpdates);
    toolsMenu->addAction("&Migrate Profile...", this, &CraftPacker::migrateProfile);

    QMenu* helpMenu = menuBar->addMenu("&Help");
    helpMenu->addAction("Export Search Results Debug...", this, [this]() {
        QString savePath = QFileDialog::getSaveFileName(this,
            "Save Search Debug Report",
            QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + "/craftpacker_search_debug.txt",
            "Text Files (*.txt)");
        if (savePath.isEmpty()) return;
        
        QFile f(savePath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, "Error", "Could not open file for writing.");
            return;
        }
        
        QTextStream out(&f);
        out << "========================================\n";
        out << "CraftPacker v3 - Search Results Debug\n";
        out << "Generated: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
        out << "========================================\n\n";
        
        out << "--- PACK SETTINGS ---\n";
        out << "MC Version: " << m_mcVersionEntry->text() << "\n";
        out << "Loader: " << m_loaderComboBox->currentText() << "\n";
        out << "Download Dir: " << m_dirEntry->text() << "\n\n";
        
        out << "--- MOD LIST (from input) ---\n";
        out << m_modlistInput->toPlainText() << "\n\n";
        
        out << "--- RESULTS TABLE ---\n";
        for (int i = 0; i < m_resultsTree->topLevelItemCount(); ++i) {
            auto* item = m_resultsTree->topLevelItem(i);
            if (!item) continue;
            out << "Row " << (i + 1) << ":\n";
            out << "  Name:           " << item->text(0) << "\n";
            out << "  Status:         " << item->text(1) << "\n";
            out << "  Reason:         " << item->text(2) << "\n";
            out << "  Source:         " << item->text(3) << "\n";
            out << "  Environment:    " << item->text(4) << "\n";
            out << "  UserRole Key:   " << item->data(0, Qt::UserRole).toString() << "\n";
            
            QString lookupKey = item->data(0, Qt::UserRole).toString();
            if (m_results.contains(lookupKey)) {
                const ModInfo& mi = m_results[lookupKey];
                out << "  Mod Info:\n";
                out << "    projectId:    " << mi.projectId << "\n";
                out << "    slug:         " << mi.slug << "\n";
                out << "    versionId:    " << mi.versionId << "\n";
                out << "    loader:       " << mi.loader << "\n";
                out << "    gameVersion:  " << mi.gameVersion << "\n";
                out << "    versionType:  " << mi.versionType << "\n";
                out << "    author:       " << mi.author << "\n";
                out << "    filename:     " << mi.filename << "\n";
                out << "    downloadUrl:  " << mi.downloadUrl << "\n";
                out << "    iconUrl:      " << mi.iconUrl << "\n";
                out << "    clientSide:   " << mi.clientSide << "\n";
                out << "    serverSide:   " << mi.serverSide << "\n";
                out << "    isDependency: " << (mi.isDependency ? "true" : "false") << "\n";
                out << "    fileSize:     " << mi.fileSize << "\n";
            }
            if (m_modEntries.contains(lookupKey)) {
                const ModEntry& me = m_modEntries[lookupKey];
                out << "  ModEntry:\n";
                out << "    originalName: " << me.originalName << "\n";
                out << "    cleanName:    " << me.cleanName << "\n";
                out << "    status:       " << me.status << "\n";
                out << "    reason:       " << me.reason << "\n";
                out << "    isConflict:   " << (me.isConflict ? "true" : "false") << "\n";
            }
            out << "\n";
        }
        
        // Phase 10.7: Rich totals
        int found = 0, softOverlapRows = 0, hardConflictRows = 0, missingDep = 0, depMismatch = 0;
        int loaderIncompat = 0, wrongVersion = 0, notFound = 0, needsReview = 0;
        for (int rowIdx = 0; rowIdx < m_resultsTree->topLevelItemCount(); ++rowIdx) {
            auto* rowItem = m_resultsTree->topLevelItem(rowIdx);
            if (!rowItem) continue;
            QString status = rowItem->text(1);
            if (status == "Available") found++;
            else if (status == "Soft Overlap") softOverlapRows++;
            else if (status == "Hard Conflict") hardConflictRows++;
            else if (status == "Missing Dependency") missingDep++;
            else if (status == "Dependency Mismatch") depMismatch++;
            else if (status == "Loader Incompatible") loaderIncompat++;
            else if (status == "Wrong Version") wrongVersion++;
            else if (status == "Not Found") notFound++;
            else if (status == "Needs Review") needsReview++;
        }
        out << "--- TOTAL COUNTS (from visible rows) ---\n";
        out << "Available:             " << found << "\n";
        out << "Soft Overlap Rows:     " << softOverlapRows << "\n";
        out << "Hard Conflict Rows:    " << hardConflictRows << "\n";
        out << "Missing Dependency:    " << missingDep << "\n";
        out << "Dependency Mismatch:   " << depMismatch << "\n";
        out << "Loader Incompatible:   " << loaderIncompat << "\n";
        out << "Wrong Version:         " << wrongVersion << "\n";
        out << "Not Found:             " << notFound << "\n";
        out << "Needs Review:          " << needsReview << "\n";
        out << "Total Rows:            " << m_resultsTree->topLevelItemCount() << "\n\n";
        
        // Phase 10.7: Canonical overlap groups
        out << "--- CANONICAL OVERLAP GROUPS ---\n";
        {
            QSet<QString> seenOverlapKeys;
            for (int rowIdx = 0; rowIdx < m_resultsTree->topLevelItemCount(); ++rowIdx) {
                auto* rowItem = m_resultsTree->topLevelItem(rowIdx);
                if (!rowItem) continue;
                QString status = rowItem->text(1);
                if (status == "Soft Overlap") {
                    QString reason = rowItem->text(2);
                    QString category;
                    if (reason.contains("minimap", Qt::CaseInsensitive)) category = "minimap";
                    else if (reason.contains("recipe", Qt::CaseInsensitive)) category = "recipe_viewer";
                    else if (reason.contains("rendering", Qt::CaseInsensitive)) category = "rendering";
                    else category = "general";
                    if (!seenOverlapKeys.contains("group:" + category)) {
                        seenOverlapKeys.insert("group:" + category);
                        // Collect all mods in this category
                        QStringList modsInGroup;
                        for (int r2 = 0; r2 < m_resultsTree->topLevelItemCount(); ++r2) {
                            auto* item2 = m_resultsTree->topLevelItem(r2);
                            if (!item2) continue;
                            if (item2->text(1) == "Soft Overlap") {
                                QString r2 = item2->text(2);
                                if ((category == "minimap" && r2.contains("minimap", Qt::CaseInsensitive)) ||
                                    (category == "recipe_viewer" && r2.contains("recipe", Qt::CaseInsensitive)) ||
                                    (category == "rendering" && r2.contains("rendering", Qt::CaseInsensitive))) {
                                    modsInGroup.append(item2->text(0));
                                }
                            }
                        }
                        QString type = "Soft Overlap";
                        out << "  group:" << category << " | " << type << " | mods: " << modsInGroup.join(", ") << "\n";
                    }
                }
            }
            if (seenOverlapKeys.isEmpty()) out << "  (none)\n";
        }
        out << "\n";
        
        // Phase 10.7: Canonical hard conflicts
        out << "--- CANONICAL HARD CONFLICTS ---\n";
        {
            bool hasHard = false;
            for (int rowIdx = 0; rowIdx < m_resultsTree->topLevelItemCount(); ++rowIdx) {
                auto* rowItem = m_resultsTree->topLevelItem(rowIdx);
                if (!rowItem) continue;
                if (rowItem->text(1) == "Hard Conflict") {
                    hasHard = true;
                    out << "  " << rowItem->text(0) << ": " << rowItem->text(2) << "\n";
                }
            }
            if (!hasHard) out << "  (none)\n";
        }
        out << "\n";
        
        // Phase 10.7: Dependency debug
        out << "--- DEPENDENCY DEBUG ---\n";
        {
            for (int rowIdx = 0; rowIdx < m_resultsTree->topLevelItemCount(); ++rowIdx) {
                auto* rowItem = m_resultsTree->topLevelItem(rowIdx);
                if (!rowItem) continue;
                QString lookupKey = rowItem->data(0, Qt::UserRole).toString();
                if (m_results.contains(lookupKey)) {
                    const ModInfo& mi = m_results[lookupKey];
                    if (mi.dependencies.isEmpty()) continue;
                    out << "Mod: " << mi.name << "\n";
                    for (const auto& depVal : mi.dependencies) {
                        QJsonObject dep = depVal.toObject();
                        QString depId = dep["project_id"].toString();
                        QString depType = dep["dependency_type"].toString();
                        QString depVersionId = dep["version_id"].toString();
                        
                        // Check if dependency is present in results
                        QString depStatus = "?";
                        bool present = false;
                        for (auto it = m_results.constBegin(); it != m_results.constEnd(); ++it) {
                            if (it.value().projectId == depId || it.value().slug == depId) {
                                present = true;
                                depStatus = it.value().projectId == depId ? "present" : "present (slug match)";
                                break;
                            }
                        }
                        if (!present) depStatus = "missing";
                        
                        // Look up dependency name
                        QString depName = depId;
                        for (auto it = m_results.constBegin(); it != m_results.constEnd(); ++it) {
                            if (it.value().projectId == depId) { depName = it.value().name; break; }
                        }
                        
                        out << "  " << depType << ": " << depName << " -> " << depStatus << "\n";
                    }
                }
            }
        }
        out << "\n";
        
        // Phase 10.7: Validation
        out << "--- VALIDATION ---\n";
        {
            bool integrityOk = true;
            // Verify every "Available" row has no missing required deps
            for (int rowIdx = 0; rowIdx < m_resultsTree->topLevelItemCount(); ++rowIdx) {
                auto* rowItem = m_resultsTree->topLevelItem(rowIdx);
                if (!rowItem) continue;
                if (rowItem->text(1) == "Available") {
                    QString lookupKey = rowItem->data(0, Qt::UserRole).toString();
                    if (m_results.contains(lookupKey)) {
                        const ModInfo& mi = m_results[lookupKey];
                        for (const auto& depVal : mi.dependencies) {
                            QJsonObject dep = depVal.toObject();
                            if (dep["dependency_type"].toString() == "required") {
                                QString depId = dep["project_id"].toString();
                                bool present = false;
                                for (auto it = m_results.constBegin(); it != m_results.constEnd(); ++it) {
                                    if (it.value().projectId == depId) { present = true; break; }
                                }
                                if (!present) {
                                    qDebug() << "[Validation] INTEGRITY ERROR: Available mod:" << mi.name
                                             << "has unresolved required dep:" << depId;
                                    out << "  INTEGRITY ERROR: Available mod " << mi.name
                                        << " has unresolved required dep: " << depId << "\n";
                                    integrityOk = false;
                                }
                            }
                        }
                    }
                }
            }
            // Verify totals match rows
            int totalFromCounts = found + softOverlapRows + hardConflictRows + missingDep + depMismatch
                                + loaderIncompat + wrongVersion + notFound + needsReview;
            if (totalFromCounts != m_resultsTree->topLevelItemCount()) {
                out << "  INTEGRITY ERROR: Counts " << totalFromCounts
                    << " do not match rows " << m_resultsTree->topLevelItemCount() << "\n";
                integrityOk = false;
            }
            if (integrityOk) out << "  INTEGRITY PASS\n";
        }
        out << "\n";
        
        f.close();
        updateStatusBar("Search debug report saved to " + savePath);
    });
    helpMenu->addSeparator();
    helpMenu->addAction("&GitHub", this, &CraftPacker::openGitHub);
    helpMenu->addAction("&Donate", this, &CraftPacker::openPayPal);
    setMenuBar(menuBar);

    // Central widget with QSplitter
    QWidget *centralWidget = new QWidget;
    setCentralWidget(centralWidget);
    m_mainSplitter = new QSplitter(Qt::Horizontal, centralWidget);

    // ============================================================
    // LEFT PANEL - Pack Settings + Mod List Input + Profiles
    // ============================================================
    QWidget* leftContainer = new QWidget();
    QVBoxLayout *leftColumnLayout = new QVBoxLayout(leftContainer);
    leftColumnLayout->setContentsMargins(8, 4, 8, 4);
    leftColumnLayout->setSpacing(6);

    QGroupBox *settingsGroup = new QGroupBox("Pack Settings");
    QGridLayout *settingsLayout = new QGridLayout(settingsGroup);
    settingsLayout->addWidget(new QLabel("MC Version:"), 0, 0);
    m_mcVersionEntry = new QLineEdit("1.20.1");
    settingsLayout->addWidget(m_mcVersionEntry, 0, 1);
    settingsLayout->addWidget(new QLabel("Loader:"), 0, 2);
    m_loaderComboBox = new QComboBox;
    m_loaderComboBox->addItems({"fabric", "forge", "neoforge", "quilt"});
    settingsLayout->addWidget(m_loaderComboBox, 0, 3);
    settingsLayout->setColumnStretch(1, 1);
    settingsLayout->addWidget(new QLabel("Download To:"), 1, 0);
    m_dirEntry = new QLineEdit(
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/CraftPacker_Downloads");
    settingsLayout->addWidget(m_dirEntry, 1, 1, 1, 3);
    QPushButton *browseButton = new QPushButton("Browse...");
    settingsLayout->addWidget(browseButton, 1, 4);

    QGroupBox *inputGroup = new QGroupBox("Mod List");
    QVBoxLayout *inputLayout = new QVBoxLayout(inputGroup);
    m_modlistInput = new QTextEdit;
    m_modlistInput->setPlaceholderText("Enter mod names (one per line)...");
    QPushButton *importButton = new QPushButton("Import from Folder...");
    inputLayout->addWidget(m_modlistInput);
    inputLayout->addWidget(importButton);

    QGroupBox *profileGroup = new QGroupBox("Mod Profiles");
    QVBoxLayout *profileLayout = new QVBoxLayout(profileGroup);
    m_profileListWidget = new QListWidget();
    QHBoxLayout *profileButtonsLayout = new QHBoxLayout();
    m_loadProfileButton = new QPushButton("Load");
    m_saveProfileButton = new QPushButton("Save");
    m_deleteProfileButton = new QPushButton("Delete");
    profileButtonsLayout->addWidget(m_loadProfileButton);
    profileButtonsLayout->addWidget(m_saveProfileButton);
    profileButtonsLayout->addWidget(m_deleteProfileButton);
    profileLayout->addWidget(m_profileListWidget);
    profileLayout->addLayout(profileButtonsLayout);

    leftColumnLayout->addWidget(settingsGroup);
    leftColumnLayout->addWidget(inputGroup);
    leftColumnLayout->addWidget(profileGroup);

    // ============================================================
    // CENTER PANEL - SINGLE unified Search Results table
    // ============================================================
    QWidget* centerContainer = new QWidget();
    QVBoxLayout *centerLayout = new QVBoxLayout(centerContainer);
    centerLayout->setContentsMargins(8, 4, 8, 4);
    centerLayout->setSpacing(6);

    m_taskManagerTab = new QTabWidget();

    // Results tab — SINGLE unified table replaces two trees
    QWidget* resultsTab = new QWidget();
    QVBoxLayout* resultsLayout = new QVBoxLayout(resultsTab);
    resultsLayout->setContentsMargins(4, 4, 4, 4);

    m_resultsTree = new QTreeWidget;
    m_resultsTree->setColumnCount(5);
    m_resultsTree->setHeaderLabels({"Mod Name", "Status", "Reason", "Source", "Environment"});
    m_resultsTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_resultsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_resultsTree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_resultsTree->setAlternatingRowColors(true);
    m_resultsTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_resultsTree->setAnimated(true);
    m_resultsTree->setRootIsDecorated(false);
    m_resultsTree->setSortingEnabled(true);
    m_resultsTree->sortItems(1, Qt::AscendingOrder);
    resultsLayout->addWidget(m_resultsTree);

    // Action buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout;
    m_searchButton = new QPushButton("🔍 Search Mods");
    m_researchButton = new QPushButton("Re-Search");
    m_downloadSelectedButton = new QPushButton("⬇ Download Selected");
    m_downloadAllButton = new QPushButton("⬇ Download All");
    m_downloadAllButton->setObjectName("downloadAllButton");
    m_downloadSelectedButton->setObjectName("downloadSelectedButton");
    buttonLayout->addWidget(m_searchButton);
    buttonLayout->addWidget(m_researchButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_downloadSelectedButton);
    buttonLayout->addWidget(m_downloadAllButton);
    resultsLayout->addLayout(buttonLayout);
    m_taskManagerTab->addTab(resultsTab, "🔍 Search Results");

    // Task Manager tab
    QWidget* taskTab = new QWidget();
    QVBoxLayout* taskLayout = new QVBoxLayout(taskTab);
    taskLayout->setContentsMargins(8, 8, 8, 8);

    m_overallProgressLabel = new QLabel("Overall Progress: Idle");
    m_overallProgressLabel->setStyleSheet("font-size: 12pt; font-weight: bold;");
    m_overallProgressBar = new QProgressBar();
    m_overallProgressBar->setRange(0, 100);
    m_overallProgressBar->setValue(0);
    m_overallProgressBar->setTextVisible(true);

    m_speedLabel = new QLabel("Speed: -- MB/s");
    m_speedLabel->setStyleSheet("color: #8a8aaa;");

    m_taskTree = new QTreeWidget;
    m_taskTree->setHeaderLabels({"Task", "Status", "Progress"});
    m_taskTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_taskTree->setColumnCount(3);

    taskLayout->addWidget(m_overallProgressLabel);
    taskLayout->addWidget(m_overallProgressBar);
    taskLayout->addWidget(m_speedLabel);
    taskLayout->addWidget(new QLabel("Active Tasks:"));
    taskLayout->addWidget(m_taskTree);
    m_taskManagerTab->addTab(taskTab, "⚡ Task Manager");

    centerLayout->addWidget(m_taskManagerTab);

    // Build right panel mod info
    m_modInfoPanel = new QWidget();
    m_modInfoPanel->setMinimumWidth(280);
    m_modInfoPanel->setMaximumWidth(350);
    QVBoxLayout* rightLayout = new QVBoxLayout(m_modInfoPanel);
    rightLayout->setContentsMargins(8, 4, 8, 4);

    m_modIconLabel = new QLabel("No Icon");
    m_modIconLabel->setAlignment(Qt::AlignCenter);
    m_modIconLabel->setMinimumSize(80, 80);
    m_modIconLabel->setStyleSheet("background:#2a2a3a;border-radius:8px;padding:8px;");
    rightLayout->addWidget(m_modIconLabel);

    m_modTitleLabel = new QLabel("Select a mod");
    m_modTitleLabel->setStyleSheet("font-size:14pt;font-weight:bold;color:#eee;");
    m_modTitleLabel->setWordWrap(true);
    rightLayout->addWidget(m_modTitleLabel);

    m_modAuthorLabel = new QLabel("");
    m_modAuthorLabel->setStyleSheet("color:#8a8aaa;font-size:10pt;");
    rightLayout->addWidget(m_modAuthorLabel);

    m_modEnvLabel = new QLabel("");
    m_modEnvLabel->setStyleSheet("color:#6a8cf7;font-size:9pt;");
    m_modEnvLabel->setWordWrap(true);
    rightLayout->addWidget(m_modEnvLabel);

    m_modConflictLabel = new QLabel("");
    m_modConflictLabel->setStyleSheet("color:#ff4444;font-size:9pt;font-weight:bold;");
    m_modConflictLabel->setWordWrap(true);
    m_modConflictLabel->hide();
    rightLayout->addWidget(m_modConflictLabel);

    m_modSummaryText = new QTextEdit();
    m_modSummaryText->setReadOnly(true);
    m_modSummaryText->setStyleSheet("QTextEdit{background:#1e1e2e;border:1px solid #2a2a3a;border-radius:6px;color:#ccc;}");
    m_modSummaryText->setPlaceholderText("Mod description...");
    rightLayout->addWidget(m_modSummaryText);

    // Assemble splitter
    m_mainSplitter->addWidget(leftContainer);
    m_mainSplitter->addWidget(centerContainer);
    m_mainSplitter->addWidget(m_modInfoPanel);
    m_mainSplitter->setStretchFactor(0,1);
    m_mainSplitter->setStretchFactor(1,3);
    m_mainSplitter->setStretchFactor(2,1);

    // Central layout
    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0,0,0,0);
    mainLayout->addWidget(m_mainSplitter);

    // Status bar
    statusBar()->showMessage("Ready");
    m_statusLabel = new QLabel("CraftPacker v3");
    m_statusLabel->setStyleSheet("padding:2px 8px;");
    statusBar()->addPermanentWidget(m_statusLabel);

    // Button connections
    connect(m_searchButton, &QPushButton::clicked, this, &CraftPacker::startSearch);
    connect(m_researchButton, &QPushButton::clicked, this, &CraftPacker::startReSearch);
    connect(m_downloadSelectedButton, &QPushButton::clicked, this, &CraftPacker::startDownloadSelected);
    connect(m_downloadAllButton, &QPushButton::clicked, this, &CraftPacker::startDownloadAll);
    connect(browseButton, &QPushButton::clicked, this, &CraftPacker::browseDirectory);
    connect(importButton, &QPushButton::clicked, this, &CraftPacker::importFromFolder);
    connect(m_loadProfileButton, &QPushButton::clicked, this, &CraftPacker::loadProfile);
    connect(m_saveProfileButton, &QPushButton::clicked, this, &CraftPacker::saveProfile);
    connect(m_deleteProfileButton, &QPushButton::clicked, this, &CraftPacker::deleteProfile);
    connect(m_resultsTree, &QTreeWidget::currentItemChanged, this, &CraftPacker::updateModInfoPanel);
    connect(m_resultsTree, &QTreeWidget::customContextMenuRequested, this, &CraftPacker::showResultsContextMenu);
    connect(m_profileListWidget, &QListWidget::itemDoubleClicked, this, &CraftPacker::loadProfile);

    m_actionButtons = {m_searchButton,m_researchButton,m_downloadSelectedButton,m_downloadAllButton};
}

// ============================================================
// HELPER: Strip version/loader/noise from a display name
// Preserves real mod names, only removes trailing version-like fragments,
// known noise words, and loader markers
// ============================================================
static QString sanitizeDisplayName(const QString& raw) {
    QString s = raw.trimmed();
    // If empty or very short, leave as-is
    if (s.length() <= 2) return s;

    // Remove leading loader tags: "Fabric | Sodium" -> "Sodium"
    s = s.remove(QRegularExpression(
        R"(^(fabric|forge|neoforge|quilt)\s*[|:\-]\s*)",
        QRegularExpression::CaseInsensitiveOption));

    // Remove trailing loader words: "Sodium Fabric" -> "Sodium"
    s = s.remove(QRegularExpression(
        R"(\s+(fabric|forge|neoforge|quilt|neoforge)\s*$)",
        QRegularExpression::CaseInsensitiveOption));

    // Remove version patterns: "1.20.1", "0.5.13+mc1.20.1", "v4.5", "1.0.36+mc1.20.1"
    s = s.remove(QRegularExpression(
        R"(\s+(v?\d+(\.\d+)+([-+][a-zA-Z0-9._]+)*)\s*$)"));
    s = s.remove(QRegularExpression(
        R"(\s+[a-zA-Z]*\d+\.\d+\.\d+\s*$)"));

    // Remove trailing version with mc prefix: "mc1.20.1", "1.20.1-forge"
    s = s.remove(QRegularExpression(
        R"(\s+(mc|forge|fabric|neoforge)[-_.]?\d+(\.\d+)+[-_a-zA-Z0-9]*$)",
        QRegularExpression::CaseInsensitiveOption));

    // Remove extra version residue: "-0.5.13+mc1.20.1", "+1.20.1", "_0.5.3"
    s = s.remove(QRegularExpression(
        R"(\s*[-_+(]?v?\d+\.\d+\.\d+[-+_a-zA-Z0-9]*\)?\s*$)"));

    // Remove parentheses/bracket content remnants
    s = s.remove(QRegularExpression(R"(\s*[\[\(\{][^\]\)\}]*[\]\)\}]\s*)"));
    s = s.remove(QRegularExpression(R"(\s*\([^)]*\)\s*$)"));

    // Remove stray [-_] that might remain
    s = s.replace(QRegularExpression("[-_]"), " ");

    // Collapse whitespace
    s = s.simplified().trimmed();

    return s;
}

// ============================================================
// STRUCT: Full import identity for one JAR
// Separates display_name, canonical_name, mod_id, version, loader cleanly
// ============================================================
struct ImportedModIdentity {
    // Identity
    QString displayName;        // Clean user-facing mod name (e.g. "Sodium")
    QString canonicalName;      // Clean query term (e.g. "sodium")
    QString modId;              // Canonical identifier (e.g. "sodium" from fabric.mod.json)
    QString version;            // Version string (e.g. "0.5.13+mc1.20.1")
    QString detectedLoader;     // "Fabric", "Forge", "NeoForge", "Quilt", "Unknown"
    QString extractionMethod;   // "fabric.mod.json", "mods.toml", "MANIFEST.MF", "filename heuristic"

    // Source tracking
    QString sourceFileName;
    QString sourceFilePath;

    // Duplicate status
    bool isDuplicate = false;
    QString duplicateOf;        // modId of the original it's a duplicate of
    QString identityConfidence; // "high" (from metadata), "medium" (MANIFEST.MF), "low" (filename)

    // Derive canonical search term — lowest-noise string for Modrinth API
    QString searchQuery() const {
        if (!canonicalName.isEmpty()) return canonicalName;
        if (!displayName.isEmpty()) return displayName.toLower();
        return modId;
    }
};

// ============================================================
// HELPER: Build display name from metadata, sanitized
// Returns the cleanest possible user-facing mod name
// ============================================================
static QString buildDisplayName(const ExtractedModMetadata& meta) {
    QString name = meta.bestName();
    // If bestName came from internal metadata (fabric.mod.json), it's already clean
    if (meta.extractionMethod == "fabric.mod.json" ||
        meta.extractionMethod == "mods.toml") {
        // modId is the canonical anchor
        if (!meta.displayName.isEmpty()) return meta.displayName;
        if (!meta.modId.isEmpty()) {
            // Clean slug to name: "fabric-api" -> "Fabric Api" -> "Fabric API" won't work
            // Just title-case the slug
            QString id = meta.modId;
            id.replace('-', ' ');
            QStringList words = id.split(' ', Qt::SkipEmptyParts);
            for (auto& w : words) {
                if (w.length() > 1) w = w.at(0).toUpper() + w.mid(1).toLower();
            }
            return words.join(' ');
        }
    }
    // Other sources may need post-processing sanitization
    return sanitizeDisplayName(name);
}

// ============================================================
// IMPORT FROM FOLDER — PHASE 9.3: CLEAN IDENTITY MODEL
// ============================================================
void CraftPacker::importFromFolder() {
    QString p = QFileDialog::getExistingDirectory(this, "Select Minecraft mods folder");
    if (p.isEmpty()) return;

    QDirIterator it(p, {"*.jar"}, QDir::Files, QDirIterator::Subdirectories);
    QVector<ImportedModIdentity> allImports;
    QMap<QString, int> modIdCount;          // modId -> count for duplicate detection
    QMap<QString, int> canonicalCount;      // canonicalName -> count
    int jarCount = 0, metadataCount = 0, fallbackCount = 0;

    // --- Phase 1: Extract metadata from every jar ---
    while (it.hasNext()) {
        it.next(); jarCount++;
        auto meta = JarMetadataExtractor::extract(it.filePath());

        ImportedModIdentity imp;
        imp.sourceFileName = it.fileName();
        imp.sourceFilePath = it.filePath();
        imp.extractionMethod = meta.extractionMethod;
        imp.version = meta.version;
        imp.detectedLoader = meta.detectedLoader;
        imp.modId = meta.modId;

        if (meta.extractionMethod == "fabric.mod.json" ||
            meta.extractionMethod == "mods.toml" ||
            meta.extractionMethod == "MANIFEST.MF") {
            // Internal metadata found — HIGH confidence
            metadataCount++;
            imp.identityConfidence = "high";
            // Use displayName from metadata directly; it's already clean
            imp.displayName = buildDisplayName(meta);
            imp.canonicalName = meta.modId.isEmpty() ? imp.displayName.toLower() : meta.modId;
        } else {
            // Filename fallback — LOW confidence, apply aggressive sanitization
            fallbackCount++;
            imp.identityConfidence = "low";

            QString base = it.fileName();
            if (base.endsWith(".jar")) base.chop(4);

            // Aggressive cleaning: strip version, loader, brackets
            base = base.remove(QRegularExpression(
                R"([-_]?(fabric|forge|neoforge|quilt)([-_]?mc\d.*)?$)",
                QRegularExpression::CaseInsensitiveOption));
            base = base.remove(QRegularExpression(R"([-_]+(mc|v)?\d+\.\d+\.?\d*[-+_a-zA-Z0-9]*$)"));
            base = base.remove(QRegularExpression(R"(\s*\[.*\]\s*)"));
            base = base.replace(QRegularExpression("[-_]"), " ");
            base = base.simplified().trimmed();

            // Title-case
            QStringList words = base.split(' ', Qt::SkipEmptyParts);
            for (auto& w : words) {
                if (w.length() > 1) w = w.at(0).toUpper() + w.mid(1).toLower();
            }
            imp.displayName = words.join(' ');
            imp.canonicalName = imp.displayName.toLower();
            imp.modId = imp.canonicalName;

            // Re-sanitize after title-case (catches "Create 1.20.1" -> "Create")
            imp.displayName = sanitizeDisplayName(imp.displayName);
            imp.canonicalName = imp.displayName.toLower();
            imp.modId = imp.canonicalName;
        }

        // Final safety sanitization on ALL display names
        imp.displayName = sanitizeDisplayName(imp.displayName);
        if (imp.displayName.isEmpty()) {
            // Absolute fallback: first meaningful word
            QString base = it.fileName();
            if (base.endsWith(".jar")) base.chop(4);
            base = base.simplified();
            QStringList parts = base.replace(QRegularExpression("[-_]"), " ")
                .split(' ', Qt::SkipEmptyParts);
            if (!parts.isEmpty()) {
                QString first = parts.first();
                if (first.length() > 1) first = first.at(0).toUpper() + first.mid(1).toLower();
                imp.displayName = first;
                imp.canonicalName = imp.displayName.toLower();
            }
        }

        // Count for duplicate detection
        modIdCount[imp.modId]++;
        canonicalCount[imp.canonicalName]++;
        allImports.append(imp);
    }

    // --- Phase 2: Mark duplicates using mod_id as primary anchor ---
    for (auto& imp : allImports) {
        if (modIdCount.value(imp.modId, 0) > 1) {
            imp.isDuplicate = true;
        }
    }

    // --- Phase 3: Build deduplicated list ---
    // Always keep the first occurrence (usually the one with internal metadata)
    QSet<QString> seenModIds;
    QVector<ImportedModIdentity> deduped;
    QVector<ImportedModIdentity> duplicates;

    for (auto& imp : allImports) {
        if (!imp.isDuplicate) {
            deduped.append(imp);
            seenModIds.insert(imp.modId);
        } else if (!seenModIds.contains(imp.modId)) {
            seenModIds.insert(imp.modId);
            imp.duplicateOf = imp.modId;
            deduped.append(imp);
            duplicates.append(imp);
        } else {
            imp.duplicateOf = imp.modId;
            duplicates.append(imp);
        }
    }

    // --- Phase 4: Populate mod list with CLEAN display names ---
    // Only include non-duplicate mods. Duplicates are tracked internally but
    // do NOT become separate search queries.
    if (!deduped.isEmpty()) {
        // Remove any duplicates that were accidentally kept in the deduped list
        QVector<ImportedModIdentity> cleanList;
        QSet<QString> seenCleanIds;
        for (const auto& imp : deduped) {
            if (imp.isDuplicate || seenCleanIds.contains(imp.modId)) continue;
            seenCleanIds.insert(imp.modId);
            cleanList.append(imp);
        }
        // Recompute duplicate count for summary
        int actualDupes = deduped.size() - cleanList.size();

        QStringList modNames;
        for (const auto& imp : cleanList) {
            modNames.append(imp.displayName);
        }
        m_modlistInput->setText(modNames.join('\n'));

        // --- Phase 5: Import quality summary ---
        int highConfCount = 0, medConfCount = 0, lowConfCount = 0;
        for (const auto& imp : deduped) {
            if (imp.identityConfidence == "high") highConfCount++;
            else if (imp.identityConfidence == "medium") medConfCount++;
            else lowConfCount++;
        }

        QString msg = QString("📥 Imported %1 mods from %2 JARs: %3 from metadata")
            .arg(deduped.size()).arg(jarCount).arg(highConfCount);
        if (medConfCount > 0) msg += QString(", %1 from MANIFEST.MF").arg(medConfCount);
        if (lowConfCount > 0) msg += QString(", %1 from filename fallback").arg(lowConfCount);
        if (duplicates.size() > 0) msg += QString(", %1 duplicate(s) merged").arg(duplicates.size());
        msg += ". Auto-searching...";
        updateStatusBar(msg);

        // --- Diagnostic logging ---
        qDebug() << "[ImportFromFolder] === IMPORT SUMMARY ===";
        qDebug() << "  JARs scanned:" << jarCount;
        qDebug() << "  Mods imported:" << deduped.size();
        qDebug() << "  High confidence (metadata):" << highConfCount;
        qDebug() << "  Low confidence (filename):" << lowConfCount;
        qDebug() << "  Duplicates merged:" << duplicates.size();
        qDebug() << "";
        qDebug() << "  Deduplicated mods:";
        for (const auto& imp : deduped) {
            qDebug() << "    [" << imp.identityConfidence << "]"
                     << imp.displayName
                     << "| modId:" << imp.modId
                     << "| source:" << imp.extractionMethod
                     << "| version:" << imp.version
                     << "| loader:" << imp.detectedLoader
                     << "| query:" << imp.searchQuery()
                     << "| file:" << imp.sourceFileName;
        }
        qDebug() << "";
        if (!duplicates.empty()) {
            qDebug() << "  Skipped duplicates:";
            for (const auto& imp : duplicates) {
                qDebug() << "    └─" << imp.sourceFileName
                         << "→ duplicate of modId:" << imp.duplicateOf
                         << "display:" << imp.displayName;
            }
        }
        qDebug() << "[ImportFromFolder] === END ===";

        // Auto-trigger search
        QTimer::singleShot(100, this, &CraftPacker::startSearch);
    } else {
        if (jarCount > 0)
            QMessageBox::information(this, "No Mods Parsed",
                QString("Found %1 .jar files, but could not extract mod names.").arg(jarCount));
        else
            QMessageBox::information(this, "No Mods Found", "No .jar files found.");
    }
}

// ============================================================
// ANIMATIONS
// ============================================================
void CraftPacker::runCompletionAnimation(QTreeWidgetItem *item) {
    if (!item || isMinimized()) return;
    auto* a = new QVariantAnimation(this);
    a->setDuration(800);
    a->setStartValue(0.0);
    a->setEndValue(1.0);
    QColor fc("#27ae60"), bc("#34495e");
    connect(a, &QVariantAnimation::valueChanged, this, [item, fc, bc](const QVariant& v) {
        if (!item) return;
        qreal p = v.toReal();
        QColor nc;
        nc.setRedF(bc.redF() + (fc.redF() - bc.redF()) * (1.0 - p));
        nc.setGreenF(bc.greenF() + (fc.greenF() - bc.greenF()) * (1.0 - p));
        nc.setBlueF(bc.blueF() + (fc.blueF() - bc.blueF()) * (1.0 - p));
        for (int i = 0; i < item->columnCount(); ++i) item->setBackground(i, nc);
    });
    connect(a, &QVariantAnimation::finished, this, [item, bc]() {
        if (!item) return;
        for (int i = 0; i < item->columnCount(); ++i) item->setBackground(i, bc);
    });
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

void CraftPacker::runJumpAnimation(QWidget* widget) {
    if (!widget) return;
    QPoint startPos = widget->pos();
    QPropertyAnimation* anim = new QPropertyAnimation(widget, "pos", this);
    anim->setDuration(400);
    anim->setStartValue(startPos);
    anim->setKeyValueAt(0.5, startPos - QPoint(0, 10));
    anim->setEndValue(startPos);
    anim->setEasingCurve(QEasingCurve::OutBounce);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

// ============================================================
// SEARCH LOGIC - Builds ModEntry for ALL input mods
// ============================================================
void CraftPacker::startSearch() {
    if (m_modlistInput->toPlainText().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Empty List", "Please enter some mod names to search for.");
        return;
    }
    QStringList l = m_modlistInput->toPlainText().split('\n', Qt::SkipEmptyParts);
    m_resultsTree->clear();
    m_results.clear();
    m_modEntries.clear();
    m_treeItems.clear();
    m_modConflicts.clear();
    m_allFoundOrDependencyProjects.clear();

    // Build ModEntry for EVERY input line
    for (const auto& name : l) {
        QString trimmed = name.trimmed();
        QString clean = StringCleaner::sanitizeModName(trimmed);
        ModEntry entry;
        entry.originalName = trimmed;
        entry.cleanName = clean;
        entry.status = "Searching...";
        entry.isConflict = false;
        m_modEntries.insert(trimmed, entry);
    }

    startModSearch(l);
}

void CraftPacker::startReSearch() {
    // Collect all entries with non-successful statuses
    QStringList l;
    for (int i = 0; i < m_resultsTree->topLevelItemCount(); ++i) {
        auto* item = m_resultsTree->topLevelItem(i);
        if (item) {
            QString status = item->text(1);
            if (status != "Available" && status != "CONFLICT") {
                l.append(item->text(0));
            }
        }
    }
    if (l.isEmpty()) {
        QMessageBox::warning(this, "Empty List", "No mods to re-search.");
        return;
    }
    m_resultsTree->clear();
    m_results.clear();
    m_modEntries.clear();
    m_treeItems.clear();
    m_modConflicts.clear();
    m_allFoundOrDependencyProjects.clear();

    for (const auto& name : l) {
        QString trimmed = name.trimmed();
        QString clean = StringCleaner::sanitizeModName(trimmed);
        ModEntry entry;
        entry.originalName = trimmed;
        entry.cleanName = clean;
        entry.status = "Searching...";
        entry.isConflict = false;
        m_modEntries.insert(trimmed, entry);
    }

    startModSearch(l);
}

void CraftPacker::startModSearch(const QStringList &modNames) {
    setButtonsEnabled(false);
    updateStatusBar(QString("Searching %1 mods...").arg(modNames.size()));

    m_searchCounter = modNames.size();
    m_overallProgressBar->setValue(0);
    m_overallProgressBar->setMaximum(modNames.size());

    QString loader = m_loaderComboBox->currentText();
    QString version = m_mcVersionEntry->text();

    for (const auto& name : modNames) {
        QThreadPool::globalInstance()->start([this, name, loader, version]() {
            findOneMod(name.trimmed(), loader, version);
        });
    }
}

void CraftPacker::findOneMod(QString name, QString loader, QString version) {
    emit sigUpdateStatusBar("Searching: " + name);

    // ================================================================
    // Step 1: Canonical normalization pipeline
    // ================================================================
    // Step 1a: Aggressive bracket/version/loader stripping
    QString cleanName = StringCleaner::sanitizeModName(name);
    QString spacedName = StringCleaner::splitCamelCase(cleanName);

    // Step 1b: Determine "match type" for debug logging
    // Possible types:
    //   "Alias->DirectSlug"  — alias resolved to slug, direct endpoint success
    //   "Alias->Search"      — alias resolved to slug, direct failed but search found it
    //   "Raw Search"         — no alias, found via generic search
    //   "Dependency Project ID" — from dependency resolver
    QString matchType = "Raw Search";

    // Step 1c: Check if we have a direct alias → slug mapping
    // If so, try the /project/{slug} endpoint FIRST (no search needed)
    QString directSlug = StringCleaner::getDirectSlug(cleanName);
    if (directSlug.isEmpty()) {
        // Try the raw name too (e.g. "Fabric API (Required)" -> after bracket strip: "Fabric API")
        directSlug = StringCleaner::getDirectSlug(name);
    }

    // ================================================================
    // Helper lambda: perform API request with rate limiting
    // ================================================================
    QNetworkAccessManager manager;
    QEventLoop loop;

    auto performRequest = [&](const QUrl& url) -> std::optional<QJsonDocument> {
        m_rateLimiter.wait();
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::UserAgentHeader,
            "helloworldx64/CraftPacker/3.0.0");
        QNetworkReply *reply = manager.get(req);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        if (reply->error() == QNetworkReply::NoError) {
            auto data = reply->readAll();
            reply->deleteLater();
            return QJsonDocument::fromJson(data);
        }
        reply->deleteLater();
        return std::nullopt;
    };

    // Helper: extract ModInfo from a project JSON object + version lookup
    auto tryGetModInfo = [&](const QString& projectIdOrSlug,
                             const QString& tag) -> std::optional<ModInfo> {
        if (auto mi = getModInfo(projectIdOrSlug, loader, version)) {
            auto info = mi.value();
            info.originalQuery = name;
            return info;
        }
        return std::nullopt;
    };

    // ================================================================
    // Step 2: Try DIRECT SLUG LOOKUP (if alias table has an entry)
    // This is the critical fix for Fabric API, Cloth Config, etc.
    // ================================================================
    if (!directSlug.isEmpty()) {
        qDebug() << "[Search] Direct slug lookup for:" << name << "->" << directSlug;
        matchType = "Alias->DirectSlug";

        // Try the direct project endpoint with the known slug
        QUrl projUrl(ModrinthAPI::API_BASE + "/project/" + directSlug);
        if (auto projCheck = performRequest(projUrl)) {
            QJsonObject projObj = projCheck->object();
            if (!projObj.isEmpty()) {
                QString projType = projObj["project_type"].toString();
                if (projType.isEmpty() || projType == "mod") {
                    QString projectId = projObj["id"].toString();
                    if (projectId.isEmpty()) projectId = projObj["slug"].toString();
                    if (projectId.isEmpty()) projectId = directSlug;

                    if (auto info = tryGetModInfo(projectId, "direct-slug")) {
                        qDebug() << "[Search] Direct slug HIT:" << directSlug
                                 << "->" << info.value().name;
                        emit sigModFound(info.value(), "Available", "canonical");
                        return;
                    }

                    // Direct project found but version mismatch — give informative error
                    QString title = projObj["title"].toString();
                    qDebug() << "[Search] Direct slug found but version mismatch:"
                             << title << loader << version;
                    matchType = "Alias->DirectSlug (version mismatch)";
                } else {
                    // Found but wrong project type
                    matchType = "Alias->DirectSlug (wrong type: " + projType + ")";
                }
            }
        } else {
            qDebug() << "[Search] Direct slug lookup FAILED for:" << directSlug;
            // Fall through to search
        }
    }

    // ================================================================
    // Step 3: Try generic SEARCH (only reached if direct slug failed or no alias)
    // ================================================================
    // Best-ranked result tracker
    struct RankedHit {
        QString projectId;
        QString slug;
        QString title;
        double similarity = 0.0;
        bool exactTitleMatch = false;
        bool exactSlugMatch = false;
        bool hasLoaderMatch = false;
        bool hasVersionMatch = false;
    };
    RankedHit bestHit;
    bool hasAnyHit = false;

    QUrlQuery query;
    query.addQueryItem("query", spacedName);
    query.addQueryItem("limit", "10"); // Get more results for better ranking
    QJsonArray facetInner; facetInner.append("project_type:mod");
    QJsonArray facetOuter; facetOuter.append(facetInner);
    query.addQueryItem("facets",
        QString::fromUtf8(QJsonDocument(facetOuter).toJson(QJsonDocument::Compact)));

    QString searchUrl = ModrinthAPI::API_SEARCH + "?" + query.toString();
    if (auto doc = performRequest(QUrl(searchUrl))) {
        if (doc->isObject() && doc->object().contains("hits")) {
            for (const auto& hitVal : doc->object()["hits"].toArray()) {
                QJsonObject hit = hitVal.toObject();
                QString projectId = hit["project_id"].toString();
                QString hitTitle = hit["title"].toString();
                QString hitSlug = hit["slug"].toString();
                QString description = hit["description"].toString();

                // Get project details for version checking
                if (auto projData = performRequest(
                        QUrl(ModrinthAPI::API_BASE + "/project/" + projectId))) {
                    QJsonObject projObj = projData->object();
                    if (projObj.isEmpty()) continue;

                    QString modName = projObj["title"].toString();
                    QString slug = projObj["slug"].toString();
                    QString projType = projObj["project_type"].toString();
                    if (projType != "mod" && !projType.isEmpty()) continue;

                    // Compute ranking score
                    QString apiNameClean = StringCleaner::sanitizeModName(modName);
                    double sim = StringCleaner::similarityRatio(cleanName, apiNameClean);
                    double simSpaced = StringCleaner::similarityRatio(cleanName,
                        StringCleaner::splitCamelCase(apiNameClean));
                    double bestSim = std::max(sim, simSpaced);
                    bool primaryExists = StringCleaner::primaryWordExists(cleanName, modName);

                    // Skip very poor matches (raise threshold for strict filtering)
                    if (bestSim < 0.75 || !primaryExists) continue;

                    // Check for exact matches
                    bool exactTitle = cleanName.toLower() == modName.toLower().trimmed();
                    bool exactSlug = directSlug.isEmpty() ?
                        (slug.toLower() == cleanName.toLower().replace(' ', '-')) :
                        (slug.toLower() == directSlug);

                    // Check loader/version availability
                    bool hasLoader = false, hasVersion = false;
                    QUrlQuery vq;
                    QUrl vu(ModrinthAPI::API_BASE + "/project/" + slug + "/version");
                    vu.setQuery(vq);
                    if (auto versionsData = performRequest(vu)) {
                        for (const auto& verVal : versionsData->array()) {
                            QJsonObject vo = verVal.toObject();
                            QJsonArray lArr = vo["loaders"].toArray();
                            QJsonArray gvArr = vo["game_versions"].toArray();
                            if (!hasLoader) {
                                for (const auto& l : lArr)
                                    if (l.toString() == loader) { hasLoader = true; break; }
                            }
                            if (!hasVersion) {
                                for (const auto& gv : gvArr)
                                    if (gv.toString() == version) { hasVersion = true; break; }
                            }
                        }
                    }

                    // Score: exact title > exact slug > high similarity
                    double score = bestSim;
                    if (exactTitle) score += 10.0;
                    if (exactSlug) score += 5.0;
                    if (hasLoader) score += 2.0;
                    if (hasVersion) score += 1.0;

                    if (!hasAnyHit || score > (bestHit.similarity +
                        (bestHit.exactTitleMatch ? 10.0 : 0.0) +
                        (bestHit.exactSlugMatch ? 5.0 : 0.0) +
                        (bestHit.hasLoaderMatch ? 2.0 : 0.0) +
                        (bestHit.hasVersionMatch ? 1.0 : 0.0))) {
                        bestHit = {projectId, slug, modName,
                                   bestSim, exactTitle, exactSlug,
                                   hasLoader, hasVersion};
                        hasAnyHit = true;
                    }
                }
            }
        }
    }

    // ================================================================
    // Step 4: Use best-ranked result
    // ================================================================
    if (hasAnyHit) {
        // If we reached via alias but direct slug failed, mark as "Alias->Search"
        if (!directSlug.isEmpty() && matchType == "Alias->DirectSlug") {
            matchType = "Alias->Search";
        }

        qDebug() << "[Search] Best hit for" << name
                 << ": title=" << bestHit.title
                 << " slug=" << bestHit.slug
                 << " sim=" << bestHit.similarity
                 << " exactTitle=" << bestHit.exactTitleMatch
                 << " exactSlug=" << bestHit.exactSlugMatch
                 << " loader=" << bestHit.hasLoaderMatch
                 << " version=" << bestHit.hasVersionMatch
                 << " type=" << matchType;

        // Try to get full ModInfo matching loader+version
        if (auto info = tryGetModInfo(bestHit.projectId, "search-best")) {
            emit sigModFound(info.value(), "Available", matchType);
            return;
        }

        // Version mismatch — give specific error
        if (!bestHit.hasVersionMatch) {
            if (!bestHit.hasLoaderMatch) {
                emit sigModNotFound(name,
                    "Loader Incompatible: " + bestHit.title + " has no " + loader + " version");
            } else {
                emit sigModNotFound(name,
                    "Wrong MC Version for " + bestHit.title);
            }
            return;
        }

        if (!bestHit.hasLoaderMatch) {
            emit sigModNotFound(name,
                "Loader Incompatible: " + bestHit.title + " has no " + loader + " version");
            return;
        }
    }

    // ================================================================
    // Step 5: Final fallback — try direct slug from clean name
    // (only if we haven't already tried a direct slug)
    // ================================================================
    if (directSlug.isEmpty()) {
        QString slugFallback = cleanName.toLower()
            .replace(QRegularExpression(R"(\s)"), "-");
        if (!slugFallback.isEmpty()) {
            QUrl projUrl(ModrinthAPI::API_BASE + "/project/" + slugFallback);
            if (auto projCheck = performRequest(projUrl)) {
                QJsonObject projObj = projCheck->object();
                if (!projObj.isEmpty()) {
                    QString projType = projObj["project_type"].toString();
                    if (projType.isEmpty() || projType == "mod") {
                        QString fallbackId = projObj["id"].toString();
                        if (fallbackId.isEmpty()) fallbackId = slugFallback;
                        if (auto info = tryGetModInfo(fallbackId, "slug-fallback")) {
                            QString slugClean = StringCleaner::sanitizeModName(info->name);
                            double sim = StringCleaner::similarityRatio(cleanName, slugClean);
                            if (sim >= 0.85) {
                                emit sigModFound(info.value(), "Available (Slug)", "slug-fallback");
                                return;
                            }
                        }
                    }
                }
            }
        }
    }

    // ================================================================
    // Step 6: If direct slug lookup was attempted but failed,
    // do NOT stop here — fall through to generic search.
    // Only stop if we had no direct slug AND no search hits.
    // ================================================================
    QString reason = "Not found on Modrinth";
    if (directSlug.isEmpty() && !hasAnyHit)
        reason = "Not on Modrinth (Check CurseForge)";

    // If we had a direct slug and it failed, try again via search fallback
    if (!directSlug.isEmpty() && !hasAnyHit) {
        qDebug() << "[Search] Direct lookup failed for" << name 
                 << "— falling through to search...";
    }

    if (hasAnyHit) {
        qDebug() << "[Search] Best hit existed but getModInfo returned empty:"
                 << name << "bestHit=" << bestHit.title 
                 << "projectId=" << bestHit.projectId;
    }

    qDebug() << "[Search] NOT FOUND:" << name
             << "matchType=" << matchType
             << "directSlug=" << directSlug
             << "hasAnyHit=" << hasAnyHit;

    emit sigModNotFound(name, reason);
}

// ============================================================
// RESULTS HANDLING - Single unified table
// ============================================================
void CraftPacker::onModFound(const ModInfo& modInfo, const QString& status, const QString& tag) {
    QMutexLocker l(&m_searchMutex);

    if (m_allFoundOrDependencyProjects.contains(modInfo.projectId)) {
        if (m_searchCounter.fetchAndAddRelaxed(-1) - 1 <= 0) onSearchFinished();
        return;
    }
    m_allFoundOrDependencyProjects.insert(modInfo.projectId);
    m_results.insert(modInfo.projectId, modInfo);

    // Update the ModEntry
    if (m_modEntries.contains(modInfo.originalQuery)) {
        m_modEntries[modInfo.originalQuery].status = status;
        m_modEntries[modInfo.originalQuery].modInfo = modInfo;
    }

    QTreeWidgetItem* i = new QTreeWidgetItem(m_resultsTree);
    // Tag dependency mods with [Dependency] suffix for clarity
    QString displayName = modInfo.name;
    if (modInfo.isDependency && !displayName.contains("[Dependency]"))
        displayName += " [Dependency]";
    i->setText(0, displayName);
    i->setText(1, status);
    i->setText(2, modInfo.isDependency ? "Auto-resolved dependency" : "");
    i->setText(3, modInfo.isDependency ? "Dependency" : "Modrinth");
    i->setData(0, Qt::UserRole, modInfo.projectId); // Use projectId for lookup

    // Set environment tag
    QString envText;
    QString cs = modInfo.clientSide.isEmpty() ? "required" : modInfo.clientSide;
    QString ss = modInfo.serverSide.isEmpty() ? "required" : modInfo.serverSide;
    if (cs == "required" && ss == "unsupported") {
        envText = "🖥 Client Only";
        i->setForeground(4, QColor("#6a8cf7"));
    } else if (cs == "unsupported" && ss == "required") {
        envText = "🔧 Server Only";
        i->setForeground(4, QColor("#f39c12"));
    } else if (cs == "optional" && ss == "optional") {
        envText = "🔄 Optional";
        i->setForeground(4, QColor("#8a8aaa"));
    } else {
        envText = "✅ Both";
        i->setForeground(4, QColor("#2ecc71"));
    }
    i->setText(4, envText);

    if (status == "Available") i->setForeground(1, QColor("#2ecc71"));
    else if (tag == "fallback") i->setForeground(1, QColor("#f39c12"));
    else if (tag == "dependency") {
        i->setForeground(1, QColor("#8e44ad"));
        runCompletionAnimation(i);
    }

    // Store the item keyed by projectId for lookups
    m_treeItems.insert(modInfo.projectId, i);

    if (m_searchCounter.fetchAndAddRelaxed(-1) - 1 <= 0) {
        // Search is done — trigger auto-dependency resolution from onSearchFinished
        onSearchFinished();
    }
}

void CraftPacker::onModNotFound(const QString& modName, const QString& reason) {
    QMutexLocker l(&m_searchMutex);

    // Update the ModEntry
    if (m_modEntries.contains(modName)) {
        m_modEntries[modName].status = reason;
    }

    // Determine the status label based on reason
    QString statusLabel;
    if (reason.contains("Loader Incompatible")) {
        statusLabel = "Loader Incompatible";
    } else if (reason.contains("Wrong MC Version")) {
        statusLabel = "Wrong Version";
    } else {
        statusLabel = "Not Found";
    }

    QTreeWidgetItem* i = new QTreeWidgetItem(m_resultsTree);
    i->setText(0, modName);
    i->setText(1, statusLabel);
    i->setText(2, reason);
    i->setText(3, "Modrinth");
    i->setText(4, "N/A");
    if (statusLabel == "Not Found")
        i->setForeground(1, QColor("#e74c3c")); // Red
    else if (statusLabel == "Loader Incompatible")
        i->setForeground(1, QColor("#f39c12")); // Orange
    else if (statusLabel == "Wrong Version")
        i->setForeground(1, QColor("#e67e22")); // Dark orange
    i->setForeground(2, QColor("#f39c12")); // Yellow for reason
    i->setData(0, Qt::UserRole, modName);

    // Store under the original name for lookups
    m_treeItems.insert(modName, i);

    if (m_searchCounter.fetchAndAddRelaxed(-1) - 1 <= 0) onSearchFinished();
}

// ============================================================
// Phase 10.5: Canonical conflict engine
// Builds conflict objects FIRST, then applies to rows.
// - One canonical object per unique pair or group
// - Uses category keys to deduplicate
// - Correctly attaches only to involved mods
// ============================================================
void CraftPacker::runConflictDetection() {
    // Build list of ONLY available mods for conflict detection.
    QVector<ModInfo> allMods;
    for (auto it = m_modEntries.constBegin(); it != m_modEntries.constEnd(); ++it) {
        const ModEntry& entry = it.value();
        if (entry.status == "Available" || entry.status.startsWith("Available")) {
            allMods.append(entry.modInfo);
        }
    }

    m_modConflicts.clear();

    // Step 1: Build canonical conflict objects from ConflictDetector
    auto rawConflicts = ConflictDetector::instance().detectConflicts(allMods);

    // Step 2: Categorize each conflict into canonical objects
    // Map: canonicalKey -> CanonicalConflict
    struct CanonicalConflict {
        QString key;                // e.g. "pair:projA:projB" or "group:minimap"
        QString type;               // "hard_conflict", "soft_overlap"
        QString category;           // "minimap", "recipe_viewer", "rendering", "general"
        QSet<QString> involvedNames;  // Display names of involved mods
        QSet<QString> involvedIds;  // Project IDs of involved mods
        QSet<QString> involvedRowKeys; // Row keys (projectId or original name)
        QString message;            // Human-readable explanation
        int priority = 0;           // 0=soft overlap, 1=hard conflict
    };
    QHash<QString, CanonicalConflict> canonicalConflicts;

    // Build projectId -> display name map
    QMap<QString, QString> idToName;
    QMap<QString, QString> idToSlug;
    for (const auto& m : allMods) {
        idToName[m.projectId] = m.name;
        idToSlug[m.projectId] = m.slug;
    }

    // Determine which projectIds are in which conflict groups from known_conflicts.json
    // to categorize them (minimap, recipe_viewer, etc.)
    auto categorizeNames = [&](const QString& nameA, const QString& nameB) -> QString {
        // Check known conflict groups for category info
        // For now, use keyword detection in the message
        QSet<QString> loweredSet;
        loweredSet.insert(nameA.toLower());
        loweredSet.insert(nameB.toLower());

        // Category detection via keywords
        static const QSet<QString> minimapWords = {"minimap", "xaero", "journeymap", "map", "waypoints", "worldmap"};
        static const QSet<QString> recipeViewerWords = {"rei", "jei", "emi", "recipe", "roughly enough items", "just enough items"};
        static const QSet<QString> renderingWords = {"sodium", "canvas", "iris", "optifine", "shader", "of"};

        for (const auto& s : loweredSet) {
            for (const auto& kw : minimapWords) if (s.contains(kw)) return "minimap";
            for (const auto& kw : recipeViewerWords) if (s.contains(kw)) return "recipe_viewer";
            for (const auto& kw : renderingWords) if (s.contains(kw)) return "rendering";
        }
        return "general";
    };

    auto getConflictMessage = [](const QString& category) -> QString {
        if (category == "minimap")
            return "Multiple minimap mods detected. They may duplicate HUD features and waste screen space.";
        if (category == "recipe_viewer")
            return "Multiple recipe viewer mods detected. They overlap in functionality and may clutter the UI.";
        if (category == "rendering")
            return "Rendering engine overlap detected. These mods may conflict graphically.";
        return "Incompatible mods detected. They may not work correctly together.";
    };

    for (const auto& c : rawConflicts) {
        // Skip missing-dependency warnings for now (handled elsewhere)
        if (c.reason == "missing_dependency") continue;
        if (c.modB.isEmpty() || c.modA == c.modB) continue;

        // Find project IDs for modA and modB
        QString projA, projB;
        for (const auto& m : allMods) {
            if (m.name == c.modA) projA = m.projectId;
            if (m.name == c.modB) projB = m.projectId;
        }
        if (projA.isEmpty() || projB.isEmpty()) continue;

        // Generate canonical key: sorted pair
        QString keyA = projA, keyB = projB;
        if (keyA > keyB) { std::swap(keyA, keyB); std::swap(projA, projB); }
        QString pairKey = "pair:" + keyA + ":" + keyB;

        // Categorize
        QString category = categorizeNames(c.modA, c.modB);

        bool isHard = (c.reason == "incompatible") &&
                       !c.reason.contains("API", Qt::CaseInsensitive);

        // Build or update canonical conflict
        if (!canonicalConflicts.contains(pairKey)) {
            CanonicalConflict cc;
            cc.key = pairKey;
            cc.category = category;
            cc.type = isHard ? "hard_conflict" : "soft_overlap";
            cc.priority = isHard ? 1 : 0;
            cc.message = getConflictMessage(category);
            cc.involvedIds.insert(projA);
            cc.involvedIds.insert(projB);
            cc.involvedNames.insert(c.modA);
            cc.involvedNames.insert(c.modB);
            canonicalConflicts[pairKey] = cc;
        } else {
            auto& cc = canonicalConflicts[pairKey];
            cc.involvedIds.insert(projA);
            cc.involvedIds.insert(projB);
            cc.involvedNames.insert(c.modA);
            cc.involvedNames.insert(c.modB);
            // Upgrade severity if any source says incompatible
            if (isHard && cc.type == "soft_overlap") {
                cc.type = "hard_conflict";
                cc.priority = 1;
            }
        }
    }

    // Step 3: Build group-level canonical conflicts from pair conflicts
    // e.g., all minimap pairs -> one group:minimap conflict
    QMap<QString, CanonicalConflict> groupConflicts;
    for (const auto& cc : canonicalConflicts) {
        QString groupKey = "group:" + cc.category;
        if (!groupConflicts.contains(groupKey)) {
            CanonicalConflict gc;
            gc.key = groupKey;
            gc.type = cc.type;
            gc.category = cc.category;
            gc.priority = cc.priority;
            gc.message = getConflictMessage(cc.category);
            gc.involvedIds = cc.involvedIds;
            gc.involvedNames = cc.involvedNames;
            groupConflicts[groupKey] = gc;
        } else {
            auto& gc = groupConflicts[groupKey];
            gc.involvedIds.unite(cc.involvedIds);
            gc.involvedNames.unite(cc.involvedNames);
            if (cc.priority > gc.priority) {
                gc.priority = cc.priority;
                gc.type = cc.type;
            }
        }
    }

    // Step 4: Resolve row keys for all involved mods and apply to tree
    // Build reverse map: projectId -> row keys (original names that resolved to it)
    QHash<QString, QSet<QString>> projIdToRowKeys;
    for (auto it = m_modEntries.constBegin(); it != m_modEntries.constEnd(); ++it) {
        const ModEntry& entry = it.value();
        if (entry.status == "Available" || entry.status.startsWith("Available")) {
            projIdToRowKeys[entry.modInfo.projectId].insert(it.key());
        }
    }

    int totalConflictRows = 0;
    QSet<QString> seenConflictProjectIds; // Track which projectIds already have a conflict record

    // Apply group conflicts first (one object per category)
    for (const auto& gc : groupConflicts) {
        if (gc.involvedIds.size() < 2) continue;

        // Determine status label and color
        QString statusLabel = (gc.type == "hard_conflict") ? "Hard Conflict" : "Soft Overlap";
        QColor fg = (gc.type == "hard_conflict") ? QColor(255, 68, 68) : QColor(243, 156, 18);
        QColor bg = (gc.type == "hard_conflict") ? QColor(40, 10, 10) : QColor(30, 25, 5);

        // Collect all row keys for this conflict
        QSet<QString> involvedRowKeys;
        for (const auto& pid : gc.involvedIds) {
            if (projIdToRowKeys.contains(pid)) {
                involvedRowKeys.unite(projIdToRowKeys[pid]);
            }
            // Also store by projectId directly
            involvedRowKeys.insert(pid);
        }

        for (const auto& pid : gc.involvedIds) {
            if (seenConflictProjectIds.contains(pid)) continue;
            seenConflictProjectIds.insert(pid);

            // Store canonical conflict object (group-level)
            ConflictWarning canonicalCw;
            QStringList namesList = gc.involvedNames.values();
            canonicalCw.modA = namesList.first();
            canonicalCw.modB = (namesList.size() > 1) ? namesList.last() : "";
            canonicalCw.reason = gc.message;
            m_modConflicts[pid] = canonicalCw;

            // Apply to tree items keyed by projectId
            if (m_treeItems.contains(pid)) {
                auto* item = m_treeItems[pid];
                item->setText(1, statusLabel);
                item->setText(2, gc.message);
                for (int col = 0; col < item->columnCount(); ++col) {
                    item->setForeground(col, fg);
                    item->setBackground(col, bg);
                }
                totalConflictRows++;
            }

            // Apply to ModEntry
            for (const auto& rowKey : projIdToRowKeys.value(pid)) {
                if (m_modEntries.contains(rowKey)) {
                    m_modEntries[rowKey].status = statusLabel;
                    m_modEntries[rowKey].isConflict = true;
                }
            }
        }
    }

    // Step 5: Update status bar
    if (totalConflictRows > 0) {
        // Count soft overlaps vs hard conflicts
        int softCount = 0, hardCount = 0;
        for (const auto& cc : groupConflicts) {
            if (cc.type == "hard_conflict") hardCount++;
            else softCount++;
        }

        QString statusTip = "⚠ Conflicts detected!";
        for (const auto& gc : groupConflicts) {
            QStringList names = gc.involvedNames.values();
            statusTip += QString("\n  ⚠ [%1] %2")
                .arg(gc.type == "hard_conflict" ? "HARD" : "OVERLAP")
                .arg(names.join(", "));
        }

        m_statusLabel->setToolTip(statusTip);
        if (hardCount > 0) {
            m_statusLabel->setStyleSheet("QLabel { color: #ff4444; font-weight: bold; }");
        } else {
            m_statusLabel->setStyleSheet("QLabel { color: #f39c12; font-weight: bold; }");
        }

        QString barMsg;
        if (hardCount > 0 && softCount > 0)
            barMsg = QString("⚠ %1 hard conflict(s), %2 soft overlap(s) detected! Click rows for details.")
                .arg(hardCount).arg(softCount);
        else if (hardCount > 0)
            barMsg = QString("⚠ %1 hard conflict(s) detected! Click rows for details.").arg(hardCount);
        else
            barMsg = QString("⚠ %1 soft overlap(s) detected. Click rows for details.").arg(softCount);

        updateStatusBar(barMsg);
    }

    // Step 6: INTEGRITY VALIDATION
    // Verify no duplicate conflict keys
    QSet<QString> seenKeys;
    bool integrityFail = false;
    for (auto it = m_modConflicts.constBegin(); it != m_modConflicts.constEnd(); ++it) {
        const auto& cw = it.value();
        QString pairA = StringCleaner::normalizeForConflict(cw.modA);
        QString pairB = StringCleaner::normalizeForConflict(cw.modB);
        QString ck = "pair:" + pairA + ":" + pairB;
        if (seenKeys.contains(ck)) {
            qDebug() << "[ConflictDetector:INTEGRITY ERROR] Duplicate conflict key:" << ck;
            integrityFail = true;
        }
        seenKeys.insert(ck);

        // Verify every involved project exists in results
        if (!m_results.contains(it.key()) && !m_modEntries.contains(it.key())) {
            qDebug() << "[ConflictDetector:INTEGRITY ERROR] Conflict references unknown key:" << it.key();
            integrityFail = true;
        }
    }
    if (!integrityFail) {
        qDebug() << "[ConflictDetector:INTEGRITY PASS] All conflict records are canonical.";
    }
}

void CraftPacker::onSearchFinished() {
    // Count results from VISIBLE tree rows (single source of truth)
    int found = 0, notFound = 0, wrongVersion = 0, wrongLoader = 0, loaderIncomp = 0;
    for (int i = 0; i < m_resultsTree->topLevelItemCount(); ++i) {
        auto* item = m_resultsTree->topLevelItem(i);
        if (!item) continue;
        QString status = item->text(1);
        if (status == "Available") found++;
        else if (status == "Loader Incompatible") loaderIncomp++;
        else if (status == "Wrong Version") wrongVersion++;
        else if (status == "Not Found") notFound++;
    }

    updateStatusBar(QString("Search complete. Found %1 | Wrong Version: %2 | Wrong Loader: %3 | Not Found: %4")
        .arg(found).arg(wrongVersion).arg(loaderIncomp).arg(notFound));
    setButtonsEnabled(true);
    m_overallProgressBar->setValue(m_overallProgressBar->maximum());
    m_taskManagerTab->setCurrentIndex(0);

    // Auto-resolve required dependencies in background thread
    QThreadPool::globalInstance()->start([this]() {
        QString loader = m_loaderComboBox->currentText();
        QString version = m_mcVersionEntry->text();
        QSet<QString> seen = m_allFoundOrDependencyProjects;
        QList<QString> toResolve;
        QList<ModInfo> depsToAdd;

        {
            QMutexLocker l(&m_searchMutex);
            for (auto it = m_results.constBegin(); it != m_results.constEnd(); ++it) {
                toResolve.append(it.key());
            }
        }

        while (!toResolve.isEmpty()) {
            QString pid = toResolve.takeFirst();
            if (seen.contains(pid)) continue;
            seen.insert(pid);

            ModInfo mi;
            {
                QMutexLocker l(&m_searchMutex);
                if (!m_results.contains(pid)) continue;
                mi = m_results[pid];
            }

            for (const auto& depVal : mi.dependencies) {
                QJsonObject dep = depVal.toObject();
                if (dep["dependency_type"].toString() == "required") {
                    QString depId = dep["project_id"].toString();
                    if (!seen.contains(depId)) {
                        seen.insert(depId);
                        if (auto diOpt = resolveDependencyLocally(depId, loader, version)) {
                            auto di = diOpt.value();
                            di.isDependency = true;
                            di.originalQuery = di.name;
                            depsToAdd.append(di);
                            toResolve.append(di.projectId);
                        }
                    }
                }
            }
        }

        if (!depsToAdd.isEmpty()) {
            QMetaObject::invokeMethod(this, [this, depsToAdd]() {
                for (const auto& dep : depsToAdd) {
                    QMutexLocker l(&m_searchMutex);
                    if (m_allFoundOrDependencyProjects.contains(dep.projectId)) continue;
                    m_allFoundOrDependencyProjects.insert(dep.projectId);
                    m_results.insert(dep.projectId, dep);

                    QTreeWidgetItem* depItem = new QTreeWidgetItem(m_resultsTree);
                    QString depName = dep.name;
                    if (!depName.contains("[Dependency]"))
                        depName += " [Dependency]";
                    depItem->setText(0, depName);
                    depItem->setText(1, "Available");
                    depItem->setText(2, "Auto-resolved dependency");
                    depItem->setText(3, "Dependency");
                    depItem->setText(4, dep.clientSide + " / " + dep.serverSide);
                    depItem->setData(0, Qt::UserRole, dep.projectId);
                    depItem->setForeground(1, QColor("#8e44ad"));
                    m_treeItems.insert(dep.projectId, depItem);
                    runCompletionAnimation(depItem);
                }
                updateStatusBar(QString("Auto-resolved %1 dependencies. Re-checking conflicts...").arg(depsToAdd.size()));
                runConflictDetection();
            }, Qt::QueuedConnection);
        } else {
            QMetaObject::invokeMethod(this, [this]() {
                runJumpAnimation(m_downloadAllButton);
                runConflictDetection();
            }, Qt::QueuedConnection);
        }
    });
}

// ============================================================
// MOD INFO RETRIEVAL
// ============================================================
std::optional<ModInfo> CraftPacker::getModInfo(const QString& projectIdOrSlug,
                                                const QString& loader,
                                                const QString& gameVersion) {
    m_rateLimiter.wait();
    QNetworkAccessManager manager;
    QEventLoop loop;

    auto performRequest = [&](const QUrl& url) -> std::optional<QByteArray> {
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::UserAgentHeader,
            "helloworldx64/CraftPacker/3.0.0");
        QNetworkReply *reply = manager.get(req);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        if (reply->error() == QNetworkReply::NoError) {
            auto data = reply->readAll();
            reply->deleteLater();
            return data;
        }
        reply->deleteLater();
        return std::nullopt;
    };

    auto projData = performRequest(QUrl(ModrinthAPI::API_BASE + "/project/" + projectIdOrSlug));
    if (!projData) return std::nullopt;

    QJsonDocument doc = QJsonDocument::fromJson(projData.value());
    if (doc.isNull() || !doc.isObject()) return std::nullopt;
    QJsonObject projObj = doc.object();
    if (projObj.isEmpty()) return std::nullopt;

    QUrlQuery query;
    query.addQueryItem("loaders", QJsonDocument(QJsonArray({loader})).toJson(QJsonDocument::Compact));
    query.addQueryItem("game_versions", QJsonDocument(QJsonArray({gameVersion})).toJson(QJsonDocument::Compact));

    QUrl versionUrl(ModrinthAPI::API_BASE + "/project/" + projObj.value("slug").toString() + "/version");
    versionUrl.setQuery(query);

    auto versionsData = performRequest(versionUrl);
    if (!versionsData) return std::nullopt;

    QJsonDocument versionsDoc = QJsonDocument::fromJson(versionsData.value());
    if (versionsDoc.isNull() || !versionsDoc.isArray()) return std::nullopt;

    QJsonArray versions = versionsDoc.array();
    for (const QString& type : {"release", "beta", "alpha"}) {
        for (const auto& verVal : versions) {
            QJsonObject verObj = verVal.toObject();
            if (verObj["version_type"].toString() == type) {
                QJsonArray files = verObj["files"].toArray();
                if (files.isEmpty()) continue;
                QJsonObject fileObj = files[0].toObject();

                ModInfo info;
                info.name = projObj["title"].toString();
                info.slug = projObj["slug"].toString();
                info.projectId = projObj["id"].toString();
                info.versionId = verObj["id"].toString();
                info.downloadUrl = fileObj["url"].toString();
                info.filename = fileObj["filename"].toString();
                info.versionType = verObj["version_type"].toString();
                info.loader = loader;
                info.gameVersion = gameVersion;
                info.author = projObj["author"].toString();
                info.description = projObj["description"].toString();
                info.iconUrl = projObj["icon_url"].toString();
                info.clientSide = projObj["client_side"].toString();
                info.serverSide = projObj["server_side"].toString();
                info.fileSize = fileObj["size"].toVariant().toLongLong();
                {
                    QJsonObject fh = fileObj.value("hashes").toObject();
                    info.sha1 = fh.value("sha1").toString().toLower();
                    info.sha512 = fh.value("sha512").toString().toLower();
                }
                
                // Populate dependencies from BOTH the version object and the dedicated /dependencies endpoint
                QJsonArray versionDeps = verObj["dependencies"].toArray();
                
                // Also fetch /project/{id}/dependencies for the full dependency tree
                // Modrinth returns: { "projects": [...project_objects], "versions": [...version_objects] }
                // Each version object has: project_id, dependency_type, version_id
                QUrl depUrl(ModrinthAPI::API_BASE + "/project/" + projObj["id"].toString() + "/dependencies");
                auto depsData = performRequest(depUrl);
                if (depsData) {
                    QJsonObject depsObj = QJsonDocument::fromJson(*depsData).object();
                    // Parse the "versions" array — this contains objects with project_id + dependency_type
                    QJsonArray depVersions = depsObj["versions"].toArray();
                    for (const auto& dvVal : depVersions) {
                        QJsonObject dvObj = dvVal.toObject();
                        // dependency_type may be on the version-level entry
                        if (dvObj.contains("dependency_type")) {
                            QJsonObject depEntry;
                            depEntry["project_id"] = dvObj["project_id"];
                            depEntry["dependency_type"] = dvObj["dependency_type"];
                            depEntry["version_id"] = dvObj["id"];
                            versionDeps.append(depEntry);
                        }
                    }
                    // Also check the "projects" array — some responses put dependency_type there too
                    QJsonArray depProjects = depsObj["projects"].toArray();
                    for (const auto& dpVal : depProjects) {
                        QJsonObject dpObj = dpVal.toObject();
                        if (dpObj.contains("dependency_type")) {
                            QJsonObject depEntry;
                            depEntry["project_id"] = dpObj["id"];
                            depEntry["dependency_type"] = dpObj["dependency_type"];
                            versionDeps.append(depEntry);
                        }
                    }
                }
                
                info.dependencies = versionDeps;

                return info;
            }
        }
    }

    return std::nullopt;
}

// ============================================================
// DOWNLOAD LOGIC
// ============================================================
struct DownloadBatch {
    QList<ModInfo> mods;
    QString downloadDir;
    QString loader;
    QString gameVersion;
};

void CraftPacker::startDownloadSelected() {
    auto sel = m_resultsTree->selectedItems();
    if (sel.isEmpty()) {
        QMessageBox::warning(this, "No Selection", "Please select one or more mods to download.");
        return;
    }
    QStringList ids;
    for (auto* i : sel) {
        QString lookupKey = i->data(0, Qt::UserRole).toString();
        // Only add if it's a projectId (found mod), not an original name (not-found mod)
        if (m_results.contains(lookupKey)) {
            ids.append(lookupKey);
        }
    }
    if (ids.isEmpty()) {
        QMessageBox::warning(this, "No Valid Mods", "Selected items do not contain downloadable mods.");
        return;
    }
    startDownload(ids);
}

void CraftPacker::startDownloadAll() {
    if (m_results.isEmpty()) {
        QMessageBox::warning(this, "No Mods Found", "Search for mods first.");
        return;
    }
    startDownload(m_results.keys());
}

void CraftPacker::startDownload(const QList<QString>& projectIds) {
    setButtonsEnabled(false);
    m_activeDownloads.storeRelaxed(0);
    m_lastBytesReceived = 0;
    m_lastSpeedTime = QDateTime::currentDateTime();

    m_taskTree->clear();

    QString dlDir = m_dirEntry->text();
    QString loader = m_loaderComboBox->currentText();
    QString version = m_mcVersionEntry->text();
    QDir().mkpath(dlDir);

    QList<ModInfo> initialMods;
    for (const auto& id : projectIds) {
        if (m_results.contains(id)) {
            ModInfo mod = m_results[id];
            if (QFile::exists(dlDir + "/" + mod.filename)) continue;
            initialMods.append(mod);
        }
    }

    if (initialMods.isEmpty()) {
        updateStatusBar("All selected mods are already downloaded.");
        setButtonsEnabled(true);
        return;
    }

    updateStatusBar("Resolving dependencies...");
    auto* batch = new DownloadBatch{initialMods, dlDir, loader, version};

    QThreadPool::globalInstance()->start([this, batch]() {
        QList<ModInfo> downloadQueue;
        QSet<QString> seen;
        QList<ModInfo> toResolve = batch->mods;

        while (!toResolve.isEmpty()) {
            ModInfo current = toResolve.takeFirst();
            if (seen.contains(current.projectId)) continue;
            seen.insert(current.projectId);
            downloadQueue.prepend(current);

            for (const auto& depVal : current.dependencies) {
                QJsonObject dep = depVal.toObject();
                if (dep["dependency_type"].toString() == "required") {
                    QString depId = dep["project_id"].toString();
                    if (!seen.contains(depId)) {
                        if (auto diOpt = resolveDependencyLocally(depId, batch->loader, batch->gameVersion)) {
                            auto di = diOpt.value();
                            di.isDependency = true;
                            toResolve.append(di);
                        }
                    }
                }
            }
        }

        QMetaObject::invokeMethod(this, "onDependencyResolutionFinished",
            Qt::QueuedConnection,
            Q_ARG(QList<ModInfo>, downloadQueue),
            Q_ARG(QString, batch->downloadDir));
        delete batch;
    });
}

std::optional<ModInfo> CraftPacker::resolveDependencyLocally(const QString& projectId,
                                                               const QString& loader,
                                                               const QString& gameVersion) {
    QNetworkAccessManager mgr;
    QEventLoop loop;
    auto req = [&](const QUrl& url) -> std::optional<QByteArray> {
        QNetworkRequest r(url);
        r.setHeader(QNetworkRequest::UserAgentHeader,
            "helloworldx64/CraftPacker/3.0.0");
        QNetworkReply* reply = mgr.get(r);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        if (reply->error() == QNetworkReply::NoError) {
            auto data = reply->readAll();
            reply->deleteLater();
            return data;
        }
        reply->deleteLater();
        return std::nullopt;
    };

    auto projData = req(QUrl(ModrinthAPI::API_BASE + "/project/" + projectId));
    if (!projData) return std::nullopt;

    QJsonObject projObj = QJsonDocument::fromJson(*projData).object();
    if (projObj.isEmpty()) return std::nullopt;

    QUrlQuery q;
    q.addQueryItem("loaders", QJsonDocument(QJsonArray({loader})).toJson(QJsonDocument::Compact));
    q.addQueryItem("game_versions", QJsonDocument(QJsonArray({gameVersion})).toJson(QJsonDocument::Compact));
    QUrl vu(ModrinthAPI::API_BASE + "/project/" + projObj["slug"].toString() + "/version");
    vu.setQuery(q);

    auto verData = req(vu);
    if (!verData) return std::nullopt;
    QJsonArray versions = QJsonDocument::fromJson(*verData).array();

    for (const QString& t : {"release", "beta", "alpha"}) {
        for (const auto& v : versions) {
            QJsonObject vo = v.toObject();
            if (vo["version_type"].toString() == t) {
                QJsonArray files = vo["files"].toArray();
                if (files.isEmpty()) continue;
                QJsonObject fo = files[0].toObject();

                ModInfo info;
                info.name = projObj["title"].toString();
                info.slug = projObj["slug"].toString();
                info.projectId = projObj["id"].toString();
                info.versionId = vo["id"].toString();
                info.downloadUrl = fo["url"].toString();
                info.filename = fo["filename"].toString();
                info.versionType = t;
                info.loader = loader;
                info.gameVersion = gameVersion;
                info.author = projObj["author"].toString();
                info.description = projObj["description"].toString();
                info.iconUrl = projObj["icon_url"].toString();
                info.clientSide = projObj["client_side"].toString();
                info.serverSide = projObj["server_side"].toString();
                info.fileSize = fo["size"].toVariant().toLongLong();
                {
                    QJsonObject fh = fo.value("hashes").toObject();
                    info.sha1 = fh.value("sha1").toString().toLower();
                    info.sha512 = fh.value("sha512").toString().toLower();
                }
                info.dependencies = vo["dependencies"].toArray();
                return info;
            }
        }
    }
    return std::nullopt;
}

void CraftPacker::onDependencyResolutionFinished(const QList<ModInfo>& downloadQueue, const QString& dlDir) {
    m_taskTree->clear();
    m_downloadDir = dlDir;

    int downloadCount = 0;

    // First pass: create task items for ALL mods, including already-downloaded ones
    for (const auto& m : downloadQueue) {
        QString filePath = dlDir + "/" + m.filename;
        bool alreadyExists = QFile::exists(filePath);
        
        auto* taskItem = new QTreeWidgetItem(m_taskTree);
        taskItem->setData(0, Qt::UserRole, m.projectId);

        if (alreadyExists) {
            taskItem->setText(0, m.filename);
            taskItem->setText(1, "✓ Already Downloaded");
            taskItem->setText(2, "100%");
            taskItem->setForeground(1, QColor("#2ecc71"));
        } else {
            downloadCount++;
            taskItem->setText(0, m.filename);
            taskItem->setText(1, m.isDependency ? "Dependency" : "Downloading...");
            taskItem->setText(2, "0%");
        }
    }

    m_activeDownloads.storeRelaxed(downloadCount);
    m_overallProgressBar->setValue(0);
    m_overallProgressBar->setMaximum(downloadCount);

    // If all mods already exist, show summary and return
    if (downloadCount == 0) {
        updateStatusBar("All mods are already downloaded.");
        setButtonsEnabled(true);
        runJumpAnimation(m_downloadAllButton);
        return;
    }

    // Second pass: start download threads for mods that need them
    for (const auto& m : downloadQueue) {
        QString filePath = dlDir + "/" + m.filename;
        if (QFile::exists(filePath)) continue;

        if (m.isDependency) {
            QString depKey = m.projectId;
            m_results.insert(depKey, m);
            auto* depItem = new QTreeWidgetItem(m_resultsTree);
            depItem->setText(0, m.name);
            depItem->setText(1, "Dependency");
            depItem->setText(2, "Added as dependency");
            depItem->setText(3, "Dependency");
            depItem->setText(4, m.clientSide + " / " + m.serverSide);
            depItem->setData(0, Qt::UserRole, depKey);
            depItem->setForeground(1, QColor("#8e44ad"));
            m_treeItems.insert(depKey, depItem);
            runCompletionAnimation(depItem);
        }

        QThread* t = new QThread;
        DownloadWorker* w = new DownloadWorker(m, dlDir);
        w->moveToThread(t);

        connect(t, &QThread::started, w, &DownloadWorker::process);
        connect(w, &DownloadWorker::progress, this, &CraftPacker::onDownloadProgress);
        connect(w, &DownloadWorker::finished, this, &CraftPacker::onDownloadFinished);
        connect(w, &DownloadWorker::finished, t, &QThread::quit);
        connect(w, &DownloadWorker::finished, w, &QObject::deleteLater);
        connect(t, &QThread::finished, t, &QObject::deleteLater);
        t->start();
    }
}

void CraftPacker::onDownloadProgress(const QString& iid, qint64 r, qint64 t) {
    auto now = QDateTime::currentDateTime();
    qint64 elapsed = m_lastSpeedTime.msecsTo(now);
    if (elapsed > 500) {
        qint64 deltaBytes = r - m_lastBytesReceived;
        if (elapsed > 0) {
            double mbps = (deltaBytes / 1048576.0) / (elapsed / 1000.0);
            m_speedLabel->setText(QString("Speed: %1 MB/s").arg(mbps, 0, 'f', 2));
        }
        m_lastBytesReceived = r;
        m_lastSpeedTime = now;
    }

    for (auto it = m_results.constBegin(); it != m_results.constEnd(); ++it) {
        if (it.value().projectId == iid || it.key() == iid) {
            if (t > 0) {
                int pct = static_cast<int>((static_cast<double>(r) / t) * 100.0);
                if (m_treeItems.contains(it.key())) {
                    if (auto* item = m_treeItems[it.key()]) {
                        item->setText(2, QString::number(pct) + "%");
                    }
                }
                auto taskItems = m_taskTree->findItems(
                    it.value().filename, Qt::MatchContains, 0);
                for (auto* ti : taskItems) {
                    ti->setText(2, QString::number(pct) + "%");
                }
            }
            return;
        }
    }
}

void CraftPacker::onDownloadFinished(const QString& iid, const QString& error) {
    for (auto it = m_results.constBegin(); it != m_results.constEnd(); ++it) {
        if (it.value().projectId == iid || it.key() == iid) {
            if (m_treeItems.contains(it.key())) {
                if (auto* i = m_treeItems[it.key()]) {
                    if (error.isEmpty()) {
                        i->setText(1, "Complete");
                        i->setText(2, "100%");
                        i->setForeground(1, QColor("#2ecc71"));
                        runCompletionAnimation(i);
                        auto taskItems = m_taskTree->findItems(
                            it.value().filename, Qt::MatchContains, 0);
                        for (auto* ti : taskItems) {
                            ti->setText(1, "✓ Complete");
                            ti->setText(2, "100%");
                            ti->setForeground(1, QColor("#2ecc71"));
                        }
                    } else {
                        i->setText(1, "Error");
                        i->setForeground(1, QColor("#e74c3c"));
                        auto taskItems = m_taskTree->findItems(
                            it.value().filename, Qt::MatchContains, 0);
                        for (auto* ti : taskItems) {
                            ti->setText(1, "✗ Error: " + error);
                            ti->setForeground(1, QColor("#e74c3c"));
                        }
                    }
                }
            }
            break;
        }
    }

    m_overallProgressBar->setValue(m_overallProgressBar->value() + 1);
    if (m_activeDownloads.fetchAndAddRelaxed(-1) - 1 == 0) {
        onAllDownloadsFinished();
    }
}

void CraftPacker::runSwooshAnimation(QTreeWidgetItem* item) {
    if (item) runCompletionAnimation(item);
}

void CraftPacker::onAllDownloadsFinished() {
    updateStatusBar("All downloads completed!");
    setButtonsEnabled(true);
    m_speedLabel->setText("Speed: -- MB/s");
    runJumpAnimation(m_downloadAllButton);
}

// ============================================================
// PROFILES
// ============================================================
void CraftPacker::loadProfileList() {
    m_profileListWidget->clear();
    QDir d(m_profilePath);
    d.setNameFilters({"*.txt"});
    for (const auto& fi : d.entryInfoList(QDir::Files)) {
        m_profileListWidget->addItem(fi.baseName());
    }
}

void CraftPacker::saveProfile() {
    bool ok;
    QString t = QInputDialog::getText(this, "Save Profile", "Profile Name:",
                                       QLineEdit::Normal, "", &ok);
    if (ok && !t.isEmpty()) {
        QFile f(m_profilePath + "/" + t + ".txt");
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream o(&f);
            o << m_modlistInput->toPlainText();
            f.close();
            loadProfileList();
            updateStatusBar("Profile '" + t + "' saved.");
        }
    }
}

void CraftPacker::loadProfile() {
    auto sel = m_profileListWidget->selectedItems();
    if (sel.isEmpty()) return;
    QString n = sel.first()->text();
    QFile f(m_profilePath + "/" + n + ".txt");
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream i(&f);
        m_modlistInput->setText(i.readAll());
        updateStatusBar("Profile '" + n + "' loaded.");
    }
}

void CraftPacker::deleteProfile() {
    auto sel = m_profileListWidget->selectedItems();
    if (sel.isEmpty()) return;
    QString n = sel.first()->text();
    if (QMessageBox::question(this, "Confirm Delete",
            "Delete profile '" + n + "'?",
            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        if (QFile::remove(m_profilePath + "/" + n + ".txt")) {
            loadProfileList();
            updateStatusBar("Profile '" + n + "' deleted.");
        }
    }
}

// ============================================================
// EXPORT
// ============================================================
void CraftPacker::exportToMrpack() {
    if (m_results.isEmpty()) {
        QMessageBox::warning(this, "No Mods", "Search for mods first.");
        return;
    }

    ExportOptions opts;
    bool ok;
    opts.packName = QInputDialog::getText(this, "Pack Name", "Enter pack name:",
                                           QLineEdit::Normal, "My Modpack", &ok);
    if (!ok || opts.packName.isEmpty()) return;

    QString savePath = QFileDialog::getSaveFileName(this, "Save .mrpack",
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + "/" + opts.packName + ".mrpack",
        "Modrinth Modpack (*.mrpack)");
    if (savePath.isEmpty()) return;
    opts.outputPath = savePath;

    opts.mcVersion = m_mcVersionEntry->text().trimmed();
    opts.loader = m_loaderComboBox->currentText().trimmed();
    opts.packVersion = "1.0.0";
    opts.author = "CraftPacker User";
    opts.localModsDirectory = m_dirEntry->text().trimmed();

    opts.loaderVersion = PackExporter::suggestedLoaderVersion(opts.loader, opts.mcVersion).trimmed();
    if (opts.loaderVersion.isEmpty()) {
        bool lvOk = false;
        const QString hint = (QStringLiteral("neoforge") == opts.loader.toLower())
            ? QStringLiteral("NeoForge build version (combined with Minecraft in CurseForge loader id)")
            : QStringLiteral("Loader version — Fabric/Quilt can often be queried automatically — "
                               "Forge example: 47.4.0");
        opts.loaderVersion = QInputDialog::getText(this, QObject::tr("Loader version"),
            QObject::tr("%1").arg(hint),
            QLineEdit::Normal, {}, &lvOk).trimmed();
        if (!lvOk || opts.loaderVersion.isEmpty())
            return;
    }

    QVector<ModInfo> modList;
    for (const auto& m : m_results) modList.append(m);

    auto& exporter = PackExporter::instance();
    connect(&exporter, &PackExporter::exportProgress, this,
        [this](int pct, const QString& stage) {
            m_overallProgressBar->setValue(pct);
            updateStatusBar(stage);
        }, Qt::UniqueConnection);

    QString exportErr;
    if (exporter.exportToMrpack(modList, opts, &exportErr)) {
        updateStatusBar("Export complete: " + savePath);
    } else {
        updateStatusBar("Export failed!");
        QMessageBox::warning(this, "Export Failed",
            exportErr.isEmpty() ? QStringLiteral("Failed to create .mrpack file.") : exportErr);
    }
}

void CraftPacker::exportToCurseForge() {
    if (m_results.isEmpty()) {
        QMessageBox::warning(this, "No Mods", "Search for mods first.");
        return;
    }

    ExportOptions opts;
    bool ok;
    opts.packName = QInputDialog::getText(this, "Pack Name", "Enter pack name:",
                                           QLineEdit::Normal, "My Modpack", &ok);
    if (!ok || opts.packName.isEmpty()) return;

    QString savePath = QFileDialog::getSaveFileName(this, "Save CurseForge .zip",
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + "/" + opts.packName + "-cf.zip",
        "CurseForge Modpack (*.zip)");
    if (savePath.isEmpty()) return;
    opts.outputPath = savePath;

    opts.mcVersion = m_mcVersionEntry->text().trimmed();
    opts.loader = m_loaderComboBox->currentText().trimmed();
    opts.packVersion = "1.0.0";
    opts.author = "CraftPacker User";
    opts.localModsDirectory = m_dirEntry->text().trimmed();

    opts.loaderVersion = PackExporter::suggestedLoaderVersion(opts.loader, opts.mcVersion).trimmed();
    if (opts.loaderVersion.isEmpty()) {
        bool lvOk = false;
        opts.loaderVersion = QInputDialog::getText(
            this, QObject::tr("Loader version"),
            QObject::tr("CurseForge manifest requires loader id (fabric-x.y.z, forge-x.y.z, "
                          "neoForge uses neoforge-<mc>-<ver>). NeoForge \"version\" segment only here:"),
            QLineEdit::Normal, {}, &lvOk).trimmed();
        if (!lvOk || opts.loaderVersion.isEmpty())
            return;
    }

    QVector<ModInfo> modList;
    for (const auto& m : m_results) modList.append(m);

    QString exportErr;
    if (PackExporter::instance().exportToCurseForge(modList, opts, &exportErr)) {
        updateStatusBar("Export complete: " + savePath);
    } else {
        updateStatusBar("Export failed!");
        QMessageBox::warning(this, "Export Failed",
            exportErr.isEmpty() ? QStringLiteral("Failed to create CurseForge .zip file.") : exportErr);
    }
}

void CraftPacker::generateServerPack() {
    if (m_results.isEmpty()) {
        QMessageBox::warning(this, "No Mods", "Search for mods first.");
        return;
    }

    ExportOptions opts;
    bool ok;
    opts.packName = QInputDialog::getText(this, "Server Pack Name",
                                           "Enter server pack name:",
                                           QLineEdit::Normal, "My Server Pack", &ok);
    if (!ok || opts.packName.isEmpty()) return;

    QString savePath = QFileDialog::getSaveFileName(this, "Save Server Pack",
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + "/" + opts.packName + "-server.zip",
        "ZIP Archive (*.zip)");
    if (savePath.isEmpty()) return;
    opts.outputPath = savePath;

    opts.mcVersion = m_mcVersionEntry->text();
    opts.loader = m_loaderComboBox->currentText();
    opts.packVersion = "1.0.0";

    QVector<ModInfo> modList;
    for (const auto& m : m_results) modList.append(m);

    // Detect and show conflicts before export
    auto conflicts = ConflictDetector::instance().detectConflicts(modList);
    if (!conflicts.isEmpty()) {
        QString warning = "⚠ Conflict Warnings:\n\n";
        for (const auto& c : conflicts) {
            if (c.reason == "incompatible") {
                warning += QString("• %1 is INCOMPATIBLE with %2\n").arg(c.modA, c.modB);
            } else if (c.reason == "duplicate_function") {
                warning += QString("• Duplicate mod found: %1\n").arg(c.modA);
            }
        }
        auto result = QMessageBox::warning(this, "Conflicts Detected",
            warning + "\nContinue with server pack export?",
            QMessageBox::Yes | QMessageBox::No);
        if (result == QMessageBox::No) return;
    }

    if (PackExporter::instance().exportServerPack(modList, opts)) {
        updateStatusBar("Server pack export complete: " + savePath);
    } else {
        updateStatusBar("Server pack export failed!");
    }
}

// ============================================================
// UPDATES & MIGRATION
// ============================================================
void CraftPacker::checkForUpdates() {
    if (m_results.isEmpty()) {
        QMessageBox::warning(this, "No Mods", "Search for mods first.");
        return;
    }

    QVector<ModInfo> modList;
    for (const auto& m : m_results) modList.append(m);

    updateStatusBar("Checking for updates...");
    m_taskManagerTab->setCurrentIndex(1);
    UpdateChecker::instance().checkForUpdates(modList);
}

void CraftPacker::onUpdatesChecked(const QVector<UpdateStatus>& results) {
    int available = 0;
    for (const auto& r : results) {
        if (r.status == UpdateStatus::UpdateAvailable) available++;
    }
    updateStatusBar(QString("Update check complete: %1 updates available").arg(available));

    if (available > 0) {
        QString msg = "Updates available:\n\n";
        for (const auto& r : results) {
            if (r.status == UpdateStatus::UpdateAvailable) {
                msg += QString("• %1\n").arg(r.modName);
            }
        }
        QMessageBox::information(this, "Updates Available", msg);
    } else {
        QMessageBox::information(this, "Up to Date", "All mods are up to date.");
    }
}

void CraftPacker::migrateProfile() {
    if (m_results.isEmpty()) {
        QMessageBox::warning(this, "No Mods", "Search for mods first.");
        return;
    }

    bool ok;
    QString newVersion = QInputDialog::getText(this, "Version Migration",
        "Target Minecraft version (e.g., 1.21):",
        QLineEdit::Normal, "", &ok);
    if (!ok || newVersion.isEmpty()) return;

    QString newLoader = m_loaderComboBox->currentText();

    QVector<ModInfo> modList;
    for (const auto& m : m_results) modList.append(m);

    updateStatusBar(QString("Migrating mods to Minecraft %1...").arg(newVersion));
    UpdateChecker::instance().migrateToVersion(modList, newLoader, newVersion);
}

// ============================================================
// SETTINGS
// ============================================================
void CraftPacker::applySettings() {
    QThreadPool::globalInstance()->setMaxThreadCount(
        m_settings->value("maxThreads", QThread::idealThreadCount()).toInt());
}

void CraftPacker::openSettingsDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle("Settings");
    dialog.setMinimumWidth(400);
    QFormLayout form(&dialog);

    QSpinBox* threadCountSpinBox = new QSpinBox(&dialog);
    threadCountSpinBox->setRange(1, QThread::idealThreadCount() * 2);
    threadCountSpinBox->setValue(
        m_settings->value("maxThreads", QThread::idealThreadCount()).toInt());
    form.addRow("Max Concurrent Threads:", threadCountSpinBox);

    QLineEdit* cfKeyEntry = new QLineEdit(&dialog);
    cfKeyEntry->setEchoMode(QLineEdit::Password);
    cfKeyEntry->setPlaceholderText("Leave empty to use injected key (if available)");
    form.addRow("CurseForge API Key:", cfKeyEntry);

    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                Qt::Horizontal, &dialog);
    form.addRow(&buttonBox);

    connect(&buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        m_settings->setValue("maxThreads", threadCountSpinBox->value());
        if (!cfKeyEntry->text().isEmpty()) {
            CurseForgeAPI::instance().setUserApiKey(cfKeyEntry->text());
            m_settings->setValue("userCfApiKey", cfKeyEntry->text());
        }
        applySettings();
    }
}

// ============================================================
// MOD INFO PANEL - UPDATED for unified table
// ============================================================
void CraftPacker::updateModInfoPanel(QTreeWidgetItem *current, QTreeWidgetItem *) {
    if (!current) return;
    QString lookupKey = current->data(0, Qt::UserRole).toString();

    // Check if this is a projectId (found mod) or an original name (not found)
    if (m_results.contains(lookupKey)) {
        ModInfo mod = m_results[lookupKey];
        m_modTitleLabel->setText(mod.name);
        m_modAuthorLabel->setText("by " + mod.author);
        m_modSummaryText->setText(mod.description);

        QString envInfo = "Client: " + (mod.clientSide.isEmpty() ? "required" : mod.clientSide)
                        + " | Server: " + (mod.serverSide.isEmpty() ? "required" : mod.serverSide);
        m_modEnvLabel->setText(envInfo);

        // Show conflict if any
        if (m_modConflicts.contains(mod.projectId)) {
            const ConflictWarning& cw = m_modConflicts[mod.projectId];
            QString conflictOther;
            if (cw.modA == mod.name) conflictOther = cw.modB;
            else conflictOther = cw.modA;
            m_modConflictLabel->setText(QString("⚠️ CONFLICT: This mod conflicts with \"%1\".\n%2")
                .arg(conflictOther, cw.reason));
            m_modConflictLabel->show();
        } else {
            m_modConflictLabel->hide();
        }

        // Load icon — icon_url is already available from getModInfo()
        if (!mod.iconUrl.isEmpty()) {
            m_modIconLabel->setText("Loading...");
            QThreadPool::globalInstance()->start([this, mod]() {
                m_rateLimiter.wait();
                QNetworkAccessManager manager;
                QEventLoop loop;

                QNetworkRequest iconReq(QUrl(mod.iconUrl));
                iconReq.setHeader(QNetworkRequest::UserAgentHeader,
                    "helloworldx64/CraftPacker/3.0.0");
                QNetworkReply* iconReply = manager.get(iconReq);
                QObject::connect(iconReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
                loop.exec();

                if (iconReply->error() == QNetworkReply::NoError) {
                    QPixmap pm;
                    pm.loadFromData(iconReply->readAll());
                    if (!pm.isNull()) {
                        emit sigImageFound(mod.projectId, pm);
                    }
                }
                iconReply->deleteLater();
            });
        } else {
            m_modIconLabel->setText("No Icon");
        }
    } else {
        // Not found mod - show the entry info
        if (m_modEntries.contains(lookupKey)) {
            const ModEntry& entry = m_modEntries[lookupKey];
            m_modTitleLabel->setText(entry.originalName);
            m_modAuthorLabel->setText("");
            m_modEnvLabel->setText("");
            m_modSummaryText->setText("Mod was not found via Modrinth API.\nReason: " + entry.status);
            m_modIconLabel->setText("❌ Not Found");

            // Still show conflict if this not-found mod is in a conflict
            if (m_modConflicts.contains(lookupKey)) {
                const ConflictWarning& cw = m_modConflicts[lookupKey];
                QString conflictOther;
                if (cw.modA == entry.originalName) conflictOther = cw.modB;
                else conflictOther = cw.modA;
                m_modConflictLabel->setText(QString("⚠️ CONFLICT: \"%1\" is incompatible with \"%2\".\n%3")
                    .arg(entry.originalName, conflictOther, cw.reason));
                m_modConflictLabel->show();
            } else {
                m_modConflictLabel->hide();
            }
        }
    }
}

void CraftPacker::onImageFound(const QString& projectId, const QPixmap& pixmap) {
    auto* currentItem = m_resultsTree->currentItem();
    if (!currentItem) return;
    QString key = currentItem->data(0, Qt::UserRole).toString();
    if (m_results.contains(key) && m_results[key].projectId == projectId) {
        if (!pixmap.isNull()) {
            m_modIconLabel->setPixmap(pixmap.scaled(
                m_modIconLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            m_modIconLabel->setText("No Icon");
        }
    }
}

// ============================================================
// CONTEXT MENU - Unified for single table
// ============================================================
void CraftPacker::showResultsContextMenu(const QPoint &pos) {
    QTreeWidgetItem* item = m_resultsTree->itemAt(pos);
    if (!item) return;

    QString lookupKey = item->data(0, Qt::UserRole).toString();

    QMenu contextMenu(this);

    if (m_results.contains(lookupKey)) {
        ModInfo mod = m_results[lookupKey];
        contextMenu.addAction("Open on Modrinth", [mod]() {
            QDesktopServices::openUrl(QUrl("https://modrinth.com/mod/" + mod.projectId));
        });
        contextMenu.addAction("Copy Name", [mod]() {
            QApplication::clipboard()->setText(mod.name);
        });
    } else {
        contextMenu.addAction("Copy Name", [item]() {
            QApplication::clipboard()->setText(item->text(0));
        });
        contextMenu.addAction("Search on CurseForge", [this, item]() {
            QUrlQuery query;
            query.addQueryItem("q", "site:curseforge.com/minecraft/mc-mods " + item->text(0));
            QUrl url("https://google.com/search");
            url.setQuery(query);
            QDesktopServices::openUrl(url);
        });
    }

    contextMenu.addSeparator();
    contextMenu.addAction("Copy All Names", [this]() {
        QStringList names;
        for (int i = 0; i < m_resultsTree->topLevelItemCount(); ++i)
            names.append(m_resultsTree->topLevelItem(i)->text(0));
        QApplication::clipboard()->setText(names.join('\n'));
    });

    contextMenu.exec(m_resultsTree->mapToGlobal(pos));
}

// ============================================================
// DEBUG LOCAL FOLDER (Phase 6: Modpack Debugger)
// ============================================================
void CraftPacker::openDebugger() {
    QString folderPath = QFileDialog::getExistingDirectory(this,
        "Select mods folder to debug",
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
    if (folderPath.isEmpty()) return;

    auto* dashboard = new DebuggerDashboard(folderPath, this);
    dashboard->setAttribute(Qt::WA_DeleteOnClose);
    dashboard->show();
}

// ============================================================
// UI HELPERS
// ============================================================
void CraftPacker::updateStatusBar(const QString& text) {
    m_statusLabel->setText(text);
    statusBar()->showMessage(text, 5000);
}
void CraftPacker::setButtonsEnabled(bool enabled) {
    for (auto* btn : m_actionButtons) btn->setEnabled(enabled);
    m_searchButton->setEnabled(enabled);
    m_researchButton->setEnabled(enabled);
    m_downloadSelectedButton->setEnabled(enabled);
    m_downloadAllButton->setEnabled(enabled);
}
void CraftPacker::browseDirectory() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Download Directory");
    if (!dir.isEmpty()) m_dirEntry->setText(dir);
}

// ============================================================
// EVENT HANDLERS (Drag & Drop, Close)
// ============================================================
void CraftPacker::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}
void CraftPacker::dropEvent(QDropEvent* event) {
    QStringList paths;
    for (const auto& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) paths.append(url.toLocalFile());
    }
    // If folder dropped, check for mods/*.jar pattern
    QStringList lines;
    for (const auto& path : paths) {
        QDir dir(path);
        if (dir.exists()) {
            // Check for mods/ subfolder pattern
            QDir modsDir(path + "/mods");
            if (modsDir.exists()) {
                QDirIterator it(modsDir.absolutePath(), {"*.jar"});
                while (it.hasNext()) {
                    it.next();
                    lines.append(it.fileName());
                }
            } else {
                // Direct jar files
                QDirIterator it(path, {"*.jar"});
                while (it.hasNext()) {
                    it.next();
                    lines.append(it.fileName());
                }
            }
        } else if (path.endsWith(".jar")) {
            lines.append(QFileInfo(path).fileName());
        }
    }
    if (!lines.isEmpty()) {
        QString existing = m_modlistInput->toPlainText();
        if (!existing.isEmpty()) existing += "\n";
        m_modlistInput->setText(existing + lines.join('\n'));
        updateStatusBar(QString("📥 Dropped %1 mod file(s)").arg(lines.size()));
    }
}
void CraftPacker::closeEvent(QCloseEvent* event) {
    // Save window state
    m_settings->setValue("windowGeometry", saveGeometry());
    m_settings->setValue("windowState", saveState());
    QMainWindow::closeEvent(event);
}

// ============================================================
// URL OPENERS
// ============================================================
void CraftPacker::openGitHub() { QDesktopServices::openUrl(QUrl("https://github.com/helloworldx64/CraftPacker")); }
void CraftPacker::openPayPal() { QDesktopServices::openUrl(QUrl("https://www.paypal.com/donate/?business=4UZWFGSW6C478&no_recurring=0&item_name=Donate+to+helloworldx64¤cy_code=USD")); }

// ============================================================
// main()
// ============================================================
int main(int argc, char *argv[]) {
    QCoreApplication::setOrganizationName("CraftPacker");
    QCoreApplication::setApplicationName("CraftPacker-v3");
    QCoreApplication::setApplicationVersion("3.0.0");

    QApplication app(argc, argv);
    app.setWindowIcon(QIcon());

    ThemeManager::ApplyTheme(&app);

    CraftPacker w;
    w.show();

    return app.exec();
}