#ifndef MEMORY_HPP
#define MEMORY_HPP

#include "types.hpp"
#include <fstream>
#include <string>
#include <sstream>
#include <cstring>

class Memory {
private:
    uint8_t mem[MEM_SIZE];

public:
    void load(const char* filename) {
        memset(mem, 0, MEM_SIZE);
        std::ifstream f(filename);
        std::string line;
        uint32_t addr = 0;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            if (line[0] == '@') {
                addr = std::stoll(line.substr(1), nullptr, 16);
            } else {
                std::stringstream ss(line);
                std::string byte_str;
                while (ss >> byte_str) {
                    if (byte_str.size() == 2) {
                        mem[addr++] = std::stoul(byte_str, nullptr, 16);
                    }
                }
            }
        }
    }

    uint32_t fetch_inst(uint32_t pc) const {
        return mem[pc] | (mem[pc+1] << 8) | (mem[pc+2] << 16) | (mem[pc+3] << 24);
    }

    uint32_t read(uint32_t addr, int size) const {
        addr &= (MEM_SIZE - 1);
        if (size == 1) return mem[addr];
        if (size == 2) return mem[addr] | (mem[addr+1] << 8);
        return mem[addr] | (mem[addr+1] << 8) | (mem[addr+2] << 16) | (mem[addr+3] << 24);
    }

    void write(uint32_t addr, uint32_t val, int size) {
        addr &= (MEM_SIZE - 1);
        mem[addr] = val & 0xFF;
        if (size >= 2) { mem[addr+1] = (val >> 8) & 0xFF; }
        if (size >= 4) { mem[addr+2] = (val >> 16) & 0xFF; mem[addr+3] = (val >> 24) & 0xFF; }
    }

    uint8_t* data() { return mem; }
};

#endif
