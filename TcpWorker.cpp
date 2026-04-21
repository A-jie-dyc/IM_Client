#include "TcpWorker.h"

#include <QHostAddress>
#include <QDebug>
#include <QByteArray>
#include <QDataStream>
#include <QString>
#include <QFileInfo>
#include <QtMinMax>
#include <qendian.h>

TcpWorker::TcpWorker(QObject *parent)
    : QObject{parent}
    ,m_socket(new QTcpSocket(this))
    ,m_heartTimer(new QTimer(this))
{
    m_heartTimer->setInterval(5000);
    m_rc.timer=new QTimer(this);
    m_rc.timer->setInterval(m_rc.baseInterval);

    connect(m_socket,&QTcpSocket::connected,this,[this](){
        qDebug()<<"成功连接服务器";
        m_heartTimer->start();
        m_rc.reset();
    });
    connect(m_socket,&QTcpSocket::disconnected,this,[this](){
        qDebug()<<"已断开服务器";
        m_heartTimer->stop();
        m_heartCount=0;
        if(m_rc.allowReconnect)
        {
            qDebug()<<"异常断开，指数退避重连中...";
            errorReconnect();
        }
    });
    connect(m_socket,&QTcpSocket::readyRead,this,&TcpWorker::onReadyRead);
    connect(m_socket,&QTcpSocket::bytesWritten,this,&TcpWorker::onSendFileContent);
    connect(m_socket,&QTcpSocket::errorOccurred,this,[this](QAbstractSocket::SocketError err){
        qDebug()<<"网络错误:"<<m_socket->errorString();
        if(m_rc.allowReconnect)
            errorReconnect();
        else
            cleanResource();
    });
    //心跳
    connect(m_heartTimer,&QTimer::timeout,this,&TcpWorker::sendHeartRep);
    connect(m_rc.timer,&QTimer::timeout,this,&TcpWorker::errorReconnect);
}

//连接服务器
void TcpWorker::connectToServer(const QString &ip,const int &port)
{
    m_rc.lastIp=ip;
    m_rc.lastPort=port;
    if(m_socket->state()==QAbstractSocket::ConnectedState)
    {
        m_socket->disconnectFromHost();
    }

    m_socket->connectToHost(ip,port);
}

//断开服务器
void TcpWorker::disconnectFromServer()
{
    if(m_socket->state()!=QAbstractSocket::UnconnectedState)
    {
        m_rc.allowReconnect=false;
        m_socket->disconnectFromHost();
        cleanResource();
    }
    else
    {
        qDebug()<<"未连接服务器";
    }
}

//接收
void TcpWorker::onReadyRead()
{
    if(!m_socket) return;
    m_rf.buf.append(m_socket->readAll());
    //溢出缓存，被恶意灌输信息，自动断开
    if(m_rf.buf.size()>100*1024*1024)
    {
        m_rf.buf.clear();
        qDebug()<<"缓冲区溢出，断开连接";
        m_socket->disconnectFromHost();
        return;
    }

    QByteArray magic(reinterpret_cast<const char*>(&PACKET_MAGIC),sizeof(PACKET_MAGIC));
    int magicPos=m_rf.buf.indexOf(magic);
    if(magicPos==-1)
    {
        m_rf.buf.clear();
        qDebug()<<"未找到魔数，清空脏数据";
    }
    else if(magicPos>0)
    {
        m_rf.buf=m_rf.buf.mid(magicPos);
        qDebug()<<"检测到脏数据，已保留魔数开头的有效数据";
    }
    packetHeader inHeader;
    QByteArray inData;
    while(unpackPacket(m_rf.buf,inHeader,inData))
    {
        Cmd cmd=static_cast<Cmd>(inHeader.cmd);
        switch(cmd)
        {
            case Cmd::HeartRep:
            {
                qDebug()<<"收到心跳请求";
                auto resp=packPacket(static_cast<quint8>(Cmd::HeartResp),{});
                m_socket->write(resp);
                break;
            }
            case Cmd::HeartResp:
            {
                qDebug()<<"收到心跳响应";
                m_heartCount=0;
                break;
            }
            case Cmd::Mes:
            {
                QString mes=QString::fromUtf8(inData);
                emit sigMessage(mes);
                break;
            }
            case Cmd::File:
            {
                processFile(inData);
                break;
            }
            case Cmd::Query:
            {
                processFileQuery(inData);
                break;
            }
            case Cmd::Resp:
            {
                processFileResp(inData);
                break;
            }
        }
    }
}

//处理文件头和数据
void TcpWorker::processFile(const QByteArray &data)
{
    if(!m_rf.file&&!m_rf.fileName.isEmpty())
    {
        parseFileSize(data);
        return;
    }
    if(m_rf.totalSize>0&&m_rf.file&&m_rf.file->isOpen())
    {
        writeFileData(data);
    }
}

//解析文件大小
void TcpWorker::parseFileSize(const QByteArray &data)
{
    const char *remainPtr=data.constData()+8;
    m_rf.totalSize=qFromLittleEndian(*reinterpret_cast<const quint64*>(data.constData()));
    m_rf.remainSize=qFromLittleEndian(*reinterpret_cast<const quint64*>(remainPtr));

    if(m_rf.remainSize<=0)
    {
        qDebug()<<"文件接收完成";
        m_rf.reset();
        return;
    }

    m_rf.isBigFile=(m_rf.remainSize>100*1024*1024);
    m_rf.file=new QFile("recv_"+m_rf.fileName,this);

    if(!m_rf.file->open(QIODevice::ReadWrite))
    {
        qDebug()<<"文件打开失败,已拒绝接收";
        m_rf.reset();
        return;
    }

    m_rf.recvSize=m_rf.file->size();

    if(m_rf.isBigFile)
    {
        if(m_rf.recvSize==0)
        {
            m_rf.file->resize(m_rf.totalSize);
        }
        m_rf.fileMem=m_rf.file->map(0,m_rf.totalSize);
        if(!m_rf.fileMem)
        {
            qDebug()<<"内存映射失败，切换普通输入";
            m_rf.isBigFile=false;
        }
    }

    if(m_rf.recvSize==0)
        qDebug()<<"接收新文件："<<m_rf.fileName<<"总大小："<<m_rf.totalSize;
    else
        qDebug()<<"续传文件："<<m_rf.fileName<<"续传位置："<<m_rf.recvSize<<"剩余大小："<<m_rf.remainSize<<"总大小："<<m_rf.totalSize;
}

//读写文件数据
void TcpWorker::writeFileData(const QByteArray &data)
{
    quint64 writeSize=qMin((qint64)data.size(),m_rf.remainSize);
    if(writeSize>0)
    {
        m_rf.writeCount+=writeSize;
        if(m_rf.isBigFile&&m_rf.fileMem)
        {
            //强转char*再映射偏移
            char* dest=(char*)m_rf.fileMem+m_rf.recvSize;
            memcpy(dest,data.data(),writeSize);
        }
        else
        {
            m_rf.file->seek(m_rf.recvSize);
            m_rf.file->write(data.constData(),writeSize);
        }

        m_rf.recvSize+=writeSize;
        m_rf.remainSize-=writeSize;
        emit sigRecvProgress(m_rf.recvSize,m_rf.totalSize);

        if(m_rf.remainSize<=0)
        {
            m_rf.file->flush();
            qDebug()<<"文件接收完成："<<m_rf.fileName;
            m_rf.reset();
        }
        if(m_rf.writeCount>=65536)
        {
            m_rf.file->flush();
            m_rf.writeCount=0;
        }
    }

    if(m_rf.recvSize>=m_rf.totalSize&&m_rf.totalSize>0)
    {
        qDebug()<<"文件接收完成："<<m_rf.fileName;
        m_rf.reset();
    }
}

//处理断点查询请求
void TcpWorker::processFileQuery(const QByteArray &data)
{
    m_rf.reset();
    m_rf.fileName=QString::fromUtf8(data);
    QFile file("recv_"+m_rf.fileName);

    if(file.exists()) m_rf.recvSize=file.size();
    else m_rf.recvSize=0;

    quint64 offset=qToLittleEndian(m_rf.recvSize);
    QByteArray respData(reinterpret_cast<const char*>(&offset),8);

    QByteArray packet=packPacket(static_cast<quint8>(Cmd::Resp),respData);
    m_socket->write(packet);
}

//处理断点回应
void TcpWorker::processFileResp(const QByteArray &data)
{
    if(!m_sf.file||!m_sf.file->isOpen()) return;

    quint64 offset=qFromLittleEndian(*reinterpret_cast<const quint64*>(data.constData()));
    m_sf.file->seek(offset);
    m_sf.sendSize=offset;
    sendFileSize();
}

//发送文件头
void TcpWorker::sendFileHead(const QString &filePath)
{
    m_sf.reset();

    m_sf.file=new QFile(filePath,this);
    if (!m_sf.file->open(QIODevice::ReadOnly)) {
        qDebug() << "文件打开失败：" << filePath;
        m_sf.file->deleteLater();
        m_sf.file=nullptr;
        return;
    }

    m_sf.fileName=QFileInfo(filePath).fileName();
    QByteArray name=m_sf.fileName.toUtf8();

    QByteArray packet=packPacket(static_cast<quint8>(Cmd::Query),name);
    m_socket->write(packet);
}

//发送文件大小
void TcpWorker::sendFileSize()
{
    if(!m_sf.file) return;
    m_sf.isSending=true;
    m_sf.totalSize=m_sf.file->size();
    qint64 remain=m_sf.totalSize-m_sf.sendSize;
    if(remain<=0)
    {
        qDebug()<<"对方已接收过该文件";
        return;
    }
    //转小端
    quint64 fileSize=qToLittleEndian(m_sf.totalSize);
    quint64 remainSize=qToLittleEndian(remain);
    //转字节流
    QByteArray dataSize;
    dataSize.append(reinterpret_cast<const char*>(&fileSize),8);
    dataSize.append(reinterpret_cast<const char*>(&remainSize),8);
    QByteArray packet=packPacket(static_cast<quint8>(Cmd::File),dataSize);
    m_socket->write(packet);
}

//发送文件内容
void TcpWorker::onSendFileContent()
{
    if(!m_sf.file||!m_sf.file->isOpen()) return;
    emit sigSendProgress(m_sf.sendSize,m_sf.totalSize);
    if(m_sf.file->atEnd())
    {
        m_sf.reset();
        qDebug()<<"文件传输完成";
        return;
    }
    else
    {
        QByteArray fileData=m_sf.file->read(65536);
        m_sf.sendSize+=fileData.size();
        QByteArray packet=packPacket(static_cast<quint8>(Cmd::File),fileData);
        m_socket->write(packet);
    }
}

//发送消息
void TcpWorker::sendMessage(const QString &mes)
{
    QByteArray Mes=mes.toUtf8();
    QByteArray packet=packPacket(static_cast<quint8>(Cmd::Mes),Mes);
    m_socket->write(packet);
}

//打包包头+数据体工具
QByteArray TcpWorker::packPacket(quint8 Cmd,const QByteArray &data)
{
    QByteArray packet;
    packet.reserve(PACKET_HEADER_SIZE+data.size());

    packetHeader header;
    header.magic=PACKET_MAGIC;
    header.size=static_cast<quint16>(data.size());
    header.cmd=Cmd;
    //转换成字节流
    packet.append(reinterpret_cast<const char*>(&header),PACKET_HEADER_SIZE);
    packet.append(data);
    return packet;
}

//解析包头工具
bool TcpWorker::unpackPacket(QByteArray &buf,packetHeader &inHeader,QByteArray &inData)
{
    if(buf.size()<PACKET_HEADER_SIZE) return false;
    //拷贝二进制流内存到一个包头结构体
    memcpy(&inHeader,buf.constData(),PACKET_HEADER_SIZE);
    if(inHeader.magic != PACKET_MAGIC) return false;
    int totalSize=PACKET_HEADER_SIZE+inHeader.size;
    if(buf.size()<totalSize) return false;
    inData=buf.mid(PACKET_HEADER_SIZE,inHeader.size);
    buf=buf.mid(totalSize);
    return true;
}

//心跳
void TcpWorker::sendHeartRep()
{
    if(m_socket->state()!=QAbstractSocket::ConnectedState)
    {
        m_heartTimer->stop();
        return;
    }

    m_heartCount++;
    if(m_heartCount>=3)
    {
        qDebug()<<"心跳超时";
        disconnectFromServer();
        return;
    }

    auto rep=packPacket(static_cast<quint8>(Cmd::HeartRep),{});
    m_socket->write(rep);
}

//自动重连
void TcpWorker::errorReconnect()
{
    if(m_rc.retryCount>=8)
    {
        qDebug()<<"重连次数已达上限，停止自动重连";
        m_rc.timer->stop();
        return;
    }
    int current=qMin(m_rc.baseInterval*(1<<m_rc.retryCount),m_rc.maxInterval);
    m_rc.timer->stop();
    m_rc.timer->setInterval(current);
    m_rc.timer->start();
    qDebug()<<"第"<<m_rc.retryCount+1<<"次重连，间隔："<<current/1000<<"s";
    m_socket->connectToHost(m_rc.lastIp,m_rc.lastPort);
    m_rc.retryCount++;
}

//一键重置
void TcpWorker::cleanResource()
{
    m_rf.reset();
    m_sf.reset();
    m_rc.reset();
    m_heartTimer->stop();
}

TcpWorker::~TcpWorker()
{
    cleanResource();
}
