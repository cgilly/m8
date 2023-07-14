#include "display.h"
#include <cstring>
#include <iostream>

const std::unordered_map<int, int> display::keycodes = {
        {65438, 0},
        {65436, 1},
        {65433, 2},
        {65435, 3},
        {65430, 4},
        {65437, 5},
        {65432, 6},
        {65429, 7},
        {65431, 8},
        {65434, 9},
        {97,    10},
        {98,    11},
        {99,    12},
        {100,   13},
        {101,   14},
        {102,   15}
};

display::display(int w, int h, int pixel_size, const std::string &window_name) : pixel_size_(pixel_size), w_(w),
                                                                                 h_(h), window_name_(window_name),
                                                                                 running_(false), redraw_(false) {
    display_buffer_.resize(w_ * h_);
}

display::~display() {
    XFreeGC(x11_display_, x11_gc_);
    XDestroyWindow(x11_display_, x11_window_);
    XCloseDisplay(x11_display_);
}

int display::initialize() noexcept {
    x11_display_ = XOpenDisplay(nullptr);
    if (x11_display_ == nullptr) {
        std::cerr << "Error creating the display!" << std::endl;
        return 1;
    }
    int screen = DefaultScreen(x11_display_);
    Window root = RootWindow(x11_display_, screen);
    XWindowAttributes screen_attr;
    XGetWindowAttributes(x11_display_, root, &screen_attr);

    std::cout << "[debug] Screen resolution is " << screen_attr.width << "x" << screen_attr.height << std::endl;
    static const auto window_w = w_ * pixel_size_;
    static const auto window_h = h_ * pixel_size_;

    x11_window_ = XCreateSimpleWindow(
            x11_display_, root,
            0, 0, window_w, window_h, 0,
            BlackPixel(x11_display_, screen),
            BlackPixel(x11_display_, screen)
    );
    XSelectInput(x11_display_, x11_window_, ExposureMask | KeyPressMask | KeyReleaseMask);
    XMapWindow(x11_display_, x11_window_);

    // Centering the window before XMapWindow doesn't do anything
    XMoveWindow(x11_display_, x11_window_, static_cast<int>((screen_attr.width - window_w) / 2),
                static_cast<int>((screen_attr.height - window_h) / 2));
    XStoreName(x11_display_, x11_window_, window_name_.c_str());

    // Configure color palette
    XColor white;
    Colormap colormap = DefaultColormap(x11_display_, screen);
    XParseColor(x11_display_, colormap, "#FFFFFF", &white);
    XAllocColor(x11_display_, colormap, &white);

    XGCValues values;
    values.foreground = white.pixel;
    x11_gc_ = XCreateGC(x11_display_, x11_window_, GCForeground, &values);

    running_ = true;
    return 0;
}

void display::loop() noexcept {
    XEvent event;
    while (running_.load()) {
        while (XPending(x11_display_)) {
            // Consume all the events
            XNextEvent(x11_display_, &event);
            if (event.type == Expose) {
                XWindowAttributes gwa;
                XGetWindowAttributes(x11_display_, x11_window_, &gwa);
                std::cout << "Game window is " << gwa.width << "x" << gwa.height << std::endl;
                draw_pixels();
            } else if (event.type == KeyPress || event.type == KeyRelease) {
                const auto keycode = static_cast<int>(XLookupKeysym(&event.xkey, 0));
                bool is_pressed = event.type == KeyPress;
                if (keycodes.contains(keycode)) {
                    auto key = keycodes.at(keycode);
                    key_pressed_[key] = is_pressed;
                }
            }
        }

        if (redraw_.load()) {
            redraw_ = false;
            draw_pixels();
        }
    }
}

void display::request_redraw(const uint8_t *display_buffer) noexcept {
    static const auto buffer_size = w_ * h_;
    // Copy the whole video memory and redraw the entire screen
    std::memcpy(display_buffer_.data(), display_buffer, buffer_size);

    redraw_ = true;
}

void display::draw_pixels() const noexcept {
    XClearWindow(x11_display_, x11_window_);
    for (auto h_idx = 0; h_idx < h_; ++h_idx) {
        for (auto w_idx = 0; w_idx < w_; ++w_idx) {
            // If the corresponding byte in display_buffer_ is on then draw a (pixel_size_, pixel_size_) white square
            if (display_buffer_[h_idx * w_ + w_idx]) {
                XFillRectangle(x11_display_, x11_window_, x11_gc_, w_idx * pixel_size_, h_idx * pixel_size_,
                               pixel_size_, pixel_size_);
            }
        }
    }
}

void display::terminate() noexcept {
    running_ = false;
}
