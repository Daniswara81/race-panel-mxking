import serial
import time
import sys

PORT = 'COM5'
BAUDRATE = 115200

print("=" * 60)
print(f"  PENGUJIAN KABEL TUNING RED LEO (LOOPBACK TESTER)")
print(f"  Port: {PORT} | Chip: FTDI FT232R")
print("=" * 60)
print("Hubungkan (jumper/sentuhkan) 2 pin di soket hitam:")
print("Contoh: Sentuhkan ujung pin BIRU dan pin HIJAU pakai kawat / penjepit kertas.")
print("Script akan otomatis mendeteksi ketika data memantul balik!\n")

try:
    ser = serial.Serial(PORT, BAUDRATE, timeout=0.05)
except Exception as e:
    print(f"[ERROR] Gagal membuka {PORT}: {e}")
    sys.exit(1)

test_msg = b"REDLEO_LOOPBACK_OK\n"
counter = 0

try:
    while True:
        counter += 1
        ser.reset_input_buffer()
        ser.write(test_msg)
        time.sleep(0.08)
        received = ser.read(len(test_msg))
        
        if test_msg in received or received.startswith(b"REDLEO"):
            print(f"\r>>> [SUKSES BERHASIL] PANTULAN DATA TERDETEKSI! <<<               ")
            print(f"    Data yang memantul: {received.decode('ascii', errors='ignore').strip()}")
            print(f"    KESIMPULAN: Kedua pin yang Anda sambungkan saat ini adalah TX dan RX!\n")
            time.sleep(0.5)
        else:
            indicator = ["-", "\\", "|", "/"][counter % 4]
            print(f"\r[{indicator}] Menunggu pin TX & RX disentuhkan... (Tekan Ctrl+C untuk keluar)", end="", flush=True)
            time.sleep(0.1)

except KeyboardInterrupt:
    print("\n\nPengujian dihentikan.")
finally:
    ser.close()
