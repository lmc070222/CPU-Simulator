#ifndef RESERVATION_STATION_HPP
#define RESERVATION_STATION_HPP

#include "cdb.hpp"
#include "register.hpp"
#include "types.hpp"
#include "memory.hpp"
#include "ALU.hpp"
#include "ROB.hpp"
struct RSEntry {
  bool busy = false;
  bool just_issued = false;
  FUType fu;
  ALUOp alu_op;
  uint32_t Vj, Vk;
  int32_t Qj = -1, Qk = -1;
  uint32_t imm;//立即数
  uint8_t dest;//目标寄存器
  uint8_t rob_id;
  uint32_t pc;
  bool mem_read, mem_write;
  uint8_t mem_size; // 1=byte, 2=half, 4=word
  bool is_branch;
  Opcode op;
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
    return alu_compute(e.alu_op, e.Vj, e.Vk, e.imm);
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
      rs[i].just_issued = rs_new[i].just_issued = false;
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
      if (rs[i].busy == false)
        return i;
    }
    return -1;
  }
  bool is_full(FUType fu) {
    int start, end;
    get_range(fu, start, end);
    for (int i = start; i < end; i++) {
      if (rs[i].busy == false) return false;
    }
    return true;
  }
  void issue(int idx, const DecodedInst &inst, int rob_id,
             const class RegisterFile &rf, const class CDB &cdb,
             const class ReorderBuffer &rob) {
    RSEntry &e = rs_new[idx];
    e.busy = true;
    e.just_issued = true;
    e.fu = inst.fu_type;
    e.alu_op = inst.alu_op;
    e.imm = inst.imm;
    e.dest = inst.rd;
    e.rob_id = rob_id;
    e.pc = inst.pc;
    e.mem_read = inst.is_mem_read;
    e.mem_write = inst.is_mem_write;
    e.is_branch = inst.is_branch;
    e.op = inst.opcode;

    e.Vj = rf.read(inst.rs1);
    e.Qj = rf.get_reorder(inst.rs1);
    if (e.Qj != -1) {
      for (int s = 0; s < CDB_COUNT; s++) {
        if (cdb.has_data(s) && (int32_t)cdb.get_old(s).rob_id == e.Qj) {
          e.Vj = cdb.get_old(s).value;
          e.Qj = -1;
          break;
        }
      }
      if (e.Qj != -1 && rob.has_value(e.Qj)) {
        e.Vj = rob.get_value(e.Qj);
        e.Qj = -1;
      }
      if (e.Qj != -1) e.Vj = 0;
    }
    if (inst.opcode == AUIPC) {
      e.Vj = inst.pc;
      e.Qj = -1;
    }
    if (inst.opcode == JAL) {
      e.Vj = inst.pc;
      e.Qj = -1;
    }

    e.Vk = rf.read(inst.rs2);
    e.Qk = rf.get_reorder(inst.rs2);
    if (e.Qk != -1) {
      for (int s = 0; s < CDB_COUNT; s++) {
        if (cdb.has_data(s) && (int32_t)cdb.get_old(s).rob_id == e.Qk) {
          e.Vk = cdb.get_old(s).value;
          e.Qk = -1;
          break;
        }
      }
      if (e.Qk != -1 && rob.has_value(e.Qk)) {
        e.Vk = rob.get_value(e.Qk);
        e.Qk = -1;
      }
      if (e.Qk != -1) e.Vk = 0;
    }

    if (inst.type == I_TYPE || inst.type == U_TYPE || inst.type == J_TYPE) {
      e.Qk = -1;
      e.Vk = e.imm;
    }

    if (inst.opcode == LUI) {
      e.Qj = -1;
      e.Vj = 0;
    }

    if (inst.is_mem_read || inst.is_mem_write) {
      if (inst.opcode == LB || inst.opcode == LBU || inst.opcode == SB)
        e.mem_size = 1;
      else if (inst.opcode == LH || inst.opcode == LHU || inst.opcode == SH)
        e.mem_size = 2;
      else
        e.mem_size = 4;
    }
  }

  bool is_ready(int idx) {
    return rs_new[idx].busy && !rs_new[idx].just_issued
        && rs_new[idx].Qj == -1 && rs_new[idx].Qk == -1;
  }
  void listen_cdb(const CDBValue &cdb) {
    if (cdb.valid == false) return;
    for (int i = 0; i < RS_TOTAL; i++) {
      if (rs_new[i].busy == false) continue;
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
  void dispatch_one(CDB &cdb, ReorderBuffer &rob,
                     Memory &mem, int idx) {
    if (idx == -1) return;
    uint32_t result = compute_result(idx);

    if (rs_new[idx].mem_read) {
      uint32_t addr = result;
      uint32_t load_val = 0;
      uint32_t fwd_data = 0;
      bool forwarded = rob.forward_store(addr, rs_new[idx].mem_size,
                                          rs_new[idx].rob_id, fwd_data);
      if (!forwarded) {
        for (int k = 0; k < RS_TOTAL; k++) {
          if (rs_new[k].busy && rs_new[k].mem_write
              && (int32_t)rs_new[k].rob_id < (int32_t)rs_new[idx].rob_id
              && rs_new[k].Qj == -1 && rs_new[k].Qk == -1
              && (rs_new[k].Vj + rs_new[k].imm) == addr
              && rs_new[k].mem_size == rs_new[idx].mem_size) {
            forwarded = true;
            fwd_data = rs_new[k].Vk;
            break;
          }
        }
      }
      if (forwarded) {
        load_val = fwd_data;
      } else {
        switch (rs_new[idx].mem_size) {
          case 1: load_val = mem.read(addr, 1); break;
          case 2: load_val = mem.read(addr, 2); break;
          default:load_val = mem.read(addr, 4); break;
        }
        if (rs_new[idx].op == LB) {
          if (load_val & 0x80) load_val |= 0xFFFFFF00;
        } else if (rs_new[idx].op == LH) {
          if (load_val & 0x8000) load_val |= 0xFFFF0000;
        }
      }
      cdb.broadcast(rs_new[idx].rob_id, load_val);
      rs_new[idx].busy = false;
      rs_new[idx].just_issued = false;
      return;
    }
    if (rs_new[idx].mem_write) {
      uint32_t addr = rs_new[idx].Vj + rs_new[idx].imm;
      uint32_t data = rs_new[idx].Vk;
      rob.set_store_info(rs_new[idx].rob_id, addr, data, rs_new[idx].mem_size);
      rs_new[idx].busy = false;
      rs_new[idx].just_issued = false;
      return;
    }
    if (rs_new[idx].is_branch) {
      uint32_t target; bool taken;
      Opcode op = rs_new[idx].op;
      uint32_t reg_val = 0;
      if (op == JAL || op == JALR) {
        target = result; taken = true; reg_val = rs_new[idx].pc + 4;
      } else {
        target = rs_new[idx].pc + rs_new[idx].imm;
        if (op == BEQ)      taken = (result == 0);
        else if (op == BNE) taken = (result != 0);
        else if (op == BLT || op == BLTU) taken = (result != 0);
        else                taken = (result == 0);
      }
      rob.set_branch_info(rs_new[idx].rob_id, taken, target);
      cdb.broadcast(rs_new[idx].rob_id, reg_val);
      rob.cache_dispatched_value(rs_new[idx].rob_id, reg_val);
      rs_new[idx].busy = false;
      rs_new[idx].just_issued = false;
      return;
    }
    cdb.broadcast(rs_new[idx].rob_id, result);
    rob.cache_dispatched_value(rs_new[idx].rob_id, result);
    rs_new[idx].busy = false;
    rs_new[idx].just_issued = false;
  }
  int find_ready(int start, int end) {
    for (int i = start; i < end; i++)
      if (rs_new[i].busy && is_ready(i)) return i;
    return -1;
  }
  void listen_cdb_all(CDB &cdb) {
    for (int s = 0; s < CDB_COUNT; s++) {
      if (!cdb.has_data(s)) continue;
      listen_cdb(cdb.get_old(s));
    }
  }
  void execute(class CDB &cdb, class ReorderBuffer &rob,
               Memory &mem) {
    listen_cdb_all(cdb);
    int dispatched = 0;
    for (int pri = 0; pri < 5 && dispatched < CDB_COUNT; pri++) {
      int best = -1;
      int best_rob = 999;
      for (int i = 0; i < RS_TOTAL; i++) {
        if (rs_new[i].busy && is_ready(i)) {
          if ((int)fu_to_priority(rs_new[i].fu) == pri) {
            int r = rs_new[i].rob_id;
            if (r < best_rob) {
              best_rob = r; best = i;
            }
          }
        }
      }
      if (best != -1) {
        dispatch_one(cdb, rob, mem, best);
        dispatched++;
      }
    }
  }
  void update() {
    for (int i = 0; i < RS_TOTAL; i++) {
      rs[i] = rs_new[i];
      rs_new[i].just_issued = false;
    }
    for (int i = 0; i < 5; i++) {
      fu_busy_count[i] = fu_busy_count_new[i];
    }
  }
  void flush_after(int32_t rob_id) {
    for (int i = 0; i < RS_TOTAL; i++) {
      if (rs_new[i].busy && (int32_t)rs_new[i].rob_id >= rob_id) {
        rs_new[i].busy = false;
        rs_new[i].just_issued = false;
        rs_new[i].Qj = -1;
        rs_new[i].Qk = -1;
      }
    }
    for (int i = 0; i < RS_TOTAL; i++) {
      if (rs[i].busy && (int32_t)rs[i].rob_id >= rob_id) {
        rs[i].busy = false;
        rs[i].just_issued = false;
        rs[i].Qj = -1;
        rs[i].Qk = -1;
      }
    }
  }
};

#endif