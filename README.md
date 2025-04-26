# Time-Based Encrypted Keyboard System

![Project Demo](demo.gif) 

A hardware/software solution that encrypts keystrokes at the hardware level using time-based XOR encryption, protecting against both software and hardware keyloggers.

## Key Features

- 🔒 **Hardware-level encryption** using Arduino
- ⏱️ **Time-based XOR cipher** with minute-key rotation
- 🔄 **Auto-synchronization** between device and host
- 🛡️ **Protection against**:
  - Software keyloggers
  - Hardware keyloggers
- ⌨️ **Full keyboard support** including special keys

## Technical Specifications

| Component          | Details                                                                 |
|--------------------|-------------------------------------------------------------------------|
| Encryption         | XOR cipher with time-based key (changes every minute)                   |
| Key Transmission   | Serial (USB) at randomized intervals (30-90 seconds)                   |
| Latency            | <2ms encryption/decryption delay                                       |
| Compatibility      | Windows 10/11 (Linux/Mac support planned)                              |
| Hardware Cost      | <$30 (Arduino + USB host shield)                                       |

## Installation

### Hardware Setup
1. **Required Components**:
   - Arduino Leonardo/Micro
   - USB Host Shield 2.0
   - USB A-to-B cable

2. **Flash Arduino**:
   ```bash
   # Clone repository
   git clone https://github.com/BadrElGezeri/encrypted-keyboard.git
   
   # Open sketch_apr26a.ino in Arduino IDE
   # Select Board: Arduino Leonardo
   # Upload to device
