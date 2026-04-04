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


