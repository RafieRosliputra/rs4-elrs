# Wiring

Semua pin di sini sama dengan `remote/config.h` dan `receiver/config.h`. Kalau tukar pin, tukar di kedua-dua tempat.

Peraturan umum:

- Semua GND mesti bersambung dalam satu peranti (remote ada satu GND bersama, sisi RS4 ada satu GND bersama).
- Potentiometer dan skrin guna **3V3**, bukan 5 V. Pin ESP32 hanya tahan sehingga 3.3 V.
- Buat setiap bus (GND, 3V3, 5V) sebagai jalur pada perfboard. Hanya **satu wayar** dari jalur ke pin ESP32.
- Warna wayar dicadangkan: hitam = GND, merah = 3V3, oren = 5 V, kuning = 9/12 V.

---

## 1. Remote — ESP32-S3 N16R8

Pin yang **jangan** digunakan: 35/36/37 (PSRAM), 19/20 (USB), 43/44 (port COM), 48 (LED RGB), 3/45/46 (strapping).

### Joystick HKD400

| Komponen | Pin komponen | ESP32-S3 | Nota |
| --- | --- | --- | --- |
| Pot X (pan) | Hujung 1 / hujung 2 | 3V3 analog / GND analog | Cari kaki dengan multimeter (bacaan tetap ~10 kΩ) |
| Pot X (pan) | Wiper (tengah) | **GPIO1** | + kapasitor 100 nF (104) wiper→GND |
| Pot Y (tilt) | Hujung 1 / hujung 2 | 3V3 analog / GND analog | |
| Pot Y (tilt) | Wiper | **GPIO2** | + 100 nF ke GND |
| Twist (zoom) | Merah / hitam | 3V3 analog / GND analog | Sahkan dengan multimeter |
| Twist (zoom) | Putih (wiper) | **GPIO4** | + 100 nF ke GND |
| Butang joystick | Hijau 1 / hijau 2 | **GPIO5** / GND butang | |

Arah terbalik (contoh tolak atas = tilt bawah)? Tak perlu solder semula: tanda "Invert" di halaman tetapan.

### Butang (satu kaki ke GPIO, satu kaki ke GND butang)

| Butang | ESP32-S3 | Fungsi |
| --- | --- | --- |
| P1 | GPIO6 | Tekan = recall, tekan 2 s = save |
| P2 | GPIO7 | |
| P3 | GPIO13 | |
| P4 | GPIO15 | |
| MOVE 1 | GPIO16 | Movement speed |
| MOVE 2 | GPIO18 | |
| MOVE 3 | GPIO21 | |
| MOVE 4 | GPIO38 | |
| ZOOM 1 | GPIO39 | Had kelajuan zoom |
| ZOOM 2 | GPIO40 | |
| ZOOM 3 | GPIO41 | |
| ZOOM 4 | GPIO42 | |
| WiFi | GPIO0 | Sama dengan butang BOOT; jangan tekan masa hidupkan |
| (spare) | GPIO47 | |

Pull-up dalaman ESP32 dipakai, jadi tiada perintang luar.

### Skrin TFT 1.14" ST7789 (SPI, walaupun label tulis SCL/SDA)

| Pin skrin | ESP32-S3 |
| --- | --- |
| GND | GND analog |
| VCC | 3V3 skrin |
| SCL | GPIO12 (SPI clock) |
| SDA | GPIO11 (SPI MOSI) |
| RES | GPIO8 |
| DC | GPIO9 |
| CS | GPIO10 |
| BLK | 3V3 skrin |

### Ranger Micro

| Ranger Micro | Sambung ke | Nota |
| --- | --- | --- |
| S.Port / Data (module bay) | **GPIO17** | Satu wayar, half-duplex, 400 000 baud |
| GND (module bay) | GND Ranger | |
| XT30 + / − | PD trigger + / − (9 V atau 12 V) | Pasang antena **sebelum** hidupkan |

Kedudukan pin S.Port pada module bay: lihat gambar "Micro External Bay Pinout" di [ExpressLRS Radio Preparation](https://www.expresslrs.org/quick-start/transmitters/tx-prep/).

### Power remote

| Dari | Ke | Voltan |
| --- | --- | --- |
| Power bank (USB-C PD) | PD trigger | 9 V atau 12 V |
| PD trigger | Ranger Micro XT30 | 9/12 V (Ranger perlukan 6–16.8 V) |
| PD trigger | Buck Mini560 input | 9/12 V |
| Buck Mini560 output (5 V) | ESP32-S3 **5Vin** | 5 V |

Masa upload/debug dengan kabel USB ke PC, **cabut** wayar 5Vin dari buck.

### Bus yang dikongsi (remote)

| Bus | Pin ESP32-S3 | Disambung ke |
| --- | --- | --- |
| GND butang | GND kiri atas (pertama) | Kaki kedua 13 butang + butang joystick (hijau 2) |
| GND analog | GND kiri atas (kedua) | Pot X, pot Y, twist (hitam), 3× kapasitor 100 nF, TFT GND |
| GND power | GND kanan atas (sebelah 5Vin) | Buck output − (PD trigger −, buck −, XT30 − bersambung di PD trigger) |
| GND Ranger | GND kiri bawah (sebelah TX) | GND module bay Ranger Micro |
| 3V3 analog | 3V3 kanan bawah (pertama) | Pot X, pot Y, twist (merah) |
| 3V3 skrin | 3V3 kanan bawah (kedua) | TFT VCC, TFT BLK |
| 5 V | 5Vin | Buck output + |

Jumlah: 23 pin isyarat + 7 pin power = 30 pin, satu wayar setiap pin.

---

## 2. Penerima — ESP32-C3 Super Mini #1 (atas RS4)

| Komponen | Pin komponen | ESP32-C3 #1 | Nota |
| --- | --- | --- | --- |
| BetaFPV Nano RX | 5V / GND | 5V / GND | |
| BetaFPV Nano RX | TX | **GPIO20** | CRSF masuk, 420 000 baud |
| BetaFPV Nano RX | RX | **GPIO21** | Status balik ke remote |
| SN65HVD230 | 3V3 / GND | 3V3 / GND | |
| SN65HVD230 | CTX | **GPIO5** | |
| SN65HVD230 | CRX | **GPIO4** | |
| SN65HVD230 | CANH / CANL | Port RS4 (bawah) | |
| ESP32-C3 #2 | RX / TX / GND | GPIO6 / GPIO7 / GND | Fasa 4 (kamera), belum dipakai |

TX sentiasa ke RX, RX ke TX.

### Port pada RS4

**Pilihan A — port focus wheel** (pinout dari projek SBUS kau, sahkan dulu dengan multimeter):

| Pin | Isyarat |
| --- | --- |
| 1 | VCC |
| 2 | GND |
| 3 | CAN H |
| 4 | CAN L |

**Pilihan B — port RSA/NATO** (dari dokumen rasmi DJI R SDK):

| Pin | Isyarat | Nota |
| --- | --- | --- |
| 1 | VCC | **8 V ±0.4 V**, 0.8 A (puncak 1.2 A) |
| 2 | CAN L | |
| 3 | SBUS_RX | Tak dipakai |
| 4 | CAN H | |
| 5 | AD_COM | Kesan aksesori. **Sambung perintang 10–100 kΩ ke GND**, kalau tidak RS4 tak keluarkan power |
| 6 | GND | |

**Ukur sebelum sambung:**

1. Hidupkan RS4, letak probe hitam pada GND.
2. Pin VCC: kalau **5 V**, sambung terus ke bas 5 V. Kalau **8 V atau lebih**, mesti lalu buck Mini560 5 V dulu.
3. CAN H dan CAN L: masing-masing kira-kira **2.5 V**. Kalau tersalah tukar H/L, tiada kerosakan, cuma tiada data; tukar sahaja.
4. Matikan RS4, ukur rintangan CAN H ↔ CAN L:
   - ~**60 Ω**: bas dah ada terminasi. **Buang perintang 120 Ω ("121")** pada modul SN65HVD230.
   - ~**120 Ω**: kekalkan perintang pada modul.

### Bus yang dikongsi (sisi RS4)

| Bus | Sumber | Disambung ke |
| --- | --- | --- |
| 5 V | VCC port RS4 (terus kalau 5 V, lalu buck kalau 8 V) | C3 #1 5V, C3 #2 5V, Nano RX 5V |
| GND | GND port RS4 | C3 #1, C3 #2, Nano RX, SN65HVD230 |
| 3V3 | C3 #1 3V3 | SN65HVD230 3V3 sahaja |

Masa debug dengan kabel USB ke PC, **cabut wayar 5 V** dari port RS4 (GND kekal).

---

## 3. Senarai komponen

**Remote:** ESP32-S3 N16R8, joystick HKD400, 13 butang (+7 spare), TFT 1.14" ST7789, RadioMaster Ranger Micro, power bank USB-C PD, PD trigger 9/12 V, kabel XT30, buck Mini560 5 V, 3× kapasitor 100 nF (kod 104), perfboard, casing.

**Sisi RS4:** ESP32-C3 Super Mini (×2, satu untuk kamera nanti), BetaFPV ELRS Nano RX, SN65HVD230 (beli 2), buck Mini560 5 V (kalau port 8 V), kabel ke port RS4, perintang 10–100 kΩ (kalau guna RSA), DJI Focus Pro Motor.

**Kamera (fasa 4):** dummy battery NP-FW50, app Smart Remote dalam A7R II.
