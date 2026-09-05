#pragma once

#include "domain/Enums.h"

#include <QPoint>
#include <QList>
#include <QStringList>

class QSettings;

struct VisionApiConfig {
    QString id;
    QString name;
    QString baseUrl;
    QString apiKey;
    QString model;
};

class AppSettings {
public:
    AppSettings();
    ~AppSettings();

    QStringList recentProjects() const;
    void addRecentProject(const QString &projectPath);
    QStringList knownProjects() const;
    void addKnownProject(const QString &projectPath);
    void removeKnownProject(const QString &projectPath);
    void replaceProjectPath(const QString &oldProjectPath, const QString &newProjectPath);

    QString visionBaseUrl() const;
    void setVisionBaseUrl(const QString &value);

    QString visionApiKey() const;
    void setVisionApiKey(const QString &value);

    QString visionModel() const;
    void setVisionModel(const QString &value);

    QList<VisionApiConfig> visionApiConfigs() const;
    QString activeVisionApiConfigId() const;
    void setVisionApiConfigs(const QList<VisionApiConfig> &configs,
                             const QString &activeConfigId);

    QStringList customAnalysisDimensions() const;
    void setCustomAnalysisDimensions(const QStringList &values);

    bool searchAssistantEnabled() const;
    void setSearchAssistantEnabled(bool enabled);

    int searchAssistantAutoUnloadMinutes() const;
    void setSearchAssistantAutoUnloadMinutes(int minutes);

    bool quickSearchEnabled() const;
    void setQuickSearchEnabled(bool enabled);

    QString quickSearchShortcut() const;
    void setQuickSearchShortcut(const QString &shortcut);

    QStringList reportEnabledSections() const;
    void setReportEnabledSections(const QStringList &sections);

    bool hasQuickSearchWindowPosition() const;
    QPoint quickSearchWindowPosition() const;
    void setQuickSearchWindowPosition(const QPoint &position);

    bool startAtLogin() const;
    void setStartAtLogin(bool enabled);

    AnalysisMode analysisMode() const;
    void setAnalysisMode(AnalysisMode mode);

    int frameInterval() const;
    void setFrameInterval(int value);

    VideoFrameExtractionStrategy videoFrameExtractionStrategy() const;
    void setVideoFrameExtractionStrategy(VideoFrameExtractionStrategy value);

    double videoFrameIntervalSeconds() const;
    void setVideoFrameIntervalSeconds(double value);

    double videoSceneThreshold() const;
    void setVideoSceneThreshold(double value);

    double videoMinimumSharpness() const;
    void setVideoMinimumSharpness(double value);

    int thumbnailFrameIndex() const;
    void setThumbnailFrameIndex(int value);

    int contactSheetFrameCount() const;
    void setContactSheetFrameCount(int value);

    int analysisTimeoutSec() const;
    void setAnalysisTimeoutSec(int value);

    bool documentAutoAnalysisEnabled() const;
    void setDocumentAutoAnalysisEnabled(bool enabled);

    bool photoshopAutoAnalysisEnabled() const;
    void setPhotoshopAutoAnalysisEnabled(bool enabled);

    int themeMode() const;
    void setThemeMode(int value);

    int closeButtonBehavior() const;
    void setCloseButtonBehavior(int value);

    QString pendingUpdateVersion() const;
    void setPendingUpdateVersion(const QString &value);

    QString pendingUpdateInstallerPath() const;
    void setPendingUpdateInstallerPath(const QString &value);

    qint64 pendingUpdateInstallerSize() const;
    void setPendingUpdateInstallerSize(qint64 value);

    QString pendingUpdateInstallerSha256() const;
    void setPendingUpdateInstallerSha256(const QString &value);

    QString downloadedUpdateVersion() const;
    void setDownloadedUpdateVersion(const QString &value);
    QString scheduledUpdateVersion() const;
    void setScheduledUpdateVersion(const QString &value);
    void clearPendingUpdate();

    bool autoInstallUpdates() const;
    void setAutoInstallUpdates(bool enabled);

    int updatePolicy() const;
    void setUpdatePolicy(int value);

    int updateDownloadMode() const;
    void setUpdateDownloadMode(int value);

    QString updateManualProxyUrl() const;
    void setUpdateManualProxyUrl(const QString &value);

    QString feedbackSessionJson() const;
    void setFeedbackSessionJson(const QString &json);

    void sync();

private:
    QSettings *m_settings = nullptr;
};
