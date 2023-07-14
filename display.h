#pragma once

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <string>
#include <vector>
#include <atomic>
#include <unordered_map>


class display {
public:
    display(int w, int h, int pixel_size, const std::string &window_name);

    ~display();

    int initialize() noexcept;

    void terminate() noexcept;

    void loop() noexcept;

    void request_redraw(const uint8_t *display_buffer) noexcept;

    inline void get_keyboard(bool *keyboard_buffer) const noexcept {
        for (auto i = 0; i < 16; ++i)
            keyboard_buffer[i] = key_pressed_[i].load();
    }

private:
    // Numpad digits + letters A to F
    static const std::unordered_map<int, int> keycodes;
    const int pixel_size_;
    const int w_, h_;
    const std::string &window_name_;
    std::vector<uint8_t> display_buffer_;
    std::atomic<bool> running_, redraw_;
    Display *x11_display_ = nullptr;
    Window x11_window_{};
    GC x11_gc_{};
    std::atomic<bool> key_pressed_[16];

    void draw_pixels() const noexcept;
};

