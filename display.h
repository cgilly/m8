#pragma once

#include <X11/Xlib.h>
#include <string>
#include <vector>
#include <atomic>

class display {
public:
    display(int w, int h, int pixel_size, const std::string &window_name);

    ~display();

    int initialize() noexcept;

    void terminate() noexcept;

    void loop() noexcept;

    void request_redraw(const uint8_t *display_buffer) noexcept;

private:
    const int pixel_size_;
    const int w_, h_;
    const std::string &window_name_;
    std::vector<uint8_t> display_buffer_;
    std::atomic<bool> running_, redraw_;
    Display *x11_display_ = nullptr;
    Window x11_window_{};
    GC x11_gc_{};

    void draw_pixels() const noexcept;
};

