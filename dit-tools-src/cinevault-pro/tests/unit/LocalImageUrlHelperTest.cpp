#include "ui/imaging/LocalImageUrlHelper.h"

#include <QDir>
#include <QUrl>
#include <QtTest>

class LocalImageUrlHelperTest : public QObject {
    Q_OBJECT

private slots:
    void sourceForInput_returnsFileUrlForRegularImages();
    void sourceForInput_routesWebpThroughProvider();
    void sourceForInput_keepsProviderSourceUnchanged();
    void sourceForInput_keepsRemoteUrlUnchanged();
};

void LocalImageUrlHelperTest::sourceForInput_returnsFileUrlForRegularImages()
{
    const auto localPath = QDir::cleanPath(QStringLiteral("C:/cinevault/test-frame.jpg"));
    QCOMPARE(LocalImageUrlHelper::sourceForInputString(localPath),
             QUrl::fromLocalFile(localPath).toString());
}

void LocalImageUrlHelperTest::sourceForInput_routesWebpThroughProvider()
{
    const auto localPath = QDir::cleanPath(QStringLiteral("C:/cinevault/test-frame.webp"));
    const auto fileUrl = QUrl::fromLocalFile(localPath).toString();
    const auto expected = QStringLiteral("image://cinevault-local/%1")
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(fileUrl)));
    QCOMPARE(LocalImageUrlHelper::sourceForInputString(localPath), expected);
    QCOMPARE(LocalImageUrlHelper::sourceForInputString(fileUrl), expected);
}

void LocalImageUrlHelperTest::sourceForInput_keepsProviderSourceUnchanged()
{
    const auto providerSource = QStringLiteral("image://cinevault-local/file%3A%2F%2F%2FC%3A%2Fcinevault%2Ftest-frame.webp");
    QCOMPARE(LocalImageUrlHelper::sourceForInputString(providerSource), providerSource);
}

void LocalImageUrlHelperTest::sourceForInput_keepsRemoteUrlUnchanged()
{
    const auto httpUrl = QStringLiteral("http://127.0.0.1:3021/files/demo/frame.jpg");
    const auto httpsUrl = QStringLiteral("https://example.com/feedback/frame.webp");
    QCOMPARE(LocalImageUrlHelper::sourceForInputString(httpUrl), httpUrl);
    QCOMPARE(LocalImageUrlHelper::sourceForInputString(httpsUrl), httpsUrl);
}

QTEST_APPLESS_MAIN(LocalImageUrlHelperTest)

#include "LocalImageUrlHelperTest.moc"
