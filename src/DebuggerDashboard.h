#ifndef DEBUGGER_DASHBOARD_H
#define DEBUGGER_DASHBOARD_H

#include <QDialog>
#include <QTreeWidget>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>

#include "FolderDebugger.h"

// ============================================================
// DebuggerDashboard: Popup dialog for "Debug Local Folder"
// Runs FolderDebugger in background, displays results live
// ============================================================
class DebuggerDashboard : public QDialog {
    Q_OBJECT

public:
    explicit DebuggerDashboard(const QString& folderPath, QWidget *parent = nullptr);
    ~DebuggerDashboard() override;

private slots:
    void onScanProgress(int current, int total, const QString& currentFile);
    void onModResult(const DebuggerModResult& result);
    void onCollisionFound(const ClassCollision& collision);
    void onSummaryReady(const DebuggerSummary& summary);
    void onScanFinished();
    void onScanError(const QString& error);
    void onCancelClicked();

private:
    void setupUi();
    QTreeWidgetItem* addResultItem(const DebuggerModResult& result);
    QColor colorForIssues(DebuggerModResult::Issues issues) const;

    QString m_folderPath;
    FolderDebugger* m_debugger;

    // UI elements
    QLabel* m_summaryHeader;
    QProgressBar* m_progressBar;
    QLabel* m_progressLabel;
    QTreeWidget* m_resultsTree;
    QTreeWidget* m_collisionsTree;
    QLabel* m_collisionCountLabel;
    QPushButton* m_cancelButton;
    QPushButton* m_closeButton;
};

#endif // DEBUGGER_DASHBOARD_H