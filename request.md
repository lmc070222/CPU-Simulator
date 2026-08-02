# RISC-V Tomasulo CPU Simulator — 总体设计框架

## 项目概述

用 C++ 模拟一个采用 Tomasulo 架构的 **RV32I RISC-V 乱序执行 CPU**，通过所有下发测试数据。

**核心约束**：
- **模拟硬件时序逻辑**：每个模块存储新旧两套状态，`execute()` 读取各模块旧状态计算自身新状态，`update()` 将新状态覆盖旧状态。
- **模块顺序可任意交换**：各模块的 `execute()` / `update()` 执行顺序不影响正确性（模拟并行硬件）。
- **禁止使用**：大多数 STL 容器、指针/引用、动态内存分配、全局变量。全部使用静态数组。
- **访存延迟**：数据内存访问不能立即使用内存瞬时值，需模拟硬件延迟返回。
- **x0 恒为 0**：每时钟周期重置为 0。
- **终止条件**：读到 `0x0ff00513`（`li a0, 255`）时，不执行该指令，输出 `a0 (x10)` 低 8 位并停止。

---

## 文件结构与各类/函数设计

### 文件清单

```
src/
├── main.cpp                    入口 & 命令行解析
├── types.hpp                   公共类型 / 枚举 / 常量 / 结构体
├── memory.hpp / memory.cpp     内存子系统（指令内存 + 数据内存）
├── decoder.hpp / decoder.cpp   指令译码器
├── register_file.hpp / register_file.cpp  寄存器文件 + 重命名映射表
├── reservation_station.hpp / reservation_station.cpp  保留站
├── load_store_buffer.hpp / load_store_buffer.cpp      加载/存储缓冲
├── reorder_buffer.hpp / reorder_buffer.cpp            重排序缓冲 (ROB)
├── cdb.hpp / cdb.cpp           公共数据总线 (CDB)
├── branch_predictor.hpp / branch_predictor.cpp        分支预测器
├── cpu.hpp / cpu.cpp           顶层 CPU 控制器（Tomasulo 流程主循环）
└── common.hpp / common.cpp     工具函数（符号扩展、位操作等）
```

> 注：现有的 `cpu.hpp`（含 `load_memory` 和 `fetch`）作为起点参考，最终整合进上述文件。

---

### 1. `types.hpp` — 公共类型定义

**常量**：
| 名称 | 含义 |
|------|------|
| `REG_COUNT = 32` | 寄存器个数 |
| `MEM_SIZE = 1 << 20` | 内存大小 (1 MiB) |
| `RS_ALU_COUNT = 4` | ALU 保留站数量 |
| `RS_MUL_COUNT = 2` | 乘除保留站数量（bonus） |
| `RS_LS_COUNT = 4` | 访存保留站数量 |
| `ROB_SIZE = 16` | ROB 条目数 |
| `LSB_SIZE = 8` | Load/Store Buffer 条目数 |
| `CDB_COUNT = 2` | CDB 总线数量（可同时广播的结果数） |

**枚举 `Opcode`**：RV32I 各指令 opcode（`LUI`, `AUIPC`, `JAL`, `JALR`, `BRANCH`, `LOAD`, `STORE`, `OP_IMM`, `OP`, `SYSTEM` 等）

**枚举 `FUType`**：功能单元类型（`FU_ALU`, `FU_MUL`, `FU_LOAD`, `FU_STORE`, `FU_BRANCH`）

**枚举 `InstType`**：指令类型分类
- `R_TYPE`, `I_TYPE`, `S_TYPE`, `B_TYPE`, `U_TYPE`, `J_TYPE`

**枚举 `Funct3` / `Funct7`**：用于指令译码的 funct 值集合

**结构体 `DecodedInst`**：
```cpp
struct DecodedInst {
  Opcode opcode;
  uint32_t pc;
  uint8_t rd, rs1, rs2;
  uint32_t imm;
  InstType type;
  FUType fu_type;
  ALUOp alu_op;      // ADD/SUB/SLL/XOR/...
  bool is_branch;
  bool is_mem_read;
  bool is_mem_write;
  bool is_halt;       // true for 0x0ff00513
};
```

**状态宏/结构体 `CDBValue`**：
```cpp
struct CDBValue {
  bool   valid;
  uint8_t rob_id;      // 产生此结果的 ROB 条目
  uint32_t value;       // 计算结果或加载数据
};
```

---

### 2. `memory.hpp` / `memory.cpp` — 内存子系统

**类 `Memory`**

| 成员 | 说明 |
|------|------|
| `uint8_t inst_mem[MEM_SIZE]` | 指令内存（旧状态） |
| `uint8_t inst_mem_new[MEM_SIZE]` | 指令内存（新状态） |
| `uint8_t data_mem[MEM_SIZE]` | 数据内存（旧状态） |
| `uint8_t data_mem_new[MEM_SIZE]` | 数据内存（新状态） |

| 方法 | 说明 |
|------|------|
| `load(const char* filename)` | 解析 `.data` 文件，写入 `inst_mem` 和 `data_mem` 的旧状态 |
| `uint32_t fetch_inst(uint32_t pc)` | 从 `inst_mem` 取 4 字节拼成一条指令 |
| `uint8_t read_byte(uint32_t addr)` | 读 `data_mem` 旧状态 1 字节 |
| `uint16_t read_half(uint32_t addr)` | 读 `data_mem` 旧状态 2 字节 |
| `uint32_t read_word(uint32_t addr)` | 读 `data_mem` 旧状态 4 字节 |
| `write_byte(uint32_t addr, uint8_t v)` | 写 `data_mem_new` 1 字节 |
| `write_half(uint32_t addr, uint16_t v)` | 写 `data_mem_new` 2 字节 |
| `write_word(uint32_t addr, uint32_t v)` | 写 `data_mem_new` 4 字节 |
| `execute(...)` | 处理本周期到期的 LS 写请求（从 `LoadStoreBuffer` 读旧状态，更新 `data_mem_new`） |
| `update()` | `data_mem = data_mem_new`（指令内存无需每周期更新） |

---

### 3. `decoder.hpp` / `decoder.cpp` — 指令译码器

**类 `Decoder`**

| 方法 | 说明 |
|------|------|
| `DecodedInst decode(uint32_t inst, uint32_t pc)` | 将 32-bit 原始机器码译码为 `DecodedInst` |

**内部辅助函数**：
| 函数 | 说明 |
|------|------|
| `uint32_t sign_ext(uint32_t val, int bits)` | 符号扩展 |
| `Opcode get_opcode(uint32_t inst)` | 提取 opcode（低 7 位） |
| `uint8_t get_rd(uint32_t inst)` | 提取 rd（7:11） |
| `uint8_t get_rs1(uint32_t inst)` | 提取 rs1（15:19） |
| `uint8_t get_rs2(uint32_t inst)` | 提取 rs2（20:24） |
| `uint32_t imm_I(uint32_t inst)` | 提取 I-type 立即数 |
| `uint32_t imm_S(uint32_t inst)` | 提取 S-type 立即数 |
| `uint32_t imm_B(uint32_t inst)` | 提取 B-type 立即数 |
| `uint32_t imm_U(uint32_t inst)` | 提取 U-type 立即数 |
| `uint32_t imm_J(uint32_t inst)` | 提取 J-type 立即数 |

---

### 4. `register_file.hpp` / `register_file.cpp` — 寄存器文件 + 重命名

**类 `RegisterFile`**

| 成员 | 说明 |
|------|------|
| `uint32_t reg[REG_COUNT]` | 寄存器值（旧状态） |
| `uint32_t reg_new[REG_COUNT]` | 寄存器值（新状态） |
| `int32_t reorder_id[REG_COUNT]` | 重命名映射：`reg[i]` 将由哪个 ROB 条目写入（-1 表示已就绪）（旧状态） |
| `int32_t reorder_id_new[REG_COUNT]` | 同上（新状态） |
| `bool busy[REG_COUNT]` | 是否被某条未提交指令占用（旧状态） |
| `bool busy_new[REG_COUNT]` | 同上（新状态） |

| 方法 | 说明 |
|------|------|
| `uint32_t read(uint8_t idx)` | 读旧状态寄存器值 |
| `int32_t get_reorder(uint8_t idx)` | 读旧状态重命名映射 |
| `bool is_busy(uint8_t idx)` | 读旧状态 busy |
| `void set_reorder(uint8_t idx, int32_t rob_id)` | 写新状态重命名映射（issue 阶段） |
| `void write(uint8_t idx, uint32_t val, int32_t rob_id)` | commit 时写寄存器 + 清除重命名 |
| `void execute(...)` | （可选）处理 x0 重置等 |
| `update()` | `reg_new → reg`, `reorder_id_new → reorder_id`, `busy_new → busy` |

---

### 5. `reservation_station.hpp` / `reservation_station.cpp` — 保留站

**结构体 `RSEntry`**（每个保留站条目）：
```cpp
struct RSEntry {
  bool busy;
  FUType fu;
  ALUOp alu_op;
  uint32_t Vj, Vk;       // 源操作数值
  int32_t Qj, Qk;        // 产生源操作数的 ROB id（-1 表示已就绪）
  uint32_t imm;
  uint8_t dest;           // 目标寄存器（用于 CDB 广播）
  uint8_t rob_id;         // 对应的 ROB 条目 id
  uint32_t pc;            // 指令地址
  bool mem_read, mem_write;
  bool is_branch;
  bool predicted_taken;
  uint32_t branch_target; // 分支预测目标地址
};
```

**类 `ReservationStations`**

| 成员 | 说明 |
|------|------|
| `RSEntry rs[RS_TOTAL]` | 所有保留站条目（旧状态） |
| `RSEntry rs_new[RS_TOTAL]` | 所有保留站条目（新状态） |

| 方法 | 说明 |
|------|------|
| `int allocate(FUType fu)` | 分配一个对应类型空闲的 RS，返回 index（-1 表示满） |
| `void issue(int idx, const DecodedInst& inst, int rob_id, const RegisterFile& rf)` | 将译码后指令写入 RS（新状态），读取寄存器文件的旧状态填充 V/Q |
| `bool is_ready(int idx)` | 检查 RS[idx] 两操作数是否都就绪（Qj==Qk==-1） |
| `RSEntry& get_old(int idx)` | 获取旧状态引用 |
| `void mark_done(int idx, uint32_t result)` | 标记 RS 完成（write result 后清除 busy） |
| `void listen_cdb(const CDBValue& cdb)` | 监听 CDB：若 Qj/Qk 匹配就捕获值，标记对应 Q 为 -1 |
| `execute(...)` | 选出就绪的 RS 条目送入功能单元执行（旧状态为输入，新状态记录哪些被发射执行） |
| `update()` | `rs_new → rs` |

---

### 6. `load_store_buffer.hpp` / `load_store_buffer.cpp` — 访存缓冲

**结构体 `LSBEntry`**：
```cpp
struct LSBEntry {
  bool busy;
  bool is_load;         // true=load, false=store
  uint32_t addr;        // 内存地址
  uint32_t value;       // store 时是要写入的值
  uint32_t delay;       // 硬件延迟倒计时（load 从发起后每个周期递减）
  uint8_t rob_id;
  uint8_t size;         // 1=byte, 2=half, 4=word
  bool sign_ext;        // 是否符号扩展（lb/lh vs lbu/lhu）
  bool addr_ready;      // 地址是否计算完成
  bool value_ready;     // store 时数据是否就绪
};
```

**类 `LoadStoreBuffer`**

| 成员 | 说明 |
|------|------|
| `LSBEntry buffer[LSB_SIZE]` | LSB 条目（旧状态） |
| `LSBEntry buffer_new[LSB_SIZE]` | LSB 条目（新状态） |
| `int32_t store_queue[LSB_SIZE]` | Store 队列（维护 store 顺序，用于 store-load forwarding 检测） |
| `int32_t store_queue_new[LSB_SIZE]` | 同上（新状态） |

| 方法 | 说明 |
|------|------|
| `int allocate()` | 分配一个空闲 LSB 条目 |
| `void issue_load(int idx, uint32_t addr, uint8_t rob_id, uint8_t size, bool sign_ext)` | 发起 load 请求 |
| `void issue_store(int idx, uint32_t addr, uint32_t val, uint8_t rob_id, uint8_t size)` | 发起 store 请求 |
| `void execute(...)` | 递减各 load 的 delay；delay==0 时准备好结果；store 在 commit 时才真正写入内存 |
| `void update()` | `buffer_new → buffer` |
| `bool can_forward(uint32_t addr, uint8_t rob_id)` | 检查是否有 pending store 可以 forward 给同地址的 load |
| `CDBValue get_load_result(int idx)` | 返回已完成的 load 结果 |

---

### 7. `reorder_buffer.hpp` / `reorder_buffer.cpp` — 重排序缓冲

**结构体 `ROBEntry`**：
```cpp
struct ROBEntry {
  bool busy;
  uint32_t pc;
  DecodedInst inst;
  uint8_t dest_reg;      // 目标寄存器
  uint32_t value;        // 执行结果（未就绪时为 0）
  bool ready;            // 结果是否已产生
  bool is_branch;
  bool branch_taken;
  uint32_t branch_target;
  bool mispredicted;     // 分支预测错误
};
```

**类 `ReorderBuffer`**

| 成员 | 说明 |
|------|------|
| `ROBEntry buffer[ROB_SIZE]` | ROB 循环缓冲（旧状态） |
| `ROBEntry buffer_new[ROB_SIZE]` | ROB 循环缓冲（新状态） |
| `int head, tail` | 头尾指针（旧） |
| `int head_new, tail_new` | 头尾指针（新） |
| `int count` | 当前条目数（旧） |

| 方法 | 说明 |
|------|------|
| `int allocate(const DecodedInst& inst, uint32_t pc)` | 分配新 ROB 条目，返回 rob_id |
| `bool full()` | ROB 是否满 |
| `bool empty()` | ROB 是否空 |
| `ROBEntry& head_entry()` | 获取队头条目 |
| `void mark_ready(int rob_id, uint32_t value)` | 标记某条目的结果已就绪 |
| `int commit(RegisterFile& rf, Memory& mem, int& halt_code)` | 尝试 commit 队头：若 ready 则写回；遇到 `mispredicted` 则 flush；遇到 halt 则记录返回值 |
| `void flush_after(int rob_id)` | 分支预测错误时，flush ROB 中 rob_id 之后的条目 |
| `void listen_cdb(const CDBValue& cdb)` | CDB 广播结果写入对应 ROB 条目 |
| `execute(...)` | 尝试 commit |
| `update()` | `buffer_new → buffer`, `head_new→head`, `tail_new→tail` |

---

### 8. `cdb.hpp` / `cdb.cpp` — 公共数据总线

**类 `CDB`**

| 成员 | 说明 |
|------|------|
| `CDBValue buses[CDB_COUNT]` | CDB 总线（旧状态） |
| `CDBValue buses_new[CDB_COUNT]` | CDB 总线（新状态） |

| 方法 | 说明 |
|------|------|
| `void broadcast(int slot, uint8_t rob_id, uint32_t value)` | 往某条总线写入广播值（新状态） |
| `const CDBValue& get_old(int slot)` | 读取旧状态某条总线值 |
| `bool has_data(int slot)` | 该总线上是否有有效数据 |
| `void execute(...)` | 收集各功能单元的计算结果到 CDB 总线（新状态） |
| `void update()` | `buses_new → buses` |
| `void clear()` | 每周期开始时清空新状态总线 |

---

### 9. `branch_predictor.hpp` / `branch_predictor.cpp` — 分支预测器

**类 `BranchPredictor`**

默认实现 **2-bit 饱和计数器**（BHT: Branch History Table），可直接统计预测准确率。

| 成员 | 说明 |
|------|------|
| `uint8_t bht[BHT_SIZE]` | 分支历史表，每项 2-bit（旧状态） |
| `uint8_t bht_new[BHT_SIZE]` | 分支历史表（新状态） |
| `uint32_t predict_count` | 预测总次数（旧） |
| `uint32_t predict_correct` | 预测正确次数（旧） |
| `uint32_t predict_count_new` | （新） |
| `uint32_t predict_correct_new` | （新） |

| 方法 | 说明 |
|------|------|
| `bool predict(uint32_t pc)` | 根据 PC 查 BHT，返回是否预测跳转 |
| `void update(uint32_t pc, bool actually_taken)` | 用实际分支结果更新 BHT 和计数器 |
| `float accuracy()` | 返回预测准确率 |
| `execute(...)` | （可选） |
| `update()` | `bht_new→bht`, 计数器更新 |

> **Bonus 方向**：可替换为 GShare / Tournament / TAGE 等高级分支预测器，只需实现相同接口。

---

### 10. `cpu.hpp` / `cpu.cpp` — 顶层 CPU

**类 `CPU`**

| 成员 | 说明 |
|------|------|
| `Memory mem` | 内存模块 |
| `Decoder decoder` | 译码器 |
| `RegisterFile rf` | 寄存器文件 |
| `ReservationStations rs` | 保留站 |
| `LoadStoreBuffer lsb` | 访存缓冲 |
| `ReorderBuffer rob` | 重排序缓冲 |
| `CDB cdb` | 公共数据总线 |
| `BranchPredictor bp` | 分支预测器 |
| `uint32_t pc`, `uint32_t pc_new` | 程序计数器 |
| `uint64_t cycle` | 当前时钟周期 |
| `bool stalled` | 流水线是否阻塞 |

| 方法 | 说明 |
|------|------|
| `void init(const char* filename)` | 初始化：加载 `.data` 到内存，重置所有模块 |
| `int run()` | 主循环：反复执行 `step()` 直到 halt |
| `void step()` | 单周期执行流程（见下方） |
| `float branch_accuracy()` | 返回分支预测准确率 |
| `uint64_t clock_cycle()` | 返回时钟周期数 |

#### `step()` 单周期执行流程

```text
1. cdb.clear()                         -- 清空 CDB 新状态
2. rob.execute(mem, rf, cdb, bp, rs, lsb)  -- 尝试 commit / flush
3. lsb.execute(mem, cdb)                   -- 访存延迟递减、完成 load
4. rs.execute(cdb, lsb)                    -- 检查就绪条目，产生 CDB 结果
5. rf.execute(cdb)                         -- x0 重置等
6. cdb.execute(rs, lsb)                    -- 收集功能单元结果到总线
7. bp.execute(rob)                         -- 更新分支预测器
8. -- Decode + Issue --
   若 RS/ROB 有空位 && pc 非 halt:
     inst = mem.fetch_inst(pc)
     decoded = decoder.decode(inst, pc)
     若 decoded.is_halt: 停止取指，pc_new = pc
     否则:
       若 decoded.is_branch:
         predicted = bp.predict(pc)
         若 predicted: pc_new = pc + imm
         否则: pc_new = pc + 4
       rob_id = rob.allocate(decoded, pc)
       rf.set_reorder(decoded.rd, rob_id)
       若 decoded 是 load/store:
         lsb_idx = lsb.allocate()
         rs 暂存地址计算信息，等待地址就绪后送入 lsb
       否则:
         rs_idx = rs.allocate(decoded.fu_type)
         rs.issue(rs_idx, decoded, rob_id, rf)
   否则: pc_new = pc (stall)

9. 所有模块执行 update()
   mem.update()
   rf.update()
   rs.update()
   lsb.update()
   rob.update()
   cdb.update()
   bp.update()
   pc = pc_new
   cycle++
```

> **模块顺序可交换**：1–8 步中，只要 ensure `execute()` 只读取旧状态、只写自身新状态，任意顺序均正确。

---

### 11. `common.hpp` / `common.cpp` — 工具函数

| 函数 | 说明 |
|------|------|
| `uint32_t sign_extend(uint32_t val, int bits)` | 符号扩展 |
| `bool is_little_endian()` | 确认小端序（RISC-V 为小端） |

---

### 12. `main.cpp` — 入口

| 函数 | 说明 |
|------|------|
| `int main(int argc, char* argv[])` | 解析命令行参数（`.data` 文件路径），初始化 CPU，运行，输出返回值、周期数、分支预测准确率 |

**输出格式示例**：
```
Return value: 94
Clock cycles: 1234
Branch prediction accuracy: 87.50%
```

---

## 总体数据流

```
                         +-----------+
  .data 文件 ----------> |  Memory   | <--------+
                         +-----+-----+          |
                               |                 |
                          uint32_t               |
                               |                 |
                         +-----v------+          |
  PC ------------------->|   Decoder  |          |
                         +-----+------+          |
                               |                 |
                         DecodedInst             |
                               |                 |
                +--------------+-----------+     |
                |              |           |     |
           +----v----+  +-----v-----+ +---v-----+------+
           |   RS    |  |    LSB    | | RegisterFile   |
           |(ALU/BR) |  |(Load/Store)| |   (rename)    |
           +----+----+  +-----+-----+ +---+------+-----+
                |              |           |      |
                v              v           v      v
           +----+----+  +-----+-----+   ROB <--+  (commit)
           |   CDB   |<-+  Memory   |    |
           +----+----+  +-----------+    |
                |                        |
                +------- broadcast ------+
                         |
                +--------v--------+
                | BranchPredictor |
                +-----------------+
```

---

## RV32I 指令覆盖清单

| 类型 | 指令 |
|------|------|
| **R-type** | `ADD`, `SUB`, `SLL`, `SLT`, `SLTU`, `XOR`, `SRL`, `SRA`, `OR`, `AND` |
| **I-type (ALU)** | `ADDI`, `SLTI`, `SLTIU`, `XORI`, `ORI`, `ANDI`, `SLLI`, `SRLI`, `SRAI` |
| **I-type (Load)** | `LB`, `LH`, `LW`, `LBU`, `LHU` |
| **I-type (JALR)** | `JALR` |
| **S-type** | `SB`, `SH`, `SW` |
| **B-type** | `BEQ`, `BNE`, `BLT`, `BGE`, `BLTU`, `BGEU` |
| **U-type** | `LUI`, `AUIPC` |
| **J-type** | `JAL` |
| **Pseudo（自动映射）** | `NOP`→`ADDI x0,x0,0`, `LI`→`ADDI rd,x0,imm`, `MV`→`ADDI rd,rs1,0`, `J`→`JAL x0,offset`, `RET`→`JALR x0,ra,0`, `BNEZ`→`BNE rs,x0`, `BEQZ`→`BEQ rs,x0`, `BLEZ`→`BGE x0,rs`, `BGTZ`→`BLT x0,rs` |
| **排除** | `ECALL`, `EBREAK`（不实现）|

---

## 参考架构

基于 CAAQA §3.4–§3.6 的 Tomasulo 参考架构，结合 RISC-V RV32I 规范：

- **保留站** 对应各功能单元的 reservation stations
- **ROB** 保证 in-order commit，同时支持 precise exception / branch misprediction recovery
- **寄存器重命名** 通过 ROB id 实现（`reorder_id[]`）
- **Store 延迟 commit**：store 指令在 commit 前不写入 memory，避免 side effect 在 misprediction 后回滚困难
- **Load forwarding**：pending store → same-address load 需 forward 数据

---

## 测试与验证

1. **先写 Naïve Interpreter**（单周期顺序执行）：验证译码 + 指令语义正确性
2. **对拍**：Tomasulo 每 commit 一条指令后，比对寄存器状态与 naïve interpreter 一致
3. **数据测试**：依次通过 `data/sample` 和 `data/testcases/*.data` 所有测试点
