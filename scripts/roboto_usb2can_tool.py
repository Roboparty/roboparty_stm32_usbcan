#!/usr/bin/env python3
"""
roboto_usb2can Host Tool
Supports dual-channel CAN transmission and reception
"""

import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import os
import sys
import usb.core
import usb.util
import threading
import time
import struct
from queue import Queue
from datetime import datetime
import sys

# gs_usb protocol constants
GS_USB_REQUEST_HOST_FORMAT = 0
GS_USB_REQUEST_BITTIMING = 1
GS_USB_REQUEST_MODE = 2
GS_USB_REQUEST_DEVICE_CONFIG = 9
GS_USB_REQUEST_BT_CONST = 5

GS_USB_CHANNEL_MODE_RESET = 0
GS_USB_CHANNEL_MODE_START = 1

# Tool Version
VERSION = "1.0.0"

# CAN frame structure (corresponds to gs_host_frame in firmware)
class CANFrame:
    def __init__(self):
        self.echo_id = 0xFFFFFFFF
        self.can_id = 0
        self.can_dlc = 0
        self.channel = 0
        self.flags = 0
        self.reserved = 0
        self.data = bytearray(64)
    
    def to_bytes(self):
        """Pack into byte stream"""
        data_to_send = self.data[:self.can_dlc]
        padding = 64 - len(data_to_send)
        packed = struct.pack('<IIBBBB',
                             self.echo_id,
                             self.can_id,
                             self.can_dlc,
                             self.channel,
                             self.flags,
                             self.reserved)
        return packed + data_to_send + bytes(padding)
    
    @staticmethod
    def from_bytes(data):
        """Unpack from byte stream"""
        if len(data) < 12:
            return None
        
        frame = CANFrame()
        
        try:
            frame.echo_id, frame.can_id, frame.can_dlc, frame.channel, frame.flags, frame.reserved = \
                struct.unpack('<IIBBBB', data[:12])
            
            data_offset = 12
            if len(data) >= data_offset + frame.can_dlc:
                frame.data[:frame.can_dlc] = data[data_offset:data_offset+frame.can_dlc]
            else:
                available = len(data) - data_offset
                if available > 0:
                    frame.data[:available] = data[data_offset:]
            
            return frame
            
        except struct.error:
            return None

# Bitrate configuration (1Mbps)
BITRATE_1M = {
    'prop_seg': 15,
    'phase_seg1': 15,
    'phase_seg2': 16,
    'sjw': 1,
    'brp': 4
}

class RobopartyCAN:
    def __init__(self):
        self.dev = None
        self.ep_in = None
        self.ep_out = None
        self.is_open = False
        self.rx_thread = None
        self.rx_running = False
        self.rx_callback = None
        
    def find_all(self, vid=0x1D50, pid=0x606F):
        """Find all connected devices"""
        return list(usb.core.find(find_all=True, idVendor=vid, idProduct=pid))

    def open(self, device=None, vid=0x1D50, pid=0x606F):
        """Open device"""
        if device:
            self.dev = device
        else:
            self.dev = usb.core.find(idVendor=vid, idProduct=pid)
            
        if self.dev is None:
            raise ValueError(f"Device not found (VID=0x{vid:04X}, PID=0x{pid:04X})")
        
        try:
            if self.dev.is_kernel_driver_active(0):
                self.dev.detach_kernel_driver(0)
        except:
            pass
        
        usb.util.claim_interface(self.dev, 0)
        
        cfg = self.dev.get_active_configuration()
        intf = cfg[(0, 0)]
        
        ep_in = None
        ep_out_list = []
        
        for ep in intf:
            if usb.util.endpoint_direction(ep.bEndpointAddress) == usb.util.ENDPOINT_IN:
                if usb.util.endpoint_type(ep.bmAttributes) == usb.util.ENDPOINT_TYPE_BULK:
                    ep_in = ep.bEndpointAddress
            else:
                if usb.util.endpoint_type(ep.bmAttributes) == usb.util.ENDPOINT_TYPE_BULK:
                    ep_out_list.append(ep.bEndpointAddress)
        
        if ep_in is None:
            raise ValueError("Bulk IN endpoint not found")
        
        if not ep_out_list:
            raise ValueError("Bulk OUT endpoint not found")
        
        self.ep_in = ep_in
        self.ep_out = ep_out_list[-1]
        self.intf_num = intf.bInterfaceNumber
        
        self.is_open = True
        return True
        
    def close(self):
        """Close device"""
        self.stop_receive()
        if self.dev:
            try:
                usb.util.dispose_resources(self.dev)
            except:
                pass
        self.is_open = False
    
    def set_bitrate(self, channel, bitrate_config):
        """Set bitrate"""
        data = struct.pack('<IIIII',
                           bitrate_config['prop_seg'],
                           bitrate_config['phase_seg1'],
                           bitrate_config['phase_seg2'],
                           bitrate_config['sjw'],
                           bitrate_config['brp'])
        
        self.dev.ctrl_transfer(0x41, GS_USB_REQUEST_BITTIMING, channel, self.intf_num, data)
    
    def start_channel(self, channel):
        """Start CAN channel"""
        data = struct.pack('<II', GS_USB_CHANNEL_MODE_START, 0)
        self.dev.ctrl_transfer(0x41, GS_USB_REQUEST_MODE, channel, self.intf_num, data)
    
    def stop_channel(self, channel):
        """Stop CAN channel"""
        data = struct.pack('<II', GS_USB_CHANNEL_MODE_RESET, 0)
        self.dev.ctrl_transfer(0x41, GS_USB_REQUEST_MODE, channel, self.intf_num, data)
    
    def send_frame(self, channel, can_id, data):
        """Send CAN frame"""
        frame = CANFrame()
        frame.channel = channel
        frame.can_id = can_id
        frame.can_dlc = len(data)
        frame.data[:len(data)] = data
        
        self.dev.write(self.ep_out, frame.to_bytes(), timeout=1000)
    
    def receive_frame(self, timeout=100):
        """Receive CAN frame"""
        try:
            data = self.dev.read(self.ep_in, 512, timeout=timeout)
            return CANFrame.from_bytes(bytes(data))
        except usb.core.USBError as e:
            if e.errno == 110:
                return None
            raise
    
    def start_receive(self, callback):
        """Start receive thread"""
        self.rx_callback = callback
        self.rx_running = True
        self.rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
        self.rx_thread.start()
    
    def stop_receive(self):
        """Stop receive thread"""
        self.rx_running = False
        if self.rx_thread:
            self.rx_thread.join(timeout=2)
    
    def _rx_loop(self):
        """Receive loop"""
        while self.rx_running:
            try:
                frame = self.receive_frame(timeout=100)
                if frame and self.rx_callback:
                    self.rx_callback(frame)
            except usb.core.USBTimeoutError:
                continue
            except Exception as e:
                print(f"RX error: {e}")
                break

# Tkinter GUI
class CANToolGUI:
    def __init__(self, root):
        self.root = root
        self.root.title(f"roboto_usb2can Host Tool v{VERSION}")
        self.root.geometry("1000x800")
        
        # Set icon
        try:
            if getattr(sys, 'frozen', False):
                # Valid for PyInstaller --onefile
                base_path = sys._MEIPASS
            else:
                base_path = os.path.dirname(os.path.abspath(__file__))
                
            icon_path = os.path.join(base_path, "icon.ico")
            if os.path.exists(icon_path):
                self.root.iconbitmap(default=icon_path)
            else:
                print(f"Warning: Icon not found at {icon_path}")
        except Exception as e:
            print(f"Warning: Failed to set icon: {e}")

        # State
        self.is_connected = False
        self.is_bus_started = False
        self.scanned_devices = []
        self.connected_cans = [] 
        self.rx_queue = Queue()
        
        # Styles
        style = ttk.Style()
        style.configure("Big.TButton", font=("Helvetica", 10, "bold"))
        
        self._create_widgets()
        self._update_rx_display()
        self.refresh_devices_list()
    
    def _create_widgets(self):
        """Create GUI widgets with new layout"""
        # 1. Top Toolbar (Controls)
        toolbar = ttk.Frame(self.root, padding="5")
        toolbar.pack(fill=tk.X, side=tk.TOP)
        
        # Connection Controls
        # Connect/Disconnect Toggle
        self.btn_connect = ttk.Button(toolbar, text="Connect All", command=self.toggle_connect, style="Big.TButton")
        self.btn_connect.pack(side=tk.LEFT, padx=5)
        
        ttk.Separator(toolbar, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=10)
        
        # Bitrate
        ttk.Label(toolbar, text="Bitrate:").pack(side=tk.LEFT, padx=2)
        self.bitrate_var = tk.StringVar(value="1000000")
        ttk.Combobox(toolbar, textvariable=self.bitrate_var, width=10,
                     values=["125000", "250000", "500000", "1000000"], state="readonly").pack(side=tk.LEFT, padx=5)
        
        # Bus Controls
        self.btn_bus = ttk.Button(toolbar, text="Start CAN", command=self.toggle_bus, state="disabled")
        self.btn_bus.pack(side=tk.LEFT, padx=5)

        # 2. Send Area
        send_frame = ttk.LabelFrame(self.root, text="Send Frame", padding="5")
        send_frame.pack(fill=tk.X, padx=5, pady=5, side=tk.TOP)
        
        frame_top = ttk.Frame(send_frame)
        frame_top.pack(fill=tk.X, pady=2)
        
        ttk.Label(frame_top, text="Target:").pack(side=tk.LEFT)
        self.target_var = tk.StringVar(value="All")
        self.target_combo = ttk.Combobox(frame_top, textvariable=self.target_var, state="readonly", width=12)
        self.target_combo['values'] = ["All"]
        self.target_combo.pack(side=tk.LEFT, padx=5)
        
        ttk.Label(frame_top, text="ID (Hex):").pack(side=tk.LEFT, padx=5)
        self.send_id_var = tk.StringVar(value="123")
        ttk.Entry(frame_top, textvariable=self.send_id_var, width=8).pack(side=tk.LEFT)
        
        ttk.Label(frame_top, text="Data (Hex):").pack(side=tk.LEFT, padx=5)
        self.send_data_var = tk.StringVar(value="11 22 33 44")
        ttk.Entry(frame_top, textvariable=self.send_data_var, width=30).pack(side=tk.LEFT)
        
        self.btn_send = ttk.Button(frame_top, text="Send", command=self.send_frame, state="disabled")
        self.btn_send.pack(side=tk.LEFT, padx=10)
        
        frame_bot = ttk.Frame(send_frame)
        frame_bot.pack(fill=tk.X, pady=2)
        
        ttk.Label(frame_bot, text="Period (ms):").pack(side=tk.LEFT)
        self.period_var = tk.StringVar(value="100")
        ttk.Entry(frame_bot, textvariable=self.period_var, width=8).pack(side=tk.LEFT, padx=5)
        
        self.periodic_var = tk.BooleanVar(value=False)
        self.chk_periodic = ttk.Checkbutton(frame_bot, text="Enable Periodic", variable=self.periodic_var,
                        command=self.toggle_periodic, state="disabled")
        self.chk_periodic.pack(side=tk.LEFT, padx=5)

        # 3. Main Split View (Log + Device List)
        self.paned = tk.PanedWindow(self.root, orient=tk.VERTICAL, sashwidth=4, sashrelief=tk.RAISED)
        self.paned.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # Top Pane: Log Area
        log_frame = ttk.LabelFrame(self.paned, text="Traffic Log", padding="5")
        self.paned.add(log_frame, height=400)
        
        log_tools = ttk.Frame(log_frame)
        log_tools.pack(fill=tk.X, pady=2)
        ttk.Button(log_tools, text="Clear Log", command=self.clear_recv).pack(side=tk.LEFT)
        ttk.Label(log_tools, text="Filter ID:").pack(side=tk.LEFT, padx=10)
        self.filter_var = tk.StringVar()
        ttk.Entry(log_tools, textvariable=self.filter_var, width=10).pack(side=tk.LEFT)
        self.rx_count_label = ttk.Label(log_tools, text="Rx: 0")
        self.rx_count_label.pack(side=tk.RIGHT, padx=5)

        self.recv_text = scrolledtext.ScrolledText(log_frame, font=("Consolas", 9), state="normal")
        self.recv_text.pack(fill=tk.BOTH, expand=True)

        # Bottom Pane: Device List
        dev_frame = ttk.LabelFrame(self.paned, text="Connected Devices", padding="5")
        self.paned.add(dev_frame, height=150)
        
        cols = ("idx", "bus", "sn", "status")
        self.dev_tree = ttk.Treeview(dev_frame, columns=cols, show="headings", height=5)
        self.dev_tree.heading("idx", text="#")
        self.dev_tree.heading("bus", text="Bus/Addr")
        self.dev_tree.heading("sn", text="Serial Number")
        self.dev_tree.heading("status", text="Status")
        
        self.dev_tree.column("idx", width=40, anchor="center")
        self.dev_tree.column("bus", width=120, anchor="center")
        self.dev_tree.column("sn", width=180, anchor="center")
        self.dev_tree.column("status", width=100, anchor="center")
        
        scrollbar = ttk.Scrollbar(dev_frame, orient=tk.VERTICAL, command=self.dev_tree.yview)
        self.dev_tree.configure(yscrollcommand=scrollbar.set)
        
        self.dev_tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        
        # Periodic Thread
        self.periodic_thread = None
        self.periodic_running = False
        self.rx_count = 0

    def refresh_devices_list(self):
        """Scan only, don't auto connect"""
        self.dev_tree.delete(*self.dev_tree.get_children())
        
        # If connected, show connected devices info
        if self.is_connected:
            for i, can in enumerate(self.connected_cans):
                dev = can.dev
                try:
                    sn = dev.serial_number
                except:
                    sn = "Unknown"
                self.dev_tree.insert("", "end", values=(i, f"{dev.bus}/{dev.address}", sn, "Connected"))
        else:
            # Scan for available devices
            try:
                found = list(usb.core.find(find_all=True, idVendor=0x1D50, idProduct=0x606F))
                for i, dev in enumerate(found):
                    try:
                        sn = dev.serial_number
                    except:
                        sn = "Unknown"
                    self.dev_tree.insert("", "end", values=(i, f"{dev.bus}/{dev.address}", sn, "Available"))
                
                if not found:
                    self.dev_tree.insert("", "end", values=("-", "-", "No devices found", "-"))
            except Exception as e:
                print(f"Scan error: {e}")

    def toggle_connect(self):
        """Handle Connect/Disconnect"""
        if self.is_connected:
            self._disconnect_all()
        else:
            self._connect_all()

    def _connect_all(self):
        try:
            found = list(usb.core.find(find_all=True, idVendor=0x1D50, idProduct=0x606F))
            if not found:
                messagebox.showerror("Error", "No devices found!")
                return

            count = 0
            for i, dev in enumerate(found):
                try:
                    can_wrapper = RobopartyCAN()
                    can_wrapper.open(device=dev)
                    
                    def rx_callback(frame, idx=i):
                        frame.dev_idx = idx
                        self.rx_queue.put(frame)
                        
                    can_wrapper.start_receive(rx_callback)
                    self.connected_cans.append(can_wrapper)
                    count += 1
                except Exception as e:
                    print(f"Failed to connect device: {e}")
            
            if count > 0:
                self.is_connected = True
                self.btn_connect.config(text="Disconnect All")
                self.btn_bus.config(state="normal")
                self.btn_send.config(state="normal")
                self.chk_periodic.config(state="normal")
                self.refresh_devices_list()
                self._update_targets_list()
            else:
                messagebox.showerror("Error", "Could not connect to any device.")

        except Exception as e:
            messagebox.showerror("Error", f"Connection error: {e}")

    def _disconnect_all(self):
        self.stop_periodic()
        if self.is_bus_started:
            self.toggle_bus() # Stop bus first
            
        for c in self.connected_cans:
            try:
                c.close()
            except:
                pass
        self.connected_cans = []
        
        self.is_connected = False
        self.btn_connect.config(text="Connect All")
        self.btn_bus.config(state="disabled")
        self.btn_send.config(state="disabled")
        self.chk_periodic.config(state="disabled")
        self.refresh_devices_list()

    def toggle_bus(self):
        """Handle Start/Stop CAN"""
        if self.is_bus_started:
            self._stop_bus()
        else:
            self._start_bus()

    def _start_bus(self):
        if not self.connected_cans:
            return
        
        try:
            for c in self.connected_cans:
                c.set_bitrate(0, BITRATE_1M)
                c.start_channel(0)
            
            self.is_bus_started = True
            self.btn_bus.config(text="Stop CAN")
        except Exception as e:
            messagebox.showerror("Error", f"Start failed: {e}")

    def _stop_bus(self):
        for c in self.connected_cans:
            try:
                c.stop_channel(0)
            except:
                pass
        self.is_bus_started = False
        self.btn_bus.config(text="Start CAN")

    def _update_targets_list(self):
        values = ["All"]
        for i in range(len(self.connected_cans)):
            values.append(f"Device {i}")
        self.target_combo['values'] = values
        self.target_combo.current(0)

    def send_frame(self):
        """Send CAN frame"""
        if not self.is_connected:
            return
        
        if not self.is_bus_started:
            messagebox.showwarning("Warning", "Bus is not started!")
            return
        
        try:
            can_id = int(self.send_id_var.get(), 16)
            data_str = self.send_data_var.get().replace(" ", "")
            data = bytes.fromhex(data_str)
            
            target_str = self.target_var.get()
            targets_to_send = []
            
            if target_str == "All":
                targets_to_send = self.connected_cans
            else:
                try:
                    idx = int(target_str.split(" ")[1])
                    if idx < len(self.connected_cans):
                        targets_to_send = [self.connected_cans[idx]]
                except:
                    pass
            
            for c in targets_to_send:
                c.send_frame(0, can_id, data)
            
            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            msg = f"[{timestamp}] TX {target_str} ID:0x{can_id:03X} Data:{data.hex(' ').upper()}\n"
            self.recv_text.insert(tk.END, msg, "tx")
            self.recv_text.tag_config("tx", foreground="blue")
            self.recv_text.see(tk.END)
            
        except ValueError as e:
            messagebox.showerror("Error", f"Data format error: {e}")
        except Exception as e:
            messagebox.showerror("Error", f"Send failed: {e}")
    
    def stop_periodic(self):
        """Stop periodic sending safely"""
        self.periodic_var.set(False)
        self.periodic_running = False
        if self.periodic_thread:
            self.periodic_thread.join(timeout=0.1)

    def toggle_periodic(self):
        if self.periodic_var.get():
            self.periodic_running = True
            self.periodic_thread = threading.Thread(target=self._periodic_loop, daemon=True)
            self.periodic_thread.start()
        else:
            self.periodic_running = False
            if self.periodic_thread:
                self.periodic_thread.join(timeout=1)
    
    def _periodic_loop(self):
        while self.periodic_running:
            try:
                self.send_frame()
                period_ms = int(self.period_var.get())
                time.sleep(period_ms / 1000.0)
            except:
                break
    
    def clear_recv(self):
        self.recv_text.delete('1.0', tk.END)
        self.rx_count = 0
        self.rx_count_label.config(text="Rx: 0")

    def _update_rx_display(self):
        while not self.rx_queue.empty():
            frame = self.rx_queue.get()
            
            if frame.can_id == 0 and frame.can_dlc == 0:
                continue
            
            # Simple Filter
            filter_id = self.filter_var.get().strip()
            if filter_id:
                try:
                    if int(filter_id, 16) != frame.can_id:
                        continue
                except:
                    pass
            
            self.rx_count += 1
            self.rx_count_label.config(text=f"Rx: {self.rx_count}")
            
            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            data_hex = frame.data[:frame.can_dlc].hex(' ').upper()
            
            dev_idx = getattr(frame, 'dev_idx', '?')
            msg = f"[{timestamp}] [Dev {dev_idx}] ID:0x{frame.can_id:03X} DLC:{frame.can_dlc} Data:{data_hex}\n"
            
            self.recv_text.insert(tk.END, msg, "rx")
            self.recv_text.tag_config("rx", foreground="green")
            self.recv_text.see(tk.END)
            
            # Trim log
            if int(self.recv_text.index('end-1c').split('.')[0]) > 2000:
                self.recv_text.delete('1.0', '100.0')
        
        self.root.after(50, self._update_rx_display)
            
def main():
    root = tk.Tk()
    app = CANToolGUI(root)
    root.mainloop()

if __name__ == "__main__":
    main()