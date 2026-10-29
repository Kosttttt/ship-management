#include "modules/certificates/ui/CertificateListWidget.h"

#include "core/AppSettingRepository.h"
#include "core/InstallationContext.h"
#include "modules/certificates/data/CertificateExtensionRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/domain/CertificateState.h"
#include "modules/certificates/ui/CertificateEditDialog.h"
#include "modules/certificates/ui/CertificateListActions.h"
#include "modules/certificates/ui/CertificateListRow.h"
#include "modules/certificates/ui/ListNumberItem.h"
#include "modules/certificates/ui/StatusItem.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

// certificate-list-status-spec §3:
// No. · Name · Status · Expiry Date · Survey From · Survey To · Days Left.
//
// Issue Date and Category left this screen with step 7 — they are still on the
// edit dialog. This list's job is now "what needs attention".
enum class Column {
    ListNumber = 0,
    Name       = 1,
    Status     = 2,
    Expiry     = 3,
    SurveyFrom = 4,
    SurveyTo   = 5,
    DaysLeft   = 6
};

constexpr int kColumnCount = 7;

// The row data in CertificateListRow.h rides on the first cell, which is this
// column. Asserted rather than assumed, since the two are declared apart.
static_assert(static_cast<int>(Column::ListNumber) == CertificateRow::kDataColumn,
              "row data must live on the column fillRow() writes it to");

// Which page of the internal stack is showing. Named so the tests can assert
// the prompt state rather than guessing from an empty table.
enum class Page {
    Prompt = 0,
    List   = 1
};

const QString kDateFormat = QStringLiteral("dd MMM yyyy");

QString expiryLabel(const Certificate& certificate, const CertificateState& state)
{
    // certificate-extension-spec §7: an active extension takes the cell over,
    // with a short suffix so it is obvious at a glance that the date shown is
    // not the certificate's own. No new column — every existing sort and
    // filter behaviour stays exactly as it was.
    if (state.activeExtensionExpiry.has_value()) {
        return CertificateListWidget::tr("%1 — Short Term")
            .arg(state.activeExtensionExpiry->toString(kDateFormat));
    }
    if (certificate.neverExpires()) {
        return CertificateListWidget::tr("Does not expire");
    }
    return certificate.expiryDate.toString(kDateFormat);
}

} // namespace

CertificateListWidget::CertificateListWidget(CertificateRepository&          repository,
                                             EndorsementRepository&          endorsements,
                                             CertificateExtensionRepository& extensions,
                                             AppSettingRepository&           appSettings,
                                             const InstallationContext&      installation,
                                             QWidget*                        parent)
    : QWidget(parent)
    , m_repository(repository)
    , m_endorsements(endorsements)
    , m_extensions(extensions)
    , m_appSettings(appSettings)
{
    m_addButton = new QPushButton(tr("&Add Certificate"), this);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("certificateTable"));
    m_table->setColumnCount(kColumnCount);
    m_table->setHorizontalHeaderLabels({tr("No."), tr("Name"), tr("Status"), tr("Expiry Date"),
                                        tr("Survey From"), tr("Survey To"), tr("Days Left")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers); // edited via the dialog
    m_table->verticalHeader()->setVisible(false);
    m_table->setSortingEnabled(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    // The reference number is the whole point of the field, so it is the sort
    // a user gets without clicking anything (certificate-crud-spec §8.2).
    // Any header click still re-sorts by that column, as before.
    m_table->horizontalHeader()->setSectionResizeMode(static_cast<int>(Column::ListNumber),
                                                      QHeaderView::ResizeToContents);
    m_table->sortByColumn(static_cast<int>(Column::ListNumber), Qt::AscendingOrder);

    // After the table, which it watches for selection changes. It owns the
    // Renew and extension buttons; this widget only places them.
    m_actions = new CertificateListActions(m_repository, m_endorsements, m_extensions, m_table,
                                           this);

    auto* hint = new QLabel(tr("Double-click a certificate to edit it."), this);
    hint->setEnabled(false); // renders as secondary text

    // certificate-list-status-spec §7. Unchecked by default: the list shows
    // everything until asked otherwise.
    m_needsAttentionCheck = new QCheckBox(tr("Show only certificates needing attention"), this);
    m_needsAttentionCheck->setObjectName(QStringLiteral("needsAttentionCheck"));

    auto* controlRow = new QHBoxLayout;
    controlRow->setContentsMargins(0, 0, 0, 0);
    controlRow->addWidget(m_addButton);
    controlRow->addWidget(m_actions->renewButton());
    controlRow->addWidget(m_actions->extensionButton());
    controlRow->addStretch();
    controlRow->addWidget(m_needsAttentionCheck);

    auto* listPage       = new QWidget(this);
    auto* listPageLayout = new QVBoxLayout(listPage);
    listPageLayout->setContentsMargins(0, 0, 0, 0);
    listPageLayout->addLayout(controlRow);
    listPageLayout->addWidget(m_table);
    listPageLayout->addWidget(hint);

    // An explicit prompt, not an empty table: an empty table would look like
    // "this vessel has no certificates", which is a different fact.
    auto* prompt = new QLabel(tr("Select a vessel above to see its certificates."), this);
    prompt->setObjectName(QStringLiteral("selectVesselPrompt"));
    prompt->setAlignment(Qt::AlignCenter);
    prompt->setWordWrap(true);

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("certificateStack"));
    m_stack->insertWidget(static_cast<int>(Page::Prompt), prompt);
    m_stack->insertWidget(static_cast<int>(Page::List), listPage);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_stack);

    connect(m_addButton, &QPushButton::clicked, this, &CertificateListWidget::addCertificate);
    connect(m_table, &QTableWidget::itemDoubleClicked, this,
            &CertificateListWidget::editCertificate);
    // The actions own the button clicks and the selection watching; all this
    // widget has to know is that something changed and the list is now stale.
    connect(m_actions, &CertificateListActions::listChanged, this,
            &CertificateListWidget::reload);
    // The filter works on computed severity, which is never stored, so it can
    // only be applied by recomputing and rebuilding the rows (§7).
    connect(m_needsAttentionCheck, &QCheckBox::toggled, this, &CertificateListWidget::reload);

    // In VESSEL mode the vessel is known once, at construction, and never
    // changes — the same certainty VesselDetailPage already relies on.
    if (installation.isConfigured() && installation.mode() == InstallationMode::Vessel) {
        setVesselId(installation.vesselId());
    } else {
        updateVisibleState();
    }
}

void CertificateListWidget::setVesselId(const QString& vesselId)
{
    m_vesselId = vesselId;
    m_actions->setVesselId(vesselId);
    reload();
}

QString CertificateListWidget::vesselId() const
{
    return m_vesselId;
}

void CertificateListWidget::setNeedsAttentionFilter(bool on)
{
    m_needsAttentionCheck->setChecked(on);
}

void CertificateListWidget::updateVisibleState()
{
    const Page page = m_vesselId.isEmpty() ? Page::Prompt : Page::List;
    m_stack->setCurrentIndex(static_cast<int>(page));
}

void CertificateListWidget::reload()
{
    updateVisibleState();

    if (m_vesselId.isEmpty()) {
        m_table->setRowCount(0);
        emit certificatesChanged();
        return;
    }

    QList<Certificate> certificates;
    if (!m_repository.list(m_vesselId, &certificates)) {
        qWarning() << "Could not load certificates:" << m_repository.errorString();
        QMessageBox::warning(this, tr("Could not load certificates"),
                             m_repository.errorString());
        return;
    }

    // certificate-list-status-spec §8: this is the one legitimate place
    // "today" is read in this call chain. computeCertificateState() never
    // reads the clock itself — that is what makes it testable — so the screen
    // reads it once here and hands the same value to every row.
    const QDate today = QDate::currentDate();

    // settings-app-setting-spec §7: the one place an AppSetting (core)
    // becomes an AlertThresholds (this module), translated a field at a time
    // on this side of the boundary. Core knows nothing about AlertThresholds.
    //
    // A failed read leaves the hardcoded 30/60/90 defaults in place: a list
    // that still shows something reasonable beats one that shows nothing,
    // the same call step 7 made for unreadable endorsements.
    AppSetting      setting;
    AlertThresholds thresholds;
    if (m_appSettings.read(&setting)) {
        thresholds.criticalDays     = setting.criticalDays;
        thresholds.expiringSoonDays = setting.expiringSoonDays;
        thresholds.dueSoonDays      = setting.dueSoonDays;
    } else {
        qWarning() << "Could not read alert thresholds, using defaults:"
                   << m_appSettings.errorString();
    }

    const bool onlyNeedingAttention = m_needsAttentionCheck->isChecked();

    QList<Certificate>      visible;
    QList<CertificateState> states;

    for (const Certificate& certificate : certificates) {
        // One list() per certificate: an N+1 pattern, deliberately not
        // batched at the fleet sizes this app targets (§8).
        QList<Endorsement> endorsements;
        if (!m_endorsements.list(certificate.id, &endorsements)) {
            // A certificate whose endorsements cannot be read is still worth
            // showing; it simply looks as though it has none.
            qWarning() << "Could not load endorsements for" << certificate.name << ":"
                       << m_endorsements.errorString();
        }

        // certificate-extension-spec §3/§6: this screen is one of exactly two
        // places that must look an active extension up and hand its date to
        // the state calculation — CertificateAlertProvider is the other, and
        // the two have to agree or the badge and the list will disagree about
        // the same certificate.
        //
        // An unreadable extension is treated as "none", the same judgment
        // already made for an unreadable endorsement list: a row that still
        // shows the certificate's own dates beats a screen with a hole in it.
        std::optional<CertificateExtension> extension;
        if (!m_extensions.findActiveFor(certificate.id, &extension)) {
            qWarning() << "Could not load the short-term extension for" << certificate.name << ":"
                       << m_extensions.errorString();
        }

        const std::optional<QDate> extensionExpiry =
            extension.has_value() ? std::optional<QDate>(extension->expiryDate) : std::nullopt;

        const CertificateState state =
            computeCertificateState(certificate, endorsements, thresholds, today, extensionExpiry);

        if (onlyNeedingAttention && state.display == DisplayStatus::Valid) {
            continue;
        }

        visible.append(certificate);
        states.append(state);
    }

    // Sorting has to be off while rows are filled, or the table reorders
    // underneath the loop and cells land in the wrong rows.
    m_table->setSortingEnabled(false);
    m_table->setRowCount(visible.size());

    for (int row = 0; row < visible.size(); ++row) {
        fillRow(row, visible.at(row), states.at(row));
    }

    m_table->setSortingEnabled(true);

    // reload() can clear the selection, which would otherwise leave Renew
    // enabled with nothing selected. It is also what refreshes the extension
    // button's label after one has just been started or resolved.
    m_actions->updateButtonStates();

    emit certificatesChanged();
}

void CertificateListWidget::fillRow(int                     row,
                                    const Certificate&      certificate,
                                    const CertificateState& state)
{
    // The id rides on the first cell, so an edit knows which row it is
    // editing without a UUID ever being shown.
    auto* numberItem = new ListNumberItem(certificate.listNumber);
    numberItem->setData(CertificateRow::kCertificateIdRole, certificate.id);
    numberItem->setData(CertificateRow::kNeverExpiresRole, certificate.neverExpires());
    // An invalid QDate when there is no extension, which is what both buttons
    // test for (certificate-extension-spec §7).
    numberItem->setData(CertificateRow::kActiveExtensionRole,
                        state.activeExtensionExpiry.has_value()
                            ? *state.activeExtensionExpiry
                            : QDate());

    m_table->setItem(row, static_cast<int>(Column::ListNumber), numberItem);
    m_table->setItem(row, static_cast<int>(Column::Name), new QTableWidgetItem(certificate.name));
    m_table->setItem(row, static_cast<int>(Column::Status), new StatusItem(state.display));
    m_table->setItem(row, static_cast<int>(Column::Expiry),
                     new QTableWidgetItem(expiryLabel(certificate, state)));

    // §5: blank unless there is an outstanding survey window to show. The two
    // dates are asked directly whether they are valid, rather than inferring
    // it a second time from the severity.
    m_table->setItem(row, static_cast<int>(Column::SurveyFrom),
                     new QTableWidgetItem(state.windowOpens.isValid()
                                              ? state.windowOpens.toString(kDateFormat)
                                              : QString()));
    m_table->setItem(row, static_cast<int>(Column::SurveyTo),
                     new QTableWidgetItem(state.windowCloses.isValid()
                                              ? state.windowCloses.toString(kDateFormat)
                                              : QString()));

    // §6: the raw signed number, negative once overdue. A certificate that
    // never expires has no meaningful count, and a bare 0 would read as "due
    // today", so that cell is left blank instead.
    auto* daysItem = new QTableWidgetItem;
    // An active extension supplies a date to count to even for a certificate
    // that has none of its own, so the cell is driven by whether the state
    // actually counted to something rather than by the certificate alone.
    if (certificate.expiryDate.isValid() || state.activeExtensionExpiry.has_value()) {
        // Stored as a number, not text, so the column sorts numerically
        // rather than putting "-7" after "10".
        daysItem->setData(Qt::DisplayRole, state.daysLeft);
    }
    m_table->setItem(row, static_cast<int>(Column::DaysLeft), daysItem);
}

void CertificateListWidget::addCertificate()
{
    if (m_vesselId.isEmpty()) {
        return;
    }

    // isRenewal spelled out: it sits before `parent`, so passing `this`
    // positionally here would silently convert the pointer to true.
    CertificateEditDialog dialog(m_repository, m_endorsements, m_vesselId, std::nullopt,
                                 /*isRenewal=*/false, this);
    if (dialog.exec() == QDialog::Accepted) {
        reload();
    }
}

void CertificateListWidget::editCertificate(QTableWidgetItem* item)
{
    if (item == nullptr) {
        return;
    }

    // The id lives on the first column, whichever cell was double-clicked.
    QTableWidgetItem* numberItem = m_table->item(item->row(), static_cast<int>(Column::ListNumber));
    if (numberItem == nullptr) {
        return;
    }
    const QString id = numberItem->data(CertificateRow::kCertificateIdRole).toString();

    std::optional<Certificate> certificate;
    if (!m_repository.findById(id, &certificate)) {
        QMessageBox::warning(this, tr("Could not open certificate"), m_repository.errorString());
        return;
    }
    if (!certificate.has_value()) {
        QMessageBox::information(this, tr("Certificate not found"),
                                 tr("This certificate is no longer in the database."));
        reload();
        return;
    }

    // Double-click keeps opening the ordinary, non-destructive edit — the
    // survey-clearing side effect belongs only to the Renew button.
    CertificateEditDialog dialog(m_repository, m_endorsements, m_vesselId, certificate,
                                 /*isRenewal=*/false, this);
    if (dialog.exec() == QDialog::Accepted) {
        reload();
    }
}
