# Zigbee Gas Counter for nRF52840

This project is a DIY Zigbee gas counter based on the Nordic Semiconductor nRF52840 platform.

It measures gas consumption by detecting pulses from a mechanical gas meter using a reed sensor, keeps an internal cumulative counter, and exposes the data through Zigbee so it can be used from Zigbee2MQTT and Home Assistant.

The device is designed to run from batteries for long periods of time and to keep counting even if Zigbee2MQTT, Home Assistant, or the Zigbee coordinator are temporarily unavailable.

## Features

- Zigbee gas consumption tracking in cubic meters (m³).
- Internal cumulative counter stored by the device.
- Battery-powered low-power design.
- Zigbee radio enabled only when needed.
- Dual 18650 battery holder with batteries connected in parallel.
- Battery replacement without interrupting device operation.
- Battery voltage and battery percentage reporting.
- Main button with gesture recognition.
- Zigbee OTA firmware updates.
- MCUboot-based bootloader.
- Source code available for both:
  - Seeed Studio XIAO nRF52840
  - Nordic Semiconductor nRF52840 DK
- 3D-printable enclosure and battery holder.
- Optional gas flow measurement.
- Optional battery measurement and reporting.
- Configurable deep-sleep or light-sleep behavior.
- Optional manufacturer-specific attribute for setting the counter value.

## Hardware Pictures

### Fully assembled device

![Gas counter](images/Gas%20counter.png)

### Cover removed

![Gas counter cover removed](images/Gas%20counter%20cover%20removed.png)

### Cover removed and bottom battery extracted

![Gas counter cover removed and bottom battery extracted](images/Gas%20counter%20cover%20removed%20and%20bottom%20battery%20extracted.png)

## Supported Boards

This repository contains source code for two hardware targets.

### Seeed Studio XIAO nRF52840

This is the board used in the actual deployed gas counter device.

The XIAO nRF52840 version is optimized for low-power battery operation and is intended to be installed next to the gas meter inside the custom 3D-printed enclosure.

### Nordic Semiconductor nRF52840 DK

The nRF52840 DK version is used for development, debugging, and testing.

It is useful while developing new firmware features because it provides easier access to SWD, buttons, LEDs, and debug interfaces.

## Zigbee Device Type and Low-Power Operation

In a Zigbee network, devices are usually classified as coordinators, routers, or end devices.

This project behaves as a low-power end device, but with a very specific operating model: the Zigbee radio is normally kept off to save energy, and it is enabled only when needed.

In normal operation, the device wakes the Zigbee radio every 10 gas meter pulses. This allows the counter to keep accurate local measurements while avoiding the power cost of keeping the radio permanently active.

The device can also enable the Zigbee radio when the main button is pressed. This is useful before starting an OTA update, writing the counter value, or forcing an immediate report.

The nRF52840 does not include a dedicated real-time clock that can be used as an absolute wall-clock source while the device is sleeping. For this reason, the firmware uses time information obtained from the Zigbee coordinator to maintain internal timing-related state, such as calculating how long it has been since the battery was last measured.

## Bootloader and Firmware Updates

The firmware boots through MCUboot.

Because of this, the initial firmware must be programmed using an SWD programmer/debugger. Once the bootloader and the first application image have been written, future firmware updates can be performed over Zigbee OTA.

A typical deployment process is:

1. Build the firmware with MCUboot enabled.
2. Flash the complete merged image through SWD.
3. Pair the device with the Zigbee network.
4. Use Zigbee OTA for future firmware updates.

When using OTA, the new image is first downloaded through Zigbee, then MCUboot swaps the image on reboot. The new firmware confirms itself after it has successfully started and recovered Zigbee connectivity.

## How It Works

1. The gas meter has a rotating part with a magnet.
2. A reed sensor detects each rotation or pulse.
3. Each pulse represents a known gas volume.
4. The nRF52840 firmware updates the internal cumulative counter.
5. The device periodically enables the Zigbee radio and reports the accumulated data.
6. The device exposes the value through the Zigbee Metering cluster.
7. Zigbee2MQTT receives the data and forwards it to Home Assistant.

The device does not depend on Home Assistant being online in order to keep counting. The counter is maintained internally by the device.

## Main Button Gestures

The device has one main button. Multiple gestures are recognized and mapped to device actions.

### Button press

Pressing the button, even as part of another gesture, enables the Zigbee radio for approximately one minute and transmits the currently available data.

Use this before performing actions that require the device to be reachable, such as:

- Starting an OTA firmware update.
- Writing a new gas counter value from Zigbee2MQTT.
- Requesting immediate reports.

The Zigbee radio may take around 10 seconds to reconnect to the coordinator, so wait a short time after pressing the button before sending Zigbee commands.

### Single click

A single click is the most common user action.

It performs the following actions:

- Reports all reportable attributes, including gas counter and battery information.
- Forces a battery level measurement.
- Clears and reevaluates possible warning or fault conditions.

### Triple click

A triple click resets the device.

The gas counter value is not intentionally cleared by this action.

### Five quick clicks

Five quick button clicks make the device leave the current Zigbee network and start the join process again.

Use this when you want to move the device to another Zigbee network or force a clean rejoin.

## Battery System

The device uses two 18650 batteries connected in parallel.

The current prototype uses two 3400 mAh cells, providing a total nominal capacity of approximately 6800 mAh.

The enclosure and battery holder are designed so one battery can be removed and charged externally while the device continues running from the other battery.

Always keep at least one battery installed. This prevents the device from losing power and helps avoid missed gas meter pulses during battery replacement.

## Battery Life

The device is designed for very low power operation.

Battery voltage and percentage are measured and reported at least once every 12 hours. They can also be refreshed manually with a single button click.

With two 3400 mAh batteries connected in parallel, the expected runtime is several months. The final runtime is still being measured.

## Zigbee Integration

The device is intended to work with:

- Zigbee2MQTT
- Home Assistant

The firmware exposes gas consumption through standard Zigbee clusters where possible, while also using custom manufacturer-specific attributes for device-specific operations such as setting the current counter value.

The manufacturer information used by the device is:

- Manufacturer name: `Custom devices (DiY)`
- Model identifier: `MiCASAGasCounter`
- Manufacturer code: `0x8888`

The manufacturer code is arbitrary and intended for this DIY project.

## Setting the Counter Value

If enabled in `include/zb_features.h`, the firmware includes a manufacturer-specific attribute in the Metering cluster that allows the cumulative counter value to be set from Zigbee2MQTT.

This is useful when installing the device for the first time, because the internal counter can be synchronized with the physical gas meter reading.

Before writing the counter value, press the main button to wake the Zigbee radio and wait a few seconds for the device to reconnect.

## OTA Firmware Updates

The firmware supports Zigbee OTA updates.

Before starting an OTA update:

1. Press the main button.
2. Wait approximately 10 seconds for Zigbee connectivity to be restored.
3. Start the OTA update from Zigbee2MQTT.

After the OTA transfer completes, the device reboots. MCUboot then boots the new image. The firmware confirms the new image only after the device has started successfully and has had time to reconnect to the Zigbee network.

This reduces the risk of permanently confirming a firmware image that boots but cannot recover basic Zigbee functionality.

## Required Hardware

To build the XIAO-based version, you need:

- Seeed Studio XIAO nRF52840.
- Magnetic reed sensor.
- Two 18650 batteries.
- Battery contacts or battery holder hardware.
- Main push button.
- 3D-printed enclosure.
- SWD programmer/debugger for the initial firmware flash.
- 18 small magnets 4.75mm x 1.5mm (see the `hardware` folder)

For development, the Nordic Semiconductor nRF52840 DK can be used instead of the XIAO board.

## Software Requirements

- nRF Connect SDK.
- Zephyr RTOS.
- MCUboot.
- Zigbee2MQTT.
- Home Assistant, optional but recommended.
- SWD flashing/debugging tools.

## Configuration Options

Some firmware features can be enabled or disabled from:

```text
include/zb_features.h
```

The default configuration is tuned for my own use case: a battery-powered gas counter using deep sleep for maximum battery life.

If your use case is different, review this file before building the firmware.

### Gas flow measurement

The firmware can optionally expose gas flow measurement.

This requires more frequent Zigbee communication, so it is not recommended for battery-powered deployments. It can be useful if the device is externally powered or if battery life is not a concern.

### Battery measurement

Battery voltage and battery percentage reporting can be enabled or disabled.

If the device is not powered from batteries, this feature can be disabled.

### Deep sleep

Deep sleep provides the lowest power consumption.

This is the configuration used by my deployed device. In this mode, the device keeps counting locally and only enables the Zigbee radio when needed, such as after a configured number of gas meter pulses or when the main button is pressed.

This mode is recommended for long-term battery-powered operation.

### Light sleep

Light sleep implements a more conventional Zigbee end-device behavior.

In this mode, the device keeps the Zigbee radio enabled and does not enter deep sleep. This makes the device more readily available on the Zigbee network, but it consumes significantly more power.

This mode may be useful during development, testing, or externally powered deployments.

### Counter write attribute

The firmware includes an optional manufacturer-specific attribute that allows the gas counter value to be written from Zigbee2MQTT.

This is useful when installing the device because the internal counter can be synchronized with the physical gas meter reading.

If this feature is disabled, the counter value cannot be written remotely.

## Building and Flashing

The project is built with nRF Connect SDK and Zephyr.

The firmware uses MCUboot, so the first flash should write the complete merged image, not only the application image.

A typical SWD flashing process writes:

```bash
merged.hex
```

After the initial programming, future updates can be delivered using Zigbee OTA.

## 3D-Printed Hardware

The repository includes STEP files for the enclosure and battery holder.

The 3D-printed hardware is designed to hold:

- The main electronics board.
- Two 18650 batteries.
- The main button.
- The reed sensor.
- The removable cover.

The mechanical design is intended to make battery replacement possible without fully powering down the device.

## Project Status

This project is the nRF52840 evolution of the original ESP32-C6 Zigbee gas counter.

The ESP32-C6 version proved the concept and helped validate the Zigbee gas counter design. The nRF52840 version focuses on improving power consumption, OTA robustness, and long-term reliability.

Current status:

- Pulse counting works.
- Zigbee integration works.
- Battery reporting works.
- Main button gesture handling works.
- Zigbee OTA works.
- MCUboot image confirmation is handled by the application after startup.
- Long-term battery life is still being measured.

## Relation to the ESP32-C6 Version

This project is based on lessons learned from the previous ESP32-C6 implementation.

The ESP32-C6 version proved the concept and helped validate the Zigbee gas counter design. The nRF52840 version focuses on improving power consumption, OTA robustness, and long-term reliability.

## Customization

The same approach can be adapted to other pulse-based meters, such as:

- Water meters.
- Electricity meters with pulse output.
- Other mechanical counters with a magnet or reed-compatible pulse source.

The main changes would be the unit of measurement, the pulse-to-volume conversion factor, and the Zigbee attributes exposed by the firmware.

## Contributing

Contributions, suggestions, and improvements are welcome.

Areas where contributions may be useful include:

- Reviewing the low-power design.
- Improving Zigbee2MQTT converter support.
- Reviewing hardware design.
- Testing other reed sensors or enclosure variants.
- Improving documentation.

## Acknowledgments

I would like to thank Nordic Semiconductor for the excellent online training courses and documentation. They helped me start development on the nRF52840 platform much faster than I expected.

I also want to acknowledge the invaluable help of ChatGPT during the development process. It helped me move faster, debug complex issues, and explore solutions while building this project.

## License

This project is shared under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.

You are free to copy, modify, and adapt the project for personal or non-commercial use, provided that you give appropriate credit and share derived work under the same license.

Commercial use is not allowed.

See the license details here:

[Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International](https://creativecommons.org/licenses/by-nc-sa/4.0/deed.en)