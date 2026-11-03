#pragma once

#include <QList>
#include <QString>
#include <QStringList>

class CertificateRepository;
class EndorsementRepository;
class VesselRepository;

// Writes one vessel's certificates out as a CSV file
// (certificate-import-export-spec §4).
//
// A read-only consumer of three repositories: it writes a file, never a row.
// Which vessel is never read from anything but its own argument — the caller
// passes the vessel already selected on screen (§7).
//
// Every value is a live read at the moment of export. Nothing is cached from
// a previous export or from whatever a file once said, which is what makes
// "record a survey, export again, see the new date" true (§9.17).
class CertificateCsvExporter
{
public:
    CertificateCsvExporter(CertificateRepository& certificates,
                           EndorsementRepository& endorsements,
                           VesselRepository&      vessels);

    // The header line followed by one line per certificate, as rows of plain
    // text. Public because it is the whole content of the file with none of
    // the filesystem attached, which is what a test wants to look at.
    bool buildRows(const QString& vesselId, QList<QStringList>* rows);

    bool exportToFile(const QString& vesselId, const QString& filePath);

    QString errorString() const;

private:
    CertificateRepository& m_certificates;
    EndorsementRepository& m_endorsements;
    VesselRepository&      m_vessels;
    QString                m_errorString;
};
