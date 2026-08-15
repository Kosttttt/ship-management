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

add_ship_test(tst_ModuleRegistry        app/tst_ModuleRegistry.cpp ShipApp)
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

add_ship_test(tst_CertificateStateExpiry
    modules/certificates/tst_CertificateStateExpiry.cpp ShipCertificates)
add_ship_test(tst_CertificateStateSurvey
    modules/certificates/tst_CertificateStateSurvey.cpp ShipCertificates)
add_ship_test(tst_EndorsementRepository
    modules/certificates/tst_EndorsementRepository.cpp ShipCertificates)
add_ship_test(tst_EndorsementUi
    modules/certificates/tst_EndorsementUi.cpp ShipCertificates)

# These run the real migrations, so they need them compiled in.
ship_add_migrations(tst_InstallationRepository)
ship_add_migrations(tst_VesselRepositoryScope)
ship_add_migrations(tst_VesselRepositoryWrites)
ship_add_migrations(tst_ModuleRegistry)
ship_add_migrations(tst_VesselSelectorRefresh)
ship_add_migrations(tst_CertificateRepositoryScope)
ship_add_migrations(tst_CertificateRepositoryValidation)
ship_add_migrations(tst_CertificateRepositoryWrites)
ship_add_migrations(tst_CertificateListWidget)
ship_add_migrations(tst_CertificateListStatus)
ship_add_migrations(tst_EndorsementRepository)
ship_add_migrations(tst_EndorsementUi)
