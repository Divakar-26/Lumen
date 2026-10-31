#include "lumen/lumen.h"
#include "headless_raster.h"

#include <cstdio>
#include <cstring>

namespace {
constexpr int kWidth = 760;
constexpr int kHeight = 360;

void BuildScene(Lumen::Context* ui, bool pressed) {
    const Lumen::Theme theme = Lumen::GetTheme(ui);
    Lumen::NodeStyle row{};
    row.layout = Lumen::LayoutMode::Row;
    row.gap = 20.0f;
    row.padding = {36.0f, 28.0f, 28.0f, 36.0f};
    row.align_items = Lumen::Align::Start;
    Lumen::BeginNode(ui, "interaction-showcase", row);

    for (int i = 0; i < 2; ++i) {
        Lumen::NodeStyle card{};
        card.width = 320.0f;
        card.height = 240.0f;
        card.layout = Lumen::LayoutMode::Column;
        card.padding = {20.0f, 20.0f, 20.0f, 20.0f};
        card.gap = 12.0f;
        card.background = theme.surface;
        card.has_background = true;
        card.hover_background = i == 0 ? Lumen::Color{0.25f, 0.34f, 0.52f, 1.0f}
                                       : Lumen::Color{0.22f, 0.40f, 0.34f, 1.0f};
        card.has_hover_background = true;
        card.pressed_background = i == 0 ? Lumen::Color{0.35f, 0.46f, 0.68f, 1.0f}
                                         : Lumen::Color{0.31f, 0.52f, 0.43f, 1.0f};
        card.has_pressed_background = true;
        card.radius = 20.0f;
        card.animate_layout = false;
        const char* key = i == 0 ? "hover-card" : "pressed-card";
        Lumen::BeginNode(ui, key, card);
        const bool active_card = i == 0;
        Lumen::NodeStyle title{};
        title.font_size = 22.0f;
        title.has_text_color = true;
        title.text_color = active_card ? Lumen::Color{0.97f, 0.98f, 1.0f, 1.0f} : theme.text;
        Lumen::Label(ui, "title", active_card ? (pressed ? "Pressed container" : "Hovered container")
                                                : "Neutral container", title);
        Lumen::NodeStyle body{};
        body.font_size = 14.0f;
        body.has_text_color = true;
        body.text_color = active_card ? Lumen::Color{0.88f, 0.91f, 0.98f, 1.0f} : theme.muted_text;
        const char* description = active_card
            ? (pressed ? "Pointer is held on this surface.\nThe container and its descendants expose pressed state."
                       : "Pointer is over this surface.\nThe container resolves its hover selector.")
            : "No pointer interaction.\nThis surface keeps its resting fill.";
        Lumen::Label(ui, "description", description, body);
        Lumen::EndNode(ui);
    }
    Lumen::EndNode(ui);
}

bool Render(const char* output, bool pressed) {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(static_cast<float>(kWidth), static_cast<float>(kHeight));
    unsigned char* font_pixels = nullptr;
    int font_width = 0;
    int font_height = 0;
    ImFont* roboto = io.Fonts->AddFontFromFileTTF("imgui/misc/fonts/Roboto-Medium.ttf", 16.0f);
    if (roboto == nullptr) io.Fonts->AddFontDefault();
    io.Fonts->GetTexDataAsRGBA32(&font_pixels, &font_width, &font_height);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    Lumen::ManualClock clock(50.0);
    Lumen::Config config{};
    config.clock = &clock;
    Lumen::Context* ui = Lumen::CreateContext(config);
    if (ui == nullptr) {
        ImGui::DestroyContext();
        return false;
    }
    Lumen::SetTheme(ui, Lumen::ThemeKind::Light);
    HeadlessRaster raster(kWidth, kHeight);

    for (int frame = 0; frame < 30; ++frame) {
        clock.Advance(1.0 / 60.0);
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(kWidth), static_cast<float>(kHeight)), ImGuiCond_Always);
        ImGui::Begin("interaction-preview", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        if (frame == 0) {
            io.AddMousePosEvent(180.0f, 150.0f);
            if (pressed) io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        }
        const Lumen::Viewport viewport{
            Lumen::Rect{0.0f, 0.0f, static_cast<float>(kWidth), static_cast<float>(kHeight)},
            {}, {1.0f, 1.0f}};
        if (Lumen::BeginFrame(ui, viewport, 1.0f / 60.0f)) {
            BuildScene(ui, pressed);
            Lumen::EndFrame(ui);
        }
        ImGui::End();
        ImGui::Render();
    }

    raster.Clear(0.95f, 0.93f, 0.97f);
    raster.Draw(ImGui::GetDrawData(), font_pixels, font_width, font_height);
    const bool saved = raster.SavePng(output);
    Lumen::DestroyContext(ui);
    ImGui::DestroyContext();
    std::printf("%s %s\n", saved ? "wrote" : "failed", output);
    return saved;
}
} // namespace

int main(int argc, char** argv) {
    const char* output = argc > 1 ? argv[1] : "lumen_interaction.png";
    const bool pressed = argc > 2 && std::strcmp(argv[2], "pressed") == 0;
    return Render(output, pressed) ? 0 : 1;
}
