#include "recordfilter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace Kalburator::Shape {

bool RecordFilter::matches(const QByteArray& canonRecordBytes) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(canonRecordBytes);
    if (doc.isNull() || !doc.isObject())
        return false;
    return matches(doc);
}

bool RecordFilter::matches(const QJsonDocument& canonRecord) const
{
    if (!canonRecord.isObject())
        return false;
    const QJsonObject obj = canonRecord.object();
    const QString key = property.toString();
    if (key.isEmpty() || !obj.contains(key))
        return false;

    const QJsonValue filterValue = QJsonValue::fromVariant(value);
    const QJsonValue field = obj.value(key);

    switch (op) {
    case Op::Contains: {
        if (!field.isArray())
            return false;
        const QJsonArray arr = field.toArray();
        for (const QJsonValue& v : arr) {
            // QJsonValue::operator== is semantic (key-order-independent for
            // objects, element-wise for arrays). Case-sensitive for strings.
            if (v == filterValue)
                return true;
        }
        return false;
    }
    case Op::Equals:
        return field == filterValue;
    }
    Q_UNREACHABLE_RETURN(false);
}

}  // namespace Kalburator::Shape
