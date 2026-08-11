# Unit tests. CLAUDE.md §8: this folder mirrors src/.

find_package(Qt6 REQUIRED COMPONENTS Test)

# Adds one Qt Test executable and registers it with CTest.
function(add_ship_test target_name source_file)
    qt_add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE ShipCore Qt6::Test)

    # A test prints its results to the console, so it must never be a GUI app.
    set_target_properties(${target_name} PROPERTIES
        WIN32_EXECUTABLE FALSE
        MACOSX_BUNDLE    FALSE
    )

    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

add_ship_test(tst_SqlStatementSplitter core/tst_SqlStatementSplitter.cpp)
add_ship_test(tst_MigrationRunner      core/tst_MigrationRunner.cpp)
add_ship_test(tst_ImoNumberValidator   core/tst_ImoNumberValidator.cpp)
add_ship_test(tst_InstallationRepository core/tst_InstallationRepository.cpp)

# This one runs the real migrations, so it needs them compiled in.
ship_add_migrations(tst_InstallationRepository)
