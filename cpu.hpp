#ifndef CPU_HPP
#define CPU_HPP
#include <fstream>
#include <cstdint>
#include <string>
#include <sstream>
class cpu {
  private : 
  const static uint32_t MEM_SIZE = 1024 * 1024;
  uint8_t mem[MEM_SIZE];
  void load_memory (const char* filename , uint8_t* mem) { 
    std::ifstream f(filename) ;
    std::string line ; 
    uint32_t addr = 0 ;
    while (std::getline(f , line)) {
      if (line.empty() == true) continue ;
      if (line[0] == '@') {
        addr = std::stoll(line.substr(1) , nullptr , 16);
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
  uint32_t fetch(uint8_t* mem, uint32_t pc) {
    return mem[pc] | (mem[pc+1] << 8) | (mem[pc+2] << 16) | (mem[pc+3] << 24);
  }
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
    uint8_t opcode = raw & 0x7F;
    uint8_t funct3 = (raw >> 12) & 0x7;
    uint8_t funct7 = (raw >> 25) & 0x7F;
    int rd  = (raw >> 7)  & 0x1F;
    int rs1 = (raw >> 15) & 0x1F;
    int rs2 = (raw >> 20) & 0x1F;

    switch (opcode) {
        case 0b0110111:
            i.op = LUI;
            i.rd = rd;
            i.imm = raw & 0xFFFFF000;
            break;
        case 0b0010111: 
            i.op = AUIPC;
            i.rd = rd;
            i.imm = raw & 0xFFFFF000;
            break;
        case 0b1101111:
            i.op = JAL;
            i.rd = rd;
            { uint32_t imm = ((raw >> 31) << 20) | ((raw >> 21) & 0x3FF) << 1 |
                             ((raw >> 20) & 1) << 11 | ((raw >> 12) & 0xFF) << 12;
              if (imm & 0x100000) imm |= 0xFFE00000;
              i.imm = imm; }
            break;
        case 0b1110011 :
            i.imm = (raw >> 20) & 0xFFF ;
            if (funct3 == 0 and i.imm == 0) i.op = ECALL ;
            else if (funct3 == 0 and i.imm == 1) i.op = EBREAK ;
            break ; 
        case 0b0010011: // I-type ALU
            if (funct3 == 0) i.op = ADDI;
            else if (funct3 == 2) i.op = SLTI ;
            else if (funct3 == 7) i.op = ANDI ;
            else if (funct3 == 6) i.op = ORI ; 
            else if (funct3 == 4) i.op = XORI ;
            else if (funct3 == 1 and funct7 == 0) i.op = SLLI ;
            else if (funct3 == 5 and funct7 == 0) i.op = SRLI ;
            else if (funct3 == 5 and funct7 == 0x20) i.op = SRAI ;
            else if (funct3 == 3) i.op = SLTIU ;
            i.rd = rd; 
            i.rs1 = rs1;
            i.imm = (int32_t)raw >> 20;
            break;
        case 0b1100111: 
            if (funct3 == 0) i.op = JALR ;
            i.rd = rd ;
            i.rs1 = rs1;
            i.imm = (int32_t)raw >> 20;
            break ;
        case 0b0110011:
            if (funct3 == 0 and funct7 == 0) i.op = ADD ;
            else if (funct3 == 0 and funct7 == 0x20) i.op = SUB ;
            else if (funct3 == 7 and funct7 == 0) i.op = AND ; 
            else if (funct3 == 6 and funct7 == 0) i.op = OR ;
            else if (funct3 == 4 and funct7 == 0) i.op = XOR ;
            else if (funct3 == 1 and funct7 == 0) i.op = SLL ;
            else if (funct3 == 5 and funct7 == 0) i.op = SRL ;
            else if (funct3 == 5 and funct7 == 0x20) i.op = SRA ;
            else if (funct3 == 2 and funct7 == 0) i.op = SLT ;
            else if (funct3 == 3 and funct7 == 0) i.op = SLTU ;
            else if (funct3 == 0 and funct7 == 1) i.op = MUL ;
            i.rd = rd; i.rs1 = rs1; i.rs2 = rs2;
            break;
        case 0b0000011: // Load
            if (funct3 == 0) i.op = LB ;
            else if (funct3 == 4) i.op = LBU ;
            else if (funct3 == 1) i.op = LH ; 
            else if (funct3 == 5) i.op = LHU ;
            else if (funct3 == 2) i.op = LW ;
            i.rd = rd; 
            i.rs1 = rs1;
            i.imm = (int32_t)raw >> 20;
            break;
        case 0b0100011:
            if (funct3 == 0) i.op = SB ;
            else if (funct3 == 1) i.op = SH ;
            else if (funct3 == 2) i.op = SW ;
            i.rs1 = rs1; i.rs2 = rs2;
            i.imm = ((raw >> 25) << 5) | ((raw >> 7) & 0x1F);
            if (i.imm & 0x800) i.imm |= 0xFFFFF000;
            break;
        case 0b1100011: // Branch
            if (funct3 == 0) i.op = BEQ ;
            else if (funct3 == 5) i.op = BGE ;
            else if (funct3 == 7) i.op = BGEU ;
            else if (funct3 == 4) i.op = BLT ;
            else if (funct3 == 6) i.op = BLTU ;
            else if (funct3 == 1) i.op = BNE ;

            i.rs1 = rs1; i.rs2 = rs2;
            { uint32_t imm = ((raw >> 31) << 12) | (((raw >> 7) & 1) << 11) |
                             (((raw >> 25) & 0x3F) << 5) | (((raw >> 8) & 0xF) << 1);
              if (imm & 0x1000) imm |= 0xFFFFE000;
              i.imm = imm; }
            break;
        default:
            i.op = UNKNOWN;
    }
    return i;
}


};






#endif 