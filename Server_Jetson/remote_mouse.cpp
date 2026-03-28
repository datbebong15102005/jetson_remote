#include <thread>
#include <chrono>
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <linux/uinput.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstdlib>
#include <mutex>
#include <csignal>
#include <arpa/inet.h>
#include "httplib.h"
#include <fstream> // Need to get output of tegrastats
#include <cstdio> // Need for p.open and popen
#include <memory> // Need for shared_ptr to popen and pclose

// Định nghĩa gói tin siêu nhẹ (16 bytes)
struct MouseAndKeyboardPacket {
    int x;
    int y;
    int click; // 0: Không click, 1: Chuột trái, 2: Chuột phải
    int scroll;

    // For keyboard
    int is_keyboard; // 1: Lệnh phím, 0: Lệnh chuột
    int keycode;     // Mã phím cứng (VD: phím A, B, C...)
    int keystate;    // 1: Đang bấm xuống, 0: Nhả phím ra
};

int uinput_fd = -1; // Biến toàn cục giữ file mô phỏng chuột
std::mutex mouse_mtx; // Ổ khóa bảo vệ chuột
std::string target_display = ":0"; // Biến toàn cục giữ tên màn hình
std::string target_ip = "127.0.0.1"; 
std::string target_bitrate = "8000000";

void init_virtual_mouse(int width, int height) {
    std::lock_guard<std::mutex> lock(mouse_mtx);
    
    // Nếu có con chuột cũ đang chạy thì đập đi xây lại
    if (uinput_fd >= 0) {
        ioctl(uinput_fd, UI_DEV_DESTROY);
        close(uinput_fd);
    }

    uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinput_fd < 0) return;

    ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_RIGHT);
    ioctl(uinput_fd, UI_SET_EVBIT, EV_ABS);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_X);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_Y);
    ioctl(uinput_fd, UI_SET_EVBIT, EV_REL);
    ioctl(uinput_fd, UI_SET_RELBIT, REL_WHEEL);

    // Keyboard input
    for (int i = 1; i < 256; i++) {
        ioctl(uinput_fd, UI_SET_KEYBIT, i);
    }

    struct uinput_user_dev uud;
    memset(&uud, 0, sizeof(uud));
    snprintf(uud.name, UINPUT_MAX_NAME_SIZE, "Dynamic_Remote_Mouse_and_Keyboard");
    uud.id.bustype = BUS_USB;
    uud.id.vendor  = 0x1234; 
    uud.id.product = 0x5678;
    uud.absmin[ABS_X] = 0; uud.absmax[ABS_X] = width;
    uud.absmin[ABS_Y] = 0; uud.absmax[ABS_Y] = height;

    write(uinput_fd, &uud, sizeof(uud));
    ioctl(uinput_fd, UI_DEV_CREATE);
    std::cout << "[+] Đã tạo chuột ảo Dynamic: " << width << "x" << height << "\n";
}

// Hàm tự động kích hoạt khi ông bấm Ctrl+C
void cleanup_and_exit(int signum) {
    std::cout << "\n[!] Nhận lệnh đóng ứng dụng. Đang dọn dẹp ...\n";
    
    // 1. Phá hủy chuột ảo
    if (uinput_fd >= 0) {
        ioctl(uinput_fd, UI_DEV_DESTROY);
        close(uinput_fd);
    }
    
    // 2. Yêu cầu GStreamer tự sát nhẹ nhàng bằng kill -15 (SIGTERM)
    system("if [ -f /tmp/wsr_remote_gst.pid ]; then kill -15 $(cat /tmp/wsr_remote_gst.pid) 2>/dev/null; rm /tmp/wsr_remote_gst.pid; fi");
    
    std::cout << "[+] Shutting down...\n";
    exit(0);
}

void restart_gstreamer() {
    std::cout << "\n[*] Đang kiểm tra và dọn dẹp luồng GStreamer cũ...\n";
    
    // Chỉ Kill đúng cái tiến trình có mã PID lưu trong file tạm (nếu file tồn tại)
    system("if [ -f /tmp/wsr_remote_gst.pid ]; then kill -15 $(cat /tmp/wsr_remote_gst.pid) 2>/dev/null; rm /tmp/wsr_remote_gst.pid; fi"); 
    
    sleep(2); 
    
    std::cout << "[*] Khởi động luồng GStreamer mới...\n";
    
    // Dùng "echo $!" để lấy PID của tiến trình vừa chạy ngầm và lưu vào file
    std::string gst_cmd = "nohup gst-launch-1.0 ximagesrc display-name=" + target_display + 
                          " use-damage=0 ! video/x-raw,framerate=60/1 ! nvvidconv ! 'video/x-raw(memory:NVMM),format=NV12' ! nvv4l2h264enc insert-sps-pps=true maxperf-enable=1 preset-level=1 control-rate=1 bitrate=" + target_bitrate + 
                          " profile=4 ! rtph264pay mtu=1200 config-interval=1 ! udpsink host=" + target_ip + 
                          " port=5000 buffer-size=2147483647 > /dev/null 2>&1 & echo $! > /tmp/wsr_remote_gst.pid";

    system(gst_cmd.c_str());
    std::cout << "[+] Đã khởi động GStreamer! Đang gửi " << target_bitrate << " bps tới IP " << target_ip << "...\n";
}

void emit_event(int fd, int type, int code, int val) {
    struct input_event ie;
    memset(&ie, 0, sizeof(ie));
    ie.type = type;
    ie.code = code;
    ie.value = val;
    write(fd, &ie, sizeof(ie));
}

// Luồng giám sát X11 bằng xrandr
void monitor_resolution() {
    int current_w = 0, current_h = 0;
    
    // Ngủ 1 giây đợi hệ thống ổn định
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Dùng std::endl để ép hệ thống xả buffer ngay lập tức, không được nuốt log!
    std::cout << "[*] XRandr Monitor đã khởi động và đang kiểm tra thông số..." << std::endl;

    while (true) {
        std::string xrandr_cmd = "DISPLAY=" + target_display + " xrandr 2>/dev/null";
        FILE* fp = popen(xrandr_cmd.c_str(), "r");
        if (fp) {
            char line[256];
            int w = 0, h = 0;
            while (fgets(line, sizeof(line), fp)) {
                // Dấu * chỉ định độ phân giải ĐANG HOẠT ĐỘNG
                if (strchr(line, '*')) { 
                    // Bóc tách siêu tốc ra 2 biến w và h
                    if (sscanf(line, " %dx%d", &w, &h) == 2) break; 
                }
            }
            pclose(fp);
            // Lần đầu chạy
            if (current_w == 0 && current_h == 0) {
                current_w = w; 
                current_h = h;
                std::cout << "[*] System Monitor trả về: " << w << "x" << h << std::endl;
            } 
            // Phát hiện độ phân giải thay đổi!
            else if (w != current_w || h != current_h) {
                std::cout << "\n[!] Cảnh báo: Màn hình Jetson vừa thay đổi thành " << w << "x" << h << std::endl;
                current_w = w;
                current_h = h;
                    
                // Quản gia ra tay dọn dẹp hiện trường!
                restart_gstreamer();
                init_virtual_mouse(w, h);
            }
        }
        
        // Ngủ 2 giây đợi hiệp sau
        std::this_thread::sleep_for(std::chrono::seconds(2)); 
    }
}

// Hàm lấy chuỗi trạng thái từ tegrastats để hiển thị trên Dashboard Web
std::string get_tegrastats_string() {
    char buffer[512]; // buffer tạm để đọc output của tegrastats
    std::string result = "";
    
    // Dùng head -n 1 để lấy đúng 1 dòng rồi ngắt!
    FILE* pipe = popen("tegrastats | head -n 1", "r");
    if (!pipe) return "Error: Cannot run tegrastats";
    
    // Đọc cái chuỗi đầu ra
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        result += buffer;
    }
    pclose(pipe); // Đóng luồng
    
    // Nếu rỗng, báo lỗi
    if (result.empty()) return "Error: tegrastats returned empty";
    
    return result; // Chuỗi tegrastats
}

// Web Server để hiển thị Dashboard trạng thái 
void start_web_server() {
    httplib::Server svr;

    // Tạo Router: Bất cứ ai truy cập vào IP của Jetson cổng 8080 đều sẽ thấy trang này
    // Router: Bắn HTML mới mỗi lần ai đó F5
    svr.Get("/", [](const httplib::Request &, httplib::Response &res) {
        
        // 1. Lấy chuỗi tegrastats tươi sống
        std::string raw_stats = get_tegrastats_string();
        
        // 2. Dựng chuỗi HTML màu sắc Cyberpunk (Thêm script reload mỗi 2 giây)
        std::string dynamic_html = R"(
        <!DOCTYPE html>
        <html>
        <head>
            <title>Jetson Remote Dashboard</title>
            <style>
                body { background-color: #1e1e1e; color: #00ff00; font-family: monospace; text-align: center; margin-top: 30px; }
                .main-container { display: flex; flex-direction: column; align-items: center; gap: 20px; }
                .box { border: 2px solid #00ff00; padding: 20px; border-radius: 10px; box-shadow: 0 0 15px #00ff00; min-width: 400px;}
                #stats-box { border: 2px solid #ff00ff; box-shadow: 0 0 15px #ff00ff; color: #ff00ff;} /* Hộp thông số màu tím */
                .stats-text { font-size: 14px; white-space: pre-wrap; word-wrap: break-word; text-align: left; }
                
                .fire { color: red; font-weight: bold; } 
            </style>
            
            <script>
                setTimeout(function(){
                   location.reload();
                }, 2000); 
            </script>
        </head>
        <body>
            <div class="main-container">
                
                <div class="box">
                    <h2><span class="fire">🚀</span> Jetson Remote V2.0 (Beta)</h2>
                    <p>Status: <span style="color: yellow;">RUNNING</span></p>
                </div>
                
                <div class="box" id="stats-box">
                    <h3>📊 System Metrics (tegrastats)</h3>
                    <div class="stats-text">)";
        
        dynamic_html += raw_stats;
        
        // Đóng HTML
        dynamic_html += R"(
                    </div>
                </div>
            </div>
        </body>
        </html>
        )";

        res.set_content(dynamic_html, "text/html");
    });

    std::cout << "\n[+] Web UI Dashboard đang chạy tại cổng 8080...\n";
    
    // Lắng nghe trên mọi IP mạng LAN (0.0.0.0)
    svr.listen("0.0.0.0", 8080);
}

int main(int argc, char *argv[]) {
    // Đăng ký bắt sự kiện Ctrl+C (SIGINT) để dọn dẹp trước khi chết
    signal(SIGINT, cleanup_and_exit);
    
    // Nếu người dùng tự truyền biến môi trường, lấy luôn làm dữ liệu
    if (argc > 1) {
        target_display = argv[1];
    }

    // Nếu không chỉ định, tìm ở trong biến môi trường hệ thống
    else {
        const char* env_disp = getenv("DISPLAY");
        if (env_disp != nullptr && strlen(env_disp) > 0) {
            target_display = env_disp;
        }
    }

    // Lấy IP Laptop
    if (argc > 2) {
        target_ip = argv[2];
    }
    
    // Lấy Bitrate
    if (argc > 3) {
        target_bitrate = argv[3];
    }

    std::cout << "[+] Thông số cấu hình:\n";
    std::cout << "        Màn hình : " << target_display << "\n";
    std::cout << "        Bắn tới IP:   " << target_ip << "\n";
    std::cout << "        Bitrate:      " << target_bitrate << " bps\n";

    // Kích hoạt Web Server chạy ở một luồng riêng biệt
    std::thread web_thread(start_web_server);
    web_thread.detach();

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(5001);
    bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));

    std::cout << "[*] Mouse Server đang chờ lệnh cấu hình...\n";

    // Gọi hàm khởi động GStreamer ngay lúc mới mở tool
    restart_gstreamer();

    // Bật Camera giám sát màn hình X11 chạy ngầm
    std::thread monitor_thread(monitor_resolution);
    monitor_thread.detach();

    MouseAndKeyboardPacket packet;
    
    // 
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (true) {

        if (recvfrom(sock, &packet, sizeof(MouseAndKeyboardPacket), 0, (struct sockaddr *)&client_addr, &client_len) > 0) {
            //Debug gói tin nhận được
            //std::cout << "[Nhận] x: " << packet.x << " | y: " << packet.y 
            //          << " | click: " << packet.click << " | scroll: " << packet.scroll << "\n";

            // Dịch IP của Laptop từ mã nhị phân sang chuỗi (VD: "100.105.184.63")
            std::string sender_ip = inet_ntoa(client_addr.sin_addr);

            // Kiểm tra lệnh từ Laptop
            if (packet.click == 999) {
                // Tự động chuyển hướng Video nếu IP Laptop thay đổi
                if (target_ip != sender_ip) {
                    std::cout << "\n[!] Phát hiện Laptop gọi từ IP mới: " << sender_ip << "\n";
                    std::cout << "[*] Đang chuyển hướng GStreamer sang mục tiêu mới...\n";
                    target_ip = sender_ip; // Cập nhật IP
                    restart_gstreamer(); // Chạy lại GStreamer
                }
                init_virtual_mouse(packet.x, packet.y);
                continue; // Xử lý xong lệnh cấu hình thì bỏ qua, chờ data mới
            }

            if (packet.click == 888) {
                std::cout << "\n[!] Nhận được deadlock signal! \n";
                restart_gstreamer(); // Gọi quản gia ra dọn dẹp và bật lại
                continue;
            }

            // Nếu chưa được cấu hình mà đã nhận data chuột thì bỏ qua
            if (uinput_fd < 0) continue;

            // Mở khối khóa an toàn
            {
                std::lock_guard<std::mutex> lock(mouse_mtx);

                if (packet.is_keyboard == 1) 
                {
                    emit_event(uinput_fd, EV_KEY, packet.keycode, packet.keystate);
                    emit_event(uinput_fd, EV_SYN, SYN_REPORT, 0);
                }

                else 
                {
                    // Bắn tọa độ X, Y vào Kernel
                    emit_event(uinput_fd, EV_ABS, ABS_X, packet.x);
                    emit_event(uinput_fd, EV_ABS, ABS_Y, packet.y);

                    // Bắn Click chuẩn bài của Linux
                    if (packet.click == 1) {
                        emit_event(uinput_fd, EV_KEY, BTN_LEFT, 1); // Giữ chuột trái
                    } else if (packet.click == 2) {
                        emit_event(uinput_fd, EV_KEY, BTN_RIGHT, 1); // Giữ chuột phải
                    } else if (packet.click == 0) {
                        emit_event(uinput_fd, EV_KEY, BTN_LEFT, 0); // Nhả trái
                        emit_event(uinput_fd, EV_KEY, BTN_RIGHT, 0); // Nhả phải
                    }
                    if (packet.scroll != 0) {
                        emit_event(uinput_fd, EV_REL, REL_WHEEL, packet.scroll);
                    }

                    // Chốt Sync 1 lần duy nhất để OS nhận diện toàn bộ hành động
                    emit_event(uinput_fd, EV_SYN, SYN_REPORT, 0);
                }
            }
            // Đóng khối khóa an toàn
        }
    }
    ioctl(uinput_fd, UI_DEV_DESTROY);
    close(uinput_fd);
    close(sock);
    return 0;
}
