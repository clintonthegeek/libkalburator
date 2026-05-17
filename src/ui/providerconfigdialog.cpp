#include "providerconfigdialog.h"
#include "collectionpickerwidget.h"
#include "../sync/providermanager.h"
#include "../sync/iprovider.h"
#include "../sync/backendregistry.h"
#include "../sync/backendcontribution.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

namespace Kalburator::Ui {

ProviderConfigDialog::ProviderConfigDialog(
        Sync::ProviderManager *manager,
        const QList<ProviderKind> &availableKinds,
        Mode mode,
        const Sync::BackendConfiguration &existing,
        QWidget *parent)
    : QDialog(parent)
    , m_manager(manager)
    , m_availableKinds(availableKinds)
    , m_mode(mode)
    , m_existing(existing)
{
    auto *root = new QVBoxLayout(this);

    m_combo = new QComboBox(this);
    m_combo->setObjectName(QStringLiteral("providerCombo"));
    for (const auto &k : m_availableKinds)
        m_combo->addItem(k.displayLabel, k.backendType);

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

    QObject::connect(m_combo, &QComboBox::currentIndexChanged,
                     this, &ProviderConfigDialog::onProviderChanged);

    if (mode == EditExisting) {
        const int idx = m_combo->findData(existing.type);
        if (idx >= 0) m_combo->setCurrentIndex(idx);
    }
    rebuildProviderWidget();
}

ProviderConfigDialog::~ProviderConfigDialog() = default;

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
}

void ProviderConfigDialog::onTestClicked()
{
    if (!m_currentProvider) return;
    m_testButton->setEnabled(false);
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
    if (!m_currentProvider) return;
    if (ok) {
        m_picker->setCollections(m_currentProvider->collections());
        m_picker->setVisible(true);
        m_saveButton->setEnabled(true);
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
