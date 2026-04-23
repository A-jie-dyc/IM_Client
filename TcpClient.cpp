#include "TcpClient.h"

#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressBar>
#include <QToolBar>
#include <QWidget>
#include <QLabel>
#include <QDateTime>

TcpClient::TcpClient(QWidget *parent)
    : QMainWindow{parent}
{

    setWindowTitle("IM客户端");
    setFixedSize(800,600);

    initWindow();
    initLogsWindow();

    m_worker=new TcpWorker;
    m_workThread=new QThread(this);
    m_worker->moveToThread(m_workThread);
    connect(m_workThread,&QThread::started,m_worker,&TcpWorker::Init);
    m_workThread->start();

    connect(m_onConnection,&QPushButton::clicked,this,&TcpClient::onConnection);
    connect(m_onDisconnected,&QPushButton::clicked,this,&TcpClient::onDisconnected);
    connect(m_btnSendMes,&QPushButton::clicked,this,&TcpClient::onSendMes);
    connect(m_btnSendFile,&QPushButton::clicked,this,&TcpClient::onSendFile);
    connect(m_btnViewLogs, &QPushButton::clicked, this,[this](){
        m_logsWindow->show();
        m_logsWindow->raise();
    });
    connect(m_worker,&TcpWorker::sigMessage,this,&TcpClient::onShowMes);
    connect(m_worker,&TcpWorker::sigSendProgress,this,&TcpClient::onSendProgress);
    connect(m_worker,&TcpWorker::sigRecvProgress,this,&TcpClient::onRecvProgress);
    connect(m_worker,&TcpWorker::sigInformation,this,&TcpClient::setInfo);
    connect(m_worker,&TcpWorker::sigEquipment,this,&TcpClient::setList);
}

void TcpClient::initWindow()
{
    //顶部
    QToolBar *toolBar=addToolBar("连接工具栏");
    toolBar->setMovable(false);
    m_onConnection=new QPushButton("连接服务器");
    m_onDisconnected=new QPushButton("断开服务器");

    m_editIP=new QLineEdit;
    m_editIP->setPlaceholderText("IP地址:");
    m_editIP->setText("127.0.0.1");

    m_editPort=new QLineEdit;
    m_editPort->setPlaceholderText("端口:");
    m_editPort->setText("9999");
    toolBar->addWidget(m_editIP);
    toolBar->addWidget(m_editPort);
    toolBar->addSeparator();
    toolBar->addWidget(m_onConnection);
    toolBar->addWidget(m_onDisconnected);

    //中间
    QSplitter *splitter=new QSplitter(Qt::Horizontal);
    //左侧
    m_deviceList=new QListWidget;
    m_deviceList->setFixedWidth(200);
    splitter->addWidget(m_deviceList);
    //右侧
    QWidget *chatWidget=new QWidget;
    QVBoxLayout *chatLayout=new QVBoxLayout(chatWidget);
    m_chatShow=new QTextEdit;
    m_chatShow->setReadOnly(true);

    m_sendProgress=new QProgressBar;
    m_sendProgress->setRange(0,100);
    m_sendProgress->hide();

    m_recvProgress=new QProgressBar;
    m_recvProgress->setRange(0,100);
    m_recvProgress->hide();
    QHBoxLayout *inputLayout=new QHBoxLayout;
    m_chatInput=new QTextEdit;
    m_chatInput->setMaximumHeight(30);

    m_btnSendMes=new QPushButton("发送");
    m_btnSendMes->setEnabled(false);
    m_btnSendFile=new QPushButton("文件传输");
    m_btnSendFile->setEnabled(false);
    inputLayout->addWidget(m_chatInput);
    inputLayout->addWidget(m_btnSendMes);
    inputLayout->addWidget(m_btnSendFile);

    chatLayout->addWidget(m_chatShow);
    chatLayout->addWidget(m_sendProgress);
    chatLayout->addWidget(m_recvProgress);
    chatLayout->addLayout(inputLayout);

    splitter->addWidget(chatWidget);
    setCentralWidget(splitter);

    //底部
    m_btnViewLogs=new QPushButton("查看日志");
    m_statusLabel=new QLabel("状态:未连接");
    statusBar()->addWidget(m_statusLabel);
    statusBar()->addPermanentWidget(m_btnViewLogs);
}

void TcpClient::initLogsWindow()
{
    //日志
    m_logsWindow=new QDialog(this);
    m_logsWindow->setWindowTitle("系统日志");
    m_logsWindow->setFixedSize(700,500);

    m_logText=new QTextEdit;
    m_logText->setReadOnly(true);

    QPushButton* btnClear = new QPushButton("清空日志");
    QPushButton* btnClose = new QPushButton("关闭");

    QHBoxLayout* btnLayout = new QHBoxLayout;
    btnLayout->addWidget(btnClear);
    btnLayout->addStretch();
    btnLayout->addWidget(btnClose);

    QVBoxLayout* mainLayout = new QVBoxLayout(m_logsWindow);
    mainLayout->addWidget(m_logText);
    mainLayout->addLayout(btnLayout);

    connect(btnClear, &QPushButton::clicked, this, [this](){
        m_logText->clear();
    });
    connect(btnClose, &QPushButton::clicked, m_logsWindow, &QDialog::close);
}

void TcpClient::setList(const QString &ip,const int &port)
{
    QString equipment=QString("%1 : %2").arg(ip).arg(port);
    m_deviceList->addItem(equipment);
}

void TcpClient::setInfo(Information info,const QString &text)
{
    switch(info)
    {
        case Information::Connected:
        {
            m_statusLabel->setText("状态:已连接");
            QMessageBox::information(this,"",text);
            m_btnSendMes->setEnabled(true);
            m_btnSendFile->setEnabled(true);
            appendLog(text);
            break;
        }
        case Information::Disconnected:
        {
            m_statusLabel->setText("状态:未连接");
            QMessageBox::information(this,"",text);
            m_btnSendMes->setEnabled(false);
            m_btnSendFile->setEnabled(false);
            appendLog(text);
            break;
        }
        case Information::Reconnecting:
        {
            m_btnSendMes->setEnabled(false);
            m_btnSendFile->setEnabled(false);
            m_statusLabel->setText("状态:重连中");
            break;
        }
        case Information::Error:
        {
            QMessageBox::information(this,"",text);
            appendLog(text);
            break;
        }
        case Information::Logs:
        {
            appendLog(text);
            break;
        }
    }
}

void TcpClient::appendLog(const QString &log)
{
    if(!m_logText) return;
    QString time = QDateTime::currentDateTime().toString("[HH:mm:ss] ");
    m_logText->append(time + log);

    // 自动滚动到底部
    m_logText->moveCursor(QTextCursor::End);
    m_logText->ensureCursorVisible();
}

void TcpClient::onRecvProgress(const quint64 &sent,const quint64 &total)
{
    m_recvProgress->show();
    int percent=static_cast<int>((qreal)sent/total*100);
    m_recvProgress->setValue(percent);

    if(sent>=total)
    {
        m_recvProgress->hide();
        m_recvProgress->setValue(0);
    }
}

void TcpClient::onSendProgress(const quint64 &sent,const quint64 &total)
{
    m_sendProgress->show();
    int percent=static_cast<int>((qreal)sent/total*100);
    m_sendProgress->setValue(percent);

    if(sent>=total)
    {
        m_sendProgress->hide();
        m_sendProgress->setValue(0);
    }
}

void TcpClient::onSendFile()
{
    QString filePath=QFileDialog::getOpenFileName(this);
    if(filePath.isEmpty()) return;

    QMetaObject::invokeMethod(m_worker,"sendFileHead",Q_ARG(QString,filePath));
}

void TcpClient::onSendMes()
{
    if(m_chatInput->toPlainText().isEmpty()) return;
    QString mes=m_chatInput->toPlainText();
    m_chatShow->append("[我]"+mes);
    m_chatInput->clear();
    m_chatInput->setFocus();
    QMetaObject::invokeMethod(m_worker,"sendMessage",Q_ARG(QString,mes));
}

void TcpClient::onShowMes(const QString &mes)
{
    m_chatShow->append("[服务端]"+mes);
}

void TcpClient::onDisconnected()
{
    QMetaObject::invokeMethod(m_worker,"disconnectFromServer");
}

void TcpClient::onConnection()
{
    QString ip=m_editIP->text();
    int port=m_editPort->text().toInt();

    QMetaObject::invokeMethod(m_worker,"connectToServer",Q_ARG(QString,ip),Q_ARG(int,port));
}

TcpClient::~TcpClient()
{
    if(m_workThread->isRunning())
    {
        m_workThread->quit();
        m_workThread->wait();
    }
}
