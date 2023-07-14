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
#include <stack>
#include <cstring>

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
constexpr static uint16_t mem_size = 4096;
constexpr static uint16_t start_addr = 0x200;
constexpr static auto gp_registers = 16;
constexpr static auto display_w = 64;
constexpr static auto display_h = 32;
constexpr static auto n_pixels = display_w * display_h;
constexpr static auto font_sprite_size = 5; // bytes

static bool cpu_running = true;

struct cpu_t {
    uint8_t ram[mem_size]{};
    uint8_t register_vx[gp_registers]{};
    uint8_t register_delay{};
    uint8_t register_sound{};
    uint16_t register_index{};
    uint16_t pc{};
    std::stack<uint16_t> stack;

    uint8_t display[n_pixels]{};
    bool keys[16]{};
    int last_key_pressed = -1;
    bool redraw_required{};
};

static void set_font_sprites(cpu_t &cpu) {
    constexpr uint8_t font_sprites[] = {
            0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
            0x20, 0x60, 0x20, 0x20, 0x70, // 1
            0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
            0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
            0x90, 0x90, 0xF0, 0x10, 0x10, // 4
            0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
            0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
            0xF0, 0x10, 0x20, 0x40, 0x40, // 7
            0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
            0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
            0xF0, 0x90, 0xF0, 0x90, 0x90, // A
            0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
            0xF0, 0x80, 0x80, 0x80, 0xF0, // C
            0xE0, 0x90, 0x90, 0x90, 0xE0, // D
            0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
            0xF0, 0x80, 0xF0, 0x80, 0x80, // F
    };
    std::memcpy(cpu.ram, font_sprites, 16 * 5);
}

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

    std::cout << "ROM loaded correctly! (size: " << rom_size << " bytes)" << std::endl;
}

static inline void set_flag(cpu_t &cpu, bool set) noexcept {
    cpu.register_vx[15] = static_cast<uint8_t>(set);
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
                // Clear display
                for (auto &p: cpu.display)
                    p = 0;
                cpu.redraw_required = true;
            }
            if (imm_nn == 0xEE) {
                // Return from subroutine
                if (!cpu.stack.empty()) {
                    cpu.pc = cpu.stack.top();
                    cpu.stack.pop();
                } else {
                    std::cerr << "[warning] 00EE called before 2NNN" << std::endl;
                }
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
            cpu.stack.push(cpu.pc);
            cpu.pc = imm_nnn;
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
            // The last nibble is used to identify the actual instruction
            switch (imm_n) {
                case 0x00: {
                    // 8XY0: store the value of register Y in register X
                    cpu.register_vx[reg_x] = cpu.register_vx[reg_y];
                    break;
                }
                case 0x01: {
                    // 8XY1: store the value of register X to register Y OR X
                    cpu.register_vx[reg_x] |= cpu.register_vx[reg_y];
                    break;
                }
                case 0x02: {
                    // 8XY2: store the value of register X to register Y AND X
                    cpu.register_vx[reg_x] &= cpu.register_vx[reg_y];
                    break;
                }
                case 0x03: {
                    // 8XY3: store the value of register X to register Y XOR X
                    cpu.register_vx[reg_x] ^= cpu.register_vx[reg_y];
                    break;
                }
                case 0x04: {
                    // 8XY4: VX += VY; VF = 1 if overflow
                    const auto x_val = static_cast<int>(cpu.register_vx[reg_x]);
                    const auto y_val = static_cast<int>(cpu.register_vx[reg_y]);
                    set_flag(cpu, x_val + y_val > 255);

                    cpu.register_vx[reg_x] += y_val;
                    break;
                }
                case 0x05: {
                    // 8XY5: VX -= VY; VF = 1 if borrow
                    const auto x_val = static_cast<int>(cpu.register_vx[reg_x]);
                    const auto y_val = static_cast<int>(cpu.register_vx[reg_y]);
                    set_flag(cpu, x_val < y_val);

                    cpu.register_vx[reg_x] -= y_val;
                    break;
                }
                case 0x06: {
                    // 8XY6: VX = VY >> 1; VF = old_VY & 1
                    const auto y_val = static_cast<int>(cpu.register_vx[reg_y]);
                    set_flag(cpu, y_val & 0x01);

                    cpu.register_vx[reg_x] = static_cast<uint8_t>(y_val >> 1);
                    break;
                }
                case 0x07: {
                    // 8XY7: VX = VY - VX; VF = 1 if borrow
                    const auto x_val = static_cast<int>(cpu.register_vx[reg_x]);
                    const auto y_val = static_cast<int>(cpu.register_vx[reg_y]);
                    set_flag(cpu, y_val < x_val);

                    cpu.register_vx[reg_x] = y_val - x_val;
                    break;
                }
                case 0x0E: {
                    // 8XYE: VX = VY << 1; VF = old_VY & 1
                    const auto y_val = static_cast<int>(cpu.register_vx[reg_y]);
                    set_flag(cpu, y_val & 0x01);

                    cpu.register_vx[reg_x] = static_cast<uint8_t>(y_val << 1);
                    break;
                }
            }
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
            const auto addr = static_cast<uint16_t>(imm_nnn + cpu.register_vx[0]);
            cpu.pc = addr;
            break;
        }
        case 0x0C: {
            // CXNN: set register X to a random value masked with NN
            auto rand_value = static_cast<uint8_t>(std::rand() % 256);
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
            switch (imm_nn) {
                case 0x9E: {
                    // EX9E: skip the following instruction if the key corresponding to the hex value stored in register X is pressed
                    const auto x_val = cpu.register_vx[reg_x];
                    if (x_val < 16) {
                        if (cpu.keys[x_val])
                            cpu.pc += 2;
                    }
                    break;
                }
                case 0xA1: {
                    // EXA1: skip the following instruction if the key corresponding to the hex value stored in register X is not pressed
                    const auto x_val = cpu.register_vx[reg_x];
                    if (x_val < 16) {
                        if (!cpu.keys[x_val])
                            cpu.pc += 2;
                    }
                    break;
                }
            }
            break;
        }
        case 0x0F: {
            // FX-type instructions are identified by the last byte (imm_nn)
            switch (imm_nn) {
                case 0x07: {
                    // FX07: store the current value of the delay timer in register X
                    cpu.register_vx[reg_x] = cpu.register_delay;
                    break;
                }
                case 0x0A: {
                    // FX0A: wait for a keypress and store it in register X
                    if (cpu.last_key_pressed < 0) {
                        cpu.pc -= 2;
                    } else {
                        cpu.register_vx[reg_x] = static_cast<uint8_t>(cpu.last_key_pressed);
                    }
                    break;
                }
                case 0x15: {
                    // FX15: set the delay timer to the value of register X
                    cpu.register_delay = cpu.register_vx[reg_x];
                    break;
                }
                case 0x18: {
                    // FX18: set the sound timer to the value of register X
                    cpu.register_sound = cpu.register_vx[reg_x];
                    break;
                }
                case 0x1E: {
                    // FX1E: add the value stored in register X to register I
                    cpu.register_index += cpu.register_vx[reg_x];
                    break;
                }
                case 0x29: {
                    // FX29: set register I to the memory address of the sprite data corresponding to hex digit in register X
                    const auto digit = cpu.register_vx[reg_x];
                    cpu.register_index = digit * font_sprite_size;
                    break;
                }
                case 0x33: {
                    // FX33: store the BCD equivalent of the value stored in register X at addresses I, I+1, I+2
                    const auto x_val = cpu.register_vx[reg_x];
                    const uint8_t c = x_val / 100;
                    const int mod100 = x_val % 100;
                    const uint8_t d = mod100 / 10;
                    const uint8_t u = mod100 % 10;

                    cpu.ram[cpu.register_index] = c;
                    cpu.ram[cpu.register_index + 1] = d;
                    cpu.ram[cpu.register_index + 2] = u;
                    break;
                }
                case 0x55: {
                    // FX55: store the value of registers 0 to X inclusive in memory starting from I then set I = I + X + 1
                    for (auto i = 0; i <= reg_x; ++i) {
                        cpu.ram[cpu.register_index + i] = cpu.register_vx[i];
                    }
                    cpu.register_index += (reg_x + 1);
                    break;
                }
                case 0x65: {
                    // FX65: fill registers 0 to X inclusive with the values stored in memory starting from I then set I = I + X + 1
                    for (auto i = 0; i <= reg_x; ++i) {
                        cpu.register_vx[i] = cpu.ram[cpu.register_index + i];
                    }
                    cpu.register_index += (reg_x + 1);
                    break;
                }


            }
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


static std::string parse_cli(int argc, char **argv) {
    constexpr static auto usage = "Usage is: emu --rom=<path/to/rom>";
    if (argc != 2) {
        std::cerr << usage << std::endl;
        exit(1);
    }

    std::string rom_opt = argv[1];
    std::string rom;
    if (rom_opt.starts_with("--")) {
        auto pos = rom_opt.find('=');
        if (pos != std::string::npos) {
            const auto opt_name = rom_opt.substr(2, pos - 2);
            const auto opt_val = rom_opt.substr(pos + 1);
            if (opt_name != "rom") {
                std::cerr << "Wrong option, expected rom, found " << opt_name << "\n" << usage << std::endl;
                exit(1);
            }
            rom = opt_val;
        } else {
            std::cerr << "Invalid option format: " << rom_opt << "\n" << usage << std::endl;
            exit(1);
        }
    } else {
        std::cout << "Unknown option: " << rom_opt << "\n" << usage << std::endl;
        exit(1);
    }
    return rom;
}


int main(int argc, char **argv) {
    const auto rom = parse_cli(argc, argv);

    signal(SIGINT, on_interrupt);

    cpu_t cpu{};
    set_font_sprites(cpu);
    // Initialize the display
    display the_display{display_w, display_h, 10, "CHIP-8"};
    the_display.initialize();

    std::thread display_thread{[&the_display]() {
        the_display.loop();
    }};

    load_rom(cpu, rom);

    // Prepare random number generator
    std::srand(std::time(nullptr));

    // Set the program counter to the first instruction
    cpu.pc = start_addr;
    uint16_t next_instruction;

    constexpr static auto clock_period_us = static_cast<int>((1. / clock_freq) * 1000 * 1000);
    constexpr static auto clocks60 = static_cast<size_t>(clock_freq / 60.);

    size_t clk = 0;
    bool tmp_keys[16];
    bool something_pressed = false;
    while (cpu_running) {
        const auto start = now();
        next_instruction = (cpu.ram[cpu.pc] << 8) | (cpu.ram[cpu.pc + 1]);
        cpu.pc += 2;

        execute(cpu, next_instruction);

        clk++;

        if ((clk % clocks60) == 0) {
            // In here we can do everything that needs to be done 60 times a second
            if (cpu.redraw_required) {
                cpu.redraw_required = false;
                the_display.request_redraw(cpu.display);
            }

            // Decrement sound and delay timers
            if (cpu.register_delay > 0)
                cpu.register_delay--;

            if (cpu.register_sound > 0)
                cpu.register_sound--;
        }
        // Fetch the current input status
        the_display.get_keyboard(tmp_keys);
        // Check if something changed from the last clock cycle
        for (auto i = 0; i < 16; ++i) {
            if (tmp_keys[i] && !cpu.keys[i]) {
                // Then the user pressed key i
                cpu.last_key_pressed = i;
                something_pressed = true;
                break;
            }
        }

        if (!something_pressed) {
            cpu.last_key_pressed = -1;
        }
        std::memcpy(cpu.keys, tmp_keys, 16 * sizeof(bool));

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