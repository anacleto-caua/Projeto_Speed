import tkinter as tk
from tkinter import ttk, scrolledtext, simpledialog, filedialog
import socket
import threading
import queue
import datetime
import csv
import os
import re

# Forces Windows to render the app in crisp, native resolution.
try:
    import ctypes
    ctypes.windll.shcore.SetProcessDpiAwareness(1)
except Exception:
    pass # Safe to ignore on Mac/Linux or older Windows

# Backoff schedule (seconds) between reconnect attempts; holds at the last
# value once exhausted instead of growing forever.
RECONNECT_DELAYS = [1, 2, 4, 8, 15]


class ConnectionState:
    DISCONNECTED = "Disconnected"
    CONNECTING = "Connecting..."
    CONNECTED = "Connected"
    RECONNECTING = "Reconnecting..."


class BluetoothApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Classic BT Controller & Logger")
        self.root.geometry("1280x720")

        self.current_csv_path = None

        # Plain int mirror of self.interval_val, kept in sync by
        # update_slider_label(). Reading a Tkinter/Tcl variable from the
        # worker thread isn't safe, so the worker reads this instead when it
        # needs the current interval to sync to the firmware after connecting.
        self.last_known_interval = 100

        # Connection state. The socket is only ever touched by the worker
        # thread; the UI thread only sets flags/events and reads self.state.
        self.sock = None
        self.state = ConnectionState.DISCONNECTED
        self.want_connection = False
        self.generation = 0  # bumped on every Connect/Disconnect click, lets
                              # a stale worker thread recognize it's obsolete
        self.stop_event = threading.Event()
        self.send_queue = queue.Queue()

        self._build_menu()
        self._build_ui()

        self.root.protocol("WM_DELETE_WINDOW", self.on_closing)

    # --- UI construction ---

    def _build_menu(self):
        self.menu_bar = tk.Menu(self.root)
        self.file_menu = tk.Menu(self.menu_bar, tearoff=0)
        self.file_menu.add_command(label="Exit", command=self.on_closing)
        self.menu_bar.add_cascade(label="File", menu=self.file_menu)
        self.root.config(menu=self.menu_bar)

    def _build_ui(self):
        # Connection Frame
        conn_frame = ttk.LabelFrame(self.root, text="Bluetooth Connection", padding="10")
        conn_frame.pack(fill=tk.X, padx=10, pady=5)

        ttk.Label(conn_frame, text="MAC Address:").pack(side=tk.LEFT)
        self.mac_var = tk.StringVar(value="98:D3:51:FD:CB:35")
        self.mac_entry = ttk.Entry(conn_frame, textvariable=self.mac_var, width=20)
        self.mac_entry.pack(side=tk.LEFT, padx=5)

        ttk.Label(conn_frame, text="Channel:").pack(side=tk.LEFT)
        self.channel_var = tk.StringVar(value="1")
        self.channel_entry = ttk.Entry(conn_frame, textvariable=self.channel_var, width=4)
        self.channel_entry.pack(side=tk.LEFT, padx=5)

        self.btn_connect = ttk.Button(conn_frame, text="Connect", command=self.toggle_connection)
        self.btn_connect.pack(side=tk.LEFT, padx=5)

        self.conn_status_var = tk.StringVar(value=ConnectionState.DISCONNECTED)
        self.conn_status_label = ttk.Label(conn_frame, textvariable=self.conn_status_var, foreground="#a00")
        self.conn_status_label.pack(side=tk.LEFT, padx=10)

        # Log File Frame — promoted out of the File menu into an explicit,
        # always-visible feature, since it's a per-session decision the
        # operator needs to make, not an occasional menu action.
        log_frame = ttk.LabelFrame(self.root, text="Log File", padding="10")
        log_frame.pack(fill=tk.X, padx=10, pady=5)

        ttk.Button(log_frame, text="New Log File...", command=self.create_new_file).pack(side=tk.LEFT)
        ttk.Button(log_frame, text="Open Existing Log...", command=self.open_existing_file).pack(side=tk.LEFT, padx=5)
        self.log_path_var = tk.StringVar(value="No file open — results will not be saved")
        ttk.Label(log_frame, textvariable=self.log_path_var, foreground="#555").pack(side=tk.LEFT, padx=10)

        # Participant Frame
        user_frame = ttk.LabelFrame(self.root, text="Current Session Info", padding="10")
        user_frame.pack(fill=tk.X, padx=10, pady=5)

        ttk.Label(user_frame, text="Participant Name:").pack(side=tk.LEFT)
        self.name_var = tk.StringVar(value="Player_1")
        self.name_entry = ttk.Entry(user_frame, textvariable=self.name_var, width=25)
        self.name_entry.pack(side=tk.LEFT, padx=5)

        # Slider Frame
        slider_frame = ttk.LabelFrame(self.root, text="Interval Control", padding="10")
        slider_frame.pack(fill=tk.X, padx=10, pady=5)

        self.interval_val = tk.IntVar(value=100)
        self.slider = ttk.Scale(
            slider_frame,
            from_=0,
            to=5000,
            orient=tk.HORIZONTAL,
            variable=self.interval_val,
            command=self.update_slider_label
        )
        self.slider.pack(fill=tk.X)
        self.lbl_slider = ttk.Label(slider_frame, text="Value: 100")
        self.lbl_slider.pack()

        self.btn_send = ttk.Button(slider_frame, text="Send via Bluetooth", command=self.send_interval, state=tk.DISABLED)
        self.btn_send.pack(pady=5)

        # Console Frame
        console_frame = ttk.LabelFrame(self.root, text="Live Reaction Times", padding="10")
        console_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)

        self.console = scrolledtext.ScrolledText(console_frame, wrap=tk.WORD, state=tk.DISABLED, height=10)
        self.console.pack(fill=tk.BOTH, expand=True)

    # --- Log file feature ---

    def create_new_file(self):
        base_name = simpledialog.askstring("New Log File", "Enter a name for this session file:\n(A timestamp will be added automatically)", parent=self.root)
        if base_name:
            timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"{base_name}_{timestamp}.csv"
            self.current_csv_path = filename

            with open(self.current_csv_path, mode='w', newline='') as file:
                writer = csv.writer(file)
                writer.writerow(["Timestamp", "Name", "Status", "Reaction Time"])

            self.log_path_var.set(f"Active: {self.current_csv_path}")
            self.log_to_console(f"System: Created and opened new log file -> {filename}")

    def open_existing_file(self):
        filepath = filedialog.askopenfilename(title="Select an existing CSV log", filetypes=[("CSV Files", "*.csv"), ("All Files", "*.*")])
        if filepath:
            self.current_csv_path = filepath
            filename_only = os.path.basename(filepath)
            self.log_path_var.set(f"Active: {filename_only}")
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

    # --- Misc UI helpers ---

    def update_slider_label(self, event):
        value = self.interval_val.get()
        self.lbl_slider.config(text=f"Value: {value}")
        self.last_known_interval = value

    def log_to_console(self, text):
        self.console.config(state=tk.NORMAL)
        self.console.insert(tk.END, text + "\n")
        self.console.see(tk.END)
        self.console.config(state=tk.DISABLED)

    # --- Connection lifecycle (UI-thread side) ---

    def toggle_connection(self):
        if self.state == ConnectionState.DISCONNECTED:
            mac = self.mac_var.get().strip()
            if not mac:
                self.log_to_console("Error: Please enter the HC-05's MAC address.")
                return

            try:
                channel = int(self.channel_var.get().strip())
            except ValueError:
                self.log_to_console("Error: Channel must be a number (the HC-05 default is 1).")
                return

            self.generation += 1
            my_gen = self.generation
            self.want_connection = True
            self.stop_event.clear()
            self.state = ConnectionState.CONNECTING
            self._apply_state_to_ui(ConnectionState.CONNECTING)
            self.log_to_console(f"System: Connecting to {mac} (channel {channel})...")

            threading.Thread(target=self._worker, args=(my_gen, mac, channel), daemon=True).start()
        else:
            # Covers CONNECTING, RECONNECTING and CONNECTED: always a clean,
            # immediate stop from the user's point of view.
            self.generation += 1
            self.want_connection = False
            self.stop_event.set()

            sock, self.sock = self.sock, None
            if sock:
                try:
                    sock.close()
                except OSError:
                    pass

            self.state = ConnectionState.DISCONNECTED
            self._apply_state_to_ui(ConnectionState.DISCONNECTED)
            self.log_to_console("System: Disconnected.")

    def send_interval(self):
        if self.state != ConnectionState.CONNECTED:
            self.log_to_console("Error: Not connected.")
            return
        self.send_queue.put(self.interval_val.get())

    def on_closing(self):
        self.generation += 1
        self.want_connection = False
        self.stop_event.set()

        sock, self.sock = self.sock, None
        if sock:
            try:
                sock.close()
            except OSError:
                pass

        self.root.after(100, self.root.destroy)

    # --- Connection lifecycle (worker-thread side) ---
    #
    # Exactly one worker thread owns the live socket at a time: it connects,
    # reads, flushes queued sends, and on any drop retries with backoff until
    # either it reconnects or the user cancels (want_connection goes False /
    # generation changes). The UI thread never touches the socket directly,
    # which is what the old pyserial-era code got wrong and why a silent
    # reader-thread death used to leave the app looking connected while no
    # longer listening.

    def _worker(self, my_gen, mac, channel):
        backoff_idx = 0
        while self.want_connection and my_gen == self.generation:
            state = ConnectionState.CONNECTING if backoff_idx == 0 else ConnectionState.RECONNECTING
            self._set_state(state, my_gen)

            sock = None
            try:
                sock = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_STREAM, socket.BTPROTO_RFCOMM)
                sock.settimeout(5)
                sock.connect((mac, channel))
                sock.settimeout(1)
            except OSError as e:
                if sock:
                    try:
                        sock.close()
                    except OSError:
                        pass
                if not self.want_connection or my_gen != self.generation:
                    return
                delay = RECONNECT_DELAYS[min(backoff_idx, len(RECONNECT_DELAYS) - 1)]
                backoff_idx += 1
                self._log(f"Connect failed ({e}); retrying in {delay}s...", my_gen)
                if self.stop_event.wait(delay):
                    return
                continue

            if my_gen != self.generation or not self.want_connection:
                try:
                    sock.close()
                except OSError:
                    pass
                return

            self.sock = sock
            backoff_idx = 0
            self._set_state(ConnectionState.CONNECTED, my_gen)
            self._log(f"Connected to {mac} (channel {channel})", my_gen)

            # Force a sync: the firmware's real TestPeriodo may not match
            # what's on screen (e.g. changed via the LCD menu, or a previous
            # send was ignored/lost), so every fresh connection re-asserts
            # the app's current interval rather than assuming it already matches.
            self._log(f"System: Forcing interval sync ({self.last_known_interval} ms) with firmware...", my_gen)
            self.send_queue.put(self.last_known_interval)

            buffer = ""
            while self.want_connection and my_gen == self.generation:
                if not self._flush_send_queue(sock, my_gen):
                    break

                try:
                    incoming = sock.recv(1024)
                except socket.timeout:
                    continue
                except OSError:
                    break

                if not incoming:
                    break  # peer closed the connection

                buffer += incoming.decode('utf-8', errors='ignore')
                buffer = self._process_buffer(buffer, my_gen)

            try:
                sock.close()
            except OSError:
                pass
            if self.sock is sock:
                self.sock = None

            if my_gen != self.generation:
                return  # a newer Connect/Disconnect click already owns the UI state

            if not self.want_connection:
                self._set_state(ConnectionState.DISCONNECTED, my_gen)
                return

            delay = RECONNECT_DELAYS[min(backoff_idx, len(RECONNECT_DELAYS) - 1)]
            backoff_idx += 1
            self._log(f"Connection lost; retrying in {delay}s...", my_gen)
            if self.stop_event.wait(delay):
                return

        self._set_state(ConnectionState.DISCONNECTED, my_gen)

    def _flush_send_queue(self, sock, my_gen):
        try:
            while True:
                val = self.send_queue.get_nowait()
                sock.sendall(f"<{val}>".encode('utf-8'))
                self._log(f"Sending interval: <{val}>...", my_gen)
        except queue.Empty:
            return True
        except OSError as e:
            self._log(f"Send failed ({e})", my_gen)
            return False

    def _process_buffer(self, buffer, my_gen):
        while '<' in buffer and '>' in buffer:
            start_idx = buffer.find('<')
            end_idx = buffer.find('>', start_idx)
            if end_idx == -1:
                break

            payload = buffer[start_idx + 1:end_idx]

            # Period-set acknowledgment from the firmware (see PROTOCOL.md):
            # 'A' = applied, 'B' = ignored because a test is running. Not a
            # reaction-time result, so it's handled and consumed separately.
            if payload in ("A", "B"):
                if payload == "A":
                    self._log("System: Interval update applied by firmware.", my_gen)
                else:
                    self._log("System: Interval update ignored — a test is currently running on the firmware.", my_gen)
                buffer = buffer[end_idx + 1:]
                continue

            reaction_time = payload

            # DIRTY DATA CHECK - matches AndroidSpeed's -?\d+(\.\d+)? regex so
            # a valid negative (early) reaction isn't mislabeled BAD_DATA.
            is_valid = bool(re.fullmatch(r'-?\d+(\.\d+)?', reaction_time))
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
                    log_msg = f"FILE LOCK ERROR | [{current_name}] | [{status}] | [{reaction_time}]"

            self._log(log_msg, my_gen)
            buffer = buffer[end_idx + 1:]

        if len(buffer) > 1000:
            buffer = ""
        return buffer

    # --- Thread-safe UI marshaling ---

    def _log(self, text, my_gen=None):
        if my_gen is not None and my_gen != self.generation:
            return
        self.root.after(0, self.log_to_console, text)

    def _set_state(self, state, my_gen):
        if my_gen != self.generation:
            return
        self.state = state
        self.root.after(0, self._apply_state_to_ui, state)

    def _apply_state_to_ui(self, state):
        self.conn_status_var.set(state)
        colors = {
            ConnectionState.DISCONNECTED: "#a00",
            ConnectionState.CONNECTING: "#b8860b",
            ConnectionState.RECONNECTING: "#b8860b",
            ConnectionState.CONNECTED: "#0a0",
        }
        self.conn_status_label.config(foreground=colors.get(state, "#000"))

        if state == ConnectionState.CONNECTED:
            self.btn_connect.config(text="Disconnect")
            self.btn_send.config(state=tk.NORMAL)
            self.mac_entry.config(state=tk.DISABLED)
            self.channel_entry.config(state=tk.DISABLED)
        elif state == ConnectionState.DISCONNECTED:
            self.btn_connect.config(text="Connect")
            self.btn_send.config(state=tk.DISABLED)
            self.mac_entry.config(state=tk.NORMAL)
            self.channel_entry.config(state=tk.NORMAL)
        else:  # CONNECTING / RECONNECTING
            self.btn_connect.config(text="Cancel")
            self.btn_send.config(state=tk.DISABLED)
            self.mac_entry.config(state=tk.DISABLED)
            self.channel_entry.config(state=tk.DISABLED)


if __name__ == "__main__":
    root = tk.Tk()
    app = BluetoothApp(root)
    root.mainloop()
