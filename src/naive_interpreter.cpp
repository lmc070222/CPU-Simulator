#include <fstream>
#include <cstdint>
#include <string>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>

const uint32_t MEM_SIZE = 1024 * 1024;
uint8_t mem[MEM_SIZE];

void load_memory(const char* filename) {
    std::ifstream f(filename);
    std::string line;
    uint32_t addr = 0;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        if (line[0] == '@') {
            addr = std::stoll(line.substr(1), nullptr, 16);
        } else {
            std::stringstream ss(line);
            std::string byte_str;
            while (ss >> byte_str) {
                if (byte_str.size() == 2) {
                    uint8_t byte = std::stoul(byte_str, nullptr, 16);
                    mem[addr++] = byte;
                }
            }
        }
    }
}

uint32_t fetch(uint32_t pc) {
    return mem[pc] | (mem[pc+1] << 8) | (mem[pc+2] << 16) | (mem[pc+3] << 24);
}

enum Opcode {
    LUI, AUIPC, JAL, JALR,
    BEQ, BNE, BLT, BGE, BLTU, BGEU,
    LB, LH, LW, LBU, LHU,
    SB, SH, SW,
    ADDI, SLTI, SLTIU, XORI, ORI, ANDI, SLLI, SRLI, SRAI,
    ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND, UNKNOWN,
    EBREAK, ECALL, MUL
};

struct Instr {
    Opcode op;
    int rd, rs1, rs2;
    int32_t imm;
    uint32_t raw;
};

Instr decode(uint32_t raw) {
    Instr i;
    i.raw = raw;
    i.op = UNKNOWN;
    i.rd = 0; i.rs1 = 0; i.rs2 = 0; i.imm = 0;

    uint8_t opcode = raw & 0x7F;
    uint8_t funct3 = (raw >> 12) & 0x7;
    uint8_t funct7 = (raw >> 25) & 0x7F;
    int rd  = (raw >> 7)  & 0x1F;
    int rs1 = (raw >> 15) & 0x1F;
    int rs2 = (raw >> 20) & 0x1F;

    switch (opcode) {
        case 0b0110111: // LUI
            i.op = LUI;
            i.rd = rd;
            i.imm = raw & 0xFFFFF000;
            break;
        case 0b0010111: // AUIPC
            i.op = AUIPC;
            i.rd = rd;
            i.imm = raw & 0xFFFFF000;
            break;
        case 0b1101111: // JAL
            i.op = JAL;
            i.rd = rd;
            { uint32_t imm = ((raw >> 31) << 20) | (((raw >> 21) & 0x3FF) << 1) |
                             (((raw >> 20) & 1) << 11) | (((raw >> 12) & 0xFF) << 12);
              if (imm & 0x100000) imm |= 0xFFE00000;
              i.imm = (int32_t)imm; }
            break;
        case 0b1110011: // SYSTEM
            i.imm = (raw >> 20) & 0xFFF;
            if (funct3 == 0 && i.imm == 0) i.op = ECALL;
            else if (funct3 == 0 && i.imm == 1) i.op = EBREAK;
            break;
        case 0b0010011: // I-type ALU
            if (funct3 == 0) i.op = ADDI;
            else if (funct3 == 2) i.op = SLTI;
            else if (funct3 == 7) i.op = ANDI;
            else if (funct3 == 6) i.op = ORI;
            else if (funct3 == 4) i.op = XORI;
            else if (funct3 == 1 && funct7 == 0) i.op = SLLI;
            else if (funct3 == 5 && funct7 == 0) i.op = SRLI;
            else if (funct3 == 5 && funct7 == 0x20) i.op = SRAI;
            else if (funct3 == 3) i.op = SLTIU;
            i.rd = rd;
            i.rs1 = rs1;
            i.imm = (int32_t)raw >> 20;
            break;
        case 0b1100111: // JALR
            if (funct3 == 0) i.op = JALR;
            i.rd = rd;
            i.rs1 = rs1;
            i.imm = (int32_t)raw >> 20;
            break;
        case 0b0110011: // R-type
            if (funct3 == 0 && funct7 == 0) i.op = ADD;
            else if (funct3 == 0 && funct7 == 0x20) i.op = SUB;
            else if (funct3 == 7 && funct7 == 0) i.op = AND;
            else if (funct3 == 6 && funct7 == 0) i.op = OR;
            else if (funct3 == 4 && funct7 == 0) i.op = XOR;
            else if (funct3 == 1 && funct7 == 0) i.op = SLL;
            else if (funct3 == 5 && funct7 == 0) i.op = SRL;
            else if (funct3 == 5 && funct7 == 0x20) i.op = SRA;
            else if (funct3 == 2 && funct7 == 0) i.op = SLT;
            else if (funct3 == 3 && funct7 == 0) i.op = SLTU;
            else if (funct3 == 0 && funct7 == 1) i.op = MUL;
            i.rd = rd; i.rs1 = rs1; i.rs2 = rs2;
            break;
        case 0b0000011: // Load
            if (funct3 == 0) i.op = LB;
            else if (funct3 == 4) i.op = LBU;
            else if (funct3 == 1) i.op = LH;
            else if (funct3 == 5) i.op = LHU;
            else if (funct3 == 2) i.op = LW;
            i.rd = rd;
            i.rs1 = rs1;
            i.imm = (int32_t)raw >> 20;
            break;
        case 0b0100011: // Store
            if (funct3 == 0) i.op = SB;
            else if (funct3 == 1) i.op = SH;
            else if (funct3 == 2) i.op = SW;
            i.rs1 = rs1; i.rs2 = rs2;
            { uint32_t imm = ((raw >> 25) << 5) | ((raw >> 7) & 0x1F);
              if (imm & 0x800) imm |= 0xFFFFF000;
              i.imm = (int32_t)imm; }
            break;
        case 0b1100011: // Branch
            if (funct3 == 0) i.op = BEQ;
            else if (funct3 == 5) i.op = BGE;
            else if (funct3 == 7) i.op = BGEU;
            else if (funct3 == 4) i.op = BLT;
            else if (funct3 == 6) i.op = BLTU;
            else if (funct3 == 1) i.op = BNE;
            i.rs1 = rs1; i.rs2 = rs2;
            { uint32_t imm = ((raw >> 31) << 12) | (((raw >> 7) & 1) << 11) |
                             (((raw >> 25) & 0x3F) << 5) | (((raw >> 8) & 0xF) << 1);
              if (imm & 0x1000) imm |= 0xFFFFE000;
              i.imm = (int32_t)imm; }
            break;
        default:
            break;
    }
    return i;
}

uint32_t reg[32];
uint32_t pc;

uint32_t mem_read(uint32_t addr, int size) {
    addr &= (MEM_SIZE - 1);
    if (size == 1) return mem[addr];
    if (size == 2) return mem[addr] | (mem[addr+1] << 8);
    return mem[addr] | (mem[addr+1] << 8) | (mem[addr+2] << 16) | (mem[addr+3] << 24);
}

void mem_write(uint32_t addr, uint32_t val, int size) {
    addr &= (MEM_SIZE - 1);
    mem[addr] = val & 0xFF;
    if (size >= 2) { mem[addr+1] = (val >> 8) & 0xFF; }
    if (size >= 4) { mem[addr+2] = (val >> 16) & 0xFF; mem[addr+3] = (val >> 24) & 0xFF; }
}

int32_t sign_ext(uint32_t val, int bits) {
    if (val & (1u << (bits - 1)))
        return (int32_t)(val | (~0u << bits));
    return (int32_t)val;
}

int run() {
    pc = 0;
    memset(reg, 0, sizeof(reg));
    uint64_t cycle = 0;

    while (true) {
        uint32_t inst = fetch(pc);
        if (inst == 0x0ff00513) {
            return (int)(reg[10] & 0xFF);
        }

        Instr i = decode(inst);
        reg[0] = 0;
        uint32_t next_pc = pc + 4;

        int32_t val_rs1 = (int32_t)reg[i.rs1];
        int32_t val_rs2 = (int32_t)reg[i.rs2];
        int32_t addr, result;
        uint32_t uresult;

        switch (i.op) {
        case LUI:
            reg[i.rd] = (uint32_t)i.imm;
            break;
        case AUIPC:
            reg[i.rd] = pc + (uint32_t)i.imm;
            break;
        case JAL:
            reg[i.rd] = pc + 4;
            next_pc = pc + (uint32_t)i.imm;
            break;
        case JALR:
            reg[i.rd] = pc + 4;
            next_pc = ((uint32_t)(val_rs1 + i.imm)) & ~1u;
            break;
        case BEQ:
            if (val_rs1 == val_rs2) next_pc = pc + (uint32_t)i.imm;
            break;
        case BNE:
            if (val_rs1 != val_rs2) next_pc = pc + (uint32_t)i.imm;
            break;
        case BLT:
            if (val_rs1 < val_rs2) next_pc = pc + (uint32_t)i.imm;
            break;
        case BGE:
            if (val_rs1 >= val_rs2) next_pc = pc + (uint32_t)i.imm;
            break;
        case BLTU:
            if ((uint32_t)val_rs1 < (uint32_t)val_rs2) next_pc = pc + (uint32_t)i.imm;
            break;
        case BGEU:
            if ((uint32_t)val_rs1 >= (uint32_t)val_rs2) next_pc = pc + (uint32_t)i.imm;
            break;
        case LB:
            addr = val_rs1 + i.imm;
            reg[i.rd] = (uint32_t)sign_ext(mem_read(addr, 1), 8);
            break;
        case LBU:
            addr = val_rs1 + i.imm;
            reg[i.rd] = mem_read(addr, 1);
            break;
        case LH:
            addr = val_rs1 + i.imm;
            reg[i.rd] = (uint32_t)sign_ext(mem_read(addr, 2), 16);
            break;
        case LHU:
            addr = val_rs1 + i.imm;
            reg[i.rd] = mem_read(addr, 2);
            break;
        case LW:
            addr = val_rs1 + i.imm;
            reg[i.rd] = mem_read(addr, 4);
            break;
        case SB:
            addr = val_rs1 + i.imm;
            mem_write(addr, (uint32_t)val_rs2, 1);
            break;
        case SH:
            addr = val_rs1 + i.imm;
            mem_write(addr, (uint32_t)val_rs2, 2);
            break;
        case SW:
            addr = val_rs1 + i.imm;
            mem_write(addr, (uint32_t)val_rs2, 4);
            break;
        case ADDI:
            reg[i.rd] = (uint32_t)(val_rs1 + i.imm);
            break;
        case SLTI:
            reg[i.rd] = (val_rs1 < i.imm) ? 1 : 0;
            break;
        case SLTIU:
            reg[i.rd] = ((uint32_t)val_rs1 < (uint32_t)i.imm) ? 1 : 0;
            break;
        case XORI:
            reg[i.rd] = (uint32_t)val_rs1 ^ (uint32_t)i.imm;
            break;
        case ORI:
            reg[i.rd] = (uint32_t)val_rs1 | (uint32_t)i.imm;
            break;
        case ANDI:
            reg[i.rd] = (uint32_t)val_rs1 & (uint32_t)i.imm;
            break;
        case SLLI:
            reg[i.rd] = (uint32_t)val_rs1 << (i.imm & 0x1F);
            break;
        case SRLI:
            reg[i.rd] = (uint32_t)val_rs1 >> (i.imm & 0x1F);
            break;
        case SRAI:
            reg[i.rd] = (uint32_t)(val_rs1 >> (i.imm & 0x1F));
            break;
        case ADD:
            reg[i.rd] = (uint32_t)(val_rs1 + val_rs2);
            break;
        case SUB:
            reg[i.rd] = (uint32_t)(val_rs1 - val_rs2);
            break;
        case SLL:
            reg[i.rd] = (uint32_t)val_rs1 << (val_rs2 & 0x1F);
            break;
        case SLT:
            reg[i.rd] = (val_rs1 < val_rs2) ? 1 : 0;
            break;
        case SLTU:
            reg[i.rd] = ((uint32_t)val_rs1 < (uint32_t)val_rs2) ? 1 : 0;
            break;
        case XOR:
            reg[i.rd] = (uint32_t)val_rs1 ^ (uint32_t)val_rs2;
            break;
        case SRL:
            reg[i.rd] = (uint32_t)val_rs1 >> (val_rs2 & 0x1F);
            break;
        case SRA:
            reg[i.rd] = (uint32_t)(val_rs1 >> (val_rs2 & 0x1F));
            break;
        case OR:
            reg[i.rd] = (uint32_t)val_rs1 | (uint32_t)val_rs2;
            break;
        case AND:
            reg[i.rd] = (uint32_t)val_rs1 & (uint32_t)val_rs2;
            break;
        case MUL:
            reg[i.rd] = (uint32_t)(val_rs1 * val_rs2);
            break;
        default:
            fprintf(stderr, "Unknown opcode at pc=0x%08x, inst=0x%08x\n", pc, inst);
            return -1;
        }

        reg[0] = 0;
        pc = next_pc;
        cycle++;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <datafile>\n", argv[0]);
        return 1;
    }
    load_memory(argv[1]);
    int result = run();
    printf("%d\n", result);
    return 0;
}
