#ifndef VORTEX_CUCKOO_H
#define VORTEX_CUCKOO_H

#include <string>
#include <vector>
#include <cstdint>

class CuckooFilter {
private:
    static const size_t BUCKET_SIZE = 4;   // 4 slots per bucket
    static const size_t MAX_KICKS = 500;   

    size_t num_buckets;
    size_t num_items;
    
    std::vector<std::vector<uint8_t>> buckets;

    uint32_t hash_string(const std::string& str) const;
    uint32_t hash_int(uint32_t val) const;
    uint8_t create_fingerprint(uint32_t hash) const;
    size_t get_index(uint32_t hash) const;
    size_t get_alt_index(size_t index, uint8_t fingerprint) const;

public:
    CuckooFilter(size_t capacity = 10000);
    
    bool insert(const std::string& item);
    bool contains(const std::string& item) const;
    bool remove(const std::string& item);
    
    size_t size() const { return num_items; }
};

#endif