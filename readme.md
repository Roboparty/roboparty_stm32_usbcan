# roboto_usb2can Adapter User Manual

**[中文文档 / Chinese Documentation](readme_cn.md)**

## 📖 Introduction

roboto_usb2can is a single-channel CAN2.0 firmware based on STM32G431, compatible with the open-source `gs_usb` protocol (candleLight). It supports Windows driver-free usage (WinUSB) and Linux native SocketCAN interface, suitable for development debugging and bus analysis.

---


## 📦 Firmware Download

Pre-compiled firmware (.bin) files are available on the [GitHub Releases page](https://github.com/wentywenty/roboto_usb2can/releases).
Each release provides the corresponding version of the bin file as an attachment, which can be flashed directly to the device.

**How to use:**

1. Go to the [Releases page](https://github.com/wentywenty/roboto_usb2can/releases).
2. Download the latest `roboto_usb2can_xxx.bin` firmware file.
3. Flash it using your preferred tool as described in the "Firmware Compilation and Flashing" section below.

---

## 🔧 Hardware Specifications

- **MCU**: STM32G431CBT6 (Cortex-M4+ @ 170MHz)
- **Interface**:
  - 1x USB 2.0 Full Speed (Type-C)
  - 1x CAN2.0
- **Indicator LEDs**:
  - **Blue LED (PC11)**: USB system status indication
  - **Yellow LED (PA7)**: CAN system status indication
  - **Green LED (PA1)**: Data transmission/reception indication

---

## 🔨 Firmware Compilation and Flashing

This project is built on Zephyr RTOS.

### 1. Build Environment Setup

**Prerequisites**: Ensure [Zephyr SDK](https://docs.zephyrproject.org/latest/develop/getting_started/index.html) is properly installed.

1. **Configure CANnectivity Module**

   Create file `zephyr/submanifests/cannectivity.yaml` with the following content:

   ```yaml
   manifest:
     projects:
       - name: cannectivity
         url: https://github.com/CANnectivity/cannectivity.git
         revision: main
         path: custom/cannectivity # adjust the path as needed
   ```

2. **Update Workspace**

   ```bash
   west update
   ```

3. **Get Project Source Code**

   Clone this repository to the `zephyr/samples` directory:

   ```bash
   git clone https://github.com/wentywenty/roboto_usb2can samples/roboto_usb2can
   ```

### 2. Build

```bash
cd roboto_usb2can
west build -b roboto_usb2can
```

### 3. Flashing

The board supports multiple debuggers. Choose the appropriate command based on your debugger:

- **STM32CubeProgrammer (Recommended for STLINK-V3MINIE)**:

  ```bash
  west flash --runner stm32cubeprogrammer
  ```

- **Probe-rs**:

  ```bash
  west flash --runner probe-rs
  ```

- **J-Link**:

  ```bash
  west flash --runner jlink
  ```

- **PyOCD**:

  ```bash
  west flash --runner pyocd
  ```

- **OpenOCD**:

  ```bash
  west flash --runner openocd
  ```

---

## 💡 LED Status Indication

The device has three onboard status indicator LEDs for USB status, CAN status, and communication activity.

### 🔵 Blue LED - USB Status

| State | Blink Pattern | Description |
|-------|---------------|-------------|
| **Ready** | Medium blink (0.5s on / 0.5s off) | USB connection normal, device ready |
| **Error** | Fast blink (0.1s on / 0.1s off) | USB communication error |

### 🟡 Yellow LED - CAN Status

| State | Blink Pattern | Description |
|-------|---------------|-------------|
| **Off** | Very slow blink (0.05s on / 3.95s off) | CAN channel closed or bus off |
| **Active** | Medium blink (0.5s on / 0.5s off) | CAN channel opened, normal working |
| **Warning** | Slow blink (0.2s on / 1.8s off) | CAN bus warning state |
| **Error** | Fast blink (0.1s on / 0.1s off) | CAN bus error or error flood |

### 🟢 Green LED - Communication Activity

- **Off**: Bus idle, no data transmission
- **Brief flash**: Detected CAN bus data reception (RX) or transmission (TX)

---

## 🖥️ Host Software Usage (Windows)

No additional drivers needed on Windows. The system automatically recognizes it as a WinUSB device. We provide a Python-based multi-device management tool.

### 1. Running the Tool

Requires Python 3.8+ environment.

```bash
# Install dependencies
pip install pyusb
# Note: Windows usually comes with tkinter, Linux may need: sudo apt install python3-tk

# Run (ensure libusb-1.0.dll is in directory or system path)
cd scripts
python roboto_usb2can_tool.py
```

### 2. Features

- **Multi-device Support**: Designed for USB Hub scenarios, supports simultaneous connection and management of multiple CAN adapters. Bottom list shows real-time device bus addresses and serial numbers.
- **Global Connection**: Click **Connect All** button to automatically scan and connect all online devices.
- **CAN Control**:
  - Supports unified bitrate setting (default 1Mbps).
  - Click **Start CAN** to enable all device CAN channels at once; **Stop CAN** to disable all.
- **Data Interaction**:
  - **Send**: Supports broadcast to all devices (Target: All) or single device targeting. Supports hex data input and periodic auto-send.
  - **Receive**: Top log area displays real-time bus data with automatic device number annotation (`[Dev X]`) and ID filtering support.

### 3. Package as EXE (Optional)

To run on computers without Python environment, package as EXE file.

1. **Install packaging tool**:

   ```bash
   pip install pyinstaller
   ```

2. **Execute packaging**:

   ```bash
   cd scripts
   pyinstaller --noconfirm --onefile --windowed --clean --icon="icon.ico" --add-data "icon.ico;." roboto_usb2can_tool.py
   ```

   Generated file located at `scripts/dist/roboto_usb2can.exe`.

---

## 🐧 Linux Usage (SocketCAN)

Linux kernel includes built-in `gs_usb` driver, plug and play.

### 1. Install Python Dependencies (for host tool)

```bash
sudo apt install python3-pip python3-tk
pip install pyusb
```

### 2. Check Device

```bash
dmesg | grep gs_usb
# Should display: Configuring for 1 channels
```

### 3. Start Interface

```bash
# Set bitrate to 1Mbps and start
sudo ip link set can0 up type can bitrate 1000000
```

### 4. Test Send/Receive (requires can-utils)

```bash
# Receive
candump can0

# Send
cansend can0 123#DEADBEEF
```

### 5. Run Test Script

We provide automated test script `test_roboto_usb2can.sh` in the `scripts` directory for quick CAN interface verification.

```bash
# Grant execute permission
chmod +x scripts/test_roboto_usb2can.sh

# Run test
./scripts/test_roboto_usb2can.sh
```

### 6. Install udev Rules

Use the rule file [scripts/99-roboto-usb2can.rules](scripts/99-roboto-usb2can.rules).
For more details, see [udev-setup.md](udev-setup.md).

#### Quick Install (from workspace root)

```bash
sudo cp roboto_usb2can/scripts/99-roboto-usb2can.rules /etc/udev/rules.d/
sudo chmod 644 /etc/udev/rules.d/99-roboto-usb2can.rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```

#### Installation Steps

1. **Copy the rule file**
  ```bash
  sudo cp scripts/99-roboto-usb2can.rules /etc/udev/rules.d/
  sudo chmod 644 /etc/udev/rules.d/99-roboto-usb2can.rules
  ```
2. **Reload udev rules**
  ```bash
  sudo udevadm control --reload-rules
  sudo udevadm trigger
  ```
3. **(Optional) Add user to plugdev and dialout groups**
  ```bash
  sudo usermod -a -G plugdev,dialout $USER
  # Re-login required
  ```

#### What It Does

- Allows non-root access to the device
- Creates `/dev/roboto_usb2can*` symlinks
- Works with SocketCAN, libusb, gs_usb, ttyUSB/ttyACM access paths

#### Verify

1. Plug in the device and run:
  ```bash
  lsusb | grep 1d50:606f
  dmesg | grep gs_usb
  ls -l /dev/roboto_usb2can*
  ip link show type can
  ```
2. Test CAN traffic with `candump can0` / `cansend can0 123#DEADBEEF`

#### Troubleshooting

- Permission denied: ensure the user is in plugdev/dialout, or `sudo chmod 666 /dev/roboto_usb2can*`
- No can0: check if gs_usb is loaded, inspect dmesg
- Rules not applied: ensure filename starts with `99-`, permissions are 644, replug device or reload udev

#### Compatibility

- Works on mainstream Linux distros (Ubuntu, Debian, Fedora, Arch)
- Requires kernel gs_usb driver (3.16+)

#### Uninstall

```bash
sudo rm /etc/udev/rules.d/99-roboto-usb2can.rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```


---

## 🔍 Troubleshooting

**Q1: Windows cannot recognize device?**

- Check if USB cable supports data transmission.
- Check Device Manager for yellow exclamation marks, manually update driver (select WinUSB) if present.

**Q2: Python tool shows "Device not found"?**

- Confirm `libusb-1.0.dll` exists.
- On Linux, check USB permissions (`/etc/udev/rules.d/`), ensure current user has USB device access.

**Q3: LED continuously fast blinking?**

- Indicates CAN bus error. Check:
  1. Are termination resistors connected (CAN bus requires 120Ω at both ends).
  2. Are CAN_H / CAN_L wired correctly (not reversed).
  3. Does bitrate match between devices.

**Q4: Packet loss at high bitrates?**

- Try using shorter, higher quality USB cables.
- Reduce bus load or transmission frequency.
