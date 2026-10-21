# Protokol

## Peta channel ELRS

ELRS hantar CH1–CH4 pada resolusi 10-bit penuh dalam **semua** switch mode (Hybrid, Wide, Full Res). Jadi apa-apa yang mesti tepat diletak di situ. Sumber: [`remote/link_map.h`](../remote/link_map.h).

| Channel | Isi | Nilai |
| --- | --- | --- |
| CH1 | Pan | −1..1, dah didarab movement speed 1–4 |
| CH2 | Tilt | −1..1, sama |
| CH3 | Zoom | −1..1, dah didarab zoom speed 1–4 |
| CH4 | Arahan preset | langkah 50 tick: `arahan × 2 + toggle` |
| CH5 | Arm | Sentiasa tinggi |
| CH6 | Recall speed | indeks 0–15 sebagai nilai analog (`RECALL_DPS`) |
| CH7 | Kamera ISO | Fasa 4 |
| CH8 | Kamera EV | Fasa 4 |

Arahan CH4: 0 tiada, 1–4 recall P1–P4, 5–8 save P1–P4. Bit toggle bertukar setiap kali butang ditekan, jadi tekan butang sama dua kali tetap dikesan. Penerima perlukan nilai baru yang sama dalam 2 frame berturut-turut sebelum bertindak, dan tidak bertindak pada nilai pertama selepas link kembali.

Telemetri balik (flight-mode frame `0x21`): `"<STATE> <mask>"`, contoh `OK 1101`. STATE: `OK`, `NOCAN`, `SAVED1..4`, `MOVE1..4` (sedang recall), `EMPTY1..4`, `ERR1..4`. Mask: P1..P4, `1` = tersimpan.

## DJI R SDK (CAN)

Rujukan: DJI R SDK Protocol and User Interface. CAN 1 Mbit/s, ID standard. ESP32 hantar pada **0x223**, gimbal jawab pada **0x222**. Paket dipotong menjadi frame CAN 8 bait mengikut turutan.

```
AA | len(10b)+ver(6b) | cmdType | enc | res×3 | seq×2 | crc16×2 | cmdSet | cmdId | data… | crc32×4
```

- CRC-16: poly 0x8005, init 0xC55C, reflected, xorout 0.
- CRC-32: poly 0x04C11DB7, init 0xC55C0000, reflected, xorout 0.
- Diuji dengan contoh paket dalam dokumen DJI: [`test/test_protocol.cpp`](../test/test_protocol.cpp).

| Arahan | Set/ID | Dipakai untuk |
| --- | --- | --- |
| Position control | 0x0E 0x00 | Recall: posisi mutlak setiap 50 ms ikut lengkung smoothstep |
| Speed control | 0x0E 0x01 | Joystick pan/tilt, dihantar setiap 40 ms (setiap satu tahan 0.5 s) |
| Get angle | 0x0E 0x02 | Sudut untuk save dan permulaan recall, setiap 100 ms |
| Push enable / data | 0x0E 0x07 / 0x08 | Sandaran sudut kalau gimbal sokong push |
| Recenter | 0x0E 0x0E | Arahan konsol `c` |
| Focus motor | 0x0E 0x12 | Zoom (posisi 0–4095) |
