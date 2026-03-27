import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15

Window {
    id: mainWindow
    width: 1280
    height: 720
    visible: true
    title: qsTr("Jetson Remote - Client")
    color: "#000000"

    // Phím tắt để gọi lại hộp thoại đổi IP (Bấm Ctrl + I)
    Shortcut {
        sequence: "Ctrl+I"
        onActivated: ipPopup.open()
    }

    // Cửa sổ Pop-up nhập IP
    Popup {
        id: ipPopup
        anchors.centerIn: parent // Căn giữa màn hình
        width: 350
        height: 180
        modal: true // Làm tối background đằng sau
        focus: true
        closePolicy: Popup.CloseOnEscape // Bấm Esc để tắt

        onClosed: {
            console.log("[*] Pop-up đã đóng. Trả lại quyền nhập liệu cho màn hình chính!")
            mainArea.forceActiveFocus() // ÉP màn hình chính cầm lại quyền!
        }

        // Thiết kế background cho cái hộp thoại
        background: Rectangle {
            color: "#2b2b2b"
            radius: 10
            border.color: "#444444"
            border.width: 2
        }

        Column {
            anchors.centerIn: parent
            spacing: 15

            Text {
                text: "Type Jetson IP for mouse remote"
                color: "#ffffff"
                font.pixelSize: 16
                font.bold: true
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Rectangle {
                width: 280
                height: 40
                color: "#1e1e1e"
                radius: 5
                border.color: "#555"

                TextInput {
                    id: ipInput
                    anchors.fill: parent
                    anchors.margins: 10
                    verticalAlignment: TextInput.AlignVCenter
                    color: "#00ff00" // Màu chữ xanh lá
                    font.pixelSize: 16
                    text: "127.0.0.1" // IP mặc định
                    clip: true
                    selectByMouse: true
                }
            }

            Button {
                text: "Connect"
                width: 150
                height: 40
                anchors.horizontalCenter: parent.horizontalCenter

                // Giao diện nút bấm
                contentItem: Text {
                    text: parent.text
                    color: "white"
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.down ? "#005500" : "#008800"
                    radius: 5
                }

                onClicked: {
                    // Gửi IP xuống C++
                    backend.setTargetIp(ipInput.text)

                    // Gọi đánh thức
                    console.log("Đang gọi Jetson ở IP: " + ipInput.text)
                    backend.sendInitPacket(videoReceiver.hostWidth, videoReceiver.hostHeight)

                    // Đóng Popup
                    ipPopup.close()

                    // Trả lại quyền điều khiển cho màn hình chính
                    mainWindow.requestActivate()
                }
            }
        }
    }

    // Tự động mở hộp thoại Pop-up này khi App vừa bật lên!
    Component.onCompleted: {
        ipPopup.open()
    }

    // Màn hình hứng Video
    Image {
        id: videoFrame
        anchors.fill: parent
        fillMode: Image.Stretch
        cache: false // Phải tắt cache để load 60fps

        property int counter: 0
        // Ép QML phải vẽ lại ảnh liên tục khi có biến đổi
        source: "image://live/frame" + counter

        Connections {
            target: videoReceiver
            function onFrameUpdated() {
                videoFrame.counter++
                watchdogTimer.restart()
            }

            function onResolutionChanged() {
                console.log("Phát hiện Jetson đổi phân giải: " + videoReceiver.hostWidth + "x" + videoReceiver.hostHeight)
                // Lập tức nã gói tin cấu hình sang Jetson
                backend.sendInitPacket(videoReceiver.hostWidth, videoReceiver.hostHeight)
            }
        }
    }

    // Cơ chế Watchdog: 5 giây không có frame là báo động!
    Timer {
        id: watchdogTimer
        interval: 5000 // 5000ms = 5 giây
        running: true
        repeat: false // Chỉ chạy 1 lần nếu bị timeout
        onTriggered: {
            console.log("[!] Deadlock Detected! GStreamer đã bị lỗi. Gửi lệnh 888 để Reset...")
            // Dùng số 888 làm Magic Number ra lệnh Kill/Restart
            backend.sendMouseData(0, 0, 888, 0)
        }
    }

    // Màng bắt chuột
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        // Lấy kích thước thật từ C++ (Nếu chưa có data thì ngầm định 1280x720)
        function mapX(mouseX) {
            let hw = videoReceiver.hostWidth > 0 ? videoReceiver.hostWidth : 1280
            return Math.round((mouseX / width) * hw)
        }
        function mapY(mouseY) {
            let hh = videoReceiver.hostHeight > 0 ? videoReceiver.hostHeight : 720
            return Math.round((mouseY / height) * hh)
        }

        onPositionChanged: (mouse) => {
            let currentClick = 0
            if (mouse.buttons & Qt.LeftButton) currentClick = 1
            else if (mouse.buttons & Qt.RightButton) currentClick = 2
            backend.sendMouseData(mapX(mouse.x), mapY(mouse.y), currentClick)
        }

        onPressed: (mouse) => {
            let clickType = 0
            if (mouse.button === Qt.LeftButton) clickType = 1
            else if (mouse.button === Qt.RightButton) clickType = 2
            mainArea.forceActiveFocus()
            backend.sendMouseData(mapX(mouse.x), mapY(mouse.y), clickType)
        }

        onReleased: (mouse) => {
            backend.sendMouseData(mapX(mouse.x), mapY(mouse.y), 0)
        }
        onWheel: (wheel) => {
            // angleDelta.y thường mang giá trị 120 (lên) hoặc -120 (xuống) mỗi khấc lăn
            let scrollDir = (wheel.angleDelta.y > 0) ? 1 : -1

            // Bắn luồng scroll sang (x, y hiện tại, click=0, scroll=scrollDir)
            backend.sendMouseData(mapX(wheel.x), mapY(wheel.y), 0, scrollDir)
        }
    }

    Item {
        id: mainArea
        anchors.fill: parent
        focus: true

        // Khi phím được bấm xuống
        Keys.onPressed: (event) => {
            // Chặn spam liên tục khi nhấn giữ 1 phím
            if (!event.isAutoRepeat) {
                backend.sendKeyData(event.nativeScanCode - 8, 1)
            }
            event.accepted = true
        }

        // Khi phím được nhả ra
        Keys.onReleased: (event) => {
            if (!event.isAutoRepeat) {
                backend.sendKeyData(event.nativeScanCode - 8, 0)
            }
            event.accepted = true
        }
    }
}
