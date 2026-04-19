#include <QApplication>
#include "TcpClient.h"


int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    TcpClient clinet;
    clinet.show();

    return a.exec();
}
