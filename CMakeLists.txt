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
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql)

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
)

# PUBLIC: anything linking ShipCore also gets src/ as an include root, so both
# the app and the tests write #include "core/Database.h".
target_include_directories(ShipCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ShipCore PUBLIC Qt6::Core Qt6::Sql)

# ---------------------------------------------------------------------------
# The application.
# ---------------------------------------------------------------------------
qt_add_executable(ShipManagement
    src/main.cpp
    src/app/MainWindow.h           src/app/MainWindow.cpp
    src/app/FirstRunWizard.h       src/app/FirstRunWizard.cpp
    src/app/InstallationModePage.h src/app/InstallationModePage.cpp
    src/app/VesselIdentityPage.h   src/app/VesselIdentityPage.cpp
)

target_link_libraries(ShipManagement PRIVATE ShipCore Qt6::Widgets)
ship_add_migrations(ShipManagement)

# While developing, the console window stays open next to the application so
# diagnostic output is visible. Before shipping a release, set WIN32_EXECUTABLE
# and MACOSX_BUNDLE to TRUE here to hide it.
set_target_properties(ShipManagement PROPERTIES
    WIN32_EXECUTABLE FALSE
    MACOSX_BUNDLE    FALSE
)

# ---------------------------------------------------------------------------
# Tests.
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
