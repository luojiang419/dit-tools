#include "infrastructure/db/DatabaseManager.h"

#include <QFileInfo>
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
}

DatabaseManager::DatabaseManager(QObject *parent)
    : QObject(parent)
    , m_mainConnectionName(QStringLiteral("cinevault_main"))
{
}

bool DatabaseManager::openProjectDatabase(const QString &databaseFilePath, QString *errorMessage)
{
    closeProjectDatabase();

    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_mainConnectionName);
    db.setDatabaseName(databaseFilePath);
    if (!db.open()) {
        if (errorMessage) {
            *errorMessage = db.lastError().text();
        }
        return false;
    }

    if (!initializeSchema(db, errorMessage)) {
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
    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(m_databaseFilePath);
    if (!db.open()) {
        if (errorMessage) {
            *errorMessage = db.lastError().text();
        }
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

bool DatabaseManager::initializeSchema(QSqlDatabase &db, QString *errorMessage)
{
    if (!createBaseSchema(db, errorMessage)) {
        return false;
    }
    if (!ensureBaseSchemaCompatibility(db, errorMessage)) {
        return false;
    }

    auto version = currentSchemaVersion(db);
    if (version < 1) {
        version = 1;
        if (!setSchemaVersion(db, version, errorMessage)) {
            return false;
        }
    }

    if (version < 2) {
        if (!migrateToVersion2(db, errorMessage)) {
            return false;
        }
        version = 2;
    } else if (!ensureMediaSchemaCompatibility(db, errorMessage)) {
        return false;
    }

    m_schemaVersion = version;
    return true;
}

bool DatabaseManager::createBaseSchema(QSqlDatabase &db, QString *errorMessage) const
{
    const QStringList statements = {
        QStringLiteral("PRAGMA journal_mode=WAL;"),
        QStringLiteral("PRAGMA synchronous=NORMAL;"),
        QStringLiteral("PRAGMA foreign_keys=ON;"),
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
                       "absolute_path TEXT NOT NULL,"
                       "relative_path TEXT NOT NULL,"
                       "file_count INTEGER NOT NULL DEFAULT 0,"
                       "created_at TEXT NOT NULL,"
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
                       "FOREIGN KEY(asset_id) REFERENCES asset_file(id) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_media_stream_asset_id ON media_stream(asset_id);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_media_metadata_probe_status ON media_metadata(probe_status);")
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
                       "FOREIGN KEY(asset_id) REFERENCES asset_file(id) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_media_stream_asset_id ON media_stream(asset_id);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_media_metadata_probe_status ON media_metadata(probe_status);")
    };

    if (!executeBatch(db, statements, errorMessage)) {
        return false;
    }

    return ensureColumns(db,
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
                             {QStringLiteral("updated_at"), QStringLiteral("updated_at TEXT NOT NULL DEFAULT ''")}
                         },
                         errorMessage)
        && ensureColumns(db,
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
        && ensureColumns(db,
                         QStringLiteral("thumbnail"),
                         {
                             {QStringLiteral("asset_id"), QStringLiteral("asset_id INTEGER NOT NULL DEFAULT 0")},
                             {QStringLiteral("status"), QStringLiteral("status INTEGER NOT NULL DEFAULT 0")},
                             {QStringLiteral("image_path"), QStringLiteral("image_path TEXT")},
                             {QStringLiteral("updated_at"), QStringLiteral("updated_at TEXT NOT NULL DEFAULT ''")},
                             {QStringLiteral("error_message"), QStringLiteral("error_message TEXT")}
                         },
                         errorMessage);
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
