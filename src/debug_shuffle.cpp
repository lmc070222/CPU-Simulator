#include "cpu.hpp"
#include <cstdio>
#include <cstring>

struct CPU_Shuffle : public CPU {
    void step_fetch_issue() {
        if (halt_fetch) return;
        uint32_t inst = mem.fetch_inst(pc);
        if (inst == 0x0ff00513) { halt_fetch = true; return; }
        DecodedInst d = decode(inst, pc);
        bool predicted = d.opcode == JAL || d.opcode == JALR;
        if (!predicted && d.is_branch) predicted = bp.predict(pc);
        if (!rob.is_full() && !rob.has_pending_branch() && !rs.is_full(d.fu_type)) {
            int rid = rob.allocate(d, pc, predicted);
            if (d.rd != 0) rf.set_reorder(d.rd, rid);
            int rs_idx = rs.allocate(d.fu_type);
            rs.issue(rs_idx, d, rid, rf);
            pc = (d.is_branch && predicted) ? pc + d.imm : pc + 4;
        }
    }
    void step_rob_exec() {
        rob.execute(cdb, rf, mem, bp);
        if (rob.need_flush()) {
            pc = rob.get_correct_pc();
            halt_fetch = false;
            int32_t fid = rob.get_flush_rob_id();
            rs.flush_after(fid + 1);
            rf.flush_after(fid + 1);
        }
    }
    void step_rs_exec() { rs.execute(cdb, rob, mem); }
    void step_updates() { rs.update(); rob.update(); rf.update(); cdb.update(); bp.update_bht(); }

    int run_order(const char* order, int max_cycles = 5000000) {
        while (!halted && cycle < max_cycles) {
            for (int s = 0; order[s]; s++) {
                char op = order[s];
                if (op == 'C') cdb.clear();
                else if (op == 'R') step_rob_exec();
                else if (op == 'S') step_rs_exec();
                else if (op == 'F') { rf.execute(); cdb.execute(); bp.execute(rob); step_fetch_issue(); }
                else if (op == 'U') step_updates();
                else if (op == 'H') { if (halt_fetch && rob.is_empty()) halted = true; cycle++; }
            }
            if (halted) break;
        }
        return (int)(rf.read(10) & 0xFF);
    }
};

const char* test_paths[] = {
    "data/sample/sample.data",
    "data/testcases/naive.data",
    "data/testcases/array_test1.data",
    "data/testcases/array_test2.data",
    "data/testcases/lvalue2.data",
    "data/testcases/manyarguments.data",
    "data/testcases/expr.data",
    "data/testcases/gcd.data",
    "data/testcases/statement_test.data",
    "data/testcases/multiarray.data",
};
const int expected[] = {158, 94, 123, 43, 175, 40, 58, 178, 50, 115};
const int NUM = 10;

const char* orders[] = {"CRSFUH","CRFSUH","CSRFUH","CSFRUH","CFRSUH","CFSRUH"};
const char* descs[]  = {"rob>rs>ft","rob>ft>rs","rs>rob>ft","rs>ft>rob","ft>rob>rs","ft>rs>rob"};

int main() {
    for (int ti = 0; ti < NUM; ti++) {
        uint64_t baseline_cyc = 0;
        bool first = true;
        printf("%-35s:", test_paths[ti]);

        for (int oi = 0; oi < 6; oi++) {
            CPU_Shuffle cpu;
            cpu.init(test_paths[ti]);
            int r = cpu.run_order(orders[oi], 20000000);
            unsigned long cyc = cpu.clock_cycle();

            if (first) {
                baseline_cyc = cyc;
                first = false;
            }

            if (cyc >= 20000000) {
                printf(" %s=HUNG", descs[oi]);
            } else if (r != expected[ti]) {
                printf(" %s=%d(ERR)", descs[oi], r);
            } else if (cyc != baseline_cyc) {
                printf(" %s=%lu(CYC)", descs[oi], cyc);
            } else {
                printf(" %s=OK", descs[oi]);
            }
            fflush(stdout);
        }
        printf("\n");
    }
    return 0;
}
