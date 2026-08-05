#ifndef CPU_TOMASULO_HPP
#define CPU_TOMASULO_HPP

#include "memory.hpp"
#include "decoder.hpp"
#include "register.hpp"
#include "reservation_station.hpp"
#include "ROB.hpp"
#include "cdb.hpp"
#include "branch_predictor.hpp"

class CPU {
private:
    Memory               mem;
    RegisterFile         rf;
    ReservationStations  rs;
    ReorderBuffer        rob;
    CDB                  cdb;
    BranchPredictor      bp;

    uint32_t  pc;
    bool      halt_fetch;
    bool      halted;
    uint64_t  cycle;

public:
    void init(const char* filename) {
        mem.load(filename);
        rf.init();
        rs.init();
        rob.init();
        cdb.init();
        bp.init();

        pc         = 0;
        halt_fetch = false;
        halted     = false;
        cycle      = 0;
    }

    void step() {
        cdb.clear();

        rob.execute(cdb, rf, mem, bp);
        if (rob.need_flush()) {
            pc = rob.get_correct_pc();
            halt_fetch = false;
            int32_t fid = rob.get_flush_rob_id();
            rs.flush_after(fid + 1);
            rf.flush_after(fid + 1);
            for (int i = 0; i < CDB_COUNT; i++) {
                cdb.old[i].valid = false;
                cdb.new_[i].valid = false;
            }
        }

        rs.execute(cdb, rob, mem);

        rf.execute();

        cdb.execute();

        bp.execute(rob);

        if (!halt_fetch) {
            uint32_t inst = mem.fetch_inst(pc);
            if (inst == 0x0ff00513) {
                halt_fetch = true;
            } else {
                DecodedInst d = decode(inst, pc);
                bool predicted = false;
                if (d.opcode == JAL) {
                    predicted = true;
                } else if (d.opcode == JALR) {
                    predicted = true;
                } else if (d.is_branch) {
                    predicted = bp.predict(pc);
                }
                if (!rob.is_full() && !rob.has_pending_branch(cdb)
                    && !rs.is_full(d.fu_type)) {
                    int rob_id = rob.allocate(d, pc, predicted);
                    if (d.rd != 0) {
                        rf.set_reorder(d.rd, rob_id);
                    }
                    int rs_idx = rs.allocate(d.fu_type);
                    rs.issue(rs_idx, d, rob_id, rf, cdb, rob);
                    if (d.is_branch && predicted) {
                        pc = pc + d.imm;
                    } else {
                        pc = pc + 4;
                    }
                }
            }
        }

        rs.update();
        rob.update();
        rf.update();
        cdb.update();
        bp.update_bht();

        cycle++;
        if (halt_fetch && rob.is_empty()) {
            halted = true;
        }
    }

    int run() {
        while (!halted && cycle < 200000000) {
            step();
        }
        return (int)(rf.read(10) & 0xFF);
    }

    uint64_t clock_cycle() const { return cycle; }
    float branch_accuracy() const { return bp.accuracy(); }
};
#endif
