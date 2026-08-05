#ifndef REORDER_BUFFER_HPP
#define REORDER_BUFFER_HPP

#include "cdb.hpp"
#include "register.hpp"
#include "types.hpp"
#include "memory.hpp"
#include "branch_predictor.hpp"

struct ROBEntry {
  bool busy;
  uint32_t pc;
  uint8_t dest_reg;
  uint32_t value;
  bool ready;
  bool is_branch;
  bool is_jalr;
  bool predicted_taken;
  bool actually_taken;
  uint32_t branch_target;
  bool is_load;
  bool is_store;
  uint32_t store_addr;
  uint32_t store_data;
  uint8_t store_size;
};

class ReorderBuffer {
private:
  static const int SIZE = ROB_SIZE;

  ROBEntry buffer[SIZE];
  ROBEntry buffer_new[SIZE];

  int head, tail;
  int head_new, tail_new;

  bool flush_signal;
  uint32_t correct_pc;
  int32_t  flush_rob_id;

  void listen_cdb(const CDBValue &cdb) {
     if (cdb.valid == false) return ;
     int i = cdb.rob_id ;
     if (!buffer_new[i].busy) return ;
     if (buffer_new[i].is_store) {
         buffer_new[i].ready = true;
         return;
     }
     buffer_new[i].value = cdb.value;
     buffer_new[i].ready = true;
  }
  void listen_cdb_all(CDB &cdb) {
     for (int s = 0; s < CDB_COUNT; s++) {
       if (cdb.has_data(s)) listen_cdb(cdb.get_old(s));
     }
  }
  void flush_after(int rob_id) {
    int i = (rob_id + 1) % SIZE ;
    for (int j = 1; j < SIZE ;j++) {
      if (i == tail) break ;
      buffer_new[i].busy  = false;
      buffer_new[i].ready = false;
      i = (i + 1) % SIZE;
    }
    tail_new = (rob_id + 1) % SIZE ;
    i = (rob_id + 1) % SIZE ;
    for (int j = 1; j < SIZE ;j++) {
      if (i == tail) break ;
      buffer[i].busy  = false;
      buffer[i].ready = false;
      i = (i + 1) % SIZE;
    }
  }

public:
  void init() {
    head = head_new = 0;
    tail = tail_new = 0;
    for (int i = 0; i < ROB_SIZE; i++) {
      buffer[i].busy = buffer_new[i].busy = false;
      buffer[i].ready = buffer_new[i].ready = false;
    }
  }

  // wires (const)
  bool is_full() const { return (tail_new + 1) % ROB_SIZE == head_new; }
  bool is_empty() const { return head == tail; }
  bool head_ready() const { 
    if (head == tail) return false ;
    return buffer[head].ready; 
  }
  bool need_flush() const { return flush_signal; }
  uint32_t get_correct_pc() const { return correct_pc; }
  int32_t  get_flush_rob_id() const { return flush_rob_id; }
  int debug_head() const { return head; }
  int debug_tail() const { return tail; }
  bool has_pending_branch() const {
    if (head_new == tail_new) return false;
    if (buffer_new[head_new].busy && buffer_new[head_new].is_branch
        && !buffer_new[head_new].ready)
        return true;
    return false;
  }

  bool forward_store(uint32_t addr, uint8_t size, int32_t load_rob_id,
                     uint32_t &fwd_data) const {
    int i = head_new;
    while (i != tail_new) {
      if (i == load_rob_id) break;
      if (buffer_new[i].busy && buffer_new[i].is_store
          && buffer_new[i].ready
          && buffer_new[i].store_addr == addr
          && buffer_new[i].store_size == size) {
        fwd_data = buffer_new[i].store_data;
        return true;
      }
      i = (i + 1) % SIZE;
    }
    return false;
  }

  // ports (每周期调用上限 = 1)
  int allocate(const DecodedInst &inst, uint32_t pc, bool predicted_taken) {
    int id = tail;
    ROBEntry &e = buffer_new[id];
    e.busy = true;
    e.pc = pc;
    e.dest_reg = inst.rd;
    e.value = 0;
    e.ready = false;
    e.is_branch = inst.is_branch;
    e.is_jalr = (inst.opcode == JALR);
    e.predicted_taken = predicted_taken;
    e.actually_taken = false;
    e.branch_target = 0;
    e.is_load = inst.is_mem_read;
    e.is_store = inst.is_mem_write;
    e.store_addr = 0;
    e.store_data = 0;
    e.store_size = 0;
    tail_new = (tail + 1) % SIZE; 
    return id;
  }
  void set_store_info(int rob_id, uint32_t addr, uint32_t data, uint8_t size) {
    buffer_new[rob_id].store_addr = addr;
    buffer_new[rob_id].store_data = data;
    buffer_new[rob_id].store_size = size;
    buffer_new[rob_id].ready = true;
  }
  void set_load_result(int rob_id, uint32_t value) {
    buffer_new[rob_id].value = value;
    buffer_new[rob_id].ready = true;
  }
  void set_branch_info(int rob_id, bool taken, uint32_t target) {
    buffer_new[rob_id].actually_taken = taken;
    buffer_new[rob_id].branch_target  = target;
  }
  void execute(CDB &cdb, RegisterFile &rf, Memory &mem,
               BranchPredictor &bp) {
    flush_signal = false;
    listen_cdb_all(cdb);
    if (head == tail) return ;
    if (buffer[head].ready) { commit_head(rf, mem, bp, false); return; }
    if (buffer_new[head].ready) { commit_head(rf, mem, bp, true); return; }
  }
  void commit_head(RegisterFile &rf, Memory &mem, BranchPredictor &bp,
                   bool use_new) {
    int hd = head;
    bool is_b = use_new ? buffer_new[hd].is_branch : buffer[hd].is_branch;
    bool is_jr = use_new ? buffer_new[hd].is_jalr    : buffer[hd].is_jalr;
    bool is_s = use_new ? buffer_new[hd].is_store   : buffer[hd].is_store;
    uint8_t  dr = use_new ? buffer_new[hd].dest_reg : buffer[hd].dest_reg;
    uint32_t vl = use_new ? buffer_new[hd].value    : buffer[hd].value;
    bool   pred = use_new ? buffer_new[hd].predicted_taken : buffer[hd].predicted_taken;
    bool   actu = use_new ? buffer_new[hd].actually_taken  : buffer[hd].actually_taken;
    uint32_t bt = use_new ? buffer_new[hd].branch_target   : buffer[hd].branch_target;
    uint32_t bpc= use_new ? buffer_new[hd].pc              : buffer[hd].pc;
    uint32_t sa = use_new ? buffer_new[hd].store_addr      : buffer[hd].store_addr;
    uint32_t sd = use_new ? buffer_new[hd].store_data      : buffer[hd].store_data;
    uint8_t  ss = use_new ? buffer_new[hd].store_size      : buffer[hd].store_size;

    if (is_b) {
      bp.update(bpc, actu);
      if (dr != 0) rf.write(dr, vl, hd);
      if (is_jr || pred != actu) {
        flush_after(hd);
        flush_signal = true;
        flush_rob_id = hd;
        correct_pc = actu ? bt : bpc + 4;
      }
      buffer_new[hd].busy = false;
      head_new = (hd + 1) % SIZE;
      return;
    }
    if (is_s) {
      mem.write(sa, sd, ss);
      buffer_new[hd].busy = false;
      head_new = (hd + 1) % SIZE;
      return;
    }
    if (dr != 0) rf.write(dr, vl, hd);
    buffer_new[hd].busy = false;
    head_new = (hd + 1) % SIZE;
  }
  void update() {
    for (int i = 0;i < SIZE;i++) {
      buffer[i] = buffer_new[i] ;
    }
    head = head_new ;
    tail = tail_new ;
    flush_signal = false ;
  }
};

#endif