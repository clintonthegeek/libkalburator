#include "akonadirevisiondigest.h"
#include <QCryptographicHash>
#include <algorithm>
namespace Kalburator::Sync {
QString computeRevisionDigest(QList<QPair<qint64, int>> idRev)
{
    if (idRev.isEmpty()) return {};
    std::sort(idRev.begin(), idRev.end());
    QCryptographicHash h(QCryptographicHash::Sha256);
    for (const auto &p : idRev) {
        h.addData(QByteArray::number(p.first));
        h.addData(QByteArray(":"));
        h.addData(QByteArray::number(p.second));
        h.addData(QByteArray(";"));
    }
    return QString::fromLatin1(h.result().toHex());
}
}
