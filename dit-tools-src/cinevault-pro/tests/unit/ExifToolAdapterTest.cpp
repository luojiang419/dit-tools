#include "infrastructure/metadata/ExifToolAdapter.h"

#include <QtTest>

#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QTemporaryDir>

class ExifToolAdapterTest final : public QObject {
    Q_OBJECT

private slots:
    void bundledRuntime_readsNormalizedMetadataAndRedactsSerialNumber()
    {
        ExifToolAdapter adapter;
        QVERIFY2(adapter.isAvailable(), qPrintable(adapter.unavailableReason()));
        QVERIFY(!adapter.version().trimmed().isEmpty());

        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const auto imagePath = tempDir.filePath(QStringLiteral("metadata-fixture.jpg"));
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(QColor(QStringLiteral("#2f6feb")));
        QVERIFY2(image.save(imagePath, "JPEG"), "无法创建 JPEG 元数据测试文件");

        QProcess writer;
        writer.setProgram(adapter.executablePath());
        writer.setArguments({
            QStringLiteral("-overwrite_original"),
            QStringLiteral("-EXIF:Make=CineVault Test Make"),
            QStringLiteral("-EXIF:Model=CineVault Test Model"),
            QStringLiteral("-XMP-aux:SerialNumber=SECRET-DEVICE-123"),
            QStringLiteral("-EXIF:DateTimeOriginal=2026:07:15 12:34:56"),
            imagePath
        });
        writer.start();
        QVERIFY2(writer.waitForStarted(5000), qPrintable(writer.errorString()));
        QVERIFY2(writer.waitForFinished(30000), "ExifTool 写入测试元数据超时");
        QCOMPARE(writer.exitStatus(), QProcess::NormalExit);
        QCOMPARE(writer.exitCode(), 0);

        const QFileInfo imageInfo(imagePath);
        AssetFile asset;
        asset.id = 42;
        asset.sourceRootId = 1;
        asset.name = imageInfo.fileName();
        asset.extension = imageInfo.suffix().toLower();
        asset.absolutePath = imageInfo.absoluteFilePath();
        asset.relativePath = imageInfo.fileName();
        asset.parentPath = imageInfo.absolutePath();
        asset.assetType = AssetType::Image;
        asset.sizeBytes = imageInfo.size();
        asset.modifiedAt = imageInfo.lastModified().toString(Qt::ISODateWithMs);
        asset.readable = true;

        const auto results = adapter.extract({asset});
        QCOMPARE(results.size(), 1);
        const auto &result = results.first();
        QCOMPARE(result.assetId, qint64{42});
        QCOMPARE(result.status, ProbeStatus::Success);
        QCOMPARE(result.cameraMake, QStringLiteral("CineVault Test Make"));
        QCOMPARE(result.cameraModel, QStringLiteral("CineVault Test Model"));
        QVERIFY(result.captureTime.startsWith(QStringLiteral("2026-07-15T12:34:56")));
        QVERIFY2(!result.cameraSerialHash.isEmpty(), qPrintable(result.rawJson));
        QVERIFY(!result.rawJson.contains(QStringLiteral("SECRET-DEVICE-123")));
        QVERIFY(!result.searchText.contains(QStringLiteral("SECRET-DEVICE-123")));
        QVERIFY(result.searchText.contains(QStringLiteral("CineVault Test Make")));
        QVERIFY(result.width > 0);
        QVERIFY(result.height > 0);
    }
};

QTEST_GUILESS_MAIN(ExifToolAdapterTest)

#include "ExifToolAdapterTest.moc"
