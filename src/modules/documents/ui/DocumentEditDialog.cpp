#include "modules/documents/ui/DocumentEditDialog.h"

#include "modules/documents/ui/DocumentVersionFields.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

// The scope choice rides on the combo's item data: an empty string is
// fleet-wide, which is exactly how the database stores it.
const QString kFleetScope = QString();

void fillCategories(QComboBox* combo)
{
    combo->addItem(DocumentEditDialog::tr("SMS manuals & procedures"),
                   static_cast<int>(DocumentCategory::Sms));
    combo->addItem(DocumentEditDialog::tr("Circulars & notices"),
                   static_cast<int>(DocumentCategory::Circular));
    combo->addItem(DocumentEditDialog::tr("Plans & manuals"),
                   static_cast<int>(DocumentCategory::PlanManual));
    combo->addItem(DocumentEditDialog::tr("Reports & records"),
                   static_cast<int>(DocumentCategory::Report));
}

} // namespace

DocumentEditDialog::DocumentEditDialog(const InstallationContext&     installation,
                                       const QString&                 selectedVesselId,
                                       const QString&                 selectedVesselName,
                                       const std::optional<Document>& existing,
                                       QWidget*                       parent)
    : QDialog(parent)
    , m_isEditing(existing.has_value())
{
    setWindowTitle(m_isEditing ? tr("Edit Document Details") : tr("Add Document"));
    setModal(true);
    setObjectName(QStringLiteral("documentEditDialog"));

    m_scopeCombo = new QComboBox(this);
    m_scopeCombo->setObjectName(QStringLiteral("documentScopeCombo"));

    const bool onAVessel =
        installation.isConfigured() && installation.mode() == InstallationMode::Vessel;

    // §4.3: a ship may only add its own vessel's documents, so fleet-wide is
    // not even offered there — the repository refuses it anyway, but a choice
    // that is always refused is not a choice worth showing.
    if (!onAVessel) {
        m_scopeCombo->addItem(tr("Fleet-wide"), kFleetScope);
    }
    if (!selectedVesselId.isEmpty()) {
        m_scopeCombo->addItem(selectedVesselName.isEmpty() ? tr("This vessel")
                                                           : selectedVesselName,
                              selectedVesselId);
    }

    m_categoryCombo = new QComboBox(this);
    m_categoryCombo->setObjectName(QStringLiteral("documentCategoryCombo"));
    fillCategories(m_categoryCombo);

    m_referenceEdit = new QLineEdit(this);
    m_referenceEdit->setObjectName(QStringLiteral("documentReferenceEdit"));
    m_referenceEdit->setPlaceholderText(tr("Optional — e.g. SMS-03"));

    m_titleEdit = new QLineEdit(this);
    m_titleEdit->setObjectName(QStringLiteral("documentTitleEdit"));

    m_notesEdit = new QPlainTextEdit(this);
    m_notesEdit->setObjectName(QStringLiteral("documentNotesEdit"));
    m_notesEdit->setMaximumHeight(70);

    auto* form = new QFormLayout;
    form->addRow(tr("&Scope:"), m_scopeCombo);
    form->addRow(tr("C&ategory:"), m_categoryCombo);
    form->addRow(tr("&Reference:"), m_referenceEdit);
    form->addRow(tr("&Title:"), m_titleEdit);
    form->addRow(tr("&Notes:"), m_notesEdit);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);

    if (existing.has_value()) {
        m_documentId       = existing->id;
        m_existingVesselId = existing->vesselId;

        m_referenceEdit->setText(existing->reference);
        m_titleEdit->setText(existing->title);
        m_notesEdit->setPlainText(existing->notes);

        const int categoryIndex =
            m_categoryCombo->findData(static_cast<int>(existing->category));
        if (categoryIndex >= 0) {
            m_categoryCombo->setCurrentIndex(categoryIndex);
        }

        // The scope is shown but not editable: moving a document between the
        // fleet and a vessel would leave its files filed under the wrong
        // folder, and moving them is not in step 1 (§4.7). The repository
        // does not write it either.
        const int scopeIndex = m_scopeCombo->findData(existing->vesselId);
        if (scopeIndex >= 0) {
            m_scopeCombo->setCurrentIndex(scopeIndex);
        } else {
            m_scopeCombo->addItem(existing->isFleetWide() ? tr("Fleet-wide") : tr("Its vessel"),
                                  existing->vesselId);
            m_scopeCombo->setCurrentIndex(m_scopeCombo->count() - 1);
        }
        m_scopeCombo->setEnabled(false);
    } else {
        // Adding: the first file comes with the document, because a document
        // with no version has nothing to open.
        auto* group = new QGroupBox(tr("First version"), this);
        m_versionFields = new DocumentVersionFields(group);

        auto* groupLayout = new QVBoxLayout(group);
        groupLayout->addWidget(m_versionFields);

        layout->addWidget(group);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton    = buttons->button(QDialogButtonBox::Ok);
    m_okButton->setText(m_isEditing ? tr("Save") : tr("Add"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_titleEdit, &QLineEdit::textChanged, this, [this] { updateOkState(); });
    if (m_versionFields != nullptr) {
        connect(m_versionFields, &DocumentVersionFields::fileChosen, this,
                [this] { updateOkState(); });
    }

    updateOkState();
    resize(520, m_isEditing ? 320 : 480);
}

void DocumentEditDialog::updateOkState()
{
    // A title is required, and an addition also needs its first file. The
    // repository checks both as well; this is what stops somebody being told
    // off after filling the rest of the form in.
    bool ready = !m_titleEdit->text().trimmed().isEmpty();
    if (m_versionFields != nullptr && m_versionFields->sourcePath().isEmpty()) {
        ready = false;
    }
    m_okButton->setEnabled(ready);
}

Document DocumentEditDialog::document() const
{
    Document document;
    document.id = m_documentId;
    // When editing, the stored scope — the combo is disabled, and the
    // repository ignores this field on an update anyway.
    document.vesselId  = m_isEditing ? m_existingVesselId : m_scopeCombo->currentData().toString();
    document.category  = static_cast<DocumentCategory>(m_categoryCombo->currentData().toInt());
    document.reference = m_referenceEdit->text().trimmed();
    document.title     = m_titleEdit->text().trimmed();
    document.notes     = m_notesEdit->toPlainText().trimmed();
    return document;
}

QString DocumentEditDialog::firstVersionSourcePath() const
{
    return m_versionFields == nullptr ? QString() : m_versionFields->sourcePath();
}

DocumentVersion DocumentEditDialog::firstVersionValues() const
{
    return m_versionFields == nullptr ? DocumentVersion() : m_versionFields->values();
}
