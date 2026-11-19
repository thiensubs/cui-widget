// CUI renderer: a native implementation of the Renderer seam
// (twidget/renderer.h) that lays out the validated DOM with a built-in
// block + flexbox engine and paints it on the CPU with CUI's software
// renderer (libcui) into a premultiplied RGBA8 buffer. No WebView, no
// script, no network: the document only ever sees validated RenderOps and
// `data:` images.
//
// The C++ twin of BlitzRenderer / LitehtmlRenderer and the drop-in fourth
// argument to harness/main.cpp's runWidgetNative<CuiRenderer>: the owner
// takes the frames (paint(), onPaint from the clock) and feeds pointers in
// CSS px (touch(), click()).
//
// CSS coverage (profile 1 subset, grown as needed — see docs/cui-renderer.md):
//   supported     display:block|flex, flex-direction, justify-content,
//                 align-items, gap, padding, margin, width/height/min-height,
//                 background-color, color, border (+radius), font-size,
//                 font-weight, text-align, border-radius, the UA defaults.
//   not yet       CSS custom properties, @media, transitions/animations,
//                 pseudo-classes beyond :disabled, tables, floats, grid,
//                 inline SVG, <canvas> 2D commands, position, overflow
//                 scrolling, text-decoration, opacity compositing.
//
// Threading: the document is not locked on any side, so every public call
// goes through one mutex here; the host thread (messages), the clock thread
// (frames and paints) and the owner's thread (touches, paints) take turns.
// Messages to the host are dispatched outside the lock.
#pragma once

#include "twidget/renderer.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace twidget {

class CuiRenderer final : public Renderer {
public:
    /// One raster of the document: `width × height` physical px, premultiplied RGBA8, row-major.
    struct Frame {
        uint32_t width = 0, height = 0;
        std::vector<uint8_t> rgba;
        double paintMs = 0;
        bool empty() const { return rgba.empty(); }
    };

    /// Plug into WidgetHost: `WidgetHost(..., CuiRenderer::factory(dpr))`.
    static RendererFactory factory(double dpr = 1);

    /// `width` in CSS px, `dpr` = device pixel ratio, `theme` "light" | "dark".
    CuiRenderer(const std::string& theme, double width, double dpr);
    ~CuiRenderer() override;

    /// Always true: unlike the Rust Blitz document, the CUI engine cannot fail to start.
    bool ok() const { return true; }

    // Renderer (host thread)
    void load() override;
    void send(const std::string& json) override;
    void setTheme(const std::string& theme) override;
    void setWidth(double width) override;
    void destroy() override;

    /// A pointer in CSS px. action: 0 down, 1 move, 2 up, 3 cancel. Any thread.
    void touch(int action, double x, double y, int pointer = 0, bool primary = true);
    /// Down + up at one point (a tap).
    void click(double x, double y) { touch(0, x, y); touch(2, x, y); }

    /// Paints the document now. Empty on failure.
    Frame paint();
    /// The frames the clock paints after `requestFrame` / document changes.
    std::function<void(const Frame&)> onPaint;
    /// Start the internal frame clock (idempotent). The clock invokes `onPaint`
    /// and clock-driven messages on its own thread. Started by the constructor.
    void startClock();
    /// Stop the frame clock (idempotent). The host can then drive painting
    /// purely with paint(). Joins the clock thread.
    void stopClock();
    /// Paint statistics.
    int paints() const { return paints_; }
    double lastPaintMs() const { return lastPaintMs_; }
    /// Creating the engine (fonts are CUI's built-ins, so this is allocation only).
    double initMs() const { return initMs_; }
    double dpr() const { return dpr_; }

    static std::string fontReport();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void dispatch(const std::string& messages);
    void schedulePaint();
    void requestFrame();
    void clockLoop();
    static double nowMs();

    double dpr_;
    double width_;
    std::string theme_;
    double initMs_ = 0;
    mutable std::mutex mutex_;   // guards impl_ and every document access
    bool destroyed_ = false;

    std::mutex clockMutex_;
    std::condition_variable clockCv_;
    std::thread clock_;
    bool frameRequested_ = false;
    bool paintScheduled_ = false;
    bool stopClock_ = false;
    bool frozen_ = false;
    std::atomic<int> paints_{0};
    std::atomic<double> lastPaintMs_{0};
};

} // namespace twidget
