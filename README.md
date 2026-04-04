# VortexDB
### A Recursive WiscKey Storage Engine
![Language](https://img.shields.io/badge/Language-C++-orange) ![Platform](https://img.shields.io/badge/Platform-Windows-blue) ![License](https://img.shields.io/badge/License-Apache_2.0-green)

---
### 🔗 Quick Links
- [⚡ The Efficiency Gap](#-the-efficiency-gap) - [⚙️ Architecture](#%EF%B8%8F-architecture-the-invisible-hook) - [📦 Installation Steps](#-installation)
- [📐 Usage Example](#-usage-example) - [✨Features](#supported-features) -[📄Documentation](#documentation) - [📂 Project Structure](#-project-structure)
---
## What Problem Does VortexDB Solve?

In traditional LSM-Trees (like LevelDB), both keys and values are moved during compaction. When values are large, this causes massive Write Amplification, slowing down the entire system.

VortexDB solves this by implementing a Separation of Concerns:
- Keys remain in the LSM-Tree (SSTables) for fast indexing.
- Values are stored in an append-only Value Log (VLog).

This ensures that during compaction, the engine only reshuffles small pointers, leaving the heavy data untouched.
<p align="right">(<a href="#veilar">back to top</a>)</p>

---

### ⚡ The Performance Gap

| Metric | Standard LSM-Tree | VortexDB (WiscKey) |
| :--- | :--- | :--- |
| **Compaction Cost** | High (Keys + Values moved) | **Low** (Only Keys moved) |
| **Write Amplification** | O(depth of tree) | **Near-Constant** O(1) |
| **Large Value Handling** | Slows down merging | **Native Efficiency** |
| **Organization** | Flat Key-Space | **Recursive Sub-Vortices** |

---

## ⚙️ Architecture: The "Vortex Hook"

VortexDB's standout feature is its ability to spawn child databases within parent databases, creating a tree-like storage hierarchy similar to a file system.

>**VortexDB** uses a **custom routing logic** that resolves paths (e.g., `root/users/configs/theme`). Each "Sub-Vortex" is a fully independent VortexDB instance with its own Memtable, SSTables, and VLog.

---

~~~ mermaid
sequenceDiagram
    participant Client
    participant Server
    participant Memtable
    participant VLog
    participant SSTable

    Client->>Server: SET "user_1" "data_payload"
    Server->>Server: Infer Type (JSON/String/Int)
    Server->>VLog: Append Entry + CRC32
    VLog-->>Server: Return ValuePointer {file_id, offset, size}
    Server->>Memtable: Insert Key + ValuePointer
    Note over Memtable: If size > 100k
    Memtable->>SSTable: Flush (Write Sparse Index)
    SSTable->>Server: Register in Manifest
~~~

---
## ✨Technical Features

### 🏗️ Storage & Indexing

- **Sparse Indexing:**  
  SSTables store a key-offset every 100 keys. This allows `O(logN)` lookups with minimal memory footprint.
- **Cuckoo Membership Filter:**  
  A 4-slot bucket Cuckoo Filter intercepts "ghost reads" before they hit the disk, significantly reducing I/O for non-existent keys.
- **Type Inference Engine:**  
  VortexDB automatically detects and tags data types (`INT`, `BOOL`, `JSON`, `STRING`) at the point of entry, optimizing storage and retrieval logic.

### 🚀 Runtime Optimizations

- **LRU Value Cache:**  
  The `VLogReader` maintains a 2,000-entry Least Recently Used (LRU) cache to eliminate disk seek latency for "hot" data.
- **Zero-Copy Buffering:**
  The `VLogWriter` utilizes a 64KB internal buffer to batch disk writes, protecting the SSD from excessive small-write cycles.
- **Multi-Threaded Winsock Server:**  
  A high-concurrency TCP server using `std::shared_mutex` (Single-Writer/Multiple-Reader) for thread-safe network access.

---

## 📐 Usage & Interactive Shell
VortexDB includes a powerful CLI that lets you navigate your database like a directory.

### 💻 Interactive Shell Example

```Vortex [root] > spawn users
Vortex [root] > cd users
Vortex [root/users] > put ameetesh {"role": "admin"}
Vortex [root/users] > get ameetesh
{"role": "admin"}
Vortex [root/users] > stats
--- [root_users] HEALTH REPORT ---
   Live Keys     : 1
   Total Entries : 1
   Waste Ratio   : 0.00%
```
### 🌐 Network Protocol (TCP Port 8080)

You can communicate with the engine via any TCP client:

-`SET <key> <value>` - Writes data and returns OK.

-`GET <key>` - Retrieves value or ERROR.

-`GET RANGE <start> <end>` - Performs a lexicographical range scan.

-`GET <prefix>*` - Prefix-based search (e.g., GET user_*).

-`SAVE <state_name>` - Creates a snapshot of the current DB state.

---

### 🛡️ Data Integrity & Recovery
VortexDB is built for persistence. Every build and run cycle is protected by:
- **CRC32 Checksums:**  
  Every VLog entry is hashed. If data is corrupted on disk, the reader detects the mismatch and prevents invalid data from reaching the application.
- **The Manifest System:**
  A binary registry that tracks all active SSTables. This prevents "Orphaned Files" if the system crashes during a flush.
- **Automatic VLog Recovery:**  
  On startup, VortexDB scans the Value Log to reconstruct the Memtable if the index file is missing or out of sync.

---

## 📊 Benchmarking & Performance Analysis

To validate VortexDB's architecture, we benchmarked it against standard embedded databases: **LevelDB** and **SQLite** (in both individual and batched transaction modes). The tests expose the highly asymmetric performance profile of the WiscKey architecture. 

### Comprehensive Results (Memtable Size: 10,000)

| Operation | Metric | VortexDB | LevelDB | SQLite (Indiv) | SQLite (Batch) |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **1. Write (New)** | ops/sec | **550,221** | 188,175 | 11,222 | 641,786 |
| **2. Update (Half)**| ops/sec | **49,465** | 188,215 | 11,932 | 645,161 |
| **3. Read Hit** | ms/op (Cold) | **0.063** | 0.0008 | 0.055 | 0.055 |
| | ms/op (Hot) | **0.059** | 0.0007 | 0.054 | 0.056 |
| **4. Read Miss** | ms/op | **0.001** | 0.0004 | 0.051 | 0.055 |
| **5. Range Scan** | ms (Cold) | **6.58** | 0.17 | 0.36 | 0.33 |
| | ms (Hot) | **6.44** | 0.15 | 0.28 | 0.27 |

---

### Architectural Breakdown: Why VortexDB Behaves This Way

The numbers above perfectly illustrate the theoretical trade-offs of separating keys from values. Here is the engineering reality behind each metric:

**1. Write (New) — The Buffer Advantage (Extremely Fast)**
VortexDB writes new data significantly faster than LevelDB. Because values are appended sequentially to the Value Log (VLog) and keys are simply inserted into an in-memory `std::map`, there is zero write-amplification during ingestion. If the Memtable buffer is large enough (10,000+), the SSD is saturated with highly efficient sequential writes.

**2. Update (Overwrites) — The Sub-Vortex Penalty (Slow)**
Notice the massive drop from 550k ops/sec on new writes to 49k ops/sec on updates. This is an intentional architectural trade-off. In VortexDB, the `put()` function checks if a key already exists to prevent accidentally overwriting a recursive Sub-Database (`SUB_VORTEX`). This safety check forces a random disk-read to the VLog *during* the write operation, severely throttling the throughput compared to raw appends.

**3. Read Hit — The Disconnected Seek (Slower)**
VortexDB takes ~0.06 ms for a Read Hit, which is noticeably slower than LevelDB (~0.0008 ms). This is the primary WiscKey trade-off: finding a value requires a two-step process. First, it looks up the key in the SSTable/Memtable to get the pointer, and then it performs a **random disk seek** into the `.vlog` file to fetch the actual string. The 2,000-entry LRU Cache helps slightly on "Hot" reads, but cannot beat LevelDB's contiguous memory layouts.

**4. Read Miss — The Cuckoo Shield (Extremely Fast)**
VortexDB resolves non-existent keys in near-zero time (~0.001 ms). Before the engine even attempts to search the Memtable or read a Sparse Index from disk, the in-memory 4-slot **Cuckoo Filter** mathematically proves the key does not exist and immediately drops the operation.

**5. Range Scans — The Random I/O Trap (Slow)**
This is the most famous limitation of the WiscKey design. Because values are separated from keys, a range scan means iterating sequentially through the SSTable keys, but then firing off hundreds of *random, non-sequential disk reads* to the VLog to gather the payloads. LevelDB wins here because standard LSM-Trees store keys and values together, turning a range scan into a single, blazing-fast sequential disk read.

---

### Test Results by Memtable Size Configuration

Our testing reveals a clear relationship between configuration and performance. Increasing the `memtable` limit allows the VLog to batch larger sequential writes before pausing to flush the SSTables.

**1. Memtable Limit: 1,000**
*A highly restricted memtable forces constant SSTable flushing, creating severe I/O bottlenecks and throttling write performance.*
![Benchmark - Memtable 1000](placeholder_path_to_image_1000)

**2. Memtable Limit: 10,000**
*The optimal "Goldilocks" zone for this hardware. The buffer is large enough to allow massive sequential VLog batches, rocketing write speeds past LevelDB.*
![Benchmark - Memtable 10000](placeholder_path_to_image_10000)

**3. Memtable Limit: 100,000**
*Increasing the buffer further yields diminishing returns. While writes remain incredibly fast, the massive in-memory tree slightly increases lookup latency.*
![Benchmark - Memtable 100000](placeholder_path_to_image_100000)

---

## 🛠️ Tech Stack Decisions

- **Why C++17:** Chosen for deterministic memory management and raw pointer precision. This is strictly necessary for managing the recursive sub-database tree, orchestrating multi-threaded read/write locks, and handling high-performance byte-level buffer I/O.
- **Why WiscKey over pure LSM:** Traditional LSM-trees suffer from severe write amplification when handling larger payloads (like JSON objects or serialized game states). By isolating keys from values, VortexDB trades away sequential Range Scan speed in exchange for drastically reduced compaction overhead and high-velocity continuous ingestion.
- **Why Winsock2:** The networking layer utilizes Windows Sockets 2 directly to maintain absolute, low-level control over `TCP_NODELAY` flag settings and client thread detachment, avoiding the overhead of heavy third-party networking libraries.

---

## ⚠️Technical Trade-offs (Known & Intentional)
VortexDB is a **Systems Exploration project**. To achieve its specific performance goals, certain trade-offs were made:
- **Platform Lock-in:**  
  The networking layer utilizes `Winsock2`, making the server component specific to Windows environments.
- **Blocking Compaction:**
  To ensure 100% consistency, the `compact()` operation is a "Stop-The-World" event that locks the database during the merge.
- **Storage Overhead:**  
  Because it is optimized for speed, VortexDB creates multiple files (VLogs, SSTs, Idx, Manifest). Use `compact` regularly to prune stale data.
- **Raw Pointer Management:**  
  The engine uses raw pointers for the recursive sub-db tree to maximize control over the destruction order, requiring careful manual memory management in the `VortexDB` destructor.

---

### 📂 Project Structure

```text
/
├── vortex.h / .cpp       # 🧠 The Core Engine & Path Router
├── vlog.h / .cpp         # 📜 WiscKey Value Log (Buffered I/O + LRU)
├── sstable.h / .cpp      # 📑 Sorted String Tables (Sparse Index)
├── cuckoo.h / .cpp       # 🐦 Probabilistic Membership Filter
├── manifest.h / .cpp     # 🗃️ DB State & File Registry
├── server.h / .cpp       # 🌐 Winsock2 Multi-threaded Server
└── main.cpp              # 🐚 Interactive Shell & Entry Point
```

---

## 🚀 Getting Started

**Prerequisites**
- **Compiler:** MSVC (Visual Studio 2019+) or MinGW with C++17 support.
- **Library:** `ws2_32.lib` (Linked automatically via `#pragma`).


**Build & Run**
- Clone the repository.
- Compile using your preferred C++ compiler:
  `g++ -std=c++17 main.cpp vortex.cpp vlog.cpp sstable.cpp manifest.cpp cuckoo.cpp server.cpp -lws2_32 -o vortexdb.exe`
- Launch `vortexdb.exe`.

---

## 👨‍💻Author

Built by **Ameetesh**  
B.Tech Undergraduate (South Asian University)  
Focused on Distributed Systems, Database Internals, and Performance Engineering.

---

## License

Apache License 2.0.

<p align="right">(<a href="#veilar">back to top</a>)</p>
