#include "application/UpdateService.h"
#include "application/UpdaterSession.h"
#include "infrastructure/config/AppSettings.h"
#include "shared/Paths.h"

#include <QNetworkProxy>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class UpdateServiceTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void compareVersionTags_ordersSemanticVersions();
    void compareVersionTags_handlesInvalidValues();
    void expectedInstallerName_returnsPlatformSpecificPrimaryAsset();
    void parseLatestRelease_returnsInstallerAsset();
    void parseLatestRelease_windowsIgnoresMacAsset();
    void parseLatestRelease_macosReturnsDmgAsset();
    void parseLatestRelease_macosFallsBackToPkgAsset();
    void parseLatestRelease_rejectsMissingInstaller();
    void parseLatestRelease_rejectsInvalidPayload();
    void parseLatestRelease_rejectsMissingDigest();
    void parseLatestRelease_rejectsDuplicateInstaller();
    void parseLatestRelease_rejectsUntrustedUrl();
    void fileSha256_detectsSingleByteTampering();
    void validateInstallerFile_rejectsTamperingAndExternalPath();
    void verifyInstallerAuthenticode_allowsUnsignedInstallerWithoutPinnedSigner();
    void latestReleaseStatusMessage_handlesNoRelease();
    void normalizedProxyUrl_addsHttpSchemeForHostPort();
    void normalizedProxyUrl_rejectsInvalidValues();
    void proxyUrlForNetworkProxy_handlesHttpProxy();
    void proxyUrlForNetworkProxy_rejectsCredentialBearingSocksProxy();
    void preferredProxyUrl_skipsUnsupportedEntries();
    void proxyUrlsForEnvironment_readsCommonVariables();
    void localProxyCandidates_containsCommonPorts();
    void updatePolicyAndNetworkMode_coversNineCombinations();
    void updaterSessionArguments_roundTripPathsWithSpaces();
    void updaterSessionArguments_rejectIncompleteSession();
    void updaterSessionProgress_parsesAndMapsRealInstallerProgress();
    void updaterProgressContract_usesDeterminateInstallerSignal();
    void updaterSessionRunner_reportsMissingInstaller();
    void appSettings_persistsUpdatePolicyAndClearsSchedule();
};

void UpdateServiceTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

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
                "browser_download_url": "https://github.com/example/README.txt",
                "size": 1
            },
            {
                "name": "CineVault-Setup-v0.1.75.exe",
                "browser_download_url": "https://github.com/luojiang419/dit-tools/releases/download/v0.1.75/CineVault-Setup-v0.1.75.exe",
                "size": 123456,
                "digest": "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
            }
        ]
    })";

    UpdateReleaseInfo info;
    QString errorMessage;
    QVERIFY(UpdateService::parseLatestRelease(payload, &info, &errorMessage));
    QCOMPARE(info.versionTag, QStringLiteral("v0.1.75"));
    QCOMPARE(info.installerName, QStringLiteral("CineVault-Setup-v0.1.75.exe"));
    QCOMPARE(info.installerUrl, QStringLiteral("https://github.com/luojiang419/dit-tools/releases/download/v0.1.75/CineVault-Setup-v0.1.75.exe"));
    QCOMPARE(info.installerSize, 123456);
    QCOMPARE(info.installerSha256, QString(64, QLatin1Char('a')));
}

void UpdateServiceTest::parseLatestRelease_windowsIgnoresMacAsset()
{
    const QByteArray payload = R"({
        "tag_name": "v0.1.75",
        "assets": [
            {
                "name": "CineVault-macOS-v0.1.75.dmg",
                "browser_download_url": "https://github.com/luojiang419/dit-tools/releases/download/v0.1.75/CineVault-macOS-v0.1.75.dmg",
                "size": 456,
                "digest": "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
            },
            {
                "name": "CineVault-Setup-v0.1.75.exe",
                "browser_download_url": "https://github.com/luojiang419/dit-tools/releases/download/v0.1.75/CineVault-Setup-v0.1.75.exe",
                "size": 123,
                "digest": "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
            }
        ]
    })";

    UpdateReleaseInfo info;
    QString errorMessage;
    QVERIFY(UpdateService::parseLatestRelease(payload, &info, &errorMessage, QStringLiteral("windows")));
    QCOMPARE(info.installerName, QStringLiteral("CineVault-Setup-v0.1.75.exe"));
    QCOMPARE(info.installerUrl, QStringLiteral("https://github.com/luojiang419/dit-tools/releases/download/v0.1.75/CineVault-Setup-v0.1.75.exe"));
}

void UpdateServiceTest::parseLatestRelease_macosReturnsDmgAsset()
{
    const QByteArray payload = R"({
        "tag_name": "v0.1.75",
        "assets": [
            {
                "name": "CineVault-Setup-v0.1.75.exe",
                "browser_download_url": "https://github.com/luojiang419/dit-tools/releases/download/v0.1.75/CineVault-Setup-v0.1.75.exe",
                "size": 123,
                "digest": "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
            },
            {
                "name": "CineVault-macOS-v0.1.75.dmg",
                "browser_download_url": "https://github.com/luojiang419/dit-tools/releases/download/v0.1.75/CineVault-macOS-v0.1.75.dmg",
                "size": 456,
                "digest": "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
            }
        ]
    })";

    UpdateReleaseInfo info;
    QString errorMessage;
    QVERIFY(UpdateService::parseLatestRelease(payload, &info, &errorMessage, QStringLiteral("macos")));
    QCOMPARE(info.installerName, QStringLiteral("CineVault-macOS-v0.1.75.dmg"));
    QCOMPARE(info.installerUrl, QStringLiteral("https://github.com/luojiang419/dit-tools/releases/download/v0.1.75/CineVault-macOS-v0.1.75.dmg"));
    QCOMPARE(info.installerSize, 456);
}

void UpdateServiceTest::parseLatestRelease_macosFallsBackToPkgAsset()
{
    const QByteArray payload = R"({
        "tag_name": "v0.1.75",
        "assets": [
            {
                "name": "CineVault-macOS-v0.1.75.pkg",
                "browser_download_url": "https://github.com/luojiang419/dit-tools/releases/download/v0.1.75/CineVault-macOS-v0.1.75.pkg",
                "size": 789,
                "digest": "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
            }
        ]
    })";

    UpdateReleaseInfo info;
    QString errorMessage;
    QVERIFY(UpdateService::parseLatestRelease(payload, &info, &errorMessage, QStringLiteral("macos")));
    QCOMPARE(info.installerName, QStringLiteral("CineVault-macOS-v0.1.75.pkg"));
    QCOMPARE(info.installerUrl, QStringLiteral("https://github.com/luojiang419/dit-tools/releases/download/v0.1.75/CineVault-macOS-v0.1.75.pkg"));
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
    QVERIFY(UpdateService::normalizedProxyUrl(QStringLiteral("http://user:secret@127.0.0.1:7890")).isEmpty());
}

void UpdateServiceTest::proxyUrlForNetworkProxy_handlesHttpProxy()
{
    const QNetworkProxy proxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), 7890);
    QCOMPARE(UpdateService::proxyUrlForNetworkProxy(proxy), QStringLiteral("http://127.0.0.1:7890"));
}

void UpdateServiceTest::proxyUrlForNetworkProxy_rejectsCredentialBearingSocksProxy()
{
    QNetworkProxy proxy(QNetworkProxy::Socks5Proxy, QStringLiteral("127.0.0.1"), 1080);
    proxy.setUser(QStringLiteral("tester"));
    proxy.setPassword(QStringLiteral("secret"));
    QVERIFY(UpdateService::proxyUrlForNetworkProxy(proxy).isEmpty());
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
    source.installerSize = 123456;
    source.installerSha256 = QString(64, QLatin1Char('a'));
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
    QCOMPARE(parsed.installerSize, source.installerSize);
    QCOMPARE(parsed.installerSha256, source.installerSha256);
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
    const auto updateServiceSource = readSource(QStringLiteral(
        "dit-tools-src/cinevault-pro/src/application/UpdateService.cpp"));
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
    QVERIFY(windowSource.contains("QVariantAnimation"));
    QVERIFY(windowSource.contains("setStartValue(m_displayedPercentage)"));
    QVERIFY(windowSource.contains("setEndValue(target)"));
    QVERIFY(windowSource.contains("QPlainTextEdit"));
    QVERIFY(windowSource.contains(QStringLiteral("更新详情").toUtf8()));
    QVERIFY(windowSource.contains("appendUpdateDetail(event)"));
    QVERIFY(updaterSource.contains(QStringLiteral("已等待 %1 秒").toUtf8()));
    QVERIFY(updaterSource.contains(QStringLiteral("安装器实际进度 %1%").toUtf8()));
    QVERIFY(updaterSource.contains("Get-FileHash"));
    QVERIFY(updaterSource.contains("Get-AuthenticodeSignature"));
    QVERIFY(updaterSource.contains("GetCertHashString('SHA256')"));
    QVERIFY(updaterSource.contains("if (-not [string]::IsNullOrWhiteSpace($expectedSignerSha256))"));
    QVERIFY(updateServiceSource.contains("--continue-at"));
    QVERIFY(updateServiceSource.contains("--speed-time"));
    QVERIFY(updateServiceSource.contains("QStorageInfo"));
    QVERIFY(!updateServiceSource.contains("QStringLiteral(\"600\")"));
    QVERIFY(!windowSource.contains("setValue(event.percentage)"));
    QVERIFY(!windowSource.contains("setRange(0, 0)"));
}

void UpdateServiceTest::updatePolicyAndNetworkMode_coversNineCombinations()
{
    for (int policy = 0; policy < 3; ++policy) {
        QCOMPARE(UpdateService::updateCheckAllowed(policy, false), policy == 0);
        QCOMPARE(UpdateService::updateCheckAllowed(policy, true), policy != 2);
        for (int networkMode = 0; networkMode < 3; ++networkMode) {
            QCOMPARE(UpdateService::directFallbackAllowed(networkMode), networkMode == 0);
        }
    }
}

void UpdateServiceTest::parseLatestRelease_rejectsMissingDigest()
{
    const QByteArray payload = R"({
        "tag_name": "v0.1.75",
        "assets": [{
            "name": "CineVault-Setup-v0.1.75.exe",
            "browser_download_url": "https://github.com/luojiang419/dit-tools/releases/download/v0.1.75/CineVault-Setup-v0.1.75.exe",
            "size": 123
        }]
    })";

    UpdateReleaseInfo info;
    QString errorMessage;
    QVERIFY(!UpdateService::parseLatestRelease(payload, &info, &errorMessage, QStringLiteral("windows")));
    QVERIFY(errorMessage.contains(QStringLiteral("SHA-256")));
}

void UpdateServiceTest::parseLatestRelease_rejectsDuplicateInstaller()
{
    const QByteArray payload = R"({
        "tag_name": "v0.1.75",
        "assets": [
            {
                "name": "CineVault-Setup-v0.1.75.exe",
                "browser_download_url": "https://github.com/luojiang419/dit-tools/releases/download/v0.1.75/CineVault-Setup-v0.1.75.exe",
                "size": 123,
                "digest": "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
            },
            {
                "name": "CineVault-Setup-v0.1.75.exe",
                "browser_download_url": "https://github.com/luojiang419/dit-tools/releases/download/v0.1.75/duplicate.exe",
                "size": 123,
                "digest": "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
            }
        ]
    })";

    UpdateReleaseInfo info;
    QString errorMessage;
    QVERIFY(!UpdateService::parseLatestRelease(payload, &info, &errorMessage, QStringLiteral("windows")));
    QVERIFY(errorMessage.contains(QStringLiteral("重复")));
}

void UpdateServiceTest::parseLatestRelease_rejectsUntrustedUrl()
{
    const QByteArray payload = R"({
        "tag_name": "v0.1.75",
        "assets": [{
            "name": "CineVault-Setup-v0.1.75.exe",
            "browser_download_url": "https://attacker.example/CineVault-Setup-v0.1.75.exe",
            "size": 123,
            "digest": "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        }]
    })";

    UpdateReleaseInfo info;
    QString errorMessage;
    QVERIFY(!UpdateService::parseLatestRelease(payload, &info, &errorMessage, QStringLiteral("windows")));
    QVERIFY(errorMessage.contains(QStringLiteral("受信任")));
}

void UpdateServiceTest::fileSha256_detectsSingleByteTampering()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto path = temp.filePath(QStringLiteral("installer.exe"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("trusted-installer"), qint64{17});
    file.close();
    const auto originalHash = UpdateService::fileSha256(path);
    QCOMPARE(originalHash.size(), 64);

    QVERIFY(file.open(QIODevice::Append));
    QCOMPARE(file.write("x"), qint64{1});
    file.close();
    const auto tamperedHash = UpdateService::fileSha256(path);
    QCOMPARE(tamperedHash.size(), 64);
    QVERIFY(tamperedHash != originalHash);
}

void UpdateServiceTest::validateInstallerFile_rejectsTamperingAndExternalPath()
{
    const auto versionTag = QStringLiteral("v9.9.9");
    const auto installerName = UpdateService::expectedInstallerName(versionTag);
    QVERIFY(!installerName.isEmpty());
    const auto platformRoot = QDir(Paths::updatesRoot()).filePath(UpdateService::currentPlatformKey());
    QVERIFY(QDir().mkpath(platformRoot));
    const auto trustedPath = QDir(platformRoot).filePath(installerName);
    QFile::remove(trustedPath);

    QFile trustedFile(trustedPath);
    QVERIFY(trustedFile.open(QIODevice::WriteOnly));
    QCOMPARE(trustedFile.write("trusted-update"), qint64{14});
    trustedFile.close();
    const auto sha256 = UpdateService::fileSha256(trustedPath);
    QCOMPARE(sha256.size(), 64);

    QString errorMessage;
    QVERIFY2(UpdateService::validateInstallerFile(versionTag, trustedPath, 14, sha256, &errorMessage),
             qPrintable(errorMessage));

    QTemporaryDir externalRoot;
    QVERIFY(externalRoot.isValid());
    const auto externalPath = externalRoot.filePath(installerName);
    QVERIFY(QFile::copy(trustedPath, externalPath));
    QVERIFY(!UpdateService::validateInstallerFile(versionTag, externalPath, 14, sha256, &errorMessage));
    QVERIFY(errorMessage.contains(QStringLiteral("专用缓存")));

    QVERIFY(trustedFile.open(QIODevice::ReadWrite));
    QVERIFY(trustedFile.seek(13));
    QCOMPARE(trustedFile.write("x"), qint64{1});
    trustedFile.close();
    QVERIFY(!UpdateService::validateInstallerFile(versionTag, trustedPath, 14, sha256, &errorMessage));
    QVERIFY(errorMessage.contains(QStringLiteral("SHA-256")));
    QVERIFY(QFile::remove(trustedPath));
}

void UpdateServiceTest::verifyInstallerAuthenticode_allowsUnsignedInstallerWithoutPinnedSigner()
{
    QVERIFY(UpdateService::expectedUpdateSignerSha256().isEmpty());

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto installerPath = temp.filePath(QStringLiteral("unsigned-installer.exe"));
    QFile installer(installerPath);
    QVERIFY(installer.open(QIODevice::WriteOnly));
    QCOMPARE(installer.write("unsigned-installer"), qint64{18});
    installer.close();

    QString errorMessage;
    QVERIFY2(UpdateService::verifyInstallerAuthenticode(installerPath, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY(errorMessage.isEmpty());
}

void UpdateServiceTest::updaterSessionRunner_reportsMissingInstaller()
{
    UpdaterInstallSession session;
    session.sessionId = QStringLiteral("update_missing_installer");
    session.versionTag = QStringLiteral("v0.1.145");
    session.installerPath = QStringLiteral("Z:/missing/CineVault-Setup-v0.1.145.exe");
    session.installerSize = 123456;
    session.installerSha256 = QString(64, QLatin1Char('a'));
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
    QVERIFY(resultMessage.startsWith(QStringLiteral("更新安装包不存在或不是普通文件：")));
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
    QCOMPARE(settings.updatePolicy(), 0);
    QCOMPARE(settings.updateManualProxyUrl(), QStringLiteral("http://127.0.0.1:7890"));
    settings.setAutoInstallUpdates(true);
    settings.setUpdatePolicy(1);
    settings.setPendingUpdateVersion(QStringLiteral("v0.1.145"));
    settings.setPendingUpdateInstallerPath(QStringLiteral("C:/Updates/CineVault-Setup-v0.1.145.exe"));
    settings.setPendingUpdateInstallerSize(123456);
    settings.setPendingUpdateInstallerSha256(QString(64, QLatin1Char('a')));
    settings.setScheduledUpdateVersion(QStringLiteral("v0.1.145"));
    settings.sync();

    QVERIFY(!settings.autoInstallUpdates());
    QCOMPARE(settings.updatePolicy(), 1);
    QCOMPARE(settings.scheduledUpdateVersion(), QStringLiteral("v0.1.145"));

    settings.clearPendingUpdate();
    QVERIFY(settings.pendingUpdateVersion().isEmpty());
    QVERIFY(settings.pendingUpdateInstallerPath().isEmpty());
    QCOMPARE(settings.pendingUpdateInstallerSize(), qint64{0});
    QVERIFY(settings.pendingUpdateInstallerSha256().isEmpty());
    QVERIFY(settings.scheduledUpdateVersion().isEmpty());
    QVERIFY(!settings.autoInstallUpdates());
    QCOMPARE(settings.updatePolicy(), 1);
}

QTEST_APPLESS_MAIN(UpdateServiceTest)

#include "UpdateServiceTest.moc"
