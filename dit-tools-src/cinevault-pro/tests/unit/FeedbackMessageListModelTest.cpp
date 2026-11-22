#include "ui/models/FeedbackMessageListModel.h"

#include <QtTest>

namespace {
FeedbackAttachment makeAttachment(const QString &name,
                                  const QString &mimeType,
                                  const QString &url)
{
    FeedbackAttachment attachment;
    attachment.id = QStringLiteral("attachment-1");
    attachment.name = name;
    attachment.mimeType = mimeType;
    attachment.url = url;
    attachment.sizeBytes = 2048;
    return attachment;
}

FeedbackMessage makeMessage(qint64 id, const QString &text)
{
    FeedbackMessage message;
    message.id = id;
    message.senderRole = QStringLiteral("client");
    message.text = text;
    message.createdAt = QStringLiteral("2026-07-01T10:00:00Z");
    return message;
}
}

class FeedbackMessageListModelTest : public QObject {
    Q_OBJECT

private slots:
    void attachmentPreviewFlags_data();
    void attachmentPreviewFlags();
    void appendsRowsWithoutReset();
    void updatesRowsWithoutReset();
};

void FeedbackMessageListModelTest::attachmentPreviewFlags_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("mimeType");
    QTest::addColumn<QString>("url");
    QTest::addColumn<QString>("expectedPreviewKind");
    QTest::addColumn<bool>("expectedPreviewable");
    QTest::addColumn<bool>("expectedDownloadable");

    const auto remoteUrl = QStringLiteral("https://example.com/attachments/demo");

    QTest::newRow("mime-image-png") << QStringLiteral("frame.bin")
                                    << QStringLiteral("image/png")
                                    << remoteUrl
                                    << QStringLiteral("image")
                                    << true
                                    << true;
    QTest::newRow("extension-image-jpg") << QStringLiteral("frame.jpg")
                                         << QString()
                                         << remoteUrl
                                         << QStringLiteral("image")
                                         << true
                                         << true;
    QTest::newRow("mime-video-mp4") << QStringLiteral("clip.bin")
                                    << QStringLiteral("video/mp4")
                                    << remoteUrl
                                    << QStringLiteral("video")
                                    << true
                                    << true;
    QTest::newRow("document-pdf") << QStringLiteral("manual.pdf")
                                  << QString()
                                  << remoteUrl
                                  << QStringLiteral("document")
                                  << true
                                  << true;
    QTest::newRow("document-md") << QStringLiteral("readme.md")
                                 << QString()
                                 << remoteUrl
                                 << QStringLiteral("document")
                                 << true
                                 << true;
    QTest::newRow("document-txt") << QStringLiteral("notes.txt")
                                  << QString()
                                  << remoteUrl
                                  << QStringLiteral("document")
                                  << true
                                  << true;
    QTest::newRow("document-json") << QStringLiteral("payload.json")
                                   << QString()
                                   << remoteUrl
                                   << QStringLiteral("document")
                                   << true
                                   << true;
    QTest::newRow("document-csv") << QStringLiteral("report.csv")
                                  << QString()
                                  << remoteUrl
                                  << QStringLiteral("document")
                                  << true
                                  << true;
    QTest::newRow("document-docx") << QStringLiteral("spec.docx")
                                   << QString()
                                   << remoteUrl
                                   << QStringLiteral("document")
                                   << true
                                   << true;
    QTest::newRow("document-xlsx") << QStringLiteral("sheet.xlsx")
                                   << QString()
                                   << remoteUrl
                                   << QStringLiteral("document")
                                   << true
                                   << true;
    QTest::newRow("document-pptx") << QStringLiteral("slides.pptx")
                                   << QString()
                                   << remoteUrl
                                   << QStringLiteral("document")
                                   << true
                                   << true;
    QTest::newRow("document-doc") << QStringLiteral("legacy.doc")
                                  << QString()
                                  << remoteUrl
                                  << QStringLiteral("document")
                                  << true
                                  << true;
    QTest::newRow("document-xls") << QStringLiteral("legacy.xls")
                                  << QString()
                                  << remoteUrl
                                  << QStringLiteral("document")
                                  << true
                                  << true;
    QTest::newRow("document-ppt") << QStringLiteral("legacy.ppt")
                                  << QString()
                                  << remoteUrl
                                  << QStringLiteral("document")
                                  << true
                                  << true;
    QTest::newRow("mime-text-plain") << QStringLiteral("stdout.bin")
                                     << QStringLiteral("text/plain")
                                     << remoteUrl
                                     << QStringLiteral("document")
                                     << true
                                     << true;
    QTest::newRow("archive-zip") << QStringLiteral("bundle.zip")
                                 << QStringLiteral("application/zip")
                                 << remoteUrl
                                 << QString()
                                 << false
                                 << true;
    QTest::newRow("archive-7z") << QStringLiteral("bundle.7z")
                                << QString()
                                << remoteUrl
                                << QString()
                                << false
                                << true;
    QTest::newRow("archive-rar") << QStringLiteral("bundle.rar")
                                 << QString()
                                 << remoteUrl
                                 << QString()
                                 << false
                                 << true;
    QTest::newRow("blank-download-url") << QStringLiteral("frame.png")
                                        << QStringLiteral("image/png")
                                        << QString()
                                        << QStringLiteral("image")
                                        << true
                                        << false;
}

void FeedbackMessageListModelTest::attachmentPreviewFlags()
{
    QFETCH(QString, name);
    QFETCH(QString, mimeType);
    QFETCH(QString, url);
    QFETCH(QString, expectedPreviewKind);
    QFETCH(bool, expectedPreviewable);
    QFETCH(bool, expectedDownloadable);

    FeedbackMessage message;
    message.id = 1;
    message.senderRole = QStringLiteral("client");
    message.text = QStringLiteral("attachment");
    message.attachments = {makeAttachment(name, mimeType, url)};
    message.createdAt = QStringLiteral("2026-07-01T10:00:00Z");

    FeedbackMessageListModel model;
    model.setItems({message});

    QCOMPARE(model.rowCount({}), 1);
    const auto rowIndex = model.index(0, 0);
    QVERIFY(rowIndex.isValid());

    const auto attachments = model.data(rowIndex, FeedbackMessageListModel::AttachmentsRole).toList();
    QCOMPARE(attachments.size(), 1);

    const auto attachmentRow = attachments.constFirst().toMap();
    QCOMPARE(attachmentRow.value(QStringLiteral("previewKind")).toString(), expectedPreviewKind);
    QCOMPARE(attachmentRow.value(QStringLiteral("isPreviewable")).toBool(), expectedPreviewable);
    QCOMPARE(attachmentRow.value(QStringLiteral("canDownload")).toBool(), expectedDownloadable);
    QCOMPARE(attachmentRow.value(QStringLiteral("isImage")).toBool(), expectedPreviewKind == QStringLiteral("image"));
    QCOMPARE(attachmentRow.value(QStringLiteral("isVideo")).toBool(), expectedPreviewKind == QStringLiteral("video"));
}

void FeedbackMessageListModelTest::appendsRowsWithoutReset()
{
    FeedbackMessageListModel model;
    model.setItems({makeMessage(1, QStringLiteral("first"))});

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy rowsInsertedSpy(&model, &QAbstractItemModel::rowsInserted);

    model.setItems({
        makeMessage(1, QStringLiteral("first")),
        makeMessage(2, QStringLiteral("second"))
    });

    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(rowsInsertedSpy.count(), 1);
    const auto arguments = rowsInsertedSpy.takeFirst();
    QCOMPARE(arguments.at(1).toInt(), 1);
    QCOMPARE(arguments.at(2).toInt(), 1);
    QCOMPARE(model.rowCount({}), 2);
    QCOMPARE(model.data(model.index(1, 0), FeedbackMessageListModel::TextRole).toString(),
             QStringLiteral("second"));
}

void FeedbackMessageListModelTest::updatesRowsWithoutReset()
{
    FeedbackMessageListModel model;
    model.setItems({makeMessage(1, QStringLiteral("draft"))});

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy rowsInsertedSpy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy dataChangedSpy(&model, &QAbstractItemModel::dataChanged);

    model.setItems({makeMessage(1, QStringLiteral("confirmed"))});

    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(rowsInsertedSpy.count(), 0);
    QCOMPARE(dataChangedSpy.count(), 1);
    QCOMPARE(model.rowCount({}), 1);
    QCOMPARE(model.data(model.index(0, 0), FeedbackMessageListModel::TextRole).toString(),
             QStringLiteral("confirmed"));
}

QTEST_APPLESS_MAIN(FeedbackMessageListModelTest)

#include "FeedbackMessageListModelTest.moc"
