# rs4-elrs

Remote PTZ untuk gimbal **DJI RS4**, guna **ExpressLRS (ELRS)** untuk jarak 100–500 m.

```
[Joystick + 13 butang + TFT]
          │
     ESP32-S3 (remote) ──CRSF──► Ranger Micro ))) ELRS 2.4 GHz ((( Nano RX ──CRSF──► ESP32-C3 #1 ──CAN──► DJI RS4
                                       ▲                                                   │             (+ Focus Pro Motor = zoom)
                                       └───────────── telemetri: LQ, RSSI, status ◄────────┘
                                                                                 ESP32-C3 #2 ──WiFi──► Sony A7R II (fasa 4)
```

## Apa yang ia buat

| Kawalan | Fungsi |
| --- | --- |
| Joystick X / Y | Pan / tilt (kelajuan, dengan deadzone, expo, ramp) |
| Twist joystick | Zoom, melalui DJI Focus Pro Motor |
| Butang atas joystick | Tekan = movement speed 1→2→3→4. Tekan lama 1 s = recall P1 |
| P1–P4 | Tekan = recall. Tekan 2 s = save (pan, tilt dan zoom) |
| MOVE 1–4 | Pilih movement speed |
| ZOOM 1–4 | Pilih had kelajuan zoom |
| WiFi | Buka halaman tetapan di telefon (`RS4-Remote` / `rs4remote`, http://192.168.4.1) |

- **Recall** bergerak smooth (ease in / ease out) pada recall speed yang dipilih di halaman tetapan.
- **Sentuh joystick masa recall** akan batalkan recall serta-merta.
- **Failsafe:** kalau link ELRS putus 0.3 s, gimbal berhenti.
- **Skrin:** movement speed, zoom speed, LQ/RSSI, status penerima, preset yang tersimpan, dan notis seperti `SAVED P2`.
- **Preset disimpan dalam flash penerima**, jadi kekal walaupun power dimatikan.

## Isi repo

| Folder | Isi |
| --- | --- |
| [`remote/`](remote) | Firmware ESP32-S3 (pegang tangan) |
| [`receiver/`](receiver) | Firmware ESP32-C3 #1 (atas RS4) |
| [`camera/`](camera) | ESP32-C3 #2 untuk ISO/EV Sony (fasa 4, belum ditulis) |
| [`docs/wiring.md`](docs/wiring.md) | Jadual wiring semua komponen, termasuk bus GND/3V3/5V |
| [`docs/debug.md`](docs/debug.md) | Langkah uji satu-satu dengan Serial Monitor |
| [`docs/elrs-setup.md`](docs/elrs-setup.md) | Bind dan tetapan Ranger Micro + Nano RX |
| [`docs/protocol.md`](docs/protocol.md) | Peta channel ELRS dan arahan DJI R SDK yang dipakai |
| [`test/`](test) | Ujian protokol (CRC DJI, CRSF) yang jalan di PC / GitHub Actions |

## Cara mula (ringkas)

1. **Arduino IDE 2.x** → Boards Manager → pasang **esp32 by Espressif Systems** (versi 3.x).
2. Library Manager → pasang **Adafruit GFX Library** dan **Adafruit ST7735 and ST7789 Library** (untuk remote sahaja).
3. Wiring ikut [`docs/wiring.md`](docs/wiring.md).
4. Buka dan upload:

   | Sketch | Board dalam Arduino IDE | Tetapan penting |
   | --- | --- | --- |
   | `remote/remote.ino` | **ESP32S3 Dev Module** | USB CDC On Boot: **Enabled** |
   | `receiver/receiver.ino` | **ESP32C3 Dev Module** | USB CDC On Boot: **Enabled** |

5. Ikut fasa uji dalam [`docs/debug.md`](docs/debug.md): remote dulu, kemudian link ELRS, kemudian CAN ke RS4.

Fail `.bin` siap compile ada dalam tab **Actions** di GitHub (artifact `remote-bin` dan `receiver-bin`) setiap kali kod dikemas kini.

## Status dan perkara yang perlu disahkan di gimbal sebenar

Kod ini compile dan protokolnya diuji di PC, tetapi belum diuji pada RS4 sebenar. Perkara yang perlu disahkan masa fasa 3:

- [ ] RS4 terima arahan kelajuan dan posisi dari CAN tanpa device ID yang diluluskan DJI.
- [ ] Arahan focus motor (`0x0E 0x12`) menggerakkan Focus Pro Motor pada gelang zoom.
- [ ] Sudut untuk preset: `ANGLE_SOURCE` attitude (default) atau joint, mana yang lebih tepat.
- [ ] Telemetri half-duplex pada Ranger Micro (LQ/RSSI di skrin). Kalau tak keluar, set `CRSF_TELEMETRY 0`; kawalan tetap berfungsi.
- [ ] Voltan pin power port yang dipakai (5 V atau 8 V), sebelum sambung ke bas 5 V.

Had yang diketahui:

- RS SDK tiada arahan baca posisi focus motor. Penerima ingat posisi zoom terakhir dalam flash. Kalau gelang zoom diputar dengan tangan, posisi sebenar dan yang diingat akan berbeza sehingga twist berikutnya.
- RS SDK tidak beri bateri RS4, jadi bateri tidak dipaparkan.
- Kuasa TX dan packet rate Ranger Micro ditetapkan di ELRS web UI / Lua, bukan dari remote ini (lihat [`docs/elrs-setup.md`](docs/elrs-setup.md)).
