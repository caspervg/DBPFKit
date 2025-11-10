#pragma once

#include "RUL0.h"

namespace IntersectionOrdering {
    int IniHandler(void* user, const char* section, const char* key, const char* value) {
        auto* data = static_cast<Data*>(user);
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
                } else {
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

}
