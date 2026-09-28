# ESP32-S3 PC Face Tracker

A pan-and-tilt camera that follows a face using an ESP32-S3 camera board and
SCRFD face detection on an NVIDIA-equipped Windows PC. The ESP32 captures
images and drives the servos; the PC selects a face and sends its position back
over Wi-Fi. 

## How it works

```text
ESP32 camera -- JPEG over Wi-Fi --> PC: SCRFD + target association
ESP32 servos <-- face offsets ---- PC: smoothed face center
```

- The camera captures RGB565 at 320x240 and converts it to JPEG in software.
  The tested sensor does not support direct JPEG capture.
- SCRFD-10G runs through ONNX Runtime with CUDA. The PC processes the latest
  available image rather than building a queue of old frames.
- The largest face is initially selected, then associated by position across
  frames. A smoothed center reduces jitter. It can switch identities if people
  cross; it does not recognize a specific person.
- The ESP32 applies bounded proportional corrections at a 20 ms update interval.
- Optional search sweeps start after 1.2 seconds without a detected face while
  the PC continues providing fresh observations.
- Invalid, replayed, or expired observations are rejected. Camera/connection
  loss stops movement as the last valid frame ages past one second.

## Hardware

Tested with a Freenove ESP32-S3-WROOM camera board with 8 MB flash and OPI PSRAM,
two SG90 servos, a pan-and-tilt mount, and an RTX 3050 Laptop GPU with 4 GB VRAM.

| Connection | Destination |
| --- | --- |
| Pan servo signal (usually orange/yellow) | ESP32 GPIO 14 |
| Tilt servo signal (usually orange/yellow) | ESP32 GPIO 21 |
| Both servo red wires | Suitable regulated external 5 V supply |
| Both servo brown/black wires | External supply ground |
| ESP32 GND | Same ground |


The firmware commands both servos to nominal 90 degrees at startup, then stays
paused until the PC enables tracking. Check your mount's physical clearance
before applying the limits below; nominal angles depend on the servo and horn.

## Files

```text
firmware/PCFaceTrack/
  PCFaceTrack.ino       Camera HTTP server and control endpoints
  camera_setup.cpp     Sensor setup and orientation
  camera_pins.h        Camera pin definitions
  board_config.h       Freenove-compatible camera configuration
  motion.h             Tracking and search controller
  tracking.h           PWM and motion task
pc_tracker/
  track.py             Camera receiver, preview, and control client
  detector.py          SCRFD decoding and target selection
  download_model.py    Official model download
  requirements.txt     Pinned Python dependencies
run_tracker.ps1        Windows launcher with NVIDIA DLL path setup
```

## 1. Prepare the PC

Install standard 64-bit Python 3.14 (including the Windows `py` launcher) and an NVIDIA graphics
driver. Open PowerShell in this repository's root folder, then run:

```powershell
py -3.14 -m venv .\pc_tracker\.venv
& .\pc_tracker\.venv\Scripts\python.exe -m pip install --only-binary=:all: -r .\pc_tracker\requirements.txt
& .\pc_tracker\.venv\Scripts\python.exe .\pc_tracker\download_model.py
```

The virtual environment isolates project dependencies. Requirements install
OpenCV, NumPy, Requests, ONNX Runtime GPU, and NVIDIA runtime libraries. The
downloads can be large. Download them before joining the ESP32's network,
which does not provide internet access.

These pins provide Windows Python 3.14 wheels while retaining CUDA 12/cuDNN 9.
`--only-binary=:all:` prevents pip from attempting local source compilation.
Use standard CPython, not the experimental free-threaded build.

The model downloader retrieves the official InsightFace model pack, extracts
the detector, and records its source URL and SHA256 in `pc_tracker/models/`.
Weights are not committed.

Test inference without connecting to the robot:

```powershell
.\run_tracker.ps1 --self-test
```

Expect `CUDAExecutionProvider` in the provider list. The script refuses silent
CPU fallback. Explicit CPU execution is available with `--cpu`, but was not
the final hardware configuration tested here.

The launcher adds the environment's NVIDIA DLL directories to the process PATH
and restores PATH on exit. This resolves the observed cuDNN supporting-DLL
loading error without changing the system PATH. It works when invoked by its
full path from another directory too.

If PowerShell blocks the local script, use a process-only invocation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\run_tracker.ps1 --self-test
```

`-ExecutionPolicy Bypass` applies to this PowerShell process, not a permanent
machine-wide setting. Only run scripts you have reviewed and trust.

## 2. Upload the ESP32 firmware

In Arduino IDE, install **esp32 by Espressif Systems, version 3.3.11** using
Boards Manager.

Open `firmware/PCFaceTrack/PCFaceTrack.ino`; keep all supporting files in that
folder. Choose:

| Setting | Value |
| --- | --- |
| Board | ESP32S3 Dev Module |
| Flash size | 8 MB |
| Partition scheme | Default 8 MB |
| PSRAM | OPI PSRAM |
| Port | The connected ESP32's actual USB port |
| Serial Monitor | 115200 baud |

Upload, then reset if needed. Look for `PCFaceTrack ready` in Serial Monitor.
No extra servo or face-detection library is required on the ESP32.

Connect the PC to **RobotArm-Face**, password **FaceCentre32**. These are demo
credentials embedded in `PCFaceTrack.ino`, not your home Wi-Fi credentials.
Windows may report no internet; stay connected to this local network.

Check these endpoints:

- `http://192.168.4.1/status`: JSON containing `firmware: PCFaceTrack` and initially `state: paused`.
- `http://192.168.4.1:81/frame`: one JPEG image; refresh for another.

## 3. Test detection, then movement

Show detection without enabling motion:

```powershell
.\run_tracker.ps1 --dry-run
```

The green rectangle is the selected face, the yellow dot is its smoothed
center, and the white cross is the image center. Check faces near the edges and
at different head angles. Quit with Q before starting another instance.

Enable tracking with searching disabled for the first movement test:

```powershell
.\run_tracker.ps1 --no-search
```

Move slightly left/right, then up/down. The face should approach the image
center. Once that works, enable tracking with automatic search:

```powershell
.\run_tracker.ps1
```

| Key (preview window focused) | Action |
| --- | --- |
| Space | Pause/resume motion (disabled in dry-run mode) |
| S | Toggle search when the face is lost |
| R | Reset target selection |
| Q / Escape | Quit and request pause |

Other options: `--host 192.168.4.1`, `--headless` (Ctrl+C to exit), `--cpu`,
and `--model path/to/detector.onnx`. Run `--help` for the command-line options.

## Current tuning

These values are in `firmware/PCFaceTrack/motion.h` and match the final workspace:

| Parameter | Value |
| --- | --- |
| Pan range / direction | 15..165 degrees / -1 |
| Tilt range / direction | 65..165 degrees / +1 |
| Startup positions | 90 / 90 degrees |
| Center dead zone | 10 pixels in normalized 320x240 units |
| Correction gain | 0.20 degrees per pixel beyond the dead zone |
| Maximum correction | 10 degrees per observation |
| Commanded movement speed | 100 degrees/second |
| Search speed | 14 degrees/second |

Pan decreases when a face is on the image right. Decreasing tilt points this
mount upward. Different assemblies may require different signs and limits.
PWM maps nominal 0..180 degrees to 1000..2000 microseconds. These are commanded
positions and speed limits, not measured physical angles or speeds.


