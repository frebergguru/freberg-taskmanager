// A small SQLite file for finished disk scans and benchmark results. It opens on
// first use, and pages close it when left, so it costs nothing otherwise.
#pragma once

#include <QtSql/QSqlQuery>

namespace fg {

QSqlQuery resultsQuery();
void closeResultsDb();

} // namespace fg
