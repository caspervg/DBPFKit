#include "RUL0.h"

#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <format>
#include <ranges>

#include "ParseTypes.h"
#include "ini.h"

namespace RUL0::ParseHelpers {
    std::string_view Trim(std::string_view s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
            s.remove_prefix(1);
        }
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
            s.remove_suffix(1);
        }
        return s;
    }

    bool ParseFloat(std::string_view s, float& out) {
        s = Trim(s);
        const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out, std::chars_format::general);
        return ec == std::errc() && ptr == s.data() + s.size();
    }

    bool ParseHex(std::string_view s, uint32_t& out) {
        s = Trim(s);
        if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            s = s.substr(2);
        }
        const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out, 16);
        return ec == std::errc() && ptr == s.data() + s.size();
    }

    bool EqualsIgnoreCase(const std::string_view a, const std::string_view b) {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); ++i) {
            const auto ca = static_cast<unsigned char>(a[i]);
            const auto cb = static_cast<unsigned char>(b[i]);
            if (std::tolower(ca) != std::tolower(cb)) {
                return false;
            }
        }
        return true;
    }

    bool StartsWithIgnoreCase(const std::string_view text, const std::string_view prefix) {
        if (prefix.size() > text.size()) {
            return false;
        }
        for (size_t i = 0; i < prefix.size(); ++i) {
            const auto ct = static_cast<unsigned char>(text[i]);
            const auto cp = static_cast<unsigned char>(prefix[i]);
            if (std::tolower(ct) != std::tolower(cp)) {
                return false;
            }
        }
        return true;
    }
}

namespace {
    using namespace RUL0::ParseHelpers;
}

namespace RUL0 {
    namespace {
        Grid NormalizeGrid(const Grid& grid, const char fillChar = kEmptyLayoutCell) {
            if (grid.empty()) {
                return {};
            }

            size_t width = 0;
            for (const auto& row : grid) {
                width = std::max(width, row.size());
            }

            Grid normalized;
            normalized.reserve(grid.size());
            for (const auto& row : grid) {
                std::string padded(width, fillChar);
                std::copy(row.begin(), row.end(), padded.begin());
                normalized.push_back(std::move(padded));
            }
            return normalized;
        }

        Grid RotateGrid90Clockwise(const Grid& grid, const char fillChar = kEmptyLayoutCell) {
            Grid normalized = NormalizeGrid(grid, fillChar);
            if (normalized.empty()) {
                return normalized;
            }

            const size_t height = normalized.size();
            const size_t width = normalized.front().size();
            Grid rotated(width, std::string(height, fillChar));

            for (size_t y = 0; y < height; ++y) {
                for (size_t x = 0; x < width; ++x) {
                    rotated[x][height - 1 - y] = normalized[y][x];
                }
            }

            return rotated;
        }

        Grid RotateGrid(const Grid& grid, int times, const char fillChar = kEmptyLayoutCell) {
            times = ((times % 4) + 4) % 4;
            Grid rotated = NormalizeGrid(grid, fillChar);
            for (int i = 0; i < times; ++i) {
                rotated = RotateGrid90Clockwise(rotated, fillChar);
            }
            return rotated;
        }

        Grid TransposeGrid(const Grid& grid, const char fillChar = kEmptyLayoutCell) {
            Grid normalized = NormalizeGrid(grid, fillChar);
            if (normalized.empty()) {
                return normalized;
            }

            const size_t height = normalized.size();
            const size_t width = normalized.front().size();
            Grid transposed(width, std::string(height, fillChar));

            for (size_t y = 0; y < height; ++y) {
                for (size_t x = 0; x < width; ++x) {
                    transposed[x][y] = normalized[y][x];
                }
            }

            return transposed;
        }

        Grid TranslateGrid(const Grid& grid, const uint32_t deltaX, const uint32_t deltaZ,
                           const char fillChar = kEmptyLayoutCell) {
            Grid normalized = NormalizeGrid(grid, fillChar);
            if (normalized.empty() || (deltaX == 0 && deltaZ == 0)) {
                return normalized;
            }

            const size_t height = normalized.size();
            const size_t width = normalized.front().size();

            Grid translated(height + deltaZ, std::string(width + deltaX, fillChar));
            for (size_t y = 0; y < height; ++y) {
                for (size_t x = 0; x < width; ++x) {
                    translated[y + deltaZ][x + deltaX] = normalized[y][x];
                }
            }

            return translated;
        }

        uint32_t TransposeEdgeFlags(const uint32_t flags) {
            const uint32_t south = (flags >> 24) & 0xFF;
            const uint32_t east = (flags >> 16) & 0xFF;
            const uint32_t north = (flags >> 8) & 0xFF;
            const uint32_t west = flags & 0xFF;

            const uint32_t newSouth = east;
            const uint32_t newEast = south;
            const uint32_t newNorth = west;
            const uint32_t newWest = north;

            return (newSouth << 24) | (newEast << 16) | (newNorth << 8) | newWest;
        }

        OneWayDir TransposeOneWayDir(const OneWayDir dir) {
            switch (dir) {
            case OneWayDir::WEST:
                return OneWayDir::NORTH;
            case OneWayDir::NORTH_WEST:
                return OneWayDir::NORTH_WEST;
            case OneWayDir::NORTH:
                return OneWayDir::WEST;
            case OneWayDir::NORTH_EAST:
                return OneWayDir::SOUTH_WEST;
            case OneWayDir::EAST:
                return OneWayDir::SOUTH;
            case OneWayDir::SOUTH_EAST:
                return OneWayDir::SOUTH_EAST;
            case OneWayDir::SOUTH:
                return OneWayDir::EAST;
            case OneWayDir::SOUTH_WEST:
                return OneWayDir::NORTH_EAST;
            default:
                return dir;
            }
        }
    }

    // Convert PuzzlePiece to human-readable string representation
    std::string PuzzlePiece::ToString() const {
        std::string result = std::format("Piece 0x{:08X}", id);

        // Add name if available
        if (!effect.name.empty()) {
            result += std::format(" - {}", effect.name);
        }

        // Add grid dimensions
        if (!cellLayout.empty()) {
            size_t cols = cellLayout.empty() ? 0 : cellLayout[0].size();
            result += std::format("\n  Grid: {} rows x {} cols", cellLayout.size(), cols);
        }

        // Add check types information
        if (!checkTypes.empty()) {
            result += std::format("\n  CheckTypes: {} defined", checkTypes.size());
            for (const auto& ct : checkTypes) {
                if (ct.initialized || true) {
                    result += std::format("\n    Symbol '{}': {} network(s)", ct.symbol, ct.networks.size());
                    for (const auto& net : ct.networks) {
                        result += " [";
                        switch (net.networkType) {
                        case NetworkType::ROAD:
                            result += "Road";
                            break;
                        case NetworkType::RAIL:
                            result += "Rail";
                            break;
                        case NetworkType::HIGHWAY:
                            result += "Highway";
                            break;
                        case NetworkType::STREET:
                            result += "Street";
                            break;
                        case NetworkType::PIPE:
                            result += "Pipe";
                            break;
                        case NetworkType::POWERLINE:
                            result += "Powerline";
                            break;
                        case NetworkType::AVENUE:
                            result += "Avenue";
                            break;
                        case NetworkType::SUBWAY:
                            result += "Subway";
                            break;
                        case NetworkType::LIGHT_RAIL:
                            result += "LightRail";
                            break;
                        case NetworkType::MONORAIL:
                            result += "Monorail";
                            break;
                        case NetworkType::ONE_WAY_ROAD:
                            result += "OneWayRoad";
                            break;
                        case NetworkType::DIRT_ROAD:
                            result += "DirtRoad";
                            break;
                        case NetworkType::GROUND_HIGHWAY:
                            result += "GroundHighway";
                            break;
                        default:
                            result += "Unknown";
                            break;
                        }
                        result += "]";
                    }
                }
            }
        }

        // Add preview effect information
        if (effect.initialized) {
            result += std::format("\n  Preview: pos({}, {}), rot={}, flip={}",
                                  effect.x, effect.y, effect.rotation, effect.flip);
        }

        // Add base IDs if they're set
        if (autoTileBase != 0xFFFFFFFF) {
            result += std::format("\n  AutoTileBase: 0x{:08X}", autoTileBase);
        }
        if (autoPathBase != 0xFFFFFFFF) {
            result += std::format("\n  AutoPathBase: 0x{:08X}", autoPathBase);
        }

        // Add transformation info if applicable
        if (copyFrom != 0) {
            result += std::format("\n  CopyFrom: 0x{:08X}", copyFrom);
        }
        if (rotate != Rotation::NONE) {
            result += std::format("\n  Rotate: {} (90° increments)", +rotate);
        }
        if (transpose) {
            result += "\n  Transpose: true";
        }
        if (translate.initialized) {
            result += std::format("\n  Translate: ({}, {})", translate.x, translate.z);
        }

        // Add costs if set
        if (costs != 0xFFFFFFFF) {
            result += std::format("\n  Costs: {}", costs);
        }

        // Add OneWayDir if set
        if (oneWayDir != OneWayDir::NONE) {
            result += std::format("\n  OneWayDir: {}", +oneWayDir);
        }

        return result;
    }

    Grid PuzzlePiece::NormalizedCellLayout(const char fillChar) const {
        return NormalizeGrid(cellLayout, fillChar);
    }

    Grid PuzzlePiece::NormalizedConsLayout(const char fillChar) const {
        return NormalizeGrid(consLayout, fillChar);
    }

    LayoutSample PuzzlePiece::SampleLayout(const size_t row, const size_t col,
                                           const char fillChar) const {
        LayoutSample sample{};
        sample.row = row;
        sample.col = col;

        const Grid normCell = NormalizeGrid(cellLayout, fillChar);
        if (!normCell.empty() && row < normCell.size() && col < normCell.front().size()) {
            sample.cell = normCell[row][col];
            sample.hasCell = true;
        }

        const Grid normCons = NormalizeGrid(consLayout, fillChar);
        if (!normCons.empty() && row < normCons.size() && col < normCons.front().size()) {
            sample.cons = normCons[row][col];
            sample.hasCons = true;
        }

        if (sample.hasCell && sample.cell != fillChar) {
            for (const auto& checkType : checkTypes) {
                if (checkType.symbol == sample.cell) {
                    sample.checkType = &checkType;
                    break;
                }
            }
        }

        return sample;
    }

    uint32_t ParsePieceId(std::string_view section) {
        const auto prefixLength = strlen(kIntersectionInfoPrefix);
        const auto val = section.substr(prefixLength, section.size() - prefixLength);
        return std::strtoul(val.data(), nullptr, 16);
    }


    std::vector<uint32_t> ParseIdList(std::string_view value) {
        std::vector<uint32_t> result;
        size_t start = 0;

        while (start < value.size()) {
            size_t end = value.find(kListDelimiter, start);
            if (end == std::string_view::npos) {
                end = value.size();
            }
            auto idStr = value.substr(start, end - start);
            result.push_back(std::strtoul(idStr.data(), nullptr, 16));
            start = end + 1;
        }

        return result;
    }

    bool ParsePieceValue(std::string_view value, PreviewEffect& previewEffect) {
        float x, y; // The game reads these specifically in English number formatting
        int rotation, flip; // The game reads these as %i (which can also be octal or hexadecimal format), so we do too
        uint32_t instanceId;
        std::string name;

        std::string_view parts[5];
        size_t count = 0;
        size_t start = 0;
        size_t semi = value.find(kCommentPrefix);
        if (semi != std::string_view::npos) {
            value = value.substr(0, semi);
        }

        while (start < value.size() && count < 5) {
            size_t comma = value.find(kListDelimiter, start);
            if (comma == std::string_view::npos) comma = value.size();
            parts[count++] = Trim(value.substr(start, comma - start));
            start = comma + 1;
        }
        if (count != 5) return false;

        if (!ParseFloat(parts[0], x) || !ParseFloat(parts[1], y) ||
            !ParseIntAuto(parts[2], rotation) || !ParseIntAuto(parts[3], flip) ||
            !ParseHex(parts[4], instanceId)) {
            return false;
        }

        previewEffect.initialized = true;
        previewEffect.x = x;
        previewEffect.y = y;
        previewEffect.rotation = rotation;
        previewEffect.flip = flip;
        previewEffect.instanceId = instanceId;
        return true;
    }

    CheckType ParseCheckType(std::string_view value) {
        CheckType ct;
        ct.symbol = value[0];

        // Find dash and get everything after it
        size_t dashPos = value.find('-');
        if (dashPos == std::string_view::npos)
            return ct;

        std::string_view rest = value.substr(dashPos + 1);

        // Helper lambda to get the next token
        auto nextToken = [&rest]() -> std::string_view {
            // Skip whitespace
            while (!rest.empty() && (rest[0] == ' ' || rest[0] == '\t')) {
                rest.remove_prefix(1);
            }
            if (rest.empty())
                return {};

            // Find end of token (space, comma, colon, or end)
            size_t end = rest.find_first_of(" \t,:");
            std::string_view token = rest.substr(0, end);
            rest.remove_prefix(end == std::string_view::npos ? rest.size() : end);
            return token;
        };

        auto expectChar = [&rest](char c) -> bool {
            while (!rest.empty() && (rest[0] == ' ' || rest[0] == '\t')) {
                rest.remove_prefix(1);
            }
            if (!rest.empty() && rest[0] == c) {
                rest.remove_prefix(1);
                return true;
            }
            return false;
        };

        // Parse loop
        while (!rest.empty()) {
            auto token = nextToken();
            if (token.empty())
                break;

            if (token == "optional") {
                if (!ct.networks.empty())
                    ct.networks.back().optional = true;
            }
            else if (token == "check") {
                if (!ct.networks.empty())
                    ct.networks.back().check = true;
            }
            else if (expectChar(':')) {
                // It's a network definition
                NetworkCheck nc;
                nc.networkType = ParseNetworkType(token);

                auto flags = nextToken();
                nc.ruleFlagByte = std::stoul(std::string(flags), nullptr, 16);

                if (expectChar(',')) {
                    auto mask = nextToken();
                    nc.hexMask = std::stoul(std::string(mask.substr(0, std::min(mask.length(), 10zu))), nullptr, 16);
                }

                ct.networks.push_back(nc);
            }
        }

        return ct;
    }

    int IniHandler(void* user, const char* section, const char* key, const char* value) {
        auto* data = static_cast<Record*>(user);
        const auto secStr = std::string_view(section);
        const auto keyStr = std::string_view(key);
        const auto valStr = std::string_view(value);

        // We are either in the Ordering section or in a sectionless preamble
        if (EqualsIgnoreCase(secStr, kOrderingSection) || secStr.empty()) {
            if (EqualsIgnoreCase(keyStr, kRotationRingKey)) {
                // Start a new ordering when we discovered a new RotationRing entry
                data->orderings.emplace_back();
                data->orderings.back().rotationRing = ParseIdList(valStr);
            }
            else if (EqualsIgnoreCase(keyStr, kAddTypesKey)) {
                if (data->orderings.empty()) {
                    // Malformed RUL0: AddTypes before RotationRing
                    return 0;
                }
                data->orderings.back().addTypes.push_back(ParseIdList(valStr));
            }
            else {
                // Malformed RUL0: Unknown key
                return 0;
            }

            return 1;
        }

        // We have found a HighwayIntersectionInfo section
        if (StartsWithIgnoreCase(secStr, kIntersectionInfoPrefix)) {
            const uint32_t id = ParsePieceId(secStr);

            // We are starting a new puzzle piece
            if (!data->currentPiece || data->currentPiece->id != id) {
                // Fetch or create the puzzle piece
                data->currentPiece = &data->puzzlePieces[id];
                data->currentPiece->id = id;
            }

            auto* piece = data->currentPiece;

            if (EqualsIgnoreCase(keyStr, kPieceKey)) {
                ParsePieceValue(valStr, piece->effect);
            }
            else if (EqualsIgnoreCase(keyStr, kPreviewEffectKey)) {
                piece->effect.name = std::string(valStr);
            }
            else if (EqualsIgnoreCase(keyStr, kCellLayoutKey)) {
                piece->cellLayout.emplace_back(valStr);
            }
            else if (EqualsIgnoreCase(keyStr, kCheckTypeKey)) {
                piece->checkTypes.push_back(ParseCheckType(valStr));
            }
            else if (EqualsIgnoreCase(keyStr, kConsLayoutKey)) {
                piece->consLayout.emplace_back(valStr);
            }
            else if (EqualsIgnoreCase(keyStr, kAutoPathBaseKey)) {
                piece->autoPathBase = std::strtoul(value, nullptr, 16);
            }
            else if (EqualsIgnoreCase(keyStr, kAutoTileBaseKey)) {
                piece->autoTileBase = std::strtoul(value, nullptr, 16);
            }
            else if (StartsWithIgnoreCase(keyStr, kReplacementIntersectionKey)) {
                int replRotation;
                uint32_t replFlip;
                if (!ParseIntPair(value, replRotation, replFlip)) {
                    // Invalid ReplacementIntersection format
                    return 0;
                }
                if (replRotation < +Rotation::ROT_0 || replRotation > +Rotation::ROT_270) {
                    // Invalid rotation
                    return 0;
                }
                piece->replacementIntersection = {
                    true,
                    static_cast<Rotation>(replRotation),
                    replFlip
                };
            }
            else if (EqualsIgnoreCase(keyStr, kPlaceQueryIdKey)) {
                piece->placeQueryId = std::strtoul(value, nullptr, 16);
            }
            else if (EqualsIgnoreCase(keyStr, kCostsKey)) {
                if (!valStr.empty()) {
                    piece->costs = std::stoi(value);
                }
                else {
                    piece->costs = 0;
                }
            }
            else if (EqualsIgnoreCase(keyStr, kConvertQueryIdKey)) {
                piece->convertQueryId = std::strtoul(value, nullptr, 16);
            }
            else if (EqualsIgnoreCase(keyStr, kAutoPlaceKey)) {
                piece->autoPlace = (std::stoi(value) != 0);
            }
            else if (EqualsIgnoreCase(keyStr, kHandleOffsetKey)) {
                if (ParseIntPair(value,
                                 piece->handleOffset.deltaStraight,
                                 piece->handleOffset.deltaSide)) {
                    piece->handleOffset.initialized = true;
                }
            }
            else if (EqualsIgnoreCase(keyStr, kStepOffsetsKey)) {
                if (ParseIntPair(value,
                                 piece->stepOffsets.dragStartThreshold,
                                 piece->stepOffsets.dragCompletionOffset)) {
                    piece->stepOffsets.initialized = true;
                }
            }
            else if (EqualsIgnoreCase(keyStr, kOneWayDirKey)) {
                const auto val = std::stoi(value);
                if (val < +OneWayDir::WEST || val > +OneWayDir::SOUTH_WEST) {
                    // Invalid OneWayDir value
                    return 0;
                }
                piece->oneWayDir = static_cast<OneWayDir>(val);
            }
            else if (EqualsIgnoreCase(keyStr, kCopyFromKey)) {
                piece->copyFrom = std::strtoul(value, nullptr, 16);
                piece->requestedTransform.copyFrom = piece->copyFrom;
            }
            else if (EqualsIgnoreCase(keyStr, kRotateKey)) {
                const auto val = std::stoi(value);
                if (val < +Rotation::ROT_0 || val > +Rotation::ROT_270) {
                    // Invalid rotation value
                    return 0;
                }
                piece->rotate = static_cast<Rotation>(val);
                piece->requestedTransform.rotate = piece->rotate;
            }
            else if (EqualsIgnoreCase(keyStr, kTransposeKey)) {
                piece->transpose = (std::stoi(value) != 0);
                piece->requestedTransform.transpose = piece->transpose;
            }
            else if (EqualsIgnoreCase(keyStr, kTranslateKey)) {
                // This key is not documented, but present in SC4 game decompilation, so included.
                if (ParseIntPair(value, piece->translate.x, piece->translate.z)) {
                    piece->translate.initialized = true;
                    piece->requestedTransform.translate = piece->translate;
                }
            }
            else {
                // Malformed RUL0: Unknown key in HighwayIntersectionInfo section
                return 0;
            }
        }

        return 1;
    }

    // rotation: 0=0°, 1=90°CW, 2=180°, 3=270°CW
    void RotatePoint(float& x, float& y, int rotation) {
        rotation = rotation & 3; // Normalize to 0-3

        if (rotation == 1) {
            // 90° CW: (x, y) -> (y, -x)
            const float temp = y;
            y = x;
            x = -temp;
        }
        else if (rotation == 2) {
            // 180°: (x, y) -> (-x, -y)
            x = -x;
            y = -y;
        }
        else if (rotation == 3) {
            // 270° CW: (x, y) -> (-y, x)
            float temp = x;
            x = y;
            y = -temp;
        }
    }

    // Based on SC4's RotateEdgeFlags decompilation
    // return param_1 << (param_2 & 3) * 8 | param_1 >> (param_2 & 3) * -8 + 0x20
    // This rotates a 32-bit flag value by shifting left by rotation*8 bits
    uint32_t RotateEdgeFlags(const uint32_t flags, int rotation) {
        rotation = rotation & 3; // Normalize to 0-3
        const auto shiftBits = rotation * 8;
        return (flags << shiftBits) | (flags >> (32 - shiftBits));
    }

    // Copy all data from source piece to destination piece
    // Preserves the destination's ID and its PlaceQueryID
    void CopyPuzzlePiece(const PuzzlePiece& source, PuzzlePiece& dest) {
        dest.effect = source.effect;
        dest.cellLayout = source.cellLayout;
        dest.checkTypes = source.checkTypes;
        dest.consLayout = source.consLayout;

        dest.autoPathBase = source.autoPathBase;
        dest.autoTileBase = source.autoTileBase;

        dest.replacementIntersection = source.replacementIntersection;

        dest.costs = source.costs;
        dest.convertQueryId = source.convertQueryId;

        dest.autoPlace = source.autoPlace;

        dest.handleOffset = source.handleOffset;
        dest.stepOffsets = source.stepOffsets;

        dest.oneWayDir = source.oneWayDir;
    }

    // Apply rotation transformation to piece
    // Rotates grids, edge flags, constraints, preview effect, and OneWayDir
    // Based on SC4's Rotate method decompilation
    void ApplyRotation(PuzzlePiece& piece, const Rotation rotation) {
        if (rotation == Rotation::NONE || rotation == Rotation::ROT_0) {
            return;
        }

        const int times = +rotation; // Convert enum to int (0-3)

        // Rotate layouts
        if (!piece.cellLayout.empty()) {
            piece.cellLayout = RotateGrid(piece.cellLayout, times);
        }
        if (!piece.consLayout.empty()) {
            piece.consLayout = RotateGrid(piece.consLayout, times);
        }

        // Rotate preview effect position and rotation
        if (piece.effect.initialized) {
            // RotatePoint for the preview effect coordinates
            RotatePoint(piece.effect.x, piece.effect.y, times);
            // Update rotation: increment by times*90° and mask to 0-359
            piece.effect.rotation = (piece.effect.rotation + times * 90) % 360;
        }

        // Rotate OneWayDir if set
        if (piece.oneWayDir != OneWayDir::NONE) {
            // OneWayDir has 8 directions (0-7), rotate by 2 positions per rotation unit
            int dirValue = +piece.oneWayDir;
            if (dirValue < 8) {
                dirValue = (dirValue + times * 2) % 8;
                piece.oneWayDir = static_cast<OneWayDir>(dirValue);
            }
        }

        // Rotate edge flags in CheckTypes
        // UNCERTAIN: How to identify which CheckTypes need edge flag rotation
        for (auto& checkType : piece.checkTypes) {
            for (auto& network : checkType.networks) {
                // Rotate the rule flag byte
                network.ruleFlagByte = RotateEdgeFlags(network.ruleFlagByte, times);
                network.hexMask = RotateEdgeFlags(network.hexMask, times);
            }
        }

        // Clear the rotation field since we've applied it
        piece.rotate = Rotation::NONE;
    }

    // Apply transpose transformation to piece
    // Flips the grids and updates related fields
    void ApplyTranspose(PuzzlePiece& piece) {
        if (!piece.transpose) {
            return;
        }

        if (!piece.cellLayout.empty()) {
            piece.cellLayout = TransposeGrid(piece.cellLayout);
        }

        if (!piece.consLayout.empty()) {
            piece.consLayout = TransposeGrid(piece.consLayout);
        }

        // Update effect flip state
        if (piece.effect.initialized) {
            std::swap(piece.effect.x, piece.effect.y);
            piece.effect.flip = (piece.effect.flip == 0) ? 1 : 0;
        }

        if (piece.oneWayDir != OneWayDir::NONE) {
            piece.oneWayDir = TransposeOneWayDir(piece.oneWayDir);
        }

        for (auto& checkType : piece.checkTypes) {
            for (auto& network : checkType.networks) {
                network.ruleFlagByte = TransposeEdgeFlags(network.ruleFlagByte);
                network.hexMask = TransposeEdgeFlags(network.hexMask);
            }
        }

        // Clear the transpose flag since we've applied it
        piece.transpose = false;
    }

    // Apply translation transformation to piece
    void ApplyTranslation(PuzzlePiece& piece) {
        if (!piece.translate.initialized) {
            return;
        }

        if (!piece.cellLayout.empty()) {
            piece.cellLayout = TranslateGrid(piece.cellLayout, piece.translate.x, piece.translate.z);
        }
        if (!piece.consLayout.empty()) {
            piece.consLayout = TranslateGrid(piece.consLayout, piece.translate.x, piece.translate.z);
        }

        // Apply translation to preview effect position
        if (piece.effect.initialized) {
            piece.effect.x += piece.translate.x;
            piece.effect.y += piece.translate.z; // Note: translate.z is the Y coordinate
        }

        // Clear the translation since we've applied it
        piece.translate.initialized = false;
    }

    // Build navigation indices and apply transformations
    void BuildNavigationIndices(Record& data) {
        // Create a list of pieces sorted by ID to process them in order
        std::vector<uint32_t> pieceIds;
        pieceIds.reserve(data.puzzlePieces.size());

        for (const auto& id : data.puzzlePieces | std::views::keys) {
            pieceIds.push_back(id);
        }

        // Sort by ID to ensure dependencies are processed first
        // (CopyFrom source IDs must be lower than destination ID per RUL loading order)
        std::sort(pieceIds.begin(), pieceIds.end());

        // Process each piece and apply transformations
        for (uint32_t id : pieceIds) {
            auto& piece = data.puzzlePieces[id];

            // Snapshot requested transform before we mutate fields
            piece.requestedTransform.copyFrom = piece.copyFrom;
            piece.requestedTransform.rotate = piece.rotate;
            piece.requestedTransform.transpose = piece.transpose;
            piece.requestedTransform.translate = piece.translate;

            // If this piece copies from another, copy the source data first
            if (piece.copyFrom != 0) {
                auto it = data.puzzlePieces.find(piece.copyFrom);
                if (it != data.puzzlePieces.end()) {
                    const auto& source = it->second;
                    // Copy data from source piece (but preserve the destination's ID)
                    CopyPuzzlePiece(source, piece);
                    piece.id = id; // Ensure ID is not overwritten
                }
            }

            piece.appliedTransform = piece.requestedTransform;

            // Apply transformations in order: Rotate -> Transpose -> Translate
            ApplyRotation(piece, piece.rotate);
            ApplyTranspose(piece);
            ApplyTranslation(piece);
        }
    }

    ParseExpected<Record> Parse(const std::span<const uint8_t> buffer) {
        Record data;
        const auto text = reinterpret_cast<const char*>(buffer.data());
        const int parseResult = ini_parse_string_length(text, buffer.size(), IniHandler, &data);
        if (parseResult != 0) {
            if (parseResult > 0) {
                return Fail("Failed to parse RUL0 data at line {}", parseResult);
            }
            return Fail("Failed to parse RUL0 data");
        }
        BuildNavigationIndices(data);
        return data;
    }
}
