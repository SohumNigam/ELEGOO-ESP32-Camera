# ESP32-CAM Object Tracking

Lightweight embedded computer vision system built on the ESP32-CAM using FreeRTOS and custom image processing for object segmentation.

> Core question:  
> **How much computer vision capability can fit on a $10 microcontroller?**

---

## Project Information

- **Project Start:** December 30, 2025
- **Status:** Complete — potential future updates

### Technologies
- ESP32-CAM
- Embedded C & ESP-IDF
- FreeRTOS
- Computer Vision
- Real-Time Processing
- WiFi Streaming

---

# Project Overview

This project explores lightweight computer vision directly on the ESP32-CAM MCU, a platform with severe memory and compute constraints compared to conventional computer vision systems.

Frames are processed entirely on-device using:
- Sobel-style edge detection
- Binary thresholding
- Object segmentation
- Centroid tracking

No image processing is offloaded to a host machine.

The goal was to determine whether a low-cost microcontroller could meaningfully perform real-time object detection and tracking while operating within tight SRAM, PSRAM, and CPU limitations.

Achieving this required careful tradeoffs between:
- Image resolution
- Processing complexity
- Memory usage
- RTOS scheduling
- Frame throughput

---

# System Architecture

## Hardware

The system is built around the ESP32-CAM with an OV3660 image sensor.

### Configuration
| Parameter | Value |
|---|---|
| Resolution | 160 × 120 (QQVGA) |
| Pixel Format | Grayscale |
| Frame Buffering | PSRAM |
| Active Processing | SRAM |

Frames are intentionally captured at low resolution and grayscale format to reduce:
- memory bandwidth
- processing overhead
- frame latency

This allowed real-time processing on the ESP32's constrained hardware.

---

## Firmware Architecture

The firmware uses FreeRTOS with task separation and queue-based communication.

### Task Structure
- Frame Capture Task
- Image Processing Task
- WiFi Streaming Task

Queues are used for inter-task communication, preventing blocking between:
- image acquisition
- frame buffering
- processing
- transmission

This architecture keeps frame capture consistent even under heavy processing load.

---

# Computer Vision Pipeline

The CV pipeline is designed specifically for embedded execution.

## Processing Stages

1. Grayscale Frame Capture
2. Gaussian Blur
3. FAST Corner Detection
4. SAD Scoring
5. Object Grouping
6. Object Centroid Averaging

FAST corner detection is an algorithm that destects contiguois arcs of thresholded pixels arounf a target point to classify a corner.

Detected corners are fed into a SAD score function to identify the relative motion of the area around a corner pixel.

These tracked features are then grouped based on distance and relative movment to generate objects that can be monitored by the ESP32.

---

# Real-Time Processing Optimizations

## Dual-Core Task Scheduling

FreeRTOS task pinning is used to distribute workload across both ESP32 CPU cores.

### Core Responsibilities
| Core | Tasks |
|---|---|
| Core 0 | Frame acquisition + buffering |
| Core 1 | Image processing + streaming |

This minimizes:
- pipeline stalls
- dropped frames
- blocking between processing stages

---

## Memory Optimization

Memory constraints were one of the largest engineering challenges of the project.

Several optimizations were required:
- grayscale-only image format
- QQVGA resolution
- lightweight edge kernels
- minimized buffer duplication
- PSRAM frame storage
- SRAM-only active processing

These reductions were necessary to maintain usable frame rates within available memory limits.

---

# WiFi Streaming

Processed and raw frames can be streamed over WiFi to a Python server running on a host PC.

### Purpose
- Real-time debugging
- Pipeline visualization
- Remote monitoring
- Development without display hardware

### Streaming Path
```text
ESP32-CAM → WiFi → Python Host Server