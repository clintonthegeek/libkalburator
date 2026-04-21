#ifndef BACKENDREGISTRY_H
#define BACKENDREGISTRY_H

#include <QObject>
#include <QMap>
#include <QString>
#include <functional>

namespace Kalburator::Sync {

class SyncBackend;

/**
 * @brief Factory function type for creating backend instances.
 *
 * Takes backend-specific configuration as QVariantMap.
 */
using BackendFactory = std::function<SyncBackend*(const QVariantMap &config, QObject *parent)>;

/**
 * @brief Registry and factory for sync backends.
 *
 * Manages available backend types and creates backend instances.
 * Designed to support the future SyncRouter which will need to
 * access backends by ID from the sync mapping configuration.
 *
 * Usage:
 *   BackendRegistry registry;
 *   registry.registerBackendType("local", &LocalBackend::create);
 *   registry.registerBackendType("orgmode", &OrgBackend::create);
 *   registry.registerBackendType("caldav", &RemoteBackend::create);
 *
 *   auto *backend = registry.createBackend("local", config, parent);
 */
class BackendRegistry : public QObject
{
    Q_OBJECT
public:
    explicit BackendRegistry(QObject *parent = nullptr);
    ~BackendRegistry() override = default;

    /**
     * @brief Register a backend type with its factory function.
     * @param typeName The backend type identifier (e.g., "local", "orgmode", "caldav")
     * @param factory Factory function to create instances
     */
    void registerBackendType(const QString &typeName, BackendFactory factory);

    /**
     * @brief Unregister a backend type.
     */
    void unregisterBackendType(const QString &typeName);

    /**
     * @brief Check if a backend type is registered.
     */
    bool hasBackendType(const QString &typeName) const;

    /**
     * @brief Get list of all registered backend type names.
     */
    QStringList registeredTypes() const;

    /**
     * @brief Create a new backend instance of the given type.
     * @param typeName The backend type to create
     * @param config Backend-specific configuration
     * @param parent Parent QObject for the new backend
     * @return New backend instance, or nullptr if type not registered
     */
    SyncBackend* createBackend(const QString &typeName,
                               const QVariantMap &config,
                               QObject *parent = nullptr) const;

    /**
     * @brief Register a live backend instance by ID.
     *
     * Used by CollectionController to make backends accessible
     * to the SyncRouter by their configured ID.
     */
    void registerBackendInstance(const QString &backendId, SyncBackend *backend);

    /**
     * @brief Unregister a backend instance.
     */
    void unregisterBackendInstance(const QString &backendId);

    /**
     * @brief Get a registered backend instance by ID.
     */
    SyncBackend* backendInstance(const QString &backendId) const;

    /**
     * @brief Get all registered backend instance IDs.
     */
    QStringList registeredInstanceIds() const;

    /**
     * @brief Get user-friendly display name for a backend type.
     * @param typeName Backend type identifier (e.g., "local", "caldav")
     * @return Human-readable name (e.g., "Local ICS Calendar", "CalDAV Server")
     */
    static QString backendDisplayName(const QString &typeName);

    /**
     * @brief Get list of available backend types for UI menus.
     * @return List of {typeName, displayName} pairs for all registered types
     */
    struct BackendTypeInfo {
        QString typeName;
        QString displayName;
    };
    QList<BackendTypeInfo> availableBackendTypes() const;

signals:
    /**
     * @brief Emitted when a backend type is registered.
     */
    void backendTypeRegistered(const QString &typeName);

    /**
     * @brief Emitted when a backend instance is registered.
     */
    void backendInstanceRegistered(const QString &backendId);

    /**
     * @brief Emitted when a backend instance is unregistered.
     */
    void backendInstanceUnregistered(const QString &backendId);

private:
    QMap<QString, BackendFactory> m_factories;
    QMap<QString, SyncBackend*> m_instances;
};

} // namespace Kalburator::Sync

#endif // BACKENDREGISTRY_H
