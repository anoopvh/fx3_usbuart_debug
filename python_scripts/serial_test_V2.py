import serial
import time
import sys
import argparse
from datetime import datetime

# --- SETTINGS ---
# Default settings (can be overridden by command line args)
DEFAULT_PORT = "COM17"
BAUDRATE = 115200
TIMEOUT = 1         # Short timeout for faster loops
DELAY_AFTER_SEND = 0.1   
LOOP_DELAY = 0       

# The data we expect to send AND receive back (Loopback check)
hex_data = "5A 00 00 00 00 00 f6 96 00 00"

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

def main():
    args = parse_arguments()
    port_name = args.port

    try:
        # Open Serial Port
        ser = serial.Serial(port_name, BAUDRATE, timeout=TIMEOUT)
        print(f"Connected to {port_name} @ {BAUDRATE}. Sending: {hex_data}")
        print("----------------------------------------------------------------")
        print("  [TIMESTAMP]           | #MSG  | ERROR TYPE      | DETAILS")
        print("----------------------------------------------------------------")

        target_payload = hex_to_bytes(hex_data)
        
        # Statistics Counters
        stats = {
            "total_sent": 0,
            "total_errors": 0,
            "err_no_resp": 0,
            "err_mismatch": 0
        }

        while True:
            # 1. Send Data
            ser.write(target_payload)
            stats["total_sent"] += 1

            # 2. Wait and Read
            time.sleep(DELAY_AFTER_SEND)
            response = ser.read_all() # Reads everything currently in buffer

            # 3. Analyze Result
            error_occurred = False
            err_msg = ""
            err_detail = ""

            if not response:
                # Case: No data received at all
                stats["err_no_resp"] += 1
                stats["total_errors"] += 1
                error_occurred = True
                err_msg = "NO RESPONSE"
                err_detail = "Buffer empty"

            elif response != target_payload:
                # Case: Data received, but it doesn't match what we sent
                stats["err_mismatch"] += 1
                stats["total_errors"] += 1
                error_occurred = True
                err_msg = "DATA MISMATCH"
                # Show what we got (limited to first 20 chars to keep UI clean)
                rx_hex = response.hex().upper()
                err_detail = f"Rx: {rx_hex}" 

            # 4. Log Error (if any)
            # We use \n to move to a new line, print the error, then the loop 
            # continues and the dashboard overwrites the line below it.
            if error_occurred:
                timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                print(f"\n  [{timestamp}] | #{stats['total_sent']:<4} | {err_msg:<15} | {err_detail}")

            # 5. Calculate Percentages
            total = stats["total_sent"]
            if total > 0:
                pct_error = (stats["total_errors"] / total) * 100
                pct_no_resp = (stats["err_no_resp"] / total) * 100
                pct_mismatch = (stats["err_mismatch"] / total) * 100
            else:
                pct_error = pct_no_resp = pct_mismatch = 0.0

            # 6. Update Terminal UI (Dynamic Status Line)
            # \r moves cursor to start of line, end='' prevents new line
            status_line = (
                f"\r[STATUS] Sent: {total} | "
                f"Errors: {pct_error:.1f}% ({stats['total_errors']}) | "
                f"NoResp: {pct_no_resp:.1f}% | "
                f"Mismatch: {pct_mismatch:.1f}% "
            )
            
            # Write to stdout and flush buffer immediately
            sys.stdout.write(f"{status_line: <100}") # Padding to clear previous text
            sys.stdout.flush()

            time.sleep(LOOP_DELAY)

    except KeyboardInterrupt:
        print("\n\nTest stopped by user.")
        # print("Final Stats:", stats) # Optional: print raw dict

    except serial.SerialException as e:
        print(f"\n\nSerial Error: {e}")
        print("Check if the port is correct and not open in another program.")

    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()
            print("Port closed.")

if __name__ == "__main__":
    main()