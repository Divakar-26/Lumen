#ifndef LUMEN_KIT_OVERLAYS_H
#define LUMEN_KIT_OVERLAYS_H

#include "lumen/lumen.h"

namespace Lumen::Kit {

enum class DialogResult : std::uint8_t { None, Confirm, Cancel };

// Opens (or reopens) a modal alert dialog layer that Escape and scrim clicks dismiss.
bool OpenAlertDialog(Context* context, std::uint64_t layer_id) noexcept;

// Declare every frame. While the layer is open this declares a centred card over the modal scrim
// that fades in while rising. Returns Confirm or Cancel once, on the frame after the dialog starts
// closing; Escape and scrim clicks report Cancel.
DialogResult AlertDialog(Context* context, std::uint64_t layer_id, const char* title, const char* body,
                         const char* confirm_label, const char* cancel_label = "Cancel") noexcept;

enum class ToastStatus : std::uint8_t { Info, Success, Warning, Error };
enum class ToastAnchor : std::uint8_t { BottomCenter, BottomRight, TopRight };

// Caller-owned toast stack. Fixed capacity; text is copied (and truncated) into the entries.
struct ToastQueue {
    static constexpr int kCapacity = 6;
    static constexpr int kMaxVisible = 4;

    struct Entry {
        std::uint64_t id = 0u;
        char title[64]{};
        char message[160]{};
        char action[24]{};
        ToastStatus status = ToastStatus::Info;
        double duration = 3.5;
        double remaining = 3.5;
        double enter_start = -1.0;
        double exit_start = -1.0;
        bool hovered = false;
        bool close_requested = false;
        bool action_requested = false;
    };

    explicit ToastQueue(std::uint64_t layer = 0x4C4B54u) noexcept : layer_id(layer) {}

    Entry entries[kCapacity]{};
    int count = 0;
    std::uint64_t next_id = 1u;
    std::uint64_t layer_id = 0u;
    ToastAnchor anchor = ToastAnchor::BottomCenter;
    double last_time = -1.0;
};

// Queues a toast and returns its id. Beyond kMaxVisible the oldest visible toast is dismissed;
// at kCapacity the oldest entry is dropped. A null or empty action shows no action button.
std::uint64_t ShowToast(ToastQueue& queue, ToastStatus status, const char* title, const char* message,
                        const char* action = nullptr, double duration_seconds = 3.5) noexcept;

// Declare every frame. Toasts stack at queue.anchor (newest nearest the edge) in one portal layer,
// enter with Theme::enter_transition, leave with Theme::exit_transition, glide to new slots with
// Theme::reflow_transition, and count down while the pointer is not over them. Returns the id of a
// toast whose action was pressed (that toast is then dismissed), or 0.
std::uint64_t Toasts(Context* context, ToastQueue& queue) noexcept;

} // namespace Lumen::Kit

#endif
