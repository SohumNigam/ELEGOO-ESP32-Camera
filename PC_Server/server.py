import socket
import struct
import time

import cv2
import numpy as np

# =========================
# CONFIG
# =========================

UDP_PORT = 5000
MAX_PACKET_SIZE = 1500

FRAME_WIDTH = 160
FRAME_HEIGHT = 120

# Must match ESP32 struct:
# uint16_t frame_id;
# uint16_t packet_id;
# uint16_t total_packets;
# uint16_t payload_size;

HEADER_FORMAT = "<HHHH"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)

# =========================
# UDP SOCKET
# =========================

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("", UDP_PORT))

print(f"Listening for grayscale UDP stream on port {UDP_PORT}")

# =========================
# FRAME STORAGE
# =========================

frames = {}

# =========================
# MAIN LOOP
# =========================

while True:

    data, addr = sock.recvfrom(MAX_PACKET_SIZE)

    # Ignore invalid packets
    if len(data) < HEADER_SIZE:
        continue

    # -------------------------
    # Extract header
    # -------------------------

    header = data[:HEADER_SIZE]
    payload = data[HEADER_SIZE:]

    frame_id, packet_id, total_packets, payload_size = \
        struct.unpack(HEADER_FORMAT, header)

    # Validate payload size
    if len(payload) != payload_size:
        continue

    # -------------------------
    # Create frame entry
    # -------------------------

    if frame_id not in frames:
        frames[frame_id] = {
            "packets": [None] * total_packets,
            "time": time.time()
        }

    # Store packet
    frames[frame_id]["packets"][packet_id] = payload

    # -------------------------
    # Check if frame complete
    # -------------------------

    frame = frames[frame_id]

    if all(p is not None for p in frame["packets"]):

        # Reassemble frame
        frame_bytes = b"".join(frame["packets"])

        # Remove completed frame from memory
        del frames[frame_id]

        # -------------------------
        # Validate frame size
        # -------------------------

        expected_size = FRAME_WIDTH * FRAME_HEIGHT

        if len(frame_bytes) != expected_size:
            print(
                f"Invalid frame size: "
                f"{len(frame_bytes)} "
                f"(expected {expected_size})"
            )
            continue

        # -------------------------
        # Convert to image
        # -------------------------

        img = np.frombuffer(
            frame_bytes,
            dtype=np.uint8
        ).reshape((FRAME_HEIGHT, FRAME_WIDTH))

        # -------------------------
        # Display
        # -------------------------

        cv2.imshow("ESP32 Grayscale Stream", img)

        key = cv2.waitKey(1)

        # Press Q to quit
        if key == ord('q'):
            break

    # -------------------------
    # Cleanup stale frames
    # -------------------------

    now = time.time()

    stale_ids = []

    for fid in frames:
        if now - frames[fid]["time"] > 0.5:
            stale_ids.append(fid)

    for fid in stale_ids:
        del frames[fid]

# =========================
# CLEANUP
# =========================

sock.close()
cv2.destroyAllWindows()