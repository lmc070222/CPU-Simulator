# types.hpp — 公共类型定义 设计文档

## 1. 概述

`types.hpp` 是整个模拟器的「全局头文件」，被所有模块包含。它定义：
- 全局常量（各模块容量、大小）
- 枚举类型（指令类别、功能单元、ALU 操作）
- 跨模块共享的结构体（译码结果、CDB 广播值）

**不含任何函数实现**（仅有常量、枚举、结构体定义）。

---

## 2. 全局常量

```cpp
const int REG_COUNT    = 32;        // 寄存器个数 (x0-x31)
const int MEM_SIZE     = 1 << 20;   // 内存大小 (1 MiB)

const int RS_ALU_COUNT = 4;         // ALU 保留站条目数
const int RS_MUL_COUNT = 2;         // 乘除保留站条目数 (bonus)
const int RS_LS_COUNT  = 4;         // 访存保留站条目数
const int RS_TOTAL     = RS_ALU_COUNT + RS_MUL_COUNT + RS_LS_COUNT;

const int ROB_SIZE     = 16;        // ROB 环形缓冲条目数
const int LSB_SIZE     = 8;         // Load/Store Buffer 条目数
const int CDB_COUNT    = 1;         // CDB 总线数量 (每周期最多广播结果数)
const int BHT_SIZE     = 64;        // 分支历史表条目数 (2-bit 饱和计数器)
```

| 常量 | 值 | 使用者 |
|------|-----|--------|
| `REG_COUNT` | 32 | RegisterFile, ROB, RS |
| `MEM_SIZE` | 1 MiB | Memory |
| `RS_TOTAL` | 10 | ReservationStations |
| `ROB_SIZE` | 16 | ReorderBuffer, RegisterFile |
| `LSB_SIZE` | 8 | LoadStoreBuffer |
| `CDB_COUNT` | 2 | CDB, RS, LSB |
| `BHT_SIZE` | 64 | BranchPredictor |

---

## 3. 枚举类型

### 3.1 `Opcode` — 指令操作码

与 `cpu.hpp` 中的 decoder 对齐，覆盖所有 RV32I 指令：

```cpp
enum Opcode {
    // U-type
    LUI, AUIPC,
    // J-type
    JAL,
    // I-type (JALR)
    JALR,
    // B-type (branch)
    BEQ, BNE, BLT, BGE, BLTU, BGEU,
    // I-type (load)
    LB, LH, LW, LBU, LHU,
    // S-type (store)
    SB, SH, SW,
    // I-type (ALU immediate)
    ADDI, SLTI, SLTIU, XORI, ORI, ANDI, SLLI, SRLI, SRAI,
    // R-type (ALU register)
    ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND,
    // RV32M (bonus)
    MUL,
    // System
    ECALL, EBREAK,
    // Sentinel
    UNKNOWN
};
```

### 3.2 `FUType` — 功能单元类型

用于 RS 分配和 CDB 仲裁时区分功能单元：

```cpp
enum FUType {
    FU_ALU,      // 整数 ALU (ADD, SUB, XOR, ...)
    FU_MUL,      // 乘法器 (MUL)
    FU_LOAD,     // 加载单元
    FU_STORE,    // 存储单元 (地址计算)
    FU_BRANCH    // 分支单元
};
``` 

### 3.3 `InstType` — 指令格式类型

用于 decoder 输出和 RS 执行逻辑中的立即数/操作数处理：

```cpp
enum InstType {
    R_TYPE,      // R-type: 三寄存器
    I_TYPE,      // I-type: 寄存器 + 立即数
    S_TYPE,      // S-type: store (rs1 + imm → addr, rs2 → data)
    B_TYPE,      // B-type: branch (rs1, rs2 + imm → target)
    U_TYPE,      // U-type: rd + upper 20-bit imm
    J_TYPE       // J-type: rd + 21-bit offset
};
```

### 3.4 `ALUOp` — ALU 操作类型

用于保留站中的操作选择（决定 RS 如何计算结果）：

```cpp
enum ALUOp {
    ALU_ADD, ALU_SUB, ALU_SLL, ALU_SLT, ALU_SLTU,
    ALU_XOR, ALU_SRL, ALU_SRA, ALU_OR, ALU_AND, ALU_MUL,
    ALU_COPY,    // 用于 LUI (直接传递立即数)
    ALU_NOP      // 无操作 / 占位
};
```

---

## 4. 结构体

### 4.1 `DecodedInst` — 译码后指令

Decoder 的输出，包含 CPU 执行该指令所需的全部信息：

```cpp
struct DecodedInst {
    Opcode   opcode;        // 操作码
    uint32_t pc;            // 指令地址 (用于分支预测 / flush)
    uint8_t  rd;            // 目标寄存器 (0-31)
    uint8_t  rs1;           // 源寄存器 1
    uint8_t  rs2;           // 源寄存器 2
    uint32_t imm;           // 符号扩展后的立即数 (int32_t 语义)
    InstType type;          // 指令格式
    FUType   fu_type;       // 应分配到的功能单元
    ALUOp    alu_op;        // ALU 操作类型
    bool     is_branch;     // 是否分支指令
    bool     is_mem_read;   // 是否加载指令
    bool     is_mem_write;  // 是否存储指令
    bool     is_halt;       // 是否停机指令 (0x0ff00513)
};
```

**`fu_type` 映射关系**：

| 指令 | FUType |
|------|--------|
| R-type ALU, I-type ALU, LUI, AUIPC | `FU_ALU` |
| MUL | `FU_MUL` |
| LB, LH, LW, LBU, LHU | `FU_LOAD` |
| SB, SH, SW (地址计算) | `FU_STORE` |
| BEQ, BNE, BLT, BGE, BLTU, BGEU | `FU_BRANCH` |
| JAL, JALR | `FU_ALU` (简单条件下可用 ALU) |

### 4.2 `CDBValue` — 公共数据总线广播值

```cpp
struct CDBValue {
    bool     valid;     // 该槽位是否有效
    uint8_t  rob_id;    // 产生结果的 ROB 条目编号
    uint32_t value;     // 计算结果 / 加载数据
};
```

`valid == false` 表示该 CDB 槽位本周期无数据广播。

---

## 5. 头文件骨架

```cpp
#ifndef TYPES_HPP
#define TYPES_HPP

#include <cstdint>

// ============ 常量 ============
const int REG_COUNT    = 32;
const int MEM_SIZE     = 1 << 20;
const int RS_ALU_COUNT = 4;
const int RS_MUL_COUNT = 2;
const int RS_LS_COUNT  = 4;
const int RS_TOTAL     = RS_ALU_COUNT + RS_MUL_COUNT + RS_LS_COUNT;
const int ROB_SIZE     = 16;
const int LSB_SIZE     = 8;
const int CDB_COUNT    = 2;
const int BHT_SIZE     = 64;

// ============ 枚举 ============
enum Opcode { /* ... 见上文 3.1 ... */ };
enum FUType { FU_ALU, FU_MUL, FU_LOAD, FU_STORE, FU_BRANCH };
enum InstType { R_TYPE, I_TYPE, S_TYPE, B_TYPE, U_TYPE, J_TYPE };
enum ALUOp { ALU_ADD, ALU_SUB, ALU_SLL, ALU_SLT, ALU_SLTU, 
             ALU_XOR, ALU_SRL, ALU_SRA, ALU_OR, ALU_AND, ALU_MUL,
             ALU_COPY, ALU_NOP };

// ============ 结构体 ============
struct DecodedInst { /* ... 见上文 4.1 ... */ };
struct CDBValue    { /* ... 见上文 4.2 ... */ };

#endif
```

---

## 6. 与现有代码的整合

现有 `cpu.hpp` 中定义了自己的 `Opcode` 枚举、`Instr` 结构体。重构时：
- `Opcode` 枚举移入 `types.hpp`，`cpu.hpp` 通过 `#include "types.hpp"` 引用
- `Instr` 结构体可由 `DecodedInst` 替代（`DecodedInst` 更丰富）
- decoder 的输出类型改为 `DecodedInst`
- `load_memory` / `fetch` 等内存操作函数独立为 `memory.hpp`
