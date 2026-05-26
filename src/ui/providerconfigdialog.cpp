#include "providerconfigdialog.h"
#include "collectionpickerwidget.h"
#include "../sync/providermanager.h"
#include "../sync/iprovider.h"
#include "../sync/backendregistry.h"
#include "../sync/backendcontribution.h"

#include <algorithm>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace Kalburator::Ui {

// O.1.4: registry-aware constructor — builds the dialog, populates from
// the registry, and subscribes to contribution change signals so the
// combo stays live with plugin loads. O.4.10: this is the sole ctor;
// the hardcoded-kinds variant was deleted.
ProviderConfigDialog::ProviderConfigDialog(
        Sync::ProviderManager *manager,
        Sync::BackendRegistry *registry,
        Mode mode,
        const Sync::BackendConfiguration &existing,
        QWidget *parent)
    : QDialog(parent)
    , m_manager(manager)
    , m_mode(mode)
    , m_existing(existing)
{
    auto *root = new QVBoxLayout(this);

    m_combo = new QComboBox(this);
    m_combo->setObjectName(QStringLiteral("providerCombo"));

    auto *formRow = new QFormLayout;
    formRow->addRow(tr("Provider:"), m_combo);
    root->addLayout(formRow);

    auto *embedHost = new QWidget(this);
    embedHost->setObjectName(QStringLiteral("providerConfigEmbed"));
    embedHost->setLayout(new QVBoxLayout(embedHost));
    root->addWidget(embedHost);

    m_picker = new CollectionPickerWidget(this);
    m_picker->setObjectName(QStringLiteral("collectionPicker"));
    m_picker->setVisible(false);
    root->addWidget(m_picker);

    auto *btnRow = new QHBoxLayout;
    m_testButton = new QPushButton(tr("Test connection"), this);
    QObject::connect(m_testButton, &QPushButton::clicked,
                     this, &ProviderConfigDialog::onTestClicked);
    btnRow->addWidget(m_testButton);
    btnRow->addStretch();
    auto *bb = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    m_saveButton = bb->button(QDialogButtonBox::Save);
    m_saveButton->setEnabled(false);
    QObject::connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    btnRow->addWidget(bb);
    root->addLayout(btnRow);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("testStatusLabel"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_statusLabel);

    QObject::connect(m_combo, &QComboBox::currentIndexChanged,
                     this, &ProviderConfigDialog::onProviderChanged);

    Q_ASSERT(registry);
    if (registry) {
        m_registry = registry;
        populateKindsFromRegistry();

        QObject::connect(m_registry,
                         &Sync::BackendRegistry::contributionRegistered,
                         this, [this](const QString &) {
                             populateKindsFromRegistry();
                         });
        QObject::connect(m_registry,
                         &Sync::BackendRegistry::contributionUnregistered,
                         this, [this](const QString &) {
                             populateKindsFromRegistry();
                         });
    }

    if (mode == EditExisting) {
        const int idx = m_combo->findData(existing.type);
        if (idx >= 0) m_combo->setCurrentIndex(idx);
    }
    rebuildProviderWidget();
}

ProviderConfigDialog::~ProviderConfigDialog() = default;

void ProviderConfigDialog::populateKindsFromRegistry()
{
    if (!m_registry || !m_combo) return;

    // Preserve the currently selected backendType so we can restore it.
    const QString currentType = m_combo->currentData().toString();
    // EditExisting: if combo is empty on first populate, fall back to
    // the existing config's type so the right kind is pre-selected.
    const QString effectiveType = currentType.isEmpty() ? m_existing.type : currentType;

    // Build a sorted list of kinds from the registry contributions.
    QList<Sync::BackendContribution*> contribs = m_registry->contributions();
    std::sort(contribs.begin(), contribs.end(),
              [](const Sync::BackendContribution *a, const Sync::BackendContribution *b) {
                  return a->backendType() < b->backendType();
              });

    // Rebuild m_availableKinds and repopulate the combo.
    m_availableKinds.clear();
    m_combo->blockSignals(true);
    m_combo->clear();
    for (const Sync::BackendContribution *c : contribs) {
        m_availableKinds.append(ProviderKind{ c->backendType(), c->displayName() });
        m_combo->addItem(c->displayName(), c->backendType());
    }
    m_combo->blockSignals(false);

    // Restore previous selection if it's still available.
    const int idx = m_combo->findData(effectiveType);
    if (idx >= 0) {
        m_combo->setCurrentIndex(idx);
    } else {
        // Selection was removed or this is initial population — rebuild widget.
        rebuildProviderWidget();
    }
}

void ProviderConfigDialog::onProviderChanged(int)
{
    rebuildProviderWidget();
}

void ProviderConfigDialog::rebuildProviderWidget()
{
    auto *embedHost = findChild<QWidget*>(QStringLiteral("providerConfigEmbed"));
    if (!embedHost) return;

    while (auto *c = embedHost->layout()->takeAt(0)) {
        delete c->widget();
        delete c;
    }
    m_currentProvider.reset();

    m_embeddedConfig = nullptr;

    const QString kind = m_combo->currentData().toString();
    if (kind.isEmpty()) return;

    // M.5: instantiate the provider via the registry contribution.
    // Pass nullptr parent — unique_ptr owns; QObject parent-child would
    // double-delete.
    auto *registry = m_manager ? m_manager->backendRegistry() : nullptr;
    auto *contribution = registry ? registry->contributionFor(kind) : nullptr;
    if (!contribution) return;
    m_currentProvider = contribution->createProvider(nullptr);
    if (!m_currentProvider) return;

    if (m_mode == EditExisting && m_existing.type == kind)
        m_currentProvider->load(m_existing);

    QWidget *w = m_currentProvider->createConfigWidget(embedHost);
    if (w) {
        m_embeddedConfig = w;
        embedHost->layout()->addWidget(w);
    }

    m_picker->setVisible(false);
    m_saveButton->setEnabled(false);
    if (m_statusLabel) m_statusLabel->clear();
}

void ProviderConfigDialog::onTestClicked()
{
    if (!m_currentProvider) return;
    m_testButton->setEnabled(false);
    if (m_statusLabel) m_statusLabel->setText(tr("Testing…"));

    // Capture the provider's error() message for the duration of this test so
    // onConnectFinished can report the actual reason instead of a bare
    // "Failed". The connection is torn down once the test resolves.
    m_lastTestError.clear();
    QObject::disconnect(m_errorConn);
    m_errorConn = QObject::connect(
        m_currentProvider.get(), &Sync::IProvider::error,
        this, [this](const QString &msg) { m_lastTestError = msg; });

    auto fut = m_currentProvider->connect();
    auto *watcher = new QFutureWatcher<bool>(this);
    QObject::connect(watcher, &QFutureWatcher<bool>::finished, this,
        [this, watcher]() {
            const bool ok = watcher->result();
            watcher->deleteLater();
            onConnectFinished(ok);
        });
    watcher->setFuture(fut);
}

void ProviderConfigDialog::onConnectFinished(bool ok)
{
    m_testButton->setEnabled(true);
    QObject::disconnect(m_errorConn);
    if (!m_currentProvider) return;

    if (ok) {
        m_picker->setCollections(m_currentProvider->collections());
        m_picker->setVisible(true);
        m_saveButton->setEnabled(true);

        const int n = m_currentProvider->collections().size();
        QString msg = tr("Connected — %n collection(s) found", nullptr, n);
        // A partial success (e.g. CalDAV worked but CardDAV didn't) is reported
        // via lastWarning() even when connect() overall succeeded.
        const QString warning = m_currentProvider->lastWarning();
        if (!warning.isEmpty())
            msg += QStringLiteral("\n⚠ %1").arg(warning);
        if (m_statusLabel) m_statusLabel->setText(msg);
    } else {
        // Prefer the captured error(); fall back to lastWarning(), then a
        // generic message — but always say more than just "Failed".
        QString reason = m_lastTestError;
        if (reason.isEmpty()) reason = m_currentProvider->lastWarning();
        if (reason.isEmpty()) reason = tr("Connection failed (no detail reported).");
        if (m_statusLabel)
            m_statusLabel->setText(tr("Connection failed: %1").arg(reason));
    }
}

Sync::BackendConfiguration ProviderConfigDialog::result() const
{
    return m_currentProvider ? m_currentProvider->save()
                             : Sync::BackendConfiguration{};
}

QStringList ProviderConfigDialog::selectedCollectionIds() const
{
    return m_picker ? m_picker->selected() : QStringList{};
}

std::unique_ptr<Sync::IProvider> ProviderConfigDialog::takeProvider()
{
    return std::move(m_currentProvider);
}

} // namespace Kalburator::Ui
