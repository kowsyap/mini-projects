# kat_uart_transfer.py
# Sends KAT test cases (MSG, CPK, SIGNATURE) one by one over UART and reads
# back a per-case result from the device.
#
# KAT file format (KEY=VALUE, one field per line, cases separated by blank
# lines or "# Case N" comment headers):
#
#   # Case 1
#   LENGTH=25
#   MSG=480CCC...
#   CSK=A1CCB9...   <- not sent
#   CPK=A0B6B9...
#   SIGNATURE=44B122...
#
# Wire protocol (matches what the device must implement on the other side):
#   For each field sent:   [2-byte big-endian length] [payload bytes]
#   Device response:       [1 byte]  0x01 = PASS, 0x00 = FAIL
#                          (or a 2-byte big-endian length + payload if
#                          --response-bytes N is given)

import serial
import struct
import sys
import os
import argparse

# ---------------------------------------------------------------------------
# Defaults — edit these to match your setup
# ---------------------------------------------------------------------------
DEFAULT_PORT     = 'COM8'
DEFAULT_BAUD     = 115200
DEFAULT_KAT_FILE = 'kat/KAT_R1_S2.kat'
EXPECTED_RESPONSE = bytes.fromhex('01')   # 0x01 = PASS from device

FIELDS_TO_SEND = ['MSG', 'CPK', 'SIGNATURE']   # order matters — must match firmware


# ---------------------------------------------------------------------------
# KAT parser
# ---------------------------------------------------------------------------
class TestCase:
    def __init__(self, number: int, fields: dict):
        self.number = number       # 1-based case number from file
        self.fields = fields       # field_name -> hex string (uppercase)

    def __repr__(self):
        keys = list(self.fields.keys())
        return f"TestCase(#{self.number}, fields={keys})"


def parse_kat_file(filepath: str) -> list:
    """
    Parse a KEY=VALUE .kat file.
    Returns a list of TestCase objects.
    """
    cases = []
    current = {}
    case_num = 0

    with open(filepath, 'r') as f:
        for raw in f:
            line = raw.strip()

            if not line:
                # blank line — flush current case if non-empty
                if current:
                    cases.append(TestCase(case_num, current))
                    current = {}
                continue

            if line.startswith('#'):
                # flush previous case before starting new one
                if current:
                    cases.append(TestCase(case_num, current))
                    current = {}
                # extract case number from "# Case N"
                parts = line.split()
                if len(parts) >= 3 and parts[1].lower() == 'case':
                    try:
                        case_num = int(parts[2])
                    except ValueError:
                        case_num += 1
                else:
                    case_num += 1
                continue

            if '=' in line:
                key, _, value = line.partition('=')
                current[key.strip().upper()] = value.strip().upper()

    if current:
        cases.append(TestCase(case_num, current))

    return cases


# ---------------------------------------------------------------------------
# Serial helpers
# ---------------------------------------------------------------------------
def open_serial(port: str, baud: int, timeout: float = 100.0) -> serial.Serial:
    ser = serial.Serial(
        port=port,
        baudrate=baud,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        bytesize=serial.EIGHTBITS,
        timeout=timeout
    )
    ser.isOpen()
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    return ser


def wait_for_sync(ser: serial.Serial):
    """Wait for a newline-terminated sync message from the device."""
    readbyte = 0
    readstring = ""
    rawbyte = ""
    print("Waiting for device synchronization...")
    buf = b''
    while (rawbyte != b'\n'):
        rawbyte = ser.read(1)
        readbyte = rawbyte.decode("utf-8")
        readstring = readstring + readbyte
    
    print(readstring)
    ser.reset_input_buffer()


def send_field(ser: serial.Serial, name: str, hex_str: str):
    """Send a 2-byte big-endian length prefix followed by the field bytes."""
    data = bytes.fromhex(hex_str)
    header = struct.pack('>H', len(data))
    ser.write(header)
    ser.write(data)
    preview = hex_str[:24] + ('...' if len(hex_str) > 24 else '')
    print(f"    Sent  {name:<12} {len(data):>5} bytes  [{preview}]")


def recv_exact(ser: serial.Serial, n: int) -> bytes:
    """Read exactly n bytes; raise TimeoutError on short read."""
    buf = b''
    while len(buf) < n:
        chunk = ser.read(n - len(buf))
        if not chunk:
            raise TimeoutError(
                f"Short read: expected {n} bytes, got {len(buf)}."
            )
        buf += chunk
    return buf


def recv_result(ser: serial.Serial) -> bytes:
    """Read the 1-byte pass/fail response from the device."""
    return recv_exact(ser, 1)


# ---------------------------------------------------------------------------
# Main test loop
# ---------------------------------------------------------------------------
def run_kat(ser: serial.Serial, cases: list) -> bool:
    passed = skipped = failed = 0

    for tc in cases:
        print(f"\n{'─'*60}")
        print(f"Case #{tc.number}")

        missing = [f for f in FIELDS_TO_SEND if f not in tc.fields]
        if missing:
            print(f"  SKIP — missing fields: {missing}")
            skipped += 1
            continue

        for fname in FIELDS_TO_SEND:
            send_field(ser, fname, tc.fields[fname])

        try:
            raw = recv_result(ser)
        except TimeoutError as e:
            print(f"  TIMEOUT — {e}")
            failed += 1
            continue

        ok = (raw == EXPECTED_RESPONSE)
        print(f"  {'PASS' if ok else 'FAIL'}  response={raw.hex().upper()}")
        if ok:
            passed += 1
        else:
            failed += 1

    total = passed + failed + skipped
    print(f"\n{'='*60}")
    print(f"Results: {passed} passed | {failed} failed | {skipped} skipped | {total} total")
    return failed == 0


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def parse_args():
    p = argparse.ArgumentParser(
        description='Send KAT test cases (MSG, CPK, SIGNATURE) over UART and validate device responses.'
    )
    p.add_argument('--port', default=DEFAULT_PORT,     help='Serial port  (default: %(default)s)')
    p.add_argument('--kat',  default=DEFAULT_KAT_FILE, help='Path to .kat file  (default: %(default)s)')
    return p.parse_args()


def main():
    args = parse_args()

    if not os.path.isfile(args.kat):
        print(f"ERROR: KAT file not found: {args.kat}")
        sys.exit(1)

    print(f"Parsing: {args.kat}")
    cases = parse_kat_file(args.kat)
    print(f"  {len(cases)} test cases found.")

    print(f"\nOpening {args.port} @ {DEFAULT_BAUD} baud...")
    ser = open_serial(args.port, DEFAULT_BAUD)

    try:
        wait_for_sync(ser)
        ok = run_kat(ser, cases)
    finally:
        ser.close()
        print("Serial port closed.")

    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
