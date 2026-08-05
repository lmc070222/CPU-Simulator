#ifndef DECODER_HPP
#define DECODER_HPP

#include "types.hpp"

static DecodedInst decode(uint32_t raw, uint32_t pc) {
    DecodedInst d;
    d.opcode      = UNKNOWN;
    d.pc          = pc;
    d.rd          = (raw >> 7)  & 0x1F;
    d.rs1         = (raw >> 15) & 0x1F;
    d.rs2         = (raw >> 20) & 0x1F;
    d.imm         = 0;
    d.type        = I_TYPE;
    d.fu_type     = FU_ALU;
    d.alu_op      = ALU_NOP;
    d.is_branch   = false;
    d.is_mem_read = false;
    d.is_mem_write = false;
    d.is_halt     = (raw == 0x0ff00513);

    uint8_t opcode = raw & 0x7F;
    uint8_t funct3 = (raw >> 12) & 0x7;
    uint8_t funct7 = (raw >> 25) & 0x7F;

    switch (opcode) {
    case 0b0110111: // LUI
        d.opcode = LUI;
        d.type   = U_TYPE;
        d.alu_op = ALU_COPY;
        d.imm    = raw & 0xFFFFF000;
        break;

    case 0b0010111: // AUIPC
        d.opcode = AUIPC;
        d.type   = U_TYPE;
        d.alu_op = ALU_ADD;
        d.imm    = raw & 0xFFFFF000;
        break;

    case 0b1101111: { // JAL
        d.opcode  = JAL;
        d.type    = J_TYPE;
        d.alu_op  = ALU_ADD;
        d.is_branch = true;
        d.fu_type  = FU_BRANCH;
        uint32_t imm = ((raw >> 31) << 20) | (((raw >> 21) & 0x3FF) << 1) |
                       (((raw >> 20) & 1) << 11) | (((raw >> 12) & 0xFF) << 12);
        if (imm & 0x100000) imm |= 0xFFE00000;
        d.imm = (int32_t)imm;
        break;
    }

    case 0b1100111: // JALR
        d.opcode  = JALR;
        d.type    = I_TYPE;
        d.alu_op  = ALU_ADD;
        d.fu_type = FU_BRANCH;
        d.is_branch = true;
        d.imm = (int32_t)raw >> 20;
        break;

    case 0b1100011: { // Branch
        d.type    = B_TYPE;
        d.fu_type = FU_BRANCH;
        d.is_branch = true;
        d.alu_op  = ALU_SUB;
        d.rd      = 0;  // branches have no dest register
        if      (funct3 == 0) d.opcode = BEQ;
        else if (funct3 == 1) d.opcode = BNE;
        else if (funct3 == 4) { d.opcode = BLT; d.alu_op = ALU_SLT; }
        else if (funct3 == 5) { d.opcode = BGE; d.alu_op = ALU_SLT; }
        else if (funct3 == 6) { d.opcode = BLTU; d.alu_op = ALU_SLTU; }
        else if (funct3 == 7) { d.opcode = BGEU; d.alu_op = ALU_SLTU; }
        uint32_t imm = ((raw >> 31) << 12) | (((raw >> 7) & 1) << 11) |
                       (((raw >> 25) & 0x3F) << 5) | (((raw >> 8) & 0xF) << 1);
        if (imm & 0x1000) imm |= 0xFFFFE000;
        d.imm = (int32_t)imm;
        break;
    }

    case 0b0000011: // Load
        d.type        = I_TYPE;
        d.fu_type     = FU_LOAD;
        d.is_mem_read = true;
        d.alu_op      = ALU_ADD;
        if      (funct3 == 0) d.opcode = LB;
        else if (funct3 == 1) d.opcode = LH;
        else if (funct3 == 2) d.opcode = LW;
        else if (funct3 == 4) d.opcode = LBU;
        else if (funct3 == 5) d.opcode = LHU;
        d.imm = (int32_t)raw >> 20;
        break;

    case 0b0100011: // Store
        d.type         = S_TYPE;
        d.fu_type      = FU_STORE;
        d.is_mem_write = true;
        d.alu_op       = ALU_ADD;
        d.rd           = 0;  // stores have no dest register
        if      (funct3 == 0) d.opcode = SB;
        else if (funct3 == 1) d.opcode = SH;
        else if (funct3 == 2) d.opcode = SW;
        {
            uint32_t imm = ((raw >> 25) << 5) | ((raw >> 7) & 0x1F);
            if (imm & 0x800) imm |= 0xFFFFF000;
            d.imm = (int32_t)imm;
        }
        break;

    case 0b0010011: // I-type ALU
        d.type = I_TYPE;
        if      (funct3 == 0)                { d.opcode = ADDI;  d.alu_op = ALU_ADD;  }
        else if (funct3 == 2)                { d.opcode = SLTI;  d.alu_op = ALU_SLT;  }
        else if (funct3 == 3)                { d.opcode = SLTIU; d.alu_op = ALU_SLTU; }
        else if (funct3 == 4)                { d.opcode = XORI;  d.alu_op = ALU_XOR;  }
        else if (funct3 == 6)                { d.opcode = ORI;   d.alu_op = ALU_OR;   }
        else if (funct3 == 7)                { d.opcode = ANDI;  d.alu_op = ALU_AND;  }
        else if (funct3 == 1 && funct7 == 0) { d.opcode = SLLI;  d.alu_op = ALU_SLL;  }
        else if (funct3 == 5 && funct7 == 0) { d.opcode = SRLI;  d.alu_op = ALU_SRL;  }
        else if (funct3 == 5 && funct7 == 0x20) { d.opcode = SRAI; d.alu_op = ALU_SRA; }
        d.imm = (int32_t)raw >> 20;
        break;

    case 0b0110011: // R-type
        d.type = R_TYPE;
        if      (funct3 == 0 && funct7 == 0)    { d.opcode = ADD;  d.alu_op = ALU_ADD;  }
        else if (funct3 == 0 && funct7 == 0x20) { d.opcode = SUB;  d.alu_op = ALU_SUB;  }
        else if (funct3 == 0 && funct7 == 1)    { d.opcode = MUL;  d.alu_op = ALU_MUL; d.fu_type = FU_MUL; }
        else if (funct3 == 1 && funct7 == 0)    { d.opcode = SLL;  d.alu_op = ALU_SLL;  }
        else if (funct3 == 2 && funct7 == 0)    { d.opcode = SLT;  d.alu_op = ALU_SLT;  }
        else if (funct3 == 3 && funct7 == 0)    { d.opcode = SLTU; d.alu_op = ALU_SLTU; }
        else if (funct3 == 4 && funct7 == 0)    { d.opcode = XOR;  d.alu_op = ALU_XOR;  }
        else if (funct3 == 5 && funct7 == 0)    { d.opcode = SRL;  d.alu_op = ALU_SRL;  }
        else if (funct3 == 5 && funct7 == 0x20) { d.opcode = SRA;  d.alu_op = ALU_SRA;  }
        else if (funct3 == 6 && funct7 == 0)    { d.opcode = OR;   d.alu_op = ALU_OR;   }
        else if (funct3 == 7 && funct7 == 0)    { d.opcode = AND;  d.alu_op = ALU_AND;  }
        break;

    case 0b1110011: // SYSTEM
        d.imm = (raw >> 20) & 0xFFF;
        if (funct3 == 0 && d.imm == 0)      d.opcode = ECALL;
        else if (funct3 == 0 && d.imm == 1) d.opcode = EBREAK;
        break;

    default:
        break;
    }

    return d;
}

#endif
