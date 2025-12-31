import socket
import struct
import time

import cv2
import numpy as np

UDP_PORT = 5000
MAX_PACKET_SIZE = 1500

# Header format must match ESP32 struct:
# uint16 frame_id, packet_id, total_packets, payload_size
HEADER_FORMAT = "<HHHH"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("", UDP_PORT))
sock.setblocking(True)

print(f"Listening for UDP camera stream on port {UDP_PORT}")

frames = {}
last_frame_time = time.time()

while True:
    data, _ = sock.recvfrom(MAX_PACKET_SIZE)

    if len(data) < HEADER_SIZE:
        continue

    header = data[:HEADER_SIZE]
    payload = data[HEADER_SIZE:]

    frame_id, packet_id, total_packets, payload_size = \
        struct.unpack(HEADER_FORMAT, header)

    if len(payload) != payload_size:
        continue

    # Initialize frame buffer if needed
    if frame_id not in frames:
        frames[frame_id] = {
            "packets": [None] * total_packets,
            "time": time.time()
        }

    frame = frames[frame_id]
    frame["packets"][packet_id] = payload

    # If frame complete, decode and display
    if all(p is not None for p in frame["packets"]):
        jpeg_bytes = b"".join(frame["packets"])
        del frames[frame_id]

        img = cv2.imdecode(
            np.frombuffer(jpeg_bytes, dtype=np.uint8),
            cv2.IMREAD_COLOR
        )

        if img is not None:
            cv2.imshow("ESP32 UDP Camera", img)
            cv2.waitKey(1)

    # Drop stale frames (avoid memory growth)
    now = time.time()
    for fid in list(frames.keys()):
        if now - frames[fid]["time"] > 0.5:
            del frames[fid]
