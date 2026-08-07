# Code Review Questions — RISC-V Tomasulo CPU Simulator

---

## 一、架构与设计结构

1. `cpu.hpp` 中所有模块（Memory、RegisterFile、ReservationStations、ReorderBuffer、CDB、BranchPredictor）均作为 `private` 成员直接嵌入 CPU 类，没有使用依赖注入或接口抽象。这种紧耦合设计对可测试性和模块替换有何影响？

2. 整个模拟器采用 header-only 的实现方式（所有逻辑都在 `.hpp` 中），没有 `.cpp` 实现文件。这种设计的优缺点是什么？在大型项目中是否合适？

3. `step()` 方法中模块的执行顺序为 rob → rs → rf → cdb → bp → fetch → update。你是否能证明这个顺序是唯一正确的？如果打乱顺序（如先执行 rs 再执行 rob），结果和周期数是否仍然一致？

4. 各模块的 `execute()` + `update()` 双缓冲模式（`xxx` / `xxx_new`）是否严格保证了模块间的数据隔离？是否存在某个模块读取了另一个模块本周期写入的 `_new` 状态？

5. CPU 类的 `run()` 方法硬编码了 `cycle < 200000000` 作为超时上限。如果程序确实需要超过 2 亿周期，会发生什么？这个超时是否应该由调用者控制？

6. `init(filename)` 和 `init_stdin()` 存在大量重复代码（rf.init, rs.init, rob.init, cdb.init, bp.init, pc=0, halt_fetch=false, halted=false, cycle=0）。应该如何重构以消除重复？

7. CPU 类没有析构函数、拷贝构造函数或赋值运算符。由于所有成员都是值类型，编译器生成的默认版本是否足够？是否存在潜在的资源管理问题？

8. 项目使用 `CMakeLists.txt` 管理构建，但所有依赖头文件通过 `include_directories` 全局引入，而非按 target 引入。这会带来什么风险？

---

## 二、类型与常量定义（types.hpp）

9. `ROB_SIZE = 16` 的选择依据是什么？这个值与保留站数量（ALUx4 + MULx2 + LSx4 = 10）之间的关系如何影响 ILP？

10. `CDB_COUNT = 4`，但 dispatch 循环中 `dispatched < CDB_COUNT` 实际控制了每周期广播数量。为什么是 4？commit_head 中的 `cdb.broadcast` 是否会与 RS dispatch 竞争 CDB 槽位？

11. `RS_LS_COUNT = 4` 的注释缺失。LS 保留站同时服务于 Load 和 Store，两者的分配是共享还是分区？如果不是分区，是否会互相抢占？

12. `BHT_SIZE = 64` 的选择依据是什么？这个大小对应多少条分支指令的高效预测？是否会与测试程序中的分支数量匹配？

13. `ROBEntry` 中 `rob_id` 使用 `uint8_t`（最大 255），但 ROB 只有 16 个条目。为什么不用更小的类型？CDB 广播的 `rob_id` 用 `uint8_t` 与 RS 条目的 `rob_id` 用 `uint8_t` 是否一致？

14. `DecodedInst` 中 `is_halt` 字段被设定但解码器以 `raw == 0x0ff00513` 来判断停机，`is_halt` 字段似乎并未在解码器中被赋值。这是遗留代码还是有意设计？

15. `FUType` 枚举中为什么 ALU 和 BRANCH 共享保留站？这个设计决策在哪些情况下可能导致结构性冒险？

16. 为什么 `ALUOp` 中需要 `ALU_COPY` 和 `ALU_NOP`？它们在哪些指令的译码中使用？

---

## 三、内存系统（memory.hpp）

17. `Memory` 类中 `mem` 数组从原来的 `uint8_t mem[MEM_SIZE]`（栈上）改为了 `std::vector<uint8_t>`（堆上）。这个修改解决了什么问题？是否引入了额外的性能开销？

18. `Memory::read()` 和 `Memory::write()` 使用 `addr &= (MEM_SIZE - 1)` 来绑定地址范围。这实际上是静默地让越界访问回绕到 0，而不是报告错误。这种"静默"行为在生产环境中是否可取？

19. `Memory::load()` 和 `Memory::load_stdin()` 是拷贝粘贴的代码，只有一个地方不同（`std::ifstream` vs `std::cin`）。如何重构以避免重复？

20. `Memory::fetch_inst()` 没有进行 `addr &= (MEM_SIZE - 1)` 的边界检查，而 `read()` 和 `write()` 有。这种不一致是故意的还是疏忽？

21. 数据文件解析中 `byte_str.size() == 2` 检查假设每个字节恰好是 2 个十六进制字符。如果数据文件中有注释、空格或格式错误，会发生什么？

22. `Memory` 的 `data()` 方法返回内部 buffer 的裸指针，这破坏了封装性。调用者可以任意修改内存内容，是否应该删除或改为返回 `const` 指针？

23. 内存初始化的 `memset(mem.data(), 0, MEM_SIZE)` 在 `std::vector` 构造函数已经零初始化后调用是冗余的。这是否可以移除？

---

## 四、指令解码器（decoder.hpp）

24. `decode()` 函数使用 `static` 关键字修饰，返回一个 `DecodedInst` 值对象。每次调用都会产生拷贝开销。改为接受引用参数（`DecodedInst&`）是否更高效？

25. JALR 指令在解码时总是将 `is_branch` 设为 `true`，且 branch predictor 总是预测 "taken"（`predicted = true`）。在真实 RISC-V 中 JALR 可以用于非分支场景（如函数返回），这种无条件预测的策略是否合理？

26. BGE 指令（`funct3 == 5`）在分支解码中使用的 ALUOp 是 `ALU_SLT`，而 BGEU（`funct3 == 7`）使用 `ALU_SLTU`。看一下 `dispatch_one` 中的分支处理：`BLT || BLTU` 判断 `taken = (result != 0)`，`BGE || BGEU` 判断 `taken = (result == 0)`。这个逻辑是否正确？是否应该反过来？

27. SLTIU（`funct3 == 3`）的 I-type 解码中没有出现——它被放在 `else` 默认分支中。如果遇到未覆盖的 funct3 值（如 `funct3 == 3` 对应的 SLTIU），译码结果会是什么？

28. ALU I-type 的移位指令（SLLI/SRLI/SRAI）使用了 `funct7` 字段的一部分来区分。SRAI 使用了 `funct7 == 0x20` 而 SLLI/SRLI 使用 `funct7 == 0`。如果遇到 `funct7 == 0x20` 的 SLLI（非法指令），会被错误解码为 SRAI 吗？

29. R-type 解码中，MUL 指令使用 `funct7 == 1` 来区分。这与 RV32M 标准（funct7 == 0x01）一致。但 `funct3 == 0 && funct7 == 1` 这个判断在 ADD（`funct3 == 0 && funct7 == 0`）和 SUB（`funct3 == 0 && funct7 == 0x20`）之前，会不会优先匹配到 MUL？

30. `DecodedInst` 中的 `alu_op` 初始值设为 `ALU_NOP`。如果一条指令在解码过程中未被任何 case 覆盖（如未知 opcode），`alu_op` 保持 `ALU_NOP`，这会导致 ALU 返回 0。这是否可以视为安全默认？或者应该显式处理非法指令？

31. SYSTEM 类指令（ECALL/EBREAK）被解码但在 RS 和 ROB 中没有专门的处理逻辑。遇到这些指令时程序的行为是什么？

---

## 五、寄存器文件与 RAT（register.hpp）

32. `RegisterFile` 中的 `busy` 和 `busy_new` 字段实际上始终与 `reorder_id != -1` 同义。为什么维护了两个冗余的状态？是否可以直接用 `reorder_id != -1` 替代所有的 `busy` 检查？

33. `write()` 方法在 `reorder_id[idx] == rob_id` 时写入寄存器值，但如果 `reorder_id_new[idx] != rob_id`（意味着更年轻的指令已经覆盖了该寄存器的 RAT 映射），则不更新 `reg_new`。这是否会在后续的 `update()` 中导致 `reg[idx]` 丢失刚提交的正确值？

34. `restore_mapping()` 方法被声明但从未被调用。它的预期用途是什么？是用于分支预测失败后的 RAT 恢复吗？当前代码是如何处理 RAT 恢复的？

35. `execute()` 方法中只处理了 `x0` 寄存器。为什么需要把 `reg_new[0]` 显式置零？（提示：`x0` 永远为 0，但流水线中可能有指令向其写入非零值）

36. `flush_after()` 现在同时检查 `reorder_id[i]` 和 `reorder_id_new[i]`。为什么需要同时检查两个版本？如果只检查其中一个会漏掉什么情况？

37. RAT 的 `reorder_id` 使用 `int32_t`，其中 -1 表示"无挂起的写者"。rob_id 自身是 `uint8_t` 类型。当 ROB 满（16 条）时，rob_id 范围为 0~15，与 -1 不会冲突。但随着 ROB 大小增加（如 256），`int32_t` 能否安全地表示"无挂起"的语义？

---

## 六、保留站（reservation_station.hpp）

38. `RSEntry` 结构体有 23 个字段和 2 个默认成员初始化器，但没有构造函数。这种 POD 风格的结构体在使用中是否存在未初始化字段的风险？

39. `issue()` 方法中先设置 `Qj`/`Vj`，再设置 `Qk`/`Vk`，然后被 LUI/JAL/AUIPC 的特殊逻辑覆盖。这些覆盖操作的执行顺序是否可能产生错误的依赖关系？例如 JAL 修改 `Vj` 之后，后续的 `Vk` 设置会受影响吗？

40. `compute_result()` 使用 `rs_new[idx]` 而非 `rs[idx]` 来计算结果。当 dispatch 和 issue 在同周期内发生顺序变化时，这是否可能读到错误的操作数？

41. `is_ready()` 和 `find_ready()` 都读取 `rs_new`。引入 `just_issued` 标志后，在同周期内被发射的指令不会再被调度。这个机制是否完全消除了 issue→dispatch 的"0 周期转发"问题？

42. `listen_cdb()` 更新了 `rs_new` 中的操作数。但如果 `rs.execute()` 在 `rob.execute()` 之前执行，CDB 上是否可能还没有包含已经被提交指令的值？是否有机制能处理这种延迟？

43. `dispatch_one()` 中的 Load 转发逻辑包含两阶段：先查 `rob.forward_store()`，再扫描 RS 中更老的 store 指令。这两种转发方式是否覆盖了所有存在的 store-to-load 转发场景？有没有可能漏掉对同周期内 store 的转发？

44. `dispatch_one()` 中 store 指令通过 `rob.set_store_info()` 标记为 ready，但 store 数据（`Vk` 的值）仅在 dispatch 时记录。如果 store 在 dispatch 后、commit 前被 flush，`set_store_info` 写入的 ROB 条目会被 `flush_after` 清除吗？

45. `execute()` 中的调度优先级是 BRANCH > LOAD > ALU > STORE。这个优先级设计的原理是什么？如果改成 LOAD > BRANCH 会对分支预测的精度产生什么影响？

46. `dispatch_one()` 中调用 `cdb.broadcast()` 后未检查返回值。如果 CDB 已满（`new_slot >= CDB_COUNT`），`broadcast` 返回 false，但 RS 条目已被标记为 `busy = false`。此时会发生什么？这是否会导致结果丢失？

47. `dispatch_one()` 中对 Load 的符号扩展处理（LB 检查 `& 0x80`，LH 检查 `& 0x8000`）是否对所有情况都正确？比如 `load_val` 来自 mem.read(1) 时最多只有 8 位，`& 0x80` 检查是安全的。但如果 `load_val` 来自 store 转发呢？转发的值 `fwd_data` 可能是一个全宽度的值，这时符号扩展逻辑是否正确？

48. `fu_busy_count` 和 `fu_busy_count_new` 被声明和初始化但从未被更新或读取。是否是死代码？

49. `flush_after()` 同时清除了 `rs_new` 和 `rs`。如果 flush 发生在 rob.execute 中（当 commit 发现分支预测失败），此时 `rs` 中是否已经包含了本周期刚刚发射的指令？如果是，这些指令的 RS 条目会被正确清除吗？

---

## 七、重排序缓冲区（ROB.hpp）

50. `ROBEntry` 中有 `is_load` 字段但似乎未在任何核心逻辑中使用（仅在 `allocate` 中设置）。是否有场景需要区分 load 和其他指令？

51. `buffer` 和 `buffer_new` 是固定大小数组（`ROBEntry buffer[SIZE]`）。这意味着 ROB 的物理大小在编译时确定。如果要支持可配置的 ROB 大小（例如通过命令行参数），需要如何修改？

52. `flush_after()` 中第一层循环使用 `tail_new` 作为终止条件，第二层循环使用 `tail`。这个不对称设计的意图是什么？如果去掉这个不对称性（都使用 `tail_new` 或都使用 `tail`），会出现什么问题？

53. `commit_head()` 中 `use_new` 参数用于选择读取 `buffer` 还是 `buffer_new`。但 `commit_head` 总是将结果写入 `buffer_new[hd]`（清空条目）。为什么写始终用 `buffer_new` 而读可能用 `buffer`？在什么情况下 `buffer[head].ready` 为真但 `buffer_new[head].ready` 为假？

54. `commit_head()` 中 branch 指令的处理：先调用 `bp.update()`，再调用 `rf.write()`，然后如果预测失败则 `flush_after()`。如果 `bp.update()` 的副作用依赖于 `rf.write()` 的成功（例如 `rf.write()` 失败时 RAT 未清除），是否会出错？

55. `forward_store()` 扫描 `buffer_new` 从头到尾，直到遇到 `load_rob_id` 或找到一个匹配的 store。为什么要在遇到 `load_rob_id` 时停止？如果有一个 store 正好发生在 load 之后（更年轻），这个 store 应该被忽略——当前的逻辑是否正确？

56. `forward_store()` 中的 `size` 匹配检查要求 store 和 load 的 `size` 完全相等。但如果一个 `SW`（4 字节）后面跟着一个 `LB`（1 字节），`LB` 读取的地址落在 `SW` 写入的范围之内，这种部分重叠的 forwarding 能被检测到吗？

57. `has_pending_branch()` 现在接受 `CDB` 引用并检查 CDB 中是否有该分支的结果。如果分支的 CDB 值已经在上一周期的 CDB 中被消费（出现在 `cdb.old`），但在本周期才轮到 ROB head，这个检查是否会错误地认为分支不 pending？

58. `set_load_result()` 和 `cache_dispatched_value()` 功能上相似（都设置 `value` 和 `value_valid`），但前者还设置 `ready`。为什么需要两个不同的接口？是否可以用一个参数化的方法替代？

59. `ROB` 中 `head_ready()` 的方法似乎未被使用。是否有计划在将来使用它？或者这是可以删除的死代码？

---

## 八、公共数据总线（cdb.hpp）

60. `CDB` 类有一个 `new_slot` 计数器跟踪目前已经写入 `new_` 数组的位置。如果同一周期内有超过 `CDB_COUNT` 次广播尝试，后续的 `broadcast()` 会返回 false。这可能导致什么后果？`new_slot` 的 "溢出" 应如何处理？

61. `cdb.old` 和 `cdb.new_` 是 public 成员。为什么将内部状态暴露给外部？是否应该使用 accessor 方法？

62. `cdb.clear()` 在 `step()` 的最开始被调用，重置 `new_` 数组。但如果某模块（如 `commit_head()` 中的 `cdb.broadcast`）在 `clear()` 之前产生了广播，这些广播的值是写入 `new_` 的。`clear()` 会擦除它们吗？

63. `cdb.execute()` 是空函数（`void execute() {}`）。它的存在是否只是为了保持与其它模块接口的一致性？对于接口设计而言，这是好模式还是反模式？

64. `cdb.update()` 将 `new_` 逐元素拷贝到 `old`。对于 `CDB_COUNT = 4` 来说开销很小，但如果 `CDB_COUNT` 很大（如 16），这种逐元素拷贝是否会成为性能瓶颈？能否用环形缓冲或交换指针来优化？

---

## 九、分支预测器（branch_predictor.hpp）

65. BHT 初始化时所有条目被设为 1（弱不跳转）。为什么选用这个初始状态而不是 2（弱跳转）或 0（强不跳转）？

66. BHT 的索引是 `(pc >> 2) % BHT_SIZE`。这忽略了 PC 的低 2 位（恒为 0）和高位（通过取模）。这种简单的哈希对于不同 PC 的别名（aliasing）冲突有什么影响？

67. `update()` 中同时更新 BHT 和统计信息，但统计 `predict_correct` 的比较使用了 `bht[idx]`（旧状态）进行判断。如果在同一周期内 `update()` 被多次调用（例如同一周期提交了多条分支），`bht[idx]` 是否已经被之前的调用修改？

68. `accuracy()` 方法使用 `float` 类型运算。在嵌入式或高性能环境中，浮点运算可能较慢。是否有其他方式来报告分支预测准确率？

69. `execute()` 方法也是一个空函数。与其他模块的空 `execute()` 相似，这种设计在什么动机下被保留？

---

## 十、Load/Store Buffer（LSB.hpp）

70. `LSB.hpp` 文件定义了完整的 `LoadStoreBuffer` 类，但它是否被实际使用？在 `cpu.hpp` 中没有包含 `LSB.hpp`，也没有 LSB 成员。Load 和 Store 指令似乎完全在保留站和 ROB 内部处理。这个文件是否为未来的扩展功能？

71. `LSB` 中的 `can_forward()` 只在 LSB 内部的 store queue 中搜索。如果 load 需要 forwarded 的数据来自 RS 中已 dispatch 但未进 LSB 的 store，LSB 能满足吗？还是说 LSB 的设计依赖于 store 必须先进入 LSB？

72. LSB 中 load 的延迟被硬编码为 `delay = 3`（3 个周期）。这个值是否应该可配置或通过参数传入？

73. LSB 的 `flush_after()` 清除了 `buffer_new` 和 `buffer` 中的条目，但最后用 `store_queue[i] = store_queue_new[i]` 和 `store_count = store_count_new` 更新了状态。如果 flush 发生时 `store_queue_new` 中还包含被清除的条目索引，会发生什么？

---

## 十一、流水线控制（cpu.hpp）

74. `cpu.hpp` 的 `step()` 方法承担了流水线中全部的协调工作，却没有一个清晰的"阶段"概念（如 `fetch_stage()`, `dispatch_stage()`, `commit_stage()`）。一个约 60 行的单一方法符合良好的代码组织原则吗？

75. 取指逻辑中，`halt_fetch` 标志在遇到 `0x0ff00513` 时被设置为 true，这使得后续不再取指。但 ROB 和 RS 中可能仍有尚未提交的指令。如果程序在停机指令之后仍有 store 未提交，它们在提交时是否仍会写入内存？

76. `halt_fetch` 在分支预测失败时被重置为 false（`halt_fetch = false`）。这个逻辑的意图是什么？分支预测失败后的重新取指是否总是需要重置此标志？

77. `run()` 返回 `rf.read(10) & 0xFF`。这意味着返回值的低 8 位被作为程序的"退出码"。这符合 RISC-V ABI 中对 `a0` 寄存器的使用吗？`judgeResult` 是程序要返回的 8 位值，截断为 `& 0xFF` 是否总是正确的？

78. 为什么停机指令选用 `0x0ff00513`？这个 magic number 的来源和依据是什么？（提示：请查看 RISC-V 标准中 `ebreak` 和 `ecall` 的编码）

79. 每个周期只取指并发射一条指令。如果你的微架构允许每个周期发射多条指令（superscalar），`step()` 需要如何改动？

80. `init()` 和 `init_stdin()` 在整个程序生命周期中只被调用一次。如果用户想要在同一个 CPU 实例上先后运行两个不同的程序（重新初始化），现有 `init` 方法是否正确清理了所有状态？

---

## 十二、ALU 执行（ALU.hpp）

81. `alu_compute()` 被声明为 `static inline`。这意味着它在每个包含它的编译单元中都会被实例化。这会增加二进制大小吗？`inline` 关键字是否必要？

82. ALU 的 `SLL`、`SRL`、`SRA` 都使用 `vk & 0x1F` 限制移位量。这符合 RISC-V 规范（取立即数/寄存器值低 5 位）。但对于移位量超过 31 的情况，标准的 RISC-V 行为是否就是这样？

83. `ALU_MUL` 直接使用 `vj * vk` 进行 64 位乘法然后截断到 32 位。对于 RV32M 规范中的 `MUL` 指令，这个行为是否正确？（RV32M MUL 返回乘法结果的低 32 位）

84. `ALU_COPY` 返回 `imm` 值，`ALU_ADD` 返回 `sj + sk`。LUI 指令使用 `ALU_COPY` 返回立即数，而 AUIPC 使用 `ALU_ADD` 返回 `pc + imm`。这两种映射是否恰当？LUI 的操作数 `Vj` 和 `Vk` 在 RS 中是如何设置的？

---

## 十三、正确性与边界条件

85. ROB 环形缓冲的 `is_empty()` 和 `is_full()` 检查使用 `head == tail` 和 `(tail + 1) % SIZE == head`。这意味着 ROB 最多只能容纳 SIZE-1 条指令（有一个位置永远空闲）。对于 SIZE=16，最大容量是 15。这比 `ROB_SIZE` 的名称可能暗示的少 1，是否应该添加文档说明？

86. `rob.allocate()` 不检查 ROB 是否已满——调用者应该在调用前通过 `rob.is_full()` 检查。如果调用者忘记检查，会发生什么？`buffer_new` 会覆盖一个已存在的条目吗？

87. `rs.allocate()` 返回 -1 表示没有可用的 RS 条目，但调用前已有 `rs.is_full()` 检查保证了不会出现这种情况。如果将来修改了调用逻辑，返回 -1 会被如何处理？`rs.issue(idx=-1, ...)` 会导致数组越界访问吗？

88. `rob_id` 在 `ROBEntry` 中没有直接存储（rob_id 是从数组索引隐式推导的），但在 `RSEntry` 中显式存储了 `rob_id`。如果 ROB 的分配方式改变（如使用空闲列表而不是位置索引），这种不一致会导致什么后果？

89. `dispatch_one()` 中 load 地址计算使用了 `uint32_t addr = result`，其中 `result` 是 `compute_result()` 的返回值。`compute_result()` 使用 `rs_new` 中的 `Vj`, `Vk`, `imm`。如果 `Vj` 或 `Vk` 尚未准备好（`Qj != -1` 或 `Qk != -1`），dispatch 就不应该发生。那么 `is_ready()` 和 `compute_result()` 之间是否存在 TOCTOU（time-of-check-time-of-use）问题？

90. CDB 广播的 `rob_id` 是 `uint8_t` 类型。ROB 大小是 16（索引 0~15）。如果将来 ROB 大小增加到超过 256，`uint8_t` 类型能否容纳？同样的问题也存在于 `RSEntry.rob_id` 和 `ROBEntry` 中。

91. 程序在第 1 周期时 `cdb.old` 是空的（已初始化）。第 1 条指令被发射后，在 RS 中等待操作数（如果有依赖）。但如果第 1 条指令的操作数来自尚未被写入的寄存器（如 `sp`），程序是否会在无限循环中等待永远不会到来的 CDB 值？

92. 分支预测失败时，CDB 中已有的值（来自错误路径上已 dispatch 的指令）会污染正确路径吗？当前代码通过在 flush 时同时清空 `cdb.old` 和 `cdb.new_` 来解决这个问题。这样做是否会丢失正确路径上、分支之前的有效 CDB 值？

---

## 十四、性能与优化

93. `step()` 函数在每个周期的末尾调用 `rs.update()`, `rob.update()`, `rf.update()`, `cdb.update()`, `bp.update_bht()`。每个 `update()` 都执行完整的数组拷贝（对于 RF: 32×3 个 `int32_t`）。这种全量拷贝的方式在所有数据结构中是一致的。是否可以使用双缓冲交换指针（swap pointers）来减少拷贝开销？

94. 分支预测的 BHT 更新和预测准确率统计使用 `uint32_t` 计数器。对于 `basicopt1`（708,725 周期）和 `pi`（143,769,511 周期），32 位是否足够？`predict_count` 最大能表示多少？

95. 主循环 `while (!halted && cycle < 200000000)` 每次迭代调用 `step()`，而 `step()` 调用了多个模块的 `execute()` 和 `update()` 方法。对于 pi 计算用例（1.43 亿周期），有没有进行性能剖析？最耗时的部分是哪个模块？

96. `compute_result()` 在 dispatch 时调用 `alu_compute()`。如果某个 RS 条目等待操作数多周期，`compute_result()` 是否可以更早计算（在操作数到齐后立即计算），还是只能等到 dispatch 时刻？

---

## 十五、代码质量与风格

97. 代码中同时存在英文注释（`// wires (const)`）和中文注释（`// 保留站条目数`）。对于开源项目，是否应该统一注释语言？中英混杂对协作者是否友好？

98. `ROB.hpp` 中 `SIZE` 是 `static const int`，`REG_COUNT` 是全局 `const int`。`ROB_SIZE` 也是全局 `const int`。这些常量的可见性和定义方式不一致。是否应该全部放在 `types.hpp` 中或全部作为类内部常量？

99. `CDBValue` 结构体的字段没有使用默认成员初始化器（如 `valid = false`），而 `RSEntry` 有。这是有意为之还是疏忽？

100. 回顾整个项目，你如何评价在 4 天内实现一个完整 Tomasulo 算法的难度？你认为项目中最具挑战性的部分是什么（分支预测、内存转发、乱序提交、精确异常）？如果你有更多时间，会优先改进哪些方面？
