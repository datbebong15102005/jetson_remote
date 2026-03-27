#ifndef VIDEORECEIVER_H
#define VIDEORECEIVER_H

#include <QObject>
#include <QImage>

class VideoReceiver : public QObject {
    Q_PROPERTY(int hostWidth READ hostWidth NOTIFY resolutionChanged)
    Q_PROPERTY(int hostHeight READ hostHeight NOTIFY resolutionChanged)

    Q_OBJECT
public:
    explicit VideoReceiver(QObject *parent = nullptr) : QObject(parent), m_width(0), m_height(0) {}

    QImage currentFrame() const { return m_frame; }
    int hostWidth() const { return m_width; }
    int hostHeight() const { return m_height; }

    void updateFrame(const QImage &img, int w, int h) {
        m_frame = img;
        // Nếu độ phân giải thay đổi thì báo cho QML biết
        if (m_width != w || m_height != h) {
            m_width = w;
            m_height = h;
            emit resolutionChanged();
        }
        emit frameUpdated(); // Báo cho giao diện biết có ảnh mới
    }

signals:
    void frameUpdated();
    void resolutionChanged();

private:
    QImage m_frame;
    int m_width;
    int m_height;
};

#endif // VIDEORECEIVER_H
