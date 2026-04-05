#include "vlog.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace {
    uint32_t calculate_crc32(const char* data, size_t length) {
        uint32_t crc = 0xFFFFFFFF;
        for (size_t i = 0; i < length; i++) {
            crc ^= static_cast<uint8_t>(data[i]);
            for (int j = 0; j < 8; j++) {
                crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
            }
        }
        return ~crc;
    }
}

VLogWriter::VLogWriter(const std::string& filepath, uint32_t start_file_id) 
    : base_path(filepath), current_file_id(start_file_id), current_size(0), buffer_pos(0), is_dirty(false) {
    
    std::string active_segment = base_path + "." + std::to_string(current_file_id);
    log_file.open(active_segment, std::ios::out | std::ios::binary | std::ios::app);
    
    if (fs::exists(active_segment)) {
        current_size = fs::file_size(active_segment);
    }
}

VLogWriter::~VLogWriter() {
    flush(true);
    if (log_file.is_open()) log_file.close();
}

void VLogWriter::flush(bool force) {
    if (buffer_pos > 0 && log_file.is_open() && (is_dirty || force)) {
        log_file.write(buffer, buffer_pos);
        log_file.flush();
        current_size += buffer_pos;
        buffer_pos = 0;
        is_dirty = false;
    }
}

void VLogWriter::rotate() {
    flush(true); 
    if (log_file.is_open()) log_file.close();
    
    current_file_id++;
    std::string new_segment = base_path + "." + std::to_string(current_file_id);
    
    log_file.open(new_segment, std::ios::out | std::ios::binary | std::ios::trunc);
    current_size = 0;
    buffer_pos = 0;
    is_dirty = false;
}

ValuePointer VLogWriter::append_entry(const std::string& key, const std::string& value, ValueType type) {
    uint32_t k_size = static_cast<uint32_t>(key.size());
    uint32_t v_size = static_cast<uint32_t>(value.size());
    uint8_t type_tag = static_cast<uint8_t>(type);
    size_t total_entry_size = 4 + 4 + k_size + 4 + 1 + v_size; 

    if (current_size + buffer_pos + total_entry_size > MAX_LOG_SIZE) {
        rotate();
    }

    if (total_entry_size > BUFFER_SIZE) {
        flush(true); 
        
        std::string temp_buf;
        temp_buf.reserve(total_entry_size - 4);
        temp_buf.append(reinterpret_cast<const char*>(&k_size), 4);
        temp_buf.append(key);
        temp_buf.append(reinterpret_cast<const char*>(&v_size), 4);
        temp_buf.append(reinterpret_cast<const char*>(&type_tag), 1);
        temp_buf.append(value);

        uint32_t crc = calculate_crc32(temp_buf.data(), temp_buf.size());
        uint64_t start_pos = current_size;

        log_file.write(reinterpret_cast<const char*>(&crc), 4);
        log_file.write(temp_buf.data(), temp_buf.size());
        current_size += total_entry_size;
        is_dirty = false;
        
        return {current_file_id, v_size, start_pos + 12 + k_size};
    }

    if (buffer_pos + total_entry_size > BUFFER_SIZE) {
        flush(true);
    }

    is_dirty = true;
    uint64_t start_pos_virtual = current_size + buffer_pos;
    
    std::string body;
    body.append(reinterpret_cast<const char*>(&k_size), 4);
    body.append(key);
    body.append(reinterpret_cast<const char*>(&v_size), 4);
    body.append(reinterpret_cast<const char*>(&type_tag), 1);
    body.append(value);

    uint32_t crc = calculate_crc32(body.data(), body.size());

    std::memcpy(buffer + buffer_pos, &crc, 4);
    std::memcpy(buffer + buffer_pos + 4, body.data(), body.size());
    buffer_pos += total_entry_size;

    return {current_file_id, v_size, start_pos_virtual + 12 + k_size};
}

VLogReader::VLogReader(const std::string& filepath) : base_path(filepath) {}

VLogReader::~VLogReader() {
    close_all();
}

std::ifstream* VLogReader::get_handle(uint32_t file_id) {
    auto it = handle_cache.find(file_id);
    if (it != handle_cache.end()) return it->second;

    std::string target = base_path + "." + std::to_string(file_id);
    std::ifstream* in = new std::ifstream(target, std::ios::in | std::ios::binary);
    
    if (!in->is_open()) {
        delete in;
        return nullptr;
    }

    handle_cache[file_id] = in;
    return in;
}

void VLogReader::invalidate(uint32_t file_id) {
    auto it = handle_cache.find(file_id);
    if (it != handle_cache.end()) {
        if (it->second->is_open()) it->second->close();
        delete it->second;
        handle_cache.erase(it);
    }

    auto list_it = lru_list.begin();
    while (list_it != lru_list.end()) {
        if (list_it->ptr.file_id == file_id) {
            block_cache.erase(list_it->ptr);
            list_it = lru_list.erase(list_it);
        } else {
            ++list_it;
        }
    }
}

void VLogReader::close_all() {
    for (auto& pair : handle_cache) {
        if (pair.second->is_open()) pair.second->close();
        delete pair.second;
    }
    handle_cache.clear();
    lru_list.clear();
    block_cache.clear();
}

VortexData VLogReader::read_value(ValuePointer ptr) {
    auto cache_it = block_cache.find(ptr);
    if (cache_it != block_cache.end()) {
        lru_list.splice(lru_list.begin(), lru_list, cache_it->second);
        return cache_it->second->data;
    }

    std::ifstream* in = get_handle(ptr.file_id);
    if (!in) return {ValueType::TOMBSTONE, ""};

    in->clear();
    in->seekg(ptr.offset);
    
    uint8_t tag; 
    in->read(reinterpret_cast<char*>(&tag), 1);
    
    std::string val(ptr.value_size, '\0');
    in->read(&val[0], ptr.value_size);
    
    VortexData result = {static_cast<ValueType>(tag), val};

    if (block_cache.size() >= MAX_CACHE_SIZE) {
        ValuePointer lru_ptr = lru_list.back().ptr;
        block_cache.erase(lru_ptr);
        lru_list.pop_back();
    }

    lru_list.push_front({ptr, result});
    block_cache[ptr] = lru_list.begin();

    return result;
}

bool VLogReader::read_next_entry(uint32_t file_id, std::string& key, VortexData& data, ValuePointer& ptr, uint64_t& current_pos) {
    std::ifstream* in = get_handle(file_id);
    if (!in) return false;

    in->clear();
    in->seekg(current_pos);

    uint32_t expected_crc;
    if (!in->read(reinterpret_cast<char*>(&expected_crc), 4)) return false;

    uint32_t k_size; in->read(reinterpret_cast<char*>(&k_size), 4);
    key.resize(k_size); in->read(&key[0], k_size);
    uint32_t v_size; in->read(reinterpret_cast<char*>(&v_size), 4);

    ptr.file_id = file_id;
    ptr.value_size = v_size;
    ptr.offset = in->tellg();

    uint8_t tag; in->read(reinterpret_cast<char*>(&tag), 1);
    data.type = static_cast<ValueType>(tag);
    data.value.resize(v_size); in->read(&data.value[0], v_size);

    current_pos = in->tellg();

    if (block_cache.size() < MAX_CACHE_SIZE && block_cache.find(ptr) == block_cache.end()) {
        lru_list.push_front({ptr, data});
        block_cache[ptr] = lru_list.begin();
    }

    return true;
}