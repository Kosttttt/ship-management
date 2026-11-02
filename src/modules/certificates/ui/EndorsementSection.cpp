#include "modules/certificates/ui/EndorsementSection.h"

#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/ui/EndorsementEditDialog.h"

#include <QDebug>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

// The row's position in m_endorsements, so a selected row can be turned back
// into the whole struct rather than into three strings. Kept here rather than
// in CertificateListRow.h, which is about the certificate list's own rows.
constexpr int kIndexRole = Qt::UserRole + 1;

QString surveyTypeLabel(SurveyType type)
{
    switch (type) {
    case SurveyType::Annual:
        return EndorsementSection::tr("Annual");
    case SurveyType::Intermediate:
        return EndorsementSection::tr("Intermediate");
    case SurveyType::Initial:
        return EndorsementSection::tr("Initial");
    case SurveyType::Renewal:
        return EndorsementSection::tr("Renewal");
    case SurveyType::Unset:
        break;
    }
    return QString();
}

} // namespace

EndorsementSection::EndorsementSection(EndorsementRepository&   repository,
                                       const QString&           certificateId,
                                       const QList<SurveyType>& allowedTypes,
                                       QWidget*                 parent)
    : QGroupBox(tr("Endorsements"), parent)
    , m_repository(repository)
    , m_certificateId(certificateId)
    , m_allowedTypes(allowedTypes)
{
    setObjectName(QStringLiteral("endorsementsSection"));

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("endorsementTable"));
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("Type"), tr("Date"), tr("Place")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers); // edited via the dialog
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setMaximumHeight(120);
    m_table->setMinimumHeight(90);

    m_addButton = new QPushButton(tr("Add &Endorsement"), this);
    m_addButton->setObjectName(QStringLiteral("addEndorsementButton"));

    m_editButton = new QPushButton(tr("Ed&it"), this);
    m_editButton->setObjectName(QStringLiteral("editEndorsementButton"));
    m_editButton->setEnabled(false);

    m_deleteButton = new QPushButton(tr("Dele&te"), this);
    m_deleteButton->setObjectName(QStringLiteral("deleteEndorsementButton"));
    m_deleteButton->setEnabled(false);

    auto* buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->addWidget(m_addButton);
    buttonRow->addWidget(m_editButton);
    buttonRow->addWidget(m_deleteButton);
    buttonRow->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_table);
    layout->addLayout(buttonRow);

    connect(m_addButton, &QPushButton::clicked, this, &EndorsementSection::addEndorsement);
    connect(m_editButton, &QPushButton::clicked, this, &EndorsementSection::editEndorsement);
    connect(m_deleteButton, &QPushButton::clicked, this, &EndorsementSection::deleteEndorsement);

    // A double-click on the row does what the Edit button does, the same way
    // the certificate list itself opens a certificate.
    connect(m_table, &QTableWidget::itemDoubleClicked, this,
            [this]() { editEndorsement(); });
    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            &EndorsementSection::updateButtonStates);

    reload();
}

void EndorsementSection::reload()
{
    m_endorsements.clear();
    if (!m_repository.list(m_certificateId, &m_endorsements)) {
        // A section that cannot be read is shown empty rather than blocking
        // the certificate's own fields, the same judgment the list screen
        // makes for an unreadable endorsement list.
        qWarning() << "Could not load endorsements:" << m_repository.errorString();
    }

    m_table->setRowCount(m_endorsements.size());
    for (int row = 0; row < m_endorsements.size(); ++row) {
        const Endorsement& endorsement = m_endorsements.at(row);

        auto* typeItem = new QTableWidgetItem(surveyTypeLabel(endorsement.surveyType));
        // The index, not the id: what the edit dialog needs is every field,
        // and the loaded struct has them all.
        typeItem->setData(kIndexRole, row);
        m_table->setItem(row, 0, typeItem);

        m_table->setItem(row, 1,
                         new QTableWidgetItem(endorsement.endorsementDate.toString(
                             QStringLiteral("dd MMM yyyy"))));
        m_table->setItem(row, 2, new QTableWidgetItem(endorsement.place));
    }

    // A rebuilt table can drop the selection without emitting anything, which
    // would otherwise leave Edit and Delete enabled with nothing selected.
    updateButtonStates();
}

std::optional<Endorsement> EndorsementSection::selectedEndorsement() const
{
    const QModelIndexList selected = m_table->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        return std::nullopt;
    }

    QTableWidgetItem* item = m_table->item(selected.first().row(), 0);
    if (item == nullptr) {
        return std::nullopt;
    }

    const int index = item->data(kIndexRole).toInt();
    if (index < 0 || index >= m_endorsements.size()) {
        return std::nullopt;
    }
    return m_endorsements.at(index);
}

void EndorsementSection::updateButtonStates()
{
    const bool hasSelection = selectedEndorsement().has_value();
    m_editButton->setEnabled(hasSelection);
    m_deleteButton->setEnabled(hasSelection);
}

void EndorsementSection::addEndorsement()
{
    EndorsementEditDialog dialog(m_repository, m_certificateId, m_allowedTypes, std::nullopt,
                                 this);
    if (dialog.exec() == QDialog::Accepted) {
        reload();
    }
}

void EndorsementSection::editEndorsement()
{
    const std::optional<Endorsement> selected = selectedEndorsement();
    if (!selected.has_value()) {
        return;
    }

    // §6: the same dialog, pre-filled, saving back to the same row.
    EndorsementEditDialog dialog(m_repository, m_certificateId, m_allowedTypes, selected, this);
    if (dialog.exec() == QDialog::Accepted) {
        reload();
    }
}

void EndorsementSection::deleteEndorsement()
{
    const std::optional<Endorsement> selected = selectedEndorsement();
    if (!selected.has_value()) {
        return;
    }

    // Confirmed, because a survey record disappearing changes what the
    // certificate's status says. No warning about files: an endorsement has
    // none of its own.
    const auto answer = QMessageBox::question(
        this, tr("Delete endorsement"),
        tr("Delete the %1 survey recorded on %2?\n\n"
           "The certificate's status will be recalculated without it.")
            .arg(surveyTypeLabel(selected->surveyType),
                 selected->endorsementDate.toString(QStringLiteral("dd MMM yyyy"))),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (answer != QMessageBox::Yes) {
        return;
    }

    if (!m_repository.softDelete(selected->id)) {
        QMessageBox::warning(this, tr("Could not delete the endorsement"),
                             m_repository.errorString());
        return;
    }

    reload();
}
