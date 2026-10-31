#ifndef LUMEN_EXAMPLES_GALLERY_APP_H
#define LUMEN_EXAMPLES_GALLERY_APP_H

#include "lumen/lumen.h"
#include "lumen/kit/overlays.h"

struct GalleryState {
    // 0 = Material, 1 = Dark, 2 = Light; applied_theme tracks what the context currently uses.
    int theme_index = 0;
    int applied_theme = -1;
    int click_count = 0;
    bool wifi = true;
    bool notifications = true;
    bool reduce_motion = false;
    bool applied_reduce_motion = false;
    bool locked = true;
    int toast_anchor = 0;
    int toasts_shown = 0;
    int chart_seed = 1;
    float drag_x = 16.0f;
    float drag_y = 16.0f;
    int drag_updates = 0;
    bool dragging = false;
    const char* dialog_status = "No decision yet";
    char name[40] = "Ada";
    char search[48]{};
    int matches[5000]{};
    int match_count = -1;
    float fps = 60.0f;
    Lumen::DebugStats stats{};
    Lumen::Kit::ToastQueue toasts{};
    // Optional deterministic initial overlay for automated screenshots.
    // 0 = none, 1 = popover, 2 = dialog, 3 = toasts.
    int initial_overlay = 0;
    bool initial_overlay_opened = false;
};

Lumen::ThemeKind GalleryThemeKind(int theme_index);
void BuildLumenGallery(Lumen::Context* context, GalleryState* state);

#endif
