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
TaskHandle_t autoWriteTaskHandle = NULL;

// Semaphore for SPI access
SemaphoreHandle_t spiMutex;

// Flash initialization status
bool flashInitialized = false;

// Auto-write control
bool autoWriteEnabled = false;

// Bluetooth Commander instance
SerialBT_Commander* btCommander = nullptr;

//=============================================================================
// DATA STRUCTURES FOR VEHICLE LOGGING
//=============================================================================

#define MAXPAGESIZE 256  // Maximum data log size

// Vehicle Info Structure
struct {
  float odometerKm;
  float tripKm;
  float speedKmh;
  bool isInReverseMode;
  uint8_t ridingMode;
} vehicleInfo;

// MCU Data Structure
struct {
  float busCurrent;
  uint8_t throttle;
  float controllerTemperature;
  float motorTemperature;
} MCUData;

// BMS Data Structure
struct {
  float current;
  float voltage;
  uint8_t SOC;
} BMSData;

// Info to Save Structure
struct {
  float odometerKm;
  float tripKm;
  float speedKmh;
  uint8_t vehicleStatuByte1;
  uint8_t vehicleStatuByte2;
  float BMSCellHighestVoltageValue;
  float BMSCellLowestVoltageValue;
  uint16_t rpm;
  float boardSupplyVoltage;
  float chargerVoltage;
  float chargerCurrent;
  uint8_t numActiveErrors;
  uint16_t sumActiveErrors;
} infoToSave;

// Input Switches Structure
struct {
  bool headlightHighBeam;
  bool turnLeftSwitch;
  bool turnRightSwitch;
  bool modeButton;
  bool kickstand;
  bool killswitch;
  bool key;
  bool breakSwitch;
} inputs;

/**
 * @brief Generate simulated vehicle data for logging
 * Updates all vehicle data structures with random/simulated values
 */
void generateSimulatedData() {
  // Generate realistic vehicle data
  static float odometer = 0.0;
  static float trip = 0.0;
  
  // Speed varies between 0-100 km/h
  float speed = random(0, 10000) / 100.0;
  vehicleInfo.speedKmh = speed;
  infoToSave.speedKmh = speed;
  
  // Update odometer and trip (increment based on speed)
  odometer += speed / 3600.0;  // km per second
  trip += speed / 3600.0;
  vehicleInfo.odometerKm = odometer;
  vehicleInfo.tripKm = trip;
  infoToSave.odometerKm = odometer;
  infoToSave.tripKm = trip;
  
  // Reverse mode (10% chance)
  vehicleInfo.isInReverseMode = (random(0, 10) == 0);
  
  // Riding mode (0-3: Eco, Normal, Sport, Custom)
  vehicleInfo.ridingMode = random(0, 4);
  
  // MCU Data
  MCUData.busCurrent = random(0, 15000) / 100.0;  // 0-150A
  MCUData.throttle = random(0, 101);  // 0-100%
  MCUData.controllerTemperature = random(2000, 8000) / 100.0;  // 20-80°C
  MCUData.motorTemperature = random(2500, 9000) / 100.0;  // 25-90°C
  
  // BMS Data
  BMSData.current = random(-5000, 15000) / 100.0;  // -50A to 150A
  BMSData.voltage = random(4800, 5800) / 100.0;  // 48-58V
  BMSData.SOC = random(10, 101);  // 10-100%
  
  // Cell voltages
  infoToSave.BMSCellHighestVoltageValue = random(360, 420) / 100.0;  // 3.6-4.2V
  infoToSave.BMSCellLowestVoltageValue = random(340, 400) / 100.0;  // 3.4-4.0V
  
  // RPM
  infoToSave.rpm = random(0, 5000);  // 0-5000 RPM
  
  // Voltages
  infoToSave.boardSupplyVoltage = random(1150, 1350) / 100.0;  // 11.5-13.5V
  infoToSave.chargerVoltage = random(0, 6000) / 100.0;  // 0-60V
  infoToSave.chargerCurrent = random(0, 1000) / 100.0;  // 0-10A
  
  // Status bytes (random bits)
  infoToSave.vehicleStatuByte1 = random(0, 256);
  infoToSave.vehicleStatuByte2 = random(0, 256);
  
  // Errors
  infoToSave.numActiveErrors = random(0, 5);
  infoToSave.sumActiveErrors = random(0, 100);
  
  // Input switches (random boolean states)
  inputs.headlightHighBeam = random(0, 2);
  inputs.turnLeftSwitch = random(0, 2);
  inputs.turnRightSwitch = random(0, 2);
  inputs.modeButton = random(0, 2);
  inputs.kickstand = random(0, 2);
  inputs.killswitch = random(0, 2);
  inputs.key = random(0, 2);
  inputs.breakSwitch = random(0, 2);
}

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
 * @brief Dump entire flash memory to Serial output in hex format
 * This is practical for viewing/saving flash contents without needing 4MB RAM
 * @param chunkSize Size of chunks to read at a time (default 256 bytes)
 */
void flashDumpAll(size_t chunkSize = 256) {
  if (!flashInitialized) {
    Serial.println("[ERROR] Flash not initialized!");
    return;
  }
  
  uint8_t* buffer = (uint8_t*)malloc(chunkSize);
  if (buffer == NULL) {
    Serial.println("[ERROR] Failed to allocate memory!");
    return;
  }
  
  Serial.println("\n========== FLASH MEMORY DUMP START ==========");
  Serial.printf("Total Size: %u bytes (%.2f MB)\n", FLASH_TOTAL_SIZE, FLASH_TOTAL_SIZE / 1048576.0);
  Serial.println("Format: [Address] Data (16 bytes per line)");
  Serial.println("=============================================\n");
  
  uint32_t totalBytes = 0;
  
  for (uint32_t addr = 0; addr < FLASH_TOTAL_SIZE; addr += chunkSize) {
    // Read chunk
    if (!flashRead(addr, buffer, chunkSize)) {
      Serial.printf("[ERROR] Failed to read at 0x%08X\n", addr);
      break;
    }
    
    // Print chunk in hex format (16 bytes per line)
    for (size_t i = 0; i < chunkSize; i++) {
      if ((addr + i) % 16 == 0) {
        if (i > 0) Serial.println();
        Serial.printf("[%08X] ", addr + i);
      }
      Serial.printf("%02X ", buffer[i]);
      totalBytes++;
    }
    
    // Progress update every 64KB
    if ((addr % (64 * 1024)) == 0 && addr > 0) {
      Serial.printf("\n[PROGRESS] %u%% - %u KB read\n", 
                    (addr * 100) / FLASH_TOTAL_SIZE, addr / 1024);
    }
    
    // Allow watchdog reset
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  
  Serial.println("\n\n========== FLASH MEMORY DUMP COMPLETE ==========");
  Serial.printf("Total bytes read: %u (%.2f MB)\n", totalBytes, totalBytes / 1048576.0);
  Serial.println("================================================\n");
  
  free(buffer);
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
// RING BUFFER MANAGEMENT
//=============================================================================

// Ring buffer state
uint32_t ringBufferWriteAddress = 0;  // Current write position
bool ringBufferInitialized = false;
bool ringBufferPaused = false;  // Flag to pause ring buffer writes

/**
 * @brief Initialize ring buffer by finding the first empty sector
 * Scans flash to find where to start writing
 * @return true if successful, false otherwise
 */
bool flashRingBufferInit() {
  if (!flashInitialized) {
    Serial.println("[ERROR] Flash not initialized!");
    return false;
  }
  
  Serial.println("[RING] Initializing ring buffer...");
  Serial.println("[RING] Scanning for first empty sector...");
  
  // Scan flash to find first non-empty (0xFF) sector
  uint8_t buffer[256];
  bool foundData = false;
  uint32_t lastDataSector = 0;
  
  for (uint32_t sector = 0; sector < (FLASH_TOTAL_SIZE / FLASH_SECTOR_SIZE); sector++) {
    uint32_t addr = sector * FLASH_SECTOR_SIZE;
    
    // Read first 256 bytes of sector
    if (!flashRead(addr, buffer, 256)) {
      Serial.printf("[ERROR] Failed to read sector %u\n", sector);
      return false;
    }
    
    // Check if sector contains data (not all 0xFF)
    bool isEmpty = true;
    for (int i = 0; i < 256; i++) {
      if (buffer[i] != 0xFF) {
        isEmpty = false;
        foundData = true;
        lastDataSector = sector;
        break;
      }
    }
    
    // If we found data before and now found empty sector, start here
    if (foundData && isEmpty) {
      ringBufferWriteAddress = addr;
      ringBufferInitialized = true;
      Serial.printf("[RING] Write position set to sector %u (address 0x%08X)\n", 
                    sector, ringBufferWriteAddress);
      return true;
    }
    
    // Progress indicator
    if (sector % 100 == 0) {
      Serial.printf("[RING] Scanned %u/%u sectors...\n", 
                    sector, FLASH_TOTAL_SIZE / FLASH_SECTOR_SIZE);
    }
  }
  
  // If no empty sector found, start after last data sector
  if (foundData) {
    ringBufferWriteAddress = ((lastDataSector + 1) % (FLASH_TOTAL_SIZE / FLASH_SECTOR_SIZE)) * FLASH_SECTOR_SIZE;
  } else {
    // Flash is empty, start at beginning
    ringBufferWriteAddress = 0;
  }
  
  ringBufferInitialized = true;
  Serial.printf("[RING] Write position set to address 0x%08X\n", ringBufferWriteAddress);
  return true;
}

/**
 * @brief Write data in ring buffer mode
 * Automatically wraps around to beginning and erases next sector before writing
 * @param data Pointer to data to write
 * @param length Length of data
 * @return true if successful, false otherwise
 */
bool flashRingBufferWrite(const uint8_t* data, size_t length) {
  if (!flashInitialized || !ringBufferInitialized) {
    Serial.println("[ERROR] Ring buffer not initialized! Call flashRingBufferInit() first");
    return false;
  }
  
  if (ringBufferPaused) {
    Serial.println("[WARNING] Ring buffer is paused (read operation in progress)");
    return false;
  }
  
  if (data == NULL || length == 0) {
    return false;
  }
  
  Serial.printf("[RING] Writing %u bytes at 0x%08X\n", length, ringBufferWriteAddress);
  
  size_t bytesWritten = 0;
  
  while (bytesWritten < length) {
    // Calculate current sector
    uint32_t currentSector = ringBufferWriteAddress / FLASH_SECTOR_SIZE;
    uint32_t sectorOffset = ringBufferWriteAddress % FLASH_SECTOR_SIZE;
    
    // If at sector boundary, erase the sector first
    if (sectorOffset == 0) {
      Serial.printf("[RING] Erasing sector %u at 0x%08X\n", 
                    currentSector, ringBufferWriteAddress);
      if (!flashEraseSector(ringBufferWriteAddress)) {
        Serial.println("[ERROR] Failed to erase sector");
        return false;
      }
    }
    
    // Calculate how much we can write in current sector
    size_t remainingInSector = FLASH_SECTOR_SIZE - sectorOffset;
    size_t toWrite = (length - bytesWritten) < remainingInSector ? 
                     (length - bytesWritten) : remainingInSector;
    
    // Write data
    if (!flashWrite(ringBufferWriteAddress, &data[bytesWritten], toWrite)) {
      Serial.println("[ERROR] Failed to write data");
      return false;
    }
    
    bytesWritten += toWrite;
    ringBufferWriteAddress += toWrite;
    
    // Wrap around if we reach end of flash
    if (ringBufferWriteAddress >= FLASH_TOTAL_SIZE) {
      Serial.println("[RING] Wrapping around to beginning of flash");
      ringBufferWriteAddress = 0;
    }
  }
  
  Serial.printf("[RING] Write complete. Next write address: 0x%08X\n", ringBufferWriteAddress);
  return true;
}

/**
 * @brief Write string in ring buffer mode
 * @param str String to write
 * @return true if successful, false otherwise
 */
bool flashRingBufferWriteString(const String& str) {
  // Add null terminator
  size_t len = str.length() + 1;
  uint8_t* buffer = (uint8_t*)malloc(len);
  if (buffer == NULL) {
    Serial.println("[ERROR] Failed to allocate memory");
    return false;
  }
  
  str.getBytes(buffer, len);
  bool result = flashRingBufferWrite(buffer, len);
  free(buffer);
  
  return result;
}

/**
 * @brief Get current ring buffer write position
 * @return Current write address
 */
uint32_t flashRingBufferGetPosition() {
  return ringBufferWriteAddress;
}

/**
 * @brief Set ring buffer write position manually
 * @param address New write address
 * @return true if successful, false otherwise
 */
bool flashRingBufferSetPosition(uint32_t address) {
  if (address >= FLASH_TOTAL_SIZE) {
    Serial.println("[ERROR] Address out of bounds");
    return false;
  }
  
  // Align to sector boundary
  address = (address / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;
  
  ringBufferWriteAddress = address;
  ringBufferInitialized = true;
  
  Serial.printf("[RING] Write position set to 0x%08X\n", ringBufferWriteAddress);
  return true;
}

/**
 * @brief Reset ring buffer to start of flash
 */
void flashRingBufferReset() {
  ringBufferWriteAddress = 0;
  ringBufferInitialized = true;
  Serial.println("[RING] Ring buffer reset to address 0x00000000");
}

/**
 * @brief Pause ring buffer writes (e.g., during read operations)
 */
void flashRingBufferPause() {
  ringBufferPaused = true;
  Serial.println("[RING] Ring buffer writes paused");
}

/**
 * @brief Resume ring buffer writes
 */
void flashRingBufferResume() {
  ringBufferPaused = false;
  Serial.println("[RING] Ring buffer writes resumed");
}

/**
 * @brief Check if ring buffer is paused
 * @return true if paused, false otherwise
 */
bool flashRingBufferIsPaused() {
  return ringBufferPaused;
}

//=============================================================================
// AUTO-WRITE TASK
//=============================================================================

/**
 * @brief Task that automatically writes vehicle data to ring buffer
 * Writes complete vehicle data log every second
 * Automatically handles ring buffer wrapping
 */
void autoWriteTask(void* parameter) {
  Serial.println("[AUTO] Auto-write task started");
  Serial.println("[AUTO] Waiting for ring buffer initialization...");
  
  // Wait for ring buffer to be initialized
  while (!ringBufferInitialized) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  
  Serial.println("[AUTO] Ring buffer initialized, starting auto-write");
  uint32_t writeCount = 0;
  
  while (true) {
    if (autoWriteEnabled && !ringBufferPaused) {
      // Generate simulated vehicle data
      generateSimulatedData();
      
      // Prepare the dataset
      String datalog = ";";
      datalog.concat(infoToSave.odometerKm);
      datalog.concat(";");
      datalog.concat(infoToSave.tripKm);
      datalog.concat(";");
      datalog.concat(infoToSave.speedKmh);
      datalog.concat(";");
      datalog.concat(vehicleInfo.isInReverseMode);
      datalog.concat(";");
      datalog.concat(vehicleInfo.ridingMode);
      datalog.concat(";");
      datalog.concat(MCUData.busCurrent);
      datalog.concat(";");
      datalog.concat(BMSData.current);
      datalog.concat(";");
      datalog.concat(infoToSave.vehicleStatuByte1);
      datalog.concat(";");
      datalog.concat(infoToSave.vehicleStatuByte2);
      datalog.concat(";");
      datalog.concat(MCUData.throttle);
      datalog.concat(";");
      datalog.concat(MCUData.controllerTemperature);
      datalog.concat(";");
      datalog.concat(MCUData.motorTemperature);
      datalog.concat(";");
      datalog.concat(BMSData.voltage);
      datalog.concat(";");
      datalog.concat(infoToSave.BMSCellHighestVoltageValue);
      datalog.concat(";");
      datalog.concat(infoToSave.BMSCellLowestVoltageValue);
      datalog.concat(";");
      datalog.concat(BMSData.SOC);
      datalog.concat(";");
      datalog.concat(infoToSave.rpm);
      datalog.concat(";");
      datalog.concat(infoToSave.boardSupplyVoltage);
      datalog.concat(";");
      datalog.concat(infoToSave.chargerVoltage);
      datalog.concat(";");
      datalog.concat(infoToSave.chargerCurrent);
      datalog.concat(";");
      datalog.concat(infoToSave.numActiveErrors);
      datalog.concat(";");
      datalog.concat(infoToSave.sumActiveErrors);
      datalog.concat(";");
      datalog.concat(inputs.headlightHighBeam);
      datalog.concat(";");
      datalog.concat(inputs.turnLeftSwitch);
      datalog.concat(";");
      datalog.concat(inputs.turnRightSwitch);
      datalog.concat(";");
      datalog.concat(inputs.modeButton);
      datalog.concat(";");
      datalog.concat(inputs.kickstand);
      datalog.concat(";");
      datalog.concat(inputs.killswitch);
      datalog.concat(";");
      datalog.concat(inputs.key);
      datalog.concat(";");
      datalog.concat(inputs.breakSwitch);
      datalog.concat(";");
      
      // Pad to MAXPAGESIZE with dots
      for(int i = datalog.length(); i < MAXPAGESIZE-1; i++){
        datalog.concat(".");
      }
      
      // Write to ring buffer
      if (flashRingBufferWriteString(datalog)) {
        writeCount++;
        Serial.printf("[AUTO] #%u: Logged vehicle data at 0x%08X (Speed: %.1f km/h, SOC: %d%%)\n", 
                      writeCount, 
                      flashRingBufferGetPosition() - datalog.length(), 
                      vehicleInfo.speedKmh,
                      BMSData.SOC);
      } else {
        Serial.println("[AUTO] Write failed!");
      }
    }
    
    // Wait 1 second before next write
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

/**
 * @brief Start automatic writing to ring buffer
 */
void startAutoWrite() {
  if (!autoWriteEnabled) {
    autoWriteEnabled = true;
    Serial.println("[AUTO] Auto-write ENABLED");
    
    // Create auto-write task if it doesn't exist
    if (autoWriteTaskHandle == NULL) {
      xTaskCreatePinnedToCore(
        autoWriteTask,
        "AutoWrite",
        4096,
        NULL,
        1,
        &autoWriteTaskHandle,
        1  // Run on core 1
      );
    }
  } else {
    Serial.println("[AUTO] Auto-write already enabled");
  }
}

/**
 * @brief Stop automatic writing to ring buffer
 */
void stopAutoWrite() {
  if (autoWriteEnabled) {
    autoWriteEnabled = false;
    Serial.println("[AUTO] Auto-write DISABLED");
  } else {
    Serial.println("[AUTO] Auto-write already disabled");
  }
}

/**
 * @brief Check if auto-write is enabled
 */
bool isAutoWriteEnabled() {
  return autoWriteEnabled;
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