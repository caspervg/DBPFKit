#include <filesystem>
#include <format>
#include <ostream>
#include <print>
#include <ranges>
#include <span>

#include "DBPFReader.h"
#include "ExemplarReader.h"
#include "FSHReader.h"
#include "LTextReader.h"
#include "RUL0.h"
#include "S3DReader.h"
#include "TGI.h"
#include "ini.h"

#ifdef _WIN32
#    include <objbase.h>
#    include <wincodec.h>
#    include <wrl/client.h>
#    pragma comment(lib, "windowscodecs.lib")
#endif

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
    RUL0::Record data;
    auto res = ini_parse("../examples/rul0/rul0_full.txt", RUL0::IniHandler, &data);
    if (res < 0) {
        std::println("An error occurred during parsing");
    }

    RUL0::BuildNavigationIndices(data);

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
    return 0;

    DBPF::Reader reader;
    if (!reader.LoadFile("../examples/dat/800-nam_001.dat")) {
        std::println("Failed to load DAT");
        return 1;
    }

    std::filesystem::path outputDir = "fsh_output";
    std::filesystem::create_directories(outputDir);

    auto savedImages = 0;

    for (const auto& entry : reader.GetIndex()) {
        if (entry.tgi.type != 0x7AB50E44) {
            continue;
        }

        auto payload = reader.ReadEntryData(entry);
        if (!payload) {
            continue;
        }

        std::span<const uint8_t> payloadSpan(payload->data(), payload->size());

        auto parsed = FSH::Reader::Parse(payloadSpan);
        if (!parsed.has_value()) {
            std::println("Failed to parse FSH {}: {}", entry.tgi.ToString(), parsed.error().message);
            continue;
        }
    }

    std::println("Saved {} FSH textures to {}", savedImages, outputDir.string());


    auto exemplarEntries = reader.FindEntries("Exemplar");
    for (const auto& entry : exemplarEntries) {
        std::println("ExemplarEntry {}: {}", entry->tgi.ToString(), entry->GetSize());
        const auto res = reader.LoadExemplar(*entry);
        if (!res.has_value()) {
            std::println("Failed to load exemplar {}: {}", entry->tgi.ToString(), res.error().message);
        }
    }

    auto ltextEntries = reader.FindEntries("LText");
    for (const auto& entry : ltextEntries) {
        const auto res = reader.LoadLText(*entry);
        if (!res.has_value()) {
            std::println("Failed to load LText {}: {}", entry->tgi.ToString(), res.error().message);
        }
    }

    auto s3dEntries = reader.FindEntries("S3D");
    for (const auto& entry : s3dEntries) {
        const auto res = reader.LoadS3D(*entry);
        if (!res.has_value()) {
            std::println("Failed to load S3D {}: {}", entry->tgi.ToString(), res.error().message);
        } else {
            //std::println("Loaded S3D {} with {} vertices", entry->tgi.ToString(), res->vertexBuffers.size());
        }
    }

    return 0;
}
