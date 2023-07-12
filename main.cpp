#include <cstdio>
#include <cstdlib>
#include <string>
#include <iostream>
#include <cassert>
#include <ctime>
#include <csignal>
#include <chrono>
#include <thread>
#include "display.h"

// Notes:

// CHIP-8 instructions are 16-bits wide, stored in big-endian format.

// The CHIP-8 CPU is made up of:
//  - 4096 bytes of RAM (the program has to be loaded starting from address 0x200)
//  - 16 8-bit (V0-VF) general purpose data registers. VF sometimes acts as a flag
//  - 1 16-bit register (I) used to store memory addresses
//  - 1 8-bit delay timer which counts down to 0 at 60Hz
//  - 1 8-bit sound timer which counts down to 0 at 60Hz, when it's value is > 1 a sound frequency will be generated
//  - a stack to handle up to 12 successive subroutines
//  - a 64x32 monochrome display

constexpr static auto clock_freq = 7e2; // 700 Hz
constexpr static auto display_refresh_rate = 3e1; // 30 FPS
constexpr static uint16_t mem_size = 4096;
constexpr static uint16_t start_addr = 0x200;
constexpr static auto gp_registers = 16;
constexpr static auto display_w = 64;
constexpr static auto display_h = 32;
constexpr static auto n_pixels = display_w * display_h;

static bool cpu_running = true;

struct cpu_t {
    uint8_t ram[mem_size];
    uint8_t register_vx[gp_registers];
    uint8_t register_delay;
    uint8_t register_sound;
    uint16_t register_index;
    uint16_t pc;

    uint8_t display[n_pixels];
    bool redraw_required;
};

static void load_rom(cpu_t &cpu, const std::string &filename) {
    std::cout << "Loading ROM: " << filename << std::endl;

    FILE *rom = fopen(filename.c_str(), "rb");
    assert(rom != nullptr);

    fseek(rom, 0, SEEK_END);
    auto rom_size = ftell(rom);
    fseek(rom, 0, SEEK_SET);

    if (static_cast<uint16_t>(rom_size) > (mem_size - start_addr)) {
        std::cerr << "Error! Can't fit ROM of size: " << rom_size << " in 4KB" << std::endl;
        exit(1);
    }

    fread(&cpu.ram[start_addr], 1, rom_size, rom);

    fclose(rom);

    std::cout << "ROM loaded correctly! (size = " << rom_size << ")" << std::endl;
}


static void execute(cpu_t &cpu, uint16_t instruction) noexcept {
    const auto ms_nibble = static_cast<uint8_t>((instruction >> 12) & 0x000F);
    const auto reg_x = static_cast<uint8_t>((instruction >> 8) & 0x000F);
    const auto reg_y = static_cast<uint8_t>((instruction >> 4) & 0x000F);
    const auto imm_n = static_cast<uint8_t>(instruction & 0x000F);
    const auto imm_nn = static_cast<uint16_t>(instruction & 0x00FF);
    const auto imm_nnn = static_cast<uint16_t>(instruction & 0x0FFF);

    switch (ms_nibble) {
        case 0x00: {
            if (imm_nn == 0xE0) {
                for (auto &p: cpu.display)
                    p = 0;
                cpu.redraw_required = true;
            }
            if (imm_nn == 0xEE) {
                // Return from subroutine
            }
            break;
        }
        case 0x01: {
            // 1NNN: jump to address NNN
            cpu.pc = imm_nnn;
            break;
        }
        case 0x02: {
            // 2NNN: execute subroutine at address NNN
            std::cout << "2NNN" << std::endl;
            break;
        }
        case 0x03: {
            // 3XNN: skip the following instruction if the value of register X is equal to NN
            if (cpu.register_vx[reg_x] == imm_nn) {
                // skip the next instruction
                cpu.pc += 2;
            }
            break;
        }
        case 0x04: {
            // 4XNN: skip the following instruction if the value of register X is NOT equal to NN
            if (cpu.register_vx[reg_x] != imm_nn) {
                // skip the next instruction
                cpu.pc += 2;
            }
            break;
        }
        case 0x05: {
            // 5XY0: skip the following instruction if the value of register X is equal to the value of register Y
            if (cpu.register_vx[reg_x] == cpu.register_vx[reg_y]) {
                // skip next instruction
                cpu.pc += 2;
            }
            break;
        }
        case 0x06: {
            // 6XNN: store the value NN in register X
            cpu.register_vx[reg_x] = imm_nn;
            break;
        }
        case 0x07: {
            // 7XNN: add the value NN to register X
            cpu.register_vx[reg_x] += imm_nn;
            break;
        }
        case 0x08: {
            std::cout << "8XY_" << std::endl;
            break;
        }
        case 0x09: {
            // 9XY0: skip the following instruction if the value of register X is NOT equal to the value of register Y
            if (cpu.register_vx[reg_x] != cpu.register_vx[reg_y]) {
                // skip next instruction
                cpu.pc += 2;
            }
            break;
        }
        case 0x0A: {
            // ANNN: store memory address NNN in register I
            cpu.register_index = imm_nnn;
            break;
        }
        case 0x0B: {
            // BNNN: jump at address NNN + V0
            auto addr = static_cast<uint16_t>(imm_nnn + cpu.register_vx[0]);
            cpu.pc = addr;
            break;
        }
        case 0x0C: {
            // CXNN: set register X to a random value masked with NN
            uint8_t rand_value = std::rand() % 256;
            rand_value &= imm_nn;
            cpu.register_vx[reg_x] = rand_value;
            break;
        }
        case 0x0D: {
            const auto x = cpu.register_vx[reg_x] % display_w;
            const auto y = cpu.register_vx[reg_y] % display_h;
            const auto display_offset = x + y * display_w;
            cpu.register_vx[15] = 0;

            for (uint8_t row = 0; (row < imm_n) && ((row + y) < display_h); ++row) {
                auto sprite_data = cpu.ram[cpu.register_index + row];
                for (auto col = 0; (col < 8) && ((x + col) < display_w); ++col) {
                    uint8_t sprite_p = (sprite_data >> (7 - col)) & 0x01;
                    uint8_t &current_p = cpu.display[row * display_w + col + display_offset];
                    if (sprite_p) {
                        if (current_p) {
                            current_p = 0;
                            cpu.register_vx[15] = 1;
                        } else {
                            current_p = sprite_p;
                        }
                    }
                }
            }
            cpu.redraw_required = true;
            break;
        }
        case 0x0E: {
            std::cout << "EX__" << std::endl;
            break;
        }
        case 0x0F: {
            std::cout << "FX__" << std::endl;
            break;
        }
    }
}

void on_interrupt(int) {
    std::cout << "Received interrupt, shutting down..." << std::endl;
    cpu_running = false;
}

using namespace std::chrono;

static inline microseconds now() noexcept {
    return duration_cast<microseconds>(high_resolution_clock::now().time_since_epoch());
}


int main() {
    signal(SIGINT, on_interrupt);

    cpu_t cpu{};
    // Initialize the display
    display the_display{display_w, display_h, 10, "CHIP-8"};
    the_display.initialize();

    std::thread display_thread{[&the_display]() {
        the_display.loop();
    }};

    load_rom(cpu, "testroms/ibm-logo.ch8");

    // Prepare random number generator
    std::srand(std::time(nullptr));

    // Set the program counter to the first instruction
    cpu.pc = start_addr;
    uint16_t next_instruction;

    constexpr static auto clock_period_us = static_cast<int>((1. / clock_freq) * 1000 * 1000);
    constexpr static auto clocks_per_refresh_cycle = static_cast<size_t>(clock_freq / display_refresh_rate);

    size_t clk = 0;
    while (cpu_running) {
        const auto start = now();
        next_instruction = (cpu.ram[cpu.pc] << 8) | (cpu.ram[cpu.pc + 1]);
        cpu.pc += 2;

        execute(cpu, next_instruction);

        clk++;

        // Check if a redraw is needed
        if ((clk % clocks_per_refresh_cycle) == 0 && cpu.redraw_required) {
            cpu.redraw_required = false;
            the_display.request_redraw(cpu.display);
        }
        const auto stop = now();
        const auto period_us = (stop - start).count();
        if (period_us < clock_period_us) {
            usleep(clock_period_us - period_us);
        }
    }

    the_display.terminate();
    display_thread.join();
    return 0;
}