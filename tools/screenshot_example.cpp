// Renders a Lumen scene to a PNG with no window or GPU.
//   g++ -std=c++17 -O1 -Iinclude -Icore -Iimgui -Itools tools/screenshot_example.cpp <lumen core objs + imgui objs> -o shot
//   ./shot out.png light
// Remember: ImGui::SetNextWindow* calls go AFTER ImGui::NewFrame(), and time-dependent visuals need a
// ManualClock advanced every frame (animations never progress on a real clock between instant frames).
#include "lumen/lumen.h"
#include "headless_raster.h"

#include <cstdio>
#include <cstring>

static void Build(Lumen::Context* ui) {
    const Lumen::Theme theme = Lumen::GetTheme(ui);
    Lumen::NodeStyle row{};
    row.layout = Lumen::LayoutMode::Row;
    row.gap = 40.0f;
    row.padding = {50.0f, 40.0f, 40.0f, 50.0f};
    Lumen::BeginNode(ui, "row", row);

    Lumen::ShadowStyle shadow{};
    shadow.blur = 24.0f;
    shadow.offset_y = 8.0f;
    Lumen::NodeStyle card{};
    card.width = 220.0f;
    card.height = 150.0f;
    card.padding = {18.0f, 18.0f, 18.0f, 18.0f};
    card.background = theme.surface;
    card.has_background = true;
    card.radius = 20.0f;
    card.shadow = &shadow;
    Lumen::BeginNode(ui, "card", card);
    Lumen::Label(ui, "title", "Elevated card");
    Lumen::EndNode(ui);

    Lumen::NodeStyle button{};
    button.radius = 20.0f;
    button.width = 160.0f;
    Lumen::Button(ui, "button", "Button", nullptr, nullptr, button);
    Lumen::EndNode(ui);
}

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "shot.png";
    const bool light = argc > 2 && std::strcmp(argv[2], "light") == 0;
    const int W = 640, H = 300;

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(static_cast<float>(W), static_cast<float>(H));
    unsigned char* pixels = nullptr;
    int tex_w = 0, tex_h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &tex_w, &tex_h);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    Lumen::ManualClock clock(10.0);
    Lumen::Config config{};
    config.clock = &clock;
    Lumen::Context* ui = Lumen::CreateContext(config);
    Lumen::SetTheme(ui, light ? Lumen::ThemeKind::Light : Lumen::ThemeKind::Dark);

    HeadlessRaster raster(W, H);
    for (int frame = 0; frame < 40; ++frame) { // 40 frames of manual-clock time lets every tween settle
        clock.Advance(1.0 / 60.0);
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(W), static_cast<float>(H)));
        ImGui::Begin("host", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove);
        const Lumen::Viewport viewport{Lumen::Rect{0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H)}, {}, {1.0f, 1.0f}};
        if (Lumen::BeginFrame(ui, viewport, 1.0f / 60.0f)) {
            Build(ui);
            Lumen::EndFrame(ui);
        }
        ImGui::End();
        ImGui::Render();
    }
    raster.Clear(0.0f, 0.0f, 0.0f);
    raster.Draw(ImGui::GetDrawData(), pixels, tex_w, tex_h);
    const bool ok = raster.SavePng(out);
    std::printf("%s %s\n", ok ? "wrote" : "FAILED to write", out);

    Lumen::DestroyContext(ui);
    ImGui::DestroyContext();
    return ok ? 0 : 1;
}
