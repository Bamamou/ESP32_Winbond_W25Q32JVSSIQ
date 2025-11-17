#include "SerialBT_Commander.h"
#include <stdarg.h>

SerialBT_Commander::SerialBT_Commander(const char* deviceName) 
    : btDeviceName(deviceName), commandBuffer("") {
}

bool SerialBT_Commander::begin() {
    if (!SerialBT.begin(btDeviceName)) {
        Serial.println("[BT] Failed to initialize Bluetooth!");
        return false;
    }
    
    Serial.printf("[BT] Bluetooth initialized as '%s'\n", btDeviceName);
    Serial.println("[BT] Waiting for connection...");
    return true;
}

bool SerialBT_Commander::isConnected() {
    return SerialBT.hasClient();
}

void SerialBT_Commander::println(const String& message) {
    SerialBT.println(message);
}

void SerialBT_Commander::print(const String& message) {
    SerialBT.print(message);
}

void SerialBT_Commander::printf(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    SerialBT.print(buffer);
}

uint32_t SerialBT_Commander::parseHex(String hexStr) {
    hexStr.trim();
    return strtoul(hexStr.c_str(), NULL, 16);
}

void SerialBT_Commander::printMenu() {
    println("\n========== FLASH MEMORY COMMANDS ==========");
    println("Write Commands:");
    println("  write <addr> <data>    - Write string to address (hex)");
    println("  writeb <addr> <bytes>  - Write bytes (e.g., writeb 1000 0,1,2,3)");
    println("");
    println("Read Commands:");
    println("  read <addr>            - Read string from address (hex)");
    println("  readb <addr> <len>     - Read bytes (e.g., readb 1000 16)");
    println("  readrange <start> <end> - Read address range (hex)");
    println("  readall                - Dump entire flash (4MB!)");
    println("");
    println("Erase Commands:");
    println("  erase <addr>           - Erase sector at address (hex)");
    println("  eraserange <start> <end> - Erase address range (hex)");
    println("  eraseall               - Erase entire chip (CAUTION!)");
    println("");
    println("Info Commands:");
    println("  info                   - Show flash chip information");
    println("  help                   - Show this menu");
    println("===========================================\n");
}

void SerialBT_Commander::handleWriteCommand(String args) {
    int dataIdx = args.indexOf(' ');
    if (dataIdx > 0) {
        String addrStr = args.substring(0, dataIdx);
        String data = args.substring(dataIdx + 1);
        uint32_t addr = parseHex(addrStr);
        
        printf("[BT] Writing string to 0x%08X: %s\n", addr, data.c_str());
        flashEraseSector(addr);
        
        if (flashWriteString(addr, data)) {
            println("[BT] ✓ Write successful");
        } else {
            println("[BT] ✗ Write failed");
        }
    } else {
        println("[ERROR] Usage: write <addr> <data>");
    }
}

void SerialBT_Commander::handleWriteBytesCommand(String args) {
    int dataIdx = args.indexOf(' ');
    if (dataIdx > 0) {
        String addrStr = args.substring(0, dataIdx);
        String bytesStr = args.substring(dataIdx + 1);
        uint32_t addr = parseHex(addrStr);
        
        // Parse comma-separated bytes
        uint8_t bytes[256];
        int count = 0;
        int lastIdx = 0;
        
        for (int i = 0; i <= bytesStr.length(); i++) {
            if (i == bytesStr.length() || bytesStr.charAt(i) == ',') {
                String byteStr = bytesStr.substring(lastIdx, i);
                byteStr.trim();
                bytes[count++] = byteStr.toInt();
                lastIdx = i + 1;
                if (count >= 256) break;
            }
        }
        
        printf("[BT] Writing %d bytes to 0x%08X\n", count, addr);
        flashEraseSector(addr);
        
        if (flashWrite(addr, bytes, count)) {
            println("[BT] ✓ Write successful");
        } else {
            println("[BT] ✗ Write failed");
        }
    } else {
        println("[ERROR] Usage: writeb <addr> <byte1,byte2,...>");
    }
}

void SerialBT_Commander::handleReadCommand(String args) {
    if (args.length() > 0) {
        uint32_t addr = parseHex(args);
        String data = "";
        
        printf("[BT] Reading string from 0x%08X\n", addr);
        
        if (flashReadString(addr, data)) {
            printf("[BT] Result: %s\n", data.c_str());
        } else {
            println("[BT] ✗ Read failed");
        }
    } else {
        println("[ERROR] Usage: read <addr>");
    }
}

void SerialBT_Commander::handleReadBytesCommand(String args) {
    int lenIdx = args.indexOf(' ');
    if (lenIdx > 0) {
        String addrStr = args.substring(0, lenIdx);
        String lenStr = args.substring(lenIdx + 1);
        uint32_t addr = parseHex(addrStr);
        uint32_t len = lenStr.toInt();
        
        if (len > 0 && len <= 256) {
            uint8_t buffer[256];
            printf("[BT] Reading %u bytes from 0x%08X\n", len, addr);
            
            if (flashRead(addr, buffer, len)) {
                print("[BT] Result: ");
                for (uint32_t i = 0; i < len; i++) {
                    printf("%02X ", buffer[i]);
                    if ((i + 1) % 16 == 0) println("");
                }
                println("");
            } else {
                println("[BT] ✗ Read failed");
            }
        } else {
            println("[ERROR] Length must be 1-256");
        }
    } else {
        println("[ERROR] Usage: readb <addr> <length>");
    }
}

void SerialBT_Commander::handleReadRangeCommand(String args) {
    int endIdx = args.indexOf(' ');
    if (endIdx > 0) {
        String startStr = args.substring(0, endIdx);
        String endStr = args.substring(endIdx + 1);
        uint32_t startAddr = parseHex(startStr);
        uint32_t endAddr = parseHex(endStr);
        uint32_t len = endAddr - startAddr + 1;
        
        if (len <= 256) {
            uint8_t buffer[256];
            
            if (flashReadRange(startAddr, endAddr, buffer)) {
                printf("[BT] Read %u bytes:\n", len);
                for (uint32_t i = 0; i < len; i++) {
                    printf("%02X ", buffer[i]);
                    if ((i + 1) % 16 == 0) println("");
                }
                println("");
            }
        } else {
            println("[ERROR] Range too large (max 256 bytes)");
        }
    } else {
        println("[ERROR] Usage: readrange <start> <end>");
    }
}

void SerialBT_Commander::handleEraseCommand(String args) {
    if (args.length() > 0) {
        uint32_t addr = parseHex(args);
        flashEraseSector(addr);
    } else {
        println("[ERROR] Usage: erase <addr>");
    }
}

void SerialBT_Commander::handleEraseRangeCommand(String args) {
    int endIdx = args.indexOf(' ');
    if (endIdx > 0) {
        String startStr = args.substring(0, endIdx);
        String endStr = args.substring(endIdx + 1);
        uint32_t startAddr = parseHex(startStr);
        uint32_t endAddr = parseHex(endStr);
        
        flashEraseRange(startAddr, endAddr);
    } else {
        println("[ERROR] Usage: eraserange <start> <end>");
    }
}

void SerialBT_Commander::handleEraseAllCommand() {
    println("[BT] WARNING: This will erase ALL data!");
    println("[BT] Type 'yes' to confirm within 5 seconds: ");
    
    // Wait for confirmation
    unsigned long timeout = millis() + 5000;
    String confirm = "";
    while (millis() < timeout) {
        if (SerialBT.available()) {
            confirm = SerialBT.readStringUntil('\n');
            confirm.trim();
            break;
        }
        delay(100);
    }
    
    if (confirm == "yes") {
        flashEraseAll();
    } else {
        println("[BT] Erase cancelled");
    }
}

void SerialBT_Commander::handleInfoCommand() {
    if (flashInitialized) {
        xSemaphoreTake(spiMutex, portMAX_DELAY);
        uint32_t jedecID = flash.getJEDECID();
        uint32_t capacity = flash.getCapacity();
        uint16_t maxPages = flash.getMaxPage();
        xSemaphoreGive(spiMutex);
        
        println("\n[INFO] Flash Chip Information:");
        printf("  JEDEC ID: 0x%08X\n", jedecID);
        printf("  Capacity: %u bytes (%.2f MB)\n", capacity, capacity / 1048576.0);
        printf("  Max Pages: %u\n", maxPages);
        printf("  Sector Size: %u bytes\n", FLASH_SECTOR_SIZE);
    } else {
        println("[ERROR] Flash not initialized!");
    }
}

void SerialBT_Commander::processCommand(String cmd) {
    cmd.trim();
    cmd.toLowerCase();
    
    if (cmd.length() == 0) return;
    
    // Parse command and arguments
    int spaceIdx = cmd.indexOf(' ');
    String command = (spaceIdx > 0) ? cmd.substring(0, spaceIdx) : cmd;
    String args = (spaceIdx > 0) ? cmd.substring(spaceIdx + 1) : "";
    
    // Route commands to handlers
    if (command == "help") {
        printMenu();
    }
    else if (command == "info") {
        handleInfoCommand();
    }
    else if (command == "write") {
        handleWriteCommand(args);
    }
    else if (command == "writeb") {
        handleWriteBytesCommand(args);
    }
    else if (command == "read") {
        handleReadCommand(args);
    }
    else if (command == "readb") {
        handleReadBytesCommand(args);
    }
    else if (command == "readrange") {
        handleReadRangeCommand(args);
    }
    else if (command == "erase") {
        handleEraseCommand(args);
    }
    else if (command == "eraserange") {
        handleEraseRangeCommand(args);
    }
    else if (command == "readall") {
        handleReadAllCommand();
    }
    else if (command == "eraseall") {
        handleEraseAllCommand();
    }
    else {
        printf("[ERROR] Unknown command: %s\n", command.c_str());
        println("[INFO] Type 'help' for available commands");
    }
}

void SerialBT_Commander::handleReadAllCommand() {
    println("[BT] Starting full flash dump (4MB)...");
    println("[BT] This will take several minutes...\n");
    
    // Redirect output through Bluetooth
    uint8_t buffer[256];
    uint32_t totalBytes = 0;
    const uint32_t FLASH_SIZE = 4194304;
    
    println("\n========== FLASH MEMORY DUMP START ==========");
    printf("Total Size: %u bytes (4.00 MB)\n", FLASH_SIZE);
    println("Format: [Address] Data (16 bytes per line)");
    println("=============================================\n");
    
    for (uint32_t addr = 0; addr < FLASH_SIZE; addr += 256) {
        if (!flashRead(addr, buffer, 256)) {
            printf("[ERROR] Failed to read at 0x%08X\n", addr);
            break;
        }
        
        // Print in hex format
        for (size_t i = 0; i < 256; i++) {
            if ((addr + i) % 16 == 0) {
                if (i > 0) println("");
                printf("[%08X] ", addr + i);
            }
            printf("%02X ", buffer[i]);
            totalBytes++;
        }
        
        // Progress every 64KB
        if ((addr % (64 * 1024)) == 0 && addr > 0) {
            printf("\n[PROGRESS] %u%% - %u KB\n", 
                  (addr * 100) / FLASH_SIZE, addr / 1024);
        }
        
        delay(10); // Prevent watchdog and allow BT buffer to flush
    }
    
    println("\n\n========== FLASH MEMORY DUMP COMPLETE ==========");
    printf("Total bytes read: %u (4.00 MB)\n", totalBytes);
    println("================================================\n");
}

void SerialBT_Commander::processCommands() {
    if (SerialBT.available()) {
        char c = SerialBT.read();
        
        if (c == '\n' || c == '\r') {
            if (commandBuffer.length() > 0) {
                processCommand(commandBuffer);
                commandBuffer = "";
            }
        } else {
            commandBuffer += c;
        }
    }
}
