import tkinter as tk
from tkinter import scrolledtext, ttk
import serial
import serial.tools.list_ports
import threading
import time
import datetime

class HardwareSimulator:
    def __init__(self, root):
        self.root = root
        self.root.title("PIC18F4423 Simulator (Drift-Free + Full Stats)")
        self.root.geometry("1000x750")
        
        # Hardware State Variables
        self.serial_port = None
        self.is_connected = False
        self.listen_thread = None
        
        # Unified State Machine
        self.machine_state = "IDLE" 
        self.sequence_start_time = 0  # Restored for absolute timeline
        self.led_on_time = 0  
        self.current_led = 0
        self.target_led_index = 26 
        self.test_job = None 
        
        self.setup_ui()
        self.log_cmd("System Initialized. Full Statistics & Drift-Free Math Enabled.")

    def setup_ui(self):
        # 1. Optional Bluetooth Module (HC-05/SPP)
        conn_frame = tk.LabelFrame(self.root, text="Bluetooth Hardware (HC-05/SPP)", padx=10, pady=10)
        conn_frame.pack(fill=tk.X, padx=10, pady=5)
        
        tk.Label(conn_frame, text="Virtual COM Port:").pack(side=tk.LEFT)
        self.port_var = tk.StringVar()
        self.port_dropdown = ttk.Combobox(conn_frame, textvariable=self.port_var, width=15)
        self.port_dropdown.pack(side=tk.LEFT, padx=5)
        self.refresh_ports()
        
        self.btn_connect = tk.Button(conn_frame, text="Connect", command=self.toggle_connection)
        self.btn_connect.pack(side=tk.LEFT, padx=5)
        tk.Button(conn_frame, text="Refresh", command=self.refresh_ports).pack(side=tk.LEFT)

        # 2. Configs
        config_frame = tk.Frame(self.root, padx=10, pady=5)
        config_frame.pack(fill=tk.X)
        tk.Label(config_frame, text="Chaser Speed (ms per LED):").pack(side=tk.LEFT)
        self.period_slider = tk.Scale(config_frame, from_=10, to=1000, orient=tk.HORIZONTAL, length=300)
        self.period_slider.set(150)
        self.period_slider.pack(side=tk.LEFT, padx=10)

        # 3. Dedicated Reaction Time Display
        rt_frame = tk.Frame(self.root, pady=10)
        rt_frame.pack(fill=tk.X)
        self.lbl_rt = tk.Label(rt_frame, text="0 ms", font=("Consolas", 36, "bold"), fg="gray")
        self.lbl_rt.pack()

        # 4. LED Strip (32 LEDs)
        led_frame = tk.LabelFrame(self.root, text="PORTC DMUX LEDs (0-31)", padx=10, pady=10)
        led_frame.pack(fill=tk.X, padx=10, pady=5)
        
        self.led_canvas = tk.Canvas(led_frame, height=60, bg="#2b2b2b")
        self.led_canvas.pack(fill=tk.X)
        
        self.leds = []
        radius = 10
        spacing = 28
        start_x = 30
        for i in range(32):
            x = start_x + (i * spacing)
            y = 25
            
            if i == self.target_led_index:
                outline_color = "cyan"
                width = 3
            else:
                outline_color = "black"
                width = 1

            led_id = self.led_canvas.create_oval(x - radius, y - radius, x + radius, y + radius, 
                                                 fill="gray30", outline=outline_color, width=width)
            self.led_canvas.create_text(x, y + 20, text=str(i), fill="white", font=("Arial", 7))
            self.leds.append(led_id)

        # 5. Unified Hardware Button
        btn_frame = tk.Frame(self.root, padx=10, pady=10)
        btn_frame.pack(fill=tk.X)
        self.btn_main = tk.Button(btn_frame, text="START GAME", 
                                   font=("Arial", 16, "bold"), bg="#5cb85c", fg="white", 
                                   height=3, command=self.on_main_button_press)
        self.btn_main.pack(fill=tk.X)

        # 6. Command Log (CMD) with Flush Button
        log_frame = tk.LabelFrame(self.root, text="System Console (Results)", padx=10, pady=10)
        log_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)
        
        btn_flush = tk.Button(log_frame, text="Flush Console", command=self.clear_console, bg="#444", fg="white", font=("Arial", 8))
        btn_flush.pack(side=tk.TOP, anchor="e", pady=(0, 5))
        
        self.console = scrolledtext.ScrolledText(log_frame, bg="black", fg="lime", font=("Consolas", 10))
        self.console.pack(fill=tk.BOTH, expand=True)

    def refresh_ports(self):
        ports = [port.device for port in serial.tools.list_ports.comports()]
        self.port_dropdown['values'] = ports
        if ports:
            self.port_dropdown.current(0)

    def log_cmd(self, msg):
        timestamp = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        self.console.insert(tk.END, f"[{timestamp}] {msg}\n")
        self.console.see(tk.END)

    def clear_console(self):
        self.console.delete(1.0, tk.END)
        self.log_cmd("Console flushed.")

    def toggle_connection(self):
        if self.is_connected:
            self.disconnect()
        else:
            port = self.port_var.get()
            try:
                self.serial_port = serial.Serial(port, 9600, timeout=1)
                self.is_connected = True
                self.btn_connect.config(text="Disconnect", fg="red")
                self.log_cmd(f"UART INITIALIZED: Connected to {port}.")
                
                self.listen_thread = threading.Thread(target=self.uart_rx_interrupt, daemon=True)
                self.listen_thread.start()
            except Exception as e:
                self.log_cmd(f"HARDWARE ERROR: Could not open {port}. {e}")

    def disconnect(self):
        self.is_connected = False
        if self.serial_port:
            self.serial_port.close()
        self.btn_connect.config(text="Connect", fg="black")
        self.log_cmd("UART DISABLED: Connection closed.")

    def uart_rx_interrupt(self):
        buffer = ""
        while self.is_connected:
            try:
                if self.serial_port.in_waiting > 0:
                    char = self.serial_port.read().decode('ascii', errors='ignore')
                    buffer += char
                    
                    if '<' in buffer and '>' in buffer:
                        start = buffer.index('<')
                        end = buffer.index('>')
                        if end > start:
                            payload = buffer[start+1:end]
                            self.root.after(0, self.process_remote_command, payload)
                        buffer = buffer[end+1:] 
            except serial.SerialException:
                self.root.after(0, self.disconnect)
                break
            time.sleep(0.01)

    def process_remote_command(self, payload):
        self.log_cmd(f"BLUETOOTH RX: Received <{payload}>")
        try:
            remote_period = int(payload)
            if remote_period < 0: remote_period = 0
            if remote_period > 5000: remote_period = 5000
            
            self.period_slider.set(remote_period)
            self.log_cmd(f"STATE OVERRIDE: Remote app set speed to {remote_period}ms.")
            self.initiate_test_cycle()
        except ValueError:
            self.log_cmd(f"PARSE ERROR: Ignoring bad data '{payload}'.")

    def on_main_button_press(self):
        if self.machine_state == "IDLE":
            self.initiate_test_cycle()
            
        elif self.machine_state == "SEQUENCING":
            if self.test_job:
                self.root.after_cancel(self.test_job) 
            
            stopped_at = self.current_led - 1
            period = self.period_slider.get()
            
            # --- FULL STATS RESTORED ---
            # 1. Absolute Timeline (Total time since clicking start)
            absolute_elapsed_ms = int((time.time() - self.sequence_start_time) * 1000)
            ideal_absolute_target_min = self.target_led_index * period
            ideal_absolute_target_max = ideal_absolute_target_min + period
            
            # 2. Drift-Free Timeline (Anchored to current LED)
            ms_into_current_led = int((time.time() - self.led_on_time) * 1000)
            led_offset = stopped_at - self.target_led_index
            difference_ms = (led_offset * period) + ms_into_current_led
            
            if stopped_at == self.target_led_index:
                self.lbl_rt.config(text=f"{difference_ms:+} ms", fg="#5cb85c")
                status_msg = "BULLSEYE"
            elif stopped_at < self.target_led_index:
                self.lbl_rt.config(text=f"{difference_ms:+} ms", fg="#f0ad4e")
                status_msg = "EARLY"
            else:
                self.lbl_rt.config(text=f"{difference_ms:+} ms", fg="#d9534f")
                status_msg = "LATE"
                
            # Log BOTH sets of statistics
            self.log_cmd(f"{status_msg} Stopped on LED {stopped_at}.")
            self.log_cmd(f"   [ABSOLUTE TIMELINE]")
            self.log_cmd(f"   -> Total Elapsed Time : {absolute_elapsed_ms} ms")
            self.log_cmd(f"   -> Ideal Time Window  : [{ideal_absolute_target_min} ms, {ideal_absolute_target_max} ms]")
            self.log_cmd(f"   -> Timer Drift Error  : {absolute_elapsed_ms - ideal_absolute_target_min - difference_ms:+} ms lag")
            self.log_cmd(f"   [DRIFT-FREE MATH]")
            self.log_cmd(f"   -> Exact MS into LED {stopped_at}: {ms_into_current_led} ms")
            self.log_cmd(f"   -> LEDs Off Target    : {led_offset}")
            self.log_cmd(f"   -> True Reaction Time : {difference_ms:+} ms")
            self.log_cmd("-" * 50)
            
            # Blast the difference over Bluetooth
            self.send_reaction_time(difference_ms)
            
            self.reset_to_idle()

    def initiate_test_cycle(self):
        if self.test_job:
            self.root.after_cancel(self.test_job)
            
        self.machine_state = "SEQUENCING"
        self.current_led = 0
        
        self.btn_main.config(text="STOP ON LED 26", bg="#d9534f")
        self.lbl_rt.config(text="--- ms", fg="gray")
        
        for i, led in enumerate(self.leds):
            outline_col = "cyan" if i == self.target_led_index else "black"
            width = 3 if i == self.target_led_index else 1
            self.led_canvas.itemconfig(led, fill="gray30", outline=outline_col, width=width)
            
        period = self.period_slider.get()
        self.log_cmd(f"SEQUENCE START: Chaser running at {period}ms per LED.")
        
        # Anchor the absolute timeline right before sequence starts
        self.sequence_start_time = time.time()
        self.step_sequence()

    def step_sequence(self):
        period = self.period_slider.get()
        
        prev_led = self.current_led - 1
        if prev_led >= 0:
            outline_col = "cyan" if prev_led == self.target_led_index else "black"
            width = 3 if prev_led == self.target_led_index else 1
            self.led_canvas.itemconfig(self.leds[prev_led], fill="gray30", outline=outline_col, width=width)

        if self.current_led >= 32:
            absolute_elapsed_ms = int((time.time() - self.sequence_start_time) * 1000)
            self.lbl_rt.config(text="TIMEOUT", fg="#d9534f")
            self.log_cmd("TIMEOUT: Sequence reached end of board.")
            self.log_cmd(f"   -> Total Elapsed Time: {absolute_elapsed_ms} ms")
            self.log_cmd("-" * 50)
            self.reset_to_idle()
            return

        outline_col = "cyan" if self.current_led == self.target_led_index else "black"
        width = 3 if self.current_led == self.target_led_index else 1
        self.led_canvas.itemconfig(self.leds[self.current_led], fill="red", outline=outline_col, width=width)
        
        self.led_on_time = time.time()
        
        self.current_led += 1
        self.test_job = self.root.after(period, self.step_sequence)

    def reset_to_idle(self):
        self.machine_state = "IDLE"
        self.btn_main.config(text="START GAME", bg="#5cb85c")

    def send_reaction_time(self, rt):
        if self.is_connected and self.serial_port and self.serial_port.is_open:
            payload = f"<{rt}>"
            try:
                self.serial_port.write(payload.encode('ascii'))
                self.serial_port.flush()
                self.log_cmd(f"BLUETOOTH TX: Sent {payload} back to host.")
            except Exception as e:
                self.log_cmd(f"BLUETOOTH TX ERROR: {e}")

if __name__ == "__main__":
    root = tk.Tk()
    app = HardwareSimulator(root)
    root.mainloop()
