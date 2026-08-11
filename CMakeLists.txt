# Ship Management System — top-level build description.
#
# Step 2 of the build order in CLAUDE.md §11: core infrastructure.
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
# ShipCore — infrastructure shared by the application and the tests.
# No widgets here: core must be usable without a window (CLAUDE.md §4).
# ---------------------------------------------------------------------------
qt_add_library(ShipCore STATIC
    src/core/Logger.h           src/core/Logger.cpp
    src/core/Database.h         src/core/Database.cpp
    src/core/MigrationRunner.h  src/core/MigrationRunner.cpp
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
    src/app/MainWindow.h
    src/app/MainWindow.cpp
)

target_link_libraries(ShipManagement PRIVATE ShipCore Qt6::Widgets)

# Compiles the migration files into the executable, reachable at
# ":/migrations/...". An installed copy can then never lose them.
qt_add_resources(ShipManagement "migrations"
    PREFIX "/"
    FILES
        migrations/001_create_installation.sql
)

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
