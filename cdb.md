# CDB (Common Data Bus) 设计文档

## 1. 模块定位

CDB（Common Data Bus）是 Tomasulo 架构中的**公共数据总线**，负责将功能单元（保留站、访存单元）产生的计算结果广播给所有需要它的模块。

在硬件中，CDB 就是一组连线——功能单元把结果「驱动」到总线上，所有监听者（保留站、ROB）在同一周期内捕获。在模拟器中，CDB 是一个集中式的广播/收集中心。

**核心职责**：
1. 接收来自 RS 和 LSB 的计算结果
2. 仲裁多个同时产生的结果（最多 `CDB_COUNT=2` 个/周期）
3. 将结果广播给所有监听者（RS、ROB）
4. 维护新/旧两套状态，遵循硬件时序

---

## 2. 数据成员

```cpp
class CDB {
private:
    CDBValue old;        // 旧状态：本周期所有模块可读
    CDBValue buses_new[CDB_COUNT];    // 新状态：本周期被写入，下周期变为旧状态
    
    // 广播请求队列（用于 execute 阶段的仲裁）
    struct BroadcastRequest {
        uint8_t  rob_id;
        uint32_t value;
        int      priority;   // 0=最高 (branch), 1=load, 2=ALU, 3=MUL
    };
    BroadcastRequest pending[CDB_COUNT + 2];  // 暂存本周期所有广播请求
    int pending_count;                         // 请求计数（新状态）
};
```

**`CDBValue` 结构体**（定义在 `types.hpp`）：

```cpp
struct CDBValue {
    bool     valid;      // 该槽位本周期是否有效
    uint8_t  rob_id;     // 产生结果的 ROB 条目编号
    uint32_t value;      // 计算结果 / 加载数据
};
```

---

## 3. 方法详解

### 3.1 `void init()`

**调用时机**：CPU 初始化

**行为**：
- 所有 `buses[]` / `buses_new[]` 的 `valid` 置 `false`
- `rob_id` 置 0，`value` 置 0
- `pending_count = 0`

---

### 3.2 `void clear()`

**调用时机**：每个时钟周期开始时（step() 第 1 步）

**行为**：
- 将 `buses_new` 所有槽位的 `valid` 置 `false`
- 将 `pending_count` 置 0

```cpp
void clear() {
    for (int i = 0; i < CDB_COUNT; i++) {
        buses_new[i].valid = false;
    }
    pending_count = 0;
}
```

**设计要点**：只重置 `buses_new` 和 `pending_count`，不碰 `buses`（旧状态）。上周期广播的结果保留在 `buses` 中供本周期各模块读取。

---

### 3.3 `const CDBValue& get_old(int slot)`

**调用时机**：任何模块的 `execute()` 中读取旧状态时

**行为**：返回 `buses[slot]` 的 const 引用（旧状态）。

```cpp
const CDBValue& get_old(int slot) const {
    return buses[slot];
}
```

**使用者**：
- `RS::listen_cdb()` — 遍历 CDB 所有槽位，捕获匹配 Qj/Qk 的值
- `ROB::listen_cdb()` — 接收结果写入对应 ROB 条目
- `RF::execute()` — （可选）若设计为 CDB 直写寄存器文件

---

### 3.4 `bool has_data(int slot)`

**调用时机**：读取方判断某槽位是否有有效数据

**行为**：返回 `buses[slot].valid`（旧状态）。

```cpp
bool has_data(int slot) const {
    return buses[slot].valid;
}
```

---

### 3.5 `bool broadcast(uint8_t rob_id, uint32_t value, int priority)`

**调用时机**：RS 或 LSB 的 `execute()` 中，功能单元产生结果后

**行为**：向 CDB 提交一个广播请求。请求暂存在 `pending` 数组中（不直接写入 `buses_new`），等待 `execute()` 阶段仲裁。

**参数**：
| 参数 | 说明 |
|------|------|
| `rob_id` | 产生结果的 ROB 条目编号（消费者用此 id 匹配） |
| `value` | 计算结果或加载数据 |
| `priority` | 优先级（0=branch, 1=load, 2=ALU, 3=MUL） |

**返回值**：`true` 表示请求已接受，`false` 表示请求队列已满（极罕见，stall 信号）。

```cpp
bool broadcast(uint8_t rob_id, uint32_t value, int priority) {
    if (pending_count >= CDB_COUNT + 2) return false;
    pending[pending_count].rob_id   = rob_id;
    pending[pending_count].value    = value;
    pending[pending_count].priority = priority;
    pending_count++;
    return true;
}
```

**调用者示例**（RS::execute 中）：
```cpp
// ALU 计算完毕
uint32_t result = compute_result(idx);
cdb.broadcast(rs_new[idx].rob_id, result, /*priority=*/2);
```

---

### 3.6 `void execute()`

**调用时机**：每个时钟周期中（step() 第 6 步），在所有生产者（RS, LSB）的 `execute()` 之后

**行为**：对 `pending` 中的所有广播请求按优先级排序，选出至多 `CDB_COUNT` 个，写入 `buses_new`。

**仲裁算法**：
1. 按 `priority` 升序排序（数值越小优先级越高）
2. 同级优先级按 `rob_id` 升序（确定性保证，避免同一周期的非确定性）
3. 取前 `CDB_COUNT` 个请求
4. 填入 `buses_new` 的对应槽位

```cpp
void execute() {
    // 按优先级排序 (bubble sort, pending 数量很小)
    for (int i = 0; i < pending_count; i++) {
        for (int j = i + 1; j < pending_count; j++) {
            if (pending[j].priority < pending[i].priority ||
                (pending[j].priority == pending[i].priority && 
                 pending[j].rob_id < pending[i].rob_id)) {
                BroadcastRequest tmp = pending[i];
                pending[i] = pending[j];
                pending[j] = tmp;
            }
        }
    }
    
    // 取前 CDB_COUNT 个
    int count = (pending_count < CDB_COUNT) ? pending_count : CDB_COUNT;
    for (int i = 0; i < count; i++) {
        buses_new[i].valid  = true;
        buses_new[i].rob_id = pending[i].rob_id;
        buses_new[i].value  = pending[i].value;
    }
    // 剩余槽位保持 clear() 设置的 valid=false
}
```

**优先级定义**：

| 优先级 | 值 | 来源 | 理由 |
|--------|-----|------|------|
| 0 (最高) | Branch | RS (FU_BRANCH) | 尽快检测分支预测错误，减少 misprediction 损失 |
| 1 | Load | LSB | 加载结果通常是关键路径，尽快解除依赖 |
| 2 | ALU | RS (FU_ALU) | 普通运算 |
| 3 (最低) | MUL | RS (FU_MUL) | 乘法指令不常见 |

---

### 3.7 `void update()`

**调用时机**：每个时钟周期末尾（step() 第 9 步最后）

**行为**：将 `buses_new` 拷贝到 `buses`，完成状态更新。

```cpp
void update() {
    for (int i = 0; i < CDB_COUNT; i++) {
        buses[i] = buses_new[i];
    }
}
```

---

## 4. 一个完整周期中的数据流

```
┌─────────────────────────────────────────────────────────┐
│  Step 1: cdb.clear()                                    │
│    buses_new[0..1].valid = false; pending_count = 0     │
│    buses[0..1] 保留上周期广播结果（旧状态）               │
├─────────────────────────────────────────────────────────┤
│  Step 2–3: rob.execute(), lsb.execute()                 │
│    ROB 读取 cdb.buses (旧) 捕获结果                       │
│    LSB 可能读取 cdb.buses (旧) 用于地址解析               │
│    LSB 产生 load 结果时调用 cdb.broadcast()                │
├─────────────────────────────────────────────────────────┤
│  Step 4: rs.execute()                                   │
│    ① 遍历 cdb.buses (旧) → listen_cdb() 捕获操作数        │
│    ② 就绪条目计算 → compute_result()                     │
│    ③ 调用 cdb.broadcast(rob_id, result, priority)        │
├─────────────────────────────────────────────────────────┤
│  Step 5: rf.execute()                                   │
│    读取 cdb.buses (旧)，可选地直写寄存器                    │
├─────────────────────────────────────────────────────────┤
│  Step 6: cdb.execute()                                  │
│    对 pending[] 排序仲裁，选出 CDB_COUNT 个写入 buses_new  │
├─────────────────────────────────────────────────────────┤
│  Step 9: cdb.update()                                   │
│    buses ← buses_new  (下周期生效)                        │
└─────────────────────────────────────────────────────────┘
```

**关键时序说明**：
- RS 和 LSB 在 step 3–4 中产生的广播请求，在 step 6 才真正写入 `buses_new`
- 其他模块在 step 6 **之前**读取 CDB 旧状态（`buses`），读到的是**上周期**的广播结果
- 本周期新产生的结果在 step 9 `update()` 之后（即下一周期）才能被其他模块读取
- 这引入了 1 个周期的 CDB 广播延迟——这是模拟器架构的固有特性，对正确性无影响，仅影响时钟周期数的统计

---

## 5. 与其它模块的接口

```
┌──────────────────────────────────────────────────────┐
│                       CDB                            │
│                                                      │
│  broadcast() ◄── RS::execute()   (ALU/BR/MUL result) │
│  broadcast() ◄── LSB::execute()  (load result)       │
│                                                      │
│  get_old()   ──► RS::listen_cdb() (operand capture)  │
│  has_data()  ──► RS::listen_cdb()                    │
│  get_old()   ──► ROB::listen_cdb() (result capture)  │
│  has_data()  ──► ROB::listen_cdb()                   │
│  get_old()   ──► RF::execute()    (optional)         │
└──────────────────────────────────────────────────────┘
```

---

## 6. 头文件骨架

```cpp
#ifndef CDB_HPP
#define CDB_HPP

#include "types.hpp"

class CDB {
private:
    CDBValue buses[CDB_COUNT];        // 旧状态
    CDBValue buses_new[CDB_COUNT];    // 新状态

    struct BroadcastRequest {
        uint8_t  rob_id;
        uint32_t value;
        int      priority;
    };
    BroadcastRequest pending[CDB_COUNT + 2];
    int pending_count;

public:
    void init();

    // 每周期方法
    void clear();
    void execute();
    void update();

    // 被生产者调用 (RS, LSB)
    bool broadcast(uint8_t rob_id, uint32_t value, int priority);

    // 被消费者调用 (RS, ROB, RF)
    const CDBValue& get_old(int slot) const;
    bool            has_data(int slot) const;
};

#endif
```

---

## 7. 设计决策与备选方案

### 7.1 为什么用 `broadcast() + execute()` 两步而非一步直写？

**一步直写**（RS 直接写 `buses_new`）的问题是：
- 多个 RS 条目同时就绪，结果数 > CDB_COUNT 时无法仲裁
- 需要在 RS 内部做仲裁，职责不清

**两步方式**（先收集请求，再仲裁写入）的优势：
- 仲裁逻辑集中在 CDB，符合单一职责
- RS 只需调用 `broadcast()`，不管槽位分配
- 和 `request.md` 中 `cdb.execute(rs, lsb)` 的语义一致

### 7.2 为什么 `execute()` 不直接读 RS/LSB 内部状态？

`request.md` 描述的 `cdb.execute(rs, lsb)` 是一种 pull 模型（CDB 主动收集结果）。本设计采用 push+pull 混合模型：
- Push：RS/LSB 调用 `broadcast()` 提交请求
- Pull：`execute()` 从 `pending[]` 中仲裁出最终广播

这样 CDB 不需要知道 RS/LSB 内部数据结构，模块耦合更低。

如果你想严格遵循 `cdb.execute(rs, lsb)` 的 pull 语义，可在 `execute()` 中添加：
```cpp
void execute(ReservationStations& rs, LoadStoreBuffer& lsb) {
    // Pull from RS
    for each FU type:
        if (rs.has_result(fu)) {
            broadcast(rs.get_rob_id(fu), rs.get_result(fu), fu_priority(fu));
        }
    // Pull from LSB
    for each completed load:
        broadcast(lsb.get_rob_id(i), lsb.get_result(i), 1);
    // Then arbitrate as usual
    arbitrate_and_fill();
}
```
这作为备选实现，两种方式功能等价。

---

## 8. 测试策略

1. **单元测试**：模拟 RS/ROB 监听者，验证 `broadcast` + `execute` + `get_old` 的时序正确性（旧状态在下周期才可见）
2. **仲裁测试**：同时提交 3+ 个不同优先级的广播请求，验证只有 `CDB_COUNT` 个通过，且按优先级排序
3. **集成测试**：在完整 Tomasulo 流程中，验证 RS 通过 CDB 捕获操作数的正确性
