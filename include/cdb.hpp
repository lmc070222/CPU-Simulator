#ifndef CDB_HPP
#define CDB_HPP

#include "types.hpp"
#include <cstdint>

class CDB {
public:
  CDBValue old[CDB_COUNT];
  CDBValue new_[CDB_COUNT];
  int new_slot;

  void init() {
    for (int i = 0; i < CDB_COUNT; i++) {
      old[i].valid = false;
      new_[i].valid = false;
      old[i].rob_id = new_[i].rob_id = old[i].value = new_[i].value = 0;
    }
    new_slot = 0;
  }
  void clear() {
    for (int i = 0; i < CDB_COUNT; i++)
      new_[i].valid = false;
    new_slot = 0;
  }
  const CDBValue& get_old(int slot) const { return old[slot]; }
  bool has_data(int slot) const { return old[slot].valid; }

  bool broadcast(uint8_t rob_id, uint32_t value) {
    if (new_slot >= CDB_COUNT) return false;
    new_[new_slot].rob_id = rob_id;
    new_[new_slot].value = value;
    new_[new_slot].valid = true;
    new_slot++;
    return true;
  }
  void execute() {}
  void update() {
    for (int i = 0; i < CDB_COUNT; i++)
      old[i] = new_[i];
  }
};

#endif
