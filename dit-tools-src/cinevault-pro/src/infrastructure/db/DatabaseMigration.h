#pragma once

#include <QString>

class QSqlDatabase;

namespace DatabaseMigration {

bool configureSqlite(QSqlDatabase &db, QString *errorMessage);
QString backupFilePath(const QString &databaseFilePath, int targetVersion);
bool createUpgradeBackup(QSqlDatabase &db,
                         int targetVersion,
                         bool databaseExistedBeforeOpen,
                         QString *errorMessage);
QString legacyMigrationMarkerPath(const QString &destinationFilePath);
bool ensureUserDatabase(const QString &destinationFilePath,
                        const QString &legacyDataRoot,
                        QString *recoveryMessage,
                        QString *errorMessage);

}
