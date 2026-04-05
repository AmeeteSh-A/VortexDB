#ifndef VORTEX_VLOG_H
#define VORTEX_VLOG_H

#include <string>
#include <fstream>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <list>

enum class ValueType : uint8_t {
    STRING = 0x00,
    INT = 0x01,
    DOUBLE = 0x02,
    BOOL = 0x03,
    JSON = 0x04,
    SUB_VORTEX = 0x05,
    TOMBSTONE = 0xFF 
};

struct VortexData {
    ValueType type;
    std::string value;
};

struct ValuePointer {
    uint32_t file_id;
    uint32_t value_size; 
    uint64_t offset; 

    bool operator==(const ValuePointer& other) const {
        return file_id == other.file_id && offset == other.offset;
    }
};

struct ValuePointerHash {
    size_t operator()(const ValuePointer& ptr) const {
        return std::hash<uint32_t>{}(ptr.file_id) ^ (std::hash<uint64_t>{}(ptr.offset) << 1);
    }
};

class VLogWriter {
private:
    std::ofstream log_file;
    std::string base_path;
    uint32_t current_file_id;
    uint64_t current_size; 
    const uint64_t MAX_LOG_SIZE = 5 * 1024 * 1024;

    static const size_t BUFFER_SIZE = 64 * 1024; 
    char buffer[BUFFER_SIZE];
    size_t buffer_pos;
    
    bool is_dirty; 

    void rotate();

public:
    VLogWriter(const std::string& filepath, uint32_t start_file_id);
    ~VLogWriter();
    
    void flush(bool force = false);
    
    ValuePointer append_entry(const std::string& key, const std::string& value, ValueType type);
    uint32_t get_current_id() const { return current_file_id; }
};

class VLogReader {
private:
    std::string base_path;
    std::unordered_map<uint32_t, std::ifstream*> handle_cache;

    struct CacheEntry {
        ValuePointer ptr;
        VortexData data;
    };

    std::list<CacheEntry> lru_list;
    std::unordered_map<ValuePointer, std::list<CacheEntry>::iterator, ValuePointerHash> block_cache;
    
    const size_t MAX_CACHE_SIZE = 2000; 

    std::ifstream* get_handle(uint32_t file_id);

public:
    VLogReader(const std::string& filepath);
    ~VLogReader();
    
    VortexData read_value(ValuePointer ptr);
    bool read_next_entry(uint32_t file_id, std::string& key, VortexData& data, ValuePointer& ptr, uint64_t& current_pos);

    void invalidate(uint32_t file_id);
    void close_all();
};

#endif