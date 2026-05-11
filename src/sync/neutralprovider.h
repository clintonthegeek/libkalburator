#ifndef KALBURATOR_SYNC_NEUTRALPROVIDER_H
#define KALBURATOR_SYNC_NEUTRALPROVIDER_H

#include <functional>
#include <memory>
#include "iprovider.h"
#include "collectioninfo.h"

namespace Kalburator::Sync {

class IBlobBackend;

/**
 * @brief Account-neutral IProvider adapter.
 *
 * Wraps a single-collection backend factory and a CollectionInfo so that
 * backends with no account concept (local files, raw-file sinks, etc.)
 * fit the IProvider shape without requiring a full CalDAV/CardDAV provider.
 *
 * The provider is always considered "connected" once connect() is called;
 * there is no network auth or capability discovery.
 */
class NeutralProvider : public IProvider {
    Q_OBJECT
public:
    using BackendFactory = std::function<std::unique_ptr<IBlobBackend>()>;

    NeutralProvider(QString kind, CollectionInfo info, BackendFactory factory,
                    QObject *parent = nullptr);
    ~NeutralProvider() override;

    // Non-copyable/non-movable (QObject rule)
    NeutralProvider(const NeutralProvider &) = delete;
    NeutralProvider &operator=(const NeutralProvider &) = delete;

    // ── IProvider ─────────────────────────────────────────────────────
    QString id() const override { return m_id; }
    QString kind() const override { return m_kind; }
    QString displayName() const override { return m_info.name; }

    void load(const BackendConfiguration &config) override;
    BackendConfiguration save() const override;

    QWidget *createConfigWidget(QWidget *parent) override;
    QFuture<bool> connect() override;
    void disconnect() override;
    bool isConnected() const override { return m_connected; }
    QList<CollectionInfo> collections() const override { return { m_info }; }
    std::unique_ptr<IBlobBackend> createBackend(const QString &collectionId) override;

private:
    QString          m_id;
    QString          m_kind;
    CollectionInfo   m_info;
    BackendFactory   m_factory;
    bool             m_connected = false;
};

} // namespace Kalburator::Sync

#endif
