# HomeEdge Monitor

HomeEdge Monitor is a reusable ESP32-S3 based environmental and equipment monitoring platform built with ESP-IDF, MQTT, and Home Assistant.

The project is designed as both a practical home monitoring system and a hands-on embedded software development project.

## Current Hardware

- Espressif ESP32-S3-DevKitC-1-N8R8
  - 8 MB flash
  - 8 MB PSRAM
- Adafruit SHT40 temperature and humidity sensor
- I2C communication
- Wi-Fi connectivity

## Current Features

- Temperature monitoring in Celsius and Fahrenheit
- Relative humidity monitoring
- Wi-Fi connection and automatic reconnection
- MQTT communication
- MQTT authentication
- Home Assistant MQTT Discovery
- Automatic Home Assistant device and entity creation
- MQTT Last Will and Testament availability monitoring
- Online/offline device status
- Wi-Fi RSSI monitoring
- Dual 3 MB OTA firmware partitions
- OTA-ready flash layout
- Local credentials excluded from source control

## Architecture

```text
SHT40 Sensor
    |
    | I2C
    v
ESP32-S3
    |
    | Wi-Fi
    v
MQTT Broker
    |
    v
Home Assistant
```

## Planned Device Profiles

The same core firmware will eventually support multiple HomeEdge device profiles.

### HomeEdge Dev

Development and firmware testing unit.

- Temperature
- Humidity
- 5-second telemetry interval
- Used to validate firmware before deployment

### HomeEdge Office

Environmental monitoring.

- Temperature
- Humidity

### HomeEdge Laundry

Environmental and washing-machine monitoring.

- Temperature
- Humidity
- Washing machine cycle-complete detection
- Immediate washer events

### HomeEdge Garage

Environmental and freezer monitoring.

- Temperature
- Humidity
- Freezer door monitoring
- Immediate freezer alerts/events

## MQTT

Current development topics include:

```text
homeedge/dev/temperature_f
homeedge/dev/humidity
homeedge/dev/status
```

Home Assistant entities are registered automatically using MQTT Discovery.

## Availability

The device publishes:

```text
online
```

when connected to MQTT.

An MQTT Last Will publishes:

```text
offline
```

if the device unexpectedly loses power or connectivity.

The MQTT keepalive is currently configured for approximately 20 to 30 second offline detection.

## OTA Layout

The ESP32-S3 uses an 8 MB flash layout with two OTA application partitions:

```text
ota_0    3 MB
ota_1    3 MB
```

This allows future firmware updates to be installed into the inactive partition before switching boot partitions.

OTA updating itself is planned for a future development stage.

## Security

Wi-Fi and MQTT credentials are stored in local header files:

```text
firmware/main/wifi_secrets.h
firmware/main/mqtt_secrets.h
```

These files are excluded from Git through `.gitignore` and are not included in this repository.

## Development Environment

- ESP-IDF 6.0.2
- C
- FreeRTOS
- ESP-MQTT
- ESP-IDF Component Manager
- Git / GitHub
- Home Assistant
- Mosquitto MQTT

## Project Status

Current milestone:

**Environmental sensor → ESP32-S3 → Wi-Fi → MQTT → Home Assistant**

Working features include sensor acquisition, network connectivity, MQTT publishing, Home Assistant discovery, device availability, and an OTA-ready partition layout.

Future work will include additional telemetry, configurable device profiles, fault handling, OTA firmware updates, equipment-specific sensors, and 3D-printed enclosures.