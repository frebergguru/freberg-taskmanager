#include "resultsdb.h"

#include <QDir>
#include <QStandardPaths>
#include <QtSql/QSqlDatabase>

namespace fg {

namespace {
const QString kConnection = QStringLiteral("results");
}

QSqlQuery resultsQuery()
{
    QSqlDatabase db = QSqlDatabase::database(kConnection);
    if (!db.isValid()) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dir);
        db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kConnection);
        db.setDatabaseName(dir + QStringLiteral("/results.sqlite"));
        db.open();
        QSqlQuery create(db);
        create.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS scans (finished INTEGER, path TEXT, bytes INTEGER, files INTEGER, seconds REAL)"));
        create.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS benchmarks (finished INTEGER, cpuSingle REAL, cpuMulti REAL, memoryMBs REAL, latencyNs REAL, diskReadMBs REAL, diskWriteMBs REAL)"));
    }
    return QSqlQuery(db);
}

void closeResultsDb()
{
    if (QSqlDatabase::contains(kConnection)) {
        QSqlDatabase::database(kConnection, false).close();
        QSqlDatabase::removeDatabase(kConnection);
    }
}

} // namespace fg
