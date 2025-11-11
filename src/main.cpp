#include <ostream>
#include <print>
#include <ranges>

#include "RUL0.h"
#include "ini.h"
#include "DBPFReader.h"
#include "Exemplar.h"

struct ExemplarLoadResult {
    std::optional<Exemplar::Record> exemplar;
    std::string error;
};

ExemplarLoadResult LoadExemplar(const DBPF::Reader& reader,
                                const DBPF::IndexEntry& entry) {
    ExemplarLoadResult out;

    if (entry.tgi.type != 0x6534284A) {
        out.error = "not an exemplar TGI";
        return out;
    }

    auto payload = reader.ReadEntryData(entry);
    if (!payload) {
        out.error = "failed to read payload (offset/size mismatch?)";
        return out;
    }

    std::println("[{}] size={} label={} head={:02X} {:02X} {:02X} {:02X}",
                 entry.tgi.ToString(),
                 entry.decompressedSize.value_or(entry.size),
                 DBPF::Describe(entry.tgi),
                 (*payload)[0], (*payload)[1], (*payload)[2], (*payload)[3]);

    auto parsed = Exemplar::Parse(payload->data(), payload->size());
    if (!parsed.success) {
        out.error = parsed.errorMessage.empty()
            ? "parse failed without message"
            : parsed.errorMessage;
        return out;
    }

    out.exemplar = std::move(parsed.record);
    return out;
}

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
        std::println("{} size:{}, type:{}", entry.tgi.ToString(), entry.decompressedSize.value_or(entry.size),
                     DBPF::Describe(entry.tgi));
        if (entry.tgi.type != 0x6534284A)
            continue;

        auto payload = reader.ReadEntryData(entry);
        if (!payload) {
            std::println("Exemplar {}: failed to read payload", entry.tgi.ToString());
            continue;
        }

        std::println("[{}] size={} label={} head={:02X} {:02X} {:02X} {:02X}",
                     entry.tgi.ToString(),
                     payload->size(),
                     DBPF::Describe(entry.tgi),
                     (*payload)[0], (*payload)[1], (*payload)[2], (*payload)[3]);

        auto result = Exemplar::Parse(payload->data(), payload->size());
        if (!result.success) {
            std::println("Exemplar {} failed: {}", entry.tgi.ToString(), result.errorMessage);
            continue;
        } else {
            std::println("Exemplar {} parsed successfully", entry.tgi.ToString());
            for (auto item : result.record.properties) {
                std::println("  {}", item.ToString());
            }
        }

        std::println("Parent {}", result.record.parent.ToString());
    }
}
