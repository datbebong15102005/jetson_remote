#include "include/jetson_remote.hpp"

int main(int argc, char *argv[]) {
    // Đăng ký bắt sự kiện Ctrl+C (SIGINT) để dọn dẹp trước khi chết
    signal(SIGINT, JetsonRemote::cleanup_and_exit);
    
    // Nếu người dùng tự truyền biến môi trường, lấy luôn làm dữ liệu
    if (argc > 1) {
        JetsonRemote::target_display = argv[1];
    }

    // Nếu không chỉ định, tìm ở trong biến môi trường hệ thống
    else {
        const char* env_disp = getenv("DISPLAY");
        if (env_disp != nullptr && strlen(env_disp) > 0) {
            JetsonRemote::target_display = env_disp;
        }
    }

    // Lấy IP Laptop
    if (argc > 2) {
        JetsonRemote::target_ip = argv[2];
    }
    
    // Lấy Bitrate
    if (argc > 3) {
        JetsonRemote::target_bitrate = argv[3];
    }

    std::cout << "[+] Thông số cấu hình:\n";
    std::cout << "        Màn hình : " << JetsonRemote::target_display << "\n";
    std::cout << "        Bắn tới IP:   " << JetsonRemote::target_ip << "\n";
    std::cout << "        Bitrate:      " << JetsonRemote::target_bitrate << " bps\n";

    // Chạy luồng giám sát tegrastats để lấy dữ liệu phần cứng liên tục
    std::thread stats_thread(JetsonRemote::tegrastats_worker);
    stats_thread.detach();
    // Kích hoạt Web Server chạy ở một luồng riêng biệt
    std::thread web_thread(JetsonRemote::start_web_server);
    web_thread.detach();

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(5001);
    bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));

    std::cout << "[*] Mouse Server đang chờ lệnh cấu hình...\n";

    // Gọi hàm khởi động GStreamer ngay lúc mới mở tool
    // JetsonRemote::restart_gstreamer();

    // Bật Camera giám sát màn hình X11 chạy ngầm
    std::thread monitor_thread(JetsonRemote::monitor_resolution);
    monitor_thread.detach();

    MouseAndKeyboardPacket packet;
    
    // 
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // Cứ 1 giây là hàm recvfrom phải tự động tỉnh dậy nhả luồng 1 lần
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    // Biến quản lý thời gian kết nối
    auto last_packet_time = std::chrono::steady_clock::now();
    int current_w = 0, current_h = 0; // Để tránh tạo lại chuột vô tội vạ

    while (true) {
        // Kiểm tra Timeout: Nếu đang stream mà quá 5 giây chưa nhận được gì thì tắt GStreamer!
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_packet_time).count();
        
        if (JetsonRemote::is_streaming && duration > 5) {
            JetsonRemote::stop_gstreamer();
        }

        if (recvfrom(sock, &packet, sizeof(MouseAndKeyboardPacket), 0, (struct sockaddr *)&client_addr, &client_len) > 0) {
            // Debug gói tin nhận được
            //std::cout << "Nhận được: x: " << packet.x << " | y: " << packet.y 
            //          << " | click: " << packet.click << " | scroll: " << packet.scroll 
            //          << " | signal: " << packet.signal << "\n";

            // Cập nhật lại thời gian nhận gói tin mới nhất
            last_packet_time = std::chrono::steady_clock::now();

            // Dịch IP của Laptop từ mã nhị phân sang chuỗi
            std::string sender_ip = inet_ntoa(client_addr.sin_addr);

            // Kiểm tra lệnh từ Laptop
            if (packet.signal == 999) {
            // Chỉ bật GStreamer nếu chưa bật HOẶC đổi IP
                if (!JetsonRemote::is_streaming || JetsonRemote::target_ip != sender_ip) {
                    std::cout << "\n[!] Laptop (Client) " << sender_ip << " đã yêu cầu Remote!\n";
                    JetsonRemote::target_ip = sender_ip; 
                    JetsonRemote::restart_gstreamer(); 
                    JetsonRemote::is_streaming = true;
                }
                
                // Chỉ tạo lại chuột nếu độ phân giải thay đổi
                if (packet.x != current_w || packet.y != current_h) {
                    std::cout << "[*] Cấu hình lại chuột ảo: " << packet.x << "x" << packet.y << "\n";
                    JetsonRemote::init_virtual_mouse(packet.x, packet.y);
                    current_w = packet.x;
                    current_h = packet.y;
                }
                continue;
            }

            // Lệnh ngắt kết nối từ Laptop
            if (packet.signal == 998) {
                std::cout << "\n[!] Laptop (Client) " << sender_ip << " đã ngắt kết nối!\n";
                
                if (JetsonRemote::is_streaming) {
                    JetsonRemote::stop_gstreamer(); 
                }
                
                current_w = 0; 
                current_h = 0; 
                continue; 
            }

            if (packet.signal == 888) {
                std::cout << "\n[!] Nhận được deadlock signal! \n";
                JetsonRemote::restart_gstreamer(); // Gọi quản gia ra dọn dẹp và bật lại
                continue;
            }

            // Nếu chưa được cấu hình mà đã nhận data chuột thì bỏ qua
            if (JetsonRemote::uinput_fd < 0) continue;

            // Mở khối khóa an toàn
            {
                std::lock_guard<std::mutex> lock(JetsonRemote::mouse_mtx);

                if (packet.is_keyboard == 1) 
                {
                    JetsonRemote::emit_event(JetsonRemote::uinput_fd, EV_KEY, packet.keycode, packet.keystate);
                    JetsonRemote::emit_event(JetsonRemote::uinput_fd, EV_SYN, SYN_REPORT, 0);
                }

                else 
                {
                    // Bắn tọa độ X, Y vào Kernel
                    JetsonRemote::emit_event(JetsonRemote::uinput_fd, EV_ABS, ABS_X, packet.x);
                    JetsonRemote::emit_event(JetsonRemote::uinput_fd, EV_ABS, ABS_Y, packet.y);

                    // Bắn Click chuẩn bài của Linux
                    if (packet.click == 1) {
                        JetsonRemote::emit_event(JetsonRemote::uinput_fd, EV_KEY, BTN_LEFT, 1); // Giữ chuột trái
                    } else if (packet.click == 2) {
                        JetsonRemote::emit_event(JetsonRemote::uinput_fd, EV_KEY, BTN_RIGHT, 1); // Giữ chuột phải
                    } else if (packet.click == 0) {
                        JetsonRemote::emit_event(JetsonRemote::uinput_fd, EV_KEY, BTN_LEFT, 0); // Nhả trái
                        JetsonRemote::emit_event(JetsonRemote::uinput_fd, EV_KEY, BTN_RIGHT, 0); // Nhả phải
                    }
                    if (packet.scroll != 0) {
                        JetsonRemote::emit_event(JetsonRemote::uinput_fd, EV_REL, REL_WHEEL, packet.scroll);
                    }

                    // Chốt Sync 1 lần duy nhất để OS nhận diện toàn bộ hành động
                    JetsonRemote::emit_event(JetsonRemote::uinput_fd, EV_SYN, SYN_REPORT, 0);
                }
            }
            // Đóng khối khóa an toàn
        }
    }
    ioctl(JetsonRemote::uinput_fd, UI_DEV_DESTROY);
    close(JetsonRemote::uinput_fd);
    close(sock);
    return 0;
}