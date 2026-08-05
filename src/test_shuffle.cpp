#include "cpu.hpp"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <algorithm>

struct TestCase {
    const char* path;
    int expected;
};

TestCase tests[] = {
    {"data/sample/sample.data", 158},
    {"data/testcases/array_test1.data", 123},
    {"data/testcases/array_test2.data", 43},
    {"data/testcases/expr.data", 58},
    {"data/testcases/gcd.data", 178},
    {"data/testcases/lvalue2.data", 175},
    {"data/testcases/manyarguments.data", 40},
    {"data/testcases/multiarray.data", 115},
    {"data/testcases/naive.data", 94},
    {"data/testcases/statement_test.data", 50},
};
const int NUM_TESTS = sizeof(tests) / sizeof(tests[0]);

struct CPU_Shuffle : public CPU {
    void step_fetch_issue() {
        if (halt_fetch) return;
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
            if (!rob.is_full() && !rob.has_pending_branch()
                && !rs.is_full(d.fu_type)) {
                int rob_id = rob.allocate(d, pc, predicted);
                if (d.rd != 0) {
                    rf.set_reorder(d.rd, rob_id);
                }
                int rs_idx = rs.allocate(d.fu_type);
                rs.issue(rs_idx, d, rob_id, rf);
                if (d.is_branch && predicted) {
                    pc = pc + d.imm;
                } else {
                    pc = pc + 4;
                }
            }
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

    void step_rs_exec() {
        rs.execute(cdb, rob, mem);
    }

    void step_updates() {
        rs.update();
        rob.update();
        rf.update();
        cdb.update();
        bp.update_bht();
    }

    int run_order(const char* order) {
        while (!halted && cycle < 200000000) {
            for (int s = 0; order[s] != '\0'; s++) {
                char op = order[s];
                if (op == 'C') cdb.clear();
                else if (op == 'R') step_rob_exec();
                else if (op == 'S') step_rs_exec();
                else if (op == 'F') { rf.execute(); cdb.execute(); bp.execute(rob); step_fetch_issue(); }
                else if (op == 'U') step_updates();
                else if (op == 'H') {
                    if (halt_fetch && rob.is_empty()) { halted = true; }
                    cycle++;
                }
            }
            if (halted) break;
        }
        return (int)(rf.read(10) & 0xFF);
    }
};

int main() {
    const char* baseline_order = "CRSFUH";

    printf("=== Baseline (order = %s) ===\n", baseline_order);
    uint64_t baseline_cycles[NUM_TESTS];
    int baseline_results[NUM_TESTS];
    CPU_Shuffle cpu_base;

    for (int i = 0; i < NUM_TESTS; i++) {
        cpu_base.init(tests[i].path);
        int result = cpu_base.run_order(baseline_order);
        baseline_results[i] = result;
        baseline_cycles[i] = cpu_base.clock_cycle();
        printf("[%2d] %-35s got=%-4d expect=%-4d cycles=%-10lu %s\n",
               i + 1, tests[i].path, result, tests[i].expected,
               (unsigned long)baseline_cycles[i],
               result == tests[i].expected ? "PASS" : "FAIL");
    }

    printf("\n=== All 6 permutations of R/S/F ===\n");

    const char* perms[][2] = {
        {"CRSFUH", "rob->rs->fetch [baseline]"},
        {"CRFSUH", "rob->fetch->rs"},
        {"CSRFUH", "rs->rob->fetch"},
        {"CSFRUH", "rs->fetch->rob"},
        {"CFRSUH", "fetch->rob->rs"},
        {"CFSRUH", "fetch->rs->rob"},
    };

    int perm_cycle_diff[6] = {0};
    int perm_result_diff[6] = {0};

    for (int p = 0; p < 6; p++) {
        const char* order = perms[p][0];
        const char* desc  = perms[p][1];
        printf("\n--- %s ---\n", desc);

        for (int i = 0; i < NUM_TESTS; i++) {
            CPU_Shuffle cpu;
            cpu.init(tests[i].path);
            int result = cpu.run_order(order);
            uint64_t cyc = cpu.clock_cycle();

            bool ok_result = (result == tests[i].expected);
            bool ok_cycles = (cyc == baseline_cycles[i]);

            if (!ok_result) perm_result_diff[p]++;
            if (!ok_cycles)   perm_cycle_diff[p]++;

            const char* tag = "OK";
            if (!ok_result && !ok_cycles) tag = "BAD";
            else if (!ok_result) tag = "RES";
            else if (!ok_cycles) tag = "CYC";

            printf("[%2d] %-30s got=%-4d cyc=%-8lu %s (baseline=%lu)\n",
                   i + 1, tests[i].path, result, (unsigned long)cyc,
                   tag, (unsigned long)baseline_cycles[i]);
        }
        printf("  >> result mismatches: %d, cycle mismatches: %d\n",
               perm_result_diff[p], perm_cycle_diff[p]);
    }

    printf("\n========================================\n");
    printf("=== SUMMARY ===\n");
    printf("========================================\n");

    bool any_issue = false;
    for (int p = 0; p < 6; p++) {
        printf("[%s] %-25s result_diff=%d  cycle_diff=%d\n",
               perms[p][0], perms[p][1],
               perm_result_diff[p], perm_cycle_diff[p]);
        if (perm_result_diff[p] > 0 || perm_cycle_diff[p] > 0)
            any_issue = true;
    }

    if (any_issue) {
        printf("\nISSUES FOUND: Module execution order affects correctness or cycle count.\n");
        printf("This violates the hardware logic principle that ordering should be swappable.\n\n");
        printf("Root causes:\n");
        printf("\n1. [rs.execute() reads rs_new instead of rs]\n");
        printf("   In reservation_station.hpp, execute() checks is_ready(i) which reads\n");
        printf("   rs_new[i].Qj/Qk. When fetch/issue runs BEFORE rs.execute(), newly-issued\n");
        printf("   instructions with ready operands can dispatch in the SAME cycle.\n");
        printf("   This changes cycle counts (e.g., order CRFSUH shows fewer cycles).\n");
        printf("\n2. [rf.write() unconditionally clears reorder_id_new]\n");
        printf("   In register.hpp, write() sets reorder_id_new[idx] = -1 even when\n");
        printf("   it may have been already set to a new value by fetch/issue this cycle.\n");
        printf("   If fetch->issue runs before rob->commit for the same destination register,\n");
        printf("   the RAT mapping for the younger instruction is lost.\n");
        printf("\n3. [Flush ordering: rob.execute must come before rs.execute]\n");
        printf("   rob.execute detects branch mispredictions and triggers flush.\n");
        printf("   If rs.execute runs before the flush, mis-speculated instructions may dispatch.\n");
    } else {
        printf("\nOK: All permutations give consistent results and cycle counts.\n");
    }

    return any_issue ? 1 : 0;
}
