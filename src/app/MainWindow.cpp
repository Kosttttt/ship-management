#include "app/MainWindow.h"

#include <QDir>
#include <QStatusBar>

MainWindow::MainWindow(const QString& databasePath, QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Ship Management System"));
    resize(1024, 700);

    statusBar()->showMessage(tr("Database: %1").arg(QDir::toNativeSeparators(databasePath)));
}
