#ifndef CRAFTPACKER_V3_H
#define CRAFTPACKER_V3_H

#include <QMainWindow>
#include <QObject>
#include <QJsonObject>
#include <QTreeWidget>
#include <QListWidget>
#include <QMutex>
#include <QAtomicInt>
#include <QThread>
#include <QVector>
#include <QSettings>
#include <optional>
#include <chrono>

// Forward declarations
QT_BEGIN_NAMESPACE
class QLineEdit;
class QComboBox;
class QTextEdit;
class QPushButton;
class QLabel;
class QPropertyAnimation;
class QSplitter;
class QMenu;
class QMenuBar;
class QProgressBar;
class QTabWidget;
QT_END_NAMESPACE

#include "src/ModrinthAPI.h"
#include "src/CurseForgeAPI.h"
#include "src/CacheManager.h"
#include "src/ConflictDetector.h"
#include "src/PackExporter.h"
#include "src/UpdateChecker.h"
#include "src/StringCleaner.h"

// Rate limiter for API calls
class RateLimiter {
public:
    explicit RateLimiter(int callsPerMinute);
    void wait();
private:
    std::chrono::milliseconds m_minInterval;
    QMutex m_mutex;
    std::chrono::steady_clock::time_point m_lastCall;
};

// Download worker: runs in background thread, downloads a single file
class DownloadWorker : public QObject {
    Q_OBJECT
public:
    DownloadWorker(ModInfo mod, QString dir);
    ~DownloadWorker() = default;
public slots:
    void process();
signals:
    void progress(const QString& iid, qint64 bytesReceived, qint64 bytesTotal);
    void finished(const QString& iid, const QString& error);
private:
    ModInfo m_modInfo;
    QString m_downloadDir;
};

// Image search worker: fetches mod icons via web search
class ImageSearchWorker : public QObject {
    Q_OBJECT
public:
    ImageSearchWorker(QString modName, QString projectId);
public slots:
    void process();
signals:
    void imageFound(const QString& projectId, const QPixmap& pixmap);
private:
    QString m_modName;
    QString m_projectId;
};

// Struct for tracking original input mod states
struct ModEntry {
    QString originalName;       // Raw original input
    QString cleanName;          // Bracket-stripped + sanitized
    QString status;             // Available / Not Found / Wrong Version / Wrong Loader / CONFLICT
    QString reason;             // Detailed reason text
    bool isConflict = false;
    ModInfo modInfo;            // Populated only if found
};

// ============================================================
// CraftPacker v3 - Main Application Window
// ============================================================
class CraftPacker : public QMainWindow {
    Q_OBJECT

public:
    explicit CraftPacker(QWidget *parent = nullptr);
    ~CraftPacker() override;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

    // ================ SIGNALS (DIFFERENT NAMES FROM SLOTS) ================
signals:
    void sigModFound(const ModInfo& modInfo, const QString& status, const QString& tag);
    void sigModNotFound(const QString& modName, const QString& reason);
    void sigSearchFinished();
    void sigUpdateStatusBar(const QString& text);
    void sigAllDownloadsFinished();
    void sigDependencyResolutionFinished(const QList<ModInfo>& downloadQueue, const QString& dlDir);
    void sigImageFound(const QString& projectId, const QPixmap& pixmap);

    // ================ PRIVATE SLOTS ================
private slots:
    // Search
    void startSearch();
    void startReSearch();

    // Results handling
    void onModFound(const ModInfo& modInfo, const QString& status, const QString& tag);
    void onModNotFound(const QString& modName, const QString& reason);
    void onSearchFinished();

    // Downloads
    void startDownloadSelected();
    void startDownloadAll();
    void onDownloadProgress(const QString& iid, qint64 bytesReceived, qint64 bytesTotal);
    void onDownloadFinished(const QString& iid, const QString& error);
    void onAllDownloadsFinished();

    // Dependencies
    void onDependencyResolutionFinished(const QList<ModInfo>& downloadQueue, const QString& dlDir);

    // Navigation / UI
    void browseDirectory();
    void importFromFolder();
    void openGitHub();
    void openPayPal();
    void openSettingsDialog();

    // Profiles
    void saveProfile();
    void loadProfile();
    void deleteProfile();
    void loadProfileList();

    // Mod details panel
    void updateModInfoPanel(QTreeWidgetItem* current, QTreeWidgetItem* previous);
    void onImageFound(const QString& projectId, const QPixmap& pixmap);

    // Context menus
    void showResultsContextMenu(const QPoint &pos);

    // Debug (Phase 6)
    void openDebugger();

    // Export (Phase 3)
    void exportToMrpack();
    void exportToCurseForge();
    void generateServerPack();

    // Updates (Phase 5)
    void checkForUpdates();
    void onUpdatesChecked(const QVector<UpdateStatus>& results);
    void migrateProfile();

    // UI helpers
    void updateStatusBar(const QString& text);
    void setButtonsEnabled(bool enabled);
    void applySettings();

    // Animations
    void runCompletionAnimation(QTreeWidgetItem* item);
    void runSwooshAnimation(QTreeWidgetItem* item);
    void runJumpAnimation(QWidget* widget);

private:
    void setupUi();
    void startModSearch(const QStringList& modNames);
    void findOneMod(QString name, QString loader, QString version);
    void startDownload(const QList<QString>& itemIds);
    std::optional<ModInfo> getModInfo(const QString& projectIdOrSlug,
                                       const QString& loader,
                                       const QString& gameVersion);
    std::optional<ModInfo> resolveDependencyLocally(const QString& projectId,
                                                      const QString& loader,
                                                      const QString& gameVersion);
    // Run conflict detection against ALL input mods (including not-found)
    void runConflictDetection();

    // UI elements
    QLineEdit* m_mcVersionEntry;
    QComboBox* m_loaderComboBox;
    QLineEdit* m_dirEntry;
    QTextEdit* m_modlistInput;
    // SINGLE unified results tree
    QTreeWidget* m_resultsTree;
    QPushButton* m_searchButton;
    QPushButton* m_researchButton;
    QPushButton* m_downloadSelectedButton;
    QPushButton* m_downloadAllButton;
    QPushButton* m_exportMrpackButton;
    QPushButton* m_exportCfButton;
    QPushButton* m_serverPackButton;
    QPushButton* m_checkUpdatesButton;
    QLabel* m_statusLabel;
    QList<QPushButton*> m_actionButtons;

    // Profile management
    QListWidget* m_profileListWidget;
    QPushButton* m_loadProfileButton;
    QPushButton* m_saveProfileButton;
    QPushButton* m_deleteProfileButton;

    // Layout
    QSplitter* m_mainSplitter;
    QTabWidget* m_taskManagerTab;

    // Mod details panel
    QWidget* m_modInfoPanel;
    QLabel* m_modIconLabel;
    QLabel* m_modTitleLabel;
    QLabel* m_modAuthorLabel;
    QTextEdit* m_modSummaryText;
    QLabel* m_modEnvLabel;
    QLabel* m_modConflictLabel;

    // Task manager (Phase 6: Rich Progress)
    QTreeWidget* m_taskTree;
    QLabel* m_overallProgressLabel;
    QProgressBar* m_overallProgressBar;
    QLabel* m_speedLabel;

    // Conflict tracking for details panel
    QHash<QString, ConflictWarning> m_modConflicts; // projectId -> conflict info

    // Core state
    QSettings* m_settings;
    RateLimiter m_rateLimiter{300};
    // Stores ALL original input names -> ModEntry
    QHash<QString, ModEntry> m_modEntries;   // originalName -> entry
    QHash<QString, QTreeWidgetItem*> m_treeItems; // query key -> item
    // Storage for found mods (projectId -> ModInfo)
    QHash<QString, ModInfo> m_results;
    QHash<QString, QTreeWidgetItem*> m_taskTreeItems;
    QAtomicInt m_activeDownloads{0};
    QSet<QString> m_allFoundOrDependencyProjects;
    QAtomicInt m_searchCounter{0};
    QMutex m_searchMutex;
    QString m_profilePath;
    qint64 m_lastBytesReceived{0};
    QDateTime m_lastSpeedTime;
    int m_animationCount{0};
    QString m_downloadDir;
};

#endif // CRAFTPACKER_V3_H