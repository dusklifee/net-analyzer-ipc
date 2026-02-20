import os
import json
import time
FIFO_PATH = "/tmp/netgate_stats.fifo"
def main():
    print(f"Connecting to NetGateLite IPC socket: {FIFO_PATH}...")
    while not os.path.exists(FIFO_PATH):
        time.sleep(0.5)
    print("Connected! Waiting for data...\n")
    try:
        with open(FIFO_PATH, "r") as fifo:
            while True:
                line = fifo.readline()
                if not line:
                    print("\nGateway disconnected. Pipe closed.")
                    break
                line = line.strip()
                if line:
                    try:
                        data = json.loads(line)
                        print(f"\r[Dashboard] Total Packets Processed: {data['packets']} | Data Transferred: {data['megabytes_processed']:.2f} MB | ns-3 Delay: {data.get('predicted_delay_ms', 0):.2f} ms", end="", flush=True)
                    except json.JSONDecodeError:
                        pass
                else:
                    time.sleep(0.1)
    except KeyboardInterrupt:
        print("\nExiting dashboard.")
    except Exception as e:
        print(f"\nError reading from IPC pipe: {e}")
if __name__ == "__main__":
    main()
