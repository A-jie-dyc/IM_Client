#ifndef TCPCLIENT_H
#define TCPCLIENT_H

#include <QWidget>
#include <QThread>
#include "TcpWorker.h"

class QTextEdit;
class QPushButton;
class QVBoxLayout;
class QProgressBar;

class TcpClient : public QWidget
{
    Q_OBJECT
public:
    explicit TcpClient(QWidget *parent = nullptr);
    ~TcpClient() override;

private slots:
    void onConnection();
    void onDisconnected();
    void onSendMes();
    void onSendFile();
    void onReadMes(const QString &mes);

private:
    QTextEdit *m_read;
    QTextEdit *m_send;
    QPushButton *m_btnSendMes;
    QPushButton *m_btnSendFile;
    QPushButton *m_onConnection;
    QPushButton *m_onDisconnected;
    QVBoxLayout *m_mainlay;
    QProgressBar *m_progress;

    QThread *m_workThread;       //工作线程
    TcpWorker *m_worker;        //工作对象
};

#endif // TCPCLIENT_H
