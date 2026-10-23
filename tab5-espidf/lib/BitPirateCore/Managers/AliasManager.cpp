#include "AliasManager.h"
#include <cctype>

bool AliasManager::add(const std::string& from, const std::string& to) {
    std::string key = from;
    std::string value = to;

    if (key.empty() || value.empty()) return false;
    if (aliases.size() >= kMaxAliasCount) return false;

    aliases[key] = value;
    return true;
}

bool AliasManager::remove(const std::string& from) {
    return aliases.erase(from) > 0;
}

bool AliasManager::has(const std::string& from) const {
    return aliases.find(from) != aliases.end();
}

void AliasManager::clear() {
    aliases.clear();
}

size_t AliasManager::size() const {
    return aliases.size();
}

size_t AliasManager::sizeMax() const {
    return kMaxAliasCount;
}

const std::string& AliasManager::expand(const std::string& line) {
    auto it = aliases.find(line);
    
    if (it != aliases.end()) {
        return it->second;   // alias found
    } 

    return line;
}