#include "RUL0.h"
#include "ini.h"
#include "imgui.h"
#include "rlImGui.h"
#include "raylib.h"

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr auto kDefaultRul0Path = "../examples/rul0/nam_full.txt";

struct PieceView {
    uint32_t id = 0;
    std::string shortDescription;
    std::string fullDetail;
};

std::vector<PieceView> BuildPieceViews(const IntersectionOrdering::Data& data) {
    std::vector<PieceView> views;
    views.reserve(data.puzzlePieces.size());

    for (const auto& [id, piece] : data.puzzlePieces) {
        PieceView view;
        view.id = id;
        if (!piece.effect.name.empty()) {
            view.shortDescription = std::format("0x{:08X} - {}", id, piece.effect.name);
        }
        else {
            view.shortDescription = std::format("0x{:08X}", id);
        }
        view.fullDetail = piece.ToString();
        views.push_back(std::move(view));
    }

    std::sort(views.begin(), views.end(), [](const PieceView& a, const PieceView& b) {
        return a.id < b.id;
    });

    return views;
}

std::string DescribeParseError(int resultCode) {
    switch (resultCode) {
        case -1:
            return "failed to open the file";
        case -2:
            return "parsing error occurred";
        default:
            return "unknown error";
    }
}

bool TryLoadData(std::string_view filePath, IntersectionOrdering::Data& data, std::string& errorMessage) {
    const int result = ini_parse(std::string(filePath).c_str(), IntersectionOrdering::IniHandler, &data);
    if (result < 0) {
        errorMessage = DescribeParseError(result);
        return false;
    }

    IntersectionOrdering::BuildNavigationIndices(data);
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    const std::string rul0Path = (argc > 1 ? argv[1] : kDefaultRul0Path);

    IntersectionOrdering::Data data;
    std::string parseError;
    std::vector<PieceView> pieces;
    bool parseSuccess = false;

    size_t selectedPiece = 0;
    const auto reload = [&]() {
        data = IntersectionOrdering::Data{};
        parseSuccess = TryLoadData(rul0Path, data, parseError);
        pieces = parseSuccess ? BuildPieceViews(data) : std::vector<PieceView>{};
        selectedPiece = 0;
    };

    reload();

    constexpr auto screenWidth = 1600;
    constexpr auto screenHeight = 900;
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI);
    InitWindow(screenWidth, screenHeight, "SC4 RUL Parser - Puzzle Viewer");
    SetTargetFPS(144);

    rlImGuiSetup(true);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::GetIO().Fonts->Clear();
    ImFont* font = ImGui::GetIO().Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", 16.0f);
    if (font == NULL)
        font = ImGui::GetIO().Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttf", 16.0f); // Windows 7

    bool showPiecesPanel = true;
    bool showDemoWindow = false;
    bool run = true;

    while (!WindowShouldClose() && run) {
        BeginDrawing();
        ClearBackground(RAYWHITE);

        rlImGuiBegin();
        ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Reload", nullptr, false, true)) {
                    reload();
                }
                if (ImGui::MenuItem("Exit")) {
                    run = false;
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Window")) {
                ImGui::MenuItem("Puzzle Pieces", nullptr, &showPiecesPanel);
                ImGui::MenuItem("ImGui Demo", nullptr, &showDemoWindow);
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        if (showDemoWindow) {
            ImGui::ShowDemoWindow(&showDemoWindow);
        }

        if (showPiecesPanel) {
            ImGui::Begin("Puzzle Pieces");
            ImGui::TextUnformatted(std::format("Parsed from: {}", rul0Path).c_str());
            ImGui::Text("Loaded %zu puzzle pieces", pieces.size());
            if (!parseSuccess) {
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Unable to load file: %s", parseError.c_str());
            }

            ImGui::Separator();
            ImGui::Columns(2, "PuzzlePieceColumns", true);

            ImGui::BeginChild("PieceList", ImVec2(0, 0), true);
            if (pieces.empty()) {
                ImGui::TextUnformatted("No pieces available.");
            }
            else {
                for (size_t i = 0; i < pieces.size(); ++i) {
                    const bool selected = (i == selectedPiece);
                    if (ImGui::Selectable(pieces[i].shortDescription.c_str(), selected)) {
                        selectedPiece = i;
                    }
                }
            }
            ImGui::EndChild();

            ImGui::NextColumn();
            ImGui::BeginChild("PieceDetail", ImVec2(0, 0), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
            if (pieces.empty()) {
                ImGui::TextUnformatted("Select a piece to show its details.");
            }
            else {
                ImGui::TextUnformatted(pieces[selectedPiece].fullDetail.c_str());
            }
            ImGui::EndChild();

            ImGui::Columns(1);
            ImGui::End();
        }

        rlImGuiEnd();
        EndDrawing();
    }

    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
