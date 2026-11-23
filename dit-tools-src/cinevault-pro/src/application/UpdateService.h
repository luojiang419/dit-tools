#pragma once

#include <QList>
#include <QNetworkProxy>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>

class AppSettings;

struct UpdateReleaseInfo {
    QString versionTag;
    QString installerName;
    QString installerUrl;
    qint64 installerSize = 0;
    QString installerSha256;
};

class UpdateService : public QObject {
    Q_OBJECT

public:
    explicit UpdateService(AppSettings *settings, QObject *parent = nullptr);
    ~UpdateService() override;

    static QString currentPlatformKey();
    static QString normalizeVersionTag(const QString &versionTag);
    static int compareVersionTags(const QString &left, const QString &right);
    static QString expectedInstallerName(const QString &versionTag, const QString &platformKey = QString());
    static QStringList expectedInstallerNames(const QString &versionTag, const QString &platformKey = QString());
    static bool parseLatestRelease(const QByteArray &payload, UpdateReleaseInfo *info, QString *errorMessage, const QString &platformKey = QString());
    static QString normalizeSha256(const QString &value);
    static QString fileSha256(const QString &path, QString *errorMessage = nullptr);
    static bool isTrustedReleaseUrl(const QString &url);
    static bool validateInstallerFile(const QString &versionTag,
                                      const QString &installerPath,
                                      qint64 expectedSize,
                                      const QString &expectedSha256,
                                      QString *errorMessage = nullptr);
    static QString expectedUpdateSignerSha256();
    static bool verifyInstallerAuthenticode(const QString &installerPath,
                                            QString *errorMessage = nullptr);
    static QString latestReleaseStatusMessage(int statusCode, const QString &networkErrorString);
    static QString normalizedProxyUrl(const QString &proxyUrl);
    static QString proxyUrlForNetworkProxy(const QNetworkProxy &proxy);
    static QString preferredProxyUrl(const QList<QNetworkProxy> &proxies);
    static QStringList proxyUrlsForEnvironment(const QProcessEnvironment &environment);
    static QStringList localProxyCandidates(const QStringList &hosts = QStringList());
    static QString firstReachableProxyUrl(const QStringList &proxyUrls, int timeoutMs = 120);
    static bool updateCheckAllowed(int updatePolicy, bool manual);
    static bool directFallbackAllowed(int networkMode);

    QString currentVersionTag() const;
    bool isBusy() const;
    bool hasPendingUpdate() const;

    void beginStartupFlow();
    void checkForUpdates(bool manual);
    bool installPendingUpdateNow(QString *errorMessage);

signals:
    void busyChanged();
    void statusMessageChanged(const QString &message);
    void updateReady(const QString &versionTag, const QString &installerPath, bool manual);

private:
    void setBusy(bool busy);
    void setStatusMessage(const QString &message);
    void clearPendingUpdateIfCurrentOrMissing();
    void cleanupUpdateCache();
    bool readPendingUpdate(QString *versionTag,
                           QString *installerPath,
                           qint64 *installerSize = nullptr,
                           QString *installerSha256 = nullptr) const;
    bool useExistingInstaller(const UpdateReleaseInfo &release, bool manual);
    QString systemProxyUrl() const;
    QString autoDetectedProxyUrl() const;
    QString configuredProxyUrl(QString *errorMessage) const;
    QString proxyStatusLabel(const QString &proxyUrl) const;
    void launchCheckProcess(const QString &proxyUrl, bool allowDirectFallback);
    bool retryCheckWithoutProxy();
    void startInstallerDownload(const UpdateReleaseInfo &release, bool manual);
    void launchDownloadProcess(const QString &proxyUrl, bool allowDirectFallback);
    bool retryDownloadWithoutProxy();
    void finishCheckProcess(int exitCode, QProcess::ExitStatus exitStatus);
    void finishDownloadProcess(int exitCode, QProcess::ExitStatus exitStatus);

    AppSettings *m_settings = nullptr;
    QProcess *m_checkProcess = nullptr;
    QProcess *m_downloadProcess = nullptr;
    QString m_checkProxyUrl;
    QString m_downloadVersionTag;
    QString m_downloadSourceUrl;
    QString m_downloadTargetPath;
    QString m_downloadPartPath;
    QString m_downloadProxyUrl;
    QString m_statusMessage;
    bool m_busy = false;
    bool m_manualCheck = false;
    bool m_checkAllowDirectFallback = false;
    bool m_downloadAllowDirectFallback = false;
    qint64 m_downloadExpectedSize = 0;
    QString m_downloadExpectedSha256;
};
