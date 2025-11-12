#pragma once

#include "TGI.h"

#include <array>
#include <unordered_map>
#include <vector>

namespace {
    using namespace DBPF;
    constexpr std::array<TgiLabel, 43> kTgiCatalog{{
        {{0, 0, 0}, "-"},
        {{0xe86b1eef, 0xe86b1eef, 0x286b1f03}, "Directory"},
        {{0x6be74c60, 0x6be74c60, std::nullopt}, "LD"},
        {{0x5ad0e817, 0xbadb57f1, std::nullopt}, "S3D (Maxis)"},
        {{0x5ad0e817, std::nullopt, std::nullopt}, "S3D"},
        {{0x05342861, std::nullopt, std::nullopt}, "Cohort"},
        {{0x6534284a, 0x2821ed93, std::nullopt}, "Exemplar (Road)"},
        {{0x6534284a, 0xa92a02ea, std::nullopt}, "Exemplar (Street)"},
        {{0x6534284a, 0xcbe084cb, std::nullopt}, "Exemplar (One-Way Road)"},
        {{0x6534284a, 0xcb730fac, std::nullopt}, "Exemplar (Avenue)"},
        {{0x6534284a, 0xa8434037, std::nullopt}, "Exemplar (Highway)"},
        {{0x6534284a, 0xebe084d1, std::nullopt}, "Exemplar (Ground Highway)"},
        {{0x6534284a, 0x6be08658, std::nullopt}, "Exemplar (Dirt Road)"},
        {{0x6534284a, 0xe8347989, std::nullopt}, "Exemplar (Rail)"},
        {{0x6534284a, 0x2b79dffb, std::nullopt}, "Exemplar (Light Rail)"},
        {{0x6534284a, 0xebe084c2, std::nullopt}, "Exemplar (Monorail)"},
        {{0x6534284a, 0x8a15f3f2, std::nullopt}, "Exemplar (Subway)"},
        {{0x6534284a, 0x088e1962, std::nullopt}, "Exemplar (Power Pole)"},
        {{0x6534284a, 0x89ac5643, std::nullopt}, "Exemplar (T21)"},
        {{0x6534284a, std::nullopt, std::nullopt}, "Exemplar"},
        {{0x7ab50e44, 0x1abe787d, std::nullopt}, "FSH (Misc)"},
        {{0x7ab50e44, 0x0986135e, std::nullopt}, "FSH (Base/Overlay Texture)"},
        {{0x7ab50e44, 0x2BC2759a, std::nullopt}, "FSH (Shadow Mask)"},
        {{0x7ab50e44, 0x2a2458f9, std::nullopt}, "FSH (Animation Sprites (Props))"},
        {{0x7ab50e44, 0x49a593e7, std::nullopt}, "FSH (Animation Sprites (Non Props))"},
        {{0x7ab50e44, 0x891b0e1a, std::nullopt}, "FSH (Terrain/Foundation)"},
        {{0x7ab50e44, 0x46a006b0, std::nullopt}, "FSH (UI Image)"},
        {{0x7ab50e44, std::nullopt, std::nullopt}, "FSH"},
        {{0x296678f7, 0x69668828, std::nullopt}, "SC4Path (2D)"},
        {{0x296678f7, 0xa966883f, std::nullopt}, "SC4Path (3D)"},
        {{0x296678f7, std::nullopt, std::nullopt}, "SC4Path"},
        {{0x856ddbac, 0x6a386d26, std::nullopt}, "PNG (Icon)"},
        {{0x856ddbac, std::nullopt, std::nullopt}, "PNG"},
        {{0xca63e2a3, 0x4a5e8ef6, std::nullopt}, "LUA"},
        {{0xca63e2a3, 0x4a5e8f3f, std::nullopt}, "LUA (Generators)"},
        {{0x2026960b, 0xaa4d1933, std::nullopt}, "WAV"},
        {{0x2026960b, std::nullopt, std::nullopt}, "LText"},
        {{0, 0x4a87bfe8, 0x2a87bffc}, "INI (Font Table)"},
        {{0, 0x8a5971c5, 0x8a5993b9}, "INI (Networks)"},
        {{0, 0x8a5971c5, std::nullopt}, "INI"},
        {{0x0a5bcf4b, 0xaa5bcf57, 0x10000000}, "RUL0 (Intersection Ordering)"},
        {{0xea5118b0, std::nullopt, std::nullopt}, "EffDir"},
        {{std::nullopt, std::nullopt, std::nullopt}, "Unknown"},
    }};

    struct TgiLabelIndex {
        TgiLabelIndex() {
            for (const auto& entry : kTgiCatalog) {
                labelMap.emplace(entry.label, &entry.mask);
                if (entry.mask.type.has_value()) {
                    typeBuckets[*entry.mask.type].push_back(&entry);
                }
                else {
                    wildcard.push_back(&entry);
                }
            }
        }

        std::unordered_map<std::string_view, const TgiMask*> labelMap;
        std::unordered_map<uint32_t, std::vector<const TgiLabel*>> typeBuckets;
        std::vector<const TgiLabel*> wildcard;
    } gTgiLabelIndex;

    std::vector<const TgiLabel*> CandidatesFor(const uint32_t type) {
        std::vector<const TgiLabel*> out;
        const auto it = gTgiLabelIndex.typeBuckets.find(type);
        if (it != gTgiLabelIndex.typeBuckets.end()) {
            out.insert(out.end(), it->second.begin(), it->second.end());
        }
        out.insert(out.end(), gTgiLabelIndex.wildcard.begin(), gTgiLabelIndex.wildcard.end());
        return out;
    }
}

namespace DBPF {

    std::string_view Describe(const Tgi& tgi) {
        auto candidates = CandidatesFor(tgi.type);
        for (const auto* candidate : candidates) {
            if (candidate->mask.Matches(tgi)) {
                return candidate->label;
            }
        }
        return "Unknown";
    }

    std::optional<TgiMask> MaskForLabel(std::string_view label) {
        if (const auto it = gTgiLabelIndex.labelMap.find(label); it != gTgiLabelIndex.labelMap.end()) {
            return *it->second;
        }
        return std::nullopt;
    }

}
