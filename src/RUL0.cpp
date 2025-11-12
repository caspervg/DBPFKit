#pragma once

#include "RUL0.h"

#include <format>
#include <ranges>

#include "ParseTypes.h"
#include "ini.h"

namespace RUL0 {
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
            result += std::format("\n  Preview: pos({}, {}), rot={}°, flip={}",
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
        const auto res = sscanf_s(value.data(), "%f, %f, %i, %i, 0x%x", &x, &y, &rotation, &flip, &instanceId);
        if (res != 5) {
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
                    nc.hexMask = std::stoul(std::string(mask), nullptr, 16);
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
        if (secStr == kOrderingSection || secStr.empty()) {
            if (keyStr == kRotationRingKey) {
                // Start a new ordering when we discovered a new RotationRing entry
                data->orderings.emplace_back();
                data->orderings.back().rotationRing = ParseIdList(valStr);
            }
            else if (keyStr == kAddTypesKey) {
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
        if (secStr.starts_with(kIntersectionInfoPrefix)) {
            const uint32_t id = ParsePieceId(secStr);

            // We are starting a new puzzle piece
            if (!data->currentPiece || data->currentPiece->id != id) {
                // Fetch or create the puzzle piece
                data->currentPiece = &data->puzzlePieces[id];
                data->currentPiece->id = id;
            }

            auto* piece = data->currentPiece;

            if (keyStr == kPieceKey) {
                ParsePieceValue(valStr, piece->effect);
            }
            else if (keyStr == kPreviewEffectKey) {
                piece->effect.name = std::string(valStr);
            }
            else if (keyStr == kCellLayoutKey) {
                piece->cellLayout.push_back(std::string(valStr));
            }
            else if (keyStr == kCheckTypeKey) {
                piece->checkTypes.push_back(ParseCheckType(valStr));
            }
            else if (keyStr == kConsLayoutKey) {
                piece->consLayout.push_back(std::string(valStr));
            }
            else if (keyStr == kAutoPathBaseKey) {
                piece->autoPathBase = std::strtoul(value, nullptr, 16);
            }
            else if (keyStr == kAutoTileBaseKey) {
                piece->autoTileBase = std::strtoul(value, nullptr, 16);
            }
            else if (keyStr == kReplacementIntersectionKey) {
                int replRotation;
                uint32_t replFlip;
                auto const ret = sscanf_s(value, "%d, %d", &replRotation, &replFlip);
                if (ret != 2) {
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
            else if (keyStr == kPlaceQueryIdKey) {
                piece->placeQueryId = std::strtoul(value, nullptr, 16);
            }
            else if (keyStr == kCostsKey) {
                if (valStr.size() > 0) {
                    piece->costs = std::stoi(value);
                }
                else {
                    piece->costs = 0;
                }
            }
            else if (keyStr == kConvertQueryIdKey) {
                piece->convertQueryId = std::strtoul(value, nullptr, 16);
            }
            else if (keyStr == kAutoPlaceKey) {
                piece->autoPlace = (std::stoi(value) != 0);
            }
            else if (keyStr == kHandleOffsetKey) {
                const auto ret = sscanf_s(value,
                                          "%d, %d",
                                          &piece->handleOffset.deltaStraight,
                                          &piece->handleOffset.deltaSide
                    );
                if (ret == 2) {
                    piece->stepOffsets.initialized = true;
                }
            }
            else if (keyStr == kStepOffsetsKey) {
                const auto ret = sscanf_s(value,
                                          "%d, %d",
                                          &piece->stepOffsets.dragStartThreshold,
                                          &piece->stepOffsets.dragCompletionOffset
                    );
                if (ret == 2) {
                    piece->stepOffsets.initialized = true;
                }
            }
            else if (keyStr == kOneWayDirKey) {
                const auto val = std::stoi(value);
                if (val < +OneWayDir::WEST || val > +OneWayDir::SOUTH_WEST) {
                    // Invalid OneWayDir value
                    return 0;
                }
                piece->oneWayDir = static_cast<OneWayDir>(val);
            }
            else if (keyStr == kCopyFromKey) {
                piece->copyFrom = std::strtoul(value, nullptr, 16);
                // TODO: Actually do something with this!
            }
            else if (keyStr == kRotateKey) {
                const auto val = std::stoi(value);
                if (val < +Rotation::ROT_0 || val > +Rotation::ROT_270) {
                    // Invalid rotation value
                    return 0;
                }
                piece->rotate = static_cast<Rotation>(val);
            }
            else if (keyStr == kTransposeKey) {
                piece->transpose = (std::stoi(value) != 0);
            }
            else if (keyStr == kTranslateKey) {
                // This key is not documented, but present in SC4 game decompilation, so included.
                sscanf_s(value, "%d, %d", &piece->translate.x, &piece->translate.z);
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

    // RotateConstraint: Map constraint bytes to rotated equivalents
    // Based on SC4's RotateConstraint decompilation
    uint8_t RotateConstraint(uint8_t constraint) {
        if (constraint == 4) {
            return 3;
        }
        else if (constraint < 4) {
            if (constraint == 2) {
                return 1;
            }
            else if (constraint < 2) {
                if (constraint != 0) {
                    return 2;
                }
            }
            else {
                return 4;
            }
        }
        else if (constraint == 6) {
            return 5;
        }
        else if (constraint < 6) {
            return 6;
        }
        return constraint;
    }

    // RotateMap: Rotate a byte map (grid) with optional constraint rotation
    // Based on SC4's RotateMap decompilation
    //
    // UNCERTAIN ASPECTS:
    // - The exact memory layout and how dimensions are used
    // - Whether centerX/centerY are grid coordinates or offsets
    // - The precise order of transpose vs flip operations in 90° rotation
    // - Whether the constraint rotation only applies to specific cell types
    //
    // The logic appears to be:
    // 1. If bit 2 set (180°): flip the entire map and negate center coords
    // 2. If bit 1 set (90°): transpose and rotate, optionally rotating constraints
    void RotateMap(std::vector<uint8_t>& mapData, int& width, int& height, int& centerX, int& centerY,
                   int rotation, bool rotateConstraints) {
        rotation = rotation & 3; // Normalize to 0-3

        if (rotation == 0 || mapData.empty()) {
            return;
        }

        // Handle 180° rotation (bit 2 set)
        if ((rotation & 2) != 0) {
            centerX = (width - 1) - centerX;
            centerY = (height - 1) - centerY;

            // Reverse the entire linear map
            std::reverse(mapData.begin(), mapData.end());
        }

        // Handle 90° rotation (bit 1 set)
        if ((rotation & 1) != 0) {
            std::vector<uint8_t> rotated(mapData.size());
            int newWidth = height;
            int newHeight = width;

            // GUESS: Based on decompilation, transpose with constraint rotation
            // The decompilation shows: new[x + y*newWidth] = old[(height-1-y) + x*width]
            // This is a 90° CW rotation
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    int oldIdx = y * width + x;
                    int newX = height - 1 - y;
                    int newY = x;
                    int newIdx = newY * newWidth + newX;

                    if (rotateConstraints) {
                        rotated[newIdx] = RotateConstraint(mapData[oldIdx]);
                    }
                    else {
                        rotated[newIdx] = mapData[oldIdx];
                    }
                }
            }

            mapData = rotated;

            // Update dimensions
            std::swap(width, height);

            // Update center position after rotation
            // GUESS: Based on decompilation centerX/centerY swap and negation
            int oldCenterX = centerX;
            centerX = newHeight - 1 - centerY;
            centerY = oldCenterX;
        }
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
    void ApplyRotation(PuzzlePiece& piece, Rotation rotation) {
        if (rotation == Rotation::NONE || rotation == Rotation::ROT_0) {
            return;
        }

        const int times = +rotation; // Convert enum to int (0-3)

        // NOTE: cellLayout and consLayout in our parser are stored as strings (grid rows)
        // SC4 uses byte maps. We need to convert our string grids to byte maps for RotateMap
        // UNCERTAIN: How to map string grids to byte maps and back
        // For now, we'll rotate the preview effect as a proxy

        // Rotate preview effect position and rotation
        if (piece.effect.initialized) {
            // RotatePoint for the preview effect coordinates
            RotatePoint(piece.effect.x, piece.effect.y, times);
            // Update rotation: increment by times*90° and mask to 0-359
            piece.effect.rotation = (piece.effect.rotation + times * 90) % 360;
        }

        // Rotate OneWayDir if set
        // Based on SC4 decompilation: direction += rotation (or -= if inverted flag is set)
        // UNCERTAIN: Whether the inversion flag applies and exact mapping
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

        // Transpose is a diagonal flip of the grids
        // UNCERTAIN: Exact semantics - whether it's diagonal flip or mirror
        // For string grids, we can swap rows with columns

        // Transpose cellLayout
        if (!piece.cellLayout.empty()) {
            // Find the maximum column size across all rows
            size_t maxCols = 0;
            for (const auto& row : piece.cellLayout) {
                maxCols = std::max(maxCols, row.size());
            }

            if (maxCols > 0) {
                const auto rows = piece.cellLayout.size();
                std::vector transposed(maxCols, std::string(rows, ' '));

                for (auto row = 0; row < rows; ++row) {
                    for (auto col = 0; col < piece.cellLayout[row].size(); ++col) {
                        transposed[col][row] = piece.cellLayout[row][col];
                    }
                }
                piece.cellLayout = transposed;
            }
        }

        // Transpose consLayout
        if (!piece.consLayout.empty()) {
            // Find the maximum column size across all rows
            size_t maxCols = 0;
            for (const auto& row : piece.consLayout) {
                maxCols = std::max(maxCols, row.size());
            }

            if (maxCols > 0) {
                const auto rows = piece.consLayout.size();
                std::vector transposed(maxCols, std::string(rows, ' '));

                for (auto row = 0; row < rows; ++row) {
                    for (size_t col = 0; col < piece.consLayout[row].size(); ++col) {
                        transposed[col][row] = piece.consLayout[row][col];
                    }
                }
                piece.consLayout = transposed;
            }
        }

        // Update effect flip state
        if (piece.effect.initialized) {
            piece.effect.flip = (piece.effect.flip == 0) ? 1 : 0;
        }

        // Clear the transpose flag since we've applied it
        piece.transpose = false;
    }

    // Apply translation transformation to piece
    void ApplyTranslation(PuzzlePiece& piece) {
        if (!piece.translate.initialized) {
            return;
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

            // Apply transformations in order: Rotate -> Transpose -> Translate
            ApplyRotation(piece, piece.rotate);
            ApplyTranspose(piece);
            ApplyTranslation(piece);
        }
    }

    ParseExpected<Record> Parse(std::span<const uint8_t> buffer) {
        Record data;
        auto text = reinterpret_cast<const char*>(buffer.data());
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
