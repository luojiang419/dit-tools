#pragma once

#include <QFutureSynchronizer>
#include <QObject>
#include <QStringList>

class AppSettings;
class QuickSearchController;
class UpdateService;
class VideoAnalysisService;
class VisionApiClient;
class LocalSearchAssistantRuntime;
class SearchAssistantLifecycleController;

class SettingsViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString visionBaseUrl READ visionBaseUrl WRITE setVisionBaseUrl NOTIFY settingsChanged)
    Q_PROPERTY(QString visionApiKey READ visionApiKey WRITE setVisionApiKey NOTIFY settingsChanged)
    Q_PROPERTY(QString visionModel READ visionModel WRITE setVisionModel NOTIFY settingsChanged)
    Q_PROPERTY(QStringList customAnalysisDimensions READ customAnalysisDimensions NOTIFY settingsChanged)
    Q_PROPERTY(bool searchAssistantEnabled READ searchAssistantEnabled WRITE setSearchAssistantEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int searchAssistantAutoUnloadMinutes READ searchAssistantAutoUnloadMinutes WRITE setSearchAssistantAutoUnloadMinutes NOTIFY settingsChanged)
    Q_PROPERTY(QString localSearchAssistantStatusText READ localSearchAssistantStatusText NOTIFY settingsChanged)
    Q_PROPERTY(bool quickSearchEnabled READ quickSearchEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QString quickSearchShortcut READ quickSearchShortcut NOTIFY settingsChanged)
    Q_PROPERTY(bool startAtLogin READ startAtLogin NOTIFY settingsChanged)
    Q_PROPERTY(QString quickSearchStatusText READ quickSearchStatusText NOTIFY settingsChanged)
    Q_PROPERTY(int analysisMode READ analysisMode WRITE setAnalysisMode NOTIFY settingsChanged)
    Q_PROPERTY(int frameInterval READ frameInterval WRITE setFrameInterval NOTIFY settingsChanged)
    Q_PROPERTY(int thumbnailFrameIndex READ thumbnailFrameIndex WRITE setThumbnailFrameIndex NOTIFY settingsChanged)
    Q_PROPERTY(int contactSheetFrameCount READ contactSheetFrameCount WRITE setContactSheetFrameCount NOTIFY settingsChanged)
    Q_PROPERTY(int analysisTimeoutSec READ analysisTimeoutSec WRITE setAnalysisTimeoutSec NOTIFY settingsChanged)
    Q_PROPERTY(bool documentAutoAnalysisEnabled READ documentAutoAnalysisEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool photoshopAutoAnalysisEnabled READ photoshopAutoAnalysisEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int themeMode READ themeMode WRITE setThemeMode NOTIFY settingsChanged)
    Q_PROPERTY(int closeButtonBehavior READ closeButtonBehavior WRITE setCloseButtonBehavior NOTIFY settingsChanged)
    Q_PROPERTY(bool updateBusy READ updateBusy NOTIFY settingsChanged)
    Q_PROPERTY(int updatePolicy READ updatePolicy WRITE setUpdatePolicy NOTIFY settingsChanged)
    Q_PROPERTY(QString currentVersionLabel READ currentVersionLabel NOTIFY settingsChanged)
    Q_PROPERTY(int updateDownloadMode READ updateDownloadMode WRITE setUpdateDownloadMode NOTIFY settingsChanged)
    Q_PROPERTY(QString updateManualProxyUrl READ updateManualProxyUrl WRITE setUpdateManualProxyUrl NOTIFY settingsChanged)
    Q_PROPERTY(QString dataRootPath READ dataRootPath NOTIFY settingsChanged)
    Q_PROPERTY(QString frameCacheSizeLabel READ frameCacheSizeLabel NOTIFY settingsChanged)
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY settingsChanged)

public:
    explicit SettingsViewModel(AppSettings *settings,
                               VisionApiClient *visionApiClient,
                               VideoAnalysisService *videoAnalysisService,
                               UpdateService *updateService,
                               QuickSearchController *quickSearchController,
                               LocalSearchAssistantRuntime *localSearchAssistantRuntime,
                               SearchAssistantLifecycleController *searchAssistantLifecycleController,
                               QObject *parent = nullptr);
    ~SettingsViewModel() override;
    void waitForIdle();

    QString visionBaseUrl() const;
    void setVisionBaseUrl(const QString &value);
    QString visionApiKey() const;
    void setVisionApiKey(const QString &value);
    QString visionModel() const;
    void setVisionModel(const QString &value);
    QStringList customAnalysisDimensions() const;
    bool searchAssistantEnabled() const;
    void setSearchAssistantEnabled(bool enabled);
    int searchAssistantAutoUnloadMinutes() const;
    void setSearchAssistantAutoUnloadMinutes(int minutes);
    QString localSearchAssistantStatusText() const;
    bool quickSearchEnabled() const;
    QString quickSearchShortcut() const;
    bool startAtLogin() const;
    QString quickSearchStatusText() const;
    int analysisMode() const;
    void setAnalysisMode(int value);
    int frameInterval() const;
    void setFrameInterval(int value);
    int thumbnailFrameIndex() const;
    void setThumbnailFrameIndex(int value);
    int contactSheetFrameCount() const;
    void setContactSheetFrameCount(int value);
    int analysisTimeoutSec() const;
    void setAnalysisTimeoutSec(int value);
    bool documentAutoAnalysisEnabled() const;
    bool photoshopAutoAnalysisEnabled() const;
    int themeMode() const;
    void setThemeMode(int value);
    int closeButtonBehavior() const;
    void setCloseButtonBehavior(int value);
    bool updateBusy() const;
    int updatePolicy() const;
    void setUpdatePolicy(int value);
    QString currentVersionLabel() const;
    int updateDownloadMode() const;
    void setUpdateDownloadMode(int value);
    QString updateManualProxyUrl() const;
    void setUpdateManualProxyUrl(const QString &value);
    QString dataRootPath() const;
    QString frameCacheSizeLabel() const;
    QString lastMessage() const;

    Q_INVOKABLE void beginStartupUpdateFlow();
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void saveUpdateDownloadSettings(int updatePolicy,
                                                int updateDownloadMode,
                                                const QString &updateManualProxyUrl);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshCacheInfo();
    Q_INVOKABLE void testConnection();
    Q_INVOKABLE void testConnectionWith(const QString &visionBaseUrl,
                                        const QString &visionApiKey,
                                        const QString &visionModel,
                                        int analysisTimeoutSec);
    Q_INVOKABLE bool addCustomAnalysisDimension(const QString &name);
    Q_INVOKABLE bool removeCustomAnalysisDimension(const QString &name);
    Q_INVOKABLE QString shortcutFromKeyEvent(int key, int modifiers) const;
    Q_INVOKABLE void saveAndApply(const QString &visionBaseUrl,
                                  const QString &visionApiKey,
                                  const QString &visionModel,
                                  bool searchAssistantEnabled,
                                  int searchAssistantAutoUnloadMinutes,
                                  bool quickSearchEnabled,
                                  const QString &quickSearchShortcut,
                                  bool startAtLogin,
                                  int closeButtonBehavior,
                                  int analysisMode,
                                  int frameInterval,
                                  int thumbnailFrameIndex,
                                   int contactSheetFrameCount,
                                   int analysisTimeoutSec,
                                   bool documentAutoAnalysisEnabled,
                                   bool photoshopAutoAnalysisEnabled,
                                   int updatePolicy,
                                   int updateDownloadMode,
                                  const QString &updateManualProxyUrl);

signals:
    void settingsChanged();
    void searchSettingsChanged();

private:
    void setLastMessage(const QString &message);
    void promptInstallUpdate(const QString &versionTag);

    AppSettings *m_settings = nullptr;
    VisionApiClient *m_visionApiClient = nullptr;
    VideoAnalysisService *m_videoAnalysisService = nullptr;
    UpdateService *m_updateService = nullptr;
    QuickSearchController *m_quickSearchController = nullptr;
    LocalSearchAssistantRuntime *m_localSearchAssistantRuntime = nullptr;
    SearchAssistantLifecycleController *m_searchAssistantLifecycleController = nullptr;
    QFutureSynchronizer<void> m_futures;
    QString m_frameCacheSizeLabel;
    QString m_lastMessage;
};
