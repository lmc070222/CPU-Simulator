# Register File + Rename Mapping Table 设计文档

## 1. 模块定位

RegisterFile 是 Tomasulo 架构中的**寄存器文件 + 重命名映射表（RAT）**。它有两个核心职责：

1. **存储体系结构寄存器的值**（`x0`-`x31`）
2. **维护寄存器重命名映射**：记录每个寄存器的最新值将由哪个 ROB 条目产生

在 Tomasulo 中，寄存器重命名通过 ROB id 实现。当一条写寄存器的指令被发射（issue），`rd` 寄存器被标记为 "busy"，对应的 `reorder_id` 指向该指令的 ROB 条目。后续读该寄存器的指令不直接读寄存器值，而是通过 ROB id 从 CDB 或 ROB 中获取结果。

---

## 2. 数据成员

```cpp
static const int REG_COUNT = 32;

// --- 旧状态（当前周期读取用） ---
uint32_t reg[REG_COUNT];        // 寄存器值
int32_t  reorder_id[REG_COUNT]; // 重命名映射：>=0 表示将由该 ROB 条目写入，-1 表示值已就绪
bool     busy[REG_COUNT];       // 是否被未提交指令占用

// --- 新状态（当前周期写入，下周期变为旧状态） ---
uint32_t reg_new[REG_COUNT];
int32_t  reorder_id_new[REG_COUNT];
bool     busy_new[REG_COUNT];
```

| 成员 | 旧/新 | 含义 |
|------|-------|------|
| `reg[]` | 旧 | 寄存器当前已提交的值，供 `read()` 使用 |
| `reg_new[]` | 新 | 本周期 commit 写入的值 |
| `reorder_id[]` | 旧 | 本周期可见的重命名映射（issue 时读取） |
| `reorder_id_new[]` | 新 | 本周期 issue 写入的映射（下周期生效） |
| `busy[]` | 旧 | 本周期可见的 busy 状态 |
| `busy_new[]` | 新 | 本周期修改的 busy 状态（下周期生效） |

---

## 3. 方法详细说明

### 3.1 `void init()`

**调用时机**：CPU 初始化时

**行为**：
- 所有 `reg` / `reg_new` 清零
- 所有 `reorder_id` / `reorder_id_new` 设为 -1（表示就绪）
- 所有 `busy` / `busy_new` 设为 false

---

### 3.2 `uint32_t read(uint8_t idx)`

**调用时机**：issue 阶段，保留站读取源操作数时

**行为**：读取**旧状态**的 `reg[idx]`。

**x0 特殊处理**：x0 永远返回 0，即使 `reg[0]` 被错误写入非零值。

**接口**：
```cpp
uint32_t read(uint8_t idx) {
    if (idx == 0) return 0;
    return reg[idx];
}
```

---

### 3.3 `int32_t get_reorder(uint8_t idx)`

**调用时机**：issue 阶段，保留站读取源操作数的重命名 tag

**行为**：读取**旧状态**的 `reorder_id[idx]`。

**x0 特殊处理**：x0 永远返回 -1（已就绪），因为它不需要重命名。

**接口**：
```cpp
int32_t get_reorder(uint8_t idx) {
    if (idx == 0) return -1;
    return reorder_id[idx];
}
```

---

### 3.4 `bool is_busy(uint8_t idx)`

**调用时机**：issue 阶段，判断源操作数是否就绪

**行为**：读取**旧状态**的 `busy[idx]`。

**x0 特殊处理**：x0 永远返回 false。

**接口**：
```cpp
bool is_busy(uint8_t idx) {
    if (idx == 0) return false;
    return busy[idx];
}
```

---

### 3.5 `void set_reorder(uint8_t idx, int32_t rob_id)`

**调用时机**：issue 阶段，指令被发射到保留站后

**行为**：将目标寄存器 `idx` 标记为由 ROB 条目 `rob_id` 写入。

**详细逻辑**：
1. 如果 `idx == 0`（x0），**直接返回，不做任何修改**。x0 永远不被重命名。
2. 写入 `reorder_id_new[idx] = rob_id`
3. 写入 `busy_new[idx] = true`
4. `reg_new[idx]` 不动（commit 时才写）

**调用上下文**（在 CPU::step() 的 issue 阶段）：
```
对于每条发射的指令:
  if (decoded.rd != 0) {
    rf.set_reorder(decoded.rd, rob_id);
  }
```

**注意**：此方法写的是 `_new` 状态，因为 issue 和 commit 在同周期的不同阶段执行，必须使用新状态来避免同一周期内的冲突。

---

### 3.6 `void write(uint8_t idx, uint32_t val, int32_t rob_id)`

**调用时机**：commit 阶段，ROB 提交一条指令时

**行为**：将计算结果写入寄存器，并清除该寄存器的重命名映射。

**详细逻辑**：
1. 如果 `idx == 0`，直接返回（x0 不可写）
2. **检查 `reorder_id[idx] == rob_id`**（读取的是**旧状态** `reorder_id`，因为 issue 阶段写入的是 `reorder_id_new`，还未更新到旧状态）
   - **如果匹配**：说明这条指令确实是最新的写该寄存器的指令
     - `reg_new[idx] = val`
     - `busy_new[idx] = false`
     - `reorder_id_new[idx] = -1`
   - **如果不匹配**：说明之后有一条新指令也写了同一寄存器（`reorder_id[idx]` 已被覆盖为更晚的 rob_id），此 commit 不更新寄存器值，仅清除 busy/映射是不对的——实际上不做任何操作。因为：
     - 后发射的指令已经用新的 rob_id 覆盖了 `reorder_id_new`
     - 旧指令的结果应该被丢弃（覆盖写入是一种优化：新值会覆盖旧值）
     - 但是 busy_new 不应该被清除，因为新指令还在执行中

**接口**：
```cpp
void write(uint8_t idx, uint32_t val, int32_t rob_id) {
    if (idx == 0) return;
    if (reorder_id[idx] == rob_id) {  // 读旧状态，确保是最新的映射
        reg_new[idx] = val;
        busy_new[idx] = false;
        reorder_id_new[idx] = -1;
    }
    // 如果不匹配：后续指令已覆盖映射，跳过
}
```

---

### 3.7 `void execute()`

**调用时机**：每个时钟周期，在 CPU::step() 中被调用

**行为**：处理 x0 重置和 CDB 监听。

**详细逻辑**：
1. 将 `reg_new[0]` 设为 0，确保 x0 恒为零
2. 将 `busy_new[0]` 设为 false
3. 将 `reorder_id_new[0]` 设为 -1

这确保即使某条指令错误地写了 x0，下一个周期 x0 也会被重置为 0。

**如果需要在 execute 中监听 CDB 更新 reg_new**（可选）：
- 如果设计为 CDB 广播结果也写入寄存器文件（除了写入 ROB），则在此处理。
- **推荐方案**：寄存器值只在 commit 时写入。CDB 广播的结果由 ROB 和 RS 捕获，寄存器文件不直接监听 CDB。

---

### 3.8 `void update()`

**调用时机**：每个时钟周期末尾，在 CPU::step() 中最后调用

**行为**：将所有「新状态」拷贝到「旧状态」，模拟时钟上升沿触发。

**接口**：
```cpp
void update() {
    for (int i = 0; i < REG_COUNT; i++) {
        reg[i] = reg_new[i];
        reorder_id[i] = reorder_id_new[i];
        busy[i] = busy_new[i];
    }
    // 再次确保 x0
    reg[0] = 0;
    busy[0] = false;
    reorder_id[0] = -1;
}
```

---

## 4. 与其它模块的交互

```
                     issue 阶段                         commit 阶段
  保留站 (RS) ──────────► read() ──────► 寄存器值/Vj/Vk
                  ──────► get_reorder() ► Qj/Qk  ──────► 保留站
                  ──────► is_busy() ───► 判断就绪
                  
  保留站 (RS) ── set_reorder(rd, rob_id) ──► reorder_id_new[rd] = rob_id
                                              busy_new[rd] = true

  ROB ────────── write(rd, val, rob_id) ──► 匹配则 reg_new[rd] = val
                                              busy_new[rd] = false
                                              reorder_id_new[rd] = -1
```

---

## 5. 完整生命周期示例

以 `ADD x3, x1, x2` 后跟 `SUB x5, x3, x4` 为例：

| 阶段 | 操作 | reg[3] | busy[3] | reorder_id[3] |
|------|------|--------|---------|---------------|
| 初始 | - | 0 | false | -1 |
| issue ADD | `set_reorder(3, ROB#0)` | 0 | false→true(new) | -1→0(new) |
| update | | 0 | true | 0 |
| issue SUB | `read(3)`→0, `get_reorder(3)`→0, `is_busy(3)`→true | | | |
| SUB 的 RS | Vj=0, Qj=0（等待 ROB#0 的结果）| | | |
| ADD 执行完 | CDB 广播 ROB#0=42 | | | |
| RS 监听 CDB | Qj==ROB#0? 匹配→Vj=42, Qj=-1 | | | |
| commit ADD | `write(3, 42, 0)` | 0→42(new) | true→false(new) | 0→-1(new) |
| update | | 42 | false | -1 |

---

## 6. 边界条件与注意事项

| 场景 | 处理方式 |
|------|---------|
| rd == x0 | `set_reorder`、`write` 直接 return，不修改任何状态 |
| rs1/rs2 == x0 | `read` 返回 0，`get_reorder` 返回 -1，`is_busy` 返回 false |
| 同一寄存器被两条指令写 | `set_reorder` 覆盖 `reorder_id_new`；commit 时旧指令检测到不匹配，丢弃结果 |
| 分支预测错误 flush | 需要在 flush 时恢复 `reorder_id` 和 `busy`（需额外机制，见下文） |

---

## 7. Flush 机制（分支预测错误恢复）

当分支预测错误时，需要回滚寄存器重命名状态。有两种方案：

**方案 A（推荐）：遍历 ROB + 重建映射表**
- 在 `flush()` 中遍历所有 `reorder_id`，若 `reorder_id[i]` 指向被 flush 的 ROB 条目（或更新的），则清除：
  - `reorder_id_new[i] = -1`
  - `busy_new[i] = false`
- 这需要额外传入一个函数：`void flush(int first_flushed_rob_id)`

**方案 B：checkpoint / 快照**
- 每次分支指令发射时保存一份 `reorder_id` 副本
- Flush 时恢复到快照状态
- 需要额外存储空间

**建议先用方案 A**，接口：
```cpp
void flush_after(int32_t rob_id) {
    for (int i = 1; i < REG_COUNT; i++) {
        if (reorder_id[i] >= rob_id) {
            reorder_id_new[i] = -1;
            busy_new[i] = false;
        }
    }
}
```
此方法应在 `execute()` 阶段被 ROB 调用，写入 new 状态。

---

## 8. 头文件结构

```cpp
// register_file.hpp
#ifndef REGISTER_FILE_HPP
#define REGISTER_FILE_HPP

#include "types.hpp"

class RegisterFile {
private:
    uint32_t reg[REG_COUNT];
    uint32_t reg_new[REG_COUNT];
    int32_t  reorder_id[REG_COUNT];
    int32_t  reorder_id_new[REG_COUNT];
    bool     busy[REG_COUNT];
    bool     busy_new[REG_COUNT];

public:
    void init();

    // 读旧状态
    uint32_t read(uint8_t idx);
    int32_t  get_reorder(uint8_t idx);
    bool     is_busy(uint8_t idx);

    // 写新状态
    void set_reorder(uint8_t idx, int32_t rob_id);
    void write(uint8_t idx, uint32_t val, int32_t rob_id);

    // 周期方法
    void execute();
    void update();
    void flush_after(int32_t rob_id);
};

#endif
```
