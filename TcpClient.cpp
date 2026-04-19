#include "TcpClient.h"

#include <QTextEdit>
#include <QPushButton>
#include <QString>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>

TcpClient::TcpClient(QWidget *parent)
    : QWidget{parent}
{
    setWindowTitle("客户端");
    setFixedSize(400,300);

    m_worker=new TcpWorker;
    m_workThread=new QThread(this);
    m_worker->moveToThread(m_workThread);
    m_workThread->start();

    m_read=new QTextEdit;
    m_read->setPlaceholderText("消息接收区");

    m_send=new QTextEdit;
    m_send->setPlaceholderText("输入消息");

    m_btnSendMes=new QPushButton("发送消息");
    m_btnSendFile=new QPushButton("发送文件");

    m_onConnection=new QPushButton("连接服务器");
    m_onDisconnected=new QPushButton("断开连接");

    m_mainlay=new QVBoxLayout;
    QHBoxLayout *serverlay=new QHBoxLayout;
    QHBoxLayout *sendlay=new QHBoxLayout;

    serverlay->addWidget(m_onConnection);
    serverlay->addWidget(m_onDisconnected);

    sendlay->addWidget(m_send);
    sendlay->addWidget(m_btnSendMes);
    sendlay->addWidget(m_btnSendFile);

    m_mainlay->addWidget(m_read);
    m_mainlay->addLayout(sendlay);
    m_mainlay->addLayout(serverlay);

    setLayout(m_mainlay);

    connect(m_onConnection,&QPushButton::clicked,this,&TcpClient::onConnection);
    connect(m_onDisconnected,&QPushButton::clicked,this,&TcpClient::onDisconnected);
    connect(m_btnSendMes,&QPushButton::clicked,this,&TcpClient::onSendMes);
    connect(m_btnSendFile,&QPushButton::clicked,this,&TcpClient::onSendFile);
    connect(m_worker,&TcpWorker::sigMessage,this,&TcpClient::onReadMes);
}

void TcpClient::onSendFile()
{
    QString filePath=QFileDialog::getOpenFileName(this);
    if(filePath.isEmpty()) return;

    QMetaObject::invokeMethod(m_worker,"sendFile",Q_ARG(QString,filePath));
}

void TcpClient::onSendMes()
{
    if(m_send->toPlainText().isEmpty()) return;
    QString mes=m_send->toPlainText();
    m_read->append("[客户端]"+mes);
    m_send->clear();
    QMetaObject::invokeMethod(m_worker,"sendMessage",Q_ARG(QString,mes));
}

void TcpClient::onReadMes(const QString &mes)
{
    m_read->append("[服务端]"+mes);
}

void TcpClient::onDisconnected()
{
    QMetaObject::invokeMethod(m_worker,"disconnectFromServer");
}

void TcpClient::onConnection()
{
    QString ip="127.0.0.1";
    int port=9999;

    QMetaObject::invokeMethod(m_worker,"connectToServer",Q_ARG(QString,ip),Q_ARG(int,port));
}

TcpClient::~TcpClient()
{
    m_workThread->quit();
    m_workThread->wait();
}
