#include <QApplication>
#include "TcpClient.h"


int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    TcpClient client;
    client.show();

    return a.exec();
}
