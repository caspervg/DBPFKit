#include <iostream>
#include <ostream>
#include <print>
#include "RUL0.h"
#include "ini.h"

auto main() -> int {
    IntersectionOrdering::Data data;
    if (ini_parse("../examples/rul0/nam_full.txt", IntersectionOrdering::IniHandler, &data) < 0) {
        std::println("An error occurred during parsing");
    }

    for (auto& [id, piece] : data.puzzlePieces) {
        std::cout << "Piece 0x" << std::hex << id << ": " << piece.effect.name << "\n";
    }
}
