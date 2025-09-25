# ESP32 Double Pulse Test (DPT) Signal Generator

A WiFi-controlled double pulse test signal generator built on ESP32 microcontroller for power module testing applications.

## Overview

This project implements a precise double pulse test (DPT) signal generator using ESP32's RMT (Remote Control Transceiver) peripheral. The system creates complementary high-precision pulse signals that are commonly used for testing power electronic modules like MOSFETs, IGBTs, and other switching devices.

## Features

- **WiFi Access Point**: Creates a local WiFi network for wireless control
- **Web Interface**: Intuitive web-based control panel for parameter configuration
- **Complementary Outputs**: Generates both positive and negative signal channels
- **High Precision**: Uses ESP32's RMT peripheral for nanosecond-level timing accuracy
- **Real-time Control**: Instant parameter updates via web interface
- **Hardware Button**: Physical trigger via ESP32's boot button
- **Flexible Parameters**: Configurable pulse width and timing parameters

## Hardware Requirements

- **ESP32 Development Board**: TinyS3 (UM TinyS3) or compatible ESP32 board
- **Output Pins**:
  - GPIO 7: Positive signal output
  - GPIO 8: Negative signal output (complementary)
- **Input**:
  - GPIO 0: Boot button for manual trigger

## Signal Characteristics

The system generates a double pulse test pattern with the following configurable parameters:

- **Pulse 1 High Time**: Duration of first pulse (default: 5μs)
- **Pulse 1 Low Time**: Gap after first pulse (default: 1μs)  
- **Pulse 2 High Time**: Duration of second pulse (default: 3μs)
- **Pulse 2 Low Time**: Gap after second pulse (default: 10000μs)

### Timing Specifications

- **Clock Resolution**: 12.5ns (80MHz base clock)
- **Minimum Pulse High**: 0.025μs (25ns) - 2 clock ticks
- **Minimum Pulse Low**: 0.125μs (125ns) - 10 clock ticks
- **Maximum Pulse Width**: ~800ms
- **Channel Synchronization**: <25ns between complementary outputs

## Software Architecture

### Core Components

1. **WiFi Access Point**: Creates network "dpt_test" with password "12345678"
2. **HTTP Server**: Serves web interface and handles API requests
3. **RMT Driver**: Manages high-precision signal generation
4. **Button Handler**: Processes hardware button interrupts
5. **Parameter Manager**: Stores and validates pulse parameters

### Web Interface

The system provides a modern, responsive web interface accessible at `http://192.168.4.1` after connecting to the WiFi network. Features include:

- Real-time parameter adjustment
- Instant trigger button
- Success/error feedback
- Mobile-friendly design

## Installation and Setup

### Prerequisites

- [PlatformIO](https://platformio.org/) development environment
- ESP32 toolchain (automatically handled by PlatformIO)

### Building and Flashing

1. Clone this repository:
```bash
git clone https://github.com/yourusername/TinyS3_DPT.git
cd TinyS3_DPT
```

2. Build and upload using PlatformIO:
```bash
pio run --target upload
```

3. Monitor serial output:
```bash
pio device monitor
```

### Configuration

The default configuration creates a WiFi access point with:
- **SSID**: `dpt_test`
- **Password**: `12345678`
- **IP Address**: `192.168.4.1`

To modify these settings, edit the defines in `src/main_rmt.c`:
```c
#define WIFI_SSID "your_network_name"
#define WIFI_PASS "your_password"
```

## Usage

### Web Interface Control

1. Connect to the "dpt_test" WiFi network
2. Open a web browser and navigate to `192.168.4.1`
3. Adjust pulse parameters as needed
4. Click "Set" to update parameters
5. Click "Trigger DPT" to generate the pulse sequence

### Hardware Button Control

- Press the boot button (GPIO 0) to trigger a pulse sequence with current parameters
- The system includes debouncing and interrupt management

### Parameter Ranges

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Pulse 1 High | 0.025-65535 μs | 5 μs | First pulse width |
| Pulse 1 Low | 0.125-65535 μs | 1 μs | Gap after first pulse |
| Pulse 2 High | 0.025-65535 μs | 3 μs | Second pulse width |
| Pulse 2 Low | 0.125-65535 μs | 10000 μs | Gap after second pulse |

## API Reference

### HTTP Endpoints

- `GET /`: Returns the web interface HTML
- `POST /set`: Updates pulse parameters
  - Parameters: `p1h`, `p1l`, `p2h`, `p2l` (all in microseconds)
- `GET /trigger`: Triggers a pulse sequence
- `GET /favicon.ico`: Returns 404 (favicon not implemented)

### Example API Usage

Update parameters via HTTP POST:
```bash
curl -X POST http://192.168.4.1/set \
  -d "p1h=10&p1l=2&p2h=5&p2l=5000"
```

Trigger pulse sequence:
```bash
curl http://192.168.4.1/trigger
```

## Applications

This DPT signal generator is commonly used for:

- **Power MOSFET Testing**: Safe operating area (SOA) characterization
- **IGBT Evaluation**: Switching loss measurements
- **Gate Driver Testing**: Turn-on/turn-off behavior analysis
- **Thermal Testing**: Repetitive pulse power dissipation studies
- **Educational Purposes**: Power electronics laboratory experiments

## Safety Considerations

⚠️ **Important Safety Notes**:

- This device generates control signals only - always use appropriate gate drivers
- Ensure proper isolation between control and power circuits
- Verify signal levels are compatible with your gate driver
- Use current limiting and protection circuitry in test setups
- Follow all safety protocols when working with power electronics

## Technical Details

### RMT Configuration

The ESP32's RMT peripheral is configured with:
- Clock divider: 1 (12.5ns resolution)
- Memory blocks: 1 per channel
- Idle levels: Low for positive, High for negative channel
- No carrier or looping enabled

### High-Precision Timing Details

The system achieves high precision timing through:
- **Base Clock**: 80MHz (12.5ns per tick)
- **Pulse High Minimum**: 2 ticks = 25ns = 0.025μs
- **Pulse Low Minimum**: 10 ticks = 125ns = 0.125μs
- **Conversion Formula**: `ticks = microseconds × 80`
- **Practical Limits**: 
  - Pulse High: 0.025μs - ~800ms
  - Pulse Low: 0.125μs - ~800ms
  - Maximum: ~800ms (limited by 16-bit RMT duration field)

### Testing Mode

The system operates in testing mode where:
- **Pulse High**: Can be set as low as 0.025μs (25ns) for maximum precision
- **Pulse Low**: Minimum 0.125μs (125ns) for reliable RMT operation
- **No Automatic Adjustment**: All values are used exactly as entered
- **Debug Logging**: Detailed timing information is logged for analysis

### Synchronization

Critical sections ensure simultaneous start of both channels:
```c
portENTER_CRITICAL(&mux);
rmt_tx_start(RMT_TX_CHANNEL_P, true);
rmt_tx_start(RMT_TX_CHANNEL_N, true);
portEXIT_CRITICAL(&mux);
```

## Troubleshooting

### Common Issues

1. **WiFi Connection Failed**
   - Check SSID/password settings
   - Ensure no conflicts with existing networks
   - Reset ESP32 and try again

2. **Web Interface Not Loading**
   - Verify IP address (should be 192.168.4.1)
   - Check if connected to correct WiFi network
   - Try different web browser

3. **Pulse Not Generated**
   - Verify GPIO connections
   - Check serial monitor for error messages
   - Ensure parameters are within valid ranges

4. **Timing Inaccuracy**
   - Check oscilloscope probe loading
   - Verify ground connections
   - Consider transmission line effects for very short pulses

### Debug Information

Enable verbose logging by monitoring the serial output at 115200 baud. The system provides detailed status information including:
- WiFi connection status
- Parameter updates
- Pulse generation events
- Error conditions

## Contributing

Contributions are welcome! Please feel free to submit issues and enhancement requests.

### Development

The project uses PlatformIO with the ESP-IDF framework. Key files:
- `src/main_rmt.c`: Main application code
- `platformio.ini`: Build configuration
- `src/main_mcwpm.c.bk`: Alternative MCPWM implementation (backup)

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

You are free to use, modify, and distribute this code for any purpose, including commercial use.

## Acknowledgments

- ESP32 community for excellent documentation
- PlatformIO team for the development environment
- Power electronics community for testing feedback

---

## Author

**Yang Xie** (yang.xie.2@stonybrook.edu)  
*For power electronics testing and education*
