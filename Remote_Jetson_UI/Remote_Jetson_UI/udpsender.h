#ifndef UDPSENDER_H
#define UDPSENDER_H

#include <QObject>
#include <QUdpSocket>
#include <QString>

class UdpSender : public QObject
{
    Q_OBJECT
public:
    explicit UdpSender(QObject *parent = nullptr);

    Q_INVOKABLE void setTargetIp(const QString &ip);
    Q_INVOKABLE void sendMouseData(int x, int y, int click, int scroll = 0);
    Q_INVOKABLE void sendInitPacket(int width, int height);

    Q_INVOKABLE void sendKeyData(int keycode, int keystate);

private:
    QUdpSocket *m_socket;
    QString m_targetIp;
    int m_targetPort;
};

#endif // UDPSENDER_H
