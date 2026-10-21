# Kamera (fasa 4) — belum ditulis

Rancangan:

- **ESP32-C3 Super Mini #2** di atas RS4, disambung ke penerima (C3 #1) dengan UART: C3 #1 GPIO6 → C3 #2 RX, C3 #1 GPIO7 ← C3 #2 TX, GND bersama.
- C3 #2 sambung ke WiFi Sony A7R II (app **Smart Remote Control / Smart Remote Embedded** dibuka di kamera) dan guna Sony Camera Remote API (HTTP + JSON) untuk tukar **ISO** dan **EV**.
- Remote hantar ISO/EV pada **CH7/CH8** (perlu ELRS switch mode yang beri resolusi cukup pada CH7/CH8, contoh Wide atau Full Res).
- Dummy battery NP-FW50 disyorkan kerana WiFi kamera cepat habiskan bateri.

Nota: EV compensation hanya berkesan dalam mod P/A/S, atau mod M dengan Auto ISO.
