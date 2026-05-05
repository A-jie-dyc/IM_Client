#include "TcpClient.h"

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
#include <QSqlError>
#include <QSqlQuery>

TcpClient::TcpClient(QWidget *parent)
    : QMainWindow{parent}
{

    setWindowTitle("IM客户端");
    setFixedSize(800,600);

    initWindow();
    initLogsWindow();
    initDatabase();

    m_worker=new TcpWorker;
    m_workThread=new QThread(this);
    m_worker->moveToThread(m_workThread);
    connect(m_workThread,&QThread::started,m_worker,&TcpWorker::Init);
    m_workThread->start();

    connect(m_onConnection,&QPushButton::clicked,this,&TcpClient::onConnection);
    connect(m_onDisconnected,&QPushButton::clicked,this,&TcpClient::onDisconnected);
    connect(m_deviceList,&QListWidget::itemClicked,this,&TcpClient::onDeviceSelect);
    connect(m_deviceList,&QListWidget::customContextMenuRequested,this,&TcpClient::onDeviceRightMenu);
    connect(m_btnSendMes,&QPushButton::clicked,this,&TcpClient::onSendMes);
    connect(m_chatInput,&ChatEdit::Send,this,&TcpClient::onSendMes);
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

void TcpClient::onDeviceRightMenu(const QPoint pos)
{
    QListWidgetItem *item=m_deviceList->itemAt(pos);
    if(!item) return;
    m_currentDevice=item->text();
    QMenu menu;
    QAction *actClear=menu.addAction("删除聊天记录");
    QPoint globalPos=m_deviceList->mapToGlobal(pos);
    QAction *selectAct=menu.exec(globalPos);
    if(selectAct==actClear)
        clearCurrentDeviceChat();
}

void TcpClient::clearCurrentDeviceChat()
{
    if(m_currentDevice.isEmpty())
    {
        appendLog("删除聊天记录失败:没有选择设备");
        return;
    }
    auto rep=QMessageBox::question(this,"确认","确认删除与【"+m_currentDevice+"】的聊天记录吗？");
    if(rep!=QMessageBox::Yes) return;
    QSqlQuery query;
    query.prepare("DELETE FROM chat_history WHERE device_key=:key");
    query.bindValue(":key",m_currentDevice);
    query.exec();
    m_chatShow->clear();
}

void TcpClient::localChatHistory(const QString &keyword)
{
    QSqlQuery query;
    QString sql=R"(
        SELECT sender,content,time
        FROM chat_history
        WHERE device_key LIKE :keyword
        ORDER BY time
    )";
    query.prepare(sql);
    query.bindValue(":keyword","%"+keyword+"%");
    if(!query.exec())
    {
        appendLog("查询设备"+keyword+"聊天记录失败:"+query.lastError().text());
        return;
    }
    while(query.next())
    {
        QString sender=query.value("sender").toString();
        QString content=query.value("content").toString();
        QString time=query.value("time").toString();
        m_chatShow->append("["+time+"]"+"【"+sender+"】："+content);
    }
}

void TcpClient::saveChatMessage(const QString &deviceInfo,const QString &sender,const QString &content)
{
    QSqlQuery query;
    QString sql=R"(
        INSERT INTO chat_history(device_key,sender,content)
        VALUES (:device_key,:sender,:content)
    )";
    query.prepare(sql);
    query.bindValue(":device_key", deviceInfo);
    query.bindValue(":sender", sender);
    query.bindValue(":content", content);

    if (!query.exec()) {
        appendLog("设备:"+deviceInfo+"保存聊天记录失败：" + query.lastError().text());
    }
}

void TcpClient::onDeviceSelect(QListWidgetItem *item)
{
    if(!item) return;
    m_currentDevice=item->text();
    m_chatShow->clear();
    localChatHistory(m_currentDevice);
    m_chatShow->moveCursor(QTextCursor::End);
    if(m_statusLabel->text()=="状态:已连接")
    {
        m_btnSendMes->setEnabled(true);
        m_btnSendFile->setEnabled(true);
    }
}

void TcpClient::initDatabase()
{
    m_db=QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName("local_ChatHistory.db");
    if(!m_db.open())
        appendLog("数据库打开失败:"+m_db.lastError().text());
    else
    {
        appendLog("打开数据库");
        QSqlQuery query;
        QString sql=R"(CREATE TABLE IF NOT EXISTS chat_history(
                        id INTEGER PRIMARY KEY,
                        device_key TEXT NOT NULL,
                        sender TEXT NOT NULL,
                        content TEXT NOT NULL,
                        time TIMESTAMP DEFAULT (datetime('now','localtime'))
                        );
                    )";
        if(!query.exec(sql))
            appendLog("建表失败:"+ query.lastError().text());
        else
            appendLog("聊天记录初始化成功");
    }
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
    //中左
    m_deviceList=new QListWidget;
    m_deviceList->setFixedWidth(200);
    m_deviceList->setContextMenuPolicy(Qt::CustomContextMenu);
    splitter->addWidget(m_deviceList);
    //中右
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
    m_chatInput=new ChatEdit;
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
    if(m_deviceList->findItems(equipment,Qt::MatchExactly).isEmpty())
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
            appendLog(text);
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
    onShowMes(true,mes);
    m_chatInput->clear();
    m_chatInput->setFocus();
    QMetaObject::invokeMethod(m_worker,"sendMessage",Q_ARG(QString,mes));
}
//
void TcpClient::onShowMes(bool me,const QString &mes)
{
    QString time=QDateTime::currentDateTime().toString("[HH:mm:ss]");
    if(me)
    {
        m_chatShow->append(time+"【我】："+mes);
        saveChatMessage(m_currentDevice,"我",mes);
    }
    else
    {
        m_chatShow->append(time+"【"+m_currentDevice+"】："+mes);
        saveChatMessage(m_currentDevice,m_currentDevice,mes);
    }
    m_chatShow->moveCursor(QTextCursor::End);
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
