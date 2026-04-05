# 📊 VortexDB: Performance Benchmarks & Engineering Deep Dive

This document provides a comprehensive breakdown of VortexDB's performance characteristics. As a systems exploration project, VortexDB was built to test a specific hypothesis: **Can combining a WiscKey-style value log with a recursive sub-database architecture outperform traditional LSM-trees?**

The answer is heavily dependent on the workload and memory configuration. Below is the unvarnished data—the good, the bad, and the architectural reasons behind every metric.

---

## 💻 Test Environment

To ensure replicability and consistent I/O constraints, all benchmarks were executed on the following local hardware:

* **CPU:** Intel Core i5-12450HX
* **RAM:** 12GB @ 4800 MT/s (Single Channel)
* **Storage:** 512GB NVMe SSD
* **OS:** Windows 11 Home (64-bit)
* **Competitor Versions:** SQLite v3.51.3 (Amalgamation), LevelDB v1.23
* **Testing Tool:** Custom C++ `chrono` benchmarking suite.

*(SQLite was run with `PRAGMA synchronous = OFF` and `journal_mode = MEMORY` to strip away ACID overhead and test its absolute maximum theoretical memory-throughput).*

---

## 🧪 Phase 1: The Small Payload Stress Test (~10 Bytes)

**Methodology:** 10,000 keys. Keys are ~8 bytes, Values are ~10 bytes. 
*Note: This acts as a stress test. WiscKey architectures are traditionally at a massive disadvantage with tiny payloads because the overhead of separate file pointers outweighs the cost of simply moving the small values during LSM compaction.*

### Baseline Results (10,000 Memtable Limit)

| Operation | VortexDB | LevelDB | SQLite (Indiv) | SQLite (Batch) |
| :--- | :--- | :--- | :--- | :--- |
| **Write (New)** | **550,221 ops/s** | 188,175 ops/s | 11,222 ops/s | 641,786 ops/s |
| **Update (Half)** | 49,465 ops/s | **188,215 ops/s** | 11,932 ops/s | 645,161 ops/s |
| **Read Hit (Cold)** | 0.063 ms/op | **0.0008 ms/op** | 0.055 ms/op | 0.055 ms/op |
| **Read Miss** | **0.001 ms/op** | 0.0004 ms/op | 0.051 ms/op | 0.055 ms/op |
| **Range Scan** | 6.58 ms | **0.17 ms** | 0.36 ms | 0.33 ms |

### Small Payload Scaling: Raw Output
Our testing reveals a clear relationship between memory configuration and throughput. 

* **Memtable 1k:** A highly restricted memtable forces constant SSTable flushing, creating severe I/O bottlenecks.
  ![Terminal - Small 1k](docs/images/small_1k.png)
* **Memtable 10k:** The optimal "Goldilocks" zone. The buffer allows massive sequential VLog batches.
  ![Terminal - Small 10k](docs/images/small_10k.png)
* **Memtable 100k:** Diminishing returns on latency, but massive gains in ingestion speed.
  ![Terminal - Small 100k](docs/images/small_100k.png)

### Visualizing Small Payload Performance

> **📊 ![Bar Chart - Small Payload Scaling](docs/images/small_payload_benchmark.png)**
> *Caption:* **Small Payload Ingestion Scaling:** As the memtable expands from 1k to 10k and 100k, VortexDB's performance skyrockets, overcoming the traditional WiscKey small-payload penalty. 

> **📊 ![Bar Chart - 100k Memtable Focus](docs/images/small_payload_100k_only.png)**
> *Caption:* **The 100k Memtable Buffer Advantage:** In this small-payload scenario, VortexDB (~536k ops/s) drastically outperforms LevelDB (~186k ops/s). Why? By expanding the memtable to 100,000 entries, VortexDB acts as a massive shock-absorber. It aggregates tiny payloads in memory and flushes them to the append-only VLog in huge, contiguous, zero-copy sequential batches. This completely bypasses the severe compaction write-amplification that chokes standard LSM-trees (like LevelDB).

> **📊 ![Bar Chart - Read Miss Scaling](docs/images/small_payload_read_miss.png)**
> *Caption:* **The Scaling Cuckoo Filter:** This graph isolates Read Miss latency. While SQLite consistently burns ~0.05ms doing useless disk I/O for missing keys, LevelDB's internal filters keep it flat at ~0.0005ms. Notice VortexDB's curve: at a 1k memtable, the filter struggles with constant flushing (`0.013ms`). But as the memtable expands to 100k, VortexDB's Cuckoo Filter is unleashed, dropping to an incredible `0.0003ms` and actually outperforming LevelDB in ghost-read deflection.

---

## 🧪 Phase 2: The Large Payload Reality Check (1KB)

**Methodology:** 10,000 keys. Keys are ~8 bytes, Values are **1,024 bytes (1KB)**.
*Note: This is the environment WiscKey was designed for. Heavy values typically cause severe write-amplification in standard LSM-trees.*

### Baseline Results (10,000 Memtable Limit)

| Operation | VortexDB | LevelDB | SQLite (Indiv) | SQLite (Batch) |
| :--- | :--- | :--- | :--- | :--- |
| **Write (New)** | 45,709 ops/s | **95,153 ops/s** | 9,878 ops/s | 176,495 ops/s |
| **Update (Half)** | 18,823 ops/s | **87,635 ops/s** | 12,133 ops/s | 158,784 ops/s |
| **Read Hit (Cold)** | 0.056 ms/op | **0.004 ms/op** | 0.060 ms/op | 0.062 ms/op |
| **Read Miss** | **0.0008 ms/op** | 0.0007 ms/op | 0.052 ms/op | 0.054 ms/op |
| **Range Scan** | 7.86 ms | **1.78 ms** | 2.48 ms | 2.38 ms |

### Large Payload Scaling: Raw Output

* **Memtable 1k:** With 1KB payloads, memory fills up 100x faster. A 1k limit means the engine is virtually halting on every batch to flush.
  ![Terminal - Large 1k](docs/images/large_1k.png) 
* **Memtable 10k:** The engine breathes easier, allowing the VLog buffer to saturate the disk efficiently.
  ![Terminal - Large 10k](docs/images/large_10k.png) 
* **Memtable 100k:** Reaching the limits of the hardware bandwidth for this payload size.
  ![Terminal - Large 100k](docs/images/large_100k.png) 

### Visualizing Large Payload Performance

> **📊 ![Bar Chart - Large Payload Write Ops](docs/images/large_payload_benchmark.png)**
> *Caption:* **1KB Payload Ingestion - The Read-Before-Write Penalty:** LevelDB wins this specific micro-benchmark. This exposes the core architectural trade-off of VortexDB's most unique feature: **Recursive Sub-Databases.** Because VortexDB allows databases to be nested inside keys, every `put()` operation requires an initial safety lookup to ensure a structural directory isn't being overwritten. This turns every insertion into a Read-Before-Write. LevelDB executes "Blind Writes" directly to its memory buffer, winning the benchmark, but VortexDB trades that raw speed for strict structural safety.

> **📊 ![Bar Chart - Read Miss Latency](docs/images/read_miss_latency.png)**
> *Caption:* **The Cuckoo Filter Advantage (1KB):** When querying non-existent keys (Read Miss), SQLite must perform disk I/O, resulting in high latency. VortexDB's in-memory 4-slot Cuckoo Filter mathematically proves the key does not exist and drops the operation before touching the disk, matching LevelDB's near-zero latency (~0.0008ms). Notice how the VortexDB and LevelDB bars are virtually invisible compared to SQLite's massive latency spike.

---

## 🏆 Architectural Triumphs (The Good)

1. **The Cuckoo Shield:**
   VortexDB consistently resolves non-existent keys in **< 0.001 ms**. By placing a 4-slot Cuckoo Membership filter in front of the engine, ghost-reads are mathematically proven false in memory and dropped before a single disk seek is attempted.
2. **Zero-Amplification Ingestion:**
   When pushing new keys, the payload is appended to the `.vlog` exactly once. Bypassing the compaction hell of standard LSMs allows VortexDB to crush LevelDB on raw, sequential small-payload ingestion when the memtable is properly tuned.

---

## ⚠️ Architectural Compromises (The Bad)

1. **The Disconnected Seek (Read Hits are Slow):**
   VortexDB takes ~0.05ms to find a key, significantly slower than LevelDB. To read a value, VortexDB must look up the key in the SSTable to extract a `ValuePointer`, then perform a **random disk seek** into the `.vlog` file.
2. **The Range Scan Trap:**
   Scanning sequential keys is slow because values are scattered chronologically in the VLog rather than sorted alphabetically with the keys, generating hundreds of random, unoptimized disk reads. 
3. **The Update Penalty:**
   Because VortexDB checks for `SUB_VORTEX` collisions to protect its hierarchical structure, updates are inherently bottlenecked by read latency.

---

## 🛠️ Tech Stack & Design Decisions

- **Why C++17:** Chosen for deterministic memory management and raw pointer precision. This is strictly necessary for managing the recursive sub-database tree, orchestrating multi-threaded read/write locks, and handling high-performance byte-level buffer I/O.
- **Why WiscKey over pure LSM:** Traditional LSM-trees suffer from severe write amplification when handling larger payloads (like JSON objects or serialized state) over long periods. By isolating keys from values, VortexDB trades away sequential Range Scan speed in exchange for drastically reduced compaction overhead and high-velocity continuous ingestion.
- **Why Winsock2:** The networking layer utilizes Windows Sockets 2 directly to maintain absolute, low-level control over `TCP_NODELAY` settings and client thread detachment, completely avoiding the bloat of third-party libraries.
