# 🚀 Jetson Remote
**Ultra Low-Latency Hardware-Accelerated Remote Desktop for NVIDIA Jetson**

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-NVIDIA%20Jetson%20%7C%20Linux-green.svg)
![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![Qt](https://img.shields.io/badge/Qt-5.15%2B-41cd52.svg)

Jetson Remote is a custom-built, ultra-low-latency remote control and streaming solution designed specifically for Robotics Engineers and developers working with ROS2 on NVIDIA Jetson edge devices (Jetson Nano, Xavier NX, Orin, etc.). 

By bypassing traditional laggy protocols (like VNC or AnyDesk) and utilizing Jetson's native hardware encoder (`nvv4l2h264enc`) via UDP, this tool delivers real-time X11 desktop streaming and kernel-level mouse/keyboard injection.

## ✨ Key Features
* **Zero-Lag Video Streaming:** Uses GStreamer with NVIDIA's NVMM (Hardware Acceleration) to compress and stream H.264 video directly over UDP.
* **Kernel-Level Input Injection:** Uses Linux `/dev/uinput` for absolute mouse positioning and native keyboard keystrokes.
* **Auto-Discovery System:** The Jetson Server automatically detects the Client's IP upon receiving a wake-up packet, eliminating the need to hardcode IPs.
* **Deadlock Auto-Recovery:** Built-in watchdog to automatically restart the GStreamer pipeline if the connection drops.

## 🏗 Architecture
This repository contains two main components:
* `Server_Jetson/`: The C++ UDP Server running on the NVIDIA Jetson.
* `Client_UI/`: The Qt/QML Client App running on your Host PC (Linux).

---

## 🛠 1. Jetson Server Setup

### Prerequisites
Your Jetson must be running NVIDIA L4T (Linux for Tegra) with X11 display server.

```bash
sudo apt update
sudo apt install gstreamer1.0-tools gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav libx11-dev
```

### Build & Run
```bash
cd Server_Jetson
g++ -O3 remote_mouse.cpp -o remote_server -lpthread
sudo ./remote_server :0
```

*(Note: sudo is required because the server needs permission to access /dev/uinput for hardware-level mouse/keyboard simulation).*

---

## 💻 2. Client UI Setup

### Prerequisites
You need a standard Linux distribution with Qt5 development packages installed.
```bash
sudo apt update
sudo apt install build-essential qtcreator qtbase5-dev qtdeclarative5-dev qml-module-qtquick-controls2 qml-module-qtquick-window2 qml-module-qtmultimedia libqt5multimedia5-plugins qtmultimedia5-dev gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-qt5 libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev qttools5-dev qttools5-dev-tools

```

### Build & Run
1. Open the project in **Qt Creator**.
2. Build the project using the **Release** configuration.
3. Run the application.
4. Press `Ctrl + I` (or click the UI) to open the connection popup.
5. Enter your Jetson's IP Address (LAN or VPN like Tailscale) and click **Connect**.

---

## ⚠️ Known Limitations & Troubleshooting
* **NVIDIA 40" Headless Bug:** If you boot the Jetson without an HDMI monitor attached, Nvidia's driver defaults to a locked 720p virtual display. **Solution:** Use an HDMI Dummy Plug to trick the Jetson into rendering 1080p, or configure `Virtual 1920 1080` in `/etc/X11/xorg.conf`.
* **Keyboard Mapping (The "-8" Rule):** The Qt Client currently subtracts 8 from `nativeScanCode` to perfectly match X11/evdev mappings on Linux. Running the Qt Client on Windows may result in incorrect keystrokes.

---
**Developed by Nguyễn Trọng Đạt**
