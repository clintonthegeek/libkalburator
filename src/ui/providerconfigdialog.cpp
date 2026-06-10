#include "providerconfigdialog.h"
#include "collectionpickerwidget.h"
#include "../sync/providermanager.h"
#include "../sync/iprovider.h"
#include "../sync/iproviderconfigwidget.h"
#include "../sync/backendregistry.h"
#include "../sync/backendcontribution.h"

#include <algorithm>

#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QFrame>
#include <QFutureWatcher>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
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

    m_buttonRow = new QHBoxLayout;
    m_testButton = new QPushButton(tr("Test connection"), this);
    QObject::connect(m_testButton, &QPushButton::clicked,
                     this, &ProviderConfigDialog::onTestClicked);
    m_buttonRow->addWidget(m_testButton);
    m_buttonRow->addStretch();
    auto *bb = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    m_saveButton = bb->button(QDialogButtonBox::Save);
    m_saveButton->setEnabled(false);
    QObject::connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    m_buttonRow->addWidget(bb);
    root->addLayout(m_buttonRow);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("testStatusLabel"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_statusLabel);

    // §4.4 — error-details disclosure: "Details ▶" toggle + collapsible plain-text view.
    // Built here; shown/hidden in onConnectFinished depending on outcome.
    m_detailsBtn = new QToolButton(this);
    m_detailsBtn->setObjectName(QStringLiteral("testDetailsButton"));
    m_detailsBtn->setText(tr("Details"));
    m_detailsBtn->setCheckable(true);
    m_detailsBtn->setArrowType(Qt::RightArrow);
    m_detailsBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_detailsBtn->hide();
    root->addWidget(m_detailsBtn);

    m_detailsText = new QPlainTextEdit(this);
    m_detailsText->setObjectName(QStringLiteral("testDetailsText"));
    m_detailsText->setReadOnly(true);
    m_detailsText->setMaximumHeight(100);
    m_detailsText->hide();
    root->addWidget(m_detailsText);

    QObject::connect(m_detailsBtn, &QToolButton::toggled, m_detailsText, &QPlainTextEdit::setVisible);
    QObject::connect(m_detailsBtn, &QToolButton::toggled, this, [this](bool open) {
        m_detailsBtn->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });

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
    if (m_noCalendarsPanel) m_noCalendarsPanel->setVisible(false);
    m_saveButton->setEnabled(false);
    if (m_statusLabel) m_statusLabel->clear();
    // Reset error-details disclosure
    if (m_detailsBtn)  { m_detailsBtn->setChecked(false);  m_detailsBtn->hide(); }
    if (m_detailsText) { m_detailsText->clear();            m_detailsText->hide(); }
}

void ProviderConfigDialog::applyWidgetToProvider() const
{
    if (!m_currentProvider || !m_embeddedConfig) return;
    if (auto *cw = dynamic_cast<Sync::IProviderConfigWidget *>(m_embeddedConfig))
        m_currentProvider->load(cw->configuration());
}

void ProviderConfigDialog::setAutofillProfiles(const QList<AutofillProfile> &profiles)
{
    m_autofillProfiles = profiles;

    // Rebuild from scratch so repeated calls are well-defined.
    if (m_autofillButton) {
        delete m_autofillButton;   // deletes its QMenu child too
        m_autofillButton = nullptr;
    }
    if (m_autofillProfiles.isEmpty() || !m_buttonRow)
        return;

    m_autofillButton = new QPushButton(tr("Autofill"), this);
    m_autofillButton->setObjectName(QStringLiteral("autofillButton"));
    auto *menu = new QMenu(m_autofillButton);
    for (const AutofillProfile &profile : m_autofillProfiles) {
        QAction *act = menu->addAction(profile.label);
        QObject::connect(act, &QAction::triggered, this,
                         [this, profile]() { applyAutofillProfile(profile); });
    }
    m_autofillButton->setMenu(menu);
    // Leftmost in the button row, before "Test connection".
    m_buttonRow->insertWidget(0, m_autofillButton);
}

void ProviderConfigDialog::applyAutofillProfile(const AutofillProfile &profile)
{
    if (!m_combo)
        return;
    const int idx = m_combo->findData(profile.config.type);
    if (idx < 0)
        return;   // profile references an unregistered backend — safe no-op

    // Selecting the type rebuilds the embedded widget (onProviderChanged ->
    // rebuildProviderWidget). If the index is unchanged, the existing widget
    // is reused — either way m_embeddedConfig is valid below.
    m_combo->setCurrentIndex(idx);

    if (auto *cw = dynamic_cast<Sync::IProviderConfigWidget *>(m_embeddedConfig))
        cw->setConfiguration(profile.config);
}

void ProviderConfigDialog::onTestClicked()
{
    if (!m_currentProvider) return;
    // Bridge the user's edited values into the provider before connecting;
    // otherwise connect() runs against an empty config and fails immediately.
    applyWidgetToProvider();
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

    // Always reset the details disclosure — it will be reshown for errors below.
    if (m_detailsBtn)  { m_detailsBtn->setChecked(false);  m_detailsBtn->hide(); }
    if (m_detailsText) { m_detailsText->clear();            m_detailsText->hide(); }

    if (ok) {
        auto allCollections = m_currentProvider->collections();
        QList<Sync::CollectionInfo> calendarOnly;
        calendarOnly.reserve(allCollections.size());
        for (const auto &c : allCollections) {
            if (c.type == QStringLiteral("calendar"))
                calendarOnly.append(c);
        }

        const int n = calendarOnly.size();

        // §4.3 — 0-calendars state: disable Save, hide picker, show help panel.
        if (n == 0) {
            m_saveButton->setEnabled(false);
            m_picker->setVisible(false);

            // Build the help panel lazily and insert it above the picker in the
            // root layout (root is the top-level QVBoxLayout of this dialog).
            if (!m_noCalendarsPanel) {
                m_noCalendarsPanel = new QFrame(this);
                m_noCalendarsPanel->setObjectName(QStringLiteral("noCalendarsPanel"));
                m_noCalendarsPanel->setFrameShape(QFrame::Box);

                auto *vbox = new QVBoxLayout(m_noCalendarsPanel);

                auto *title = new QLabel(tr("No calendars found on this account."), m_noCalendarsPanel);
                title->setTextFormat(Qt::PlainText);
                QFont f = title->font();
                f.setBold(true);
                title->setFont(f);

                auto *body = new QLabel(tr(
                    "This could mean:\n"
                    " • The account has no calendars yet.\n"
                    " • The URL points to the wrong server or path.\n"
                    " • The credentials don’t have permission to list calendars."
                ), m_noCalendarsPanel);
                body->setWordWrap(true);
                body->setTextFormat(Qt::PlainText);

                auto *btnRow = new QHBoxLayout;
                auto *tryAgainBtn = new QPushButton(tr("Try Again"), m_noCalendarsPanel);
                auto *backBtn     = new QPushButton(tr("Check the URL"), m_noCalendarsPanel);
                btnRow->addWidget(tryAgainBtn);
                btnRow->addWidget(backBtn);
                btnRow->addStretch();

                vbox->addWidget(title);
                vbox->addWidget(body);
                vbox->addLayout(btnRow);

                // "Try Again" re-runs the test with whatever is currently in the
                // embedded config widget. "Check the URL" closes the picker area
                // and returns focus to the config fields (the embed host is always
                // visible; hiding the panel is sufficient to restore the layout).
                QObject::connect(tryAgainBtn, &QPushButton::clicked,
                                 this, &ProviderConfigDialog::onTestClicked);
                QObject::connect(backBtn, &QPushButton::clicked, this, [this]() {
                    if (m_noCalendarsPanel) m_noCalendarsPanel->setVisible(false);
                    if (m_statusLabel) m_statusLabel->clear();
                });

                // Insert just above m_picker in the root layout.
                auto *root = qobject_cast<QVBoxLayout *>(layout());
                if (root) {
                    const int pickerIdx = root->indexOf(m_picker);
                    root->insertWidget(pickerIdx >= 0 ? pickerIdx : root->count(),
                                      m_noCalendarsPanel);
                }
            }
            m_noCalendarsPanel->setVisible(true);

            if (m_statusLabel) m_statusLabel->setText(tr("Connected — no calendars found."));
        } else {
            // n > 0: normal success path.
            if (m_noCalendarsPanel) m_noCalendarsPanel->setVisible(false);
            m_picker->setCollections(calendarOnly);
            m_picker->setVisible(true);
            m_saveButton->setEnabled(true);

            QString msg = tr("Connected — %n collection(s) found", nullptr, n);
            // A partial success (e.g. CalDAV worked but CardDAV didn't) is
            // reported via lastWarning() even when connect() overall succeeded.
            const QString warning = m_currentProvider->lastWarning();
            if (!warning.isEmpty())
                msg += QStringLiteral("\n⚠ %1").arg(warning);
            if (m_statusLabel) m_statusLabel->setText(msg);
        }
    } else {
        // Connection failure. §4.4 — show error summary in the status label
        // and expose an expandable Details disclosure for the raw error text.
        if (m_noCalendarsPanel) m_noCalendarsPanel->setVisible(false);

        // Prefer the captured error(); fall back to lastWarning(), then a
        // generic message — but always say more than just "Failed".
        QString reason = m_lastTestError;
        if (reason.isEmpty()) reason = m_currentProvider->lastWarning();
        if (reason.isEmpty()) reason = tr("Connection failed (no detail reported).");

        if (m_statusLabel)
            m_statusLabel->setText(tr("Connection failed. Check the URL and credentials."));

        // Populate and surface the details disclosure with the raw reason.
        if (m_detailsBtn && m_detailsText) {
            m_detailsText->setPlainText(reason);
            m_detailsBtn->show();
            // Leave collapsed by default — user expands if they need the detail.
        }
    }
}

Sync::BackendConfiguration ProviderConfigDialog::result() const
{
    // Pull the latest widget values into the provider so the saved config
    // reflects what the user typed (preserving provider-managed fields like
    // id that the widget doesn't carry).
    applyWidgetToProvider();
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
