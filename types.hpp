#ifndef TYPES_HPP
#define TYPES_HPP

#include <cstdint>

const int REG_COUNT    = 32;//寄存器个数
const int MEM_SIZE     = 1 << 20;//内存大小
const int RS_ALU_COUNT = 4;//Alu保留站条目数
const int RS_MUL_COUNT = 2;//乘除保留站条目数
const int RS_LS_COUNT  = 4;
const int RS_TOTAL     = RS_ALU_COUNT + RS_MUL_COUNT + RS_LS_COUNT;
const int ROB_SIZE     = 16;
const int LSB_SIZE     = 8;
const int CDB_COUNT    = 1;
const int BHT_SIZE     = 64;

enum Opcode {
    LUI, AUIPC, JAL, JALR,
    BEQ, BNE, BLT, BGE, BLTU, BGEU,
    LB, LH, LW, LBU, LHU,
    SB, SH, SW,
    ADDI, SLTI, SLTIU, XORI, ORI, ANDI, SLLI, SRLI, SRAI,
    ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND, UNKNOWN,
    EBREAK, ECALL, MUL
};
enum FUType { FU_ALU, FU_MUL, FU_LOAD, FU_STORE, FU_BRANCH };
enum InstType { R_TYPE, I_TYPE, S_TYPE, B_TYPE, U_TYPE, J_TYPE };
enum ALUOp { ALU_ADD, ALU_SUB, ALU_SLL, ALU_SLT, ALU_SLTU, 
             ALU_XOR, ALU_SRL, ALU_SRA, ALU_OR, ALU_AND, ALU_MUL,
             ALU_COPY, ALU_NOP };


struct DecodedInst {
    Opcode   opcode; // 操作码
    uint32_t pc; // 指令地址 (用于分支预测 / flush)
    uint8_t  rd; // 目标寄存器 (0-31)
    uint8_t  rs1;// 源寄存器 1
    uint8_t  rs2;// 源寄存器 2
    uint32_t imm;// 符号扩展后的立即数
    InstType type;// 指令格式
    FUType   fu_type;// 应分配到的功能单元
    ALUOp    alu_op;// ALU 操作类型
    bool     is_branch;// 是否分支指令
    bool     is_mem_read;// 是否加载指令
    bool     is_mem_write;// 是否存储指令
    bool     is_halt;// 是否停机指令 (0x0ff00513)
};
struct CDBValue {
    bool     valid;// 该槽位是否有效
    uint8_t  rob_id;// 产生结果的 ROB 条目编号
    uint32_t value;// 计算结果 / 加载数据
};
#endif