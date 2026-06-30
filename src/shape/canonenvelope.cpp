#include "canonenvelope.h"

#include <QJsonDocument>

namespace Kalburator::Shape::CanonEnvelope {

QString canonKey()          { return QStringLiteral("_canon"); }
QString uidKey()            { return QStringLiteral("uid"); }
QString providerExtrasKey() { return QStringLiteral("providerExtras"); }
QString kindKey()           { return QStringLiteral("kind"); }

QJsonObject parse(const QByteArray& bytes)
{
    return QJsonDocument::fromJson(bytes).object();
}

QByteArray serialize(const QJsonObject& obj)
{
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

void stampEnvelope(QJsonObject& obj, const QString& domain, const QString& uid,
                   const QString& kind)
{
    QJsonObject canon;
    canon.insert(QStringLiteral("domain"), domain);
    canon.insert(QStringLiteral("v"), kCanonVersion);
    if (!kind.isEmpty())
        canon.insert(kindKey(), kind);
    obj.insert(canonKey(), canon);
    obj.insert(uidKey(), uid);
}

QString uid(const QJsonObject& obj)
{
    return obj.value(uidKey()).toString();
}

QString kind(const QJsonObject& obj)
{
    return obj.value(canonKey()).toObject().value(kindKey()).toString();
}

bool valuesEqual(const QJsonValue& a, const QJsonValue& b)
{
    return a == b;  // QJsonValue::operator== is recursive, key-order-independent
}

}  // namespace Kalburator::Shape::CanonEnvelope
