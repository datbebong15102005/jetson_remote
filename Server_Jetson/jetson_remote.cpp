#include "include/jetson_remote.hpp"

namespace JetsonRemote {
    int uinput_fd = -1; // Biến toàn cục giữ file mô phỏng chuột
    std::mutex mouse_mtx; // Ổ khóa bảo vệ chuột
    std::string target_display = ":0"; // Biến toàn cục giữ tên màn hình
    std::string target_ip = "127.0.0.1"; 
    std::string target_bitrate = "8000000";
    bool is_streaming = false; // Biến cờ để đánh dấu trạng thái streaming

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

    // Hàm tự động kích hoạt khi bấm Ctrl+C
    void cleanup_and_exit(int signum) {
        std::cout << "\n[!] Nhận lệnh đóng ứng dụng. Đang dọn dẹp ...\n";
        
        // Phá hủy chuột ảo
        if (uinput_fd >= 0) {
            ioctl(uinput_fd, UI_DEV_DESTROY);
            close(uinput_fd);
        }
        
        // Yêu cầu GStreamer tự sát nhẹ nhàng bằng kill -15 (SIGTERM)
        system("if [ -f /tmp/jetson_remote_gst.pid ]; then kill -15 $(cat /tmp/jetson_remote_gst.pid) 2>/dev/null; rm /tmp/jetson_remote_gst.pid; fi");
        
        std::cout << "[+] Shutting down...\n";
        system("pkill -f 'tegrastats'"); // Dừng luôn cả script giám sát tegrastats
        exit(0);
    }

    void restart_gstreamer() {
        std::cout << "\n[*] Đang kiểm tra và dọn dẹp luồng GStreamer cũ...\n";
        
        // Chỉ Kill đúng cái tiến trình có mã PID lưu trong file tạm (nếu file tồn tại)
        system("if [ -f /tmp/jetson_remote_gst.pid ]; then kill -15 $(cat /tmp/jetson_remote_gst.pid) 2>/dev/null; rm /tmp/jetson_remote_gst.pid; fi"); 
        
        sleep(2); 
        
        std::cout << "[*] Khởi động luồng GStreamer mới...\n";
        
        // Dùng "echo $!" để lấy PID của tiến trình vừa chạy ngầm và lưu vào file
        std::string gst_cmd = "nohup gst-launch-1.0 ximagesrc display-name=" + target_display + 
                            " use-damage=0 ! video/x-raw,framerate=60/1 ! nvvidconv ! 'video/x-raw(memory:NVMM),format=NV12' ! nvv4l2h264enc insert-sps-pps=true maxperf-enable=1 preset-level=1 control-rate=1 bitrate=" + target_bitrate + 
                            " profile=4 ! rtph264pay mtu=1200 config-interval=1 ! udpsink host=" + target_ip + 
                            " port=5000 buffer-size=2147483647 > /dev/null 2>&1 & echo $! > /tmp/jetson_remote_gst.pid";

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
                int w = 1280, h = 720;
                while (fgets(line, sizeof(line), fp)) {
                    // Tìm dòng có chứa "current " để bóc tách độ phân giải hiện tại
                    char* current_ptr = strstr(line, "current ");
                    if (current_ptr) { 
                        // Bóc tách: "current 1280 x 720" thành w=1280, h=720
                        if (sscanf(current_ptr, "current %d x %d", &w, &h) == 2) break; 
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
    
    std::string latest_tegrastats = "Waiting for data...";
    std::mutex stats_mtx;

    // Luồng công nhân chuyên đọc tegrastats trực tiếp từ Kernel
    void tegrastats_worker() {
        // Dùng stdbuf để chống kẹt ống nước (pipe buffer)
        FILE* pipe = popen("stdbuf -oL tegrastats", "r");
        if (!pipe) {
            std::cout << "[!] Lỗi: Không thể chạy tegrastats!\n";
            return;
        }

        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            std::string line = buffer;
            if (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                line.pop_back(); 
            }

            {
                std::lock_guard<std::mutex> lock(stats_mtx);
                latest_tegrastats = line; // Ghi thẳng vào RAM
            }

            // std::cout << "[DEBUG] Data sạch: " << line << "\n";
        }
        pclose(pipe);
    }

    // Backend Web gọi hàm để lấy string từ RAM
    std::string get_tegrastats_string() {
        std::lock_guard<std::mutex> lock(stats_mtx);
        return latest_tegrastats;
    }

    // Web Server để hiển thị Dashboard trạng thái 
    void start_web_server() {
        httplib::Server svr;

        // Trỏ thư mục gốc vào folder "web". Bất kỳ ai truy cập IP:8080 đều sẽ tự load file index.html trong này!
        auto ret = svr.set_mount_point("/", "./web");
        if (!ret) {
            std::cout << "[!] Lỗi: Không tìm thấy thư mục ./web! Giao diện sẽ không hiển thị được.\n";
        }

        // Tạo Endpoint API trả về dữ liệu Tegrastats ở dạng JSON
        svr.Get("/api/stats", [](const httplib::Request &, httplib::Response &res) {
            std::string raw_stats = get_tegrastats_string();
            
            // Đóng gói data thành JSON
            std::string json_response = "{\"status\": \"success\", \"data\": \"" + raw_stats + "\"}";
            
            // Trả về cho frontend
            res.set_content(json_response, "application/json");
        });

        std::cout << "\n[+] Web Backend API đang chạy tại cổng 8080...\n";
        svr.listen("0.0.0.0", 8080);
    }
    void stop_gstreamer() {
        std::cout << "[!] Phát hiện mất kết nối. Đang tắt GStreamer để tiết kiệm điện...\n";
        system("if [ -f /tmp/jetson_remote_gst.pid ]; then kill -15 $(cat /tmp/jetson_remote_gst.pid) 2>/dev/null; rm /tmp/jetson_remote_gst.pid; fi");
        is_streaming = false;
    }
}