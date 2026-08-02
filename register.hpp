#ifndef REGISTER_FILE_HPP
#define REGISTER_FILE_HPP

#include "cpu.hpp"

class RegisterFile {
private:
    const static int REG_COUNT = 32;
    uint32_t reg[REG_COUNT];
    uint32_t reg_new[REG_COUNT];
    int32_t  reorder_id[REG_COUNT];
    int32_t  reorder_id_new[REG_COUNT];
    bool     busy[REG_COUNT];
    bool     busy_new[REG_COUNT];

public:
    void init() {
        for (int i = 0; i < REG_COUNT; i++) {
            reg[i] = reg_new[i] = 0;
            reorder_id[i] = reorder_id_new[i] = -1;
            busy[i] = busy_new[i] = false;
        }
    }

    uint32_t read(uint8_t idx) const {
        if (idx == 0) return 0;
        return reg[idx];
    }

    int32_t get_reorder(uint8_t idx) const {
        if (idx == 0) return -1;
        return reorder_id[idx];
    }

    bool is_busy(uint8_t idx) {
        if (idx == 0) return false;
        return busy[idx];
    }

    void set_reorder(uint8_t idx, int32_t rob_id) {
        if (idx == 0) return;
        reorder_id_new[idx] = rob_id;
        busy_new[idx] = true;
    }

    void write(uint8_t idx, uint32_t val, int32_t rob_id) {
    if (idx == 0) return;
    if (reorder_id_new[idx] == rob_id) { 
        reorder_id_new[idx] = -1;
        busy_new[idx] = false;
        reg_new[idx] = val;
    }
}

    void restore_mapping(uint8_t idx, int32_t prev_rob_id) {
        if (idx == 0) return;
        reorder_id[idx] = reorder_id_new[idx] = prev_rob_id;
        busy[idx] = busy_new[idx] = (prev_rob_id != -1);
    }

    void flush_after(int32_t rob_id) {
        for (int i = 1; i < REG_COUNT; i++) {
            if (reorder_id[i] >= rob_id) {
                reorder_id[i] = reorder_id_new[i] = -1;
                busy[i] = busy_new[i] = false;
            }
        }
        reorder_id[0] = reorder_id_new[0] = -1;
        busy[0] = busy_new[0] = false;
    }

    void execute() {
        reg_new[0] = 0;
        reorder_id_new[0] = -1;
        busy_new[0] = false;
    }

    void update() {
        for (int i = 0; i < REG_COUNT; i++) {
            reg[i] = reg_new[i];
            reorder_id[i] = reorder_id_new[i];
            busy[i] = busy_new[i];
        }
        reg[0] = reg_new[0] = 0;
        reorder_id[0] = reorder_id_new[0] = -1;
        busy[0] = busy_new[0] = false;
    }
};

#endif
