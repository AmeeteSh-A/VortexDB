# VortexDB
### A Recursive WiscKey Storage Engine
![Language](https://img.shields.io/badge/Language-C++-orange) ![Platform](https://img.shields.io/badge/Platform-Windows-blue) ![License](https://img.shields.io/badge/License-MIT-green)

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
