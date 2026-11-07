#!/usr/bin/env python3
"""Nanoleaf Canvas Network Bus Sniffer - Live Capture & Analysis Client.

Connects to the ESP32 sniffer over the local network via HTTP REST API,
streams packets in real time with decoded protocol fields, color highlights,
and can log to file for reverse engineering.
"""

import argparse
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

# ANSI Colors
C_RESET = "\033[0m"
C_BOLD = "\033[1m"
C_DIM = "\033[2m"
C_RED = "\033[31m"
C_GREEN = "\033[32m"
C_YELLOW = "\033[33m"
C_BLUE = "\033[34m"
C_MAGENTA = "\033[35m"
C_CYAN = "\033[36m"
C_WHITE = "\033[37m"


def fetch_json(url, timeout=3.0):
    req = urllib.request.Request(url, headers={"User-Agent": "NanoleafSniffer/1.0"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8"))


def main():
    parser = argparse.ArgumentParser(description="Live Nanoleaf Bus Sniffer Client")
    parser.add_argument("--ip", default="nanoleaf-sniffer.local", help="ESP32 IP or hostname (default: nanoleaf-sniffer.local)")
    parser.add_argument("--port", type=int, default=80, help="Web server port (default: 80)")
    parser.add_argument("--clear", action="store_true", help="Clear sniffer buffer before capturing")
    parser.add_argument("--all", action="store_true", help="Capture all packets including idle C0 polls")
    parser.add_argument("--mode", choices=["passive", "emulator"], help="Switch sniffer mode")
    parser.add_argument("-o", "--output", help="Save captured packets to file (.jsonl or .json)")
    parser.add_argument("-n", "--count", type=int, default=0, help="Stop after N packets (0 = infinite)")
    args = parser.parse_args()

    base_url = f"http://{args.ip}:{args.port}"
    print(f"{C_BOLD}{C_CYAN}=== Nanoleaf Canvas Network Bus Sniffer ==={C_RESET}")
    print(f"Target: {C_WHITE}{base_url}{C_RESET}")

    # Check connectivity
    try:
        status = fetch_json(f"{base_url}/api/status")
        print(f"Status: {C_GREEN}Connected{C_RESET} | Mode: {C_BOLD}{status.get('mode')}{C_RESET} | Uptime: {status.get('uptime_s')}s")
    except Exception as e:
        print(f"{C_RED}Error connecting to {base_url}: {e}{C_RESET}")
        print("Please check that the ESP32 is powered on and connected to your network.")
        sys.exit(1)

    # Apply configuration
    if args.mode:
        try:
            m_resp = fetch_json(f"{base_url}/api/mode?mode={args.mode}")
            print(f"Switched mode to: {C_YELLOW}{args.mode.upper()}{C_RESET}")
        except Exception as e:
            print(f"Failed to set mode: {e}")

    if args.all:
        try:
            fetch_json(f"{base_url}/api/filter?polls=0")
            print(f"Poll filtering: {C_YELLOW}DISABLED{C_RESET} (capturing all C0 polls)")
        except Exception as e:
            print(f"Failed to disable poll filter: {e}")
    else:
        try:
            fetch_json(f"{base_url}/api/filter?polls=1")
        except Exception:
            pass

    if args.clear:
        try:
            fetch_json(f"{base_url}/api/clear")
            print(f"Buffer: {C_GREEN}CLEARED{C_RESET}")
        except Exception as e:
            print(f"Failed to clear buffer: {e}")

    out_file = None
    if args.output:
        out_file = open(args.output, "a", encoding="utf-8")
        print(f"Logging to: {C_BOLD}{args.output}{C_RESET}")

    print(f"\n{C_DIM}Listening for Nanoleaf bus traffic... Press Ctrl+C to stop.{C_RESET}")
    print("-" * 80)

    last_id = 0
    total_received = 0

    try:
        while True:
            url = f"{base_url}/api/packets?since={last_id}&limit=100"
            try:
                data = fetch_json(url)
                packets = data.get("packets", [])
                for p in packets:
                    pid = p["id"]
                    if pid > last_id:
                        last_id = pid
                    total_received += 1

                    # Format display
                    ptype = p.get("type", "Unknown")
                    hex_str = p.get("hex", "")
                    desc = p.get("desc", hex_str)
                    time_s = p.get("time_s", 0.0)
                    delta_ms = p.get("delta_ms", 0.0)

                    # Color coding by frame type
                    prefix_color = C_WHITE
                    badge = ptype
                    if "Color" in ptype:
                        prefix_color = C_GREEN
                        badge = f"{C_BOLD}{C_GREEN}[COLOR]{C_RESET}"
                        desc = f"R={p.get('r')} G={p.get('g')} B={p.get('b')} {C_BOLD}{C_YELLOW}W={p.get('w')}{C_RESET} Trans={p.get('trans')}"
                    elif "Brightness" in ptype:
                        prefix_color = C_YELLOW
                        badge = f"{C_BOLD}{C_YELLOW}[BRI]{C_RESET}"
                        desc = f"Brightness={p.get('bri')}/255 ({round(p.get('bri',0)*100/255)}%)"
                    elif "Layout" in ptype or "Root" in ptype:
                        prefix_color = C_MAGENTA
                        badge = f"{C_BOLD}{C_MAGENTA}[LAYOUT]{C_RESET}"
                    elif "Node" in ptype:
                        prefix_color = C_CYAN
                        badge = f"{C_BOLD}{C_CYAN}[NODE {p.get('node_id')}]{C_RESET}"
                    elif "Poll" in ptype:
                        prefix_color = C_DIM
                        badge = f"{C_DIM}[POLL]{C_RESET}"
                    else:
                        prefix_color = C_RED
                        badge = f"{C_BOLD}{C_RED}[UNKNOWN]{C_RESET}"

                    line = f"{C_DIM}#{pid:04d} {time_s:8.3f}s (+{delta_ms:5.1f}ms){C_RESET} {badge} {prefix_color}{desc}{C_RESET} {C_DIM}({hex_str}){C_RESET}"
                    print(line)

                    if out_file:
                        out_file.write(json.dumps(p) + "\n")
                        out_file.flush()

                    if args.count > 0 and total_received >= args.count:
                        print(f"\nReached target count of {args.count} packets.")
                        return

            except urllib.error.URLError:
                print(f"{C_RED}[Connection Lost - Retrying...]{C_RESET}", end="\r")
            except Exception as e:
                print(f"{C_RED}[Error: {e}]{C_RESET}")

            time.sleep(0.3)

    except KeyboardInterrupt:
        print(f"\n{C_YELLOW}Capture stopped by user. Total packets captured: {total_received}{C_RESET}")
    finally:
        if out_file:
            out_file.close()


if __name__ == "__main__":
    main()
