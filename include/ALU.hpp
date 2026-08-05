#ifndef ALU_HPP
#define ALU_HPP

#include "types.hpp"

static inline uint32_t alu_compute(ALUOp op, uint32_t vj, uint32_t vk, uint32_t imm) {
    int32_t sj = (int32_t)vj;
    int32_t sk = (int32_t)vk;
    switch (op) {
        case ALU_ADD:  return (uint32_t)(sj + sk);
        case ALU_SUB:  return (uint32_t)(sj - sk);
        case ALU_SLL:  return vj << (vk & 0x1F);
        case ALU_SLT:  return (sj < sk) ? 1 : 0;
        case ALU_SLTU: return (vj < vk) ? 1 : 0;
        case ALU_XOR:  return vj ^ vk;
        case ALU_SRL:  return vj >> (vk & 0x1F);
        case ALU_SRA:  return (uint32_t)(sj >> (sk & 0x1F));
        case ALU_OR:   return vj | vk;
        case ALU_AND:  return vj & vk;
        case ALU_MUL:  return vj * vk;
        case ALU_COPY: return imm;
        default:       return 0;
    }
}

#endif
