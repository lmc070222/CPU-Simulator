#ifndef RESERVATION_STATION_HPP
#define RESERVATION_STATION_HPP

#include "cdb.hpp"
#include "register.hpp"
#include "types.hpp"

struct RSEntry {
  bool busy = false;
  FUType fu;
  ALUOp alu_op;
  uint32_t Vj, Vk;
  int32_t Qj = -1, Qk = -1;
  uint32_t imm;//立即数
  uint8_t dest;//目标寄存器
  uint8_t rob_id;
  uint32_t pc;
  bool mem_read, mem_write;
  bool is_branch;
  bool predicted_taken;
  uint32_t branch_target;
};

class ReservationStations {
private:
  RSEntry rs[RS_TOTAL];
  RSEntry rs_new[RS_TOTAL];
  int fu_busy_count[5]{0};
  int fu_busy_count_new[5]{0};
  uint32_t compute_result(int idx) {
    RSEntry &e = rs_new[idx];
    int32_t vj = (int32_t)e.Vj;
    int32_t vk = (int32_t)e.Vk;
    int32_t imm = (int32_t)e.imm;

    switch (e.alu_op) {
    case ALU_ADD:
      return (uint32_t)(vj + vk);
    case ALU_SUB:
      return (uint32_t)(vj - vk);
    case ALU_SLL:
      return e.Vj << (e.Vk & 0x1F);
    case ALU_SLT:
      return (vj < vk) ? 1 : 0;
    case ALU_SLTU:
      return (e.Vj < e.Vk) ? 1 : 0;
    case ALU_XOR:
      return e.Vj ^ e.Vk;
    case ALU_SRL:
      return e.Vj >> (e.Vk & 0x1F);
    case ALU_SRA:
      return (uint32_t)(vj >> (e.Vk & 0x1F));
    case ALU_OR:
      return e.Vj | e.Vk;
    case ALU_AND:
      return e.Vj & e.Vk;
    case ALU_MUL:
      return e.Vj * e.Vk;
    case ALU_COPY:
      return e.imm; // LUI
    default:
      return 0;
    }
  }
  void get_range(FUType fu, int &start, int &end) {
    if (fu == FU_ALU || fu == FU_BRANCH) {
      start = 0; end = RS_ALU_COUNT;
    } else if (fu == FU_MUL) {
      start = RS_ALU_COUNT; end = RS_ALU_COUNT + RS_MUL_COUNT;
    } else {
      start = RS_ALU_COUNT + RS_MUL_COUNT; end = RS_TOTAL;
    }
  }
  int fu_to_priority(FUType fu) {
    if (fu == FU_BRANCH) return 0;
    if (fu == FU_LOAD) return 1;
    if (fu == FU_ALU) return 2;
    if (fu == FU_STORE) return 3;
    return 4;
  }

public:
  void init() {
    for (int i = 0; i < RS_TOTAL; i++) {
      rs[i].busy = rs_new[i].busy = false;
      rs[i].Qj = rs[i].Qk = rs_new[i].Qj = rs_new[i].Qk = -1;
    }
    for (int i = 0; i < 5; i++) {
      fu_busy_count[i] = fu_busy_count_new[i] = 0;
    }
  }
  int allocate(FUType fu) {
    int start, end;
    get_range(fu, start, end);
    for (int i = start; i < end; i++) {
      if (rs_new[i].busy == false)
        return i;
    }
    return -1;
  }
  bool is_full(FUType fu) {
    int start, end;
    get_range(fu, start, end);
    for (int i = start; i < end; i++) {
      if (rs_new[i].busy == false) return false;
    }
    return true;
  }
  void issue(int idx, const DecodedInst &inst, int rob_id,
             const class RegisterFile &rf) {
    RSEntry &e = rs_new[idx];
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

    e.Vj = rf.read(inst.rs1);
    e.Qj = rf.get_reorder(inst.rs1);
    if (e.Qj != -1) e.Vj = 0;
    if (inst.opcode == AUIPC) {
      e.Vj = inst.pc;
      e.Qj = -1;
    }

    e.Vk = rf.read(inst.rs2);
    e.Qk = rf.get_reorder(inst.rs2);
    if (e.Qk != -1) e.Vk = 0;

    if (inst.type == I_TYPE || inst.type == U_TYPE || inst.type == J_TYPE) {
      e.Qk = -1;
      e.Vk = e.imm;
    }

    if (inst.opcode == LUI) {
      e.Qj = -1;
      e.Vj = 0;
    }
  }

  bool is_ready(int idx) {
    return rs_new[idx].busy && rs_new[idx].Qj == -1 && rs_new[idx].Qk == -1;
  }
  void listen_cdb(const CDBValue &cdb) {
    if (cdb.valid == false)
      return;
    for (int i = 0; i < RS_TOTAL; i++) {
      if (rs_new[i].busy == false)
        continue;
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
  void execute(class CDB &cdb) {
    listen_cdb(cdb.get_old());

    int best_idx = -1, best_pri = 999;
    for (int i = 0; i < RS_TOTAL; i++) {
      if (rs_new[i].busy && is_ready(i)) {
        int p = fu_to_priority(rs_new[i].fu);
        if (p < best_pri) { best_pri = p; best_idx = i; }
      }
    }
    if (best_idx == -1) return;

    uint32_t result = compute_result(best_idx);
    if (rs_new[best_idx].mem_read || rs_new[best_idx].mem_write) {
      rs_new[best_idx].busy = false;
      return;
    }
    cdb.broadcast(rs_new[best_idx].rob_id, result, best_pri);
    rs_new[best_idx].busy = false;
  }
  void update() {
    for (int i = 0; i < RS_TOTAL; i++) {
      rs[i] = rs_new[i];
    }
    for (int i = 0; i < 5; i++) {
      fu_busy_count[i] = fu_busy_count_new[i];
    }
  }
  void flush_after(int32_t rob_id) {
    for (int i = 0; i < RS_TOTAL; i++) {
      if (rs_new[i].busy && (int32_t)rs_new[i].rob_id >= rob_id) {
        rs_new[i].busy = false;
        rs_new[i].Qj = -1;
        rs_new[i].Qk = -1;
      }
    }
    for (int i = 0; i < RS_TOTAL; i++) {
      if (rs[i].busy && (int32_t)rs[i].rob_id >= rob_id) {
        rs[i].busy = false;
        rs[i].Qj = -1;
        rs[i].Qk = -1;
      }
    }
  }
};

#endif