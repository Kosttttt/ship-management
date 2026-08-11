#pragma once

#include <QMainWindow>

// The application shell. For now it is an empty frame; in later steps it gains
// the module navigation sidebar, the ship selector and the alert badges
// described in CLAUDE.md §8.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // databasePath is shown in the status bar purely as proof that step 2
    // worked. It goes away once there is a real screen to look at.
    explicit MainWindow(const QString& databasePath, QWidget* parent = nullptr);
};
