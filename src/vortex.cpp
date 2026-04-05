#include "vortex.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <set>
#include <mutex>
#include <algorithm>
#include <queue>

namespace fs = std::filesystem;

namespace {
    ValueType infer_type(const std::string& value) {
        if (value == "true" || value == "false") return ValueType::BOOL;
        if (!value.empty() && value.front() == '{' && value.back() == '}') return ValueType::JSON;
        
        bool is_number = true;
        bool is_double = false;
        if (value.empty()) return ValueType::STRING;
        
        for (size_t i = 0; i < value.length(); ++i) {
            if (i == 0 && value[i] == '-') continue;
            if (value[i] == '.') {
                if (is_double) { is_number = false; break; }
                is_double = true;
                continue;
            }
            if (!std::isdigit(value[i])) {
                is_number = false;
                break;
            }
        }
        if (is_number && value != "-" && value != "-.") {
            return is_double ? ValueType::DOUBLE : ValueType::INT;
        }
        return ValueType::STRING;
    }

    std::vector<uint32_t> discover_vlog_segments(const std::string& db_name) {
        std::vector<uint32_t> ids;
        std::string prefix = db_name + ".vlog.";
        try {
            for (const auto& entry : fs::directory_iterator(".")) {
                std::string path = entry.path().filename().string();
                if (path.find(prefix) == 0) {
                    try {
                        uint32_t id = std::stoul(path.substr(prefix.length()));
                        ids.push_back(id);
                    } catch (...) {}
                }
            }
        } catch (...) {}
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    class MergingIterator {
    private:
        struct IterNode {
            InternalIterator* iter;
            size_t priority;

            bool operator>(const IterNode& other) const {
                if (iter->key() != other.iter->key()) {
                    return iter->key() > other.iter->key();
                }
                return priority > other.priority;
            }
        };

        std::priority_queue<IterNode, std::vector<IterNode>, std::greater<IterNode>> pq;
        std::vector<InternalIterator*> all_iters;

    public:
        MergingIterator(std::vector<InternalIterator*> iters) : all_iters(iters) {}

        ~MergingIterator() {
            for (auto it : all_iters) delete it;
        }

        bool valid() const { return !pq.empty(); }

        void seek(const std::string& target) {
            while(!pq.empty()) pq.pop();
            for (size_t i = 0; i < all_iters.size(); ++i) {
                all_iters[i]->seek(target);
                if (all_iters[i]->valid()) {
                    pq.push({all_iters[i], i});
                }
            }
        }

        void next() {
            if (pq.empty()) return;
            std::string current_key = pq.top().iter->key();
            while (!pq.empty() && pq.top().iter->key() == current_key) {
                IterNode top = pq.top();
                pq.pop();
                top.iter->next();
                if (top.iter->valid()) pq.push(top);
            }
        }

        std::string key() const { return pq.top().iter->key(); }
        ValuePointer value() const { return pq.top().iter->value(); }
    };
}

VortexDB::VortexDB(const std::string& name) : db_name(name) {
    std::vector<uint32_t> segments = discover_vlog_segments(db_name);
    bool vlog_exists = !segments.empty();
    uint32_t next_id = segments.empty() ? 0 : segments.back() + 1;

    writer = new VLogWriter(db_name + ".vlog", next_id);
    reader = new VLogReader(db_name + ".vlog");
    manifest = new Manifest(db_name + ".manifest");

    load_index();
    
    if (vlog_exists && memtable.empty()) {
        recover_from_vlog();
    }

    manifest->load();
    for (const auto& fname : manifest->get_sstables()) {
        if (fs::exists(fname)) {
            sstables.push_back(new SSTable(fname));
        }
    }

    for (uint32_t id : segments) {
        std::string k; VortexData d; ValuePointer p; uint64_t pos = 0;
        while (reader->read_next_entry(id, k, d, p, pos)) {
            if (d.type != ValueType::TOMBSTONE) filter.insert(k);
            else filter.remove(k);
        }
    }
}

VortexDB::~VortexDB() {
    for (VortexDB* child : active_children) delete child;
    active_children.clear();
    save_index();
    if (writer) delete writer;
    delete reader;
    for (SSTable* sst : sstables) delete sst;
    delete manifest;
}

void VortexDB::flush_memtable() {
    if (writer) writer->flush(true); 
    if (memtable.empty()) return;

    std::string sst_name = db_name + "_" + std::to_string(sstables.size()) + "_" + std::to_string(time(0)) + ".sst";
    SSTable::write_new(sst_name, memtable);
    sstables.push_back(new SSTable(sst_name));
    manifest->register_sstable(sst_name);
    memtable.clear();
    save_index();
}

void VortexDB::put(const std::string& key, const std::string& value) {
    size_t slash_pos = key.find('/');
    if (slash_pos != std::string::npos) {
        std::string dir = key.substr(0, slash_pos);
        std::string rest = key.substr(slash_pos + 1);
        if (dir.empty()) { put(rest, value); return; }
        VortexDB* child = nullptr;
        {
            std::unique_lock<std::shared_mutex> lock(db_rw_lock);
            child = get_vortex_direct(dir);
        }
        if (!child) child = spawn_vortex(dir);
        child->put(rest, value);
        return;
    }

    std::unique_lock<std::shared_mutex> lock(db_rw_lock);
    if (memtable.size() >= MEMTABLE_LIMIT) flush_memtable();

    auto it = memtable.find(key);
    if (it != memtable.end()) {
        if (writer) writer->flush();
        VortexData current_data = reader->read_value(it->second);
        if (current_data.type == ValueType::SUB_VORTEX) return;
    }

    ValueType inferred = infer_type(value);
    ValuePointer ptr = {0, 0, 0};
    if (writer) ptr = writer->append_entry(key, value, inferred);
    memtable[key] = ptr;
    filter.insert(key);
}

void VortexDB::del(const std::string& key) {
    size_t slash_pos = key.find('/');
    if (slash_pos != std::string::npos) {
        std::string dir = key.substr(0, slash_pos);
        std::string rest = key.substr(slash_pos + 1);
        if (dir.empty()) { del(rest); return; }
        VortexDB* child = nullptr;
        {
            std::shared_lock<std::shared_mutex> lock(db_rw_lock);
            child = get_vortex_direct(dir);
        }
        if (child) child->del(rest);
        return;
    }

    std::unique_lock<std::shared_mutex> lock(db_rw_lock);
    if (memtable.size() >= MEMTABLE_LIMIT) flush_memtable();

    auto it = memtable.find(key);
    if (it != memtable.end()) {
        if (writer) writer->flush();
        VortexData data = reader->read_value(it->second);
        if (data.type == ValueType::SUB_VORTEX) {
            VortexDB* child = get_vortex_direct(key);
            if (child) child->destroy_permanently();
        }
    }

    ValuePointer ptr = {0, 0, 0};
    if (writer) ptr = writer->append_entry(key, "", ValueType::TOMBSTONE);
    memtable[key] = ptr;
    filter.remove(key);
}

std::string VortexDB::get(const std::string& key) {
    size_t slash_pos = key.find('/');
    if (slash_pos != std::string::npos) {
        std::string dir = key.substr(0, slash_pos);
        std::string rest = key.substr(slash_pos + 1);
        if (dir.empty()) return get(rest);
        VortexDB* child = nullptr;
        {
            std::shared_lock<std::shared_mutex> lock(db_rw_lock);
            child = get_vortex_direct(dir);
        }
        if (!child) return "ERROR: Key not found.";
        return child->get(rest);
    }

    std::shared_lock<std::shared_mutex> lock(db_rw_lock);
    if (!filter.contains(key)) return "ERROR: Key not found.";
    
    auto it = memtable.find(key);
    if (it != memtable.end()) {
        if (writer) writer->flush();
        reader->invalidate(it->second.file_id); 
        VortexData data = reader->read_value(it->second);
        if (data.type == ValueType::TOMBSTONE) return "ERROR: Key not found.";
        return data.value;
    }

    for (auto i = sstables.rbegin(); i != sstables.rend(); ++i) {
        ValuePointer ptr;
        if ((*i)->find(key, ptr)) {
            VortexData data = reader->read_value(ptr);
            if (data.type == ValueType::TOMBSTONE) return "ERROR: Key not found.";
            return data.value;
        }
    }
    return "ERROR: Key not found.";
}

ValueType VortexDB::get_type(const std::string& key) {
    size_t slash_pos = key.find('/');
    if (slash_pos != std::string::npos) {
        std::string dir = key.substr(0, slash_pos);
        std::string rest = key.substr(slash_pos + 1);
        if (dir.empty()) return get_type(rest);
        VortexDB* child = nullptr;
        {
            std::shared_lock<std::shared_mutex> lock(db_rw_lock);
            child = get_vortex_direct(dir);
        }
        if (!child) return ValueType::TOMBSTONE;
        return child->get_type(rest);
    }

    std::shared_lock<std::shared_mutex> lock(db_rw_lock);
    if (!filter.contains(key)) return ValueType::TOMBSTONE;
    auto it = memtable.find(key);
    if (it != memtable.end()) {
        if (writer) writer->flush();
        reader->invalidate(it->second.file_id);
        return reader->read_value(it->second).type;
    }
    for (auto i = sstables.rbegin(); i != sstables.rend(); ++i) {
        ValuePointer ptr;
        if ((*i)->find(key, ptr)) return reader->read_value(ptr).type;
    }
    return ValueType::TOMBSTONE;
}

VortexDB* VortexDB::spawn_vortex(const std::string& name) {
    std::unique_lock<std::shared_mutex> lock(db_rw_lock);
    if (memtable.size() >= MEMTABLE_LIMIT) flush_memtable();
    std::string child_db_name = db_name + "_" + name;
    ValuePointer ptr = {0, 0, 0};
    if (writer) ptr = writer->append_entry(name, child_db_name, ValueType::SUB_VORTEX);
    memtable[name] = ptr;
    filter.insert(name);
    VortexDB* child = new VortexDB(child_db_name);
    active_children.push_back(child);
    return child;
}

VortexDB* VortexDB::get_vortex_direct(const std::string& name) {
    ValuePointer ptr = {0, 0};
    bool found = false;
    auto it = memtable.find(name);
    if (it != memtable.end()) { ptr = it->second; found = true; }
    else {
        for (auto i = sstables.rbegin(); i != sstables.rend(); ++i) {
            if ((*i)->find(name, ptr)) { found = true; break; }
        }
    }

    if (!found) return nullptr;
    if (writer) writer->flush();
    VortexData data = reader->read_value(ptr);
    if (data.type != ValueType::SUB_VORTEX) return nullptr; 

    std::string child_db_name = data.value;
    for (VortexDB* child : active_children) if (child->db_name == child_db_name) return child;
    VortexDB* child = new VortexDB(child_db_name);
    active_children.push_back(child);
    return child;
}

VortexDB* VortexDB::resolve_path(const std::string& path) {
    std::shared_lock<std::shared_mutex> lock(db_rw_lock);
    std::stringstream ss(path);
    std::string part;
    VortexDB* current = this;
    while (std::getline(ss, part, '/')) {
        if (part.empty()) continue;
        if (part == current->db_name || (current->db_name == "root" && part == "root")) continue;
        current = current->get_vortex_direct(part);
        if (!current) return nullptr;
    }
    return current;
}

void VortexDB::save_index() {
    if (writer) writer->flush(true);
    std::ofstream idx_file(db_name + ".idx", std::ios::binary | std::ios::trunc);
    for (const auto& [key, ptr] : memtable) {
        size_t key_len = key.size();
        idx_file.write(reinterpret_cast<const char*>(&key_len), sizeof(size_t));
        idx_file.write(key.data(), key_len);
        idx_file.write(reinterpret_cast<const char*>(&ptr), sizeof(ValuePointer));
    }
    idx_file.close();
    for (VortexDB* child : active_children) child->save_index();
}

void VortexDB::load_index() {
    std::ifstream idx_file(db_name + ".idx", std::ios::binary);
    if (!idx_file.is_open()) return;
    memtable.clear();
    while (idx_file.peek() != EOF) {
        size_t key_len; 
        if (!idx_file.read(reinterpret_cast<char*>(&key_len), sizeof(size_t))) break;
        std::string key(key_len, '\0'); 
        idx_file.read(&key[0], key_len);
        ValuePointer ptr; 
        idx_file.read(reinterpret_cast<char*>(&ptr), sizeof(ValuePointer));
        memtable[key] = ptr;
    }
}

std::vector<std::pair<std::string, std::string>> VortexDB::get_range_internal(const std::string& start_key, const std::string& end_key) {
    std::vector<InternalIterator*> iters;
    
    iters.push_back(new MemtableIterator(memtable));
    for (int i = sstables.size() - 1; i >= 0; --i) {
        iters.push_back(new SSTableIteratorWrapper(sstables[i]->create_iterator()));
    }

    MergingIterator merge_it(iters);
    merge_it.seek(start_key);

    std::vector<std::pair<std::string, std::string>> results;
    while (merge_it.valid()) {
        if (!end_key.empty() && merge_it.key() > end_key) break;
        
        VortexData data = reader->read_value(merge_it.value());
        if (data.type != ValueType::TOMBSTONE) {
            results.push_back({merge_it.key(), data.value});
        }
        merge_it.next();
    }
    return results;
}

std::vector<std::string> VortexDB::get_all_keys_internal() {
    auto res = get_range_internal("", "");
    std::vector<std::string> keys;
    for (auto const& p : res) keys.push_back(p.first);
    return keys;
}

VortexStats VortexDB::get_stats() {
    std::unique_lock<std::shared_mutex> lock(db_rw_lock); 
    
    if (writer) {
        writer->flush(true); 
        delete writer; 
        writer = nullptr;
    }

    reader->close_all();

    VortexStats stats;
    auto range = get_range_internal("", "");
    stats.live_keys = range.size();
    
    size_t total_count = 0;
    std::vector<uint32_t> segments = discover_vlog_segments(db_name);
    for (uint32_t id : segments) {
        std::string k; VortexData data; ValuePointer p; uint64_t pos = 0;
        while (reader->read_next_entry(id, k, data, p, pos)) total_count++;
        reader->invalidate(id); 
    }
    
    stats.total_entries = total_count;
    stats.waste_ratio = (total_count > 0) ? (1.0 - ((double)stats.live_keys / total_count)) : 0.0;
    
    segments = discover_vlog_segments(db_name);
    uint32_t active_id = segments.empty() ? 0 : segments.back();
    writer = new VLogWriter(db_name + ".vlog", active_id);
    
    return stats;
}

void VortexDB::recover_from_vlog() {
    std::vector<uint32_t> segments = discover_vlog_segments(db_name);
    memtable.clear();
    for (uint32_t id : segments) {
        std::string k; VortexData d; ValuePointer p; uint64_t pos = 0;
        while (reader->read_next_entry(id, k, d, p, pos)) {
            if (d.type == ValueType::TOMBSTONE) {
                memtable.erase(k);
            } else {
                memtable[k] = p;
            }
            if (memtable.size() >= MEMTABLE_LIMIT) flush_memtable();
        }
    }
    save_index();
}

std::vector<std::pair<std::string, std::string>> VortexDB::get_range(const std::string& start, const std::string& end) {
    std::unique_lock<std::shared_mutex> lock(db_rw_lock);
    if (writer) {
        writer->flush(true);
        delete writer;
        writer = nullptr;
    }
    reader->close_all();
    
    auto res = get_range_internal(start, end);
    
    auto segments = discover_vlog_segments(db_name);
    uint32_t active_id = segments.empty() ? 0 : segments.back();
    writer = new VLogWriter(db_name + ".vlog", active_id);
    return res;
}

std::vector<std::string> VortexDB::get_all_keys() {
    std::unique_lock<std::shared_mutex> lock(db_rw_lock);
    if (writer) {
        writer->flush(true);
        delete writer;
        writer = nullptr;
    }
    reader->close_all();
    
    auto res = get_all_keys_internal();
    
    auto segments = discover_vlog_segments(db_name);
    uint32_t active_id = segments.empty() ? 0 : segments.back();
    writer = new VLogWriter(db_name + ".vlog", active_id);
    return res;
}

void VortexDB::compact() {
    std::unique_lock<std::shared_mutex> lock(db_rw_lock);
    std::map<std::string, ValuePointer> merged_index;
    for (SSTable* sst : sstables) {
        std::ifstream in(sst->get_filename(), std::ios::binary);
        if (!in) continue;
        uint32_t num_keys; in.read(reinterpret_cast<char*>(&num_keys), 4);
        for (uint32_t i = 0; i < num_keys; i++) {
            uint32_t k_size; in.read(reinterpret_cast<char*>(&k_size), 4);
            std::string k(k_size, '\0'); in.read(&k[0], k_size);
            ValuePointer p; in.read(reinterpret_cast<char*>(&p), sizeof(ValuePointer));
            merged_index[k] = p;
        }
    }
    for (const auto& [k, p] : memtable) merged_index[k] = p;

    std::string temp_vlog = db_name + ".temp_vlog.0";
    VLogWriter* temp_writer = new VLogWriter(db_name + ".temp_vlog", 0);
    std::vector<std::string> new_sst_names;
    std::map<std::string, ValuePointer> current_chunk;
    filter = CuckooFilter();

    reader->close_all();

    for (auto const& [k, p] : merged_index) {
        VortexData data = reader->read_value(p);
        if (data.type == ValueType::TOMBSTONE) continue;
        ValuePointer new_ptr = temp_writer->append_entry(k, data.value, data.type);
        current_chunk[k] = new_ptr;
        filter.insert(k);
        if (current_chunk.size() >= MEMTABLE_LIMIT) {
            std::string sst_name = db_name + "_compact_" + std::to_string(time(0)) + ".sst";
            SSTable::write_new(sst_name, current_chunk);
            new_sst_names.push_back(sst_name);
            current_chunk.clear();
        }
    }

    memtable = current_chunk;
    reader->close_all();
    if (writer) delete writer; 
    delete reader; 
    delete temp_writer;
    
    std::vector<uint32_t> segments = discover_vlog_segments(db_name);
    for (uint32_t id : segments) fs::remove(db_name + ".vlog." + std::to_string(id));
    fs::rename(temp_vlog, db_name + ".vlog.0");

    for (SSTable* sst : sstables) { 
        if (fs::exists(sst->get_filename())) fs::remove(sst->get_filename()); 
        delete sst; 
    }
    sstables.clear();
    manifest->clear();
    for (const auto& fname : new_sst_names) {
        sstables.push_back(new SSTable(fname));
        manifest->register_sstable(fname);
    }

    writer = new VLogWriter(db_name + ".vlog", 1);
    reader = new VLogReader(db_name + ".vlog");
    save_index();
}

void VortexDB::compact_all() {
    compact();
    for (VortexDB* child : active_children) child->compact_all();
}

void VortexDB::destroy_permanently() {
    std::unique_lock<std::shared_mutex> lock(db_rw_lock);
    for (VortexDB* child : active_children) child->destroy_permanently();
    
    reader->close_all();
    if (writer) delete writer; 
    delete reader;
    
    std::vector<uint32_t> segments = discover_vlog_segments(db_name);
    for (uint32_t id : segments) fs::remove(db_name + ".vlog." + std::to_string(id));
    fs::remove(db_name + ".idx"); fs::remove(db_name + ".manifest");
    for (SSTable* sst : sstables) { 
        if (fs::exists(sst->get_filename())) fs::remove(sst->get_filename()); 
        delete sst; 
    }
    sstables.clear();
}

std::vector<std::pair<std::string, std::string>> VortexDB::get_prefix(const std::string& prefix) {
    size_t slash_pos = prefix.find_last_of('/');
    if (slash_pos != std::string::npos) {
        std::string dir = prefix.substr(0, slash_pos);
        std::string rest = prefix.substr(slash_pos + 1);
        VortexDB* child = resolve_path(dir);
        if (!child) return {};
        auto child_res = child->get_prefix(rest);
        std::vector<std::pair<std::string, std::string>> res;
        for (auto& p : child_res) res.push_back({dir + "/" + p.first, p.second});
        return res;
    }
    
    std::unique_lock<std::shared_mutex> lock(db_rw_lock);
    if (writer) {
        writer->flush(true);
        delete writer;
        writer = nullptr;
    }
    reader->close_all();
    
    std::string end_key = prefix;
    if (!end_key.empty()) end_key.back()++;
    auto res = get_range_internal(prefix, end_key);
    
    auto segments = discover_vlog_segments(db_name);
    uint32_t active_id = segments.empty() ? 0 : segments.back();
    writer = new VLogWriter(db_name + ".vlog", active_id);
    
    return res;
}

void VortexDB::save_state(const std::string& state_name, const std::string& target_path) {
    VortexDB* target = this;
    if (!target_path.empty()) { target = resolve_path(target_path); if (!target) return; }
    target->save_index();
    std::ofstream meta(state_name + ".meta");
    meta << (target_path.empty() ? "." : target_path);
    meta.close();
    std::string target_prefix = target->db_name;
    for (const auto& entry : fs::directory_iterator(".")) {
        if (!entry.is_regular_file()) continue;
        std::string fname = entry.path().filename().string();
        if (fname.find(target_prefix) == 0 && fname.find(state_name) == std::string::npos && fname.find(".meta") == std::string::npos) {
            std::string dest = state_name + "_" + fname;
            if (fs::exists(dest)) fs::remove(dest);
            fs::copy_file(entry.path(), dest);
        }
    }
}

void VortexDB::load_state(const std::string& state_name) {
    std::ifstream meta_file(state_name + ".meta");
    if (!meta_file.is_open()) return;
    std::string target_path; std::getline(meta_file, target_path); meta_file.close();
    VortexDB* target = this;
    if (target_path != ".") target = resolve_path(target_path);
    if (!target) return;
    std::string t_db_name = target->db_name;
    {
        std::unique_lock<std::shared_mutex> lock(target->db_rw_lock);
        for (VortexDB* child : target->active_children) delete child;
        target->active_children.clear();
        
        target->reader->close_all();
        if (target->writer) delete target->writer;
        delete target->reader;
        
        for (SSTable* sst : target->sstables) delete sst;
        target->sstables.clear();
        delete target->manifest;
        for (const auto& entry : fs::directory_iterator(".")) {
            if (!entry.is_regular_file()) continue;
            std::string fname = entry.path().filename().string();
            if (fname.find(t_db_name) == 0 && fname.find(state_name) == std::string::npos && fname.find(".meta") == std::string::npos) fs::remove(entry.path());
        }
        std::string prefix = state_name + "_";
        for (const auto& entry : fs::directory_iterator(".")) {
            if (!entry.is_regular_file()) continue;
            std::string fname = entry.path().filename().string();
            if (fname.find(prefix) == 0) {
                std::string original = fname.substr(prefix.length());
                if (original.find(t_db_name) == 0) {
                    if (fs::exists(original)) fs::remove(original);
                    fs::copy_file(entry.path(), original, fs::copy_options::overwrite_existing);
                }
            }
        }
        target->memtable.clear(); target->load_index();
        target->manifest = new Manifest(t_db_name + ".manifest"); target->manifest->load();
        for (const auto& f : target->manifest->get_sstables()) target->sstables.push_back(new SSTable(f));
        std::vector<uint32_t> segments = discover_vlog_segments(t_db_name);
        uint32_t next_id = segments.empty() ? 0 : segments.back() + 1;
        target->writer = new VLogWriter(t_db_name + ".vlog", next_id);
        target->reader = new VLogReader(t_db_name + ".vlog");
        target->filter = CuckooFilter();
        for (uint32_t id : segments) {
            std::string k; VortexData d; ValuePointer p; uint64_t pos = 0;
            while (target->reader->read_next_entry(id, k, d, p, pos)) {
                if (d.type != ValueType::TOMBSTONE) target->filter.insert(k);
                else target->filter.remove(k);
            }
        }
    }
}

std::string VortexDB::get_db_name() { return db_name; }

void VortexDB::convert_to_subdb(const std::string& key) {
    size_t slash_pos = key.find('/');
    if (slash_pos != std::string::npos) {
        std::string dir = key.substr(0, slash_pos);
        std::string rest = key.substr(slash_pos + 1);
        if (dir.empty()) { convert_to_subdb(rest); return; }
        VortexDB* child = nullptr;
        {
            std::shared_lock<std::shared_mutex> lock(db_rw_lock);
            child = get_vortex_direct(dir);
        }
        if (child) child->convert_to_subdb(rest);
        return;
    }
    if (get_type(key) == ValueType::SUB_VORTEX) return;
    del(key);
    spawn_vortex(key);
}