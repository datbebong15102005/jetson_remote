#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include "udpsender.h" // Thêm Header
#include <stdlib.h>
#include "videoreceiver.h"

// Lớp này dùng để đưa ảnh QImage từ C++ sang cho QML vẽ
class LiveImageProvider : public QQuickImageProvider {
public:
    LiveImageProvider(VideoReceiver *receiver)
        : QQuickImageProvider(QQuickImageProvider::Image), m_receiver(receiver) {}

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override {
        Q_UNUSED(id); Q_UNUSED(requestedSize);
        QImage img = m_receiver->currentFrame();
        if (size) *size = img.size();
        return img;
    }
private:
    VideoReceiver *m_receiver;
};

// Hàm này tự động chạy mỗi khi Jetson bắn về 1 khung hình mới
static GstFlowReturn on_new_sample(GstElement *sink, VideoReceiver *receiver) {
    GstSample *sample;
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    if (sample) {
        // Đọc kích thước thật từ Jetson truyền sang
        GstCaps *caps = gst_sample_get_caps(sample);
        GstStructure *s = gst_caps_get_structure(caps, 0);
        int width = 1920, height = 1080; // Giá trị mặc định
        gst_structure_get_int(s, "width", &width);
        gst_structure_get_int(s, "height", &height);

        GstBuffer *buffer = gst_sample_get_buffer(sample);
        GstMapInfo map;
        if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            // Ép data thô thành ảnh HD
            QImage img(map.data, width, height, QImage::Format_RGBX8888);
            QImage safeImg = img.copy();

            // Ép gửi cả ảnh lẫn kích thước về
            QMetaObject::invokeMethod(receiver, [receiver, safeImg, width, height]() {
                receiver->updateFrame(safeImg, width, height);
            }, Qt::QueuedConnection);

            gst_buffer_unmap(buffer, &map);
        }
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }
    return GST_FLOW_ERROR;
}

int main(int argc, char *argv[]) {
    gst_init(&argc, &argv);
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QGuiApplication app(argc, argv);

    UdpSender udpSender;
    VideoReceiver videoReceiver;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &udpSender);
    engine.rootContext()->setContextProperty("videoReceiver", &videoReceiver);

    // Đăng ký cổng "image://live" cho QML
    engine.addImageProvider(QLatin1String("live"), new LiveImageProvider(&videoReceiver));

    // Dùng appsink hút dữ liệu thô
    QString pipelineStr = "udpsrc port=5000 buffer-size=2147483647 caps=\"application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H264\" ! rtph264depay ! decodebin ! videoconvert ! video/x-raw,format=RGBx ! appsink name=mysink drop=true max-buffers=1 emit-signals=true sync=false";
    GstElement *pipeline = gst_parse_launch(pipelineStr.toStdString().c_str(), nullptr);
    GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline), "mysink");

    g_signal_connect(sink, "new-sample", G_CALLBACK(on_new_sample), &videoReceiver);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    gst_object_unref(sink);

    const QUrl url(QStringLiteral("qrc:/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl) QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);
    engine.load(url);

    // Bẫy sự kiện: Khi bấm dấu X tắt app, dừng ngay GStreamer
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        udpSender.sendSignal(998, 0, 0); // Gửi lệnh ngắt kết nối về Jetson
    });

    return app.exec();
}
