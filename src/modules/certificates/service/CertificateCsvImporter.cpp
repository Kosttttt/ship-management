#include "modules/certificates/service/CertificateCsvImporter.h"

#include "core/CsvFile.h"
#include "core/TransactionScope.h"
#include "core/VesselRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/domain/Endorsement.h"
#include "modules/certificates/service/CertificateCsvFileChecks.h"

#include <QHash>
#include <QSet>

namespace {

Endorsement endorsementFor(const QString& certificateId, SurveyType type, const QDate& date)
{
    Endorsement endorsement;
    endorsement.certificateId   = certificateId;
    endorsement.surveyType      = type;
    endorsement.endorsementDate = date;
    return endorsement;
}

} // namespace

bool CertificateImportPreview::hasInvalidRows() const
{
    for (const CertificateImportRow& row : rows) {
        if (row.outcome == ImportRowOutcome::Invalid) {
            return true;
        }
    }
    return false;
}

int CertificateImportPreview::countOf(ImportRowOutcome outcome) const
{
    int count = 0;
    for (const CertificateImportRow& row : rows) {
        if (row.outcome == outcome) {
            ++count;
        }
    }
    return count;
}

CertificateCsvImporter::CertificateCsvImporter(QSqlDatabase&          database,
                                               CertificateRepository& certificates,
                                               EndorsementRepository& endorsements,
                                               VesselRepository&      vessels)
    : m_database(database)
    , m_certificates(certificates)
    , m_endorsements(endorsements)
    , m_vessels(vessels)
{
}

bool CertificateCsvImporter::buildPreview(const QString&            vesselId,
                                          const QString&            filePath,
                                          CertificateImportPreview* preview)
{
    m_errorString.clear();
    preview->rows.clear();

    std::optional<Vessel> vessel;
    if (!m_vessels.findById(vesselId, &vessel)) {
        m_errorString = m_vessels.errorString();
        return false;
    }
    if (!vessel.has_value()) {
        m_errorString = QStringLiteral("That vessel could not be found.");
        return false;
    }

    QList<QStringList> fileRows;
    if (!CsvFile::read(filePath, &fileRows, &m_errorString)) {
        return false;
    }
    if (fileRows.isEmpty()) {
        m_errorString = QStringLiteral("That file is empty — there is not even a header row.");
        return false;
    }

    QHash<QString, int> columnIndex;
    if (!CertificateCsvFormat::mapHeader(fileRows.first(), &columnIndex, &m_errorString)) {
        return false;
    }

    const QList<QStringList> dataRows = fileRows.mid(1);

    // The whole-file questions first (§7, §9.6), before any row is parsed, so
    // an Invalid row cannot hide the fact that this is the wrong file.
    if (!CertificateCsvFileChecks::vesselMatches(dataRows, columnIndex, *vessel, &m_errorString)
        || !CertificateCsvFileChecks::idsAreUnique(dataRows, columnIndex, &m_errorString)) {
        return false;
    }

    // §5.1: the scoped list, read once, is the only thing an Id is matched
    // against — never findById(), which applies no vessel filter in OFFICE
    // mode and would hand back another vessel's certificate.
    QList<Certificate> existing;
    if (!m_certificates.list(vesselId, &existing)) {
        m_errorString = m_certificates.errorString();
        return false;
    }
    QSet<QString> liveIds;
    for (const Certificate& certificate : existing) {
        liveIds.insert(certificate.id);
    }

    // Read once as well, and only to tell "never here" from "here and
    // deleted" (§5.1).
    QSet<QString> deletedIds;
    if (!m_certificates.listDeletedIds(vesselId, &deletedIds)) {
        m_errorString = m_certificates.errorString();
        return false;
    }

    // Both kept on the preview so the placeholder checkbox can re-evaluate
    // every row in the open dialog without re-reading the file
    // (certificate-list-usability-fixes.md §3).
    preview->vesselId    = vesselId;
    preview->columnIndex = columnIndex;

    for (int index = 0; index < dataRows.size(); ++index) {
        CertificateImportRow row;
        row.rowNumber = index + 1;
        row.cells     = dataRows.at(index);

        const bool valid =
            CertificateCsvFormat::parseRow(row.cells, columnIndex, &row.parsed, &row.reason);

        // Worked out whether the row parsed or not: parseRow() fills fileId
        // before anything in it can fail, so even a rejected row knows which
        // certificate it claimed to be — which is what lets the placeholder
        // option put a rescued row straight into its real outcome (§3).
        if (row.parsed.fileId.isEmpty()) {
            row.outcomeIfValid = ImportRowOutcome::New;
        } else if (liveIds.contains(row.parsed.fileId)) {
            row.outcomeIfValid = ImportRowOutcome::Match;
            row.matchedId      = row.parsed.fileId;
        } else if (deletedIds.contains(row.parsed.fileId)) {
            row.outcomeIfValid = ImportRowOutcome::NewPreviouslyDeleted;
        } else {
            row.outcomeIfValid = ImportRowOutcome::New;
        }

        if (!valid) {
            row.outcome = ImportRowOutcome::Invalid;
            preview->rows.append(row);
            continue;
        }

        // The vessel comes from the selection, always, so that nothing in the
        // file can decide what a row belongs to (§7).
        row.parsed.certificate.vesselId = vesselId;
        row.outcome                     = row.outcomeIfValid;

        preview->rows.append(row);
    }

    return true;
}

bool CertificateCsvImporter::createRow(const CertificateImportRow& row)
{
    // §5.1: create() mints its own UUID. Whatever the file's Id column said
    // is deliberately not carried over — it named a certificate that is not
    // here any more, or was never here.
    Certificate certificate = row.parsed.certificate;
    certificate.id.clear();

    QString newId;
    if (!m_certificates.create(certificate, &newId)) {
        m_errorString =
            QStringLiteral("Row %1: %2").arg(row.rowNumber).arg(m_certificates.errorString());
        return false;
    }

    // §6: one endorsement per date, exactly as live data entry would record
    // them, so SurveySchedule's existing anniversary matching has one past
    // survey available per past anniversary. No special case for
    // REPLACES_ANNUAL — evaluateAnnual() already merges an intermediate date
    // falling inside an annual window, and it does so for these rows for the
    // same reason it does for any other.
    struct Track {
        SurveyType          type;
        const QList<QDate>& dates;
    };
    const Track tracks[] = {
        {SurveyType::Annual, row.parsed.annualSurveys},
        {SurveyType::Intermediate, row.parsed.intermediateSurveys},
    };

    for (const Track& track : tracks) {
        for (const QDate& date : track.dates) {
            if (!m_endorsements.create(endorsementFor(newId, track.type, date))) {
                m_errorString = QStringLiteral("Row %1: %2")
                                    .arg(row.rowNumber)
                                    .arg(m_endorsements.errorString());
                return false;
            }
        }
    }

    return true;
}

bool CertificateCsvImporter::updateRow(const CertificateImportRow& row)
{
    // §6: an overwrite touches the certificate's own fields and nothing else.
    // The survey columns were read and shown in the preview, but writing them
    // here would duplicate a survey every time the same file was imported
    // twice, and would give "correct a recorded survey" two places to happen
    // instead of one.
    Certificate certificate = row.parsed.certificate;
    certificate.id          = row.matchedId;

    if (!m_certificates.update(certificate)) {
        m_errorString =
            QStringLiteral("Row %1: %2").arg(row.rowNumber).arg(m_certificates.errorString());
        return false;
    }
    return true;
}

bool CertificateCsvImporter::runImport(const QString&                  vesselId,
                                       const CertificateImportPreview& preview,
                                       int*                            createdCount,
                                       int*                            updatedCount)
{
    m_errorString.clear();
    if (createdCount != nullptr) {
        *createdCount = 0;
    }
    if (updatedCount != nullptr) {
        *updatedCount = 0;
    }

    if (preview.hasInvalidRows()) {
        m_errorString =
            QStringLiteral("The file still has rows that cannot be imported. Nothing was "
                           "written — correct the file and open it again.");
        return false;
    }

    // §5.2: one transaction around the whole loop, rolled back by its own
    // destructor on every early return below.
    TransactionScope transaction(m_database);
    if (!transaction.isOpen()) {
        m_errorString =
            QStringLiteral("Could not start a database transaction, so nothing was imported.");
        return false;
    }

    int created = 0;
    int updated = 0;

    for (const CertificateImportRow& preparedRow : preview.rows) {
        // The vessel is re-applied here as well as in the preview, so that a
        // preview assembled anywhere else still cannot aim a write at another
        // ship.
        CertificateImportRow row            = preparedRow;
        row.parsed.certificate.vesselId     = vesselId;

        switch (row.outcome) {
        case ImportRowOutcome::New:
        case ImportRowOutcome::NewPreviouslyDeleted:
            if (!createRow(row)) {
                return false;
            }
            ++created;
            break;

        case ImportRowOutcome::Match:
            if (!row.overwrite) {
                break; // skip is the default, and skipping writes nothing
            }
            if (!updateRow(row)) {
                return false;
            }
            ++updated;
            break;

        case ImportRowOutcome::Invalid:
            // Unreachable: hasInvalidRows() refused above. Handled anyway so
            // the switch stays exhaustive and the compiler keeps checking it.
            m_errorString = QStringLiteral("Row %1 cannot be imported.").arg(row.rowNumber);
            return false;
        }
    }

    if (!transaction.commit()) {
        m_errorString = QStringLiteral("The import could not be saved, so nothing was changed.");
        return false;
    }

    if (createdCount != nullptr) {
        *createdCount = created;
    }
    if (updatedCount != nullptr) {
        *updatedCount = updated;
    }
    return true;
}

QString CertificateCsvImporter::errorString() const
{
    return m_errorString;
}
