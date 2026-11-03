#include "modules/certificates/service/CertificateCsvExporter.h"

#include "core/CsvFile.h"
#include "core/VesselRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/domain/Endorsement.h"
#include "modules/certificates/service/CertificateCsvFormat.h"

#include <algorithm>

namespace {

// Every non-deleted endorsement of one type, in date order (§4). There is no
// "most recent" accessor on the repository and this does not need one: §6
// needs the whole history, because the matching walk in
// SurveySchedule::evaluateAnnual() accounts for one past anniversary per past
// survey.
QList<QDate> datesOfType(const QList<Endorsement>& endorsements, SurveyType type)
{
    QList<QDate> dates;
    for (const Endorsement& endorsement : endorsements) {
        if (endorsement.surveyType == type) {
            dates.append(endorsement.endorsementDate);
        }
    }
    std::sort(dates.begin(), dates.end());
    return dates;
}

} // namespace

CertificateCsvExporter::CertificateCsvExporter(CertificateRepository& certificates,
                                               EndorsementRepository& endorsements,
                                               VesselRepository&      vessels)
    : m_certificates(certificates)
    , m_endorsements(endorsements)
    , m_vessels(vessels)
{
}

bool CertificateCsvExporter::buildRows(const QString& vesselId, QList<QStringList>* rows)
{
    m_errorString.clear();
    rows->clear();

    std::optional<Vessel> vessel;
    if (!m_vessels.findById(vesselId, &vessel)) {
        m_errorString = m_vessels.errorString();
        return false;
    }
    if (!vessel.has_value()) {
        m_errorString = QStringLiteral("That vessel could not be found.");
        return false;
    }

    QList<Certificate> certificates;
    if (!m_certificates.list(vesselId, &certificates)) {
        m_errorString = m_certificates.errorString();
        return false;
    }

    // §4: list() orders by name, while the screen a user is looking at is
    // ordered by reference number. The file follows the screen, so the sort
    // happens here, with the same domain comparison the table's own
    // ListNumberItem hands its cells to. Stable, so certificates sharing a
    // number keep the name order list() returned them in.
    std::stable_sort(certificates.begin(), certificates.end(),
                     [](const Certificate& left, const Certificate& right) {
                         return CertificateListNumber::lessThan(left.listNumber,
                                                                right.listNumber);
                     });

    rows->append(CertificateCsvFormat::headerRow());

    for (const Certificate& certificate : certificates) {
        QList<Endorsement> endorsements;
        if (!m_endorsements.list(certificate.id, &endorsements)) {
            m_errorString = m_endorsements.errorString();
            return false;
        }

        rows->append(CertificateCsvFormat::toRow(certificate, vessel->name, vessel->imoNumber,
                                                 datesOfType(endorsements, SurveyType::Annual),
                                                 datesOfType(endorsements,
                                                             SurveyType::Intermediate)));
    }

    return true;
}

bool CertificateCsvExporter::exportToFile(const QString& vesselId, const QString& filePath)
{
    QList<QStringList> rows;
    if (!buildRows(vesselId, &rows)) {
        return false;
    }
    return CsvFile::write(filePath, rows, &m_errorString);
}

QString CertificateCsvExporter::errorString() const
{
    return m_errorString;
}
