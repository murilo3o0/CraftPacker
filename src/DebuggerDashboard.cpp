#include "DebuggerDashboard.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSplitter>
#include <QGroupBox>
#include <QApplication>
#include <QStyle>
#include <QFileInfo>
#include <QThreadPool>

DebuggerDashboard::DebuggerDashboard(const QString& folderPath, QWidget *parent)
    : QDialog(parent), m_folderPath(folderPath)
{
    m_debugger = new FolderDebugger(this);
    setupUi();
    setWindowTitle("Debug Local Folder: " + QFileInfo(folderPath).fileName());
    setMinimumSize(1200, 700);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    // Connect FolderDebugger signals
    connect(m_debugger, &FolderDebugger::scanProgress, this, &DebuggerDashboard::onScanProgress);
    connect(m_debugger, &FolderDebugger::modResultReady, this, &DebuggerDashboard::onModResult);
    connect(m_debugger, &FolderDebugger::collisionFound, this, &DebuggerDashboard::onCollisionFound);
    connect(m_debugger, &FolderDebugger::summaryReady, this, &DebuggerDashboard::onSummaryReady);
    connect(m_debugger, &FolderDebugger::scanFinished, this, &DebuggerDashboard::onScanFinished);
    connect(m_debugger, &FolderDebugger::scanError, this, &DebuggerDashboard::onScanError);

    // Run in background thread using QThreadPool (no QtConcurrent dependency)
    m_summaryHeader->setText("🔍 Scanning: 0 mods found...");
    FolderDebugger* debugger = m_debugger;
    QString fp = m_folderPath;
    QThreadPool::globalInstance()->start([debugger, fp]() {
        debugger->debugFolder(fp);
    });
}

DebuggerDashboard::~DebuggerDashboard() = default;

void DebuggerDashboard::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);

    // Summary header
    m_summaryHeader = new QLabel("Select a folder to scan...");
    m_summaryHeader->setStyleSheet(
        "font-size: 14pt; font-weight: bold; padding: 8px;"
        "background-color: #1e1e3a; border-radius: 6px;");
    mainLayout->addWidget(m_summaryHeader);

    // Progress bar
    auto* progressLayout = new QHBoxLayout();
    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_progressLabel = new QLabel("Initializing...");
    m_progressLabel->setStyleSheet("color: #8a8aaa;");
    progressLayout->addWidget(m_progressBar, 1);
    progressLayout->addWidget(m_progressLabel);
    mainLayout->addLayout(progressLayout);

    // Main splitter: Results + Collisions
    auto* splitter = new QSplitter(Qt::Vertical);

    // Results Tree (JAR analysis)
    auto* resultsGroup = new QGroupBox("Mod Analysis Results");
    auto* resultsLayout = new QVBoxLayout(resultsGroup);
    m_resultsTree = new QTreeWidget();
    m_resultsTree->setColumnCount(8);
    m_resultsTree->setHeaderLabels({"JAR File", "Size", "Loader", "API Name",
                                    "Project ID", "Issues", "Severity", "Details"});
    m_resultsTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_resultsTree->header()->setSectionResizeMode(6, QHeaderView::Stretch);
    m_resultsTree->setAlternatingRowColors(true);
    m_resultsTree->setRootIsDecorated(false);
    resultsLayout->addWidget(m_resultsTree);
    splitter->addWidget(resultsGroup);

    // Collisions Tree (class collisions)
    auto* collisionGroup = new QGroupBox("Class Collision Scanner");
    auto* collisionLayout = new QVBoxLayout(collisionGroup);
    m_collisionCountLabel = new QLabel("Class collisions: Scanning...");
    m_collisionCountLabel->setStyleSheet("font-weight: bold; color: #ff6666;");
    collisionLayout->addWidget(m_collisionCountLabel);

    m_collisionsTree = new QTreeWidget();
    m_collisionsTree->setColumnCount(3);
    m_collisionsTree->setHeaderLabels({"Class Path", "Conflicting Mods", "Count"});
    m_collisionsTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_collisionsTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_collisionsTree->setAlternatingRowColors(true);
    m_collisionsTree->setRootIsDecorated(false);
    collisionLayout->addWidget(m_collisionsTree);
    splitter->addWidget(collisionGroup);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    mainLayout->addWidget(splitter, 1);

    // Buttons
    auto* buttonLayout = new QHBoxLayout();
    m_cancelButton = new QPushButton("Cancel Scan");
    m_closeButton = new QPushButton("Close");
    m_closeButton->setEnabled(false);
    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_closeButton);
    mainLayout->addLayout(buttonLayout);

    connect(m_cancelButton, &QPushButton::clicked, this, &DebuggerDashboard::onCancelClicked);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

QColor DebuggerDashboard::colorForIssues(DebuggerModResult::Issues issues) const {
    if (issues & DebuggerModResult::DuplicateMod) return QColor("#ff4444");
    if (issues & DebuggerModResult::LoaderMismatch) return QColor("#ff8844");
    if (issues & DebuggerModResult::MissingApi) return QColor("#ffff44");
    if (issues & DebuggerModResult::ClassCollision) return QColor("#ff6666");
    if (issues & DebuggerModResult::ApiNotFound) return QColor("#aaaaaa");
    return QColor(); // default (transparent = no styling)
}

QTreeWidgetItem* DebuggerDashboard::addResultItem(const DebuggerModResult& result) {
    auto* item = new QTreeWidgetItem();
    item->setText(0, result.jarFileName);
    item->setText(1, result.fileSize > 1024
        ? QString::number(result.fileSize / 1024) + " KB"
        : QString::number(result.fileSize) + " B");
    item->setText(2, result.detectedLoader);
    item->setText(3, result.apiProjectName.isEmpty() ? "—" : result.apiProjectName);
    item->setText(4, result.apiProjectId.isEmpty() ? "—" : result.apiProjectId);

    // Issues column
    QStringList issueList;
    if (result.issues & DebuggerModResult::DuplicateMod) issueList << "DUPLICATE";
    if (result.issues & DebuggerModResult::LoaderMismatch) issueList << "LOADER MISMATCH";
    if (result.issues & DebuggerModResult::ClassCollision) issueList << "CLASS COLLISION";
    if (result.issues & DebuggerModResult::MissingApi) issueList << "MISSING API";
    if (result.issues & DebuggerModResult::ApiNotFound) issueList << "NOT ON MODRINTH";
    item->setText(5, issueList.isEmpty() ? "OK" : issueList.join(", "));

    // Severity column
    QString severity = result.severityText();
    item->setText(6, severity);
    if (severity == "Error") {
        item->setForeground(6, QColor("#ff4444"));
    } else if (severity == "Warning") {
        item->setForeground(6, QColor("#ffaa44"));
    } else if (severity == "Info") {
        item->setForeground(6, QColor("#8a8aaa"));
    } else {
        item->setForeground(6, QColor("#44ff44"));
    }

    // Details column
    item->setText(7, result.reasonText.isEmpty() ? "—" : result.reasonText);

    // Color coding
    QColor bg = colorForIssues(result.issues);
    if (bg.isValid()) {
        for (int i = 0; i < item->columnCount(); ++i) {
            item->setBackground(i, bg);
            if (qGray(bg.rgb()) < 128) item->setForeground(i, Qt::white);
        }
    }

    m_resultsTree->addTopLevelItem(item);
    return item;
}

void DebuggerDashboard::onScanProgress(int current, int total, const QString& currentFile) {
    m_progressBar->setMaximum(total);
    m_progressBar->setValue(current);
    m_progressLabel->setText(currentFile);
    m_summaryHeader->setText(
        QString("🔍 Analyzing %1 mods... (#%2/%3)")
            .arg(currentFile).arg(current).arg(total));
}

void DebuggerDashboard::onModResult(const DebuggerModResult& result) {
    addResultItem(result);
}

void DebuggerDashboard::onCollisionFound(const ClassCollision& collision) {
    auto* item = new QTreeWidgetItem();
    item->setText(0, collision.classPath);
    item->setText(1, collision.conflictingJars.join(", "));
    item->setText(2, QString::number(collision.conflictingJars.size()));
    item->setForeground(0, QColor("#ff6666"));
    m_collisionsTree->addTopLevelItem(item);
}

void DebuggerDashboard::onSummaryReady(const DebuggerSummary& summary) {
    QString loaderSummary = summary.dominantLoader;
    if (summary.fabricCount > 0 && summary.forgeCount > 0)
        loaderSummary = "MIXED (Fabric=" + QString::number(summary.fabricCount) +
                       ", Forge=" + QString::number(summary.forgeCount) +
                       ", NeoForge=" + QString::number(summary.neoforgeCount) + ")";

    m_summaryHeader->setText(
        QString("📊 Scanned %1 Mods | %2 | %3 Fabric, %4 Forge, %5 NeoForge | %6 Issues Found")
            .arg(summary.totalJars)
            .arg(loaderSummary)
            .arg(summary.fabricCount)
            .arg(summary.forgeCount)
            .arg(summary.neoforgeCount)
            .arg(summary.issueCount));

    m_collisionCountLabel->setText(
        QString("🔬 Class Collisions: %1 found | %2 duplicates detected")
            .arg(summary.collisionCount)
            .arg(summary.duplicateCount));

    // Color the header based on issues
    if (summary.issueCount > 0) {
        m_summaryHeader->setStyleSheet(
            "font-size: 14pt; font-weight: bold; padding: 8px;"
            "background-color: #442222; border: 1px solid #ff4444; border-radius: 6px;"
            "color: #ff8888;");
    } else {
        m_summaryHeader->setStyleSheet(
            "font-size: 14pt; font-weight: bold; padding: 8px;"
            "background-color: #224422; border: 1px solid #44ff44; border-radius: 6px;"
            "color: #88ff88;");
    }

    m_progressLabel->setText("Done");
    m_progressBar->setValue(m_progressBar->maximum());
}

void DebuggerDashboard::onScanFinished() {
    m_cancelButton->setEnabled(false);
    m_closeButton->setEnabled(true);
    m_cancelButton->setText("Scan Complete");
}

void DebuggerDashboard::onScanError(const QString& error) {
    m_summaryHeader->setText("❌ " + error);
    m_summaryHeader->setStyleSheet(
        "font-size: 14pt; font-weight: bold; padding: 8px;"
        "background-color: #552222; color: #ff6666; border-radius: 6px;");
    m_cancelButton->setEnabled(false);
    m_closeButton->setEnabled(true);
}

void DebuggerDashboard::onCancelClicked() {
    m_debugger->cancel();
    m_cancelButton->setEnabled(false);
    m_cancelButton->setText("Cancelling...");
}