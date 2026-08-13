#include "modules/certificates/ui/CertificateListWidget.h"

#include "core/InstallationContext.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/ui/CertificateEditDialog.h"
#include "modules/certificates/ui/ListNumberItem.h"

#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

// The certificate id is carried on the row's first cell, so an edit knows
// which row it is editing without showing a UUID to the user.
constexpr int kCertificateIdRole = Qt::UserRole + 1;

// certificate-crud-spec §8.3: No. · Name · Issue Date · Expiry Date · Category.
enum class Column {
    ListNumber = 0,
    Name       = 1,
    IssueDate  = 2,
    Expiry     = 3,
    Category   = 4
};

constexpr int kColumnCount = 5;

// Which page of the internal stack is showing. Named so the tests can assert
// the prompt state rather than guessing from an empty table.
enum class Page {
    Prompt = 0,
    List   = 1
};

QString categoryLabel(CertificateCategory category)
{
    switch (category) {
    case CertificateCategory::Statutory:
        return CertificateListWidget::tr("Statutory");
    case CertificateCategory::Class:
        return CertificateListWidget::tr("Class");
    case CertificateCategory::Equipment:
        return CertificateListWidget::tr("Equipment");
    case CertificateCategory::Other:
        return CertificateListWidget::tr("Other");
    case CertificateCategory::Unset:
        break;
    }
    return QString();
}

const QString kDateFormat = QStringLiteral("dd MMM yyyy");

QString expiryLabel(const Certificate& certificate)
{
    if (certificate.neverExpires()) {
        return CertificateListWidget::tr("Does not expire");
    }
    return certificate.expiryDate.toString(kDateFormat);
}

} // namespace

CertificateListWidget::CertificateListWidget(CertificateRepository&     repository,
                                             const InstallationContext& installation,
                                             QWidget*                   parent)
    : QWidget(parent)
    , m_repository(repository)
{
    m_addButton = new QPushButton(tr("&Add Certificate"), this);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("certificateTable"));
    m_table->setColumnCount(kColumnCount);
    m_table->setHorizontalHeaderLabels(
        {tr("No."), tr("Name"), tr("Issue Date"), tr("Expiry Date"), tr("Category")});
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

    auto* hint = new QLabel(tr("Double-click a certificate to edit it."), this);
    hint->setEnabled(false); // renders as secondary text

    auto* listPage       = new QWidget(this);
    auto* listPageLayout = new QVBoxLayout(listPage);
    listPageLayout->setContentsMargins(0, 0, 0, 0);
    listPageLayout->addWidget(m_addButton, 0, Qt::AlignLeft);
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
    reload();
}

QString CertificateListWidget::vesselId() const
{
    return m_vesselId;
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
        return;
    }

    QList<Certificate> certificates;
    if (!m_repository.list(m_vesselId, &certificates)) {
        qWarning() << "Could not load certificates:" << m_repository.errorString();
        QMessageBox::warning(this, tr("Could not load certificates"),
                             m_repository.errorString());
        return;
    }

    // Sorting has to be off while rows are filled, or the table reorders
    // underneath the loop and cells land in the wrong rows.
    m_table->setSortingEnabled(false);
    m_table->setRowCount(certificates.size());

    for (int row = 0; row < certificates.size(); ++row) {
        const Certificate& certificate = certificates.at(row);

        // The id rides on the first cell, so an edit knows which row it is
        // editing without a UUID ever being shown.
        auto* numberItem = new ListNumberItem(certificate.listNumber);
        numberItem->setData(kCertificateIdRole, certificate.id);

        m_table->setItem(row, static_cast<int>(Column::ListNumber), numberItem);
        m_table->setItem(row, static_cast<int>(Column::Name),
                         new QTableWidgetItem(certificate.name));
        m_table->setItem(row, static_cast<int>(Column::IssueDate),
                         new QTableWidgetItem(certificate.issueDate.toString(kDateFormat)));
        m_table->setItem(row, static_cast<int>(Column::Expiry),
                         new QTableWidgetItem(expiryLabel(certificate)));
        m_table->setItem(row, static_cast<int>(Column::Category),
                         new QTableWidgetItem(categoryLabel(certificate.category)));
    }

    m_table->setSortingEnabled(true);
}

void CertificateListWidget::addCertificate()
{
    if (m_vesselId.isEmpty()) {
        return;
    }

    CertificateEditDialog dialog(m_repository, m_vesselId, std::nullopt, this);
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
    const QString id = numberItem->data(kCertificateIdRole).toString();

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

    CertificateEditDialog dialog(m_repository, m_vesselId, certificate, this);
    if (dialog.exec() == QDialog::Accepted) {
        reload();
    }
}
