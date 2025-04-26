#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <ctime>
#include <chrono>
#include <sstream>
#include <thread>
#include <mutex>

// Encryption parameters
#define ASCII_PRINTABLE_MIN 32  // Space character
#define ASCII_PRINTABLE_MAX 126 // Tilde character
#define ASCII_RANGE (ASCII_PRINTABLE_MAX - ASCII_PRINTABLE_MIN + 1)

// Time synchronization parameters
std::time_t currentEpochTime;
unsigned long lastTimeSent = 0;
unsigned int timeSyncInterval = 30000; // Sync time every 30 seconds
std::mutex timeMutex;

// Serial port parameters
HANDLE hSerial = INVALID_HANDLE_VALUE;
bool serialConnected = false;
std::mutex serialMutex;

// Keyboard hook
HHOOK hKeyboardHook;

// Function prototypes
bool initializeSerial(const char* portName);
void closeSerial();
void sendTimeToArduino();
uint8_t decryptCharacter(uint8_t encryptedChar);
void serialMonitorThread();

// Simple time-based decryption that's easy to debug
// Simplified decryption using only minutes
uint8_t decryptCharacter(uint8_t encryptedChar) {
    if (encryptedChar < ASCII_PRINTABLE_MIN || encryptedChar > ASCII_PRINTABLE_MAX) {
        return encryptedChar;
    }

    std::time_t currentTime;
    {
        std::lock_guard<std::mutex> lock(timeMutex);
        currentTime = currentEpochTime;
    }

    struct tm timeinfo;
    localtime_s(&timeinfo, &currentTime);
    uint8_t minute = timeinfo.tm_min;

    // Same key derivation as Arduino
    uint8_t key = (minute * 17) % ASCII_RANGE;
    uint8_t decrypted = encryptedChar ^ key;

    // Ensure printable range
    if (decrypted < ASCII_PRINTABLE_MIN) decrypted += ASCII_PRINTABLE_MIN;
    if (decrypted > ASCII_PRINTABLE_MAX) decrypted -= ASCII_RANGE;

    return decrypted;
}

// Modified time sync function with random seconds
void sendTimeToArduino() {
    std::lock_guard<std::mutex> serialLock(serialMutex);
    std::lock_guard<std::mutex> timeLock(timeMutex);

    if (!serialConnected || hSerial == INVALID_HANDLE_VALUE) return;

    // Get current time with random second (0-59)
    currentEpochTime = std::time(nullptr);
    int random_second = rand() % 60;
    currentEpochTime = (currentEpochTime / 60) * 60 + random_second;

    std::stringstream ss;
    ss << "TIME_SYNC:" << currentEpochTime << "\r\n";
    std::string timeMsg = ss.str();

    DWORD bytesWritten = 0;
    WriteFile(hSerial, timeMsg.c_str(), timeMsg.length(), &bytesWritten, NULL);
    FlushFileBuffers(hSerial);

    // Update sync interval (30-90 seconds)
    timeSyncInterval = 30000 + (rand() % 60000);
}

// Low-level keyboard hook procedure
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* pKeyInfo = (KBDLLHOOKSTRUCT*)lParam;

        // Only process keydown events
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            // Get current time information
            std::time_t currentTime;
            struct tm timeinfo;
            {
                std::lock_guard<std::mutex> lock(timeMutex);
                currentTime = currentEpochTime;
                localtime_s(&timeinfo, &currentTime);
            }

            // Special handling for space key
            if (pKeyInfo->vkCode == VK_SPACE) {
                uint8_t minute = timeinfo.tm_min;
                uint8_t second = timeinfo.tm_sec;
                uint8_t key = (minute ^ second) % ASCII_RANGE;
                char encryptedSpace = ' ' ^ key;

                // Ensure printable range
                if (encryptedSpace < ASCII_PRINTABLE_MIN) encryptedSpace += ASCII_PRINTABLE_MIN;
                else if (encryptedSpace > ASCII_PRINTABLE_MAX) encryptedSpace -= ASCII_RANGE;

                // Now decrypt it back
                char decryptedSpace = (char)decryptCharacter((uint8_t)encryptedSpace);

                // Send the decrypted space
                INPUT input[2] = { 0 };
                input[0].type = INPUT_KEYBOARD;
                input[0].ki.wScan = ' ';
                input[0].ki.dwFlags = KEYEVENTF_UNICODE;
                input[1].type = INPUT_KEYBOARD;
                input[1].ki.wScan = ' ';
                input[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
                SendInput(2, input, sizeof(INPUT));
                return 1;
            }

            // Get the character that was typed directly from the keyboard event
            BYTE keyboardState[256] = {0};
            // Set the current modifier state
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) keyboardState[VK_SHIFT] = 0x80;
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000) keyboardState[VK_CONTROL] = 0x80;
            if (GetAsyncKeyState(VK_MENU) & 0x8000) keyboardState[VK_MENU] = 0x80;
            if (GetAsyncKeyState(VK_CAPITAL) & 0x0001) keyboardState[VK_CAPITAL] = 0x01;

            char originalChar[2] = {0, 0};
            WORD character = 0;
            int result = ToAscii(pKeyInfo->vkCode, pKeyInfo->scanCode, keyboardState, &character, 0);

            if (result == 1) {
                originalChar[0] = (char)(character & 0xFF);

                if (originalChar[0] >= ASCII_PRINTABLE_MIN && originalChar[0] <= ASCII_PRINTABLE_MAX) {
                    // Decrypt the character - this is where we need to focus
                    // The issue was here - we need to directly handle the received encrypted character
                    char decrypted = (char)decryptCharacter((uint8_t)originalChar[0]);

                    // Print debugging information
                    std::cout << "Encrypted Key: " << originalChar[0] << ", Decrypted Key: " << decrypted << std::endl;

                    // Use the standard SendInput API to send the decrypted character
                    INPUT input[2] = { 0 };

                    // Key down
                    input[0].type = INPUT_KEYBOARD;
                    input[0].ki.wScan = (WORD)decrypted;
                    input[0].ki.dwFlags = KEYEVENTF_UNICODE;

                    // Key up
                    input[1].type = INPUT_KEYBOARD;
                    input[1].ki.wScan = (WORD)decrypted;
                    input[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

                    SendInput(2, input, sizeof(INPUT));
                    return 1; // Block the original keystroke
                }
            }
        }
    }

    // Pass unhandled events to the next hook
    return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
}

// Initialize the serial connection to the Arduino
bool initializeSerial(const char* portName) {
    std::lock_guard<std::mutex> lock(serialMutex);

    // Close any existing connection
    if (hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
    }

    // Open serial port
    hSerial = CreateFileA(
        portName,
        GENERIC_READ | GENERIC_WRITE,
        0,
        0,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        0
    );

    if (hSerial == INVALID_HANDLE_VALUE) {
        serialConnected = false;
        return false;
    }

    // Set serial parameters (115200 8N1)
    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(hSerial, &dcbSerialParams)) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
        serialConnected = false;
        return false;
    }

    dcbSerialParams.BaudRate = CBR_115200;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;

    if (!SetCommState(hSerial, &dcbSerialParams)) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
        serialConnected = false;
        return false;
    }

    // Set timeouts
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;

    if (!SetCommTimeouts(hSerial, &timeouts)) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
        serialConnected = false;
        return false;
    }

    serialConnected = true;
    return true;
}

// Close serial connection
void closeSerial() {
    std::lock_guard<std::mutex> lock(serialMutex);
    if (hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
    }
    serialConnected = false;
}

// Thread that continuously monitors the serial port for messages
void serialMonitorThread() {
    std::vector<char> buffer(256);
    std::string accumulatedData;
    int noDataCount = 0;

    while (true) {
        // Check if we have a valid serial connection
        {
            std::lock_guard<std::mutex> lock(serialMutex);
            if (!serialConnected || hSerial == INVALID_HANDLE_VALUE) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
        }

        // Read data from serial
        DWORD bytesRead = 0;

        {
            std::lock_guard<std::mutex> lock(serialMutex);
            if (!ReadFile(hSerial, buffer.data(), buffer.size() - 1, &bytesRead, NULL)) {
                DWORD error = GetLastError();

                // If the device was disconnected, try to reconnect
                if (error == ERROR_DEVICE_NOT_CONNECTED) {
                    CloseHandle(hSerial);
                    hSerial = INVALID_HANDLE_VALUE;
                    serialConnected = false;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
        }

        if (bytesRead > 0) {
            buffer[bytesRead] = '\0'; // Null-terminate
            accumulatedData += buffer.data();
            noDataCount = 0; // Reset no data counter

            // Process complete lines
            size_t pos;
            while ((pos = accumulatedData.find('\n')) != std::string::npos) {
                std::string line = accumulatedData.substr(0, pos);
                accumulatedData.erase(0, pos + 1);

                // Trim carriage return if present
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
            }
        }
        else {
            // No data available
            noDataCount++;

            // Sleep to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

// Set up the keyboard hook
void SetLowLevelKeyboardProc() {
    hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
}

// Remove the keyboard hook
void UnhookLowLevelKeyboardHook() {
    if (hKeyboardHook != NULL) {
        UnhookWindowsHookEx(hKeyboardHook);
        hKeyboardHook = NULL;
    }
}

// Function to manually trigger a time sync
void requestTimeSync() {
    sendTimeToArduino();
}

// Time management thread
void timeManagementThread() {
    unsigned long lastUpdate = 0;
    while (true) {
        unsigned long currentTick = GetTickCount();

        // Check if it's time to resync the time with Arduino
        if (currentTick - lastTimeSent > timeSyncInterval) {
            sendTimeToArduino();
        }

        // Update our local time every second regardless of Arduino sync
        if (currentTick - lastUpdate > 1000) {
            std::lock_guard<std::mutex> lock(timeMutex);
            currentEpochTime = std::time(nullptr);
            lastUpdate = currentTick;
        }

        // Sleep to avoid busy-waiting but keep responsive
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// Main function
int main(int argc, char* argv[]) {
    // Default COM port
    std::string comPort = "COM3";

    // Check if COM port was specified
    if (argc > 1) {
        comPort = argv[1];
    }

    std::cout << "Simple Time-Based Keyboard Decryptor" << std::endl;
    std::cout << "-----------------------------------" << std::endl;
    std::cout << "Using serial port: " << comPort << std::endl;

    // Initialize time
    {
        std::lock_guard<std::mutex> lock(timeMutex);
        currentEpochTime = std::time(nullptr);
    }

    // Initialize serial connection
    if (!initializeSerial(comPort.c_str())) {
        std::cout << "Failed to connect to Arduino on " << comPort << std::endl;
    }

    // Start serial monitor thread
    std::thread serialThread(serialMonitorThread);
    serialThread.detach();

    // Start time management thread
    std::thread timeThread(timeManagementThread);
    timeThread.detach();

    // Initial time synchronization
    if (serialConnected) {
        sendTimeToArduino();
        std::this_thread::sleep_for(std::chrono::seconds(2)); // Give Arduino time to process
    }

    // Set up keyboard hook
    SetLowLevelKeyboardProc();
    std::cout << "Keyboard hook installed. Commands:" << std::endl;
    std::cout << "  'T' - Send time synchronization" << std::endl;
    std::cout << "  Ctrl+C - Exit program" << std::endl;

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Clean up
    UnhookLowLevelKeyboardHook();
    closeSerial();

    return 0;
}