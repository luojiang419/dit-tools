#include "application/UpdateService.h"
#include "application/UpdaterSession.h"
#include "infrastructure/config/AppSettings.h"

#include <QNetworkProxy>
#include <QProcessEnvironment>
#include <QSettings>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class UpdateServiceTest : public QObject {
    Q_OBJECT

private slots:
    void compareVersionTags_ordersSemanticVersions();
    void compareVersionTags_handlesInvalidValues();
    void expectedInstallerName_returnsPlatformSpecificPrimaryAsset();
    void parseLatestRelease_returnsInstallerAsset();
    void parseLatestRelease_windowsIgnoresMacAsset();
    void parseLatestRelease_macosReturnsDmgAsset();
    void parseLatestRelease_macosFallsBackToPkgAsset();
    void parseLatestRelease_rejectsMissingInstaller();
    void parseLatestRelease_rejectsInvalidPayload();
    void latestReleaseStatusMessage_handlesNoRelease();
    void normalizedProxyUrl_addsHttpSchemeForHostPort();
    void normalizedProxyUrl_rejectsInvalidValues();
    void proxyUrlForNetworkProxy_handlesHttpProxy();
    void proxyUrlForNetworkProxy_handlesSocksProxy();
    void preferredProxyUrl_skipsUnsupportedEntries();
    void proxyUrlsForEnvironment_readsCommonVariables();
    void localProxyCandidates_containsCommonPorts();
    void updaterSessionArguments_roundTripPathsWithSpaces();
    void updaterSessionArguments_rejectIncompleteSession();
    void updaterSessionProgress_parsesAndMapsRealInstallerProgress();
    void updaterProgressContract_usesDeterminateInstallerSignal();
    void updaterSessionRunner_reportsMissingInstaller();
    void appSettings_persistsUpdatePolicyAndClearsSchedule();
};

void UpdateServiceTest::compareVersionTags_ordersSemanticVersions()
{
    QCOMPARE(UpdateService::compareVersionTags(QStringLiteral("v0.1.75"), QStringLiteral("v0.1.74")), 1);
    QCOMPARE(UpdateService::compareVersionTags(QStringLiteral("0.1.74"), QStringLiteral("v0.1.74")), 0);
    QCOMPARE(UpdateService::compareVersionTags(QStringLiteral("v0.1.74"), QStringLiteral("v0.1.75")), -1);
}

void UpdateServiceTest::compareVersionTags_handlesInvalidValues()
{
    QCOMPARE(UpdateService::compareVersionTags(QStringLiteral("invalid"), QStringLiteral("v0.1.75")), -1);
    QCOMPARE(UpdateService::compareVersionTags(QStringLiteral("v0.1.75"), QStringLiteral("invalid")), 1);
    QCOMPARE(UpdateService::compareVersionTags(QStringLiteral("invalid"), QStringLiteral("broken")), 0);
}

void UpdateServiceTest::expectedInstallerName_returnsPlatformSpecificPrimaryAsset()
{
    QCOMPARE(UpdateService::expectedInstallerName(QStringLiteral("v0.1.75"), QStringLiteral("windows")),
             QStringLiteral("CineVault-Setup-v0.1.75.exe"));
    QCOMPARE(UpdateService::expectedInstallerName(QStringLiteral("v0.1.75"), QStringLiteral("macos")),
             QStringLiteral("CineVault-macOS-v0.1.75.dmg"));
}

void UpdateServiceTest::parseLatestRelease_returnsInstallerAsset()
{
    const QByteArray payload = R"({
        "tag_name": "v0.1.75",
        "assets": [
            {
                "name": "README.txt",
                "browser_download_url": "https://example.com/README.txt",
                "size": 1
            },
            {
                "name": "CineVault-Setup-v0.1.75.exe",
                "browser_download_url": "https://example.com/CineVault-Setup-v0.1.75.exe",
                "size": 123456
            }
        ]
    })";

    UpdateReleaseInfo info;
    QString errorMessage;
    QVERIFY(UpdateService::parseLatestRelease(payload, &info, &errorMessage));
    QCOMPARE(info.versionTag, QStringLiteral("v0.1.75"));
    QCOMPARE(info.installerName, QStringLiteral("CineVault-Setup-v0.1.75.exe"));
    QCOMPARE(info.installerUrl, QStringLiteral("https://example.com/CineVault-Setup-v0.1.75.exe"));
    QCOMPARE(info.installerSize, 123456);
}

void UpdateServiceTest::parseLatestRelease_windowsIgnoresMacAsset()
{
    const QByteArray payload = R"({
        "tag_name": "v0.1.75",
        "assets": [
            {
                "name": "CineVault-macOS-v0.1.75.dmg",
                "browser_download_url": "https://example.com/CineVault-macOS-v0.1.75.dmg",
                "size": 456
            },
            {
                "name": "CineVault-Setup-v0.1.75.exe",
                "browser_download_url": "https://example.com/CineVault-Setup-v0.1.75.exe",
                "size": 123
            }
        ]
    })";

    UpdateReleaseInfo info;
    QString errorMessage;
    QVERIFY(UpdateService::parseLatestRelease(payload, &info, &errorMessage, QStringLiteral("windows")));
    QCOMPARE(info.installerName, QStringLiteral("CineVault-Setup-v0.1.75.exe"));
    QCOMPARE(info.installerUrl, QStringLiteral("https://example.com/CineVault-Setup-v0.1.75.exe"));
}

void UpdateServiceTest::parseLatestRelease_macosReturnsDmgAsset()
{
    const QByteArray payload = R"({
        "tag_name": "v0.1.75",
        "assets": [
            {
                "name": "CineVault-Setup-v0.1.75.exe",
                "browser_download_url": "https://example.com/CineVault-Setup-v0.1.75.exe",
                "size": 123
            },
            {
                "name": "CineVault-macOS-v0.1.75.dmg",
                "browser_download_url": "https://example.com/CineVault-macOS-v0.1.75.dmg",
                "size": 456
            }
        ]
    })";

    UpdateReleaseInfo info;
    QString errorMessage;
    QVERIFY(UpdateService::parseLatestRelease(payload, &info, &errorMessage, QStringLiteral("macos")));
    QCOMPARE(info.installerName, QStringLiteral("CineVault-macOS-v0.1.75.dmg"));
    QCOMPARE(info.installerUrl, QStringLiteral("https://example.com/CineVault-macOS-v0.1.75.dmg"));
    QCOMPARE(info.installerSize, 456);
}

void UpdateServiceTest::parseLatestRelease_macosFallsBackToPkgAsset()
{
    const QByteArray payload = R"({
        "tag_name": "v0.1.75",
        "assets": [
            {
                "name": "CineVault-macOS-v0.1.75.pkg",
                "browser_download_url": "https://example.com/CineVault-macOS-v0.1.75.pkg",
                "size": 789
            }
        ]
    })";

    UpdateReleaseInfo info;
    QString errorMessage;
    QVERIFY(UpdateService::parseLatestRelease(payload, &info, &errorMessage, QStringLiteral("macos")));
    QCOMPARE(info.installerName, QStringLiteral("CineVault-macOS-v0.1.75.pkg"));
    QCOMPARE(info.installerUrl, QStringLiteral("https://example.com/CineVault-macOS-v0.1.75.pkg"));
    QCOMPARE(info.installerSize, 789);
}

void UpdateServiceTest::parseLatestRelease_rejectsMissingInstaller()
{
    const QByteArray payload = R"({
        "tag_name": "v0.1.75",
        "assets": [
            {
                "name": "CineVault-Portable-v0.1.75.zip",
                "browser_download_url": "https://example.com/CineVault-Portable-v0.1.75.zip",
                "size": 1
            }
        ]
    })";

    UpdateReleaseInfo info;
    QString errorMessage;
    QVERIFY(!UpdateService::parseLatestRelease(payload, &info, &errorMessage, QStringLiteral("windows")));
    QVERIFY(errorMessage.contains(QStringLiteral("CineVault-Setup-v0.1.75.exe")));
}

void UpdateServiceTest::parseLatestRelease_rejectsInvalidPayload()
{
    UpdateReleaseInfo info;
    QString errorMessage;
    QVERIFY(!UpdateService::parseLatestRelease("not-json", &info, &errorMessage));
    QVERIFY(!errorMessage.isEmpty());
}

void UpdateServiceTest::latestReleaseStatusMessage_handlesNoRelease()
{
    QCOMPARE(UpdateService::latestReleaseStatusMessage(404, QStringLiteral("Not Found")),
             QStringLiteral("当前仓库还没有可用的发布版本。"));
    QCOMPARE(UpdateService::latestReleaseStatusMessage(500, QStringLiteral("Server Error")),
             QStringLiteral("检查更新失败：Server Error"));
}

void UpdateServiceTest::normalizedProxyUrl_addsHttpSchemeForHostPort()
{
    QCOMPARE(UpdateService::normalizedProxyUrl(QStringLiteral("127.0.0.1:7890")),
             QStringLiteral("http://127.0.0.1:7890"));
    QCOMPARE(UpdateService::normalizedProxyUrl(QStringLiteral("socks5://localhost:1080")),
             QStringLiteral("socks5://localhost:1080"));
}

void UpdateServiceTest::normalizedProxyUrl_rejectsInvalidValues()
{
    QVERIFY(UpdateService::normalizedProxyUrl(QString()).isEmpty());
    QVERIFY(UpdateService::normalizedProxyUrl(QStringLiteral("127.0.0.1")).isEmpty());
    QVERIFY(UpdateService::normalizedProxyUrl(QStringLiteral("ftp://127.0.0.1:21")).isEmpty());
}

void UpdateServiceTest::proxyUrlForNetworkProxy_handlesHttpProxy()
{
    const QNetworkProxy proxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), 7890);
    QCOMPARE(UpdateService::proxyUrlForNetworkProxy(proxy), QStringLiteral("http://127.0.0.1:7890"));
}

void UpdateServiceTest::proxyUrlForNetworkProxy_handlesSocksProxy()
{
    QNetworkProxy proxy(QNetworkProxy::Socks5Proxy, QStringLiteral("127.0.0.1"), 1080);
    proxy.setUser(QStringLiteral("tester"));
    proxy.setPassword(QStringLiteral("secret"));
    QCOMPARE(UpdateService::proxyUrlForNetworkProxy(proxy),
             QStringLiteral("socks5://tester:secret@127.0.0.1:1080"));
}

void UpdateServiceTest::preferredProxyUrl_skipsUnsupportedEntries()
{
    const QList<QNetworkProxy> proxies{
        QNetworkProxy(QNetworkProxy::NoProxy),
        QNetworkProxy(QNetworkProxy::DefaultProxy),
        QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), 7890)
    };
    QCOMPARE(UpdateService::preferredProxyUrl(proxies), QStringLiteral("http://127.0.0.1:7890"));
}

void UpdateServiceTest::proxyUrlsForEnvironment_readsCommonVariables()
{
    QProcessEnvironment environment;
    environment.insert(QStringLiteral("HTTPS_PROXY"), QStringLiteral("127.0.0.1:7890"));
    environment.insert(QStringLiteral("ALL_PROXY"), QStringLiteral("socks5://localhost:1080"));

    const auto proxyUrls = UpdateService::proxyUrlsForEnvironment(environment);
    QVERIFY(proxyUrls.contains(QStringLiteral("http://127.0.0.1:7890")));
    QVERIFY(proxyUrls.contains(QStringLiteral("socks5://localhost:1080")));
}

void UpdateServiceTest::localProxyCandidates_containsCommonPorts()
{
    const auto proxyUrls = UpdateService::localProxyCandidates({QStringLiteral("127.0.0.1")});
    QVERIFY(proxyUrls.contains(QStringLiteral("http://127.0.0.1:7890")));
    QVERIFY(proxyUrls.contains(QStringLiteral("http://127.0.0.1:10809")));
    QVERIFY(proxyUrls.contains(QStringLiteral("socks5://127.0.0.1:1080")));
}

void UpdateServiceTest::updaterSessionArguments_roundTripPathsWithSpaces()
{
    UpdaterInstallSession source;
    source.sessionId = QStringLiteral("update_123456");
    source.versionTag = QStringLiteral("v0.1.145");
    source.installerPath = QStringLiteral("C:/Users/Test User/Updates/CineVault-Setup-v0.1.145.exe");
    source.installRoot = QStringLiteral("C:/Program Files/影资管家");
    source.executableName = QStringLiteral("CineVault.exe");
    source.oldProcessId = 7788;

    UpdaterInstallSession parsed;
    QString errorMessage;
    QVERIFY(UpdaterSessionRunner::parseArguments(
        UpdaterSessionRunner::buildArguments(source), &parsed, &errorMessage));
    QVERIFY(errorMessage.isEmpty());
    QCOMPARE(parsed.sessionId, source.sessionId);
    QCOMPARE(parsed.versionTag, source.versionTag);
    QCOMPARE(parsed.installerPath, source.installerPath);
    QCOMPARE(parsed.installRoot, source.installRoot);
    QCOMPARE(parsed.executableName, source.executableName);
    QCOMPARE(parsed.oldProcessId, source.oldProcessId);
}

void UpdateServiceTest::updaterSessionArguments_rejectIncompleteSession()
{
    const QStringList arguments{
        QStringLiteral("--run-update-session=update_123456"),
        QStringLiteral("--update-version=v0.1.145")
    };

    UpdaterInstallSession parsed;
    QString errorMessage;
    QVERIFY(!UpdaterSessionRunner::parseArguments(arguments, &parsed, &errorMessage));
    QCOMPARE(errorMessage, QStringLiteral("更新会话参数不完整。"));
}

void UpdateServiceTest::updaterSessionProgress_parsesAndMapsRealInstallerProgress()
{
    QCOMPARE(UpdaterSessionRunner::parseInstallerProgress("0"), 0);
    QCOMPARE(UpdaterSessionRunner::parseInstallerProgress("42\r\n"), 42);
    QCOMPARE(UpdaterSessionRunner::parseInstallerProgress("100"), 100);
    QCOMPARE(UpdaterSessionRunner::parseInstallerProgress("101"), -1);
    QCOMPARE(UpdaterSessionRunner::parseInstallerProgress("invalid"), -1);

    QCOMPARE(UpdaterSessionRunner::overallProgressForInstallerProgress(0), 10);
    QCOMPARE(UpdaterSessionRunner::overallProgressForInstallerProgress(50), 50);
    QCOMPARE(UpdaterSessionRunner::overallProgressForInstallerProgress(100), 90);
    QCOMPARE(UpdaterSessionRunner::overallProgressForInstallerProgress(-10), 10);
    QCOMPARE(UpdaterSessionRunner::overallProgressForInstallerProgress(110), 90);
}

void UpdateServiceTest::updaterProgressContract_usesDeterminateInstallerSignal()
{
    const auto readSource = [](const QString &relativePath) {
        QFile file(QStringLiteral(CINEVAULT_SOURCE_DIR "/../../") + relativePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QByteArray{};
        }
        return file.readAll();
    };

    const auto installerScript = readSource(QStringLiteral("installer/windows/cinevault.iss"));
    const auto updaterSource = readSource(QStringLiteral(
        "dit-tools-src/cinevault-pro/src/application/UpdaterSession.cpp"));
    const auto windowSource = readSource(QStringLiteral(
        "dit-tools-src/cinevault-pro/src/ui/widgets/UpdaterWindow.cpp"));

    QVERIFY(installerScript.contains("CurInstallProgressChanged"));
    QVERIFY(installerScript.contains("UPDATEPROGRESSFILE"));
    QVERIFY(installerScript.contains("CloseApplications=no"));
    QVERIFY(installerScript.contains("StopCineVaultProcesses"));
    QVERIFY(installerScript.contains("ShouldForceStopCineVaultProcesses"));
    QVERIFY(installerScript.contains("/F /T /IM CineVault.exe"));
    QVERIFY(installerScript.contains("PrepareToInstall"));
    QVERIFY(installerScript.contains("NextButtonClick"));
    QVERIFY(installerScript.contains("InitializeUninstall"));
    QVERIFY(installerScript.contains("Result := UpdateProgressFilePath = ''"));
    QVERIFY(updaterSource.contains("/UPDATEPROGRESSFILE="));
    QVERIFY(updaterSource.contains("pollInstallerProgress"));
    QVERIFY(windowSource.contains("setRange(0, 100)"));
    QVERIFY(windowSource.contains(QStringLiteral("总进度").toUtf8()));
    QVERIFY(!windowSource.contains("setRange(0, 0)"));
}

void UpdateServiceTest::updaterSessionRunner_reportsMissingInstaller()
{
    UpdaterInstallSession session;
    session.sessionId = QStringLiteral("update_missing_installer");
    session.versionTag = QStringLiteral("v0.1.145");
    session.installerPath = QStringLiteral("Z:/missing/CineVault-Setup-v0.1.145.exe");
    session.installRoot = QStringLiteral("C:/Program Files/影资管家");
    session.executableName = QStringLiteral("CineVault.exe");
    session.oldProcessId = 7788;

    UpdaterSessionRunner runner;
    bool didFinish = false;
    bool succeeded = true;
    QString resultMessage;
    connect(&runner, &UpdaterSessionRunner::finished,
            &runner, [&](bool success, const QString &message) {
                didFinish = true;
                succeeded = success;
                resultMessage = message;
            });

    runner.start(session);

    QVERIFY(didFinish);
    QVERIFY(!succeeded);
    QVERIFY(resultMessage.startsWith(QStringLiteral("更新安装包不存在：")));
}

void UpdateServiceTest::appSettings_persistsUpdatePolicyAndClearsSchedule()
{
    QTemporaryDir settingsRoot;
    QVERIFY(settingsRoot.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsRoot.path());
    QCoreApplication::setOrganizationName(QStringLiteral("DIT Tools Update Test"));
    QCoreApplication::setApplicationName(QStringLiteral("CineVault Update Test"));

    AppSettings settings;
    QVERIFY(!settings.autoInstallUpdates());
    settings.setAutoInstallUpdates(true);
    settings.setPendingUpdateVersion(QStringLiteral("v0.1.145"));
    settings.setPendingUpdateInstallerPath(QStringLiteral("C:/Updates/CineVault-Setup-v0.1.145.exe"));
    settings.setScheduledUpdateVersion(QStringLiteral("v0.1.145"));
    settings.sync();

    QVERIFY(settings.autoInstallUpdates());
    QCOMPARE(settings.scheduledUpdateVersion(), QStringLiteral("v0.1.145"));

    settings.clearPendingUpdate();
    QVERIFY(settings.pendingUpdateVersion().isEmpty());
    QVERIFY(settings.pendingUpdateInstallerPath().isEmpty());
    QVERIFY(settings.scheduledUpdateVersion().isEmpty());
    QVERIFY(settings.autoInstallUpdates());
}

QTEST_APPLESS_MAIN(UpdateServiceTest)

#include "UpdateServiceTest.moc"
