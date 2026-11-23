#include "infrastructure/db/DatabaseManager.h"

#include "infrastructure/db/DatabaseMigration.h"
#include "shared/FolderPathMetadata.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

namespace {
bool executeBatch(QSqlDatabase &db, const QStringList &statements, QString *errorMessage)
{
    QSqlQuery query(db);
    for (const auto &statement : statements) {
        if (!query.exec(statement)) {
            if (errorMessage) {
                *errorMessage = query.lastError().text();
            }
            return false;
        }
    }
    return true;
}

QStringList tableColumns(QSqlDatabase &db, const QString &tableName, QString *errorMessage)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(tableName))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("读取旧项目表结构失败：%1，%2").arg(tableName, query.lastError().text());
        }
        return {};
    }

    QStringList columns;
    while (query.next()) {
        columns.append(query.value(1).toString());
    }
    return columns;
}

bool ensureColumn(QSqlDatabase &db,
                  const QString &tableName,
                  const QString &columnName,
                  const QString &columnDefinition,
                  QString *errorMessage)
{
    const auto columns = tableColumns(db, tableName, errorMessage);
    if (columns.contains(columnName, Qt::CaseInsensitive)) {
        return true;
    }

    QSqlQuery query(db);
    const auto statement = QStringLiteral("ALTER TABLE %1 ADD COLUMN %2").arg(tableName, columnDefinition);
    if (!query.exec(statement)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("迁移旧项目字段失败：%1.%2，%3").arg(tableName, columnName, query.lastError().text());
        }
        return false;
    }
    return true;
}

bool ensureColumns(QSqlDatabase &db,
                   const QString &tableName,
                   const QList<QPair<QString, QString>> &columns,
                   QString *errorMessage)
{
    for (const auto &column : columns) {
        if (!ensureColumn(db, tableName, column.first, column.second, errorMessage)) {
            return false;
        }
    }
    return true;
}

QString createCatalogChangeTrigger(const QString &triggerName,
                                   const QString &tableName,
                                   const QString &eventName,
                                   const QString &entityType,
                                   const QString &entityId,
                                   const QString &entityKey,
                                   const QString &previousEntityKey,
                                   const QString &sourceRootId,
                                   const QString &previousSourceRootId,
                                   int operation,
                                   int changeMask,
                                   const QString &condition = QStringLiteral("1"))
{
    auto statement = QStringLiteral(
        "CREATE TRIGGER %1 AFTER %2 ON %3 WHEN %4 BEGIN "
        "UPDATE catalog_change_state SET next_log_id = next_log_id + 1 WHERE singleton_id = 1; "
        "INSERT INTO catalog_change_log "
        "(log_id, entity_type, entity_id, entity_key, previous_entity_key, source_root_id, "
        "previous_source_root_id, operation, change_mask, updated_at) "
        "VALUES ((SELECT next_log_id FROM catalog_change_state WHERE singleton_id = 1), "
        "%5, %6, %7, %8, %9, %10, %11, %12, CURRENT_TIMESTAMP) "
        "ON CONFLICT(entity_type, entity_id) DO UPDATE SET "
        "log_id = excluded.log_id, "
        "entity_key = excluded.entity_key, "
        "previous_entity_key = CASE "
        "WHEN catalog_change_log.operation = 1 THEN catalog_change_log.previous_entity_key "
        "WHEN catalog_change_log.previous_entity_key = '' THEN excluded.previous_entity_key "
        "ELSE catalog_change_log.previous_entity_key END, "
        "source_root_id = excluded.source_root_id, "
        "previous_source_root_id = CASE "
        "WHEN catalog_change_log.operation = 1 THEN catalog_change_log.previous_source_root_id "
        "WHEN catalog_change_log.previous_source_root_id = 0 THEN excluded.previous_source_root_id "
        "ELSE catalog_change_log.previous_source_root_id END, "
        "operation = CASE "
        "WHEN catalog_change_log.operation = 1 AND excluded.operation = 2 THEN 1 "
        "WHEN catalog_change_log.operation = 3 AND excluded.operation = 1 THEN 2 "
        "ELSE excluded.operation END, "
        "change_mask = catalog_change_log.change_mask | excluded.change_mask, "
        "updated_at = excluded.updated_at; END")
                         .arg(triggerName)
                         .arg(eventName)
                         .arg(tableName)
                         .arg(condition)
                         .arg(entityType)
                         .arg(entityId)
                         .arg(entityKey)
                         .arg(previousEntityKey)
                         .arg(sourceRootId)
                         .arg(previousSourceRootId)
                         .arg(operation)
                         .arg(changeMask);
    return statement;
}
}

DatabaseManager::DatabaseManager(QObject *parent)
    : QObject(parent)
    , m_mainConnectionName(QStringLiteral("cinevault_main"))
{
}

bool DatabaseManager::openProjectDatabase(const QString &databaseFilePath, QString *errorMessage)
{
    closeProjectDatabase();

    const QFileInfo databaseInfo(databaseFilePath);
    const bool databaseExistedBeforeOpen = databaseInfo.exists() && databaseInfo.isFile() && databaseInfo.size() > 0;

    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_mainConnectionName);
    db.setDatabaseName(databaseFilePath);
    if (!db.open()) {
        if (errorMessage) {
            *errorMessage = db.lastError().text();
        }
        return false;
    }

    if (!initializeSchema(db, databaseExistedBeforeOpen, errorMessage)) {
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_mainConnectionName);
        return false;
    }

    m_databaseFilePath = databaseFilePath;
    return true;
}

void DatabaseManager::closeProjectDatabase()
{
    if (!QSqlDatabase::contains(m_mainConnectionName)) {
        m_databaseFilePath.clear();
        m_schemaVersion = 0;
        return;
    }

    {
        auto db = QSqlDatabase::database(m_mainConnectionName);
        db.close();
    }
    QSqlDatabase::removeDatabase(m_mainConnectionName);
    m_databaseFilePath.clear();
    m_schemaVersion = 0;
}

bool DatabaseManager::hasOpenProject() const
{
    return !m_databaseFilePath.isEmpty() && QSqlDatabase::contains(m_mainConnectionName);
}

QString DatabaseManager::databaseFilePath() const
{
    return m_databaseFilePath;
}

int DatabaseManager::schemaVersion() const
{
    return m_schemaVersion;
}

QSqlDatabase DatabaseManager::database() const
{
    return QSqlDatabase::database(m_mainConnectionName);
}

QSqlDatabase DatabaseManager::openThreadConnection(const QString &connectionName, QString *errorMessage) const
{
    return openThreadConnectionForPath(m_databaseFilePath, connectionName, errorMessage);
}

QSqlDatabase DatabaseManager::openThreadConnectionForPath(const QString &databaseFilePath,
                                                           const QString &connectionName,
                                                           QString *errorMessage) const
{
    if (databaseFilePath.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("项目数据库路径为空");
        }
        return {};
    }
    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(databaseFilePath);
    if (!db.open()) {
        if (errorMessage) {
            *errorMessage = db.lastError().text();
        }
        return db;
    }
    if (!DatabaseMigration::configureSqlite(db, errorMessage)) {
        db.close();
    }
    return db;
}

void DatabaseManager::closeThreadConnection(const QString &connectionName) const
{
    if (!QSqlDatabase::contains(connectionName)) {
        return;
    }
    {
        auto db = QSqlDatabase::database(connectionName);
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
}

bool DatabaseManager::initializeSchema(QSqlDatabase &db, bool databaseExistedBeforeOpen, QString *errorMessage)
{
    auto version = currentSchemaVersion(db);
    if (version < CurrentSchemaVersion
        && !DatabaseMigration::createUpgradeBackup(db, CurrentSchemaVersion, databaseExistedBeforeOpen, errorMessage)) {
        return false;
    }
    if (!DatabaseMigration::configureSqlite(db, errorMessage)) {
        return false;
    }
    if (!db.transaction()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法开始项目数据库迁移事务：%1").arg(db.lastError().text());
        }
        return false;
    }

    auto rollback = [&db]() {
        db.rollback();
        return false;
    };
    if (!createBaseSchema(db, errorMessage)) {
        return rollback();
    }
    if (!ensureBaseSchemaCompatibility(db, errorMessage)) {
        return rollback();
    }

    if (version < 1) {
        version = 1;
        if (!setSchemaVersion(db, version, errorMessage)) {
            return rollback();
        }
    }

    if (version < 2) {
        if (!migrateToVersion2(db, errorMessage)) {
            return rollback();
        }
        version = 2;
    } else if (!ensureMediaSchemaCompatibility(db, errorMessage)) {
        return rollback();
    }

    if (version < 3) {
        if (!migrateToVersion3(db, errorMessage)) {
            return rollback();
        }
        version = 3;
    } else if (!ensureFolderSchemaCompatibility(db, errorMessage)) {
        return rollback();
    }

    if (version < 4) {
        if (!migrateToVersion4(db, errorMessage)) {
            return rollback();
        }
        version = 4;
    } else if (!ensureAtomicScanSchemaCompatibility(db, errorMessage)) {
        return rollback();
    }

    if (version < 5) {
        if (!migrateToVersion5(db, errorMessage)) {
            return rollback();
        }
        version = 5;
    } else if (!ensureEmbeddedMetadataSchemaCompatibility(db, errorMessage)) {
        return rollback();
    }

    if (version < 6) {
        if (!migrateToVersion6(db, errorMessage)) {
            return rollback();
        }
        version = 6;
    } else if (!ensureResumableScanSchemaCompatibility(db, errorMessage)) {
        return rollback();
    }

    if (version < 7) {
        if (!migrateToVersion7(db, errorMessage)) {
            return rollback();
        }
        version = 7;
    } else if (!ensureJobHistorySchemaCompatibility(db, errorMessage)) {
        return rollback();
    }

    if (version < 8) {
        if (!migrateToVersion8(db, errorMessage)) {
            return rollback();
        }
        version = 8;
    } else if (!ensureCatalogChangeLogSchemaCompatibility(db, errorMessage)) {
        return rollback();
    }

    if (version < 9) {
        if (!migrateToVersion9(db, errorMessage)) {
            return rollback();
        }
        version = 9;
    }

    if (version < 10) {
        if (!migrateToVersion10(db, errorMessage)) {
            return rollback();
        }
        version = 10;
    } else if (!ensureMetadataRetrySchemaCompatibility(db, errorMessage)) {
        return rollback();
    }

    if (!db.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("提交项目数据库迁移失败：%1").arg(db.lastError().text());
        }
        return rollback();
    }

    m_schemaVersion = version;
    return true;
}

bool DatabaseManager::createBaseSchema(QSqlDatabase &db, QString *errorMessage) const
{
    const QStringList statements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL);"),
        QStringLiteral("INSERT INTO schema_version(version) SELECT 1 WHERE NOT EXISTS (SELECT 1 FROM schema_version);"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS project (id TEXT PRIMARY KEY, name TEXT NOT NULL, root_path TEXT NOT NULL, created_at TEXT NOT NULL);"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS source_root ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "name TEXT NOT NULL,"
                       "path TEXT NOT NULL UNIQUE,"
                       "status TEXT NOT NULL,"
                       "total_files INTEGER NOT NULL DEFAULT 0,"
                       "total_folders INTEGER NOT NULL DEFAULT 0,"
                       "total_size_bytes INTEGER NOT NULL DEFAULT 0,"
                       "video_count INTEGER NOT NULL DEFAULT 0,"
                       "audio_count INTEGER NOT NULL DEFAULT 0,"
                       "image_count INTEGER NOT NULL DEFAULT 0,"
                       "other_count INTEGER NOT NULL DEFAULT 0,"
                       "warning_count INTEGER NOT NULL DEFAULT 0,"
                       "scan_version INTEGER NOT NULL DEFAULT 0,"
                       "created_at TEXT NOT NULL,"
                       "updated_at TEXT NOT NULL"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS folder_node ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "source_root_id INTEGER NOT NULL,"
                       "name TEXT NOT NULL DEFAULT '',"
                       "absolute_path TEXT NOT NULL,"
                       "path_key TEXT NOT NULL DEFAULT '',"
                       "relative_path TEXT NOT NULL,"
                       "parent_relative_path TEXT NOT NULL DEFAULT '',"
                       "depth INTEGER NOT NULL DEFAULT 0,"
                       "file_count INTEGER NOT NULL DEFAULT 0,"
                       "direct_file_count INTEGER NOT NULL DEFAULT 0,"
                       "recursive_file_count INTEGER NOT NULL DEFAULT 0,"
                       "normalized_date TEXT NOT NULL DEFAULT '',"
                       "date_anchor TEXT NOT NULL DEFAULT '',"
                       "created_at TEXT NOT NULL,"
                       "updated_at TEXT NOT NULL DEFAULT '',"
                       "FOREIGN KEY(source_root_id) REFERENCES source_root(id) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS asset_file ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "source_root_id INTEGER NOT NULL,"
                       "name TEXT NOT NULL,"
                       "extension TEXT,"
                       "absolute_path TEXT NOT NULL,"
                       "relative_path TEXT NOT NULL,"
                       "parent_path TEXT NOT NULL,"
                       "path_key TEXT NOT NULL DEFAULT '',"
                       "asset_type INTEGER NOT NULL,"
                       "size_bytes INTEGER NOT NULL DEFAULT 0,"
                       "modified_at TEXT NOT NULL,"
                       "is_readable INTEGER NOT NULL DEFAULT 0,"
                       "is_favorite INTEGER NOT NULL DEFAULT 0,"
                       "created_at TEXT NOT NULL,"
                       "FOREIGN KEY(source_root_id) REFERENCES source_root(id) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS job ("
                       "id INTEGER PRIMARY KEY,"
                       "type INTEGER NOT NULL,"
                       "state INTEGER NOT NULL,"
                       "title TEXT NOT NULL,"
                       "detail TEXT,"
                       "error_message TEXT,"
                       "progress INTEGER NOT NULL DEFAULT 0,"
                       "source_root_id INTEGER NOT NULL DEFAULT 0,"
                       "subject_json TEXT NOT NULL DEFAULT '{}',"
                       "progress_context_json TEXT NOT NULL DEFAULT '{}',"
                       "started_at TEXT NOT NULL,"
                       "updated_at TEXT NOT NULL"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS app_log ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "level TEXT NOT NULL,"
                       "message TEXT NOT NULL,"
                       "created_at TEXT NOT NULL"
                       ");"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_asset_file_source_root_id ON asset_file(source_root_id);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_asset_file_name ON asset_file(name);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_folder_node_source_root_id ON folder_node(source_root_id);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_source_root_status ON source_root(status);")
    };

    return executeBatch(db, statements, errorMessage);
}

bool DatabaseManager::ensureBaseSchemaCompatibility(QSqlDatabase &db, QString *errorMessage) const
{
    return ensureColumns(db,
                         QStringLiteral("project"),
                         {
                             {QStringLiteral("id"), QStringLiteral("id TEXT NOT NULL DEFAULT ''")},
                             {QStringLiteral("name"), QStringLiteral("name TEXT NOT NULL DEFAULT ''")},
                             {QStringLiteral("root_path"), QStringLiteral("root_path TEXT NOT NULL DEFAULT ''")},
                             {QStringLiteral("created_at"), QStringLiteral("created_at TEXT NOT NULL DEFAULT ''")}
                         },
                         errorMessage)
        && ensureColumns(db,
                         QStringLiteral("source_root"),
                         {
                             {QStringLiteral("id"), QStringLiteral("id INTEGER NOT NULL DEFAULT 0")},
                             {QStringLiteral("name"), QStringLiteral("name TEXT NOT NULL DEFAULT ''")},
                             {QStringLiteral("path"), QStringLiteral("path TEXT NOT NULL DEFAULT ''")},
                             {QStringLiteral("status"), QStringLiteral("status TEXT NOT NULL DEFAULT 'ok'")},
                             {QStringLiteral("total_files"), QStringLiteral("total_files INTEGER NOT NULL DEFAULT 0")},
                             {QStringLiteral("total_folders"), QStringLiteral("total_folders INTEGER NOT NULL DEFAULT 0")},
                             {QStringLiteral("total_size_bytes"), QStringLiteral("total_size_bytes INTEGER NOT NULL DEFAULT 0")},
                             {QStringLiteral("video_count"), QStringLiteral("video_count INTEGER NOT NULL DEFAULT 0")},
                             {QStringLiteral("audio_count"), QStringLiteral("audio_count INTEGER NOT NULL DEFAULT 0")},
                             {QStringLiteral("image_count"), QStringLiteral("image_count INTEGER NOT NULL DEFAULT 0")},
                             {QStringLiteral("other_count"), QStringLiteral("other_count INTEGER NOT NULL DEFAULT 0")},
                             {QStringLiteral("warning_count"), QStringLiteral("warning_count INTEGER NOT NULL DEFAULT 0")},
                             {QStringLiteral("scan_version"), QStringLiteral("scan_version INTEGER NOT NULL DEFAULT 0")},
                             {QStringLiteral("created_at"), QStringLiteral("created_at TEXT NOT NULL DEFAULT ''")},
                             {QStringLiteral("updated_at"), QStringLiteral("updated_at TEXT NOT NULL DEFAULT ''")}
                         },
                         errorMessage)
        && ensureColumns(db,
                         QStringLiteral("folder_node"),
                         {
                             {QStringLiteral("id"), QStringLiteral("id INTEGER NOT NULL DEFAULT 0")},
                             {QStringLiteral("source_root_id"), QStringLiteral("source_root_id INTEGER NOT NULL DEFAULT 0")},
                             {QStringLiteral("absolute_path"), QStringLiteral("absolute_path TEXT NOT NULL DEFAULT ''")},
                             {QStringLiteral("relative_path"), QStringLiteral("relative_path TEXT NOT NULL DEFAULT ''")},
                             {QStringLiteral("file_count"), QStringLiteral("file_count INTEGER NOT NULL DEFAULT 0")},
                             {QStringLiteral("created_at"), QStringLiteral("created_at TEXT NOT NULL DEFAULT ''")}
                         },
                         errorMessage)
        && ensureColumns(db,
                         QStringLiteral("asset_file"),
                         {
                             {QStringLiteral("id"), QStringLiteral("id INTEGER NOT NULL DEFAULT 0")},
                             {QStringLiteral("source_root_id"), QStringLiteral("source_root_id INTEGER NOT NULL DEFAULT 0")},
                             {QStringLiteral("name"), QStringLiteral("name TEXT NOT NULL DEFAULT ''")},
                             {QStringLiteral("extension"), QStringLiteral("extension TEXT")},
                             {QStringLiteral("absolute_path"), QStringLiteral("absolute_path TEXT NOT NULL DEFAULT ''")},
                             {QStringLiteral("relative_path"), QStringLiteral("relative_path TEXT NOT NULL DEFAULT ''")},
                              {QStringLiteral("parent_path"), QStringLiteral("parent_path TEXT NOT NULL DEFAULT ''")},
                              {QStringLiteral("path_key"), QStringLiteral("path_key TEXT NOT NULL DEFAULT ''")},
                              {QStringLiteral("asset_type"), QStringLiteral("asset_type INTEGER NOT NULL DEFAULT 0")},
                             {QStringLiteral("size_bytes"), QStringLiteral("size_bytes INTEGER NOT NULL DEFAULT 0")},
                             {QStringLiteral("modified_at"), QStringLiteral("modified_at TEXT NOT NULL DEFAULT ''")},
                             {QStringLiteral("is_readable"), QStringLiteral("is_readable INTEGER NOT NULL DEFAULT 0")},
                             {QStringLiteral("is_favorite"), QStringLiteral("is_favorite INTEGER NOT NULL DEFAULT 0")},
                             {QStringLiteral("created_at"), QStringLiteral("created_at TEXT NOT NULL DEFAULT ''")}
                         },
                         errorMessage)
        && ensureColumns(db,
                         QStringLiteral("job"),
                         {
                             {QStringLiteral("id"), QStringLiteral("id INTEGER NOT NULL DEFAULT 0")},
                             {QStringLiteral("type"), QStringLiteral("type INTEGER NOT NULL DEFAULT 0")},
                             {QStringLiteral("state"), QStringLiteral("state INTEGER NOT NULL DEFAULT 0")},
                             {QStringLiteral("title"), QStringLiteral("title TEXT NOT NULL DEFAULT ''")},
                             {QStringLiteral("detail"), QStringLiteral("detail TEXT")},
                             {QStringLiteral("error_message"), QStringLiteral("error_message TEXT")},
                              {QStringLiteral("progress"), QStringLiteral("progress INTEGER NOT NULL DEFAULT 0")},
                              {QStringLiteral("source_root_id"), QStringLiteral("source_root_id INTEGER NOT NULL DEFAULT 0")},
                              {QStringLiteral("subject_json"), QStringLiteral("subject_json TEXT NOT NULL DEFAULT '{}'")},
                              {QStringLiteral("progress_context_json"), QStringLiteral("progress_context_json TEXT NOT NULL DEFAULT '{}'")},
                              {QStringLiteral("started_at"), QStringLiteral("started_at TEXT NOT NULL DEFAULT ''")},
                             {QStringLiteral("updated_at"), QStringLiteral("updated_at TEXT NOT NULL DEFAULT ''")}
                         },
                         errorMessage);
}

bool DatabaseManager::migrateToVersion2(QSqlDatabase &db, QString *errorMessage) const
{
    const QStringList statements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS media_metadata ("
                       "asset_id INTEGER PRIMARY KEY,"
                       "probe_status INTEGER NOT NULL DEFAULT 0,"
                       "media_type INTEGER NOT NULL DEFAULT 0,"
                       "container TEXT,"
                       "duration_ms INTEGER NOT NULL DEFAULT 0,"
                       "bit_rate INTEGER NOT NULL DEFAULT 0,"
                       "raw_json TEXT,"
                       "error_message TEXT,"
                       "retry_count INTEGER NOT NULL DEFAULT 0,"
                       "next_retry_at TEXT NOT NULL DEFAULT '',"
                       "retry_policy_version INTEGER NOT NULL DEFAULT 0,"
                       "failure_kind TEXT NOT NULL DEFAULT '',"
                       "last_attempt_at TEXT NOT NULL DEFAULT '',"
                       "updated_at TEXT NOT NULL,"
                       "FOREIGN KEY(asset_id) REFERENCES asset_file(id) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS media_stream ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "asset_id INTEGER NOT NULL,"
                       "stream_index INTEGER NOT NULL,"
                       "stream_kind TEXT NOT NULL,"
                       "codec TEXT,"
                       "bit_rate INTEGER NOT NULL DEFAULT 0,"
                       "width INTEGER NOT NULL DEFAULT 0,"
                       "height INTEGER NOT NULL DEFAULT 0,"
                       "channels INTEGER NOT NULL DEFAULT 0,"
                       "sample_rate INTEGER NOT NULL DEFAULT 0,"
                       "FOREIGN KEY(asset_id) REFERENCES media_metadata(asset_id) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS thumbnail ("
                       "asset_id INTEGER PRIMARY KEY,"
                       "status INTEGER NOT NULL DEFAULT 0,"
                       "image_path TEXT,"
                       "updated_at TEXT NOT NULL,"
                       "error_message TEXT,"
                       "retry_count INTEGER NOT NULL DEFAULT 0,"
                       "next_retry_at TEXT NOT NULL DEFAULT '',"
                       "retry_policy_version INTEGER NOT NULL DEFAULT 0,"
                       "failure_kind TEXT NOT NULL DEFAULT '',"
                       "FOREIGN KEY(asset_id) REFERENCES asset_file(id) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_media_stream_asset_id ON media_stream(asset_id);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_media_metadata_probe_status ON media_metadata(probe_status);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_media_metadata_retry "
                       "ON media_metadata(probe_status, next_retry_at, retry_policy_version, asset_id);")
    };

    if (!executeBatch(db, statements, errorMessage)) {
        return false;
    }
    if (!ensureMediaSchemaCompatibility(db, errorMessage)) {
        return false;
    }

    return setSchemaVersion(db, 2, errorMessage);
}

bool DatabaseManager::ensureMediaSchemaCompatibility(QSqlDatabase &db, QString *errorMessage) const
{
    const QStringList statements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS media_metadata ("
                       "asset_id INTEGER PRIMARY KEY,"
                       "probe_status INTEGER NOT NULL DEFAULT 0,"
                       "media_type INTEGER NOT NULL DEFAULT 0,"
                       "container TEXT,"
                       "duration_ms INTEGER NOT NULL DEFAULT 0,"
                       "bit_rate INTEGER NOT NULL DEFAULT 0,"
                       "raw_json TEXT,"
                       "error_message TEXT,"
                       "retry_count INTEGER NOT NULL DEFAULT 0,"
                       "next_retry_at TEXT NOT NULL DEFAULT '',"
                       "retry_policy_version INTEGER NOT NULL DEFAULT 0,"
                       "failure_kind TEXT NOT NULL DEFAULT '',"
                       "last_attempt_at TEXT NOT NULL DEFAULT '',"
                       "updated_at TEXT NOT NULL,"
                       "FOREIGN KEY(asset_id) REFERENCES asset_file(id) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS media_stream ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "asset_id INTEGER NOT NULL,"
                       "stream_index INTEGER NOT NULL,"
                       "stream_kind TEXT NOT NULL,"
                       "codec TEXT,"
                       "bit_rate INTEGER NOT NULL DEFAULT 0,"
                       "width INTEGER NOT NULL DEFAULT 0,"
                       "height INTEGER NOT NULL DEFAULT 0,"
                       "channels INTEGER NOT NULL DEFAULT 0,"
                       "sample_rate INTEGER NOT NULL DEFAULT 0,"
                       "FOREIGN KEY(asset_id) REFERENCES media_metadata(asset_id) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS thumbnail ("
                       "asset_id INTEGER PRIMARY KEY,"
                       "status INTEGER NOT NULL DEFAULT 0,"
                       "image_path TEXT,"
                       "updated_at TEXT NOT NULL,"
                       "error_message TEXT,"
                       "retry_count INTEGER NOT NULL DEFAULT 0,"
                       "next_retry_at TEXT NOT NULL DEFAULT '',"
                       "retry_policy_version INTEGER NOT NULL DEFAULT 0,"
                       "failure_kind TEXT NOT NULL DEFAULT '',"
                       "FOREIGN KEY(asset_id) REFERENCES asset_file(id) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_media_stream_asset_id ON media_stream(asset_id);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_media_metadata_probe_status ON media_metadata(probe_status);")
    };

    if (!executeBatch(db, statements, errorMessage)) {
        return false;
    }

    if (!ensureColumns(db,
                       QStringLiteral("media_metadata"),
                       {
                           {QStringLiteral("asset_id"), QStringLiteral("asset_id INTEGER NOT NULL DEFAULT 0")},
                           {QStringLiteral("probe_status"), QStringLiteral("probe_status INTEGER NOT NULL DEFAULT 0")},
                           {QStringLiteral("media_type"), QStringLiteral("media_type INTEGER NOT NULL DEFAULT 0")},
                           {QStringLiteral("container"), QStringLiteral("container TEXT")},
                           {QStringLiteral("duration_ms"), QStringLiteral("duration_ms INTEGER NOT NULL DEFAULT 0")},
                           {QStringLiteral("bit_rate"), QStringLiteral("bit_rate INTEGER NOT NULL DEFAULT 0")},
                           {QStringLiteral("raw_json"), QStringLiteral("raw_json TEXT")},
                           {QStringLiteral("error_message"), QStringLiteral("error_message TEXT")},
                           {QStringLiteral("retry_count"), QStringLiteral("retry_count INTEGER NOT NULL DEFAULT 0")},
                           {QStringLiteral("next_retry_at"), QStringLiteral("next_retry_at TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("retry_policy_version"), QStringLiteral("retry_policy_version INTEGER NOT NULL DEFAULT 0")},
                           {QStringLiteral("failure_kind"), QStringLiteral("failure_kind TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("last_attempt_at"), QStringLiteral("last_attempt_at TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("updated_at"), QStringLiteral("updated_at TEXT NOT NULL DEFAULT ''")}
                       },
                       errorMessage)
        || !ensureColumns(db,
                          QStringLiteral("media_stream"),
                          {
                              {QStringLiteral("id"), QStringLiteral("id INTEGER NOT NULL DEFAULT 0")},
                              {QStringLiteral("asset_id"), QStringLiteral("asset_id INTEGER NOT NULL DEFAULT 0")},
                              {QStringLiteral("stream_index"), QStringLiteral("stream_index INTEGER NOT NULL DEFAULT 0")},
                              {QStringLiteral("stream_kind"), QStringLiteral("stream_kind TEXT NOT NULL DEFAULT ''")},
                              {QStringLiteral("codec"), QStringLiteral("codec TEXT")},
                              {QStringLiteral("bit_rate"), QStringLiteral("bit_rate INTEGER NOT NULL DEFAULT 0")},
                              {QStringLiteral("width"), QStringLiteral("width INTEGER NOT NULL DEFAULT 0")},
                              {QStringLiteral("height"), QStringLiteral("height INTEGER NOT NULL DEFAULT 0")},
                              {QStringLiteral("channels"), QStringLiteral("channels INTEGER NOT NULL DEFAULT 0")},
                              {QStringLiteral("sample_rate"), QStringLiteral("sample_rate INTEGER NOT NULL DEFAULT 0")}
                          },
                          errorMessage)
        || !ensureColumns(db,
                          QStringLiteral("thumbnail"),
                          {
                              {QStringLiteral("asset_id"), QStringLiteral("asset_id INTEGER NOT NULL DEFAULT 0")},
                              {QStringLiteral("status"), QStringLiteral("status INTEGER NOT NULL DEFAULT 0")},
                              {QStringLiteral("image_path"), QStringLiteral("image_path TEXT")},
                              {QStringLiteral("updated_at"), QStringLiteral("updated_at TEXT NOT NULL DEFAULT ''")},
                              {QStringLiteral("error_message"), QStringLiteral("error_message TEXT")},
                              {QStringLiteral("retry_count"), QStringLiteral("retry_count INTEGER NOT NULL DEFAULT 0")},
                              {QStringLiteral("next_retry_at"), QStringLiteral("next_retry_at TEXT NOT NULL DEFAULT ''")},
                              {QStringLiteral("retry_policy_version"), QStringLiteral("retry_policy_version INTEGER NOT NULL DEFAULT 0")},
                              {QStringLiteral("failure_kind"), QStringLiteral("failure_kind TEXT NOT NULL DEFAULT ''")}
                          },
                          errorMessage)) {
        return false;
    }

    return executeBatch(db,
                        {QStringLiteral("CREATE INDEX IF NOT EXISTS idx_media_metadata_retry "
                                        "ON media_metadata(probe_status, next_retry_at, retry_policy_version, asset_id);"),
                         QStringLiteral("CREATE INDEX IF NOT EXISTS idx_thumbnail_retry_schedule "
                                        "ON thumbnail(status, next_retry_at, retry_policy_version, asset_id);")},
                        errorMessage);
}

bool DatabaseManager::migrateToVersion3(QSqlDatabase &db, QString *errorMessage) const
{
    if (!ensureFolderSchemaCompatibility(db, errorMessage)
        || !backfillFolderHierarchy(db, errorMessage)) {
        return false;
    }
    return setSchemaVersion(db, 3, errorMessage);
}

bool DatabaseManager::ensureFolderSchemaCompatibility(QSqlDatabase &db, QString *errorMessage) const
{
    if (!ensureColumns(db,
                       QStringLiteral("folder_node"),
                       {
                           {QStringLiteral("name"), QStringLiteral("name TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("path_key"), QStringLiteral("path_key TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("parent_relative_path"), QStringLiteral("parent_relative_path TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("depth"), QStringLiteral("depth INTEGER NOT NULL DEFAULT 0")},
                           {QStringLiteral("direct_file_count"), QStringLiteral("direct_file_count INTEGER NOT NULL DEFAULT 0")},
                           {QStringLiteral("recursive_file_count"), QStringLiteral("recursive_file_count INTEGER NOT NULL DEFAULT 0")},
                           {QStringLiteral("normalized_date"), QStringLiteral("normalized_date TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("date_anchor"), QStringLiteral("date_anchor TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("updated_at"), QStringLiteral("updated_at TEXT NOT NULL DEFAULT ''")}
                       },
                       errorMessage)) {
        return false;
    }

    return executeBatch(db,
                        {
                            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_folder_node_path_key ON folder_node(source_root_id, path_key);"),
                            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_folder_node_parent ON folder_node(source_root_id, parent_relative_path, depth);"),
                            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_folder_node_date ON folder_node(normalized_date, date_anchor);")
                        },
                        errorMessage);
}

bool DatabaseManager::backfillFolderHierarchy(QSqlDatabase &db, QString *errorMessage) const
{
    struct SourceInfo {
        QString name;
        QString path;
    };
    struct FolderInfo {
        qint64 id = 0;
        qint64 sourceRootId = 0;
        QString rootName;
        QString absolutePath;
        QString relativePath;
        qint64 directFileCount = 0;
        qint64 recursiveFileCount = 0;
    };

    QHash<qint64, SourceInfo> sources;
    QSqlQuery sourceQuery(db);
    if (!sourceQuery.exec(QStringLiteral("SELECT id, name, path FROM source_root ORDER BY id"))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("读取项目素材源失败：%1").arg(sourceQuery.lastError().text());
        }
        return false;
    }
    while (sourceQuery.next()) {
        sources.insert(sourceQuery.value(0).toLongLong(),
                       {sourceQuery.value(1).toString(), sourceQuery.value(2).toString()});
    }

    const auto now = QDateTime::currentDateTime().toString(Qt::ISODate);
    for (auto it = sources.cbegin(); it != sources.cend(); ++it) {
        bool hasRoot = false;
        QSqlQuery roots(db);
        roots.prepare(QStringLiteral("SELECT relative_path FROM folder_node WHERE source_root_id = ?"));
        roots.addBindValue(it.key());
        if (!roots.exec()) {
            if (errorMessage) {
                *errorMessage = roots.lastError().text();
            }
            return false;
        }
        while (roots.next()) {
            if (FolderPathMetadata::normalizeRelativePath(roots.value(0).toString()).isEmpty()) {
                hasRoot = true;
                break;
            }
        }
        if (hasRoot) {
            continue;
        }

        const auto date = FolderPathMetadata::inferDate(it.value().name, QString());
        QSqlQuery insertRoot(db);
        insertRoot.prepare(QStringLiteral(
            "INSERT INTO folder_node "
            "(source_root_id, name, absolute_path, path_key, relative_path, parent_relative_path, depth, file_count, "
            "direct_file_count, recursive_file_count, normalized_date, date_anchor, created_at, updated_at) "
            "VALUES (?, ?, ?, ?, '', '', 0, 0, 0, 0, ?, ?, ?, ?)"));
        insertRoot.addBindValue(it.key());
        insertRoot.addBindValue(FolderPathMetadata::folderName(it.value().path, it.value().name));
        insertRoot.addBindValue(it.value().path);
        insertRoot.addBindValue(FolderPathMetadata::normalizedPathKey(it.value().path));
        insertRoot.addBindValue(date.normalizedDate);
        insertRoot.addBindValue(date.anchorRelativePath);
        insertRoot.addBindValue(now);
        insertRoot.addBindValue(now);
        if (!insertRoot.exec()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("补建素材源根目录失败：%1").arg(insertRoot.lastError().text());
            }
            return false;
        }
    }

    QVector<FolderInfo> folders;
    QHash<QString, int> folderIndexes;
    QSqlQuery folderQuery(db);
    if (!folderQuery.exec(QStringLiteral(
            "SELECT fn.id, fn.source_root_id, fn.absolute_path, fn.relative_path, COALESCE(sr.name, '') "
            "FROM folder_node fn LEFT JOIN source_root sr ON sr.id = fn.source_root_id ORDER BY fn.id"))) {
        if (errorMessage) {
            *errorMessage = folderQuery.lastError().text();
        }
        return false;
    }
    while (folderQuery.next()) {
        FolderInfo folder;
        folder.id = folderQuery.value(0).toLongLong();
        folder.sourceRootId = folderQuery.value(1).toLongLong();
        folder.absolutePath = folderQuery.value(2).toString();
        folder.relativePath = FolderPathMetadata::normalizeRelativePath(folderQuery.value(3).toString());
        folder.rootName = folderQuery.value(4).toString();
        const auto index = folders.size();
        folders.append(folder);
        folderIndexes.insert(QStringLiteral("%1|%2")
                                 .arg(folder.sourceRootId)
                                 .arg(folder.relativePath.toCaseFolded()),
                             index);
    }

    QSqlQuery assetQuery(db);
    if (!assetQuery.exec(QStringLiteral("SELECT source_root_id, relative_path FROM asset_file"))) {
        if (errorMessage) {
            *errorMessage = assetQuery.lastError().text();
        }
        return false;
    }
    while (assetQuery.next()) {
        const auto sourceRootId = assetQuery.value(0).toLongLong();
        const auto parentPath = FolderPathMetadata::parentRelativePath(assetQuery.value(1).toString());
        const auto directKey = QStringLiteral("%1|%2").arg(sourceRootId).arg(parentPath.toCaseFolded());
        const auto directIndex = folderIndexes.value(directKey, -1);
        if (directIndex >= 0) {
            ++folders[directIndex].directFileCount;
        }
        for (const auto &ancestor : FolderPathMetadata::ancestorRelativePaths(parentPath)) {
            const auto key = QStringLiteral("%1|%2").arg(sourceRootId).arg(ancestor.toCaseFolded());
            const auto index = folderIndexes.value(key, -1);
            if (index >= 0) {
                ++folders[index].recursiveFileCount;
            }
        }
    }

    QSqlQuery update(db);
    update.prepare(QStringLiteral(
        "UPDATE folder_node SET name = ?, path_key = ?, relative_path = ?, parent_relative_path = ?, depth = ?, "
        "file_count = ?, direct_file_count = ?, recursive_file_count = ?, normalized_date = ?, date_anchor = ?, "
        "updated_at = ? WHERE id = ?"));
    for (const auto &folder : folders) {
        const auto fallbackName = sources.value(folder.sourceRootId).name;
        const auto date = FolderPathMetadata::inferDate(folder.rootName, folder.relativePath);
        update.addBindValue(FolderPathMetadata::folderName(folder.absolutePath, fallbackName));
        update.addBindValue(FolderPathMetadata::normalizedPathKey(folder.absolutePath));
        update.addBindValue(folder.relativePath);
        update.addBindValue(FolderPathMetadata::parentRelativePath(folder.relativePath));
        update.addBindValue(FolderPathMetadata::depth(folder.relativePath));
        update.addBindValue(folder.directFileCount);
        update.addBindValue(folder.directFileCount);
        update.addBindValue(folder.recursiveFileCount);
        update.addBindValue(date.normalizedDate);
        update.addBindValue(date.anchorRelativePath);
        update.addBindValue(now);
        update.addBindValue(folder.id);
        if (!update.exec()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("回填项目目录层级失败：%1").arg(update.lastError().text());
            }
            return false;
        }
        update.finish();
    }
    return true;
}

bool DatabaseManager::migrateToVersion4(QSqlDatabase &db, QString *errorMessage) const
{
    if (!ensureAtomicScanSchemaCompatibility(db, errorMessage)) {
        return false;
    }
    return setSchemaVersion(db, 4, errorMessage);
}

bool DatabaseManager::ensureAtomicScanSchemaCompatibility(QSqlDatabase &db, QString *errorMessage) const
{
    if (!ensureColumn(db,
                      QStringLiteral("asset_file"),
                      QStringLiteral("path_key"),
                      QStringLiteral("path_key TEXT NOT NULL DEFAULT ''"),
                      errorMessage)) {
        return false;
    }
    if (!backfillAssetPathKeys(db, errorMessage)) {
        return false;
    }

    const QStringList statements = {
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_asset_file_source_path_key ON asset_file(source_root_id, path_key);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_folder_node_source_path_key ON folder_node(source_root_id, path_key);")
    };
    return executeBatch(db, statements, errorMessage);
}

bool DatabaseManager::backfillAssetPathKeys(QSqlDatabase &db, QString *errorMessage) const
{
    QSqlQuery read(db);
    if (!read.exec(QStringLiteral(
            "SELECT id, absolute_path FROM asset_file WHERE COALESCE(path_key, '') = ''"))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("读取待回填素材路径键失败：%1").arg(read.lastError().text());
        }
        return false;
    }

    QVector<QPair<qint64, QString>> rows;
    while (read.next()) {
        rows.append({read.value(0).toLongLong(), read.value(1).toString()});
    }
    read.finish();

    QSqlQuery update(db);
    update.prepare(QStringLiteral("UPDATE asset_file SET path_key = ? WHERE id = ?"));
    for (const auto &row : rows) {
        update.addBindValue(FolderPathMetadata::normalizedPathKey(row.second));
        update.addBindValue(row.first);
        if (!update.exec()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("回填素材路径键失败：%1").arg(update.lastError().text());
            }
            return false;
        }
        update.finish();
    }
    return true;
}

bool DatabaseManager::migrateToVersion5(QSqlDatabase &db, QString *errorMessage) const
{
    if (!ensureEmbeddedMetadataSchemaCompatibility(db, errorMessage)) {
        return false;
    }
    return setSchemaVersion(db, 5, errorMessage);
}

bool DatabaseManager::ensureEmbeddedMetadataSchemaCompatibility(QSqlDatabase &db,
                                                                 QString *errorMessage) const
{
    const QStringList statements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS embedded_metadata ("
                       "asset_id INTEGER PRIMARY KEY,"
                       "status INTEGER NOT NULL DEFAULT 0,"
                       "tool_version TEXT NOT NULL DEFAULT '',"
                       "fingerprint_size INTEGER NOT NULL DEFAULT 0,"
                       "fingerprint_modified TEXT NOT NULL DEFAULT '',"
                       "capture_time TEXT NOT NULL DEFAULT '',"
                       "create_time TEXT NOT NULL DEFAULT '',"
                       "camera_make TEXT NOT NULL DEFAULT '',"
                       "camera_model TEXT NOT NULL DEFAULT '',"
                       "lens_model TEXT NOT NULL DEFAULT '',"
                       "camera_serial_hash TEXT NOT NULL DEFAULT '',"
                       "gps_latitude REAL,"
                       "gps_longitude REAL,"
                       "gps_altitude REAL,"
                       "orientation INTEGER NOT NULL DEFAULT 0,"
                       "width INTEGER NOT NULL DEFAULT 0,"
                       "height INTEGER NOT NULL DEFAULT 0,"
                       "duration_ms INTEGER NOT NULL DEFAULT 0,"
                       "frame_rate REAL NOT NULL DEFAULT 0,"
                       "video_codec TEXT NOT NULL DEFAULT '',"
                       "color_space TEXT NOT NULL DEFAULT '',"
                       "sample_rate INTEGER NOT NULL DEFAULT 0,"
                       "channels INTEGER NOT NULL DEFAULT 0,"
                       "bit_rate INTEGER NOT NULL DEFAULT 0,"
                       "timecode TEXT NOT NULL DEFAULT '',"
                       "title TEXT NOT NULL DEFAULT '',"
                       "description TEXT NOT NULL DEFAULT '',"
                       "artist TEXT NOT NULL DEFAULT '',"
                       "album TEXT NOT NULL DEFAULT '',"
                       "genre TEXT NOT NULL DEFAULT '',"
                       "keywords TEXT NOT NULL DEFAULT '',"
                       "search_text TEXT NOT NULL DEFAULT '',"
                       "raw_json TEXT NOT NULL DEFAULT '',"
                       "error_message TEXT NOT NULL DEFAULT '',"
                       "retry_count INTEGER NOT NULL DEFAULT 0,"
                       "next_retry_at TEXT NOT NULL DEFAULT '',"
                       "retry_policy_version INTEGER NOT NULL DEFAULT 0,"
                       "failure_kind TEXT NOT NULL DEFAULT '',"
                       "last_attempt_at TEXT NOT NULL DEFAULT '',"
                       "updated_at TEXT NOT NULL DEFAULT '',"
                       "FOREIGN KEY(asset_id) REFERENCES asset_file(id) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_embedded_metadata_status ON embedded_metadata(status);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_embedded_metadata_capture ON embedded_metadata(capture_time);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_embedded_metadata_camera ON embedded_metadata(camera_make, camera_model);")
    };
    if (!executeBatch(db, statements, errorMessage)
        || !ensureColumns(db,
                          QStringLiteral("embedded_metadata"),
                          {
                              {QStringLiteral("retry_count"), QStringLiteral("retry_count INTEGER NOT NULL DEFAULT 0")},
                              {QStringLiteral("next_retry_at"), QStringLiteral("next_retry_at TEXT NOT NULL DEFAULT ''")},
                              {QStringLiteral("retry_policy_version"), QStringLiteral("retry_policy_version INTEGER NOT NULL DEFAULT 0")},
                              {QStringLiteral("failure_kind"), QStringLiteral("failure_kind TEXT NOT NULL DEFAULT ''")},
                              {QStringLiteral("last_attempt_at"), QStringLiteral("last_attempt_at TEXT NOT NULL DEFAULT ''")}
                          },
                          errorMessage)) {
        return false;
    }
    return executeBatch(db,
                        {QStringLiteral("CREATE INDEX IF NOT EXISTS idx_embedded_metadata_retry "
                                        "ON embedded_metadata(status, next_retry_at, retry_policy_version, asset_id);")},
                        errorMessage);
}

bool DatabaseManager::migrateToVersion6(QSqlDatabase &db, QString *errorMessage) const
{
    if (!ensureResumableScanSchemaCompatibility(db, errorMessage)) {
        return false;
    }
    return setSchemaVersion(db, 6, errorMessage);
}

bool DatabaseManager::ensureResumableScanSchemaCompatibility(QSqlDatabase &db,
                                                              QString *errorMessage) const
{
    return executeBatch(db,
                        {
                            QStringLiteral("CREATE TABLE IF NOT EXISTS scan_session ("
                                           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                           "source_root_id INTEGER NOT NULL UNIQUE,"
                                           "state TEXT NOT NULL,"
                                           "last_error TEXT NOT NULL DEFAULT '',"
                                           "created_at TEXT NOT NULL,"
                                           "updated_at TEXT NOT NULL,"
                                           "FOREIGN KEY(source_root_id) REFERENCES source_root(id) ON DELETE CASCADE"
                                           ");"),
                            QStringLiteral("CREATE TABLE IF NOT EXISTS scan_work_item ("
                                           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                           "session_id INTEGER NOT NULL,"
                                           "absolute_path TEXT NOT NULL,"
                                           "relative_path TEXT NOT NULL,"
                                           "path_key TEXT NOT NULL,"
                                           "depth INTEGER NOT NULL DEFAULT 0,"
                                           "state TEXT NOT NULL,"
                                           "created_at TEXT NOT NULL,"
                                           "updated_at TEXT NOT NULL,"
                                           "UNIQUE(session_id, path_key),"
                                           "FOREIGN KEY(session_id) REFERENCES scan_session(id) ON DELETE CASCADE"
                                           ");"),
                            QStringLiteral("CREATE TABLE IF NOT EXISTS scan_stage_asset ("
                                           "session_id INTEGER NOT NULL,"
                                           "path_key TEXT NOT NULL,"
                                           "name TEXT NOT NULL,"
                                           "extension TEXT,"
                                           "absolute_path TEXT NOT NULL,"
                                           "relative_path TEXT NOT NULL,"
                                           "parent_path TEXT NOT NULL,"
                                           "parent_relative_path TEXT NOT NULL,"
                                           "asset_type INTEGER NOT NULL,"
                                           "size_bytes INTEGER NOT NULL,"
                                           "modified_at TEXT NOT NULL,"
                                           "is_readable INTEGER NOT NULL,"
                                           "created_at TEXT NOT NULL,"
                                           "PRIMARY KEY(session_id, path_key),"
                                           "FOREIGN KEY(session_id) REFERENCES scan_session(id) ON DELETE CASCADE"
                                           ");"),
                            QStringLiteral("CREATE TABLE IF NOT EXISTS scan_stage_folder ("
                                           "session_id INTEGER NOT NULL,"
                                           "path_key TEXT NOT NULL,"
                                           "name TEXT NOT NULL,"
                                           "absolute_path TEXT NOT NULL,"
                                           "relative_path TEXT NOT NULL,"
                                           "parent_relative_path TEXT NOT NULL,"
                                           "depth INTEGER NOT NULL,"
                                           "file_count INTEGER NOT NULL DEFAULT 0,"
                                           "direct_file_count INTEGER NOT NULL DEFAULT 0,"
                                           "recursive_file_count INTEGER NOT NULL DEFAULT 0,"
                                           "normalized_date TEXT NOT NULL DEFAULT '',"
                                           "date_anchor TEXT NOT NULL DEFAULT '',"
                                           "created_at TEXT NOT NULL,"
                                           "updated_at TEXT NOT NULL,"
                                           "PRIMARY KEY(session_id, path_key),"
                                           "FOREIGN KEY(session_id) REFERENCES scan_session(id) ON DELETE CASCADE"
                                           ");"),
                            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_scan_work_session_state ON scan_work_item(session_id, state, depth, id);"),
                            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_scan_stage_asset_parent ON scan_stage_asset(session_id, parent_relative_path);"),
                            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_scan_stage_folder_parent ON scan_stage_folder(session_id, parent_relative_path, depth);")
                        },
                        errorMessage);
}

bool DatabaseManager::migrateToVersion7(QSqlDatabase &db, QString *errorMessage) const
{
    if (!ensureJobHistorySchemaCompatibility(db, errorMessage)) {
        return false;
    }
    return setSchemaVersion(db, 7, errorMessage);
}

bool DatabaseManager::ensureJobHistorySchemaCompatibility(QSqlDatabase &db,
                                                            QString *errorMessage) const
{
    return ensureColumns(db,
                         QStringLiteral("job"),
                         {
                             {QStringLiteral("subject_json"),
                              QStringLiteral("subject_json TEXT NOT NULL DEFAULT '{}'")},
                             {QStringLiteral("progress_context_json"),
                              QStringLiteral("progress_context_json TEXT NOT NULL DEFAULT '{}'")}
                         },
                         errorMessage)
        && executeBatch(db,
                        {QStringLiteral(
                            "CREATE INDEX IF NOT EXISTS idx_job_updated_at ON job(updated_at DESC, id DESC)")},
                        errorMessage);
}

bool DatabaseManager::migrateToVersion8(QSqlDatabase &db, QString *errorMessage) const
{
    if (!ensureCatalogChangeLogSchemaCompatibility(db, errorMessage)) {
        return false;
    }
    return setSchemaVersion(db, 8, errorMessage);
}

bool DatabaseManager::ensureCatalogChangeLogSchemaCompatibility(QSqlDatabase &db,
                                                                 QString *errorMessage) const
{
    const QStringList schemaStatements = {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS catalog_change_state ("
            "singleton_id INTEGER PRIMARY KEY CHECK(singleton_id = 1),"
            "next_log_id INTEGER NOT NULL DEFAULT 0,"
            "requires_full_rebuild INTEGER NOT NULL DEFAULT 1,"
            "pending_change_count INTEGER NOT NULL DEFAULT 0)"),
        QStringLiteral(
            "INSERT OR IGNORE INTO catalog_change_state "
            "(singleton_id, next_log_id, requires_full_rebuild, pending_change_count) VALUES (1, 0, 1, 0)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS catalog_change_log ("
            "log_id INTEGER PRIMARY KEY,"
            "entity_type INTEGER NOT NULL,"
            "entity_id INTEGER NOT NULL,"
            "entity_key TEXT NOT NULL DEFAULT '',"
            "previous_entity_key TEXT NOT NULL DEFAULT '',"
            "source_root_id INTEGER NOT NULL DEFAULT 0,"
            "previous_source_root_id INTEGER NOT NULL DEFAULT 0,"
            "operation INTEGER NOT NULL,"
            "change_mask INTEGER NOT NULL DEFAULT 0,"
            "updated_at TEXT NOT NULL DEFAULT '',"
            "UNIQUE(entity_type, entity_id))"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_catalog_change_log_watermark "
            "ON catalog_change_log(log_id)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_catalog_change_log_entity "
            "ON catalog_change_log(entity_type, entity_id)"),
        QStringLiteral("DROP TRIGGER IF EXISTS catalog_change_count_insert"),
        QStringLiteral("DROP TRIGGER IF EXISTS catalog_change_count_delete"),
        QStringLiteral(
            "CREATE TRIGGER catalog_change_count_insert AFTER INSERT ON catalog_change_log BEGIN "
            "UPDATE catalog_change_state SET "
            "pending_change_count = pending_change_count + 1, "
            "requires_full_rebuild = CASE WHEN pending_change_count + 1 > 100000 "
            "THEN 1 ELSE requires_full_rebuild END WHERE singleton_id = 1; END"),
        QStringLiteral(
            "CREATE TRIGGER catalog_change_count_delete AFTER DELETE ON catalog_change_log BEGIN "
            "UPDATE catalog_change_state SET pending_change_count = MAX(0, pending_change_count - 1) "
            "WHERE singleton_id = 1; END")
    };
    if (!executeBatch(db, schemaStatements, errorMessage)) {
        return false;
    }

    const QStringList triggerNames = {
        QStringLiteral("catalog_change_asset_insert"),
        QStringLiteral("catalog_change_asset_update"),
        QStringLiteral("catalog_change_asset_delete"),
        QStringLiteral("catalog_change_folder_insert"),
        QStringLiteral("catalog_change_folder_update"),
        QStringLiteral("catalog_change_folder_delete"),
        QStringLiteral("catalog_change_media_insert"),
        QStringLiteral("catalog_change_media_update"),
        QStringLiteral("catalog_change_media_delete"),
        QStringLiteral("catalog_change_embedded_insert"),
        QStringLiteral("catalog_change_embedded_update"),
        QStringLiteral("catalog_change_embedded_delete"),
        QStringLiteral("catalog_change_thumbnail_insert"),
        QStringLiteral("catalog_change_thumbnail_update"),
        QStringLiteral("catalog_change_thumbnail_delete")
    };
    QStringList triggerStatements;
    for (const auto &triggerName : triggerNames) {
        triggerStatements.append(QStringLiteral("DROP TRIGGER IF EXISTS %1").arg(triggerName));
    }

    constexpr int Added = 1;
    constexpr int Updated = 2;
    constexpr int Removed = 3;
    constexpr int AssetCore = 1;
    constexpr int AssetMetadata = 2;
    constexpr int Thumbnail = 4;
    constexpr int Folder = 8;
    triggerStatements.append({
        createCatalogChangeTrigger(QStringLiteral("catalog_change_asset_insert"),
                                   QStringLiteral("asset_file"), QStringLiteral("INSERT"),
                                   QStringLiteral("1"), QStringLiteral("NEW.id"),
                                   QStringLiteral("NEW.absolute_path"), QStringLiteral("''"),
                                   QStringLiteral("NEW.source_root_id"), QStringLiteral("0"),
                                   Added, AssetCore),
        createCatalogChangeTrigger(QStringLiteral("catalog_change_asset_update"),
                                   QStringLiteral("asset_file"), QStringLiteral("UPDATE"),
                                   QStringLiteral("1"), QStringLiteral("NEW.id"),
                                   QStringLiteral("NEW.absolute_path"), QStringLiteral("OLD.absolute_path"),
                                   QStringLiteral("NEW.source_root_id"), QStringLiteral("OLD.source_root_id"),
                                   Updated, AssetCore),
        createCatalogChangeTrigger(QStringLiteral("catalog_change_asset_delete"),
                                   QStringLiteral("asset_file"), QStringLiteral("DELETE"),
                                   QStringLiteral("1"), QStringLiteral("OLD.id"),
                                   QStringLiteral("OLD.absolute_path"), QStringLiteral("OLD.absolute_path"),
                                   QStringLiteral("OLD.source_root_id"), QStringLiteral("OLD.source_root_id"),
                                   Removed, AssetCore),
        createCatalogChangeTrigger(QStringLiteral("catalog_change_folder_insert"),
                                   QStringLiteral("folder_node"), QStringLiteral("INSERT"),
                                   QStringLiteral("2"), QStringLiteral("NEW.id"),
                                   QStringLiteral("NEW.relative_path"), QStringLiteral("''"),
                                   QStringLiteral("NEW.source_root_id"), QStringLiteral("0"),
                                   Added, Folder),
        createCatalogChangeTrigger(QStringLiteral("catalog_change_folder_update"),
                                   QStringLiteral("folder_node"), QStringLiteral("UPDATE"),
                                   QStringLiteral("2"), QStringLiteral("NEW.id"),
                                   QStringLiteral("NEW.relative_path"), QStringLiteral("OLD.relative_path"),
                                   QStringLiteral("NEW.source_root_id"), QStringLiteral("OLD.source_root_id"),
                                   Updated, Folder),
        createCatalogChangeTrigger(QStringLiteral("catalog_change_folder_delete"),
                                   QStringLiteral("folder_node"), QStringLiteral("DELETE"),
                                   QStringLiteral("2"), QStringLiteral("OLD.id"),
                                   QStringLiteral("OLD.relative_path"), QStringLiteral("OLD.relative_path"),
                                   QStringLiteral("OLD.source_root_id"), QStringLiteral("OLD.source_root_id"),
                                   Removed, Folder)
    });

    const auto appendAssetChildTriggers = [&triggerStatements](const QString &prefix,
                                                                const QString &tableName,
                                                                int changeMask) {
        const auto keyForNew = QStringLiteral(
            "COALESCE((SELECT absolute_path FROM asset_file WHERE id = NEW.asset_id), '')");
        const auto keyForOld = QStringLiteral(
            "COALESCE((SELECT absolute_path FROM asset_file WHERE id = OLD.asset_id), '')");
        const auto sourceForNew = QStringLiteral(
            "COALESCE((SELECT source_root_id FROM asset_file WHERE id = NEW.asset_id), 0)");
        const auto sourceForOld = QStringLiteral(
            "COALESCE((SELECT source_root_id FROM asset_file WHERE id = OLD.asset_id), 0)");
        const auto existsForNew = QStringLiteral(
            "EXISTS(SELECT 1 FROM asset_file WHERE id = NEW.asset_id)");
        const auto existsForOld = QStringLiteral(
            "EXISTS(SELECT 1 FROM asset_file WHERE id = OLD.asset_id)");
        triggerStatements.append(createCatalogChangeTrigger(
            QStringLiteral("catalog_change_%1_insert").arg(prefix), tableName, QStringLiteral("INSERT"),
            QStringLiteral("1"), QStringLiteral("NEW.asset_id"), keyForNew, keyForNew,
            sourceForNew, sourceForNew, 2, changeMask, existsForNew));
        triggerStatements.append(createCatalogChangeTrigger(
            QStringLiteral("catalog_change_%1_update").arg(prefix), tableName, QStringLiteral("UPDATE"),
            QStringLiteral("1"), QStringLiteral("NEW.asset_id"), keyForNew, keyForNew,
            sourceForNew, sourceForNew, 2, changeMask, existsForNew));
        triggerStatements.append(createCatalogChangeTrigger(
            QStringLiteral("catalog_change_%1_delete").arg(prefix), tableName, QStringLiteral("DELETE"),
            QStringLiteral("1"), QStringLiteral("OLD.asset_id"), keyForOld, keyForOld,
            sourceForOld, sourceForOld, 2, changeMask, existsForOld));
    };
    appendAssetChildTriggers(QStringLiteral("media"), QStringLiteral("media_metadata"), AssetMetadata);
    appendAssetChildTriggers(QStringLiteral("embedded"), QStringLiteral("embedded_metadata"), AssetMetadata);
    appendAssetChildTriggers(QStringLiteral("thumbnail"), QStringLiteral("thumbnail"), Thumbnail);
    triggerStatements.append(QStringLiteral(
        "UPDATE catalog_change_state SET pending_change_count = (SELECT COUNT(*) FROM catalog_change_log), "
        "next_log_id = MAX(next_log_id, COALESCE((SELECT MAX(log_id) FROM catalog_change_log), 0)) "
        "WHERE singleton_id = 1"));
    return executeBatch(db, triggerStatements, errorMessage);
}

bool DatabaseManager::migrateToVersion9(QSqlDatabase &db, QString *errorMessage) const
{
    if (!ensureMediaSchemaCompatibility(db, errorMessage)
        || !executeBatch(db,
                         {QStringLiteral(
                             "CREATE INDEX IF NOT EXISTS idx_thumbnail_retry_schedule "
                             "ON thumbnail(status, next_retry_at, retry_policy_version, asset_id)")},
                         errorMessage)) {
        return false;
    }
    return setSchemaVersion(db, 9, errorMessage);
}

bool DatabaseManager::ensureMetadataRetrySchemaCompatibility(QSqlDatabase &db,
                                                             QString *errorMessage) const
{
    return ensureMediaSchemaCompatibility(db, errorMessage)
        && ensureEmbeddedMetadataSchemaCompatibility(db, errorMessage);
}

bool DatabaseManager::migrateToVersion10(QSqlDatabase &db, QString *errorMessage) const
{
    if (!ensureMetadataRetrySchemaCompatibility(db, errorMessage)) {
        return false;
    }
    return setSchemaVersion(db, 10, errorMessage);
}

int DatabaseManager::currentSchemaVersion(QSqlDatabase &db) const
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT version FROM schema_version LIMIT 1"))) {
        return 0;
    }
    return query.next() ? query.value(0).toInt() : 0;
}

bool DatabaseManager::setSchemaVersion(QSqlDatabase &db, int version, QString *errorMessage) const
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM schema_version"));
    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    query.prepare(QStringLiteral("INSERT INTO schema_version(version) VALUES (?)"));
    query.addBindValue(version);
    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    return true;
}
