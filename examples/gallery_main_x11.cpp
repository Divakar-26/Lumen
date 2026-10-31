#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#undef Success
#undef None

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "gallery_app.h"

using GLXFBConfig = struct __GLXFBConfigRec*;
using GLXContext = struct __GLXcontextRec*;
using GLXDrawable = XID;
using GLXProc = void (*)();
extern "C" {
GLXFBConfig* glXChooseFBConfig(Display*, int, const int*, int*);
XVisualInfo* glXGetVisualFromFBConfig(Display*, GLXFBConfig);
GLXProc glXGetProcAddressARB(const unsigned char*);
Bool glXMakeCurrent(Display*, GLXDrawable, GLXContext);
void glXSwapBuffers(Display*, GLXDrawable);
void glXDestroyContext(Display*, GLXContext);
GLXContext glXCreateNewContext(Display*, GLXFBConfig, int, GLXContext, Bool);
void glViewport(int, int, int, int);
void glClearColor(float, float, float, float);
void glClear(unsigned int);
}

static constexpr int GLX_X_RENDERABLE = 0x8012;
static constexpr int GLX_DRAWABLE_TYPE = 0x8010;
static constexpr int GLX_WINDOW_BIT = 0x00000001;
static constexpr int GLX_RENDER_TYPE = 0x8011;
static constexpr int GLX_RGBA_BIT = 0x00000001;
static constexpr int GLX_X_VISUAL_TYPE = 0x22;
static constexpr int GLX_TRUE_COLOR = 0x8002;
static constexpr int GLX_RED_SIZE = 8;
static constexpr int GLX_GREEN_SIZE = 9;
static constexpr int GLX_BLUE_SIZE = 10;
static constexpr int GLX_ALPHA_SIZE = 11;
static constexpr int GLX_DEPTH_SIZE = 12;
static constexpr int GLX_STENCIL_SIZE = 13;
static constexpr int GLX_DOUBLEBUFFER = 5;
static constexpr int GLX_RGBA_TYPE = 0x8014;
static constexpr unsigned int GL_COLOR_BUFFER_BIT = 0x00004000u;

typedef void* (*GLXGetProcAddressProc)(const unsigned char*);

namespace {

ImGuiKey TranslateKey(KeySym key) {
    switch (key) {
        case XK_Tab: return ImGuiKey_Tab;
        case XK_Left: return ImGuiKey_LeftArrow;
        case XK_Right: return ImGuiKey_RightArrow;
        case XK_Up: return ImGuiKey_UpArrow;
        case XK_Down: return ImGuiKey_DownArrow;
        case XK_Page_Up: return ImGuiKey_PageUp;
        case XK_Page_Down: return ImGuiKey_PageDown;
        case XK_Home: return ImGuiKey_Home;
        case XK_End: return ImGuiKey_End;
        case XK_Insert: return ImGuiKey_Insert;
        case XK_Delete: return ImGuiKey_Delete;
        case XK_BackSpace: return ImGuiKey_Backspace;
        case XK_space: return ImGuiKey_Space;
        case XK_Return: return ImGuiKey_Enter;
        case XK_Escape: return ImGuiKey_Escape;
        case XK_apostrophe: return ImGuiKey_Apostrophe;
        case XK_comma: return ImGuiKey_Comma;
        case XK_minus: return ImGuiKey_Minus;
        case XK_period: return ImGuiKey_Period;
        case XK_slash: return ImGuiKey_Slash;
        case XK_semicolon: return ImGuiKey_Semicolon;
        case XK_equal: return ImGuiKey_Equal;
        case XK_bracketleft: return ImGuiKey_LeftBracket;
        case XK_backslash: return ImGuiKey_Backslash;
        case XK_bracketright: return ImGuiKey_RightBracket;
        case XK_grave: return ImGuiKey_GraveAccent;
        case XK_Caps_Lock: return ImGuiKey_CapsLock;
        case XK_Scroll_Lock: return ImGuiKey_ScrollLock;
        case XK_Num_Lock: return ImGuiKey_NumLock;
        case XK_Print: return ImGuiKey_PrintScreen;
        case XK_Pause: return ImGuiKey_Pause;
        case XK_Shift_L: return ImGuiKey_LeftShift;
        case XK_Shift_R: return ImGuiKey_RightShift;
        case XK_Control_L: return ImGuiKey_LeftCtrl;
        case XK_Control_R: return ImGuiKey_RightCtrl;
        case XK_Alt_L: return ImGuiKey_LeftAlt;
        case XK_Alt_R: return ImGuiKey_RightAlt;
        case XK_Super_L: return ImGuiKey_LeftSuper;
        case XK_Super_R: return ImGuiKey_RightSuper;
        case XK_F1: return ImGuiKey_F1;
        case XK_F2: return ImGuiKey_F2;
        case XK_F3: return ImGuiKey_F3;
        case XK_F4: return ImGuiKey_F4;
        case XK_F5: return ImGuiKey_F5;
        case XK_F6: return ImGuiKey_F6;
        case XK_F7: return ImGuiKey_F7;
        case XK_F8: return ImGuiKey_F8;
        case XK_F9: return ImGuiKey_F9;
        case XK_F10: return ImGuiKey_F10;
        case XK_F11: return ImGuiKey_F11;
        case XK_F12: return ImGuiKey_F12;
        default: break;
    }
    if (key >= XK_a && key <= XK_z) return static_cast<ImGuiKey>(ImGuiKey_A + (key - XK_a));
    if (key >= XK_A && key <= XK_Z) return static_cast<ImGuiKey>(ImGuiKey_A + (key - XK_A));
    if (key >= XK_0 && key <= XK_9) return static_cast<ImGuiKey>(ImGuiKey_0 + (key - XK_0));
    return ImGuiKey_None;
}

void AddModifierEvents(ImGuiIO& io, unsigned int state) {
    io.AddKeyEvent(ImGuiMod_Shift, (state & ShiftMask) != 0u);
    io.AddKeyEvent(ImGuiMod_Ctrl, (state & ControlMask) != 0u);
    io.AddKeyEvent(ImGuiMod_Alt, (state & Mod1Mask) != 0u);
    io.AddKeyEvent(ImGuiMod_Super, (state & Mod4Mask) != 0u);
}

void HandleEvent(XEvent& event, Display* display, Window window, Atom wm_delete,
                 ImGuiIO& io, bool& running) {
    if (event.type == ClientMessage && static_cast<Atom>(event.xclient.data.l[0]) == wm_delete) {
        running = false;
    } else if (event.type == ConfigureNotify && event.xconfigure.window == window) {
        io.DisplaySize = ImVec2(static_cast<float>(event.xconfigure.width), static_cast<float>(event.xconfigure.height));
    } else if (event.type == MotionNotify) {
        io.AddMousePosEvent(static_cast<float>(event.xmotion.x), static_cast<float>(event.xmotion.y));
    } else if (event.type == ButtonPress || event.type == ButtonRelease) {
        const bool down = event.type == ButtonPress;
        if (event.xbutton.button >= Button1 && event.xbutton.button <= Button3) {
            io.AddMouseButtonEvent(static_cast<int>(event.xbutton.button - Button1), down);
        } else if (event.xbutton.button == Button4 && down) {
            io.AddMouseWheelEvent(0.0f, 1.0f);
        } else if (event.xbutton.button == Button5 && down) {
            io.AddMouseWheelEvent(0.0f, -1.0f);
        }
    } else if (event.type == KeyPress || event.type == KeyRelease) {
        const bool down = event.type == KeyPress;
        char text[32]{};
        KeySym symbol = NoSymbol;
        const int length = XLookupString(&event.xkey, text, static_cast<int>(sizeof(text)), &symbol, nullptr);
        const ImGuiKey key = TranslateKey(symbol);
        if (key != ImGuiKey_None) io.AddKeyEvent(key, down);
        AddModifierEvents(io, event.xkey.state);
        if (down && length > 0 && (event.xkey.state & (ControlMask | Mod1Mask | Mod4Mask)) == 0u) {
            text[length] = '\0';
            io.AddInputCharactersUTF8(text);
        }
        if (down && symbol == XK_Escape) running = false;
    }
    (void)display;
}

} // namespace

int main(int argc, char** argv) {
    int max_frames = 0;
    int initial_overlay = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) max_frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--popover") == 0) initial_overlay = 1;
        else if (std::strcmp(argv[i], "--modal") == 0) initial_overlay = 2;
        else if (std::strcmp(argv[i], "--toast") == 0) initial_overlay = 3;
    }

    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        std::fprintf(stderr, "Lumen gallery: XOpenDisplay failed. Run inside an X11 session or under Xvfb.\n");
        return 2;
    }
    const int screen = DefaultScreen(display);
    const int framebuffer_attributes[] = {
        GLX_X_RENDERABLE, True, GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT, GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8, GLX_DEPTH_SIZE, 24, GLX_STENCIL_SIZE, 8,
        GLX_DOUBLEBUFFER, True, 0
    };
    int config_count = 0;
    GLXFBConfig* configs = glXChooseFBConfig(display, screen, framebuffer_attributes, &config_count);
    if (configs == nullptr || config_count == 0) {
        std::fprintf(stderr, "Lumen gallery: no GLX framebuffer configuration.\n");
        XCloseDisplay(display);
        return 3;
    }
    GLXFBConfig framebuffer = configs[0];
    XVisualInfo* visual = glXGetVisualFromFBConfig(display, framebuffer);
    if (visual == nullptr) {
        std::fprintf(stderr, "Lumen gallery: glXGetVisualFromFBConfig failed.\n");
        XFree(configs); XCloseDisplay(display);
        return 4;
    }

    XSetWindowAttributes attributes{};
    attributes.colormap = XCreateColormap(display, RootWindow(display, visual->screen), visual->visual, AllocNone);
    attributes.event_mask = ExposureMask | StructureNotifyMask | PointerMotionMask | ButtonPressMask |
                            ButtonReleaseMask | KeyPressMask | KeyReleaseMask;
    const int width = 1360, height = 900;
    Window window = XCreateWindow(display, RootWindow(display, visual->screen), 0, 0, width, height, 0,
                                  visual->depth, InputOutput, visual->visual, CWColormap | CWEventMask, &attributes);
    XStoreName(display, window, "Lumen UI Engine — Gallery");
    Atom wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wm_delete, 1);

    GLXContext gl_context = glXCreateNewContext(display, framebuffer, GLX_RGBA_TYPE, nullptr, True);
    if (gl_context == nullptr || !glXMakeCurrent(display, window, gl_context)) {
        std::fprintf(stderr, "Lumen gallery: could not create a GLX context.\n");
        XDestroyWindow(display, window); XFree(visual); XFree(configs); XCloseDisplay(display);
        return 5;
    }
    XMapWindow(display, window);
    XFlush(display);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.BackendPlatformName = "lumen_x11_host";
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowPadding = ImVec2(0.0f, 0.0f);
    ImGui::GetStyle().WindowBorderSize = 0.0f;
    // Bake the font ladder: one family at each size the themes use, so no text is resampled.
    static const float kFontSizes[] = {12.0f, 13.0f, 14.0f, 16.0f, 19.0f, 20.0f, 24.0f, 30.0f};
    ImFont* fonts[sizeof(kFontSizes) / sizeof(kFontSizes[0])]{};
    std::size_t font_count = 0u;
    for (const float size : kFontSizes) {
        ImFontConfig font_config{};
        font_config.OversampleH = 3;
        font_config.OversampleV = 2;
        ImFont* font = io.Fonts->AddFontFromFileTTF(LUMEN_GALLERY_FONT, size, &font_config);
        if (font == nullptr) break;
        fonts[font_count++] = font;
    }
    if (font_count == 0u) {
        std::fprintf(stderr, "Lumen gallery: could not load %s; using the ImGui default font.\n", LUMEN_GALLERY_FONT);
    } else {
        io.FontDefault = fonts[3];
    }
    if (!ImGui_ImplOpenGL3_Init("#version 130")) {
        std::fprintf(stderr, "Lumen gallery: ImGui OpenGL backend initialization failed.\n");
        ImGui::DestroyContext(); glXMakeCurrent(display, 0, nullptr); glXDestroyContext(display, gl_context);
        XDestroyWindow(display, window); XFree(visual); XFree(configs); XCloseDisplay(display);
        return 6;
    }

    Lumen::Context* context = Lumen::CreateContext();
    if (context == nullptr) {
        std::fprintf(stderr, "Lumen gallery: failed to create Lumen context.\n");
        ImGui_ImplOpenGL3_Shutdown(); ImGui::DestroyContext();
        glXMakeCurrent(display, 0, nullptr); glXDestroyContext(display, gl_context);
        XDestroyWindow(display, window); XFree(visual); XFree(configs); XCloseDisplay(display);
        return 7;
    }
    Lumen::SetFontLadder(context, fonts, font_count);
    Lumen::SetTheme(context, GalleryThemeKind(0));
    GalleryState state{};
    state.initial_overlay = initial_overlay;
    auto previous = std::chrono::steady_clock::now();
    bool running = true;
    int frame = 0;
    while (running && (max_frames <= 0 || frame < max_frames)) {
        while (XPending(display) > 0) {
            XEvent event{};
            XNextEvent(display, &event);
            HandleEvent(event, display, window, wm_delete, io, running);
        }
        const auto now = std::chrono::steady_clock::now();
        io.DeltaTime = std::chrono::duration<float>(now - previous).count();
        if (!std::isfinite(io.DeltaTime) || io.DeltaTime <= 0.0f || io.DeltaTime > 0.1f) io.DeltaTime = 1.0f / 60.0f;
        previous = now;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        ImGui::Begin("Lumen Gallery", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const Lumen::Viewport viewport{
            Lumen::Rect{origin.x, origin.y, available.x, available.y},
            {}, {1.0f, 1.0f}
        };
        if (Lumen::BeginFrame(context, viewport, io.DeltaTime)) {
            BuildLumenGallery(context, &state);
            Lumen::EndFrame(context);
        }
        ImGui::End();
        ImGui::Render();

        glViewport(0, 0, static_cast<int>(io.DisplaySize.x), static_cast<int>(io.DisplaySize.y));
        const Lumen::Color canvas = Lumen::GetTheme(context).canvas;
        glClearColor(canvas.r, canvas.g, canvas.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glXSwapBuffers(display, window);
        ++frame;
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }

    Lumen::DestroyContext(context);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    glXMakeCurrent(display, 0, nullptr);
    glXDestroyContext(display, gl_context);
    XDestroyWindow(display, window);
    XFree(visual);
    XFree(configs);
    XCloseDisplay(display);
    return 0;
}
