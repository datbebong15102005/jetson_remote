#include "udpsender.h"
#include <QNetworkDatagram>
#include <QDebug>

// Cấu trúc gói tin y hệt Jetson
struct MouseAndKeyboardPacket {
    int x;
    int y;
    int click;
    int scroll;
    int signal;

    // For keyboard
    int is_keyboard; // 1: Lệnh phím, 0: Lệnh chuột
    int keycode;     // Mã phím cứng (VD: phím A, B, C...)
    int keystate;    // 1: Đang bấm xuống, 0: Nhả phím ra
};

// Hàm khởi tạo
UdpSender::UdpSender(QObject *parent)
    : QObject(parent), m_targetIp("127.0.0.1"), m_targetPort(5001) // IP mặc định lúc mới mở
{
    m_socket = new QUdpSocket(this);
}

// Hàm cập nhật IP từ giao diện QML
void UdpSender::setTargetIp(const QString &ip) {
    m_targetIp = ip;
    qDebug() << "[+] Updated IP address into:" << m_targetIp;
}

// Hàm điều khiển chuột
void UdpSender::sendMouseData(int x, int y, int click, int scroll)
{
    MouseAndKeyboardPacket packet;
    memset(&packet, 0, sizeof(packet)); // Dọn rác trong RAM
    packet.x = x;
    packet.y = y;
    packet.click = click;
    packet.scroll = scroll;
    packet.signal = 0;
    packet.is_keyboard = 0; // Mode chuột

    // Đóng gói 16 bytes và gửi sang Jetson bằng m_targetIp
    m_socket->writeDatagram(reinterpret_cast<const char*>(&packet),
                            sizeof(MouseAndKeyboardPacket),
                            QHostAddress(m_targetIp),
                            m_targetPort);
}

// Hàm signal
void UdpSender::sendSignal(int signal, int width, int height)
{
    MouseAndKeyboardPacket packet;
    memset(&packet, 0, sizeof(packet)); // Dọn rác trong RAM
    packet.signal = signal;
    packet.x = width;
    packet.y = height;

    // Đóng gói 16 bytes và gửi sang Jetson bằng m_targetIp
    m_socket->writeDatagram(reinterpret_cast<const char*>(&packet),
                            sizeof(MouseAndKeyboardPacket),
                            QHostAddress(m_targetIp),
                            m_targetPort);
}

// Hàm nhập bàn phím
void UdpSender::sendKeyData(int keycode, int keystate) {
    MouseAndKeyboardPacket packet;
    memset(&packet, 0, sizeof(packet)); // Dọn sạch rác trong bộ nhớ

    packet.is_keyboard = 1; // Bật cờ khai báo "Tao là bàn phím"
    packet.signal = 0;
    packet.keycode = keycode;
    packet.keystate = keystate;

    m_socket->writeDatagram(reinterpret_cast<const char*>(&packet),
                            sizeof(MouseAndKeyboardPacket),
                            QHostAddress(m_targetIp),
                            m_targetPort);
}
