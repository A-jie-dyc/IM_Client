#ifndef TCPSENDWORKER_H
#define TCPSENDWORKER_H

#include <QObject>
#include <QTcpSocket>
#include <QFile>




class TcpWorker : public QObject
{
    Q_OBJECT
public:
    explicit TcpWorker(QObject *parent = nullptr);
    ~TcpWorker();

signals:
    void sigConnection();
    void sigDisconnected();

    void sigRecvProgress(const quint64 &cur,const quint64 &total);
    void sigRecvFinished(const QString &fileName);

    void sigSendProgress(const quint64 &cur,const quint64 &total);
    void sigSendFinished(const QString &fileName);

    void sigMessage(const QString &mes);

private slots:
    void connectToServer(const QString &ip,const int &port);
    void disconnectFromServer();
    void onReadyRead();
    void sendMessage(const QString &mes);
    void sendFile(const QString &filePath);
    void onSendFileContent();

private:
    void cleanResource();
    void sendRealFileHead();

    QTcpSocket *m_socket;

    QFile *m_file;      //发送文件对象
    QString m_fileName;     //发送文件名

    bool isSending;     //发送中状态

    QByteArray m_buf;       //读缓存
    QFile m_recvFile;       //接收文件对象
    QString m_recvFileName;     //接收文件名
    quint64 m_fileSize=0;       //文件总大小
    quint64 m_recvSize=0;       //已接收大小
    quint64 m_writeCount=0;       //刷盘计数器
    bool isBigFile;         //大体积文件
    uchar *m_fileMem;       //内存映射指针
};

#endif // TCPSENDWORKER_H
