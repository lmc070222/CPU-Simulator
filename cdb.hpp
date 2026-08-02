#ifndef CDB_HPP
#define CDB_HPP

#include "types.hpp"
#include <cstdint>

class CDB {
private:
  CDBValue old;
  CDBValue new_;
  struct BroadcastRequest {
    uint8_t rob_id;
    uint32_t value;
    int priority;
  };
  BroadcastRequest pending[CDB_COUNT + 2];
  int pending_count;

public:
  void init() {
    old.valid = false;
    new_.valid = false;
    old.rob_id = new_.rob_id = old.value = new_.value = 0;
    pending_count = 0;
  }
  void clear() {
    new_.valid = false ;
    pending_count = 0;
  }
  const CDBValue& get_old() const {
    return old ;
  }
  bool has_data () {
    return old.valid ;
  }
  bool broadcast(uint8_t rob_id, uint32_t value, int priority) {
    if (pending_count >= CDB_COUNT + 2) return false ;
    pending[pending_count].rob_id = rob_id ;
    pending[pending_count].value = value ;
    pending[pending_count].priority = priority ;
    pending_count++;
    return true ;
  }
  bool compare (const BroadcastRequest& a , const BroadcastRequest& b) {
    return a.priority < b.priority or (a.priority == b.priority and a.rob_id < b.rob_id) ;
  }
  void execute () {
    int max_idx = 0 ;
    if (pending_count >= 2){if (compare(pending[1], pending[0])) max_idx = 1;}
    if (pending_count >= 3) {if (compare(pending[2], pending[1])) max_idx = 2;}
    new_.rob_id = pending[max_idx].rob_id ;
    new_.valid = true ;
    new_.value = pending[max_idx].value ;
  }
  void update () {
    old = new_ ;
  }
};

#endif