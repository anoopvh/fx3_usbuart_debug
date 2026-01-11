import serial
import time
import sys
import argparse
import csv
from datetime import datetime

# --- SETTINGS ---
DEFAULT_PORT = "COM15"
BAUDRATE = 115200
TIMEOUT = 1
DELAY_AFTER_SEND = 0.1
LOOP_DELAY = 0

hex_data = "5A 00 00 00 00 00 F6 96 00 00"
CSV_FILE = "serial_log.csv"


def hex_to_bytes(hex_string):
    return bytes.fromhex(hex_string)


def parse_arguments():
    parser = argparse.ArgumentParser(description="Serial Port Tester with Error Dashboard")
    parser.add_argument(
        "-p", "--port",
        type=str,
        default=DEFAULT_PORT,
        help=f"Serial port to use (default: {DEFAULT_PORT})"
    )
    return parser.parse_args()


def init_csv():
    try:
        with open(CSV_FILE, "x", newline="") as f:
            writer = csv.writer(f)
            writer.writerow([
                "timestamp",
                "seq_no",
                "tx_hex",
                "rx_hex",
                "error_type"
            ])
    except FileExistsError:
        pass


def main():
    args = parse_arguments()
    port_name = args.port

    init_csv()
    target_payload = hex_to_bytes(hex_data)

    stats = {
        "total_sent": 0,
        "total_errors": 0,
        "err_no_resp": 0,
        "err_mismatch": 0
    }

    print("----------------------------------------------------------------")
    print("  [TIMESTAMP]           | #MSG  | ERROR TYPE")
    print("----------------------------------------------------------------")

    try:
        ser = serial.Serial(port_name, BAUDRATE, timeout=TIMEOUT)

        while True:
            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
            seq_no = stats["total_sent"] + 1

            # --- SEND ---
            ser.write(target_payload)
            stats["total_sent"] = seq_no

            time.sleep(DELAY_AFTER_SEND)

            # --- RECEIVE ---
            response = ser.read_all()

            tx_hex = target_payload.hex().upper()
            rx_hex = response.hex().upper() if response else ""

            error_type = ""

            if not response:
                stats["err_no_resp"] += 1
                stats["total_errors"] += 1
                error_type = "NO RESPONSE"

            elif response != target_payload:
                stats["err_mismatch"] += 1
                stats["total_errors"] += 1
                error_type = "DATA MISMATCH"

            # --- LOG ERROR TO SCREEN ---
            if error_type:
                print(
                    f"\n  [{timestamp}] | #{seq_no:<4} | {error_type}"
                )

            # --- WRITE CSV ---
            with open(CSV_FILE, "a", newline="") as f:
                writer = csv.writer(f)
                writer.writerow([
                    timestamp,
                    seq_no,
                    tx_hex,
                    rx_hex,
                    error_type
                ])

            # --- STATUS LINE ---
            total = stats["total_sent"]
            pct_error = (stats["total_errors"] / total) * 100

            status_line = (
                f"\r[STATUS] Sent: {total} | "
                f"Errors: {pct_error:.1f}% ({stats['total_errors']}) | "
                f"NoResp: {stats['err_no_resp']} | "
                f"Mismatch: {stats['err_mismatch']} "
            )

            sys.stdout.write(f"{status_line:<100}")
            sys.stdout.flush()

            time.sleep(LOOP_DELAY)

    except KeyboardInterrupt:
        print("\n\nTest stopped by user.")

    except serial.SerialException as e:
        print(f"\n\nSerial Error: {e}")

    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()
            print("Port closed.")


if __name__ == "__main__":
    main()
