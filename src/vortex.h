#ifndef VORTEX_DB_H
#define VORTEX_DB_H

#include <string>
#include <map>
#include <vector>
#include <filesystem>
#include <utility>
#include <shared_mutex>
#include <queue>
#include "vlog.h"
#include "cuckoo.h"
#include "sstable.h"
#include "manifest.h"

struct VortexStats {
    size_t live_keys;
    size_t total_entries;
    double waste_ratio;
};

class InternalIterator {
public:
    virtual ~InternalIterator() = default;
    virtual bool valid() const = 0;
    virtual void seek(const std::string& target) = 0;
    virtual void next() = 0;
    virtual std::string key() const = 0;
    virtual ValuePointer value() const = 0;
};

class MemtableIterator : public InternalIterator {
private:
    const std::map<std::string, ValuePointer>& data;
    std::map<std::string, ValuePointer>::const_iterator it;
public:
    MemtableIterator(const std::map<std::string, ValuePointer>& m) : data(m), it(m.begin()) {}
    bool valid() const override { return it != data.end(); }
    void seek(const std::string& target) override { it = data.lower_bound(target); }
    void next() override { ++it; }
    std::string key() const override { return it->first; }
    ValuePointer value() const override { return it->second; }
};

class SSTableIteratorWrapper : public InternalIterator {
private:
    SSTableIterator* inner;
public:
    SSTableIteratorWrapper(SSTableIterator* it) : inner(it) {}
    ~SSTableIteratorWrapper() { delete inner; }
    bool valid() const override { return inner->valid(); }
    void seek(const std::string& target) override { inner->seek(target); }
    void next() override { inner->next(); }
    std::string key() const override { return inner->key(); }
    ValuePointer value() const override { return inner->value_ptr(); }
};

class VortexDB {
private:
    std::map<std::string, ValuePointer> memtable;
    std::vector<VortexDB*> active_children;
    VLogWriter* writer;
    VLogReader* reader;
    std::string db_name;
    CuckooFilter filter;

    std::vector<SSTable*> sstables;
    Manifest* manifest;
    const size_t MEMTABLE_LIMIT = 100000;

    void load_index();
    void recover_from_vlog();
    VortexDB* get_vortex_direct(const std::string& name);
    void flush_memtable();

    std::vector<std::string> get_all_keys_internal();
    std::vector<std::pair<std::string, std::string>> get_range_internal(const std::string& start_key, const std::string& end_key);

    mutable std::shared_mutex db_rw_lock;

public:
    VortexDB(const std::string& name);
    ~VortexDB();

    void put(const std::string& key, const std::string& value);
    void del(const std::string& key);
    std::string get(const std::string& key);
    ValueType get_type(const std::string& key);

    VortexDB* spawn_vortex(const std::string& name);
    VortexDB* resolve_path(const std::string& path);

    void save_index();
    void compact();
    void compact_all();
    void destroy_permanently();
    
    VortexStats get_stats();

    void save_state(const std::string& state_name, const std::string& target_path = "");
    void load_state(const std::string& state_name);

    std::vector<std::string> get_all_keys();
    std::string get_db_name();

    std::vector<std::pair<std::string, std::string>> get_prefix(const std::string& prefix);
    std::vector<std::pair<std::string, std::string>> get_range(const std::string& start_key, const std::string& end_key);
    void convert_to_subdb(const std::string& key);
};

#endif