#include "manifest.h"
#include <iostream>

Manifest::Manifest(const std::string& path) : filepath(path) {}

void Manifest::register_sstable(const std::string& fname) {
    sst_filenames.push_back(fname);
    save();
}

void Manifest::clear() {
    sst_filenames.clear();
    save();
}

void Manifest::save() {
    std::ofstream out(filepath, std::ios::binary | std::ios::trunc);
    if (!out) return;

    uint32_t count = static_cast<uint32_t>(sst_filenames.size());
    out.write(reinterpret_cast<const char*>(&count), sizeof(uint32_t));

    for (const auto& fname : sst_filenames) {
        uint32_t len = static_cast<uint32_t>(fname.size());
        out.write(reinterpret_cast<const char*>(&len), sizeof(uint32_t));
        out.write(fname.data(), len);
    }
}

void Manifest::load() {
    std::ifstream in(filepath, std::ios::binary);
    if (!in) return;

    sst_filenames.clear();
    uint32_t count;
    if (!in.read(reinterpret_cast<char*>(&count), sizeof(uint32_t))) return;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t len;
        in.read(reinterpret_cast<char*>(&len), sizeof(uint32_t));
        std::string fname(len, '\0');
        in.read(&fname[0], len);
        sst_filenames.push_back(fname);
    }
}