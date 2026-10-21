# Tetapan ELRS (Ranger Micro + BetaFPV Nano RX)

Remote ini **tidak** guna radio handset. ESP32-S3 terus hantar CRSF ke Ranger Micro, jadi tetapan module dibuat melalui ELRS web UI.

## 1. Firmware

- Ranger Micro dan Nano RX mesti versi **major yang sama** (contoh dua-dua 3.x, atau dua-dua 4.x).
- Kemas kini dengan [ExpressLRS Configurator](https://github.com/ExpressLRS/ExpressLRS-Configurator/releases), dan masukkan **binding phrase** yang sama untuk kedua-duanya (cara paling mudah untuk bind).

## 2. Masuk web UI tanpa handset

**Ranger Micro (TX):**

1. Hidupkan Ranger Micro (antena dipasang!) dengan remote `ENABLE_CRSF 0`, atau tanpa ESP32 langsung.
2. Tunggu ~60 s. Module buka WiFi sendiri: rangkaian **ExpressLRS TX**, kata laluan **expresslrs**.
3. Buka http://10.0.0.1.

**Nano RX:**

1. Hidupkan Nano RX tanpa TX aktif. Tunggu ~60 s.
2. Rangkaian **ExpressLRS RX**, kata laluan **expresslrs**, buka http://10.0.0.1.

## 3. Tetapan yang disyorkan

| Tetapan | Nilai | Sebab |
| --- | --- | --- |
| Binding phrase | Sama di TX dan RX | Bind automatik |
| RX serial protocol | **CRSF** (default) | ESP32-C3 baca CRSF |
| RX baud | **420000** (default) | Sama dengan `CRSF_BAUD` di `receiver/config.h` |
| Telemetry ratio | **Std** atau mana-mana kecuali Off | Status `OK 1101` dan LQ perlu telemetri |
| Packet rate | Default (250 Hz) atau 100 Hz | Jarak 100–500 m selesa dengan mana-mana |
| Switch mode | Default (Hybrid) boleh | Arahan preset di CH4 (resolusi penuh dalam semua mod) |
| TX power | **250 mW**, atau 500 mW | 500 mW ≈ "80%" yang kau mahu; lebih panas dan tarik lebih arus |

Kalau pilihan packet rate / power tiada dalam web UI versi kau, set melalui ExpressLRS Configurator (build options) atau sementara pasang module pada mana-mana radio EdgeTX dan guna ELRS Lua script. Tetapan disimpan dalam module.

## 4. Pin CRSF

- Remote → Ranger Micro: pin **S.Port / Data** module bay ke GPIO17, 400 000 baud, satu wayar.
- Nano RX → penerima: TX RX → GPIO20, RX RX ← GPIO21, 420 000 baud.
