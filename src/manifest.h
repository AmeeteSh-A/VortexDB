#ifndef VORTEX_MANIFEST_H
#define VORTEX_MANIFEST_H

#include <string>
#include <vector>
#include <fstream>
#include <cstdint>

class Manifest {
private:
    std::string filepath;
    std::vector<std::string> sst_filenames;

public:
    Manifest(const std::string& path);
    
    void register_sstable(const std::string& fname);
    void clear();
    
    void save();
    void load();
    
    const std::vector<std::string>& get_sstables() const {
        return sst_filenames;
    }
};

#endif