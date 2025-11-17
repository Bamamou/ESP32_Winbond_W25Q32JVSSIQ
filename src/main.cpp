#include <Arduino.h>
#include <SPI.h>
#include <SPIMemory.h>
#include "SerialBT_Commander.h"

// Winbond W25Q32JVSSIQ SPI Flash Pin Configuration
#define SPI_FLASH_CLK   14
#define SPI_FLASH_MISO  12
#define SPI_FLASH_MOSI  13
#define SPI_FLASH_CS    26

// Flash memory constants
const uint32_t FLASH_SECTOR_SIZE = 4096;
#define FLASH_TOTAL_SIZE  4194304  // 4MB (W25Q32)

SPIFlash flash(SPI_FLASH_CS);

// Task handles
TaskHandle_t writeTaskHandle = NULL;
TaskHandle_t readTaskHandle = NULL;
TaskHandle_t eraseTaskHandle = NULL;
TaskHandle_t monitorTaskHandle = NULL;
TaskHandle_t bluetoothTaskHandle = NULL;

// Semaphore for SPI access
SemaphoreHandle_t spiMutex;

// Flash initialization status
bool flashInitialized = false;

// Bluetooth Commander instance
SerialBT_Commander* btCommander = nullptr;

//=============================================================================
// FLASH MEMORY UTILITY FUNCTIONS
//=============================================================================

/**
 * @brief Write data to flash memory at specified address
 * @param address Starting address to write
 * @param data Pointer to data buffer
 * @param length Number of bytes to write
 * @return true if successful, false otherwise
 */
bool flashWrite(uint32_t address, const uint8_t* data, size_t length) {
  if (!flashInitialized || data == NULL || length == 0) {
    return false;
  }
  
  if (address + length > FLASH_TOTAL_SIZE) {
    Serial.println("[ERROR] Write address out of bounds");
    return false;
  }
  
  xSemaphoreTake(spiMutex, portMAX_DELAY);
  bool success = flash.writeByteArray(address, (uint8_t*)data, length);
  xSemaphoreGive(spiMutex);
  
  return success;
}

/**
 * @brief Write a string to flash memory
 * @param address Starting address to write
 * @param str String to write
 * @return true if successful, false otherwise
 */
bool flashWriteString(uint32_t address, const String& str) {
  if (!flashInitialized) {
    return false;
  }
  
  xSemaphoreTake(spiMutex, portMAX_DELAY);
  String tempStr = str;
  bool success = flash.writeStr(address, tempStr);
  xSemaphoreGive(spiMutex);
  
  return success;
}

/**
 * @brief Read data from flash memory at specified address
 * @param address Starting address to read
 * @param buffer Buffer to store read data
 * @param length Number of bytes to read
 * @return true if successful, false otherwise
 */
bool flashRead(uint32_t address, uint8_t* buffer, size_t length) {
  if (!flashInitialized || buffer == NULL || length == 0) {
    return false;
  }
  
  if (address + length > FLASH_TOTAL_SIZE) {
    Serial.println("[ERROR] Read address out of bounds");
    return false;
  }
  
  xSemaphoreTake(spiMutex, portMAX_DELAY);
  bool success = flash.readByteArray(address, buffer, length);
  xSemaphoreGive(spiMutex);
  
  return success;
}

/**
 * @brief Read a string from flash memory
 * @param address Starting address to read
 * @param str Reference to string object to store result
 * @return true if successful, false otherwise
 */
bool flashReadString(uint32_t address, String& str) {
  if (!flashInitialized) {
    return false;
  }
  
  xSemaphoreTake(spiMutex, portMAX_DELAY);
  bool success = flash.readStr(address, str);
  xSemaphoreGive(spiMutex);
  
  return success;
}

/**
 * @brief Read entire flash memory contents
 * @param buffer Buffer to store all data (must be FLASH_TOTAL_SIZE bytes)
 * @param printProgress Print progress to serial (default: true)
 * @return true if successful, false otherwise
 */
bool flashReadAll(uint8_t* buffer, bool printProgress = true) {
  if (!flashInitialized || buffer == NULL) {
    return false;
  }
  
  if (printProgress) {
    Serial.println("[INFO] Reading entire flash memory...");
  }
  
  const size_t chunkSize = 256;
  for (uint32_t addr = 0; addr < FLASH_TOTAL_SIZE; addr += chunkSize) {
    if (!flashRead(addr, &buffer[addr], chunkSize)) {
      Serial.printf("[ERROR] Failed to read at address 0x%08X\n", addr);
      return false;
    }
    
    if (printProgress && (addr % (64 * 1024)) == 0) {
      Serial.printf("[PROGRESS] %d%% complete\n", (addr * 100) / FLASH_TOTAL_SIZE);
    }
  }
  
  if (printProgress) {
    Serial.println("[INFO] Read complete!");
  }
  
  return true;
}

/**
 * @brief Read specific address range from flash memory
 * @param startAddress Starting address to read
 * @param endAddress Ending address (inclusive)
 * @param buffer Buffer to store read data
 * @return true if successful, false otherwise
 */
bool flashReadRange(uint32_t startAddress, uint32_t endAddress, uint8_t* buffer) {
  if (!flashInitialized || buffer == NULL) {
    return false;
  }
  
  if (startAddress > endAddress || endAddress >= FLASH_TOTAL_SIZE) {
    Serial.println("[ERROR] Invalid address range");
    return false;
  }
  
  uint32_t length = endAddress - startAddress + 1;
  Serial.printf("[INFO] Reading range 0x%08X to 0x%08X (%u bytes)\n", 
                startAddress, endAddress, length);
  
  return flashRead(startAddress, buffer, length);
}

/**
 * @brief Erase entire flash memory chip
 * @return true if successful, false otherwise
 */
bool flashEraseAll() {
  if (!flashInitialized) {
    return false;
  }
  
  Serial.println("[INFO] Erasing entire flash memory...");
  Serial.println("[WARNING] This may take several seconds...");
  
  xSemaphoreTake(spiMutex, portMAX_DELAY);
  bool success = flash.eraseChip();
  xSemaphoreGive(spiMutex);
  
  if (success) {
    Serial.println("[INFO] Chip erase complete!");
  } else {
    Serial.println("[ERROR] Chip erase failed!");
  }
  
  return success;
}

/**
 * @brief Erase a single sector at specified address
 * @param address Address within the sector to erase (will erase entire 4KB sector)
 * @return true if successful, false otherwise
 */
bool flashEraseSector(uint32_t address) {
  if (!flashInitialized) {
    return false;
  }
  
  if (address >= FLASH_TOTAL_SIZE) {
    Serial.println("[ERROR] Erase address out of bounds");
    return false;
  }
  
  uint32_t sectorNum = address / FLASH_SECTOR_SIZE;
  Serial.printf("[INFO] Erasing sector %u at address 0x%08X\n", sectorNum, address);
  
  xSemaphoreTake(spiMutex, portMAX_DELAY);
  bool success = flash.eraseSector(address);
  xSemaphoreGive(spiMutex);
  
  return success;
}

/**
 * @brief Erase multiple sectors in specified address range
 * @param startAddress Starting address (will round down to sector boundary)
 * @param endAddress Ending address (will round up to sector boundary)
 * @return true if successful, false otherwise
 */
bool flashEraseRange(uint32_t startAddress, uint32_t endAddress) {
  if (!flashInitialized) {
    return false;
  }
  
  if (startAddress > endAddress || endAddress >= FLASH_TOTAL_SIZE) {
    Serial.println("[ERROR] Invalid address range");
    return false;
  }
  
  // Align to sector boundaries
  uint32_t startSector = (startAddress / FLASH_SECTOR_SIZE);
  uint32_t endSector = (endAddress / FLASH_SECTOR_SIZE);
  uint32_t numSectors = endSector - startSector + 1;
  
  Serial.printf("[INFO] Erasing %u sectors (0x%08X to 0x%08X)\n", 
                numSectors, startSector * FLASH_SECTOR_SIZE, 
                (endSector + 1) * FLASH_SECTOR_SIZE - 1);
  
  for (uint32_t sector = startSector; sector <= endSector; sector++) {
    uint32_t sectorAddr = sector * FLASH_SECTOR_SIZE;
    
    if (!flashEraseSector(sectorAddr)) {
      Serial.printf("[ERROR] Failed to erase sector %u\n", sector);
      return false;
    }
    
    Serial.printf("[PROGRESS] Erased sector %u/%u\n", 
                  sector - startSector + 1, numSectors);
  }
  
  Serial.println("[INFO] Range erase complete!");
  return true;
}

//=============================================================================
// FREERTOS TASK FUNCTIONS
//=============================================================================

//=============================================================================
// FREERTOS TASK FUNCTIONS
//=============================================================================

/**
 * @brief FreeRTOS Task: Write test data to flash memory
 */
void writeTask(void *parameter) {
  Serial.println("\n=== WRITE TASK STARTED ===");
  
  // Write string data
  uint32_t addr1 = 0x0000;
  flashEraseSector(addr1);
  
  String testString = "Hello Winbond W25Q32JVSSIQ!";
  Serial.printf("[WRITE] Writing string at 0x%08X: %s\n", addr1, testString.c_str());
  
  if (flashWriteString(addr1, testString)) {
    Serial.println("[WRITE] ✓ String write successful");
  } else {
    Serial.println("[WRITE] ✗ String write failed");
  }
  
  // Write byte array
  uint32_t addr2 = 0x1000;
  flashEraseSector(addr2);
  
  uint8_t testData[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  Serial.printf("[WRITE] Writing 16 bytes at 0x%08X\n", addr2);
  
  if (flashWrite(addr2, testData, 16)) {
    Serial.println("[WRITE] ✓ Byte array write successful");
  } else {
    Serial.println("[WRITE] ✗ Byte array write failed");
  }
  
  // Write large data block
  uint32_t addr3 = 0x2000;
  flashEraseSector(addr3);
  
  uint8_t largeData[256];
  for (int i = 0; i < 256; i++) {
    largeData[i] = i;
  }
  
  Serial.printf("[WRITE] Writing 256 bytes at 0x%08X\n", addr3);
  
  if (flashWrite(addr3, largeData, 256)) {
    Serial.println("[WRITE] ✓ Large block write successful");
  } else {
    Serial.println("[WRITE] ✗ Large block write failed");
  }
  
  Serial.println("=== WRITE TASK COMPLETE ===\n");
  vTaskDelay(pdMS_TO_TICKS(1000));
  
  // Delete this task
  vTaskDelete(NULL);
}

/**
 * @brief FreeRTOS Task: Read and verify data from flash memory
 */
void readTask(void *parameter) {
  Serial.println("\n=== READ TASK STARTED ===");
  
  vTaskDelay(pdMS_TO_TICKS(2000)); // Wait for write task to complete
  
  // Read string data
  uint32_t addr1 = 0x0000;
  String readString = "";
  
  Serial.printf("[READ] Reading string at 0x%08X\n", addr1);
  
  if (flashReadString(addr1, readString)) {
    Serial.printf("[READ] Result: %s\n", readString.c_str());
    Serial.println("[READ] ✓ String read successful");
  } else {
    Serial.println("[READ] ✗ String read failed");
  }
  
  // Read byte array
  uint32_t addr2 = 0x1000;
  uint8_t readData[16];
  
  Serial.printf("[READ] Reading 16 bytes at 0x%08X\n", addr2);
  
  if (flashRead(addr2, readData, 16)) {
    Serial.print("[READ] Result: ");
    for (int i = 0; i < 16; i++) {
      Serial.printf("%d ", readData[i]);
    }
    Serial.println();
    Serial.println("[READ] ✓ Byte array read successful");
  } else {
    Serial.println("[READ] ✗ Byte array read failed");
  }
  
  // Read specific range
  uint32_t rangeStart = 0x2000;
  uint32_t rangeEnd = 0x20FF;
  uint8_t rangeData[256];
  
  if (flashReadRange(rangeStart, rangeEnd, rangeData)) {
    Serial.println("[READ] ✓ Range read successful");
    
    // Verify first 10 bytes
    Serial.print("[READ] First 10 bytes: ");
    for (int i = 0; i < 10; i++) {
      Serial.printf("%d ", rangeData[i]);
    }
    Serial.println();
  } else {
    Serial.println("[READ] ✗ Range read failed");
  }
  
  Serial.println("=== READ TASK COMPLETE ===\n");
  
  // Delete this task
  vTaskDelete(NULL);
}

/**
 * @brief FreeRTOS Task: Erase operations demonstration
 */
void eraseTask(void *parameter) {
  Serial.println("\n=== ERASE TASK STARTED ===");
  
  vTaskDelay(pdMS_TO_TICKS(5000)); // Wait for other tasks
  
  // Erase single sector
  Serial.println("[ERASE] Demonstrating sector erase...");
  flashEraseSector(0x5000);
  
  // Erase range of sectors
  Serial.println("[ERASE] Demonstrating range erase...");
  flashEraseRange(0x10000, 0x13FFF); // Erase 16KB (4 sectors)
  
  // Note: Full chip erase is commented out to prevent accidental data loss
  // Uncomment if you want to test it
  // Serial.println("[ERASE] Erasing entire chip...");
  // flashEraseAll();
  
  Serial.println("=== ERASE TASK COMPLETE ===\n");
  
  // Delete this task
  vTaskDelete(NULL);
}

/**
 * @brief FreeRTOS Task: Monitor system status
 */
void monitorTask(void *parameter) {
  while (1) {
    Serial.println("\n[MONITOR] === System Status ===");
    Serial.printf("[MONITOR] Free heap: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("[MONITOR] Tasks running: %u\n", uxTaskGetNumberOfTasks());
    Serial.printf("[MONITOR] Uptime: %lu seconds\n", millis() / 1000);
    
    if (flashInitialized) {
      xSemaphoreTake(spiMutex, portMAX_DELAY);
      uint32_t jedecID = flash.getJEDECID();
      xSemaphoreGive(spiMutex);
      
      Serial.printf("[MONITOR] Flash JEDEC ID: 0x%08X\n", jedecID);
    }
    
    Serial.println("[MONITOR] ==================\n");
    
    vTaskDelay(pdMS_TO_TICKS(10000)); // Run every 10 seconds
  }
}

/**
 * @brief FreeRTOS Task: Handle Bluetooth commands
 */
void bluetoothTask(void *parameter) {
  // Initialize Bluetooth Commander
  btCommander = new SerialBT_Commander("ESP32-Flash");
  
  if (!btCommander->begin()) {
    Serial.println("[ERROR] Failed to start Bluetooth!");
    vTaskDelete(NULL);
    return;
  }
  
  // Wait for connection
  while (!btCommander->isConnected()) {
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  
  Serial.println("[BT] Client connected!");
  btCommander->println("\n=== ESP32 Flash Memory Controller ===");
  btCommander->println("Connected successfully!");
  btCommander->printMenu();
  
  // Main command processing loop
  while (1) {
    // Check if still connected
    if (!btCommander->isConnected()) {
      Serial.println("[BT] Client disconnected. Waiting for reconnection...");
      while (!btCommander->isConnected()) {
        vTaskDelay(pdMS_TO_TICKS(500));
      }
      Serial.println("[BT] Client reconnected!");
      btCommander->println("\n=== Reconnected ===");
      btCommander->printMenu();
    }
    
    // Process incoming commands
    btCommander->processCommands();
    
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

//=============================================================================
// SETUP AND MAIN LOOP
//=============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== FreeRTOS Flash Memory System ===");
  Serial.println("Initializing Winbond W25Q32JVSSIQ SPI Flash...");
  
  // Configure custom SPI pins
  SPI.begin(SPI_FLASH_CLK, SPI_FLASH_MISO, SPI_FLASH_MOSI, SPI_FLASH_CS);
  
  // Initialize the flash memory
  if (flash.begin()) {
    flashInitialized = true;
    Serial.println("✓ Flash memory initialized successfully!");
    
    // Get flash chip information
    uint32_t jedecID = flash.getJEDECID();
    uint32_t capacity = flash.getCapacity();
    uint16_t maxPages = flash.getMaxPage();
    
    Serial.printf("  JEDEC ID: 0x%08X\n", jedecID);
    Serial.printf("  Capacity: %u bytes (%.2f MB)\n", capacity, capacity / 1048576.0);
    Serial.printf("  Max Pages: %u\n", maxPages);
    Serial.printf("  Sector Size: %u bytes\n", FLASH_SECTOR_SIZE);
  } else {
    Serial.println("✗ Flash memory initialization failed!");
    Serial.println("Please check your wiring:");
    Serial.println("  CLK:  GPIO 14");
    Serial.println("  MISO: GPIO 12");
    Serial.println("  MOSI: GPIO 13");
    Serial.println("  CS:   GPIO 26");
    while (1) {
      delay(1000);
    }
  }
  
  // Create mutex for SPI access
  spiMutex = xSemaphoreCreateMutex();
  
  if (spiMutex == NULL) {
    Serial.println("✗ Failed to create mutex!");
    while (1) {
      delay(1000);
    }
  }
  
  Serial.println("\n=== Creating FreeRTOS Tasks ===");
  
  // Comment out demo tasks - uncomment if you want to run the demos
  /*
  // Create write task
  xTaskCreatePinnedToCore(
    writeTask,            // Task function
    "WriteTask",          // Task name
    4096,                 // Stack size (bytes)
    NULL,                 // Parameter
    2,                    // Priority
    &writeTaskHandle,     // Task handle
    1                     // Core 1
  );
  Serial.println("✓ Write Task created (Core 1, Priority 2)");
  
  // Create read task
  xTaskCreatePinnedToCore(
    readTask,             // Task function
    "ReadTask",           // Task name
    4096,                 // Stack size (bytes)
    NULL,                 // Parameter
    2,                    // Priority
    &readTaskHandle,      // Task handle
    1                     // Core 1
  );
  Serial.println("✓ Read Task created (Core 1, Priority 2)");
  
  // Create erase task
  xTaskCreatePinnedToCore(
    eraseTask,            // Task function
    "EraseTask",          // Task name
    3072,                 // Stack size (bytes)
    NULL,                 // Parameter
    1,                    // Priority
    &eraseTaskHandle,     // Task handle
    1                     // Core 1
  );
  Serial.println("✓ Erase Task created (Core 1, Priority 1)");
  */
  
  // Create Bluetooth command task
  xTaskCreatePinnedToCore(
    bluetoothTask,        // Task function
    "BluetoothTask",      // Task name
    8192,                 // Stack size (bytes)
    NULL,                 // Parameter
    2,                    // Priority
    &bluetoothTaskHandle, // Task handle
    1                     // Core 1
  );
  Serial.println("✓ Bluetooth Task created (Core 1, Priority 2)");
  
  // Create monitor task
  xTaskCreatePinnedToCore(
    monitorTask,          // Task function
    "MonitorTask",        // Task name
    2048,                 // Stack size (bytes)
    NULL,                 // Parameter
    1,                    // Priority
    &monitorTaskHandle,   // Task handle
    0                     // Core 0
  );
  Serial.println("✓ Monitor Task created (Core 0, Priority 1)");
  
  Serial.println("\n=== System Ready ===");
  Serial.println("Waiting for Bluetooth connection...");
}

void loop() {
  // Empty loop - FreeRTOS tasks handle everything
  vTaskDelay(pdMS_TO_TICKS(1000));
}