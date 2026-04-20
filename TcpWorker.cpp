#include "TcpWorker.h"

#include <QHostAddress>
#include <QDebug>
#include <QByteArray>
#include <QDataStream>
#include <QString>
#include <QFileInfo>
#include <QtMinMax>

TcpWorker::TcpWorker(QObject *parent)
    : QObject{parent}
    ,m_socket(new QTcpSocket(this))
    ,m_file(nullptr)
    ,isBigFile(false)
    ,m_heartTimer(new QTimer(this))
{
    m_heartTimer->setInterval(5000);

    connect(m_socket,&QTcpSocket::readyRead,this,&TcpWorker::onReadyRead);
    connect(m_socket,&QTcpSocket::bytesWritten,this,&TcpWorker::onSendFileContent);
    connect(m_socket,&QTcpSocket::errorOccurred,this,[this](QAbstractSocket::SocketError err){
        qDebug()<<"网络错误:"<<m_socket->errorString();
        cleanResource();
    });
    connect(m_heartTimer,&QTimer::timeout,this,[this](){
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

        QByteArray hreat;
        hreat.append((char)0x05);
        m_socket->write(hreat);
    });
}


void TcpWorker::connectToServer(const QString &ip,const int &port)
{
    if(m_socket->state()==QAbstractSocket::ConnectedState)
    {
        m_socket->disconnectFromHost();
    }

    m_socket->connectToHost(ip,port);

    if(m_socket->state()==QAbstractSocket::ConnectingState)
    {
        qDebug()<<"成功连接服务器";
        m_heartTimer->start();
    }
}

void TcpWorker::disconnectFromServer()
{
    m_heartTimer->stop();
    m_heartCount=0;
    if(m_socket->state()!=QAbstractSocket::UnconnectedState)
    {
        m_socket->disconnectFromHost();
        qDebug()<<"已断开服务器";
    }
    else
    {
        qDebug()<<"未连接服务器";
    }

}

void TcpWorker::onReadyRead()
{
    if(!m_socket) return;
    m_buf.append(m_socket->readAll());
    if(m_buf.size()>100*1024*1024)
    {
        m_buf.clear();
        qDebug()<<"缓冲区溢出，断开连接";
        m_socket->disconnectFromHost();
        return;
    }
    while(!m_buf.isEmpty())
    {
        if(m_fileSize>0&&m_recvFile.isOpen())
        {
            quint64 writeSize=qMin((quint64)m_buf.size(),m_fileSize-m_recvSize);
            if(isBigFile&&m_fileMem!=nullptr)
            {
                memcpy(m_fileMem+m_recvSize,m_buf.data(),writeSize);
                m_buf=m_buf.mid(writeSize);
                m_recvSize+=writeSize;
            }
            else
            {
                isBigFile=false;
                if(writeSize>0)
                {
                    quint64 Len;
                    Len=m_recvFile.write(m_buf.data(),writeSize);
                    m_writeCount+=Len;
                    m_buf=m_buf.mid(writeSize);
                    m_recvSize+=writeSize;
                    if(Len<=0)
                    {
                        qDebug()<<"写入失败，已拒绝接收";
                        continue;
                    }
                    if(m_writeCount>=65536)
                    {
                        m_recvFile.flush();
                        m_writeCount=0;
                    }
                }
            }
        }
        if(m_recvSize>=m_fileSize&&m_fileSize>0)
        {
            if(isBigFile&&m_fileMem)
            {
                m_recvFile.unmap(m_fileMem);
                m_fileMem=nullptr;
            }
            qDebug()<<"文件传输完成";
            m_recvFile.flush();
            m_recvFile.close();
            m_fileSize=0;
            m_recvSize=0;
            m_writeCount=0;
            isBigFile=false;
            m_recvFileName.clear();
            continue;
        }

        if(m_buf.size()<1) break;
        char flag=m_buf.at(0);

        if(flag==0x01)
        {
            m_buf=m_buf.mid(1);
            emit sigMessage(m_buf);
            continue;
        }
        else if(flag==0x02)
        {
            m_buf=m_buf.mid(1);
            if(m_buf.size()<4+8) break;

            QDataStream in(m_buf);
            in.setVersion(QDataStream::Qt_6_11);

            quint32 nameLen;
            in>>nameLen;
            int needSize=4+nameLen+8;

            if(m_buf.size()<needSize) break;

            QByteArray nameBuf;
            nameBuf.resize(nameLen);
            in.readRawData(nameBuf.data(),nameLen);
            m_recvFileName=QString::fromUtf8(nameBuf);

            in>>m_fileSize;

            if(m_fileSize>100*1024*1024) isBigFile=true;

            m_buf=m_buf.mid(needSize);
            QString filePath="recv_"+m_recvFileName;

            m_recvFile.setFileName(filePath);

            if(!m_recvFile.open(QIODevice::ReadWrite))
            {
                qDebug()<<"文件打开失败";
                m_fileSize=0;
                continue;
            }
            m_recvSize=m_recvFile.size();

            if(m_recvSize>=m_fileSize)
            {
                qDebug()<<"文件已存在且完整"<<m_recvFile.errorString();
                m_recvFile.close();
                m_fileSize=0;
                continue;
            }
            else if(m_recvSize==0)
            {
                if(isBigFile)
                {
                    m_recvFile.resize(m_fileSize);
                    m_fileMem=m_recvFile.map(0,m_fileSize);
                    if(!m_fileMem)
                    {
                        qDebug()<<"内存映射失败，切换小文件写入模式";
                        isBigFile=false;
                    }
                }
                else m_fileMem=nullptr;

                qDebug()<<"接收文件:"<<m_recvFileName<<"总大小:"<<m_fileSize;
                continue;
            }
            else
            {
                isBigFile=false;
                m_fileMem=nullptr;
                qDebug()<<"接收文件:"<<m_recvFileName<<"续传位置:"<<m_recvSize<<"总大小:"<<m_fileSize;
                continue;
            }
        }
        else if(flag==0x03)
        {
            m_buf=m_buf.mid(1);
            if(m_buf.size()<4) break;

            QDataStream in(m_buf);
            in.setVersion(QDataStream::Qt_6_11);

            quint32 nameLen;
            in>>nameLen;
            if(nameLen==0||nameLen>1024*1024)
            {
                qDebug()<<"脏信息传入，已拒绝";
                continue;
            }

            int needSize=4+nameLen;
            if(m_buf.size()<needSize) break;

            QByteArray nameBuf;
            nameBuf.resize(nameLen);
            in.readRawData(nameBuf.data(),nameLen);
            m_recvFileName=QString::fromUtf8(nameBuf);
            m_buf=m_buf.mid(needSize);

            QString recvFilePath="recv_"+m_recvFileName;
            quint32 offset=0;
            if(QFile::exists(recvFilePath))
            {

                offset=QFile(recvFilePath).size();
            }

            QByteArray fileBuf;
            QDataStream out(&fileBuf,QIODevice::WriteOnly);
            out.setVersion(QDataStream::Qt_6_11);

            out<<(char)0x04;
            out<<offset;
            m_socket->write(fileBuf);
        }
        else if(flag==0x04)
        {
            m_buf=m_buf.mid(1);

            if(m_buf.size()<8) break;

            QDataStream in(m_buf);
            in.setVersion(QDataStream::Qt_6_11);

            quint64 offset;
            in>>offset;

            if(offset>=m_file->size())
            {
                qDebug()<<"文件已传输完成";
                continue;
            }
            else if(offset!=0)
            {
                if(!m_file->seek(offset))
                {
                    qDebug()<<"无法定位到断点";
                    continue;
                }
            }
            sendRealFileHead();
            continue;
        }
        else if(flag==0x05)
        {
            m_buf=m_buf.mid(1);
            QByteArray ack;
            ack.append((char)0x06);
            m_socket->write(ack);
        }
        else if(flag==0x06)
        {
            m_buf=m_buf.mid(1);
            m_heartCount=0;
            continue;
        }
        else
        {
            qDebug()<<"未知信息,已拒绝接收";
            m_buf.clear();
            return;
        }
    }
}

void TcpWorker::sendFile(const QString &filePath)
{
    if(m_file)
    {
        m_file->close();
        m_file->deleteLater();
        m_file=nullptr;
    }
    m_file=new QFile(filePath);
    if (!m_file->open(QIODevice::ReadOnly)) {
        qDebug() << "文件打开失败：" << filePath;
        m_file->deleteLater();
        m_file = nullptr;
        return;
    }

    m_fileName=QFileInfo(filePath).fileName();
    QByteArray nameByte=m_fileName.toUtf8();

    QByteArray buf;
    QDataStream out(&buf,QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_11);

    out<<(char)0x03;
    out<<(quint32)nameByte.size();
    out.writeRawData(nameByte.data(),nameByte.size());
    m_socket->write(buf);
}

void TcpWorker::sendRealFileHead()
{
    if(!m_file) return;
    isSending=true;
    QByteArray nameByte=m_fileName.toUtf8();

    QByteArray buf;
    QDataStream out(&buf,QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_11);

    out<<(char)0x02;
    out<<(quint32)nameByte.size();
    out.writeRawData(nameByte.data(),nameByte.size());
    out<<(quint64)m_file->size();
    m_socket->write(buf);
}

void TcpWorker::onSendFileContent()
{
    if(!isSending||!m_file)
    {
        return;
    }
    if(!m_file->atEnd())
    {
        QByteArray buf=m_file->read(65536);
        m_socket->write(buf);
    }
    else
    {
        isSending=false;
        m_file->close();
        m_file->deleteLater();
        m_file=nullptr;
        qDebug()<<"文件传输完成";
    }
}

void TcpWorker::sendMessage(const QString &mes)
{
    QByteArray buf;
    buf.append(0x01);
    buf.append(mes.toUtf8());
    m_socket->write(buf);
}

TcpWorker::~TcpWorker()
{
    cleanResource();
}

void TcpWorker::cleanResource()
{
    if(m_file)
    {
        m_file->close();
        m_file->deleteLater();
        m_file=nullptr;
    }
    isBigFile=false;
    m_fileMem=nullptr;
    isSending=false;
    m_fileSize=0;
    m_recvSize=0;
    m_buf.clear();
    m_recvFileName.clear();
    m_fileName.clear();
    m_heartTimer->stop();
}