# ESP32-Medibox-Project---EN2853

## Overview
This project is a fully functioning simulation of a Medibox using an ESP32 microcontroller. The Medibox reminds users to take their medicine on time, monitors environmental conditions, and provides alerts when needed.

## Features
- **Time Management**: Fetches current time from NTP server over Wi-Fi and displays it on an OLED screen
- **Customizable Time Zone**: Set your local time zone by inputting UTC offset
- **Multiple Alarm System**: Set up to 2 medication reminder alarms
- **Alarm Management**: View active alarms and delete specific alarms as needed
- **Alarm Interaction**: Stop an active alarm or snooze it for 5 minutes using a push button
- **Environmental Monitoring**: Tracks temperature and humidity levels
- **Health Warnings**: Provides alerts when temperature or humidity exceeds healthy limits
  - Healthy Temperature Range: 24°C - 32°C
  - Healthy Humidity Range: 65% - 80%

## Components Used
- ESP32 Microcontroller
- OLED Display
- DHT22/DHT11 Temperature and Humidity Sensor
- Buzzer for Alarm Sounds
- LEDs for Visual Indicators
- Push Buttons for User Interface

## Development Environment
- PlatformIO in Visual Studio Code
- Wokwi for ESP32 simulation

## Setup Instructions
1. Open the project in VS Code with PlatformIO extension
2. Use Wokwi for VS Code to run the simulation
3. Connect the ESP32 to a Wi-Fi network by updating the network credentials in the code
4. Power on the system and follow the on-screen menu to set up your time zone and alarms

## User Interface
The system provides a menu-driven interface with the following options:
1. Set time zone (UTC offset)
2. Set alarms
3. View active alarms
4. Delete alarms

## Implementation Details
- NTP client for accurate time synchronization
- OLED display for user interface and time display
- DHT sensor library for temperature and humidity monitoring
- State machine design for menu navigation and alarm handling

## Project Structure
medibox

└── src

└── main.cpp     # Main project file containing all the code

## Project Status
This project was developed as part of the EN2853: Embedded Systems and Applications Programming course assignment.

## Author
Sahas Eashan

## License
This project is submitted for educational purposes as part of coursework.
