#include "synctransactionitem.h"
#include <QDebug>

SyncTransactionItem::SyncTransactionItem(const QString &calendarId,
                                          const QString &uid,
                                          ItemType type,
                                          QObject *parent)
    : QObject(parent)
    , m_calendarId(calendarId)
    , m_uid(uid)
    , m_type(type)
{
}

SyncTransactionItem::~SyncTransactionItem()
{
}

QJsonObject SyncTransactionItem::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("type")] = itemTypeToString(m_type);
    obj[QStringLiteral("calendarId")] = m_calendarId;
    obj[QStringLiteral("uid")] = m_uid;
    obj[QStringLiteral("description")] = description();
    obj[QStringLiteral("committed")] = m_committed;
    return obj;
}

void SyncTransactionItem::setCommitted(bool committed)
{
    m_committed = committed;
}

void SyncTransactionItem::setErrorString(const QString &error)
{
    m_errorString = error;
    if (!error.isEmpty()) {
        emit errorOccurred(error);
    }
}

void SyncTransactionItem::setSimulationResult(SimulationResult result)
{
    m_simulationResult = result;
}

QString SyncTransactionItem::itemTypeToString(ItemType type)
{
    switch (type) {
    case ItemType::Create:
        return QStringLiteral("create");
    case ItemType::Update:
        return QStringLiteral("update");
    case ItemType::Delete:
        return QStringLiteral("delete");
    }
    return QStringLiteral("unknown");
}

SyncTransactionItem::ItemType SyncTransactionItem::stringToItemType(const QString &str)
{
    if (str == QStringLiteral("create")) {
        return ItemType::Create;
    } else if (str == QStringLiteral("update")) {
        return ItemType::Update;
    } else if (str == QStringLiteral("delete")) {
        return ItemType::Delete;
    }
    // Default to Create if unknown
    qWarning() << "SyncTransactionItem: Unknown item type string:" << str;
    return ItemType::Create;
}
