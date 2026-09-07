#ifndef KALBURATOR_BLOB_RECORDLOADRESULT_H
#define KALBURATOR_BLOB_RECORDLOADRESULT_H

#include <QList>
#include <QString>
#include <utility>

#include <kalburator/types/backendrecord.h>

namespace Kalburator::Sync {

/// The result of reading one complete collection.
///
/// `records.isEmpty()` is valid only when `ok` is true.  In particular, a
/// failed remote read must never be represented by an empty record list.
struct RecordLoadResult {
    QList<BackendRecord> records;
    QString errorMessage;

    bool ok() const noexcept { return errorMessage.isEmpty(); }

    static RecordLoadResult success(QList<BackendRecord> value)
    {
        return {std::move(value), {}};
    }

    static RecordLoadResult failure(QString message)
    {
        return {{}, std::move(message)};
    }
};

} // namespace Kalburator::Sync

#endif
