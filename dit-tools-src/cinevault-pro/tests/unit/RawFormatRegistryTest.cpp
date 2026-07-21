#include "core/scan/FileTypeService.h"
#include "core/thumbnail/RawFormatRegistry.h"

#include <QTest>

class RawFormatRegistryTest final : public QObject {
    Q_OBJECT

private slots:
    void coversRequiredExtensions();
    void preservesProviderOrder();
    void acceptsCaseAndUnicodePaths();
    void rejectsUnknownFormats();
    void identifiesStrongTiffRawSignatures();
    void identifiesCr3IsoBmffSignature();
};

void RawFormatRegistryTest::coversRequiredExtensions()
{
    const QStringList expected = QStringLiteral(
        "3fr ari arw bay bmq cap cine cr2 cr3 crw cs1 dc2 dcr dng erf fff gpr ia iiq "
        "kc2 kdc mdc mef mos mrw nef nrw orf pef ptx pxn qtk raf raw rdc rw2 rwl sr2 "
        "srf srw sti x3f")
                                     .split(QLatin1Char(' '));
    QCOMPARE(RawFormatRegistry::formats().size(), expected.size());
    for (const auto &extension : expected) {
        const auto *format = RawFormatRegistry::findByExtension(extension);
        QVERIFY2(format, qPrintable(QStringLiteral("缺少 RAW 扩展名：%1").arg(extension)));
        QCOMPARE(format->extension, extension);
        QCOMPARE(FileTypeService::classify(QStringLiteral("sample.") + extension), AssetType::Image);
    }
}

void RawFormatRegistryTest::preservesProviderOrder()
{
    const QVector<RawPreviewProvider> expected = {
        RawPreviewProvider::LibRawEmbeddedPreview,
        RawPreviewProvider::LibRawRenderedImage,
        RawPreviewProvider::ExifToolEmbeddedPreview,
        RawPreviewProvider::WindowsImagingComponent,
        RawPreviewProvider::FFmpeg,
        RawPreviewProvider::Placeholder,
    };
    for (const auto &format : RawFormatRegistry::formats()) {
        QVERIFY(format.preferEmbeddedPreview);
        auto formatExpected = expected;
        if (format.family == RawFormatFamily::GoPro) {
            formatExpected.prepend(RawPreviewProvider::GoProGprSdk);
        }
        QCOMPARE(format.providerCandidates, formatExpected);
    }
}

void RawFormatRegistryTest::acceptsCaseAndUnicodePaths()
{
    const auto *format = RawFormatRegistry::findByFileName(QStringLiteral("D:/素材/夜景_épreuve.ARW"));
    QVERIFY(format);
    QCOMPARE(format->extension, QStringLiteral("arw"));
    QCOMPARE(format->family, RawFormatFamily::Sony);
    QVERIFY(RawFormatRegistry::isRawExtension(QStringLiteral(".Cr3")));
    QCOMPARE(FileTypeService::classify(QStringLiteral("D:/相机卡/样片.NEF")), AssetType::Image);
}

void RawFormatRegistryTest::rejectsUnknownFormats()
{
    QVERIFY(!RawFormatRegistry::findByFileName(QStringLiteral("D:/素材/unknown.xyzraw")));
    QVERIFY(!RawFormatRegistry::findByExtension(QStringLiteral("jpg")));
    QCOMPARE(FileTypeService::classify(QStringLiteral("unknown.xyzraw")), AssetType::Other);
    QCOMPARE(FileTypeService::classify(QStringLiteral("README")), AssetType::Unknown);
}

void RawFormatRegistryTest::identifiesStrongTiffRawSignatures()
{
    QByteArray dngHeader(32, '\0');
    dngHeader.replace(0, 4, QByteArray::fromHex("49492a00"));
    dngHeader.replace(4, 4, QByteArray::fromHex("08000000"));
    dngHeader.replace(8, 2, QByteArray::fromHex("0100"));
    dngHeader.replace(10, 2, QByteArray::fromHex("12c6"));
    const auto *dng = RawFormatRegistry::findBySignature(dngHeader);
    QVERIFY(dng);
    QCOMPARE(dng->extension, QStringLiteral("dng"));
    QCOMPARE(FileTypeService::classify(QStringLiteral("无扩展名"), dngHeader), AssetType::Image);

    const QByteArray cr2Header = QByteArray::fromHex("49492a001000000043520200");
    const auto *cr2 = RawFormatRegistry::findBySignature(cr2Header);
    QVERIFY(cr2);
    QCOMPARE(cr2->extension, QStringLiteral("cr2"));

    const QByteArray ordinaryTiff = QByteArray::fromHex("49492a00080000000000");
    QVERIFY(!RawFormatRegistry::findBySignature(ordinaryTiff));
}

void RawFormatRegistryTest::identifiesCr3IsoBmffSignature()
{
    const QByteArray cr3Header = QByteArray::fromHex("000000186674797063727820000000006372782069736f6d");
    const auto *cr3 = RawFormatRegistry::findBySignature(cr3Header);
    QVERIFY(cr3);
    QCOMPARE(cr3->extension, QStringLiteral("cr3"));

    const QByteArray ordinaryMp4 = QByteArray::fromHex("000000186674797069736f6d0000000069736f6d6d703432");
    QVERIFY(!RawFormatRegistry::findBySignature(ordinaryMp4));
}

QTEST_GUILESS_MAIN(RawFormatRegistryTest)

#include "RawFormatRegistryTest.moc"
