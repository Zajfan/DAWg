#include "media/JuceWavAssetReader.h"
#include "media/JuceWavAssetWriter.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>

namespace {
namespace fs = std::filesystem;
using dawg::media::MediaError;
using dawg::platform::juce::JuceWavAssetReaderFactory;

void require(const bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

struct TestDirectory {
    fs::path root;

    TestDirectory()
    {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            root = fs::temp_directory_path()
                / ("dawg-juce-wav-reader-tests-" + std::to_string(suffix) + "-"
                   + std::to_string(attempt));
            if (fs::create_directory(root))
                return;
        }
        require(false, "create isolated test directory");
    }

    ~TestDirectory()
    {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};

void writeU16LE(std::ofstream& out, const std::uint16_t value)
{
    const std::array<char, 2> bytes {
        static_cast<char>(value & 0xFFu),
        static_cast<char>((value >> 8u) & 0xFFu)
    };
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writeU32LE(std::ofstream& out, const std::uint32_t value)
{
    const std::array<char, 4> bytes {
        static_cast<char>(value & 0xFFu),
        static_cast<char>((value >> 8u) & 0xFFu),
        static_cast<char>((value >> 16u) & 0xFFu),
        static_cast<char>((value >> 24u) & 0xFFu)
    };
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writeFloatWav(const fs::path& path,
                   const std::uint32_t sampleRate,
                   const std::uint16_t channelCount,
                   const std::span<const float> samples)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    const auto dataByteCount = static_cast<std::uint32_t>(samples.size() * sizeof(float));
    const auto riffChunkSize = static_cast<std::uint32_t>(36u + dataByteCount);
    const auto blockAlign = static_cast<std::uint16_t>(channelCount * sizeof(float));
    const auto byteRate = static_cast<std::uint32_t>(sampleRate * blockAlign);

    out.write("RIFF", 4);
    writeU32LE(out, riffChunkSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeU32LE(out, 16u);
    writeU16LE(out, 3u); // IEEE float
    writeU16LE(out, channelCount);
    writeU32LE(out, sampleRate);
    writeU32LE(out, byteRate);
    writeU16LE(out, blockAlign);
    writeU16LE(out, 32u);
    out.write("data", 4);
    writeU32LE(out, dataByteCount);

    for (const float sample : samples) {
        std::uint32_t bits {};
        std::memcpy(&bits, &sample, sizeof(bits));
        writeU32LE(out, bits);
    }

    out.close();
    require(!out.fail(), "write float WAV fixture");
}

void writeTextDocument(const fs::path& path, const std::string_view text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
    require(!out.fail(), "write non-WAV fixture");
}
} // namespace

int main()
{
    TestDirectory dir;
    JuceWavAssetReaderFactory factory;

    constexpr std::array<float, 8> expected {
        0.125f, 0.25f, 0.375f, 0.5f, -0.5f, -0.375f, -0.25f, -0.125f
    };
    const auto wavPath = dir.root / "fixture.wav";
    writeFloatWav(wavPath, 48000, 1, expected);

    auto opened = factory.open(wavPath.string());
    require(opened.has_value() && *opened != nullptr, "factory opens valid WAV file");

    const auto metadata = (*opened)->metadata();
    require(metadata.sampleRate == 48000, "WAV metadata reports sampleRate");
    require(metadata.channelCount == 1, "WAV metadata reports channelCount");
    require(metadata.frameCount == expected.size(), "WAV metadata reports frameCount");

    std::array<float, 8> decoded {};
    std::array<float*, 1> planes {decoded.data()};
    require((*opened)->read(0, planes, decoded.size()), "reader decodes all eight frames");
    for (std::size_t i = 0; i < expected.size(); ++i)
        require(decoded[i] == expected[i], "decoded WAV frame matches expected float sample");

    std::array<float, 3> offsetDecoded {};
    std::array<float*, 1> offsetPlanes {offsetDecoded.data()};
    require((*opened)->read(2, offsetPlanes, offsetDecoded.size()),
            "reader decodes from nonzero source frame");
    require(offsetDecoded[0] == expected[2] && offsetDecoded[1] == expected[3]
                && offsetDecoded[2] == expected[4],
            "offset WAV read matches expected slice");

    require(!(*opened)->read(7, offsetPlanes, 2), "reader rejects read past frameCount");
    std::array<float*, 1> nullPlanes {nullptr};
    require(!(*opened)->read(0, nullPlanes, 1), "reader rejects null destination channel");

    const auto emptyPathResult = factory.open("");
    require(!emptyPathResult.has_value()
                && emptyPathResult.error().code == MediaError::Code::invalidPath,
            "empty path returns invalidPath");

    const auto missingFileResult = factory.open((dir.root / "missing.wav").string());
    require(!missingFileResult.has_value()
                && missingFileResult.error().code == MediaError::Code::cannotOpen,
            "missing file returns cannotOpen");

    const auto nonWavPath = dir.root / "not-a-wav.txt";
    writeTextDocument(nonWavPath, "not a riff wave header");
    const auto nonWavResult = factory.open(nonWavPath.string());
    require(!nonWavResult.has_value()
                && nonWavResult.error().code == MediaError::Code::unsupportedFormat,
            "non-WAV file returns unsupportedFormat");

    const auto emptyWavPath = dir.root / "empty.wav";
    writeFloatWav(emptyWavPath, 48000, 1, {});
    const auto emptyWavResult = factory.open(emptyWavPath.string());
    require(!emptyWavResult.has_value()
                && emptyWavResult.error().code == MediaError::Code::invalidMetadata,
            "zero-length WAV returns invalidMetadata");

    // Verify JuceWavAssetWriterFactory writes a stereo 32-bit float WAV that JuceWavAssetReaderFactory reads back bit-for-bit
    dawg::platform::juce::JuceWavAssetWriterFactory writerFactory;
    const auto recordedWavPath = dir.root / "recorded-take.wav";
    auto writer = writerFactory.create(recordedWavPath.string(), 48000, 2);
    require(writer.has_value() && *writer != nullptr, "JuceWavAssetWriterFactory creates WAV writer");

    constexpr std::array<float, 6> recLeft {0.1f, -0.2f, 0.3f, -0.4f, 0.5f, -0.6f};
    constexpr std::array<float, 6> recRight {-0.1f, 0.2f, -0.3f, 0.4f, -0.5f, 0.6f};
    const std::array<const float*, 2> recPlanes {recLeft.data(), recRight.data()};
    require((*writer)->write(recPlanes, recLeft.size()), "writer writes 6 stereo frames");
    require((*writer)->finalize(), "writer finalizes WAV file");
    writer->reset();

    auto reopened = factory.open(recordedWavPath.string());
    require(reopened.has_value() && *reopened != nullptr, "reader opens WAV written by JuceWavAssetWriter");
    require((*reopened)->metadata().sampleRate == 48000
                && (*reopened)->metadata().channelCount == 2
                && (*reopened)->metadata().frameCount == 6,
            "reopened WAV metadata matches writer parameters");

    std::array<float, 6> readBackL {};
    std::array<float, 6> readBackR {};
    std::array<float*, 2> readBackPlanes {readBackL.data(), readBackR.data()};
    require((*reopened)->read(0, readBackPlanes, 6), "reader decodes all 6 written stereo frames");
    require(readBackL == recLeft && readBackR == recRight,
            "32-bit float WAV round-trip through JuceWavAssetWriter + JuceWavAssetReader is bit-exact");

    return EXIT_SUCCESS;
}
