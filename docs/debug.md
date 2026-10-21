# Uji langkah demi langkah

Uji satu bahagian pada satu masa. Setiap fasa guna **Serial Monitor** (115200 baud, line ending **Newline**). Jangan pasang RS4 sampai fasa 3.

Tetapan Arduino IDE untuk kedua-dua board: **USB CDC On Boot: Enabled**, supaya Serial Monitor jalan melalui kabel USB.

---

## Fasa 1 — Remote sahaja (tanpa ELRS, tanpa CAN)

Dalam `remote/config.h`:

```c
#define ENABLE_CRSF 0
#define ENABLE_TFT  1
#define DEBUG_SERIAL 1
```

Upload `remote/remote.ino`, buka Serial Monitor. Setiap 0.25 s keluar satu baris:

```
raw X2051 Y2040 Z2048 | out pan+0.00 tilt+0.00 zoom+0.00 | MOVE2 ZOOM2 | no link | rx "" | crsf ok0 bad0
```

Semak:

- [ ] Gerak joystick kiri/kanan: `raw X` berubah dari ~0 ke ~4095. Atas/bawah: `raw Y`. Twist: `raw Z`.
- [ ] `out pan/tilt/zoom` jadi 0.00 bila joystick dilepas. Kalau tidak, buat kalibrasi (WiFi → Set centre).
- [ ] Setiap butang cetak `[BTN] P1 down` / `[BTN] P1 up`.
- [ ] Tekan P1 sekejap: `[CMD] RECALL P1`. Tahan 2 s: `[CMD] SAVE P1`, skrin tunjuk `SAVE P1`.
- [ ] MOVE 1–4 dan ZOOM 1–4 tukar nombor besar di skrin. Butang joystick pusing MOVE 1→4.
- [ ] Butang WiFi: sambung telefon ke `RS4-Remote` (kata laluan `rs4remote`), buka http://192.168.4.1. Bar Live bergerak bila joystick digerakkan.
- [ ] Buat kalibrasi joystick di halaman tu, kemudian Simpan tetapan.

Skrin kosong? Semak wiring SCL→GPIO12, SDA→GPIO11, RES/DC/CS, dan VCC/BLK ke 3V3.

## Fasa 2 — Link ELRS (penerima tanpa CAN)

1. Bind Ranger Micro dan Nano RX dulu: [`elrs-setup.md`](elrs-setup.md).
2. `remote/config.h`: `ENABLE_CRSF 1`, `CRSF_TELEMETRY 1`.
3. `receiver/config.h`: `USE_CAN 0`, `DEBUG_PRINT 1`.
4. Upload kedua-dua. Penerima di USB PC, remote guna power bank (Ranger Micro perlukan 9/12 V).

Serial Monitor **penerima**:

```
link OK pan+0.35 tilt+0.00 zoom+0.00 cmd0/0 recall30dps LQ100 -42dBm | gimbal -- ... | "NOCAN 0000"
```

Semak:

- [ ] `link OK` keluar, dan `pan/tilt/zoom` ikut joystick.
- [ ] Tekan P2 di remote: `cmd2/1`; tekan lagi P2: `cmd2/0` (bit toggle bertukar).
- [ ] Tutup remote: dalam 0.3 s jadi `link --`.

Skrin **remote**:

- [ ] Bar atas tunjuk `LQ100 -xxdBm` (hijau).
- [ ] Sudut kanan atas tunjuk `NOCAN` (status dari penerima sampai ke remote).

Kalau LQ keluar tetapi status kekal `--`, telemetri dari penerima tak sampai: semak wayar GPIO21 → RX pada Nano RX, dan telemetry ratio ELRS bukan "Off".

Kalau remote tunjuk `NO LINK` walaupun penerima terima channel, telemetri half-duplex gagal. Set `CRSF_TELEMETRY 0` di remote: kawalan tetap jalan, cuma LQ/status tidak dipaparkan.

## Fasa 3 — Penerima ke RS4 (CAN), tanpa remote

Ini fasa "tanpa butang": gimbal digerakkan dari PC melalui Serial Monitor.

1. Ikut bahagian **Ukur sebelum sambung** dalam [`wiring.md`](wiring.md).
2. `receiver/config.h`: `USE_CAN 1`. Boleh juga `USE_CRSF 0` kalau remote belum siap.
3. Penerima di USB PC (cabut wayar 5 V dari RS4, GND kekal), sambung CAN H/L, hidupkan RS4.

Taip arahan di Serial Monitor:

| Arahan | Apa yang patut berlaku |
| --- | --- |
| `help` | Senarai arahan |
| `a` | Cetak yaw/pitch/roll. Gerak gimbal dengan tangan, taip `a` lagi: nilai berubah |
| `raw` | Cetak setiap frame CAN (tekan sekali lagi untuk berhenti) |
| `v 10 0` | Pan perlahan 10°/s selama 2 s |
| `v 0 -10` | Tilt 2 s |
| `p 30 -10` | Pergi ke yaw 30°, pitch −10° dalam 3 s |
| `c` | Recenter |
| `z 1000` lalu `z 3000` | Focus Pro Motor (gelang zoom) bergerak |
| `s1` | Simpan posisi semasa sebagai P1 |
| `r1` | Kembali ke P1 (smooth) |
| `list` / `clear` | Lihat / padam preset |
| `stop` | Berhenti semua |

Bacaan status (`gimbal OK` dan `pk` naik) bermakna paket DJI yang sah sedang diterima.

Masalah biasa:

| Gejala | Punca biasa |
| --- | --- |
| `raw` tak cetak apa-apa | CAN H/L tertukar, port tiada CAN, atau RS4 belum hidup |
| `fail` naik, `pk` kekal 0 | Tiada peranti lain ACK di bas: semak GND bersama dan H/L |
| `pk` naik tapi `bad` juga naik banyak | Gangguan: wayar CAN pintal (twisted), semak perintang 120 Ω |
| `a` ada nilai, tapi `v`/`p` tak gerak | RS4 mungkin perlukan device ID DJI untuk kawalan. Catat dan maklumkan |
| `p` bergerak ke sudut lain | Cuba `ANGLE_SOURCE 0x02` (joint) dalam config.h |
| `z` tak gerak motor | Calibrate Focus Pro Motor di RS4 dulu; arahan focus SDK mungkin tak sokong zoom |

## Fasa 4 — Semua bersama

1. `receiver/config.h`: `USE_CAN 1`, `USE_CRSF 1`. Power penerima dari RS4.
2. Remote: power bank.

Semak:

- [ ] Joystick gerak gimbal; MOVE 1–4 ubah kelajuan.
- [ ] Twist gerak zoom; ZOOM 1–4 ubah had kelajuan.
- [ ] Tahan P1 2 s: skrin `SAVED1`, kotak P1 jadi hijau.
- [ ] Gerak ke tempat lain, tekan P1: gimbal kembali dengan smooth, skrin tunjuk `MOVE1` lalu `OK`.
- [ ] Masa recall, sentuh joystick: recall berhenti, kawalan kembali ke joystick.
- [ ] Matikan remote masa gimbal bergerak: gimbal berhenti.
- [ ] Bawa remote ke jarak kerja (100–500 m), perhatikan LQ.

## Fasa 5 — Kamera (akan datang)

ESP32-C3 #2 + WiFi Sony A7R II untuk ISO/EV. Lihat [`../camera/README.md`](../camera/README.md).
