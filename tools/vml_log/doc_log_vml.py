#!/usr/bin/env python3
"""Doc log VML tu HUD 2.8" (CYD) qua cap USB, giai ma ra file .txt de gui cho nguoi ho tro.

Cach dung (Windows/Mac/Linux, can Python 3):
    pip install esptool
    python doc_log_vml.py            # tu tim cong COM
    python doc_log_vml.py COM5       # chi dinh cong

Log nam o 64 KB dau khe firmware KHONG chay (thuong ota_1 = 0x200000; neu chay ota_1 thi o ota_0 = 0x10000).
Script doc ca hai, khe nao co ban ghi hop le thi giai ma. Doc flash KHONG xoa log, HUD van chay binh thuong sau khi rut cap.
Ban ghi 8 byte: [loai][giay 24 bit][d0 d1 d2][0]
  1 goi 0101 ngan (25 byte, 3 byte du lieu "86 A0 xx")   2 goi 0101 29 byte (byte 14, 15, 20 = bien hien tai)
  3 VML noi   4 VML ngat   5 chi dan (d0: 1 re / 2 bien tiep / 3 canh bao, d1d2 = met/10)   0x10 bat HUD
"""
import datetime, os, subprocess, sys, tempfile

SLOTS = {"ota_1": 0x200000, "ota_0": 0x10000}
SIZE = 0x20000            # 64 KB ban ghi + 64 KB chu (tu v10)
NAMES = {1: "goi_ngan", 2: "bien_29B", 3: "NOI", 4: "NGAT", 5: "chi_dan", 0x10: "BAT_HUD"}
GUIDE = {1: "re", 2: "bien_tiep", 3: "canh_bao"}


def read_flash(port, addr, out):
    cmd = [sys.executable, "-m", "esptool", "--chip", "esp32"]
    if port:
        cmd += ["--port", port]
    cmd += ["read_flash", hex(addr), hex(SIZE), out]
    subprocess.run(cmd, check=True)


def decode(data):
    rows, bad = [], 0
    for off in range(0, len(data), 8):
        r = data[off:off + 8]
        if r[0] == 0xFF:
            break
        if r[0] not in NAMES:
            bad += 1
            break
        sec = r[1] << 16 | r[2] << 8 | r[3]
        d = r[4:7]
        t = NAMES[r[0]]
        if r[0] == 1:
            info = f"{d[0]:02X} {d[1]:02X} {d[2]:02X}  (byte cuoi = {d[2]})"
        elif r[0] == 2:
            info = f"b14={d[0]} b15={d[1]} BIEN={d[2]}"
        elif r[0] == 5:
            info = f"{GUIDE.get(d[0], d[0])} {(d[1] << 8 | d[2]) * 10} m"
        elif r[0] == 0x10:
            info = f"lan bat {d[0] << 8 | d[1]}"
        else:
            info = ""
        rows.append(f"{sec:8d}s  {t:9s} {info}")
    return rows, bad


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else None
    tmp = tempfile.mkdtemp()
    best = None
    for name, addr in SLOTS.items():
        f = os.path.join(tmp, name + ".bin")
        print(f"== Doc {name} ({hex(addr)}) ...")
        read_flash(port, addr, f)
        raw = open(f, "rb").read()
        rows, bad = decode(raw[:0x10000])
        txt = raw[0x10000:].split(b"\xff", 1)[0]
        if txt[:1].isdigit():
            rows.append("")
            rows.append("---- CHU (giay noi-dung): CB canh bao/camera/tram, RE re, CHU chu, LA goi la ----")
            rows += txt.decode("utf-8", "replace").splitlines()
        print(f"   {len(rows)} ban ghi hop le")
        if rows and not bad and (best is None or len(rows) > len(best[1])):
            best = (name, rows, f)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M")
    out = f"log-vml-28-{stamp}.txt"
    with open(out, "w", encoding="utf-8") as o:
        if not best:
            o.write("Khong thay log VML hop le (HUD chua ghi gi hoac chua co ban v8/v9).\n")
        else:
            o.write(f"Log VML tu khe {best[0]}, {len(best[1])} ban ghi. Cot 1 = giay ke tu luc bat HUD.\n\n")
            o.write("\n".join(best[1]) + "\n")
            raw = f"log-vml-28-{stamp}.bin"
            with open(best[2], "rb") as s, open(raw, "wb") as d:
                d.write(s.read())
            print(f"Ban tho: {raw}")
    print(f"\nXONG: {os.path.abspath(out)}\nGui file nay (va file .bin) cho nguoi ho tro.")


if __name__ == "__main__":
    main()
