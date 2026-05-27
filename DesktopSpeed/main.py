import tkinter as tk
from tkinter import ttk, scrolledtext, simpledialog, filedialog
import serial
import serial.tools.list_ports
import threading
import datetime
import csv
import os
import time

# Forces Windows to render the app in crisp, native resolution.
try:
    import ctypes
    ctypes.windll.shcore.SetProcessDpiAwareness(1)
except Exception:
    pass # Safe to ignore on Mac/Linux or older Windows

class BluetoothApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Classic BT Controller & Logger")
        self.root.geometry("780x420")

        self.serial_port = None
        self.reading = False
        self.current_csv_path = None

        # Menu Bar
        self.menu_bar = tk.Menu(root)
        self.file_menu = tk.Menu(self.menu_bar, tearoff=0)
        self.file_menu.add_command(label="New Log File...", command=self.create_new_file)
        self.file_menu.add_command(label="Open Existing Log...", command=self.open_existing_file)
        self.file_menu.add_separator()
        self.file_menu.add_command(label="Exit", command=self.on_closing)
        self.menu_bar.add_cascade(label="File", menu=self.file_menu)
        self.root.config(menu=self.menu_bar)

        # UI Setup
        # Connection Frame
        conn_frame = ttk.LabelFrame(root, text="Bluetooth Connection", padding="10")
        conn_frame.pack(fill=tk.X, padx=10, pady=5)

        ttk.Label(conn_frame, text="Port:").pack(side=tk.LEFT)
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(conn_frame, textvariable=self.port_var, width=20, state="readonly")
        self.port_combo.pack(side=tk.LEFT, padx=5)

        self.btn_refresh = ttk.Button(conn_frame, text="Refresh", width=9, command=self.refresh_ports)
        self.btn_refresh.pack(side=tk.LEFT, padx=2)

        self.btn_connect = ttk.Button(conn_frame, text="Connect", command=self.toggle_connection)
        self.btn_connect.pack(side=tk.LEFT, padx=5)

        self.refresh_ports()

        # Participant Frame
        user_frame = ttk.LabelFrame(root, text="Current Session Info", padding="10")
        user_frame.pack(fill=tk.X, padx=10, pady=5)

        ttk.Label(user_frame, text="Participant Name:").pack(side=tk.LEFT)
        self.name_var = tk.StringVar(value="Player_1")
        self.name_entry = ttk.Entry(user_frame, textvariable=self.name_var, width=25)
        self.name_entry.pack(side=tk.LEFT, padx=5)

        # Slider Frame
        slider_frame = ttk.LabelFrame(root, text="Interval Control", padding="10")
        slider_frame.pack(fill=tk.X, padx=10, pady=5)

        self.interval_val = tk.IntVar(value=0)
        self.slider = ttk.Scale(
            slider_frame,
            from_=0,
            to=5000,
            orient=tk.HORIZONTAL,
            variable=self.interval_val,
            command=self.update_slider_label
        )
        self.slider.pack(fill=tk.X)
        self.lbl_slider = ttk.Label(slider_frame, text="Value: 0")
        self.lbl_slider.pack()

        self.btn_send = ttk.Button(slider_frame, text="Send via Bluetooth", command=self.send_interval, state=tk.DISABLED)
        self.btn_send.pack(pady=5)

        # 4. Console Frame
        console_frame = ttk.LabelFrame(root, text="Live Reaction Times", padding="10")
        console_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)

        self.console = scrolledtext.ScrolledText(console_frame, wrap=tk.WORD, state=tk.DISABLED, height=10)
        self.console.pack(fill=tk.BOTH, expand=True)

        # 5. Status Bar
        self.status_var = tk.StringVar(value="Active Log File: NONE (Go to File -> New)")
        self.status_bar = ttk.Label(root, textvariable=self.status_var, relief=tk.SUNKEN, anchor=tk.W, padding=2)
        self.status_bar.pack(side=tk.BOTTOM, fill=tk.X)

        self.root.protocol("WM_DELETE_WINDOW", self.on_closing)

    # Feature Methods

    def refresh_ports(self):
        ports = serial.tools.list_ports.comports()
        port_list = [port.device for port in ports]
        self.port_combo['values'] = port_list
        if port_list:
            self.port_combo.current(0)
        else:
            self.port_combo.set("No ports found")

    def create_new_file(self):
        base_name = simpledialog.askstring("New Log File", "Enter a name for this session file:\n(A timestamp will be added automatically)", parent=self.root)
        if base_name:
            timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"{base_name}_{timestamp}.csv"
            self.current_csv_path = filename

            with open(self.current_csv_path, mode='w', newline='') as file:
                writer = csv.writer(file)
                writer.writerow(["Timestamp", "Name", "Status", "Reaction Time"])

            self.status_var.set(f"Active Log File: {self.current_csv_path}")
            self.log_to_console(f"System: Created and opened new log file -> {filename}")

    def open_existing_file(self):
        filepath = filedialog.askopenfilename(title="Select an existing CSV log", filetypes=[("CSV Files", "*.csv"), ("All Files", "*.*")])
        if filepath:
            self.current_csv_path = filepath
            filename_only = os.path.basename(filepath)
            self.status_var.set(f"Active Log File: {filename_only}")
            self.log_to_console(f"System: Appending to existing file -> {filename_only}")

    def save_to_csv(self, timestamp, name, status, reaction_time):
        if self.current_csv_path:
            try:
                with open(self.current_csv_path, mode='a', newline='') as file:
                    writer = csv.writer(file)
                    writer.writerow([timestamp, name, status, reaction_time])
                return True
            except PermissionError: # Outer program lock protection
                return False
        return True

    # --- UI & Bluetooth Logic ---

    def update_slider_label(self, event):
        self.lbl_slider.config(text=f"Value: {self.interval_val.get()}")

    def log_to_console(self, text):
        self.console.config(state=tk.NORMAL)
        self.console.insert(tk.END, text + "\n")
        self.console.see(tk.END)
        self.console.config(state=tk.DISABLED)

    def handle_disconnect_error(self):
        self.log_to_console("ERROR: Connection lost (Out of range, sleep mode, or powered off).")
        self.reading = False
        try:
            if self.serial_port:
                self.serial_port.close()
        except:
            pass
        self.btn_connect.config(text="Connect")
        self.btn_send.config(state=tk.DISABLED)

    def toggle_connection(self):
        if self.serial_port and self.serial_port.is_open:
            self.reading = False
            try:
                self.serial_port.close()
            except:
                pass
            self.btn_connect.config(text="Connect")
            self.btn_send.config(state=tk.DISABLED)
            self.log_to_console("System: Disconnected.")
        else:
            port = self.port_var.get()
            if not port or port == "No ports found":
                self.log_to_console("Error: Please select a valid port.")
                return

            try:
                self.serial_port = serial.Serial(port, baudrate=9600, timeout=1)
                self.btn_connect.config(text="Disconnect")
                self.btn_send.config(state=tk.NORMAL)
                self.log_to_console(f"System: Connected to {port}")

                self.reading = True
                threading.Thread(target=self.read_loop, daemon=True).start()

            except PermissionError:
                # GHOST PORT HANDLING
                self.log_to_console(f"ERROR: Access Denied to {port}.")
                self.log_to_console("Fix: If the port is locked, try turning PC Bluetooth off and on again.")
            except serial.SerialException as e:
                self.log_to_console(f"Error: Could not open {port}. ({e})")

    def send_interval(self):
        if self.serial_port and self.serial_port.is_open:
            val = self.interval_val.get()
            packet_str = f"<{val}>"
            try:
                self.serial_port.write(packet_str.encode('utf-8'))
                self.log_to_console(f"Sent Interval: {packet_str}")
            except (serial.SerialException, OSError):
                self.handle_disconnect_error()

    def read_loop(self):
        buffer = ""
        while self.reading and self.serial_port and self.serial_port.is_open:
            try:
                if self.serial_port.in_waiting > 0:
                    incoming_bytes = self.serial_port.read(self.serial_port.in_waiting)
                    buffer += incoming_bytes.decode('utf-8', errors='ignore')

                    while '<' in buffer and '>' in buffer:
                        start_idx = buffer.find('<')
                        end_idx = buffer.find('>', start_idx)

                        if end_idx != -1:
                            reaction_time = buffer[start_idx+1 : end_idx]

                            # DIRTY DATA CHECK
                            is_valid = reaction_time.replace('.', '', 1).isdigit()
                            status = "OK" if is_valid else "BAD_DATA"

                            current_name = self.name_var.get().strip() or "Unknown"
                            timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]

                            if self.current_csv_path is None:
                                log_msg = f"NOT SAVED | [{current_name}] | [{status}] | [{reaction_time}]"
                            else:
                                saved_successfully = self.save_to_csv(timestamp, current_name, status, reaction_time)
                                if saved_successfully:
                                    log_msg = f"[{timestamp}] | [{current_name}] | [{status}] | [{reaction_time}]"
                                else:
                                    log_msg = f"EXCEL LOCK ERROR | [{current_name}] | [{status}] | [{reaction_time}]"

                            self.root.after(0, self.log_to_console, log_msg)
                            buffer = buffer[end_idx+1:]

                            # To avoid spam freeze
                            time.sleep(0.001)
                        else:
                            break

                    if len(buffer) > 1000:
                        buffer = ""

            # SLEEP MODE COMA HANDLING
            except (serial.SerialException, OSError):
                self.root.after(0, self.handle_disconnect_error)
                break
            except Exception as e:
                print(f"Read error: {e}")
                break

    def on_closing(self):
        self.reading = False

        if self.serial_port and self.serial_port.is_open:
            try:
                self.serial_port.close()
            except:
                pass

        self.root.after(100, self.root.destroy)

if __name__ == "__main__":
    root = tk.Tk()
    app = BluetoothApp(root)
    root.mainloop()
