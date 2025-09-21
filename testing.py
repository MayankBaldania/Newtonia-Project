import websocket
import json
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from collections import deque
import threading
import time

# Replace with your ESP32's IP address
ESP32_IP = "192.168.200.13"  # <<<<<<<<<<<<< CHANGE THIS
WebSocket_URL = f"ws://{ESP32_IP}:81"

# Data buffers for plotting, store the last 100 points
history_length = 100
time_buffer = deque(maxlen=history_length)
x_buffer = deque(maxlen=history_length)
y_buffer = deque(maxlen=history_length)
z_buffer = deque(maxlen=history_length)

# Create the figure and subplot
fig, ax = plt.subplots()
fig.suptitle('ESP32 ADXL345 Real-Time Linear Acceleration', fontsize=14)
ax.set_xlabel('Time (samples)')
ax.set_ylabel('Acceleration (m/s²)')
ax.grid(True)

# Initialize lines for X, Y, Z data
line_x, = ax.plot([], [], label='X-axis')
line_y, = ax.plot([], [], label='Y-axis')
line_z, = ax.plot([], [], label='Z-axis')
ax.legend(loc='upper left')

# Remove the strict static limit (we'll scale dynamically)
# ax.set_ylim(-20, 20)   # <-- commented out

def on_message(ws, message):
    try:
        data = json.loads(message)
        x_val = float(data['x'])
        y_val = float(data['y'])
        z_val = float(data['z'])
        
        # Append new data to the buffers
        new_index = (time_buffer[-1] + 1) if len(time_buffer) else 1
        time_buffer.append(new_index)
        x_buffer.append(x_val)
        y_buffer.append(y_val)
        z_buffer.append(z_val)
        
        # Print to console (optional)
        print(f"X: {x_val:6.2f}, Y: {y_val:6.2f}, Z: {z_val:6.2f}")
        
    except json.JSONDecodeError as e:
        print(f"Error decoding JSON: {e}")
    except KeyError as e:
        print(f"Missing key in data: {e}")

def on_error(ws, error):
    print(f"WebSocket Error: {error}")

def on_close(ws, close_status_code, close_msg):
    print("### WebSocket Connection Closed ###")

def on_open(ws):
    print("### Connected to ESP32 WebSocket Server ###")

# Animation update function, called by matplotlib
def update_plot(frame):
    if len(time_buffer) == 0:
        return line_x, line_y, line_z

    # update line data
    line_x.set_data(time_buffer, x_buffer)
    line_y.set_data(time_buffer, y_buffer)
    line_z.set_data(time_buffer, z_buffer)
    
    # Adjust x-axis limits to scroll with the data
    ax.set_xlim(max(0, time_buffer[0]), time_buffer[-1] + 1)

    # Dynamic y-axis scaling using min/max of buffers with margin
    all_vals = list(x_buffer) + list(y_buffer) + list(z_buffer)
    vmin = min(all_vals)
    vmax = max(all_vals)
    vrange = vmax - vmin
    if vrange < 0.1:
        vrange = 0.1  # prevent zero-range
    margin = vrange * 0.2  # 20% margin
    ymin = vmin - margin
    ymax = vmax + margin
    ax.set_ylim(ymin, ymax)

    # If you prefer autoscale based on line artists instead:
    # ax.relim()
    # ax.autoscale_view(scalex=False, scaley=True)

    return line_x, line_y, line_z

if __name__ == "__main__":
    print(f"Attempting to connect to {WebSocket_URL}")
    print("Make sure the ESP32 is running and the IP is correct.")
    
    ws = websocket.WebSocketApp(WebSocket_URL,
                                on_open=on_open,
                                on_message=on_message,
                                on_error=on_error,
                                on_close=on_close)
    
    wst = threading.Thread(target=ws.run_forever)
    wst.daemon = True
    wst.start()
    
    # Give the connection a moment to establish before starting the plot
    time.sleep(1)
    
    # Use blit=False so axis changes are redrawn properly
    ani = animation.FuncAnimation(fig, update_plot, interval=50, blit=False)
    plt.show()

    ws.close()
