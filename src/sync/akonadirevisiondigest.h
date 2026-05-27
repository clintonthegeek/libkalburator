#ifndef KALBURATOR_SYNC_AKONADIREVISIONDIGEST_H
#define KALBURATOR_SYNC_AKONADIREVISIONDIGEST_H
#include <QList>
#include <QPair>
#include <QString>
namespace Kalburator::Sync {
/// Stable token over (Akonadi item id, revision) pairs. Order-independent.
/// Empty input -> empty string (engine treats as "changed").
QString computeRevisionDigest(QList<QPair<qint64, int>> idRev);
}
#endif
