#include <kalburator/sync/secretstore.h>

#include <QUuid>

namespace Kalburator::Sync {

QString InMemorySecretStore::put(const QString &secret)
{
    if (secret.isEmpty())
        return {};
    const QString reference = QStringLiteral("secret:")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_values.insert(reference, secret);
    return reference;
}

QString InMemorySecretStore::get(const QString &reference) const
{
    return m_values.value(reference);
}

bool InMemorySecretStore::remove(const QString &reference)
{
    return m_values.remove(reference) > 0;
}

namespace {
InMemorySecretStore builtinStore;
SecretStore *activeStore = &builtinStore;
}

SecretStore *SecretStoreRegistry::defaultStore() { return activeStore; }
void SecretStoreRegistry::setDefaultStore(SecretStore *store)
{
    activeStore = store ? store : &builtinStore;
}

} // namespace Kalburator::Sync
