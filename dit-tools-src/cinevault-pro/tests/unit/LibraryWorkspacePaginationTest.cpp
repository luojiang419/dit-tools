#include "application/LibraryQueryService.h"
#include "infrastructure/db/DatabaseManager.h"
#include "ui/models/AssetListModel.h"
#include "ui/viewmodels/LibraryWorkspaceViewModel.h"

#include <QtTest>

#include <QElapsedTimer>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

namespace {
qint64 insertSourceRoot(QSqlDatabase db, const QString &name, qint64 totalFiles)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO source_root "
        "(name, path, status, total_files, total_folders, total_size_bytes, video_count, audio_count, image_count, "
        "other_count, warning_count, scan_version, created_at, updated_at) "
        "VALUES (?, ?, 'ok', ?, 0, ?, ?, 0, 0, 0, 0, 1, "
        "'2026-07-21T12:00:00', '2026-07-21T12:00:00')"));
    query.addBindValue(name);
    query.addBindValue(QStringLiteral("G:/fixtures/%1").arg(name));
    query.addBindValue(totalFiles);
    query.addBindValue(totalFiles * 10);
    query.addBindValue(totalFiles);
    if (!query.exec()) {
        return 0;
    }
    return query.lastInsertId().toLongLong();
}

bool insertAssets(QSqlDatabase db, qint64 sourceRootId, const QString &prefix, int count)
{
    if (!db.transaction()) {
        return false;
    }
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO asset_file "
        "(source_root_id, name, extension, absolute_path, relative_path, parent_path, asset_type, size_bytes, "
        "modified_at, is_readable, created_at) "
        "VALUES (?, ?, 'mp4', ?, ?, '', ?, 10, '2026-07-21T12:00:00', 1, '2026-07-21T12:00:00')"));
    for (int index = 0; index < count; ++index) {
        const auto name = QStringLiteral("%1-%2.mp4").arg(prefix).arg(index, 5, 10, QLatin1Char('0'));
        query.bindValue(0, sourceRootId);
        query.bindValue(1, name);
        query.bindValue(2, QStringLiteral("G:/fixtures/%1/%2").arg(prefix, name));
        query.bindValue(3, name);
        query.bindValue(4, static_cast<int>(AssetType::Video));
        if (!query.exec()) {
            db.rollback();
            return false;
        }
    }
    return db.commit();
}

QVector<qint64> modelIds(const AssetListModel *model)
{
    QVector<qint64> ids;
    for (int row = 0; row < model->rowCount(QModelIndex()); ++row) {
        ids.append(model->data(model->index(row, 0), AssetListModel::IdRole).toLongLong());
    }
    return ids;
}
}

class LibraryWorkspacePaginationTest final : public QObject {
    Q_OBJECT

private slots:
    void firstPageAndKeysetLoadMoreStayBounded();
    void staleGenerationCannotReplaceNewFilter();
    void assetModelUpdatesRowsWithoutReset();
};

void LibraryWorkspacePaginationTest::firstPageAndKeysetLoadMoreStayBounded()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    DatabaseManager databaseManager;
    QString errorMessage;
    const auto databasePath = temp.filePath(QStringLiteral("library.cvdb"));
    QVERIFY2(databaseManager.openProjectDatabase(databasePath, &errorMessage), qPrintable(errorMessage));
    auto db = databaseManager.database();

    const auto firstSourceId = insertSourceRoot(db, QStringLiteral("first"), 450);
    const auto secondSourceId = insertSourceRoot(db, QStringLiteral("second"), 5);
    QVERIFY(firstSourceId > 0);
    QVERIFY(secondSourceId > 0);
    QVERIFY2(insertAssets(db, firstSourceId, QStringLiteral("first"), 450), qPrintable(db.lastError().text()));
    QVERIFY2(insertAssets(db, secondSourceId, QStringLiteral("second"), 5), qPrintable(db.lastError().text()));

    LibraryQueryService queryService(&databaseManager, nullptr);
    LibraryWorkspaceViewModel viewModel(&queryService);
    QElapsedTimer callElapsed;
    callElapsed.start();
    viewModel.reload();
    QVERIFY2(callElapsed.elapsed() < 50, "reload 必须只派发后台查询，不得同步扫描素材表");

    QTRY_VERIFY_WITH_TIMEOUT(!viewModel.loading() && viewModel.loadedAssetCount() == 200, 5000);
    QVERIFY(viewModel.hasMore());
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.totalAssetCount(), qint64{455}, 5000);

    viewModel.loadMore();
    QTRY_VERIFY_WITH_TIMEOUT(!viewModel.loading() && viewModel.loadedAssetCount() == 400, 5000);
    QVERIFY(viewModel.hasMore());
    viewModel.loadMore();
    QTRY_VERIFY_WITH_TIMEOUT(!viewModel.loading() && viewModel.loadedAssetCount() == 455, 5000);
    QVERIFY(!viewModel.hasMore());

    const auto ids = modelIds(viewModel.model());
    QCOMPARE(ids.size(), 455);
    QCOMPARE(QSet<qint64>(ids.cbegin(), ids.cend()).size(), 455);
    for (qsizetype index = 1; index < ids.size(); ++index) {
        QVERIFY2(ids.at(index - 1) > ids.at(index), "相同 modified_at 必须以 id 倒序稳定分页");
    }

    viewModel.toggleModifiedTimeOrder();
    QTRY_VERIFY_WITH_TIMEOUT(!viewModel.loading() && viewModel.loadedAssetCount() == 200, 5000);
    const auto ascendingIds = modelIds(viewModel.model());
    for (qsizetype index = 1; index < ascendingIds.size(); ++index) {
        QVERIFY2(ascendingIds.at(index - 1) < ascendingIds.at(index),
                 "相同 modified_at 必须以 id 正序稳定分页");
    }

    QSqlQuery alterTotal(db);
    alterTotal.prepare(QStringLiteral("UPDATE source_root SET total_files = 999 WHERE id = ?"));
    alterTotal.addBindValue(firstSourceId);
    QVERIFY(alterTotal.exec());
    LibraryAssetPageRequest countRequest;
    const auto fastCount = queryService.assetCountForPath(databasePath, countRequest);
    QVERIFY2(fastCount.errorMessage.isEmpty(), qPrintable(fastCount.errorMessage));
    QCOMPARE(fastCount.count, qint64{1004});

    viewModel.waitForIdle();
    databaseManager.closeProjectDatabase();
}

void LibraryWorkspacePaginationTest::assetModelUpdatesRowsWithoutReset()
{
    AssetListModel model;
    AssetFile first;
    first.id = 1;
    first.name = QStringLiteral("first");
    AssetFile second;
    second.id = 2;
    second.name = QStringLiteral("second");
    model.setItems({first, second});

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
    second.name = QStringLiteral("updated");
    model.updateItemsById({second});

    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(model.rowCount(QModelIndex()), 2);
    QCOMPARE(model.data(model.index(1, 0), AssetListModel::NameRole).toString(),
             QStringLiteral("updated"));
}

void LibraryWorkspacePaginationTest::staleGenerationCannotReplaceNewFilter()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    DatabaseManager databaseManager;
    QString errorMessage;
    const auto databasePath = temp.filePath(QStringLiteral("generation.cvdb"));
    QVERIFY2(databaseManager.openProjectDatabase(databasePath, &errorMessage), qPrintable(errorMessage));
    auto db = databaseManager.database();

    const auto largeSourceId = insertSourceRoot(db, QStringLiteral("large"), 1200);
    const auto smallSourceId = insertSourceRoot(db, QStringLiteral("small"), 5);
    QVERIFY(insertAssets(db, largeSourceId, QStringLiteral("large"), 1200));
    QVERIFY(insertAssets(db, smallSourceId, QStringLiteral("small"), 5));

    LibraryQueryService queryService(&databaseManager, nullptr);
    LibraryWorkspaceViewModel viewModel(&queryService);
    viewModel.reload();
    viewModel.setSourceFilter(smallSourceId);
    QTRY_VERIFY_WITH_TIMEOUT(!viewModel.loading() && viewModel.loadedAssetCount() == 5, 5000);
    viewModel.waitForIdle();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);

    QCOMPARE(viewModel.loadedAssetCount(), 5);
    for (int row = 0; row < viewModel.model()->rowCount(QModelIndex()); ++row) {
        QCOMPARE(viewModel.model()->data(
                     viewModel.model()->index(row, 0),
                     AssetListModel::SourceRootIdRole).toLongLong(),
                 smallSourceId);
    }
    databaseManager.closeProjectDatabase();
}

QTEST_GUILESS_MAIN(LibraryWorkspacePaginationTest)

#include "LibraryWorkspacePaginationTest.moc"
