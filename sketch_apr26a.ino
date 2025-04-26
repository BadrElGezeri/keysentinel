#include <hidboot.h>
#include <usbhub.h>
#include <SPI.h>
#include <Keyboard.h>

// Encryption parameters
#define ASCII_PRINTABLE_MIN 32  // Space character
#define ASCII_PRINTABLE_MAX 126 // Tilde character
#define ASCII_RANGE (ASCII_PRINTABLE_MAX - ASCII_PRINTABLE_MIN + 1)

// Time synchronization
unsigned long currentEpochTime = 0;
unsigned long lastTimeSync = 0;
bool timeSynchronized = false;
uint8_t lastMinute = 0;  // Track the last minute value used for encryption

class KbdRptParser : public KeyboardReportParser {
public:
    void OnKeyDown(uint8_t mod, uint8_t key);
    void OnKeyUp(uint8_t mod, uint8_t key);
    void OnControlKeysChanged(uint8_t before, uint8_t after);
    void Parse(USBHID *hid, bool is_rpt_id, uint8_t len, uint8_t *buf);
private:
    uint8_t currentMod = 0;
    bool keyPressed[256] = {false};
    unsigned long keyPressTime[256] = {0};
    unsigned long keyRepeatDelay = 500; // Initial delay before repeat
    unsigned long keyRepeatRate = 50;   // Time between repeats after initial delay
    bool ctrlPressed = false;
    
    // Encrypt a single character
    uint8_t encryptCharacter(uint8_t character);
};

// Modify the time sync processing
void processTimeSync(String message) {
    if (message.startsWith("TIME_SYNC:")) {
        String timeStr = message.substring(10);
        currentEpochTime = timeStr.toInt();
        lastTimeSync = millis();
        timeSynchronized = true;
        
        // Ignore seconds from sync - just use full minute value
        lastMinute = (currentEpochTime / 60) % 60;
    }
}

// Simplified encryption using only minutes
uint8_t KbdRptParser::encryptCharacter(uint8_t character) {
    if (character < ASCII_PRINTABLE_MIN || character > ASCII_PRINTABLE_MAX) {
        return character;
    }
    
    // Get current minute only
    uint8_t minute;
    if (timeSynchronized) {
        unsigned long currentTime = currentEpochTime + ((millis() - lastTimeSync) / 1000);
        minute = (currentTime / 60) % 60;
    } else {
        minute = (millis() / 60000) % 60; // Fallback to Arduino's uptime minutes
    }
    
    // XOR with minute-based key
    uint8_t key = (minute * 17) % ASCII_RANGE; // Simple key derivation
    uint8_t encrypted = character ^ key;
    
    // Ensure printable range
    if (encrypted < ASCII_PRINTABLE_MIN) encrypted += ASCII_PRINTABLE_MIN;
    if (encrypted > ASCII_PRINTABLE_MAX) encrypted -= ASCII_RANGE;
    
    return encrypted;
}

void KbdRptParser::Parse(USBHID *hid, bool is_rpt_id, uint8_t len, uint8_t *buf) {
    // Call the parent implementation to handle basic parsing
    KeyboardReportParser::Parse(hid, is_rpt_id, len, buf);
    
    // Process keys that are being held down for repeating
    unsigned long currentTime = millis();
    for (int i = 0; i < 256; i++) {
        if (keyPressed[i] && (currentTime - keyPressTime[i] > keyRepeatDelay)) {
            // Calculate how many repeats should have happened by now
            unsigned long timeElapsed = currentTime - keyPressTime[i] - keyRepeatDelay;
            unsigned long repeatCount = timeElapsed / keyRepeatRate;
            
            // Update the time to reflect the repeats we're processing
            keyPressTime[i] = currentTime - (timeElapsed % keyRepeatRate);
            
            // Process the repeats
            for (unsigned long j = 0; j < repeatCount; j++) {
                // Handle specific repeating keys
                switch (i) {
                    case 0x2A: // Backspace
                        Keyboard.write('\b');
                        break;
                    case 0x4F: // Right Arrow
                        Keyboard.write(0x9D); // Raw scan code for right arrow
                        break;
                    case 0x50: // Left Arrow
                        Keyboard.write(0x9B); // Raw scan code for left arrow
                        break;
                    case 0x51: // Down Arrow
                        Keyboard.write(0x9C); // Raw scan code for down arrow
                        break;
                    case 0x52: // Up Arrow
                        Keyboard.write(0x9A); // Raw scan code for up arrow
                        break;
                    case 0x2C: // Space - use encryption just like regular characters
                        {
                            uint8_t encrypted = encryptCharacter(' ');
                            Keyboard.write(encrypted);
                        }
                        break;
                    default:
                        // Only repeat printable characters
                        if (!ctrlPressed) {
                            uint8_t c = OemToAscii(currentMod, i);
                            if (c) {
                                uint8_t encrypted = encryptCharacter(c);
                                Keyboard.write(encrypted);
                            }
                        }
                        break;
                }
            }
        }
    }
}

void KbdRptParser::OnControlKeysChanged(uint8_t before, uint8_t after) {
    // Store modifier keys state
    currentMod = after;
    
    // Handle Ctrl key specifically (0x01 = left ctrl, 0x10 = right ctrl)
    ctrlPressed = (after & 0x11); // Check if either Ctrl key is pressed
    
    // Forward Ctrl key state to the output
    if (ctrlPressed) {
        Keyboard.press(128); // Raw modifier value for left control
    } else {
        Keyboard.release(128); // Raw modifier value for left control
    }
}

void KbdRptParser::OnKeyDown(uint8_t mod, uint8_t key) {
    // Update current modifier state
    currentMod = mod;
    
    // Record key press time for repeat functionality
    if (!keyPressed[key]) {
        keyPressed[key] = true;
        keyPressTime[key] = millis();
        
        // Handle special keys and arrow keys
        switch (key) {
            case 0x2C: // Space
                // Encrypt the space character like any other character
                {
                    uint8_t encrypted = encryptCharacter(' ');
                    Keyboard.write(encrypted);
                }
                return;
            case 0x28: // Enter
                Keyboard.write('\n');
                return;
            case 0x2A: // Backspace
                Keyboard.write('\b');
                return;
            case 0x4F: // Right Arrow
                Keyboard.write(0x9D); // Raw scan code for right arrow
                return;
            case 0x50: // Left Arrow
                Keyboard.write(0x9B); // Raw scan code for left arrow
                return;
            case 0x51: // Down Arrow
                Keyboard.write(0x9C); // Raw scan code for down arrow
                return;
            case 0x52: // Up Arrow
                Keyboard.write(0x9A); // Raw scan code for up arrow
                return;
            default:
                break;
        }
        
        // Handle Ctrl key combinations
        if (ctrlPressed) {
            switch (key) {
                case 0x04: // A
                    Keyboard.press('a');  // Press 'a' while Ctrl is held
                    Keyboard.release('a'); // Then release it
                    return;
                case 0x06: // C
                    Keyboard.press('c');
                    Keyboard.release('c');
                    return;
                case 0x19: // V
                    Keyboard.press('v');
                    Keyboard.release('v');
                    return;
                case 0x1A: // X
                    Keyboard.press('x');
                    Keyboard.release('x');
                    return;
                case 0x16: // S
                    Keyboard.press('s');
                    Keyboard.release('s');
                    return;
                case 0x1D: // Z
                    Keyboard.press('z');
                    Keyboard.release('z');
                    return;
                // Add other Ctrl combinations as needed
                default:
                    // For other Ctrl combinations, pass through the key
                    uint8_t c = OemToAscii(mod, key);
                    if (c) {
                        Keyboard.press(c);
                        Keyboard.release(c);
                    }
                    return;
            }
        }
        
        // Process normal characters (if not a modifier key or Ctrl combination)
        uint8_t c = OemToAscii(mod, key);
        
        if (c) {
            uint8_t encrypted = encryptCharacter(c);
            Keyboard.write(encrypted);
        }
    }
}

void KbdRptParser::OnKeyUp(uint8_t mod, uint8_t key) {
    // Mark the key as released
    keyPressed[key] = false;
}

USB Usb;
HIDBoot<USB_HID_PROTOCOL_KEYBOARD> HidKeyboard(&Usb);
KbdRptParser Prs;

void setup() {
    Serial.begin(115200);
    Keyboard.begin();
    
    // Ensure proper initialization
    delay(1000);
    
    if (Usb.Init() == -1) {
        while (1); // Halt the program if USB initialization fails
    }
    
    delay(500); // Additional delay for stability
    HidKeyboard.SetReportParser(0, &Prs);
}

void loop() {
    Usb.Task(); // Poll the USB Host for events
    
    // Update time
    static unsigned long lastTimeUpdate = 0;
    if (millis() - lastTimeUpdate > 1000) {  // Every second
        if (timeSynchronized) {
            // Update current epoch time based on elapsed time
            unsigned long elapsedSeconds = (millis() - lastTimeSync) / 1000;
            unsigned long newEpochTime = currentEpochTime + elapsedSeconds;
            
            // Check if minute changed
            uint8_t newMinute = (newEpochTime / 60) % 60;
            if (newMinute != lastMinute) {
                lastMinute = newMinute;
            }
        }
        lastTimeUpdate = millis();
    }
    
    // Check if there's data available on the serial port
    while (Serial.available() > 0) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        
        // Process time sync or other commands from PC
        processTimeSync(command);
    }
}