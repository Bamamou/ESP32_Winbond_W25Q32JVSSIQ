#ifndef SERIALBT_COMMANDER_H
#define SERIALBT_COMMANDER_H

#include <Arduino.h>
#include <BluetoothSerial.h>
#include <SPIMemory.h>

// Forward declaration of flash functions
extern bool flashWrite(uint32_t address, const uint8_t* data, size_t length);
extern bool flashWriteString(uint32_t address, const String& str);
extern bool flashRead(uint32_t address, uint8_t* buffer, size_t length);
extern bool flashReadString(uint32_t address, String& str);
extern bool flashReadRange(uint32_t startAddress, uint32_t endAddress, uint8_t* buffer);
extern bool flashEraseAll();
extern bool flashEraseSector(uint32_t address);
extern bool flashEraseRange(uint32_t startAddress, uint32_t endAddress);

// Forward declarations
extern SPIFlash flash;
extern SemaphoreHandle_t spiMutex;
extern bool flashInitialized;
extern const uint32_t FLASH_SECTOR_SIZE;

class SerialBT_Commander {
public:
    /**
     * @brief Constructor
     * @param deviceName Bluetooth device name
     */
    SerialBT_Commander(const char* deviceName = "ESP32-Flash");
    
    /**
     * @brief Initialize Bluetooth Serial
     * @return true if successful, false otherwise
     */
    bool begin();
    
    /**
     * @brief Check if Bluetooth is connected
     * @return true if connected, false otherwise
     */
    bool isConnected();
    
    /**
     * @brief Process incoming Bluetooth commands
     * Call this periodically to handle commands
     */
    void processCommands();
    
    /**
     * @brief Print available commands menu to BT
     */
    void printMenu();
    
    /**
     * @brief Send message via Bluetooth
     */
    void println(const String& message);
    void print(const String& message);
    void printf(const char* format, ...);

private:
    BluetoothSerial SerialBT;
    String commandBuffer;
    const char* btDeviceName;
    
    /**
     * @brief Parse hex string to uint32_t
     */
    uint32_t parseHex(String hexStr);
    
    /**
     * @brief Process a single command
     */
    void processCommand(String cmd);
    
    /**
     * @brief Handle write command
     */
    void handleWriteCommand(String args);
    
    /**
     * @brief Handle write bytes command
     */
    void handleWriteBytesCommand(String args);
    
    /**
     * @brief Handle read string command
     */
    void handleReadCommand(String args);
    
    /**
     * @brief Handle read bytes command
     */
    void handleReadBytesCommand(String args);
    
    /**
     * @brief Handle read range command
     */
    void handleReadRangeCommand(String args);
    
    /**
     * @brief Handle erase sector command
     */
    void handleEraseCommand(String args);
    
    /**
     * @brief Handle erase range command
     */
    void handleEraseRangeCommand(String args);
    
    /**
     * @brief Handle erase all command
     */
    void handleEraseAllCommand();
    
    /**
     * @brief Handle info command
     */
    void handleInfoCommand();
};

#endif // SERIALBT_COMMANDER_H
