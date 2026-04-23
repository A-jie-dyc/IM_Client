#ifndef TCPCLIENT_H
#define TCPCLIENT_H

#include "TcpWorker.h"
#include <QMainWindow>
#include <QThread>
#include <QCheckBox>
#include <QListWidget>
#include <QSplitter>
#include <QStatusBar>
#include <QDialog>

class QTextEdit;
class QPushButton;
class QVBoxLayout;
class QProgressBar;
class QLabel;

class TcpClient : public QMainWindow
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
    void onShowMes(const QString &mes);
    void onSendProgress(const quint64 &sent,const quint64 &total);
    void onRecvProgress(const quint64 &sent,const quint64 &total);

private:
    void initWindow();
    void initLogsWindow();
    void setInfo(Information info,const QString &text);
    void setList(const QString &ip,const int &port);
    void appendLog(const QString &log);
    //工具栏
    QLineEdit *m_editIP;
    QLineEdit *m_editPort;
    QPushButton *m_onConnection;
    QPushButton *m_onDisconnected;
    //中间
    QListWidget *m_deviceList;
    QTextEdit *m_chatShow;
    QTextEdit *m_chatInput;
    QPushButton *m_btnSendMes;
    QPushButton *m_btnSendFile;
    QProgressBar *m_sendProgress;
    QProgressBar *m_recvProgress;
    //底部
    QPushButton *m_btnViewLogs;
    QLabel *m_statusLabel;
    //日志
    QDialog *m_logsWindow;
    QTextEdit *m_logText;

    QThread *m_workThread;       //工作线程
    TcpWorker *m_worker;        //工作对象
};

#endif // TCPCLIENT_H
