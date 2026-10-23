#include "SdService.h"

SdService::SdService() {}

bool SdService::configure(uint8_t clkPin, uint8_t misoPin, uint8_t mosiPin, uint8_t csPin) {
    if (sdCardMounted) return true;

    SPI.begin(clkPin, misoPin, mosiPin, csPin);
    delay(10);

    if (!SD.begin(csPin, SPI)) {
        sdCardMounted = false;
        return false;
    }

    sdCardMounted = true;
    return sdCardMounted;
}

void SdService::end() {
    SD.end();
    SPI.end();
    sdCardMounted = false;
}

bool SdService::isFile(const std::string& filePath) {
    File f = SD.open(filePath.c_str());
    if (f && !f.isDirectory()) {
        f.close();
        return true;
    }
    return false;
}

bool SdService::isDirectory(const std::string& path) {
    File f = SD.open(path.c_str());
    if (f && f.isDirectory()) {
        f.close();
        return true;
    }
    return false;
}

bool SdService::getSdState() {
    return sdCardMounted;
}

std::vector<std::string> SdService::listElements(const std::string& dirPath, size_t limit) {
    if (limit == 0) {
        limit = 256;
    }

    std::vector<std::string> filesList;
    std::vector<std::string> foldersList;

    if (!sdCardMounted) {
        return filesList;
    }

    File dir = SD.open(dirPath.c_str());
    if (!dir || !dir.isDirectory()) {
        return filesList;
    }

    File file = dir.openNextFile();
    size_t i = 0;
    while (file) {
        if (file.name()[0] != '.') {  // Exclude hidden files
            if (file.isDirectory()) {
                foldersList.push_back(file.name());
            } else {
                filesList.push_back(file.name());
            }
            i++;
        }

        if (i > limit) {
            break;
        }

        file = dir.openNextFile();
    }

    std::sort(foldersList.begin(), foldersList.end());
    std::sort(filesList.begin(), filesList.end());
    foldersList.insert(foldersList.end(), filesList.begin(), filesList.end());

    return foldersList;
}

std::vector<uint8_t> SdService::readBinaryFile(const std::string& filePath) {
    std::vector<uint8_t> content;
    if (!sdCardMounted) {
        return content;
    }

    File file = SD.open(filePath.c_str(), FILE_READ);
    if (file) {
        content.reserve(file.size());
        while (file.available()) {
            content.push_back(file.read());
        }
        file.close();
    }
    return content;
}

std::string SdService::readFile(const std::string& filePath) {
    std::string content;
    if (!sdCardMounted) {
        return content;
    }

    File file = SD.open(filePath.c_str());
    if (file) {
        while (file.available()) {
            content += static_cast<char>(file.read());
        }
        file.close();
    }
    return content;
}

std::string SdService::readFileChunk(const std::string& filePath, size_t offset, size_t maxBytes) {
    std::string content;
    if (!sdCardMounted) return content;

    File file = SD.open(filePath.c_str());
    if (!file) return content;

    // Go to offset
    if (!file.seek(offset)) {
        file.close();
        return content;
    }

    const size_t BUFFER_SIZE = 512;
    char buffer[BUFFER_SIZE];

    size_t totalRead = 0;
    while (file.available() && totalRead < maxBytes) {
        size_t toRead = std::min(BUFFER_SIZE, maxBytes - totalRead);
        size_t bytesRead = file.readBytes(buffer, toRead);
        content.append(buffer, bytesRead);
        totalRead += bytesRead;
    }

    file.close();
    return content;
}

bool SdService::writeFile(const std::string& filePath, const std::string& data, bool append) {
    if (!sdCardMounted) {
        return false;
    }

    File file = SD.open(filePath.c_str(), append ? FILE_APPEND : FILE_WRITE);
    if (file) {
        file.write(reinterpret_cast<const uint8_t*>(data.c_str()), data.size());
        file.close();
        return true;
    }
    return false;
}

bool SdService::writeBinaryFile(const std::string& filePath, const std::vector<uint8_t>& data) {
    if (!sdCardMounted) {
        return false;
    }

    File file = SD.open(filePath.c_str(), FILE_WRITE);
    if (file) {
        file.write(data.data(), data.size());
        file.close();
        return true;
    }
    return false;
}

bool SdService::appendToFile(const std::string& filePath, const std::string& data) {
    if (!sdCardMounted) {
        return false;
    }

    File file = SD.open(filePath.c_str(), FILE_APPEND);
    if (file) {
        file.write(reinterpret_cast<const uint8_t*>(data.c_str()), data.size());
        file.close();
        return true;
    }
    return false;
}

bool SdService::deleteFile(const std::string& filePath) {
    if (!sdCardMounted) {
        return false;
    }

    if (SD.exists(filePath.c_str())) {
        return SD.remove(filePath.c_str());
    }
    return false;
}

std::string SdService::getFileExt(const std::string& path) {
    size_t pos = path.find_last_of('.');
    return (pos != std::string::npos && pos < path.length() - 1) ? path.substr(pos + 1) : "";
}

std::string SdService::getParentDirectory(const std::string& path) {
    size_t pos = path.find_last_of('/');
    return (pos != std::string::npos && pos > 0) ? path.substr(0, pos) : "/";
}

std::vector<std::string> SdService::listElementsCached(const std::string& path) {
    if (cachedDirectoryElements.find(path) != cachedDirectoryElements.end()) {
        return cachedDirectoryElements[path];
    }

    std::vector<std::string> elements = listElements(path);
    if (elements.size() > 4) {
        if (cachedDirectoryElements.size() >= 50) {
            cachedDirectoryElements.erase(cachedDirectoryElements.begin());
        }
        cachedDirectoryElements[path] = elements;
    }
    return elements;
}

void SdService::setCachedDirectoryElements(const std::string& path, const std::vector<std::string>& elements) {
    cachedDirectoryElements[path] = elements;
}

void SdService::removeCachedPath(const std::string& path) {
    cachedDirectoryElements.erase(path);
}

std::string SdService::getFileName(const std::string& path) {
    size_t lastSlash = path.find_last_of('/');
    size_t lastDot = path.find_last_of('.');
    if (lastDot == std::string::npos || lastDot < lastSlash) {
        lastDot = path.length(); // Not ext, get end of path
    }
    size_t start = (lastSlash != std::string::npos) ? lastSlash + 1 : 0;
    return path.substr(start, lastDot - start);
}

bool SdService::ensureDirectory(const std::string& directory) {
    if (!sdCardMounted) {
        return false;
    }

    if (!SD.exists(directory.c_str())) {
        return SD.mkdir(directory.c_str()); // Create forlder
    }
    return true; // Folder already exists
}

File SdService::openFileRead(const std::string& path) {
    if (!sdCardMounted) return File();
    return SD.open(path.c_str(), FILE_READ);
}

File SdService::openFileWrite(const std::string& path) {
    if (!sdCardMounted) return File();
    return SD.open(path.c_str(), FILE_WRITE);
}

bool SdService::deleteDirectory(const std::string& dirPath) {
    if (!sdCardMounted) return false;
    File dir = SD.open(dirPath.c_str());
    if (!dir || !dir.isDirectory()) return false;

    File entry = dir.openNextFile();
    while (entry) {
        std::string entryPath = std::string(dirPath) + "/" + entry.name();
        if (entry.isDirectory()) {
            deleteDirectory(entryPath);
        } else {
            SD.remove(entryPath.c_str());
        }
        entry = dir.openNextFile();
    }

    dir.close();
    return SD.rmdir(dirPath.c_str());
}