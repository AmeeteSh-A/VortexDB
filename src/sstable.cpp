#include "sstable.h"
#include <iostream>
#include <algorithm>

SSTable::SSTable(const std::string& fname) : filename(fname) {
    load_sparse_index();
}

std::string SSTable::write_new(const std::string& fname, const std::map<std::string, ValuePointer>& data) {
    std::ofstream out(fname, std::ios::binary);
    uint32_t num_keys = static_cast<uint32_t>(data.size());
    out.write(reinterpret_cast<const char*>(&num_keys), sizeof(uint32_t));

    for (const auto& [key, ptr] : data) {
        uint32_t k_size = static_cast<uint32_t>(key.size());
        out.write(reinterpret_cast<const char*>(&k_size), sizeof(uint32_t));
        out.write(key.data(), k_size);
        out.write(reinterpret_cast<const char*>(&ptr), sizeof(ValuePointer));
    }
    out.close();
    return fname;
}

void SSTable::load_sparse_index() {
    std::ifstream in(filename, std::ios::binary);
    if (!in) return;

    uint32_t num_keys;
    in.read(reinterpret_cast<char*>(&num_keys), sizeof(uint32_t));

    for (uint32_t i = 0; i < num_keys; i++) {
        uint64_t offset = in.tellg();
        uint32_t k_size;
        in.read(reinterpret_cast<char*>(&k_size), sizeof(uint32_t));
        std::string key(k_size, '\0');
        in.read(&key[0], k_size);
        
        if (i % 100 == 0) {
            sparse_index.push_back({key, offset});
        }
        
        in.seekg(sizeof(ValuePointer), std::ios::cur);
    }
}

bool SSTable::find(const std::string& key, ValuePointer& result) {
    SSTableIterator it(*this);
    it.seek(key);
    if (it.valid() && it.key() == key) {
        result = it.value_ptr();
        return true;
    }
    return false;
}


SSTableIterator::SSTableIterator(const SSTable& sst) 
    : table(sst), is_valid(false), total_keys(0), cur_idx(0) {
    file.open(table.filename, std::ios::binary);
    if (file.is_open()) {
        file.read(reinterpret_cast<char*>(&total_keys), sizeof(uint32_t));
    }
}

void SSTableIterator::read_at_current() {
    if (cur_idx >= total_keys) {
        is_valid = false;
        return;
    }

    uint32_t k_size;
    if (!file.read(reinterpret_cast<char*>(&k_size), sizeof(uint32_t))) {
        is_valid = false;
        return;
    }

    cur_key.resize(k_size);
    file.read(&cur_key[0], k_size);
    file.read(reinterpret_cast<char*>(&cur_ptr), sizeof(ValuePointer));
    
    is_valid = true;
}

void SSTableIterator::seek(const std::string& target) {
    if (table.sparse_index.empty() || total_keys == 0) {
        is_valid = false;
        return;
    }

    uint64_t offset = table.sparse_index[0].second;
    cur_idx = 0;

    for (size_t i = 0; i < table.sparse_index.size(); ++i) {
        if (table.sparse_index[i].first <= target) {
            offset = table.sparse_index[i].second;
            cur_idx = i * 100; // Index is stored har 100th key pe
        } else {
            break;
        }
    }

    file.seekg(offset, std::ios::beg);
    
    while (cur_idx < total_keys) {
        read_at_current();
        if (!is_valid || cur_key >= target) break;
        cur_idx++;
    }
}

void SSTableIterator::next() {
    cur_idx++;
    read_at_current();
}