#ifndef LOAD_STORE_BUFFER_HPP
#define LOAD_STORE_BUFFER_HPP
#include "ROB.hpp"
#include "cdb.hpp"
#include "memory.hpp"
#include "types.hpp"
#include <cstdint>

class CDB;
class ReorderBuffer;

struct LSBEntry {
  bool busy;
  bool is_load;
  uint32_t addr;
  uint32_t value;
  int delay;
  uint8_t rob_id;
  uint8_t size;
  bool sign_ext;
  bool addr_ready;
  bool data_ready;
  bool completed;
};

class LoadStoreBuffer {
private:
  LSBEntry buffer[LSB_SIZE];
  LSBEntry buffer_new[LSB_SIZE];
  int store_queue[LSB_SIZE];
  int store_queue_new[LSB_SIZE];
  int store_count;
  int store_count_new;

  bool can_forward(uint32_t load_addr, int load_lsb_idx,
                   uint32_t &fwd_data) const {
    for (int i = 0; i < LSB_SIZE; i++) {
      int s_idx = store_queue[i];
      if (s_idx == load_lsb_idx)
        break;
      if (buffer[s_idx].busy && buffer[s_idx].addr_ready &&
          buffer[s_idx].data_ready && buffer[s_idx].addr == load_addr) {
        fwd_data = buffer[s_idx].value;
        return true;
      }
    }
    return false;
  }

public:
  void init() {
    for (int i = 0; i < LSB_SIZE; i++) {
      buffer[i].busy = buffer_new[i].busy = false;
      store_queue[i] = store_queue_new[i] = -1;
    }
    store_count = store_count_new = 0;
  }
  bool is_full() const {
    for (int i = 0; i < LSB_SIZE; i++) {
      if (!buffer_new[i].busy)
        return false;
    }
    return true;
  }

  int allocate() {
    for (int i = 0; i < LSB_SIZE; i++) {
      if (!buffer_new[i].busy) {
        buffer_new[i].busy = true;
        buffer_new[i].addr_ready = false;
        buffer_new[i].data_ready = false;
        buffer_new[i].completed = false;
        buffer_new[i].delay = 0;
        return i;
      }
    }
    return -1;
  }
  void set_addr(int idx, uint32_t addr, uint8_t size, bool sign_ext,
                uint8_t rob_id, bool is_load) {
    buffer_new[idx].addr = addr;
    buffer_new[idx].addr_ready = true;
    buffer_new[idx].sign_ext = sign_ext;
    buffer_new[idx].size = size;
    buffer_new[idx].rob_id = rob_id;
    buffer_new[idx].is_load = is_load;
    if (is_load) {
      buffer_new[idx].delay = 3;
    } else {
      store_queue_new[store_count_new] = idx;
      store_count_new++;
    }
  }
  void set_store_data(int idx, uint32_t data) {
    buffer_new[idx].value = data;
    buffer_new[idx].data_ready = true;
  }

  void execute(Memory &mem, ReorderBuffer &rob) {
    for (int i = 0; i < LSB_SIZE; i++) {
      LSBEntry &e = buffer_new[i];
      if (!e.busy || e.completed) continue;
      if (e.is_load && e.addr_ready) {
        if (e.delay > 0) { e.delay--; }
        if (e.delay == 0 && !e.completed) {
          uint32_t load_val;
          uint32_t fwd_data;
          if (can_forward(e.addr, i, fwd_data)) {
            load_val = fwd_data;
          } else {
            load_val = mem.read(e.addr, e.size);
            if (e.sign_ext && e.size < 4) {
              uint32_t mask = (1u << (e.size * 8)) - 1;
              if (load_val & (1u << (e.size * 8 - 1)))
                load_val |= ~mask;
            }
          }
          e.value = load_val;
          e.completed = true;
          rob.set_load_result(e.rob_id, load_val);
        }
      }
      if (!e.is_load && e.addr_ready && e.data_ready && !e.completed) {
        rob.set_store_info(e.rob_id, e.addr, e.value, e.size);
        e.completed = true;
      }
    }
  }
  void commit_store(int rob_id, Memory &mem) {
    for (int i = 0; i < LSB_SIZE; i++) {
      LSBEntry &e = buffer_new[i];
      if (e.busy && !e.is_load && e.rob_id == rob_id && e.completed) {
        mem.write(e.addr, e.value, e.size);
        e.busy = false;
        for (int j = 0; j < store_count; j++) {
          if (store_queue_new[j] == i) {
            for (int k = j; k < store_count_new - 1; k++)
              store_queue_new[k] = store_queue_new[k + 1];
            store_count_new--;
            break;
          }
        }
        return;
      }
    }
  }
  void update() {
    for (int i = 0; i < LSB_SIZE; i++) {
      buffer[i] = buffer_new[i];
      store_queue[i] = store_queue_new[i];
    }
    store_count = store_count_new;
  }
  void flush_after(int32_t rob_id) {
    for (int i = 0; i < LSB_SIZE; i++) {
      if (buffer_new[i].busy && buffer_new[i].rob_id >= rob_id) {
        if (!buffer_new[i].is_load) {
          for (int j = 0; j < store_count_new; j++) {
            if (store_queue_new[j] == i) {
              for (int k = j; k < store_count_new - 1; k++)
                store_queue_new[k] = store_queue_new[k + 1];
              store_count_new--;
              break;
            }
          }
        }
        buffer_new[i].busy = false;
      }
    }
    for (int i = 0; i < LSB_SIZE; i++) {
      if (buffer[i].busy && buffer[i].rob_id >= rob_id) {
        buffer[i].busy = false;
      }
    }
    for (int i = 0; i < LSB_SIZE; i++)
      store_queue[i] = store_queue_new[i];
    store_count = store_count_new;
  }
};

#endif