#pragma once
#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "ParseTypes.h"

namespace RUL0 {

    constexpr auto kListDelimiter = ',';

    constexpr auto kOrderingSection = "Ordering";
    constexpr auto kRotationRingKey = "RotationRing";
    constexpr auto kAddTypesKey = "AddTypes";
    constexpr auto kIntersectionInfoPrefix = "HighwayIntersectionInfo_";

    constexpr auto kPieceKey = "Piece";
    constexpr auto kPreviewEffectKey = "PreviewEffect";
    constexpr auto kCellLayoutKey = "CellLayout";
    constexpr auto kConsLayoutKey = "ConsLayout";
    constexpr auto kCheckTypeKey = "CheckType";
    constexpr auto kAutoTileBaseKey = "AutoTileBase";
    constexpr auto kAutoPathBaseKey = "AutoPathBase";
    constexpr auto kPlaceQueryIdKey = "PlaceQueryId";
    constexpr auto kConvertQueryIdKey = "ConvertQueryId";
    constexpr auto kCostsKey = "Costs";
    constexpr auto kAutoPlaceKey = "AutoPlace";
    constexpr auto kOneWayDirKey = "OneWayDir";
    constexpr auto kHandleOffsetKey = "HandleOffset";
    constexpr auto kStepOffsetsKey = "StepOffsets";
    constexpr auto kCopyFromKey = "CopyFrom";
    constexpr auto kRotateKey = "Rotate";
    constexpr auto kTransposeKey = "Transpose";
    constexpr auto kTranslateKey = "Translate";
    constexpr auto kReplacementIntersectionKey = "ReplacementIntersection";

    // From @Pixelchemist on StackOverflow: https://stackoverflow.com/a/42198760
    template <typename T>
    constexpr auto operator+(T e) noexcept
        -> std::enable_if_t<std::is_enum_v<T>, std::underlying_type_t<T>> {
        return static_cast<std::underlying_type_t<T>>(e);
    }

    enum class NetworkType {
        ROAD = 0,
        RAIL = 1,
        HIGHWAY = 2,
        STREET = 3,
        PIPE = 4,
        POWERLINE = 5,
        AVENUE = 6,
        SUBWAY = 7,
        LIGHT_RAIL = 8,
        MONORAIL = 9,
        ONE_WAY_ROAD = 10,
        DIRT_ROAD = 11,
        GROUND_HIGHWAY = 12,
        NONE = 13,
    };

    inline NetworkType ParseNetworkType(const std::string_view name) {
        auto lower = std::string(name);
        for (char& c : lower)
            c = std::tolower(c);

        if (lower == "road")
            return NetworkType::ROAD;
        if (lower == "rail")
            return NetworkType::RAIL;
        if (lower == "highway")
            return NetworkType::HIGHWAY;
        if (lower == "street")
            return NetworkType::STREET;
        if (lower == "pipe")
            return NetworkType::PIPE;
        if (lower == "powerline")
            return NetworkType::POWERLINE;
        if (lower == "avenue")
            return NetworkType::AVENUE;
        if (lower == "subway")
            return NetworkType::SUBWAY;
        if (lower == "lightrail")
            return NetworkType::LIGHT_RAIL;
        if (lower == "monorail")
            return NetworkType::MONORAIL;
        if (lower == "onewayroad")
            return NetworkType::ONE_WAY_ROAD;
        if (lower == "dirtroad")
            return NetworkType::DIRT_ROAD;
        if (lower == "groundhighway")
            return NetworkType::GROUND_HIGHWAY;

        // This should never happen
        return NetworkType::NONE;
    }

    struct NetworkCheck {
        NetworkType networkType = NetworkType::NONE;
        uint32_t ruleFlagByte = 0xFFFFFFFF;
        uint32_t hexMask = 0xFFFFFFFF;
        bool optional = false;
        bool check = false;
    };

    struct CheckType {
        bool initialized = false;
        char symbol = 0;
        std::vector<NetworkCheck> networks;
    };

    struct PreviewEffect {
        bool initialized = false;
        float x, y;
        int rotation, flip;
        uint32_t instanceId = 0xFFFFFFFF;
        std::string name;
    };

    enum class Rotation {
        ROT_0 = 0,
        ROT_90 = 1,
        ROT_180 = 2,
        ROT_270 = 3,
        NONE = 4
    };

    enum class OneWayDir {
        WEST = 0,
        NORTH_WEST = 1,
        NORTH = 2,
        NORTH_EAST = 3,
        EAST = 4,
        SOUTH_EAST = 5,
        SOUTH = 6,
        SOUTH_WEST = 7,
        NONE = 8
    };

    struct ReplacementIntersection {
        bool initialized = false;
        Rotation rotation = Rotation::NONE;
        uint32_t flip;
    };

    struct Translation {
        bool initialized = false;
        uint32_t x;
        uint32_t z;
    };

    struct HandleOffset {
        bool initialized = false;
        int32_t deltaStraight;
        int32_t deltaSide;
    };

    struct StepOffsets {
        bool initialized = false;
        uint32_t dragStartThreshold; // Likely unused?
        uint32_t dragCompletionOffset;
    };

    struct PuzzlePiece {
        uint32_t id;
        PreviewEffect effect;
        std::vector<std::string> cellLayout;
        std::vector<CheckType> checkTypes;
        std::vector<std::string> consLayout;

        uint32_t autoPathBase = 0xFFFFFFFF;
        uint32_t autoTileBase = 0xFFFFFFFF;

        ReplacementIntersection replacementIntersection;

        uint32_t placeQueryId = 0xFFFFFFFF;
        uint32_t costs = 0xFFFFFFFF;
        uint32_t convertQueryId = 0xFFFFFFFF;

        bool autoPlace = false;

        HandleOffset handleOffset = HandleOffset{};
        StepOffsets stepOffsets = StepOffsets{};

        OneWayDir oneWayDir = OneWayDir::NONE;

        uint32_t copyFrom = 0;
        Rotation rotate = Rotation::NONE;
        Translation translate = Translation{};
        bool transpose = false;

        [[nodiscard]] std::string ToString() const;
    };

    struct Ordering {
        std::vector<uint32_t> rotationRing;
        std::vector<std::vector<uint32_t>> addTypes;
    };

    struct Record {
        std::vector<Ordering> orderings;
        std::unordered_map<uint32_t, PuzzlePiece> puzzlePieces;
        PuzzlePiece* currentPiece = nullptr;
    };

    // Parsing functions
    uint32_t ParsePieceId(std::string_view section);
    std::vector<uint32_t> ParseIdList(std::string_view value);
    bool ParsePieceValue(std::string_view value, PreviewEffect& previewEffect);
    CheckType ParseCheckType(std::string_view value);
    int IniHandler(void* user, const char* section, const char* key, const char* value);

    // Transformation utility functions (based on SC4 source)
    // RotatePoint: Rotate a 2D point (x, y) based on rotation amount (0-3 = 0°, 90°, 180°, 270°)
    void RotatePoint(float& x, float& y, int rotation);

    // RotateEdgeFlags: Rotate edge constraint flags (bitwise rotation)
    uint32_t RotateEdgeFlags(uint32_t flags, int rotation);

    // RotateConstraint: Map constraint bytes to rotated equivalents
    uint8_t RotateConstraint(uint8_t constraint);

    // RotateMap: Rotate a byte map (grid) in place, optionally applying constraint rotation
    void RotateMap(std::vector<uint8_t>& mapData, int& width, int& height, int& centerX, int& centerY,
                   int rotation, bool rotateConstraints);

    // Piece transformation functions
    void CopyPuzzlePiece(const PuzzlePiece& source, PuzzlePiece& dest);
    void ApplyRotation(PuzzlePiece& piece, Rotation rotation);
    void ApplyTranspose(PuzzlePiece& piece);
    void ApplyTranslation(PuzzlePiece& piece);

    // Main transformation pipeline
    void BuildNavigationIndices(Record& data);

    [[nodiscard]] ParseExpected<Record> Parse(std::span<const uint8_t> buffer);
}
