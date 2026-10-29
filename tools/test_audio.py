"""Play the firmware's short/long speaker test without changing timer state.

Optional dependency: python -m pip install pyserial
Run: python tools/test_audio.py --port COM3
"""
import argparse
import sys
import time


def main():
    import serial
    parser = argparse.ArgumentParser(description="Request short + long timer cues over USB UART")
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--seconds", type=float, default=7)
    args = parser.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")
    device = serial.Serial(port=None, baudrate=115200, timeout=.2)
    device.dtr = False
    device.rts = False
    device.port = args.port
    device.open()
    done, failed, started = False, False, False
    status_during_playback = 0
    try:
        device.reset_input_buffer()
        device.write(b"AUDIO_TEST\n")
        until = time.monotonic() + args.seconds
        while time.monotonic() < until:
            line = device.readline().decode("utf-8", errors="replace").strip()
            if line.startswith(("[AUDIO]", "[UI]", "[ALERT]")):
                print(line, flush=True)
            if "[AUDIO] cue=speaker-test begin" in line:
                started = True
            if started and not done and line.startswith("[STATUS]"):
                status_during_playback += 1
            if "[AUDIO] cue=speaker-test finished" in line:
                done = True
            if "I2S write incomplete" in line or "speaker-test cancelled-or-error" in line:
                failed = True
    finally:
        device.close()
    if not done or failed:
        print("Playback not confirmed; check initialization, port and cancellation.")
        return 1
    print("Software playback completed. Audible output still requires listening.")
    print("Main-loop status updates during playback:", status_during_playback)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
