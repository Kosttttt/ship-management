#pragma once

#include <QWidget>

class CertificateRepository;
class InstallationContext;
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

    CertificateRepository& m_repository;
    QString                m_vesselId;

    QStackedWidget* m_stack      = nullptr;
    QTableWidget*   m_table      = nullptr;
    QPushButton*    m_addButton  = nullptr;
};
