#ifndef TCPSENDWORKER_H
#define TCPSENDWORKER_H

#include <QObject>
#include <QTcpSocket>
#include <QFile>
#include <QTimer>

#pragma pack(push,1)
struct packetHeader
{
    quint32 magic;
    quint16 size;
    quint8 cmd;
};
#pragma pack(pop)

enum class Cmd:quint8
{
    Mes,          //消息
    File,         //文件
    Query,        //断点询问
    Resp,         //断点回复
    HeartRep,     //心跳请求
    HeartResp,    //心跳响应
};

enum class Information
{
    Connected,
    Disconnected,
    Reconnecting,
    Logs,
    Error
};

struct sendFile
{
    QFile *file=nullptr;      //发送文件对象
    QString fileName;     //发送文件名
    quint64 totalSize=0;
    quint64 sendSize=0;
    bool isSending=false;
    void reset()
    {
        if (file){
            file->close();
            file->deleteLater();
            file=nullptr;
        }
        fileName.clear();
        totalSize=0;
        sendSize=0;
        isSending=false;
    }
};

struct recvFile
{
    QByteArray buf;       //读缓存
    QFile *file=nullptr;       //接收文件对象
    QString fileName;     //接收文件名
    quint64 totalSize=0;       //文件总大小
    quint64 recvSize=0;       //已接收大小
    quint64 remainSize=0;       //剩余大小
    quint64 writeCount=0;       //刷盘计数器
    bool isBigFile=false;         //大体积文件
    uchar *fileMem=nullptr;       //内存映射指针
    void reset()
    {
        if (fileMem)
        {
            file->unmap(fileMem);
            fileMem=nullptr;
        }
        if (file)
        {
            file->close();
            file->deleteLater();
            file=nullptr;
        }
        fileName.clear();
        totalSize=0;
        recvSize=0;
        remainSize=0;
        writeCount=0;
        isBigFile=false;
    }
};

struct ReconnectInfo
{
    QString lastIp;
    int lastPort=0;
    QTimer *timer=nullptr;
    const int baseInterval=2000;
    const int maxInterval=32000;        //最大间隔
    int setInterval=0;
    bool allowReconnect=true;
    int retryCount=0;
    void reset()
    {
        retryCount=0;
        setInterval=0;
        allowReconnect=true;
        if(timer) timer->stop();
    }
};

class TcpWorker : public QObject
{
    Q_OBJECT
public:
    explicit TcpWorker(QObject *parent = nullptr);
    ~TcpWorker();
    //魔数
    static constexpr quint32 PACKET_MAGIC=0x5A5A5A5A;
    //包头长度
    static constexpr int PACKET_HEADER_SIZE=sizeof(packetHeader);

signals:
    void sigInformation(Information info,const QString &text);
    void sigEquipment(const QString &ip,const int &port);
    void sigRecvProgress(const quint64 &sent,const quint64 &total);
    void sigSendProgress(const quint64 &sent,const quint64 &total);
    void sigMessage(const QString &mes);

public slots:
    void Init();

private slots:
    void connectToServer(const QString &ip,const int &port);
    void disconnectFromServer();
    void onReadyRead();
    void sendMessage(const QString &mes);
    void sendFileHead(const QString &filePath);
    void onSendFileContent();

private:
    void sendHeartRep();
    void sendFileSize();
    void processFile(const QByteArray &data);
    void parseFileSize(const QByteArray &data);
    void writeFileData(const QByteArray &data);
    void processFileQuery(const QByteArray &data);
    void processFileResp(const QByteArray &data);
    void errorReconnect();
    void cleanResource();
    QByteArray packPacket(quint8 Cmd,const QByteArray &data);
    bool unpackPacket(QByteArray &buf,packetHeader &header,QByteArray &data);

    QTcpSocket *m_socket=nullptr;

    QTimer *m_heartTimer=nullptr;       //心跳监测器
    int m_heartCount=0;       //心跳次数

    sendFile m_sf;      //发送
    recvFile m_rf;      //接收

    ReconnectInfo m_rc;      //重连
};

#endif // TCPSENDWORKER_H
