#ifndef VORTEX_SSTABLE_H
#define VORTEX_SSTABLE_H

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include "vlog.h"

struct SSTEntry {
    std::string key;
    ValuePointer ptr;
};

class SSTable;

class SSTableIterator {
private:
    const SSTable& table;
    std::ifstream file;
    std::string cur_key;
    ValuePointer cur_ptr;
    bool is_valid;
    uint32_t total_keys;
    uint32_t cur_idx;

    void read_at_current();

public:
    SSTableIterator(const SSTable& sst);
    void seek(const std::string& target);
    void next();
    bool valid() const { return is_valid; }
    std::string key() const { return cur_key; }
    ValuePointer value_ptr() const { return cur_ptr; }
};

class SSTable {
private:
    std::string filename;
    std::vector<std::pair<std::string, uint64_t>> sparse_index;
    
    friend class SSTableIterator;

public:
    SSTable(const std::string& fname);
    static std::string write_new(const std::string& fname, const std::map<std::string, ValuePointer>& data);
    
    bool find(const std::string& key, ValuePointer& result);
    void load_sparse_index();
    std::string get_filename() const { return filename; }

    SSTableIterator* create_iterator() const {
        return new SSTableIterator(*this);
    }
};

#endif