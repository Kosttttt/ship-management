#pragma once

#include <QMainWindow>

class InstallationContext;

// The application shell. For now it is an empty frame; in later steps it gains
// the module navigation sidebar, the ship selector and the alert badges
// described in CLAUDE.md §8.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const InstallationContext& installation, QWidget* parent = nullptr);
};
