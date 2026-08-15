#pragma once

#include <QWidget>

struct Certificate;
struct CertificateState;

class AppSettingRepository;
class CertificateRepository;
class EndorsementRepository;
class InstallationContext;
class QCheckBox;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTableWidgetItem;

// The certificate list for one vessel (certificate-crud-spec §5).
//
// Unlike vessels, this screen is not split in two by installation mode. A
// vessel in VESSEL mode is inherently singular; its certificates are not — one
// ship carries dozens. So both modes use this same widget, and the only
// difference is whether MainWindow puts a vessel selector above it.
class CertificateListWidget : public QWidget
{
    Q_OBJECT

public:
    CertificateListWidget(CertificateRepository&     repository,
                          EndorsementRepository&     endorsements,
                          AppSettingRepository&      appSettings,
                          const InstallationContext& installation,
                          QWidget*                   parent = nullptr);

    // OFFICE: called by MainWindow when the toolbar selector changes. An empty
    // id returns the screen to the "select a vessel" prompt.
    // VESSEL: called once at construction and never again.
    void setVesselId(const QString& vesselId);

    QString vesselId() const;

    void reload();

private:
    void addCertificate();
    void editCertificate(QTableWidgetItem* item);
    void updateVisibleState();

    // Renders one already-computed row. The state is passed in rather than
    // recomputed so "today" is read exactly once per reload.
    void fillRow(int row, const Certificate& certificate, const CertificateState& state);

    CertificateRepository& m_repository;
    EndorsementRepository& m_endorsements;
    AppSettingRepository&  m_appSettings;
    QString                m_vesselId;

    QStackedWidget* m_stack               = nullptr;
    QTableWidget*   m_table               = nullptr;
    QPushButton*    m_addButton           = nullptr;
    QCheckBox*      m_needsAttentionCheck = nullptr;
};
