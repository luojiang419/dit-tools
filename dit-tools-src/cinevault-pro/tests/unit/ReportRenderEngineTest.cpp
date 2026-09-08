#include "core/report/ReportRenderEngine.h"

#include <QDir>
#include <QImage>
#include <QFontDatabase>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

namespace {
ReportDocument fixture()
{
    ReportDocument document;
    document.project.projectName = QStringLiteral("视频元数据布局验收");
    document.project.exportTime = QStringLiteral("2026-09-07 12:00:00");
    document.sections = {false, false, false, false, false, true, false, false};
    for (int index = 1; index <= 13; ++index) {
        ReportAssetRow asset;
        asset.id = index;
        asset.name = QStringLiteral("Clip%1_城市街头与自然风光_拍摄素材.mp4").arg(index, 2, 10, QLatin1Char('0'));
        asset.extension = QStringLiteral("mp4");
        asset.parentPath = QStringLiteral("D:/项目素材/2026秋季品牌短片/Camera A/第一天外景");
        asset.modifiedAt = QStringLiteral("2026-09-07T10:30:15");
        asset.assetType = AssetType::Video;
        asset.sizeBytes = 32400128;
        asset.durationMs = 58900;
        asset.bitRate = 5200000;
        asset.probeStatus = ProbeStatus::Success;
        asset.streams = {{QStringLiteral("video"), QStringLiteral("h264"), 5000000, 3840, 2160, 0, 0},
                         {QStringLiteral("audio"), QStringLiteral("aac"), 192000, 0, 0, 2, 48000}};
        // An optional real thumbnail may be supplied for manual visual QA.
        asset.thumbnailPath = qEnvironmentVariable("CINEVAULT_REPORT_TEST_THUMBNAIL");
        if (index == 2) {
            asset.name = QStringLiteral("Clip02_超长文件名验证_") + QString(160, QLatin1Char('W')) + QStringLiteral(".mp4");
            asset.parentPath += QStringLiteral("/多层级素材归档与客户审核").repeated(12);
        }
        if (index == 3) {
            asset.streams.clear();
            asset.durationMs = 0;
            asset.bitRate = 0;
            asset.probeStatus = ProbeStatus::Failed;
            asset.metadataError = QStringLiteral("测试异常：无法读取视频流，文件可能尚未复制完成。");
        }
        document.assets.append(asset);
        document.totalSizeBytes += asset.sizeBytes;
    }
    document.totalFiles = document.assets.size();
    document.videoCount = document.assets.size();
    return document;
}
}

class ReportRenderEngineTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
#ifdef Q_OS_WIN
        // The offscreen platform does not enumerate the Windows font registry.
        const auto fontPath = QDir(qEnvironmentVariable("WINDIR", QStringLiteral("C:/Windows")))
            .filePath(QStringLiteral("Fonts/msyh.ttc"));
        QVERIFY(QFontDatabase::addApplicationFont(fontPath) >= 0);
#endif
    }

    void metadataPagesContainEveryAssetAndRepeatHeaders()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto output = qEnvironmentVariable("CINEVAULT_REPORT_TEST_OUTPUT", temp.path());
        QVERIFY(QDir().mkpath(output));
        const auto pdfPath = QDir(output).filePath(QStringLiteral("video-metadata.pdf"));
        ReportRenderEngine engine;
        const auto document = fixture();
        QString error;
        QVERIFY2(engine.renderPdf(document, pdfPath, &error), qPrintable(error));
        QPdfDocument pdf;
        QCOMPARE(pdf.load(pdfPath), QPdfDocument::Error::None);
        QVERIFY(pdf.pageCount() >= 2);
        QString allText;
        for (int page = 0; page < pdf.pageCount(); ++page) {
            const auto text = pdf.getAllText(page).text();
            QVERIFY2(text.contains(QStringLiteral("文件信息")), qPrintable(text));
            QVERIFY(text.contains(QStringLiteral("画面参数")));
            QVERIFY(text.contains(QStringLiteral("时长与音频")));
            QCOMPARE(text.count(QStringLiteral("视频元数据明细")), 1);
            const auto image = pdf.render(page, QSize(1684, 1191));
            QVERIFY(!image.isNull());
            QVERIFY(image.save(QDir(output).filePath(QStringLiteral("pdf-page-%1.png").arg(page + 1))));
            allText += text;
        }
        for (int index = 1; index <= 13; ++index) {
            QCOMPARE(allText.count(QStringLiteral("Clip%1_").arg(index, 2, 10, QLatin1Char('0'))), 1);
        }
        QVERIFY(allText.contains(QStringLiteral("48000")));
        QVERIFY(allText.contains(QStringLiteral("aac")));
        QVERIFY(allText.contains(QStringLiteral("测试异常")));
        QVERIFY(allText.contains(QStringLiteral("未检测")));
        QVERIFY(allText.contains(QStringLiteral("大小")));
        QStringList previews;
        QVERIFY2(engine.renderPreviewImages(document, QDir(output).filePath(QStringLiteral("preview")),
                                           &previews, &error), qPrintable(error));
        QCOMPARE(previews.size(), pdf.pageCount());
    }

    void emptyVideoSectionRemainsReadable()
    {
        QTemporaryDir temp;
        auto document = fixture();
        document.assets.clear();
        ReportRenderEngine engine;
        QString error;
        const auto path = temp.filePath(QStringLiteral("empty.pdf"));
        QVERIFY2(engine.renderPdf(document, path, &error), qPrintable(error));
        QPdfDocument pdf;
        QCOMPARE(pdf.load(path), QPdfDocument::Error::None);
        QCOMPARE(pdf.pageCount(), 1);
        QVERIFY(pdf.getAllText(0).text().contains(QStringLiteral("当前项目没有视频元数据")));
    }

    void coverAndSummaryShareFirstPage()
    {
        QTemporaryDir temp;
        auto document = fixture();
        document.sections = {true, true, false, false, false, false, false, false};
        ReportRenderEngine engine;
        QString error;
        const auto path = temp.filePath(QStringLiteral("cover-summary.pdf"));
        QVERIFY2(engine.renderPdf(document, path, &error), qPrintable(error));
        QPdfDocument pdf;
        QCOMPARE(pdf.load(path), QPdfDocument::Error::None);
        QCOMPARE(pdf.pageCount(), 1);
        const auto text = pdf.getAllText(0).text();
        QVERIFY(text.contains(QStringLiteral("项目信息")));
        QVERIFY(text.contains(QStringLiteral("项目摘要")));
        QVERIFY(text.contains(QStringLiteral("警告")));
        QStringList previews;
        QVERIFY2(engine.renderPreviewImages(document, temp.filePath(QStringLiteral("preview")),
                                           &previews, &error), qPrintable(error));
        QCOMPARE(previews.size(), 1);
    }

    void shortAndEmptySectionsFlowOnOnePage()
    {
        QTemporaryDir temp;
        ReportDocument document;
        document.sections.cover = false;
        document.sections.thumbnailIndex = false;
        ReportRenderEngine engine;
        QString error;
        const auto path = temp.filePath(QStringLiteral("short-sections.pdf"));
        QVERIFY2(engine.renderPdf(document, path, &error), qPrintable(error));
        QPdfDocument pdf;
        QCOMPARE(pdf.load(path), QPdfDocument::Error::None);
        QCOMPARE(pdf.pageCount(), 1);
        const auto text = pdf.getAllText(0).text();
        for (const auto &label : {QStringLiteral("项目摘要"), QStringLiteral("素材源与扫描概览"),
                                 QStringLiteral("格式分布"), QStringLiteral("视频元数据明细"),
                                 QStringLiteral("音频元数据明细"), QStringLiteral("项目文件夹结构树状图")}) {
            QCOMPARE(text.count(label), 1);
        }
        QVERIFY(text.contains(QStringLiteral("当前项目还没有可导出的文件树")));
    }

    void sectionBoundariesKeepTitlesWithContent_data()
    {
        QTest::addColumn<int>("sourceCount");
        QTest::addColumn<int>("section");
        // Sweep the preceding table through page-end positions, including
        // enough room for a title/header alone but not its first data row.
        for (int count = 15; count <= 29; ++count) {
            for (int section = 0; section < 5; ++section) {
                QTest::newRow(qPrintable(QStringLiteral("sources-%1-section-%2").arg(count).arg(section)))
                    << count << section;
            }
        }
    }

    void audioAndTreeContinueWithoutLosingRows()
    {
        QTemporaryDir temp;
        auto document = fixture();
        auto audio = document.assets.first();
        audio.assetType = AssetType::Audio;
        document.assets.clear();
        document.sections = {false, true, false, false, false, false, true, true};
        for (int index = 0; index < 35; ++index) {
            audio.name = QStringLiteral("Audio%1END.wav").arg(index);
            document.assets.append(audio);
        }
        for (int index = 0; index < 100; ++index) {
            document.treeLines.append({QStringLiteral("Tree%1END").arg(index), 1, false});
        }
        QString error;
        ReportRenderEngine engine;
        const auto path = QDir(qEnvironmentVariable("CINEVAULT_REPORT_TEST_OUTPUT", temp.path()))
            .filePath(QStringLiteral("audio-tree.pdf"));
        QVERIFY2(engine.renderPdf(document, path, &error), qPrintable(error));
        QPdfDocument pdf;
        QCOMPARE(pdf.load(path), QPdfDocument::Error::None);
        QVERIFY(pdf.pageCount() > 2);
        QString allText;
        for (int page = 0; page < pdf.pageCount(); ++page) {
            const auto text = pdf.getAllText(page).text();
            if (text.contains(QStringLiteral("Audio"))) {
                QVERIFY(text.contains(QStringLiteral("编码/流")));
                QVERIFY(text.contains(QStringLiteral("相对路径")));
            }
            allText += text;
        }
        // PDFium may insert line breaks between glyphs in a dense file tree.
        // Ignore extraction whitespace when checking unique ASCII row markers.
        allText.remove(QRegularExpression(QStringLiteral("\\s+")));
        for (int index = 0; index < 35; ++index) {
            QCOMPARE(allText.count(QStringLiteral("Audio%1END").arg(index)), 1);
        }
        for (int index = 0; index < 100; ++index) {
            const auto marker = QStringLiteral("Tree%1END").arg(index);
            QVERIFY2(allText.count(marker) == 1, qPrintable(marker));
        }
        QStringList previews;
        QVERIFY2(engine.renderPreviewImages(document, temp.filePath(QStringLiteral("preview")),
                                           &previews, &error), qPrintable(error));
        QCOMPARE(previews.size(), pdf.pageCount());
    }

    void noSelectedSectionsProducesSingleExplanationPage()
    {
        QTemporaryDir temp;
        ReportDocument document;
        document.sections = {false, false, false, false, false, false, false, false};
        QString error;
        ReportRenderEngine engine;
        const auto path = temp.filePath(QStringLiteral("none.pdf"));
        QVERIFY2(engine.renderPdf(document, path, &error), qPrintable(error));
        QPdfDocument pdf;
        QCOMPARE(pdf.load(path), QPdfDocument::Error::None);
        QCOMPARE(pdf.pageCount(), 1);
        QVERIFY(pdf.getAllText(0).text().contains(QStringLiteral("未选择任何报表项")));
    }

    void sectionBoundariesKeepTitlesWithContent()
    {
        QFETCH(int, sourceCount);
        QFETCH(int, section);
        QTemporaryDir temp;
        auto document = fixture();
        document.assets = {document.assets.at(1)}; // Tall first row: wrapped name/path.
        document.sections = {false, false, true, false, section == 0, section == 1,
                             section == 2, section >= 3};
        for (int index = 0; index < sourceCount; ++index) {
            ReportSourceSummary source;
            source.name = QStringLiteral("Source%1END").arg(index);
            source.path = QStringLiteral("D:/Source%1").arg(index);
            document.sources.append(source);
        }
        if (section == 2) {
            document.assets[0].assetType = AssetType::Audio;
        }
        if (section == 3) {
            document.treeLines = {{QStringLiteral("TreeFirstRow"), 0, true}};
        }
        const QStringList titles = {QStringLiteral("视频缩略图索引"), QStringLiteral("视频元数据明细"),
            QStringLiteral("音频元数据明细"), QStringLiteral("项目文件夹结构树状图"),
            QStringLiteral("项目文件夹结构树状图")};
        const QStringList bodies = {QStringLiteral("Clip02_"), QStringLiteral("Clip02_"),
            QStringLiteral("Clip02_"), QStringLiteral("TreeFirstRow"),
            QStringLiteral("当前项目还没有可导出的文件树")};
        ReportRenderEngine engine;
        QString error;
        const auto path = temp.filePath(QStringLiteral("boundary.pdf"));
        QVERIFY2(engine.renderPdf(document, path, &error), qPrintable(error));
        QPdfDocument pdf;
        QCOMPARE(pdf.load(path), QPdfDocument::Error::None);
        QString allText;
        for (int page = 0; page < pdf.pageCount(); ++page) {
            const auto text = pdf.getAllText(page).text();
            if (text.contains(titles.at(section))) {
                QVERIFY2(text.contains(bodies.at(section)), qPrintable(text));
            }
            if (text.contains(QStringLiteral("容量"))) {
                QVERIFY2(text.contains(QStringLiteral("Source")), qPrintable(text));
            }
            if (text.contains(QStringLiteral("Source"))) {
                QVERIFY2(text.contains(QStringLiteral("容量")), qPrintable(text));
                QVERIFY(text.contains(QStringLiteral("路径")));
            }
            allText += text;
        }
        QVERIFY(allText.contains(titles.at(section)));
        for (int index = 0; index < sourceCount; ++index) {
            QCOMPARE(allText.count(QStringLiteral("Source%1END").arg(index)), 1);
        }
        QStringList previews;
        QVERIFY2(engine.renderPreviewImages(document, temp.filePath(QStringLiteral("preview")),
                                           &previews, &error), qPrintable(error));
        QCOMPARE(previews.size(), pdf.pageCount());
    }

    void fullReportKeepsAllSectionsAndMatchingPreviews()
    {
        QTemporaryDir temp;
        const auto output = qEnvironmentVariable("CINEVAULT_REPORT_TEST_OUTPUT", temp.path());
        auto document = fixture();
        document.sections = {};
        ReportSourceSummary source;
        source.name = QStringLiteral("Camera A");
        source.path = QStringLiteral("D:/项目素材/2026秋季品牌短片/Camera A");
        source.totalFiles = 14;
        source.videoCount = 13;
        source.audioCount = 1;
        source.totalSizeBytes = document.totalSizeBytes;
        document.sources.append(source);
        auto audio = document.assets.first();
        audio.name = QStringLiteral("现场同期声.wav");
        audio.extension = QStringLiteral("wav");
        audio.assetType = AssetType::Audio;
        audio.relativePath = QStringLiteral("Audio/现场同期声.wav");
        audio.streams = {{QStringLiteral("audio"), QStringLiteral("pcm_s24le"), 2304000, 0, 0, 2, 48000}};
        document.assets.append(audio);
        document.audioCount = 1;
        document.totalFiles = 14;
        document.treeLines = {{QStringLiteral("Camera A"), 0, true},
                              {document.assets.first().name, 1, false},
                              {QStringLiteral("Audio"), 0, true}, {audio.name, 1, false}};
        const auto path = QDir(output).filePath(QStringLiteral("full-report.pdf"));
        QString error;
        ReportRenderEngine engine;
        QVERIFY2(engine.renderPdf(document, path, &error), qPrintable(error));
        QPdfDocument pdf;
        QCOMPARE(pdf.load(path), QPdfDocument::Error::None);
        QVERIFY2(pdf.pageCount() <= 6, qPrintable(QString::number(pdf.pageCount())));
        QString text;
        for (int page = 0; page < pdf.pageCount(); ++page) {
            text += pdf.getAllText(page).text();
        }
        for (const auto &section : {QStringLiteral("项目摘要"), QStringLiteral("素材源与扫描概览"),
                                   QStringLiteral("格式分布"), QStringLiteral("视频缩略图索引"),
                                   QStringLiteral("视频元数据明细"), QStringLiteral("音频元数据明细"),
                                   QStringLiteral("项目文件夹结构树状图")}) {
            QVERIFY2(text.contains(section), qPrintable(section));
        }
        QStringList pages;
        QVERIFY2(engine.renderPreviewImages(document, QDir(output).filePath(QStringLiteral("full-preview")),
                                           &pages, &error), qPrintable(error));
        QCOMPARE(pages.size(), pdf.pageCount());
    }
};

QTEST_MAIN(ReportRenderEngineTest)
#include "ReportRenderEngineTest.moc"
