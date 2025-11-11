#include <ostream>
#include <print>
#include <ranges>

#include "RUL0.h"
#include "ini.h"

auto main() -> int {
    IntersectionOrdering::Data data;
    if (ini_parse("../examples/rul0/nam_full.txt", IntersectionOrdering::IniHandler, &data) < 0) {
        std::println("An error occurred during parsing");
    }

    // Apply transformations (CopyFrom, Rotate, Transpose, Translate)
    IntersectionOrdering::BuildNavigationIndices(data);

    std::println("Loaded {} puzzle pieces", data.puzzlePieces.size());
    std::println("");

    // Display a few pieces as examples
    auto count = 0;
    for (auto& piece : data.puzzlePieces | std::views::values) {
        if (!piece.copyFrom || true) {
            std::println("{}", piece.ToString());
            std::println("");
            count++;
        }
    }
    std::println("{}", count);
}
