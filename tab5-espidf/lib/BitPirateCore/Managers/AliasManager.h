#pragma once
#include <string>
#include <vector>
#include <unordered_map>

class AliasManager {
public:
    bool add(const std::string& from, const std::string& to);
    bool remove(const std::string& from);
    bool has(const std::string& from) const;
    size_t size() const;
    size_t sizeMax() const;
    void clear();
    
    // Expand a full line if it matches an alias
    const std::string& expand(const std::string& line);
private:
    std::unordered_map<std::string, std::string> aliases;
    static constexpr int kMaxAliasCount = 24;
};