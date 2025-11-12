#include <filesystem>
#include <format>
#include <ostream>
#include <print>
#include <ranges>
#include <span>

#include "DBPFReader.h"
#include "ExemplarReader.h"
#include "FSHReader.h"
#include "RUL0.h"
#include "ini.h"
#include "TGI.h"

#ifdef _WIN32
#    include <objbase.h>
#    include <wincodec.h>
#    include <wrl/client.h>
#    pragma comment(lib, "windowscodecs.lib")
#endif

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

    std::span<const uint8_t> payloadSpan(payload->data(), payload->size());

    std::println("[{}] size={} label={} head={:02X} {:02X} {:02X} {:02X}",
                 entry.tgi.ToString(),
                 entry.decompressedSize.value_or(entry.size),
                 DBPF::Describe(entry.tgi),
                 (*payload)[0], (*payload)[1], (*payload)[2], (*payload)[3]);

    auto parsed = Exemplar::Parse(payloadSpan);
    if (!parsed.success) {
        out.error = parsed.errorMessage.empty()
            ? "parse failed without message"
            : parsed.errorMessage;
        return out;
    }

    out.exemplar = std::move(parsed.record);
    return out;
}

namespace {

#ifdef _WIN32
    bool SavePngViaWic(const std::filesystem::path& path,
                       const uint8_t* rgba,
                       uint32_t width,
                       uint32_t height) {
        static bool comInitialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        if (!comInitialized) {
            return false;
        }

        using Microsoft::WRL::ComPtr;

        ComPtr<IWICImagingFactory> factory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            return false;
        }

        ComPtr<IWICStream> stream;
        hr = factory->CreateStream(&stream);
        if (FAILED(hr)) {
            return false;
        }

        const std::wstring widePath = path.wstring();
        hr = stream->InitializeFromFilename(widePath.c_str(), GENERIC_WRITE);
        if (FAILED(hr)) {
            return false;
        }

        ComPtr<IWICBitmapEncoder> encoder;
        hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
        if (FAILED(hr)) {
            return false;
        }

        hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
        if (FAILED(hr)) {
            return false;
        }

        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> props;
        hr = encoder->CreateNewFrame(&frame, &props);
        if (FAILED(hr)) {
            return false;
        }

        hr = frame->Initialize(props.Get());
        if (FAILED(hr)) {
            return false;
        }

        hr = frame->SetSize(width, height);
        if (FAILED(hr)) {
            return false;
        }

        GUID format = GUID_WICPixelFormat32bppRGBA;
        hr = frame->SetPixelFormat(&format);
        if (FAILED(hr)) {
            return false;
        }

        hr = frame->WritePixels(height, width * 4, width * 4 * height,
                                const_cast<BYTE*>(reinterpret_cast<const BYTE*>(rgba)));
        if (FAILED(hr)) {
            return false;
        }

        hr = frame->Commit();
        if (FAILED(hr)) {
            return false;
        }

        hr = encoder->Commit();
        return SUCCEEDED(hr);
    }
#else
    bool SavePngViaWic(const std::filesystem::path&, const uint8_t*, uint32_t, uint32_t) {
        std::println("PNG export is only available when building on Windows.");
        return false;
    }
#endif

} // namespace

auto main() -> int {
    IntersectionOrdering::Data data;
    if (ini_parse("../examples/rul0/4023_FARR-2-3_Crossings.txt", IntersectionOrdering::IniHandler, &data) < 0) {
        std::println("An error occurred during parsing");
    }

    IntersectionOrdering::BuildNavigationIndices(data);

    std::println("Loaded {} puzzle pieces", data.puzzlePieces.size());
    std::println("");

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
    if (!reader.LoadFile("../examples/dat/051-non-pac_000.dat")) {
        std::println("Failed to load DAT");
        return 1;
    }

    std::filesystem::path outputDir = "fsh_output";
    std::filesystem::create_directories(outputDir);

    auto savedImages = 0;

    auto exemplarEntries = reader.FindEntries("Exemplar");
    for (const auto& entry : exemplarEntries) {
        std::println("ExemplarEntry {}: {}", entry->tgi.ToString(), entry->GetSize());
    }

    for (const auto& entry : reader.GetIndex()) {
        if (entry.tgi.type != 0x7AB50E44) {
            continue;
        }

        auto payload = reader.ReadEntryData(entry);
        if (!payload) {
            continue;
        }

        std::span<const uint8_t> payloadSpan(payload->data(), payload->size());

        FSH::File file;
        if (!FSH::Reader::Parse(payloadSpan, file)) {
            std::println("Failed to parse FSH {}", entry.tgi.ToString());
            continue;
        }

        for (const auto& fshEntry : file.entries) {
            for (const auto& bitmap : fshEntry.bitmaps) {
                std::vector<uint8_t> rgba;
                if (!FSH::Reader::ConvertToRGBA8(bitmap, rgba)) {
                    continue;
                }

                const auto filename = std::format(
                    "{:08X}_{:02}_{}x{}_mip{}.png",
                    entry.tgi.instance,
                    bitmap.code,
                    bitmap.width,
                    bitmap.height,
                    bitmap.mipLevel);

                const auto path = outputDir / filename;
                std::filesystem::create_directories(path.parent_path());

                if (SavePngViaWic(path, rgba.data(), bitmap.width, bitmap.height)) {
                    ++savedImages;
                }
            }
        }

        if (savedImages >= 200) {
            break;
        }
    }

    std::println("Saved {} FSH textures to {}", savedImages, outputDir.string());
    return 0;
}
