// BỌC GIÁP: Chờ HTML tải xong mới khởi tạo đồ thị
document.addEventListener("DOMContentLoaded", function() { 
    // Cấu hình chung cho Chart.js để phù hợp với theme tối và font chữ
    Chart.defaults.color = '#b0b0d0';
    Chart.defaults.borderColor = '#333';

    const maxPoints = 30; // Giữ lại 30 điểm trên màn hình

    // Hàm tạo đồ thị Base
    function createChart(ctxId, title, yMax, datasets) {
        return new Chart(document.getElementById(ctxId), {
            type: 'line',
            data: { labels: Array(maxPoints).fill(''), datasets: datasets },
            options: {
                responsive: true, maintainAspectRatio: false,
                animation: false, // Tắt animation để vẽ real-time không bị giật
                plugins: { title: { display: true, text: title, color: '#fff' } },
                scales: { y: { min: 0, max: yMax } }
            }
        });
    }

    // Khởi tạo 4 Đồ thị
    const cpuChart = createChart('cpuChart', 'CPU Usage (%) - 6 Cores', 100, [
        { label: 'Core 1', borderColor: '#ff4c4c', data: Array(maxPoints).fill(0), borderWidth: 2, pointRadius: 0 },
        { label: 'Core 2', borderColor: '#ff9933', data: Array(maxPoints).fill(0), borderWidth: 2, pointRadius: 0 },
        { label: 'Core 3', borderColor: '#ffff33', data: Array(maxPoints).fill(0), borderWidth: 2, pointRadius: 0 },
        { label: 'Core 4', borderColor: '#33ff33', data: Array(maxPoints).fill(0), borderWidth: 2, pointRadius: 0 },
        { label: 'Core 5', borderColor: '#3399ff', data: Array(maxPoints).fill(0), borderWidth: 2, pointRadius: 0 },
        { label: 'Core 6', borderColor: '#cc33ff', data: Array(maxPoints).fill(0), borderWidth: 2, pointRadius: 0 }
    ]);

    const gpuChart = createChart('gpuChart', 'GPU Usage (%)', 100, [
        { label: 'GR3D (GPU)', borderColor: '#00ff00', backgroundColor: 'rgba(0,255,0,0.1)', fill: true, data: Array(maxPoints).fill(0), pointRadius: 0 }
    ]);

    const ramChart = createChart('ramChart', 'Memory Usage (MB)', null, [
        { label: 'RAM', borderColor: '#ff00ff', data: Array(maxPoints).fill(0), pointRadius: 0 },
        { label: 'SWAP', borderColor: '#00ffff', data: Array(maxPoints).fill(0), pointRadius: 0 }
    ]);

    const powerChart = createChart('powerChart', 'Power Consumption (Watt)', null, [
        { label: 'TOTAL (W)', borderColor: '#ffff33', data: Array(maxPoints).fill(0), pointRadius: 0 },
        { label: 'CPU+GPU (VDD_CV)', borderColor: '#ff4c4c', data: Array(maxPoints).fill(0), pointRadius: 0 }, // Dành cho Xavier/Orin
        { label: 'CPU (W)', borderColor: '#ff4c4c', data: Array(maxPoints).fill(0), pointRadius: 0 }, // Dành cho Nano/TX2
        { label: 'GPU (W)', borderColor: '#00ff00', data: Array(maxPoints).fill(0), pointRadius: 0 } // Dành cho Nano/TX2
    ]);

    // Hàm đẩy data mới vào đồ thị (dịch số cũ sang trái)
    function updateChart(chart, newDataArray) {
        chart.data.datasets.forEach((dataset, i) => {
            dataset.data.push(newDataArray[i]);
            dataset.data.shift(); // Xóa điểm cũ nhất
        });
        chart.update();
    }

    // Hàm bóc tách tegrastats (Hỗ trợ tất cả các dòng Jetson)
    function parseTegrastats(raw) {
        try {
            // 1. RAM & SWAP
            const ramMatch = raw.match(/RAM (\d+)\/(\d+)MB/);
            const swapMatch = raw.match(/SWAP (\d+)\/(\d+)MB/);
            if (ramMatch && swapMatch) {
                let ramCur = parseInt(ramMatch[1]);
                let ramMax = parseInt(ramMatch[2]); // Bắt mốc Max RAM
                let swapCur = parseInt(swapMatch[1]);
                let swapMax = parseInt(swapMatch[2]); // Bắt mốc Max SWAP

                // Lấy đỉnh cao nhất giữa RAM và SWAP làm đỉnh đồ thị
                let absoluteMax = Math.max(ramMax, swapMax);
                
                // Ép Chart.js cập nhật lại cái đỉnh trục Y
                if (ramChart.options.scales.y.max !== absoluteMax) {
                    ramChart.options.scales.y.max = absoluteMax;
                }

                // Bơm số liệu hiện tại vào vẽ
                updateChart(ramChart, [ramCur, swapCur]);
            }

            // CPU Cores
            const cpuMatch = raw.match(/CPU \[([^\]]+)\]/);
            if (cpuMatch) {
                const cores = cpuMatch[1].split(',').map(c => parseInt(c.split('%')[0]));
                while(cores.length < 6) cores.push(0);
                updateChart(cpuChart, cores);
            }

            // GPU
            const gpuMatch = raw.match(/GR3D_FREQ (\d+)%/);
            if (gpuMatch) updateChart(gpuChart, [parseInt(gpuMatch[1])]);

            // POWER (Tương thích chéo Nano/TX2 và Xavier/Orin)
            // Bắt tổng điện
            let totalMatch = raw.match(/VDD_IN (\d+)mW/) || raw.match(/POM_5V_IN (\d+)\//);
            let totalW = totalMatch ? parseInt(totalMatch[1]) / 1000 : 0;

            // Bắt điện lẻ (Tùy máy có gì lấy đó)
            let cvMatch = raw.match(/VDD_CPU_GPU_CV (\d+)mW/); // Của Xavier NX (Gộp CPU+GPU)
            let cpuMatch_pwr = raw.match(/POM_5V_CPU (\d+)\//) || raw.match(/VDD_CPU (\d+)mW/);
            let gpuMatch_pwr = raw.match(/POM_5V_GPU (\d+)\//) || raw.match(/VDD_GPU (\d+)mW/);

            // Đẩy data vào mảng (Phải đẩy đủ cả 4 dây để array không bị lệch khung hình)
            powerChart.data.datasets[0].data.push(totalW); powerChart.data.datasets[0].data.shift();

            if (cvMatch) {
                // For Jetson Xavier NX/Orin
                let cvW = parseInt(cvMatch[1]) / 1000;
                
                // Hiện dây CV, Ẩn 2 dây rời
                powerChart.data.datasets[1].hidden = false;
                powerChart.data.datasets[2].hidden = true;
                powerChart.data.datasets[3].hidden = true;

                // Bơm số liệu
                powerChart.data.datasets[1].data.push(cvW); powerChart.data.datasets[1].data.shift();
                powerChart.data.datasets[2].data.push(0); powerChart.data.datasets[2].data.shift();
                powerChart.data.datasets[3].data.push(0); powerChart.data.datasets[3].data.shift();
            } else {
                // For Jetson Nano/TX2
                let cpuW = cpuMatch_pwr ? parseInt(cpuMatch_pwr[1]) / 1000 : 0;
                let gpuW = gpuMatch_pwr ? parseInt(gpuMatch_pwr[1]) / 1000 : 0;

                // Ẩn dây CV, Hiện 2 dây rời
                powerChart.data.datasets[1].hidden = true;
                powerChart.data.datasets[2].hidden = false;
                powerChart.data.datasets[3].hidden = false;

                // Bơm số liệu
                powerChart.data.datasets[1].data.push(0); powerChart.data.datasets[1].data.shift();
                powerChart.data.datasets[2].data.push(cpuW); powerChart.data.datasets[2].data.shift();
                powerChart.data.datasets[3].data.push(gpuW); powerChart.data.datasets[3].data.shift();
            }
            
            powerChart.update(); // Bắt đầu vẽ

            // Hardware Accelerators (Tàng hình khi Idle)
            const nvenc = raw.match(/NVENC (\S+)/); // \S+ là bắt mọi ký tự cho đến khi gặp dấu cách
            const nvdec = raw.match(/NVDEC (\S+)/);
            const nvdla = raw.match(/NVDLA\d? (\S+)/);
            
            document.getElementById('nvenc-val').innerText = nvenc ? (nvenc[1].includes('%') ? nvenc[1] : `${nvenc[1]} MHz`) : "0% (Idle/Off)";
            document.getElementById('nvdec-val').innerText = nvdec ? (nvdec[1].includes('%') ? nvdec[1] : `${nvdec[1]} MHz`) : "0% (Idle/Off)";
            document.getElementById('nvdla-val').innerText = nvdla ? (nvdla[1].includes('%') ? nvdla[1] : `${nvdla[1]} MHz`) : "0% (Idle/Off)";

        } catch (e) {
            console.error("Lỗi parse tegrastats: ", e);
        }
    }

    // Vòng lặp Fetch API mỗi 2 giây
    setInterval(() => {
        fetch('/api/stats')
            .then(res => res.json())
            .then(data => {
                if(data.status === "success") {
                    parseTegrastats(data.data);
                }
            })
            .catch(err => console.error("Mất kết nối API!", err));
    }, 2000);
});