#include "modules/certificates/service/CertificateCsvFormat.h"

#include "modules/certificates/domain/CertificateValidation.h"
#include "modules/certificates/domain/Endorsement.h"
#include "modules/certificates/service/CertificateCsvValues.h"

namespace {

using namespace CertificateCsvValues;

// §3's column list, in the order export writes it. One list, used both to
// build the header line and to decide whether a header in a file is known, so
// the two can never disagree.
const QStringList& columnNames()
{
    static const QStringList names = {
        QStringLiteral("Id"),
        QStringLiteral("Vessel IMO"),
        QStringLiteral("Vessel Name"),
        QStringLiteral("No."),
        QStringLiteral("Name"),
        QStringLiteral("Category"),
        QStringLiteral("Applies To"),
        QStringLiteral("Certificate Number"),
        QStringLiteral("Issue Date"),
        QStringLiteral("Expiry Date"),
        QStringLiteral("Issued By"),
        QStringLiteral("Place of Issue"),
        QStringLiteral("Interim"),
        QStringLiteral("Notes"),
        QStringLiteral("Requires Annual Survey"),
        QStringLiteral("Requires Intermediate Survey"),
        QStringLiteral("Intermediate Mode"),
        QStringLiteral("Annual Surveys"),
        QStringLiteral("Intermediate Surveys"),
    };
    return names;
}

// §3: the three columns a file cannot be imported without.
const QStringList& requiredColumnNames()
{
    static const QStringList names = {
        QStringLiteral("Name"),
        QStringLiteral("Category"),
        QStringLiteral("Issue Date"),
    };
    return names;
}

QString normalised(const QString& headerCell)
{
    return headerCell.trimmed().toLower();
}

} // namespace

QStringList CertificateCsvFormat::headerRow()
{
    return columnNames();
}

QStringList CertificateCsvFormat::toRow(const Certificate&  certificate,
                                        const QString&      vesselName,
                                        const QString&      vesselImoNumber,
                                        const QList<QDate>& annualSurveys,
                                        const QList<QDate>& intermediateSurveys)
{
    return {
        certificate.id,
        vesselImoNumber,
        vesselName,
        certificate.listNumber,
        certificate.name,
        CertificateCodes::categoryToCode(certificate.category),
        certificate.appliesTo,
        certificate.certificateNumber,
        formatDate(certificate.issueDate),
        formatDate(certificate.expiryDate),
        certificate.issuedBy,
        certificate.placeOfIssue,
        formatBoolean(certificate.isInterim),
        certificate.notes,
        formatBoolean(certificate.requiresAnnualSurvey),
        formatBoolean(certificate.requiresIntermediateSurvey),
        CertificateCodes::intermediateModeToCode(certificate.intermediateMode),
        formatDateList(annualSurveys),
        formatDateList(intermediateSurveys),
    };
}

bool CertificateCsvFormat::mapHeader(const QStringList&   headerCells,
                                     QHash<QString, int>* columnIndex,
                                     QString*             error)
{
    columnIndex->clear();

    QHash<QString, QString> canonicalByNormalised;
    const QStringList&      names = columnNames();
    for (const QString& name : names) {
        canonicalByNormalised.insert(normalised(name), name);
    }

    for (int position = 0; position < headerCells.size(); ++position) {
        const QString cell = headerCells.at(position);
        if (cell.trimmed().isEmpty()) {
            continue;
        }

        const QString canonical = canonicalByNormalised.value(normalised(cell));
        if (canonical.isEmpty()) {
            *error = QStringLiteral(
                "The file has a column called \"%1\", which this import does not recognise.\n\n"
                "Check it for a typo: a misspelled heading would otherwise be ignored, and "
                "every value under it lost without warning.")
                         .arg(cell.trimmed());
            return false;
        }
        if (columnIndex->contains(canonical)) {
            *error =
                QStringLiteral("The file names the column \"%1\" more than once.").arg(canonical);
            return false;
        }
        columnIndex->insert(canonical, position);
    }

    const QStringList& required = requiredColumnNames();
    for (const QString& column : required) {
        if (!columnIndex->contains(column)) {
            *error = QStringLiteral("The file has no \"%1\" column, which every row needs.")
                         .arg(column);
            return false;
        }
    }

    return true;
}

bool CertificateCsvFormat::parseRow(const QStringList&         cells,
                                    const QHash<QString, int>& columnIndex,
                                    CertificateCsvRow*         row,
                                    QString*                   reason)
{
    *row = CertificateCsvRow();

    // A row shorter than the header is normal — a spreadsheet drops trailing
    // empty cells — so a column past the end reads as blank rather than as an
    // error.
    const auto cell = [&cells, &columnIndex](const QString& column) {
        const int position = columnIndex.value(column, -1);
        if (position < 0 || position >= cells.size()) {
            return QString();
        }
        return cells.at(position);
    };
    const auto trimmedCell = [&cell](const QString& column) { return cell(column).trimmed(); };

    row->fileId          = trimmedCell(QStringLiteral("Id"));
    row->vesselImoNumber = trimmedCell(QStringLiteral("Vessel IMO"));

    Certificate& certificate = row->certificate;

    // Free text is taken exactly as the file holds it, whitespace included, so
    // that an export and a re-import round-trip unchanged.
    certificate.name              = cell(QStringLiteral("Name"));
    certificate.appliesTo         = cell(QStringLiteral("Applies To"));
    certificate.certificateNumber = cell(QStringLiteral("Certificate Number"));
    certificate.issuedBy          = cell(QStringLiteral("Issued By"));
    certificate.placeOfIssue      = cell(QStringLiteral("Place of Issue"));
    certificate.notes             = cell(QStringLiteral("Notes"));
    certificate.listNumber        = trimmedCell(QStringLiteral("No."));

    const QString categoryCell = trimmedCell(QStringLiteral("Category"));
    if (!categoryCell.isEmpty()) {
        certificate.category = CertificateCodes::categoryFromCode(categoryCell.toUpper());
        if (certificate.category == CertificateCategory::Unset) {
            *reason = QStringLiteral("\"%1\" is not a known category.\n\n"
                                     "Use STATUTORY, CLASS, EQUIPMENT or OTHER.")
                          .arg(categoryCell);
            return false;
        }
    }

    const QString issueCell = trimmedCell(QStringLiteral("Issue Date"));
    if (!issueCell.isEmpty() && !parseDate(issueCell, &certificate.issueDate)) {
        *reason =
            QStringLiteral("\"%1\" is not a date in YYYY-MM-DD form (Issue Date).").arg(issueCell);
        return false;
    }

    const QString expiryCell = trimmedCell(QStringLiteral("Expiry Date"));
    if (!expiryCell.isEmpty() && !parseDate(expiryCell, &certificate.expiryDate)) {
        *reason = QStringLiteral("\"%1\" is not a date in YYYY-MM-DD form (Expiry Date).")
                      .arg(expiryCell);
        return false;
    }

    struct BooleanColumn {
        QString column;
        bool*   target;
    };
    const QList<BooleanColumn> booleans = {
        {QStringLiteral("Interim"), &certificate.isInterim},
        {QStringLiteral("Requires Annual Survey"), &certificate.requiresAnnualSurvey},
        {QStringLiteral("Requires Intermediate Survey"), &certificate.requiresIntermediateSurvey},
    };
    for (const BooleanColumn& boolean : booleans) {
        const QString value = trimmedCell(boolean.column);
        if (!parseBoolean(value, boolean.target)) {
            *reason = QStringLiteral("\"%1\" is not a yes or no value (%2).\n\n"
                                     "Use Yes, No, True, False, Y, N, 1, 0, or leave it blank "
                                     "for No.")
                          .arg(value, boolean.column);
            return false;
        }
    }

    // §3: ignored entirely when no intermediate survey is required, so a
    // stale value in that column cannot make an otherwise good row Invalid.
    const QString modeCell = trimmedCell(QStringLiteral("Intermediate Mode"));
    if (certificate.requiresIntermediateSurvey && !modeCell.isEmpty()) {
        const QString code = modeCell.toUpper();
        if (code != QLatin1String("ADDITIONAL") && code != QLatin1String("REPLACES_ANNUAL")) {
            *reason = QStringLiteral("\"%1\" is not a known intermediate mode.\n\n"
                                     "Use ADDITIONAL or REPLACES_ANNUAL, or leave it blank for "
                                     "ADDITIONAL.")
                          .arg(modeCell);
            return false;
        }
        certificate.intermediateMode = CertificateCodes::intermediateModeFromCode(code);
    }

    struct SurveyColumn {
        QString       column;
        SurveyType    type;
        bool          required;
        QList<QDate>* target;
    };
    const QList<SurveyColumn> surveys = {
        {QStringLiteral("Annual Surveys"), SurveyType::Annual, certificate.requiresAnnualSurvey,
         &row->annualSurveys},
        {QStringLiteral("Intermediate Surveys"), SurveyType::Intermediate,
         certificate.requiresIntermediateSurvey, &row->intermediateSurveys},
    };
    for (const SurveyColumn& survey : surveys) {
        if (!survey.required) {
            continue;
        }
        QString badEntry;
        if (!parseDateList(trimmedCell(survey.column), survey.target, &badEntry)) {
            *reason = QStringLiteral("\"%1\" is not a date in YYYY-MM-DD form (%2).")
                          .arg(badEntry, survey.column);
            return false;
        }
    }

    // The certificate's own shared rules, before the survey dates, so that a
    // row failing one of them reports that rather than a parsing message —
    // and so the preview and the write path agree.
    if (!validateCertificate(certificate, reason)) {
        return false;
    }

    // §6: every date in either list has to be one EndorsementRepository would
    // accept, checked here so the row shows as Invalid in the preview rather
    // than failing half-way through the write. The rule that matters is "not
    // before the certificate was issued", which needs the issue date parsed
    // above.
    for (const SurveyColumn& survey : surveys) {
        if (!survey.required) {
            continue;
        }
        for (const QDate& date : *survey.target) {
            Endorsement endorsement;
            endorsement.certificateId   = certificate.id;
            endorsement.surveyType      = survey.type;
            endorsement.endorsementDate = date;

            if (!validateEndorsement(endorsement, certificate.issueDate, reason)) {
                return false;
            }
        }
    }

    return true;
}
