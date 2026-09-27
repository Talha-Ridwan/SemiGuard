#include "DashboardWindow.hpp"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    DashboardWindow window;
    window.show();

    return QApplication::exec();
}
