# Ship Management System — top-level build description.
#
# Step 3 of the build order in CLAUDE.md §11: first-run wizard.
# Core lives in a static library so that the application and the unit tests can
# both link against exactly the same code.

cmake_minimum_required(VERSION 3.21)

project(ShipManagement
    VERSION 0.1.0
    DESCRIPTION "Modular ship management system"
    LANGUAGES CXX
)

# CLAUDE.md §2: the language is C++17, the same on every platform.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CLAUDE.md §2: Qt 6 Widgets for the GUI, Qt SQL for SQLite.
# PrintSupport is what QPrinter lives in — needed from step 13 for the PDF
# statement (certificate-import-export-spec §8). Not a third-party dependency:
# CLAUDE.md §2 names QTextDocument + QPrinter as this project's reporting stack.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql PrintSupport)

# Turns on Qt's code generators, most importantly the meta-object compiler
# (moc). Without it, any class using Q_OBJECT fails to link.
qt_standard_project_setup()

# ---------------------------------------------------------------------------
# Migrations.
#
# Listed once, here, and attached to every target that needs them. A migration
# missing from this list is compiled into nothing and silently never runs, so
# one list is much safer than one per target.
# ---------------------------------------------------------------------------
set(SHIP_MIGRATION_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/migrations/001_create_installation.sql
    ${CMAKE_CURRENT_SOURCE_DIR}/migrations/002_create_vessel.sql
    ${CMAKE_CURRENT_SOURCE_DIR}/migrations/003_create_certificate.sql
    ${CMAKE_CURRENT_SOURCE_DIR}/migrations/004_add_certificate_list_number.sql
    ${CMAKE_CURRENT_SOURCE_DIR}/migrations/005_create_endorsement.sql
    ${CMAKE_CURRENT_SOURCE_DIR}/migrations/006_create_app_setting.sql
    ${CMAKE_CURRENT_SOURCE_DIR}/migrations/007_create_certificate_extension.sql
    ${CMAKE_CURRENT_SOURCE_DIR}/migrations/008_create_certificate_attachment.sql
    ${CMAKE_CURRENT_SOURCE_DIR}/migrations/009_add_certificate_extension_attachment_path.sql
    ${CMAKE_CURRENT_SOURCE_DIR}/migrations/010_add_vessel_attachment_root_path.sql
    ${CMAKE_CURRENT_SOURCE_DIR}/migrations/011_widen_certificate_list_number_check.sql
    ${CMAKE_CURRENT_SOURCE_DIR}/migrations/012_create_document.sql
)

# Compiles the migrations into a target, reachable at ":/migrations/...".
# BASE strips the source directory so the resource keeps the "migrations/"
# folder name and nothing above it.
function(ship_add_migrations target)
    qt_add_resources(${target} "migrations_${target}"
        PREFIX "/"
        BASE ${CMAKE_SOURCE_DIR}
        FILES ${SHIP_MIGRATION_FILES}
    )
endfunction()

# ---------------------------------------------------------------------------
# ShipCore — infrastructure shared by the application and the tests.
# No widgets here: core must be usable without a window (CLAUDE.md §4).
# ---------------------------------------------------------------------------
qt_add_library(ShipCore STATIC
    src/core/Logger.h                 src/core/Logger.cpp
    src/core/Database.h               src/core/Database.cpp
    src/core/SqlStatementSplitter.h   src/core/SqlStatementSplitter.cpp
    src/core/MigrationRunner.h        src/core/MigrationRunner.cpp
    src/core/ImoNumberValidator.h     src/core/ImoNumberValidator.cpp
    src/core/InstallationContext.h    src/core/InstallationContext.cpp
    src/core/InstallationRepository.h src/core/InstallationRepository.cpp
    src/core/Vessel.h
    src/core/VesselRepository.h       src/core/VesselRepository.cpp
    src/core/VesselImoNumberCheck.h   src/core/VesselImoNumberCheck.cpp
    src/core/VesselArchiveWriter.h    src/core/VesselArchiveWriter.cpp
    src/core/AppSetting.h
    src/core/AppSettingRepository.h   src/core/AppSettingRepository.cpp
    src/core/CsvFile.h                src/core/CsvFile.cpp
    src/core/BackupSettings.h         src/core/BackupSettings.cpp
    src/core/BackupFileInspector.h    src/core/BackupFileInspector.cpp
    src/core/DatabaseBackup.h         src/core/DatabaseBackup.cpp
    src/core/AutomaticBackup.h        src/core/AutomaticBackup.cpp
    src/core/RestoreSwap.h            src/core/RestoreSwap.cpp
    src/core/DocumentsFolder.h        src/core/DocumentsFolder.cpp
    src/core/TransactionScope.h
    src/core/FileTree.h               src/core/FileTree.cpp
    src/core/ManagedFileStore.h       src/core/ManagedFileStore.cpp
    src/core/VesselAttachmentRoots.h  src/core/VesselAttachmentRoots.cpp
)

# PUBLIC: anything linking ShipCore also gets src/ as an include root, so both
# the app and the tests write #include "core/Database.h".
target_include_directories(ShipCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ShipCore PUBLIC Qt6::Core Qt6::Sql)

# ---------------------------------------------------------------------------
# ShipCertificates — the Certificate Control module.
#
# Its own library, mirroring the folder split in CLAUDE.md §8. It depends on
# ShipCore and on app/IModule.h, and nothing depends on it except the registry
# — which is what "adding a module requires zero changes to existing modules"
# has to mean in build terms.
# ---------------------------------------------------------------------------
qt_add_library(ShipCertificates STATIC
    src/modules/certificates/CertificatesModule.h
    src/modules/certificates/CertificatesModule.cpp
    src/modules/certificates/CertificateAlertProvider.h
    src/modules/certificates/CertificateAlertProvider.cpp
    src/modules/certificates/domain/Certificate.h
    src/modules/certificates/domain/Certificate.cpp
    src/modules/certificates/domain/Endorsement.h
    src/modules/certificates/domain/Endorsement.cpp
    src/modules/certificates/domain/CertificateAttachment.h
    src/modules/certificates/domain/AttachmentNaming.h
    src/modules/certificates/domain/AttachmentNaming.cpp
    src/modules/certificates/domain/CertificateExtension.h
    src/modules/certificates/domain/CertificateState.h
    src/modules/certificates/domain/CertificateState.cpp
    src/modules/certificates/domain/CertificateValidation.h
    src/modules/certificates/domain/CertificateValidation.cpp
    src/modules/certificates/domain/SurveySchedule.h
    src/modules/certificates/domain/SurveySchedule.cpp
    src/modules/certificates/data/CertificateRepository.h
    src/modules/certificates/data/CertificateRepository.cpp
    src/modules/certificates/data/EndorsementRepository.h
    src/modules/certificates/data/EndorsementRepository.cpp
    src/modules/certificates/data/CertificateExtensionRepository.h
    src/modules/certificates/data/CertificateExtensionRepository.cpp
    src/modules/certificates/data/CertificateAttachmentRepository.h
    src/modules/certificates/data/CertificateAttachmentRepository.cpp
    src/modules/certificates/data/CertificateAttachmentStorage.h
    src/modules/certificates/data/CertificateAttachmentStorage.cpp
    src/modules/certificates/service/CertificateIssueDatePlaceholder.h
    src/modules/certificates/service/CertificateIssueDatePlaceholder.cpp
    src/modules/certificates/service/CertificateCsvValues.h
    src/modules/certificates/service/CertificateCsvValues.cpp
    src/modules/certificates/service/CertificateCsvFormat.h
    src/modules/certificates/service/CertificateCsvFormat.cpp
    src/modules/certificates/service/CertificateCsvFileChecks.h
    src/modules/certificates/service/CertificateCsvFileChecks.cpp
    src/modules/certificates/service/CertificateCsvExporter.h
    src/modules/certificates/service/CertificateCsvExporter.cpp
    src/modules/certificates/service/CertificateCsvImporter.h
    src/modules/certificates/service/CertificateCsvImporter.cpp
    src/modules/certificates/service/CertificateReportBuilder.h
    src/modules/certificates/service/CertificateReportBuilder.cpp
    src/modules/certificates/service/CertificateListRows.h
    src/modules/certificates/service/CertificateListRows.cpp
    src/modules/certificates/service/CertificateBulkDeletion.h
    src/modules/certificates/service/CertificateBulkDeletion.cpp
    src/modules/certificates/service/CertificateAttachmentService.h
    src/modules/certificates/service/CertificateAttachmentService.cpp
    src/modules/certificates/service/CertificateDeletionService.h
    src/modules/certificates/service/CertificateDeletionService.cpp
    src/modules/certificates/ui/CertificateEditForm.h
    src/modules/certificates/ui/CertificateEditForm.cpp
    src/modules/certificates/ui/CertificateEditDialog.h
    src/modules/certificates/ui/CertificateEditDialog.cpp
    src/modules/certificates/ui/CertificateListWidget.h
    src/modules/certificates/ui/CertificateListWidget.cpp
    src/modules/certificates/ui/CertificateListRow.h
    src/modules/certificates/ui/CertificateRowRenderer.h  src/modules/certificates/ui/CertificateRowRenderer.cpp
    src/modules/certificates/ui/CertificateCategoryTabs.h src/modules/certificates/ui/CertificateCategoryTabs.cpp
    src/modules/certificates/ui/CertificateIoActions.h
    src/modules/certificates/ui/CertificateIoActions.cpp
    src/modules/certificates/ui/CertificateImportPreviewDialog.h
    src/modules/certificates/ui/CertificateImportPreviewDialog.cpp
    src/modules/certificates/ui/CertificateListActions.h
    src/modules/certificates/ui/CertificateListActions.cpp
    src/modules/certificates/ui/CertificateSelection.h
    src/modules/certificates/ui/CertificateSelection.cpp
    src/modules/certificates/ui/CertificateDeleteActions.h
    src/modules/certificates/ui/CertificateDeleteActions.cpp
    src/modules/certificates/ui/CertificateAttachmentActions.h
    src/modules/certificates/ui/CertificateAttachmentActions.cpp
    src/modules/certificates/ui/ListNumberItem.h
    src/modules/certificates/ui/ListNumberItem.cpp
    src/modules/certificates/ui/DateItem.h
    src/modules/certificates/ui/DateItem.cpp
    src/modules/certificates/ui/TableHeaderSettings.h
    src/modules/certificates/ui/TableHeaderSettings.cpp
    src/modules/certificates/ui/StatusItem.h
    src/modules/certificates/ui/StatusItem.cpp
    src/modules/certificates/ui/EndorsementEditForm.h
    src/modules/certificates/ui/EndorsementEditForm.cpp
    src/modules/certificates/ui/EndorsementEditDialog.h
    src/modules/certificates/ui/EndorsementEditDialog.cpp
    src/modules/certificates/ui/EndorsementSection.h
    src/modules/certificates/ui/EndorsementSection.cpp
    src/modules/certificates/ui/CertificateExtensionDialog.h
    src/modules/certificates/ui/CertificateExtensionDialog.cpp
    src/modules/certificates/ui/AttachmentChooser.h
    src/modules/certificates/ui/AttachmentChooser.cpp
    src/modules/certificates/ui/AttachmentSourceField.h
    src/modules/certificates/ui/AttachmentSourceField.cpp
)

target_link_libraries(ShipCertificates PUBLIC ShipCore Qt6::Widgets Qt6::PrintSupport)

# ---------------------------------------------------------------------------
# ShipDocuments — the Document Management module.
#
# Its own library, like ShipCertificates and for the same reason. It links
# ShipCore and nothing else: a module may never reach into another module's
# folders (CLAUDE.md §5), and the build says so rather than trusting everyone
# to remember. An #include of anything under modules/certificates/ from here
# fails to compile, which is the point.
# ---------------------------------------------------------------------------
qt_add_library(ShipDocuments STATIC
    src/modules/documents/DocumentsModule.h
    src/modules/documents/DocumentsModule.cpp
    src/modules/documents/domain/Document.h
    src/modules/documents/domain/Document.cpp
    src/modules/documents/data/DocumentAccessRules.h
    src/modules/documents/data/DocumentAccessRules.cpp
    src/modules/documents/data/DocumentVersionStore.h
    src/modules/documents/data/DocumentVersionStore.cpp
    src/modules/documents/data/DocumentRepository.h
    src/modules/documents/data/DocumentRepository.cpp
    src/modules/documents/service/DocumentFileStore.h
    src/modules/documents/service/DocumentFileStore.cpp
    src/modules/documents/ui/DocumentCategoryTabs.h
    src/modules/documents/ui/DocumentCategoryTabs.cpp
    src/modules/documents/ui/DocumentVersionFields.h
    src/modules/documents/ui/DocumentVersionFields.cpp
    src/modules/documents/ui/DocumentEditDialog.h
    src/modules/documents/ui/DocumentEditDialog.cpp
    src/modules/documents/ui/DocumentVersionDialog.h
    src/modules/documents/ui/DocumentVersionDialog.cpp
    src/modules/documents/ui/DocumentVersionsDialog.h
    src/modules/documents/ui/DocumentVersionsDialog.cpp
    src/modules/documents/ui/DocumentActions.h
    src/modules/documents/ui/DocumentActions.cpp
    src/modules/documents/ui/DocumentListWidget.h
    src/modules/documents/ui/DocumentListWidget.cpp
)

target_link_libraries(ShipDocuments PUBLIC ShipCore Qt6::Widgets)

# ---------------------------------------------------------------------------
# ShipApp — the shell: MainWindow, the module registry, the wizard, vessels.
#
# A library rather than sources compiled straight into the executable, so the
# widget tests can link the very same code the application runs.
# ---------------------------------------------------------------------------
qt_add_library(ShipApp STATIC
    src/app/IModule.h
    src/app/Migration.h
    src/app/AlertProvider.h
    src/app/AlertBanner.h            src/app/AlertBanner.cpp
    src/app/ModuleRegistry.h        src/app/ModuleRegistry.cpp
    src/app/MainWindow.h           src/app/MainWindow.cpp
    src/app/FirstRunWizard.h       src/app/FirstRunWizard.cpp
    src/app/InstallationModePage.h src/app/InstallationModePage.cpp
    src/app/VesselIdentityPage.h   src/app/VesselIdentityPage.cpp
    src/app/ImoNumberMessages.h    src/app/ImoNumberMessages.cpp
    src/app/VesselEditForm.h       src/app/VesselEditForm.cpp
    src/app/VesselEditDialog.h     src/app/VesselEditDialog.cpp
    src/app/VesselListWidget.h     src/app/VesselListWidget.cpp
    src/app/VesselArchiveActions.h src/app/VesselArchiveActions.cpp
    src/app/DatabaseBackupSettings.h src/app/DatabaseBackupSettings.cpp
    src/app/RestoreConfirmationDialog.h src/app/RestoreConfirmationDialog.cpp
    src/app/RestoreFromBackup.h src/app/RestoreFromBackup.cpp
    src/app/DocumentsFolderSettings.h src/app/DocumentsFolderSettings.cpp
    src/app/VesselDetailPage.h     src/app/VesselDetailPage.cpp
    src/app/SettingsPage.h         src/app/SettingsPage.cpp
    src/app/AttachmentFolderSettings.h src/app/AttachmentFolderSettings.cpp
)

target_link_libraries(ShipApp PUBLIC ShipCore ShipCertificates ShipDocuments Qt6::Widgets)

# ---------------------------------------------------------------------------
# The application.
# ---------------------------------------------------------------------------
qt_add_executable(ShipManagement src/main.cpp)

target_link_libraries(ShipManagement PRIVATE ShipApp)
ship_add_migrations(ShipManagement)

# The console window shows diagnostic output in a Debug build. A Release
# build hides it automatically — gated by config so nobody has to remember
# to flip this by hand before shipping.
set_target_properties(ShipManagement PROPERTIES
    WIN32_EXECUTABLE $<CONFIG:Release>
    MACOSX_BUNDLE    $<CONFIG:Release>
)

# ---------------------------------------------------------------------------
# Tests.
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
