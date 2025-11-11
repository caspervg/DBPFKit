#include <ostream>
#include <print>
#include <ranges>

#include "RUL0.h"
#include "ini.h"
#include "DBPFReader.h"

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
        if (!piece.copyFrom) {
            std::println("{}", piece.ToString());
            std::println("");
            count++;
        }
    }
    std::println("{}", count);

    DBPF::Reader reader;
    reader.LoadFile("../examples/dat/SM2 Mega Prop Pack Vol1.dat");

    for (const auto& entry : reader.GetIndex()) {
        std::println("{} size:{}, type:{}", entry.tgi.ToString(), entry.decompressedSize.value_or(entry.size), DBPF::Describe(entry.tgi));
    }
}
