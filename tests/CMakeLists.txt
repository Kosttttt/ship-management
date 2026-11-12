# Unit tests. CLAUDE.md §8: this folder mirrors src/.

find_package(Qt6 REQUIRED COMPONENTS Test)

# Adds one Qt Test executable and registers it with CTest. Any extra arguments
# are treated as additional libraries to link (a widget test needs ShipApp).
function(add_ship_test target_name source_file)
    qt_add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE ShipCore Qt6::Test ${ARGN})

    # A test prints its results to the console, so it must never be a GUI app.
    set_target_properties(${target_name} PROPERTIES
        WIN32_EXECUTABLE FALSE
        MACOSX_BUNDLE    FALSE
    )

    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

add_ship_test(tst_SqlStatementSplitter   core/tst_SqlStatementSplitter.cpp)
add_ship_test(tst_MigrationRunner        core/tst_MigrationRunner.cpp)
add_ship_test(tst_ImoNumberValidator     core/tst_ImoNumberValidator.cpp)
add_ship_test(tst_InstallationRepository core/tst_InstallationRepository.cpp)
add_ship_test(tst_VesselRepositoryScope  core/tst_VesselRepositoryScope.cpp)
add_ship_test(tst_VesselRepositoryWrites core/tst_VesselRepositoryWrites.cpp)
add_ship_test(tst_VesselEditForm         app/tst_VesselEditForm.cpp ShipApp)
add_ship_test(tst_FirstRunRestore        app/tst_FirstRunRestore.cpp ShipApp)

add_ship_test(tst_AppSettingRepository  core/tst_AppSettingRepository.cpp)
add_ship_test(tst_ManagedFileStore      core/tst_ManagedFileStore.cpp)
add_ship_test(tst_SettingsPage          app/tst_SettingsPage.cpp ShipApp)
add_ship_test(tst_VesselAttachmentRoots core/tst_VesselAttachmentRoots.cpp)

# Step 15 — database backup & restore (database-backup-spec §7).
add_ship_test(tst_DatabaseBackup        core/tst_DatabaseBackup.cpp)
add_ship_test(tst_BackupFileInspector   core/tst_BackupFileInspector.cpp)
add_ship_test(tst_RestoreSwap           core/tst_RestoreSwap.cpp)
add_ship_test(tst_AutomaticBackup       core/tst_AutomaticBackup.cpp)
add_ship_test(tst_AttachmentFolderSettings app/tst_AttachmentFolderSettings.cpp ShipApp)

add_ship_test(tst_ModuleRegistry        app/tst_ModuleRegistry.cpp ShipApp)
add_ship_test(tst_Alerts                app/tst_Alerts.cpp ShipApp ShipCertificates)
add_ship_test(tst_VesselSelectorRefresh app/tst_VesselSelectorRefresh.cpp ShipApp)

add_ship_test(tst_CertificateListNumber
    modules/certificates/tst_CertificateListNumber.cpp ShipCertificates)

add_ship_test(tst_CertificateRepositoryScope
    modules/certificates/tst_CertificateRepositoryScope.cpp ShipCertificates)
add_ship_test(tst_CertificateRepositoryValidation
    modules/certificates/tst_CertificateRepositoryValidation.cpp ShipCertificates)
add_ship_test(tst_CertificateRepositoryWrites
    modules/certificates/tst_CertificateRepositoryWrites.cpp ShipCertificates)
add_ship_test(tst_CertificateEditForm
    modules/certificates/tst_CertificateEditForm.cpp ShipCertificates)
add_ship_test(tst_CertificateListWidget
    modules/certificates/tst_CertificateListWidget.cpp ShipCertificates)
add_ship_test(tst_CertificateListStatus
    modules/certificates/tst_CertificateListStatus.cpp ShipCertificates)
add_ship_test(tst_CertificateRenewal
    modules/certificates/tst_CertificateRenewal.cpp ShipCertificates)
add_ship_test(tst_CertificateExtension
    modules/certificates/tst_CertificateExtension.cpp ShipCertificates)
add_ship_test(tst_CertificateAttachment
    modules/certificates/tst_CertificateAttachment.cpp ShipCertificates)
add_ship_test(tst_CertificateAttachmentNaming
    modules/certificates/tst_CertificateAttachmentNaming.cpp ShipCertificates)
add_ship_test(tst_CertificateDeletion
    modules/certificates/tst_CertificateDeletion.cpp ShipCertificates)
add_ship_test(tst_EndorsementEditDelete
    modules/certificates/tst_EndorsementEditDelete.cpp ShipCertificates)
add_ship_test(tst_CertificateAttachmentFolders
    modules/certificates/tst_CertificateAttachmentFolders.cpp ShipCertificates)

add_ship_test(tst_AttachmentChooserFolder
    modules/certificates/tst_AttachmentChooserFolder.cpp ShipCertificates)

add_ship_test(tst_CertificateStateExpiry
    modules/certificates/tst_CertificateStateExpiry.cpp ShipCertificates)
add_ship_test(tst_CertificateStateSurvey
    modules/certificates/tst_CertificateStateSurvey.cpp ShipCertificates)
add_ship_test(tst_CertificateCategoryTabs
    modules/certificates/tst_CertificateCategoryTabs.cpp ShipCertificates)

add_ship_test(tst_CertificateListRows
    modules/certificates/tst_CertificateListRows.cpp ShipCertificates)

add_ship_test(tst_CertificateStateSurveyMatching
    modules/certificates/tst_CertificateStateSurveyMatching.cpp ShipCertificates)
add_ship_test(tst_EndorsementRepository
    modules/certificates/tst_EndorsementRepository.cpp ShipCertificates)
add_ship_test(tst_EndorsementUi
    modules/certificates/tst_EndorsementUi.cpp ShipCertificates)

# These run the real migrations, so they need them compiled in.
ship_add_migrations(tst_InstallationRepository)
ship_add_migrations(tst_FirstRunRestore)
ship_add_migrations(tst_VesselRepositoryScope)
ship_add_migrations(tst_VesselRepositoryWrites)
ship_add_migrations(tst_AppSettingRepository)
ship_add_migrations(tst_DatabaseBackup)
ship_add_migrations(tst_BackupFileInspector)
ship_add_migrations(tst_RestoreSwap)
ship_add_migrations(tst_AutomaticBackup)
ship_add_migrations(tst_SettingsPage)
ship_add_migrations(tst_ModuleRegistry)
ship_add_migrations(tst_Alerts)
ship_add_migrations(tst_VesselSelectorRefresh)
ship_add_migrations(tst_CertificateRepositoryScope)
ship_add_migrations(tst_CertificateRepositoryValidation)
ship_add_migrations(tst_CertificateRepositoryWrites)
ship_add_migrations(tst_CertificateListWidget)
ship_add_migrations(tst_CertificateListRows)
ship_add_migrations(tst_CertificateCategoryTabs)
ship_add_migrations(tst_CertificateListStatus)
ship_add_migrations(tst_CertificateRenewal)
ship_add_migrations(tst_CertificateExtension)
ship_add_migrations(tst_CertificateAttachment)
ship_add_migrations(tst_CertificateAttachmentFolders)
ship_add_migrations(tst_CertificateAttachmentNaming)
ship_add_migrations(tst_CertificateDeletion)
ship_add_migrations(tst_EndorsementEditDelete)
ship_add_migrations(tst_VesselAttachmentRoots)
ship_add_migrations(tst_AttachmentFolderSettings)
ship_add_migrations(tst_EndorsementRepository)
ship_add_migrations(tst_EndorsementUi)

# Step 13: CSV import/export and the PDF statement
# (certificate-import-export-spec §9).
add_ship_test(tst_CsvFile core/tst_CsvFile.cpp)
add_ship_test(tst_CertificateCsvFormat
    modules/certificates/tst_CertificateCsvFormat.cpp ShipCertificates)
add_ship_test(tst_CertificateCsvExport
    modules/certificates/tst_CertificateCsvExport.cpp ShipCertificates)
ship_add_migrations(tst_CertificateCsvExport)
add_ship_test(tst_CertificateCsvImport
    modules/certificates/tst_CertificateCsvImport.cpp ShipCertificates)
ship_add_migrations(tst_CertificateCsvImport)
add_ship_test(tst_CertificateCsvImportSurveys
    modules/certificates/tst_CertificateCsvImportSurveys.cpp ShipCertificates)
ship_add_migrations(tst_CertificateCsvImportSurveys)
add_ship_test(tst_CertificateReport
    modules/certificates/tst_CertificateReport.cpp ShipCertificates)
ship_add_migrations(tst_CertificateReport)
add_ship_test(tst_CertificateIoActions
    modules/certificates/tst_CertificateIoActions.cpp ShipCertificates)
ship_add_migrations(tst_CertificateIoActions)

# Certificate list usability fixes (certificate-list-usability-fixes.md).
add_ship_test(tst_CertificateListColumns
    modules/certificates/tst_CertificateListColumns.cpp ShipCertificates)
ship_add_migrations(tst_CertificateListColumns)
add_ship_test(tst_CertificateBulkDelete
    modules/certificates/tst_CertificateBulkDelete.cpp ShipCertificates)
ship_add_migrations(tst_CertificateBulkDelete)

# Document Management, step 1 — the library (document-management-spec.md §4.6).
add_ship_test(tst_DocumentScope
    modules/documents/tst_DocumentScope.cpp ShipDocuments)
add_ship_test(tst_DocumentVersions
    modules/documents/tst_DocumentVersions.cpp ShipDocuments)
add_ship_test(tst_DocumentFileStore
    modules/documents/tst_DocumentFileStore.cpp ShipDocuments)
add_ship_test(tst_DocumentListWidget
    modules/documents/tst_DocumentListWidget.cpp ShipApp ShipDocuments)
add_ship_test(tst_CertificateImportPlaceholder
    modules/certificates/tst_CertificateImportPlaceholder.cpp ShipCertificates)
ship_add_migrations(tst_CertificateImportPlaceholder)
ship_add_migrations(tst_DocumentScope)
ship_add_migrations(tst_DocumentVersions)
ship_add_migrations(tst_DocumentFileStore)
ship_add_migrations(tst_DocumentListWidget)
