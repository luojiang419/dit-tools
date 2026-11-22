#include "ui/imaging/LocalImageProvider.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

class LocalImageProviderTest : public QObject {
    Q_OBJECT

private slots:
    void requestImage_decodesWebpWithoutQtPlugin();
};

void LocalImageProviderTest::requestImage_decodesWebpWithoutQtPlugin()
{
    static const QByteArray kSampleWebp =
        QByteArray::fromBase64("UklGRjAAAABXRUJQVlA4TCMAAAAvAUAAEB8gEEjeHzqN+RcQFPwfnYCg6LrlImYPwg0YIvofAgA=");

    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "temporary directory should be created");

    const auto localPath = QDir(tempDir.path()).filePath(QStringLiteral("sample.webp"));
    QFile file(localPath);
    QVERIFY2(file.open(QIODevice::WriteOnly), "sample webp file should be writable");
    QCOMPARE(file.write(kSampleWebp), kSampleWebp.size());
    file.close();

    const auto providerId = QString::fromLatin1(
        QUrl::toPercentEncoding(QUrl::fromLocalFile(localPath).toString()));

    LocalImageProvider provider;
    QSize reportedSize;
    const auto image = provider.requestImage(providerId, &reportedSize, QSize(64, 64));

    QVERIFY2(!image.isNull(), "bundled webp decoder should produce a valid image");
    QCOMPARE(reportedSize, image.size());
    QCOMPARE(image.size(), QSize(2, 2));
}

QTEST_GUILESS_MAIN(LocalImageProviderTest)

#include "LocalImageProviderTest.moc"
