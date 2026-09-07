#pragma once

#include <QHash>
#include <QString>

namespace Kalburator::Sync {

class SecretStore {
public:
    virtual ~SecretStore() = default;
    virtual QString put(const QString &secret) = 0;
    virtual QString get(const QString &reference) const = 0;
    virtual bool remove(const QString &reference) = 0;
};

class InMemorySecretStore final : public SecretStore {
public:
    QString put(const QString &secret) override;
    QString get(const QString &reference) const override;
    bool remove(const QString &reference) override;

private:
    QHash<QString, QString> m_values;
};

class SecretStoreRegistry {
public:
    static SecretStore *defaultStore();
    static void setDefaultStore(SecretStore *store);
};

} // namespace Kalburator::Sync
