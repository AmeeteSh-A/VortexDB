#include "cuckoo.h"
#include <random>

CuckooFilter::CuckooFilter(size_t capacity) {
    num_buckets = capacity;
    num_items = 0;
    buckets.resize(num_buckets, std::vector<uint8_t>(BUCKET_SIZE, 0));
}

uint32_t CuckooFilter::hash_string(const std::string& str) const {
    uint32_t hash = 2166136261u;
    for (char c : str) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

uint32_t CuckooFilter::hash_int(uint32_t val) const {
    val = ((val >> 16) ^ val) * 0x45d9f3b;
    val = ((val >> 16) ^ val) * 0x45d9f3b;
    val = (val >> 16) ^ val;
    return val;
}

uint8_t CuckooFilter::create_fingerprint(uint32_t hash) const {
    uint8_t fp = hash % 255;
    fp += 1; 
    return fp;
}

size_t CuckooFilter::get_index(uint32_t hash) const {
    return hash % num_buckets;
}

size_t CuckooFilter::get_alt_index(size_t index, uint8_t fingerprint) const {
    uint32_t fp_hash = hash_int(fingerprint);
    return (index ^ fp_hash) % num_buckets;
}

bool CuckooFilter::insert(const std::string& item) {
    uint32_t hash = hash_string(item);
    uint8_t fp = create_fingerprint(hash);
    size_t i1 = get_index(hash);
    size_t i2 = get_alt_index(i1, fp);

    for (size_t i = 0; i < BUCKET_SIZE; i++) {
        if (buckets[i1][i] == 0) {
            buckets[i1][i] = fp;
            num_items++;
            return true;
        }
    }

    for (size_t i = 0; i < BUCKET_SIZE; i++) {
        if (buckets[i2][i] == 0) {
            buckets[i2][i] = fp;
            num_items++;
            return true;
        }
    }

    size_t current_index = (rand() % 2 == 0) ? i1 : i2;
    
    for (size_t n = 0; n < MAX_KICKS; n++) {
        size_t slot = rand() % BUCKET_SIZE;
        
        uint8_t kicked_fp = buckets[current_index][slot];
        buckets[current_index][slot] = fp;
        fp = kicked_fp;

        current_index = get_alt_index(current_index, fp);
        for (size_t i = 0; i < BUCKET_SIZE; i++) {
            if (buckets[current_index][i] == 0) {
                buckets[current_index][i] = fp;
                num_items++;
                return true;
            }
        }
    }

    
    return false; 
}

bool CuckooFilter::contains(const std::string& item) const {
    uint32_t hash = hash_string(item);
    uint8_t fp = create_fingerprint(hash);
    size_t i1 = get_index(hash);
    size_t i2 = get_alt_index(i1, fp);

    for (size_t i = 0; i < BUCKET_SIZE; i++) {
        if (buckets[i1][i] == fp || buckets[i2][i] == fp) {
            return true;
        }
    }
    return false;
}

bool CuckooFilter::remove(const std::string& item) {
    uint32_t hash = hash_string(item);
    uint8_t fp = create_fingerprint(hash);
    size_t i1 = get_index(hash);
    size_t i2 = get_alt_index(i1, fp);

    for (size_t i = 0; i < BUCKET_SIZE; i++) {
        if (buckets[i1][i] == fp) {
            buckets[i1][i] = 0; 
            num_items--;
            return true;
        }
        if (buckets[i2][i] == fp) {
            buckets[i2][i] = 0; 
            num_items--;
            return true;
        }
    }
    return false; 
}