# Reservation Station 设计文档

## 1. 模块定位

保留站（Reservation Station）是 Tomasulo 架构的核心执行调度器。每条发射的指令被分配到对应功能单元的保留站条目中，等待源操作数就绪后即可乱序执行。

**职责**：
1. **暂存已发射但操作数未就绪的指令**
2. **监听 CDB**：当某条 CDB 广播结果的 ROB id 匹配自身等待的 `Qj`/`Qk` 时，捕获值并标记操作数就绪
3. **乱序发射**：当某个保留站条目两操作数均就绪（`Qj == Qk == -1`）时，将其送入对应功能单元执行
4. **广播结果**：计算完成后将结果写入 CDB

---

## 2. 数据成员

### 2.1 `RSEntry` — 保留站条目

```cpp
struct RSEntry {
    bool     busy;             // 该条目是否被占用
    FUType   fu;               // 所属功能单元类型
    ALUOp    alu_op;           // ALU 操作类型
    uint32_t Vj, Vk;           // 源操作数值（当 Q 为 -1 时有效）
    int32_t  Qj, Qk;           // 产生源操作数的 ROB id（-1 表示值已就绪）
    uint32_t imm;              // 立即数
    uint8_t  dest;             // 目标寄存器编号（用于 CDB 广播不直接使用，但调试有用）
    uint8_t  rob_id;           // 对应的 ROB 条目编号
    uint32_t pc;               // 指令地址
    bool     mem_read;         // 是否加载指令
    bool     mem_write;        // 是否存储指令
    bool     is_branch;        // 是否分支指令
    bool     predicted_taken;  // 分支预测方向
    uint32_t branch_target;    // 分支目标地址（预测时）
};
```

**字段详解**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `busy` | bool | true=占用，false=空闲。分配时置 true，执行完写 CDB 后置 false |
| `fu` | FUType | 决定该条目属于哪个功能单元池（分配时校验） |
| `alu_op` | ALUOp | ALU/BR 指令的计算操作；LOAD/STORE 时用于地址计算 |
| `Vj`, `Vk` | uint32_t | **源操作数实际值**。仅当对应 Q 为 -1 时有效 |
| `Qj`, `Qk` | int32_t | **源操作数依赖 tag**。值为产生该操作数的 ROB id；-1 表示已就绪 |
| `imm` | uint32_t | 符号扩展后的立即数（I-type/S-type/B-type 等） |
| `dest` | uint8_t | 目标寄存器，调试用（实际 CDB 广播用 rob_id） |
| `rob_id` | uint8_t | 该指令在 ROB 中的条目编号，CDB 广播时携带此 id |
| `pc` | uint32_t | 指令地址（分支预测错误时恢复 PC 用） |
| `mem_read/write` | bool | 标记访存类型（LOAD/STORE 需要地址就绪后转入 LSB） |
| `is_branch` | bool | 标记分支指令（执行后需通知 ROB / BP 实际方向） |
| `predicted_taken` | bool | 分支预测结果 |
| `branch_target` | uint32_t | 分支预测目标地址 |

### 2.2 `ReservationStations` 类成员

```cpp
class ReservationStations {
private:
    // 保留站条目数组（按功能单元划分区域）
    // 布局: [0..RS_ALU_COUNT-1] = ALU, 
    //       [RS_ALU_COUNT..RS_ALU_COUNT+RS_MUL_COUNT-1] = MUL,
    //       [RS_ALU_COUNT+RS_MUL_COUNT..RS_TOTAL-1] = LS
    RSEntry rs[RS_TOTAL];         // 旧状态
    RSEntry rs_new[RS_TOTAL];     // 新状态

    // 功能单元占用计数（旧/新状态对本周期执行时判断是否可再发射）
    int fu_busy_count[5];         // 旧状态 (按 FUType 索引)
    int fu_busy_count_new[5];     // 新状态

    // 本周期需要写入 CDB 的结果（每个 FU 类型最多 1 个）
    struct ExecResult {
        bool     ready;
        uint8_t  rob_id;
        uint32_t value;
        bool     is_branch;
        bool     branch_taken;   // 实际分支方向
        uint32_t branch_pc;      // 分支指令的 PC
        uint32_t branch_target;  // 实际目标
    };
    ExecResult exec_out[5];       // 新状态，按 FUType 索引

public:
    // ... 方法
};
```

**功能区划分**：
```
rs[0..3]  → FU_ALU    (RS_ALU_COUNT=4)
rs[4..5]  → FU_MUL    (RS_MUL_COUNT=2)
rs[6..9]  → FU_STORE  (RS_LS_COUNT=4)
```

注意：`FU_LOAD` 和 `FU_STORE` 共用 LS 保留站区域；`FU_BRANCH` 共用 ALU 保留站区域。

---

## 3. 方法详解

### 3.1 `void init()`

**调用时机**：CPU 初始化 / reset

**行为**：
- 所有 `rs[]` / `rs_new[]` 条目的 `busy` 置为 false
- 所有 `Qj` / `Qk` 置为 -1
- `fu_busy_count` / `fu_busy_count_new` 全清零
- `exec_out` 全部标记为 not ready

---

### 3.2 `int allocate(FUType fu)`

**调用时机**：issue 阶段，将指令发射到保留站前

**行为**：在 `fu` 对应的保留站分区中查找第一个空闲（`busy == false`）条目。

**返回值**：条目索引（0 ~ RS_TOTAL-1）；若无空闲条目返回 -1（保留站满，stall）。

**伪代码**：
```cpp
int allocate(FUType fu) {
    int start, end;
    get_range(fu, start, end);  // 根据 FUType 确定索引范围
    for (int i = start; i < end; i++) {
        if (!rs_new[i].busy) return i;  // 读新状态（本周期可能已分配了其他条目）
    }
    return -1;
}
```

**注意**：读 `rs_new[].busy` 而非 `rs[].busy`，因为同一周期可能 issue 多条指令。

---

### 3.3 `void issue(int idx, const DecodedInst& inst, int rob_id, const RegisterFile& rf)`

**调用时机**：issue 阶段，`allocate()` 成功后

**行为**：将译码后的指令信息写入 RS 条目（新状态），并读取寄存器文件的**旧状态**获取源操作数。

**伪代码**：
```cpp
void issue(int idx, const DecodedInst& inst, int rob_id, const RegisterFile& rf) {
    RSEntry& e = rs_new[idx];
    e.busy = true;
    e.fu = inst.fu_type;
    e.alu_op = inst.alu_op;
    e.imm = inst.imm;
    e.dest = inst.rd;
    e.rob_id = rob_id;
    e.pc = inst.pc;
    e.mem_read = inst.is_mem_read;
    e.mem_write = inst.is_mem_write;
    e.is_branch = inst.is_branch;

    // 读源操作数 1
    e.Vj = rf.read(inst.rs1);
    e.Qj = rf.get_reorder(inst.rs1);   // -1 表示值已就绪；>=0 表示等待该 ROB
    if (e.Qj != -1) e.Vj = 0;          // 未就绪时 Vj 无效

    // 读源操作数 2
    e.Vk = rf.read(inst.rs2);
    e.Qk = rf.get_reorder(inst.rs2);
    if (e.Qk != -1) e.Vk = 0;

    // 特殊处理：不需要 rs2 的指令 (I-type/J-type/U-type)
    // 此时 Qk 应为 -1，Vk 应为 imm（或 0，根据不同指令）
    if (inst.type == I_TYPE || inst.type == U_TYPE || inst.type == J_TYPE) {
        e.Qk = -1;
        e.Vk = e.imm;   // 立即数存入 Vk（或根据 alu_op 使用 imm）
    }

    // 对于不需要 rs1 的指令 (LUI)
    if (inst.opcode == LUI) {
        e.Qj = -1;
        e.Vj = 0;
    }
}
```

**关键点**：
- `rf.read()` 和 `rf.get_reorder()` 读取的是寄存器文件的**旧状态**
- 若 `Qj != -1`（未就绪），则 `Vj` 无效（设为 0）
- 立即数存放策略：I-type/S-type 的 imm 存入 `e.imm` 字段；执行时根据 `alu_op` 决定使用 `Vk` 还是 `imm`

---

### 3.4 `void listen_cdb(const CDBValue& cdb)`

**调用时机**：在 `execute()` 开始时调用，每个 CDB 槽位调用一次

**行为**：遍历所有 busy 的 RS 条目（新状态），若某个条目的 `Qj` 或 `Qk` 等于 CDB 广播的 `rob_id`，则捕获值并清除该 Q。

**伪代码**：
```cpp
void listen_cdb(const CDBValue& cdb) {
    if (!cdb.valid) return;
    for (int i = 0; i < RS_TOTAL; i++) {
        if (!rs_new[i].busy) continue;
        if (rs_new[i].Qj == cdb.rob_id) {
            rs_new[i].Vj = cdb.value;
            rs_new[i].Qj = -1;
        }
        if (rs_new[i].Qk == cdb.rob_id) {
            rs_new[i].Vk = cdb.value;
            rs_new[i].Qk = -1;
        }
    }
}
```

**注意**：此方法写的是 `rs_new`，读的也是 `rs_new`（因为本周期内可能有多个 CDB 广播，listen 需要累积效果）。

---

### 3.5 `bool is_ready(int idx)`

**调用时机**：`execute()` 中判断某条目是否可执行

**行为**：检查 RS 条目（新状态）的 `Qj` 和 `Qk` 是否都为 -1。

**伪代码**：
```cpp
bool is_ready(int idx) {
    return rs_new[idx].busy && rs_new[idx].Qj == -1 && rs_new[idx].Qk == -1;
}
```

---

### 3.6 `void execute(CDB& cdb)`

**调用时机**：每个时钟周期，在 CPU::step() 中调用（步骤 4）

**行为**：
1. **监听 CDB**：遍历 CDB 所有槽位（旧状态），对每个有效的广播调用 `listen_cdb()`
2. **选出一条就绪指令执行**：按功能单元类型，每个类型选一个就绪的 RS 条目
3. **计算结果**：根据 `alu_op` 计算 ALU 结果（或分支结果、地址计算结果）
4. **广播到 CDB**：将结果写入 CDB 新状态

**伪代码**：
```cpp
void execute(CDB& cdb) {
    // --- 阶段 1: 监听 CDB (读取 CDB 旧状态) ---
    for (int slot = 0; slot < CDB_COUNT; slot++) {
        if (cdb.has_data(slot)) {
            listen_cdb(cdb.get_old(slot));
        }
    }

    // --- 阶段 2: 就绪检查 + 执行 ---
    int cdb_slot = 0;
    // 按优先级: BRANCH > ALU > MUL > LOAD > STORE
    bool dispatched[RS_TOTAL] = {false};

    // 对每个功能单元类型，选一个就绪条目
    for (int fu = FU_BRANCH; fu <= FU_STORE; fu++) {
        // 其实 FUType 没定义成连续整数，这里简化表示
        if (cdb_slot >= CDB_COUNT) break;
        
        int start, end;
        get_range((FUType)fu, start, end);
        
        for (int i = start; i < end && !dispatched[i]; i++) {
            if (is_ready(i) && !dispatched[i]) {
                dispatched[i] = true;
                
                // 执行计算
                uint32_t result = compute_result(i);
                
                // 对于 LOAD/STORE: 这里只做地址计算，实际访存在 LSB
                // （地址计算结果通过 CDB 广播或直接传给 LSB）
                
                // 广播到 CDB
                cdb.broadcast(cdb_slot, rs_new[i].rob_id, result);
                cdb_slot++;
                
                break;  // 每个 FU 类型每周期只执行一条
            }
        }
    }
}
```

---

### 3.7 `uint32_t compute_result(int idx)`

**内部辅助函数**，根据 `rs_new[idx]` 的 `alu_op`、`Vj`、`Vk`、`imm` 计算结果。

```cpp
uint32_t compute_result(int idx) {
    RSEntry& e = rs_new[idx];
    int32_t vj = (int32_t)e.Vj;
    int32_t vk = (int32_t)e.Vk;
    int32_t imm = (int32_t)e.imm;

    switch (e.alu_op) {
        case ALU_ADD:  return (uint32_t)(vj + vk);
        case ALU_SUB:  return (uint32_t)(vj - vk);
        case ALU_SLL:  return e.Vj << (e.Vk & 0x1F);
        case ALU_SLT:  return (vj < vk) ? 1 : 0;
        case ALU_SLTU: return (e.Vj < e.Vk) ? 1 : 0;
        case ALU_XOR:  return e.Vj ^ e.Vk;
        case ALU_SRL:  return e.Vj >> (e.Vk & 0x1F);
        case ALU_SRA:  return (uint32_t)(vj >> (e.Vk & 0x1F));
        case ALU_OR:   return e.Vj | e.Vk;
        case ALU_AND:  return e.Vj & e.Vk;
        case ALU_MUL:  return e.Vj * e.Vk;
        case ALU_COPY: return e.imm;  // LUI
        default:       return 0;
    }
}
```

---

### 3.8 `void update()`

**调用时机**：每个时钟周期末尾

**行为**：将 `rs_new` 拷贝到 `rs`，将 `fu_busy_count_new` 拷贝到 `fu_busy_count`。

另外需要**清理已完成的条目**：遍历所有 entry，若该条目在 `execute()` 中已被发射执行且结果已广播，则将其 `busy` 置 false。

做法：在 `execute()` 中，对已执行完成的条目，同时设置 `rs_new[idx].busy = false`。

```cpp
void update() {
    for (int i = 0; i < RS_TOTAL; i++) {
        rs[i] = rs_new[i];
    }
    for (int i = 0; i < 5; i++) {
        fu_busy_count[i] = fu_busy_count_new[i];
    }
}
```

---

### 3.9 `void flush_after(int32_t rob_id)`

**调用时机**：分支预测错误时，由 ROB 调用

**行为**：清除所有 `rob_id >= rob_id` 的 RS 条目（它们是被错误路径污染、应该废弃的指令）。

```cpp
void flush_after(int32_t rob_id) {
    for (int i = 0; i < RS_TOTAL; i++) {
        if (rs_new[i].busy && rs_new[i].rob_id >= rob_id) {
            rs_new[i].busy = false;
            rs_new[i].Qj = -1;
            rs_new[i].Qk = -1;
        }
    }
    // 同时清除旧状态，确保本周期 issue 阶段看到清理后的结果
    for (int i = 0; i < RS_TOTAL; i++) {
        if (rs[i].busy && rs[i].rob_id >= rob_id) {
            rs[i].busy = false;
            rs[i].Qj = -1;
            rs[i].Qk = -1;
        }
    }
}
```

---

### 3.10 `bool is_full(FUType fu)`

**调用时机**：issue 阶段，判断是否可以发射指令

**行为**：检查 `fu` 对应分区是否还有空闲条目。

```cpp
bool is_full(FUType fu) {
    int start, end;
    get_range(fu, start, end);
    for (int i = start; i < end; i++) {
        if (!rs_new[i].busy) return false;
    }
    return true;
}
```

---

## 4. 与其它模块的交互

```
 issue 阶段:
   CPU ──────► allocate(fu)         → RS 索引
          ───► is_full(fu)          → 是否 stall
          ───► issue(idx, inst, rob_id, rf)  → 填充 RS 条目 (读 rf 旧状态)

 execute 阶段:
   RS ──────── listen_cdb(cdb_old)  → 捕获操作数 (写 rs_new)
          ─── compute_result(idx)   → 计算结果
          ─── cdb.broadcast(...)    → 广播结果 (写 cdb 新状态)
          ─── rs_new[idx].busy=0    → 标记条目已使用

 分支错误:
   ROB ───────► flush_after(rob_id) → 清除错误路径指令

 update 阶段:
   CPU ───────► update()            → rs_new → rs
```

---

## 5. 关键设计决策

### 5.1 立即数处理策略

对于 I-type 指令，`Vk` 不使用寄存器值而是使用符号扩展后的立即数。在 `issue()` 时：
- 计算 `rs2` 相关字段：若指令格式需要 `rs1 op imm`，则将 `Qk = -1`，`Vk = imm`
- 对于 R-type 指令：`Qj`/`Qk` 分别对应 `rs1`/`rs2`

### 5.2 LOAD/STORE 处理

LOAD/STORE 指令在 RS 中只完成**地址计算**（`Vj + imm` → 有效地址）。计算得到地址后：
- LOAD：将地址 + rob_id 发送到 LoadStoreBuffer，由 LSB 完成实际访存（带硬件延迟）
- STORE：将地址 + 数据值 + rob_id 发送到 LoadStoreBuffer，待 commit 时写入内存

地址计算结果通过**特定的机制**传递给 LSB（而非 CDB），可通过在 RS 中预留接口实现，具体在实现 LSB 时确定。

### 5.3 BRANCH 处理

分支指令在 RS 中就绪后执行：
1. 计算分支条件（`Vj` 与 `Vk` 比较）
2. 确定实际分支方向
3. 通知 ROB/branch_predictor 分支结果
4. 若预测错误，ROB 负责 flush

分支结果不作为 CDB 广播（分支不产生寄存器值），但需要通知 ROB。

### 5.4 每 FU 类型每周期最多执行一条

模拟硬件限制：每个功能单元每周期只能执行一条指令。CDB 最多有 `CDB_COUNT` 条总线。

---

## 6. 头文件骨架

```cpp
#ifndef RESERVATION_STATION_HPP
#define RESERVATION_STATION_HPP

#include "types.hpp"

struct RSEntry {
    bool     busy;
    FUType   fu;
    ALUOp    alu_op;
    uint32_t Vj, Vk;
    int32_t  Qj, Qk;
    uint32_t imm;
    uint8_t  dest;
    uint8_t  rob_id;
    uint32_t pc;
    bool     mem_read, mem_write;
    bool     is_branch;
    bool     predicted_taken;
    uint32_t branch_target;
};

class ReservationStations {
private:
    RSEntry rs[RS_TOTAL];
    RSEntry rs_new[RS_TOTAL];

    uint32_t compute_result(int idx);
    void     get_range(FUType fu, int& start, int& end);

public:
    void init();

    int  allocate(FUType fu);
    bool is_full(FUType fu);
    void issue(int idx, const DecodedInst& inst, int rob_id,
               const class RegisterFile& rf);

    bool is_ready(int idx);
    void listen_cdb(const CDBValue& cdb);
    void execute(class CDB& cdb);
    void update();
    void flush_after(int32_t rob_id);
};

#endif
```

---

## 7. `issue()` 中 `Vj`/`Vk`/`Qj`/`Qk` 填充规则速查

| InstType | Qj | Vj | Qk | Vk |
|----------|----|----|----|-----|
| R-type | `rf.get_reorder(rs1)` | `rf.read(rs1)` | `rf.get_reorder(rs2)` | `rf.read(rs2)` |
| I-type ALU | `rf.get_reorder(rs1)` | `rf.read(rs1)` | **-1** | **imm** |
| I-type Load | `rf.get_reorder(rs1)` | `rf.read(rs1)` | **-1** | **imm** |
| I-type JALR | `rf.get_reorder(rs1)` | `rf.read(rs1)` | **-1** | **imm** |
| S-type (addr) | `rf.get_reorder(rs1)` | `rf.read(rs1)` | **-1** | **imm** |
| S-type (data) | — | — | `rf.get_reorder(rs2)` | `rf.read(rs2)` |
| B-type | `rf.get_reorder(rs1)` | `rf.read(rs1)` | `rf.get_reorder(rs2)` | `rf.read(rs2)` |
| U-type (LUI) | **-1** | **0** | **-1** | **imm** |
| U-type (AUIPC) | **-1** | **pc** | **-1** | **imm** |
| J-type (JAL) | **-1** | **pc** | **-1** | **imm** |

注意：表中 Q=-1 的项表示该操作数不需要等待，值直接可用。
