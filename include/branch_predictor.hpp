#ifndef BRANCH_PREDICTOR_HPP
#define BRANCH_PREDICTOR_HPP

#include "types.hpp"

class BranchPredictor {
private:
    uint8_t  bht[BHT_SIZE];
    uint8_t  bht_new[BHT_SIZE];
    uint32_t predict_count;
    uint32_t predict_correct;
    uint32_t predict_count_new;
    uint32_t predict_correct_new;

public:
    void init() {
        for (int i = 0; i < BHT_SIZE; i++)
            bht[i] = bht_new[i] = 1;
        predict_count = predict_correct = 0;
        predict_count_new = predict_correct_new = 0;
    }

    bool predict(uint32_t pc) const {
        int idx = (pc >> 2) % BHT_SIZE;
        return bht[idx] >= 2;
    }

    void update(uint32_t pc, bool taken) {
        int idx = (pc >> 2) % BHT_SIZE;
        bool pred = bht[idx] >= 2;
        predict_count_new++;
        if (pred == taken) predict_correct_new++;
        if (taken) {
            if (bht_new[idx] < 3) bht_new[idx]++;
        } else {
            if (bht_new[idx] > 0) bht_new[idx]--;
        }
    }

    void execute(class ReorderBuffer&) {}

    void update_bht() {
        for (int i = 0; i < BHT_SIZE; i++)
            bht[i] = bht_new[i];
        predict_count = predict_count_new;
        predict_correct = predict_correct_new;
    }

    float accuracy() const {
        if (predict_count == 0) return 1.0f;
        return (float)predict_correct / (float)predict_count;
    }
};

#endif
