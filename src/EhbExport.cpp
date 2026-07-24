#include "EhbExport.h"
#include "decrunchmania.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <climits>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#endif

namespace fs = std::filesystem;

namespace
{
    constexpr std::uint32_t kCamgEhb = 0x00000080U;

    struct IlbmImage
    {
        int width = 0;
        int height = 0;
        int depth = 0;
        int masking = 0;
        int compression = 0;
        int transparentColor = 0;
        bool hasCamg = false;
        std::uint32_t camg = 0;
        std::vector<std::uint8_t> pixels;
        std::vector<std::uint8_t> opaque;
        std::vector<std::uint8_t> cmap;
    };

    struct RuntimePictureInfo
    {
        int width = 0;
        int height = 0;
        int depth = 0;
        std::vector<std::uint8_t> pixels;
    };

    struct RgbColor
    {
        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;
    };

    struct EhbImageData
    {
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> pixels;
        std::array<RgbColor, 32> baseColors{};
        double rmse = 0.0;
        bool directEhb = false;
    };

    bool ValidatePalette128(const std::vector<std::uint8_t>& data, std::wstring& error);

    std::uint16_t ReadBe16(const std::vector<std::uint8_t>& data, size_t offset)
    {
        if (offset + 1 >= data.size())
            return 0;
        return static_cast<std::uint16_t>((static_cast<unsigned int>(data[offset]) << 8) |
            static_cast<unsigned int>(data[offset + 1]));
    }

    std::uint32_t ReadBe32(const std::vector<std::uint8_t>& data, size_t offset)
    {
        if (offset + 3 >= data.size())
            return 0;
        return (static_cast<std::uint32_t>(data[offset + 0]) << 24) |
            (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
            (static_cast<std::uint32_t>(data[offset + 2]) << 8) |
            static_cast<std::uint32_t>(data[offset + 3]);
    }

    void WriteBe16(std::vector<std::uint8_t>& out, std::uint16_t value)
    {
        out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
        out.push_back(static_cast<std::uint8_t>(value & 0xff));
    }

    bool Exists(const fs::path& path)
    {
        std::error_code ec;
        return fs::exists(path, ec) && !ec;
    }

    bool ReadFile(const fs::path& file, std::vector<std::uint8_t>& data)
    {
        std::ifstream in(file, std::ios::binary);
        if (!in)
            return false;

        in.seekg(0, std::ios::end);
        const std::streamoff length = in.tellg();
        if (length < 0)
            return false;
        in.seekg(0, std::ios::beg);

        data.resize(static_cast<size_t>(length));
        if (!data.empty())
            in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        return static_cast<bool>(in) || data.empty();
    }

    bool WriteFile(const fs::path& file, const std::vector<std::uint8_t>& data)
    {
        std::error_code ec;
        fs::create_directories(file.parent_path(), ec);
        if (ec)
            return false;

        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        if (!data.empty())
            out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        return static_cast<bool>(out);
    }

    bool CopyFileSafe(const fs::path& source, const fs::path& target)
    {
        if (!Exists(source))
            return false;

        std::error_code ec;
        if (Exists(target) && fs::equivalent(source, target, ec) && !ec)
            return true;

        ec.clear();
        fs::create_directories(target.parent_path(), ec);
        if (ec)
            return false;

        ec.clear();
        fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
        return !ec;
    }

    bool DecodeByteRunRow(const std::vector<std::uint8_t>& data, size_t& position, size_t end,
        std::vector<std::uint8_t>& row, std::wstring& error)
    {
        // Gloom's trimmed-picture stream is row based, but some retail files were
        // packed with runs/literals that include bytes beyond the visible end of a
        // plane row. The original loader consumes the complete packet and clips only
        // the row output. Control byte 128 is used as an early end-of-row marker.
        size_t output = 0;
        while (output < row.size())
        {
            if (position >= end)
            {
                error = L"ByteRun1 data ended before the plane row was complete.";
                return false;
            }

            const std::uint8_t control = data[position++];
            if (control == 128)
                break;

            if (control > 128)
            {
                const size_t count = static_cast<size_t>(257 - static_cast<unsigned int>(control));
                if (position >= end)
                {
                    error = L"ByteRun1 repeat packet has no value byte.";
                    return false;
                }

                const std::uint8_t value = data[position++];
                const size_t copyCount = std::min(count, row.size() - output);
                std::fill(row.begin() + static_cast<std::ptrdiff_t>(output),
                    row.begin() + static_cast<std::ptrdiff_t>(output + copyCount), value);
                output += copyCount;
            }
            else
            {
                const size_t count = static_cast<size_t>(control) + 1U;
                if (position + count > end)
                {
                    error = L"ByteRun1 literal packet is incomplete.";
                    return false;
                }

                const size_t copyCount = std::min(count, row.size() - output);
                std::copy(data.begin() + static_cast<std::ptrdiff_t>(position),
                    data.begin() + static_cast<std::ptrdiff_t>(position + copyCount),
                    row.begin() + static_cast<std::ptrdiff_t>(output));
                position += count;
                output += copyCount;
            }
        }
        return true;
    }

    bool DecodeIlbm(const fs::path& file, IlbmImage& image, std::wstring& error)
    {
        std::vector<std::uint8_t> data;
        if (!ReadFile(file, data))
        {
            error = L"Cannot read " + file.wstring();
            return false;
        }
        if (data.size() < 12 ||
            std::string(reinterpret_cast<const char*>(data.data()), 4) != "FORM" ||
            std::string(reinterpret_cast<const char*>(data.data() + 8), 4) != "ILBM")
        {
            error = L"Not a FORM/ILBM file: " + file.wstring();
            return false;
        }

        size_t bmhdOffset = 0;
        size_t bmhdSize = 0;
        size_t bodyOffset = 0;
        size_t bodySize = 0;

        for (size_t position = 12; position + 8 <= data.size();)
        {
            const std::uint32_t chunkSize = ReadBe32(data, position + 4);
            const size_t content = position + 8;
            if (content + static_cast<size_t>(chunkSize) > data.size())
            {
                error = L"IFF chunk exceeds the file size: " + file.wstring();
                return false;
            }

            const std::string id(reinterpret_cast<const char*>(data.data() + position), 4);
            if (id == "BMHD")
            {
                bmhdOffset = content;
                bmhdSize = chunkSize;
            }
            else if (id == "BODY")
            {
                bodyOffset = content;
                bodySize = chunkSize;
            }
            else if (id == "CMAP")
            {
                image.cmap.assign(data.begin() + static_cast<std::ptrdiff_t>(content),
                    data.begin() + static_cast<std::ptrdiff_t>(content + chunkSize));
            }
            else if (id == "CAMG" && chunkSize >= 4)
            {
                image.hasCamg = true;
                image.camg = ReadBe32(data, content);
            }

            position = content + static_cast<size_t>(chunkSize) + (chunkSize & 1U);
        }

        if (bmhdOffset == 0 || bmhdSize < 20 || bodyOffset == 0 || bodySize == 0)
        {
            error = L"ILBM is missing BMHD or BODY: " + file.wstring();
            return false;
        }

        image.width = static_cast<int>(ReadBe16(data, bmhdOffset));
        image.height = static_cast<int>(ReadBe16(data, bmhdOffset + 2));
        image.depth = static_cast<int>(data[bmhdOffset + 8]);
        image.masking = static_cast<int>(data[bmhdOffset + 9]);
        image.compression = static_cast<int>(data[bmhdOffset + 10]);
        image.transparentColor = static_cast<int>(ReadBe16(data, bmhdOffset + 12));

        if (image.width <= 0 || image.width > 2048 || image.height <= 0 || image.height > 2048)
        {
            error = L"ILBM dimensions are invalid: " + file.wstring();
            return false;
        }
        if (image.depth <= 0 || image.depth > 8)
        {
            error = L"ILBM depth must be between one and eight planes: " + file.wstring();
            return false;
        }
        if (image.masking != 0 && image.masking != 1 && image.masking != 2)
        {
            error = L"Unsupported ILBM masking mode: " + file.wstring();
            return false;
        }
        if (image.compression != 0 && image.compression != 1)
        {
            error = L"Unsupported ILBM compression: " + file.wstring();
            return false;
        }
        const size_t requiredColors = (image.depth == 6 && image.cmap.size() >= 32U * 3U)
            ? 32U
            : static_cast<size_t>(1U << image.depth);
        if (image.cmap.size() < requiredColors * 3U)
        {
            error = L"ILBM CMAP has fewer colors than required by its bitplane depth: " + file.wstring();
            return false;
        }

        const int rowBytes = ((image.width + 15) / 16) * 2;
        const int storedPlanes = image.depth + (image.masking == 1 ? 1 : 0);
        const size_t pixelCount = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
        image.pixels.assign(pixelCount, 0);
        image.opaque.assign(pixelCount, image.masking == 1 ? 0 : 1);
        std::vector<std::uint8_t> row(static_cast<size_t>(rowBytes), 0);

        size_t bodyPosition = bodyOffset;
        const size_t bodyEnd = bodyOffset + bodySize;
        for (int y = 0; y < image.height; ++y)
        {
            for (int plane = 0; plane < storedPlanes; ++plane)
            {
                std::fill(row.begin(), row.end(), 0);
                if (image.compression == 1)
                {
                    std::wstring rowError;
                    if (!DecodeByteRunRow(data, bodyPosition, bodyEnd, row, rowError))
                    {
                        error = rowError + L" File: " + file.wstring();
                        return false;
                    }
                }
                else
                {
                    if (bodyPosition + row.size() > bodyEnd)
                    {
                        error = L"Raw ILBM BODY is incomplete: " + file.wstring();
                        return false;
                    }
                    std::copy(data.begin() + static_cast<std::ptrdiff_t>(bodyPosition),
                        data.begin() + static_cast<std::ptrdiff_t>(bodyPosition + row.size()), row.begin());
                    bodyPosition += row.size();
                }

                for (int x = 0; x < image.width; ++x)
                {
                    const std::uint8_t bit = static_cast<std::uint8_t>(
                        (row[static_cast<size_t>(x / 8)] >> (7 - (x & 7))) & 1U);
                    const size_t pixel = static_cast<size_t>(y) * static_cast<size_t>(image.width) +
                        static_cast<size_t>(x);
                    if (plane < image.depth)
                    {
                        if (bit)
                            image.pixels[pixel] |= static_cast<std::uint8_t>(1U << plane);
                    }
                    else if (image.masking == 1)
                    {
                        image.opaque[pixel] = bit;
                    }
                }
            }
        }

        if (image.masking == 2)
        {
            for (size_t pixel = 0; pixel < image.pixels.size(); ++pixel)
                image.opaque[pixel] = image.pixels[pixel] != static_cast<std::uint8_t>(image.transparentColor);
        }

        return true;
    }

    void EmitLiteral(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& row,
        size_t start, size_t count)
    {
        while (count > 0)
        {
            const size_t chunk = std::min<size_t>(count, 128);
            out.push_back(static_cast<std::uint8_t>(chunk - 1));
            out.insert(out.end(), row.begin() + static_cast<std::ptrdiff_t>(start),
                row.begin() + static_cast<std::ptrdiff_t>(start + chunk));
            start += chunk;
            count -= chunk;
        }
    }

    void EmitRepeat(std::vector<std::uint8_t>& out, std::uint8_t value, size_t count)
    {
        while (count > 0)
        {
            const size_t chunk = std::min<size_t>(count, 128);
            out.push_back(static_cast<std::uint8_t>(257 - static_cast<unsigned int>(chunk)));
            out.push_back(value);
            count -= chunk;
        }
    }

    void EncodeByteRunRow(const std::vector<std::uint8_t>& row, std::vector<std::uint8_t>& out)
    {
        size_t position = 0;
        while (position < row.size())
        {
            size_t repeat = 1;
            while (position + repeat < row.size() && repeat < 128 &&
                row[position + repeat] == row[position])
            {
                ++repeat;
            }

            if (repeat >= 3)
            {
                EmitRepeat(out, row[position], repeat);
                position += repeat;
                continue;
            }

            const size_t literalStart = position;
            position += repeat;
            while (position < row.size() && position - literalStart < 128)
            {
                repeat = 1;
                while (position + repeat < row.size() && repeat < 128 &&
                    row[position + repeat] == row[position])
                {
                    ++repeat;
                }
                if (repeat >= 3)
                    break;
                position += repeat;
            }
            EmitLiteral(out, row, literalStart, position - literalStart);
        }
    }

    bool EncodeTrimmedPicture(const IlbmImage& image, std::vector<std::uint8_t>& out, std::wstring& error)
    {
        if (image.width <= 0 || image.height <= 0 || image.depth <= 0 || image.depth > 8)
        {
            error = L"Invalid image dimensions/depth for the Gloom runtime format.";
            return false;
        }
        if ((image.width & 7) != 0)
        {
            error = L"Gloom trimmed-IFF width must be divisible by eight.";
            return false;
        }

        out.clear();
        WriteBe16(out, static_cast<std::uint16_t>(image.width));
        WriteBe16(out, static_cast<std::uint16_t>(image.height));
        WriteBe16(out, static_cast<std::uint16_t>(image.depth));
        out.insert(out.end(), 6, 0);

        const int rowBytes = image.width >> 3;
        std::vector<std::uint8_t> row(static_cast<size_t>(rowBytes), 0);
        for (int y = 0; y < image.height; ++y)
        {
            for (int plane = 0; plane < image.depth; ++plane)
            {
                std::fill(row.begin(), row.end(), 0);
                for (int x = 0; x < image.width; ++x)
                {
                    const std::uint8_t index = image.pixels[
                        static_cast<size_t>(y) * static_cast<size_t>(image.width) + static_cast<size_t>(x)];
                    if ((index >> plane) & 1U)
                    {
                        row[static_cast<size_t>(x / 8)] |=
                            static_cast<std::uint8_t>(1U << (7 - (x & 7)));
                    }
                }
                EncodeByteRunRow(row, out);
            }
        }
        return true;
    }

    bool DecodeTrimmedPicture(const std::vector<std::uint8_t>& data, RuntimePictureInfo& picture,
        std::wstring& error)
    {
        if (data.size() < 12)
        {
            error = L"Runtime picture is smaller than its 12-byte header.";
            return false;
        }

        picture.width = static_cast<int>(ReadBe16(data, 0));
        picture.height = static_cast<int>(ReadBe16(data, 2));
        picture.depth = static_cast<int>(ReadBe16(data, 4));
        if (picture.width <= 0 || picture.width > 2048 || picture.height <= 0 || picture.height > 2048 ||
            picture.depth <= 0 || picture.depth > 8 || (picture.width & 7) != 0)
        {
            error = L"Runtime picture header is invalid.";
            return false;
        }

        const size_t rowBytes = static_cast<size_t>(picture.width >> 3);
        const size_t planeRows = static_cast<size_t>(picture.height) * static_cast<size_t>(picture.depth);
        if (rowBytes > SIZE_MAX / planeRows)
        {
            error = L"Runtime picture dimensions exceed the decoder limits.";
            return false;
        }

        // Retail Gloom pictures are encoded one plane row at a time. Gloom 3 and
        // Zombie Massacre contain packets whose encoded count is larger than the
        // visible bytes left in that row. Decode each row independently, consume the
        // full packet and clip only the produced row bytes, matching the game loader.
        picture.pixels.assign(static_cast<size_t>(picture.width) * static_cast<size_t>(picture.height), 0);
        std::vector<std::uint8_t> row(rowBytes, 0);
        size_t position = 12;
        for (int y = 0; y < picture.height; ++y)
        {
            for (int plane = 0; plane < picture.depth; ++plane)
            {
                std::fill(row.begin(), row.end(), 0);
                std::wstring rowError;
                if (!DecodeByteRunRow(data, position, data.size(), row, rowError))
                {
                    error = rowError;
                    return false;
                }

                for (int x = 0; x < picture.width; ++x)
                {
                    if ((row[static_cast<size_t>(x / 8)] >> (7 - (x & 7))) & 1U)
                    {
                        picture.pixels[static_cast<size_t>(y) * static_cast<size_t>(picture.width) +
                            static_cast<size_t>(x)] |= static_cast<std::uint8_t>(1U << plane);
                    }
                }
            }
        }
        return true;
    }

    std::uint16_t EncodeRgb12Word(const RgbColor& color)
    {
        const std::uint16_t red = static_cast<std::uint16_t>(std::min(15, (static_cast<int>(color.r) + 8) / 17));
        const std::uint16_t green = static_cast<std::uint16_t>(std::min(15, (static_cast<int>(color.g) + 8) / 17));
        const std::uint16_t blue = static_cast<std::uint16_t>(std::min(15, (static_cast<int>(color.b) + 8) / 17));
        return static_cast<std::uint16_t>((red << 8) | (green << 4) | blue);
    }

    RgbColor DecodeRgb12Word(std::uint16_t color)
    {
        RgbColor out;
        out.r = static_cast<std::uint8_t>(((color >> 8) & 0x0fU) * 17U);
        out.g = static_cast<std::uint8_t>(((color >> 4) & 0x0fU) * 17U);
        out.b = static_cast<std::uint8_t>((color & 0x0fU) * 17U);
        return out;
    }

    std::uint16_t HalfBriteWord(std::uint16_t base)
    {
        return static_cast<std::uint16_t>(
            ((((base >> 8) & 0x0fU) >> 1) << 8) |
            ((((base >> 4) & 0x0fU) >> 1) << 4) |
            ((base & 0x0fU) >> 1));
    }

    int ColorDistance(const RgbColor& a, const RgbColor& b)
    {
        const int dr = static_cast<int>(a.r) - static_cast<int>(b.r);
        const int dg = static_cast<int>(a.g) - static_cast<int>(b.g);
        const int db = static_cast<int>(a.b) - static_cast<int>(b.b);
        return dr * dr + dg * dg + db * db;
    }

    std::array<RgbColor, 64> BuildVisibleEhbColors(const std::array<RgbColor, 32>& baseColors)
    {
        std::array<RgbColor, 64> visible{};
        for (size_t index = 0; index < 32; ++index)
        {
            const std::uint16_t base = EncodeRgb12Word(baseColors[index]);
            visible[index] = DecodeRgb12Word(base);
            visible[index + 32] = DecodeRgb12Word(HalfBriteWord(base));
        }
        return visible;
    }

    std::vector<std::uint8_t> BuildEhbPalette(const std::array<RgbColor, 32>& baseColors)
    {
        std::vector<std::uint8_t> out;
        out.reserve(128);
        for (const RgbColor& color : baseColors)
            WriteBe16(out, EncodeRgb12Word(color));
        for (const RgbColor& color : baseColors)
            WriteBe16(out, HalfBriteWord(EncodeRgb12Word(color)));
        return out;
    }

    std::vector<std::uint8_t> BuildEhbPalette(const std::vector<std::uint8_t>& cmap)
    {
        std::array<RgbColor, 32> baseColors{};
        for (size_t index = 0; index < 32; ++index)
        {
            baseColors[index].r = cmap[index * 3 + 0];
            baseColors[index].g = cmap[index * 3 + 1];
            baseColors[index].b = cmap[index * 3 + 2];
        }
        return BuildEhbPalette(baseColors);
    }

    bool IlbmToRgbPixels(const IlbmImage& image, std::vector<RgbColor>& rgb, std::wstring& error)
    {
        const size_t colorCount = image.cmap.size() / 3U;
        if (colorCount == 0)
        {
            error = L"ILBM has no usable CMAP colors.";
            return false;
        }

        rgb.resize(image.pixels.size());
        for (size_t i = 0; i < image.pixels.size(); ++i)
        {
            const size_t index = static_cast<size_t>(image.pixels[i]);
            if (index >= colorCount)
            {
                error = L"ILBM pixel index exceeds its CMAP.";
                return false;
            }
            rgb[i].r = image.cmap[index * 3 + 0];
            rgb[i].g = image.cmap[index * 3 + 1];
            rgb[i].b = image.cmap[index * 3 + 2];
        }
        return true;
    }

    int NearestVisibleIndex(const RgbColor& color, const std::array<RgbColor, 64>& visible)
    {
        int bestIndex = 0;
        int bestDistance = INT_MAX;
        for (int index = 0; index < 64; ++index)
        {
            const int distance = ColorDistance(color, visible[static_cast<size_t>(index)]);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestIndex = index;
                if (distance == 0)
                    break;
            }
        }
        return bestIndex;
    }

    void EnsureUniqueBaseWords(std::array<std::uint16_t, 32>& baseWords,
        const std::array<double, 4096>& candidateWeights)
    {
        std::array<bool, 4096> used{};
        for (size_t index = 0; index < baseWords.size(); ++index)
        {
            const std::uint16_t current = static_cast<std::uint16_t>(baseWords[index] & 0x0fffU);
            if (!used[current])
            {
                used[current] = true;
                continue;
            }

            int bestWord = -1;
            double bestScore = -1.0;
            for (int candidate = 0; candidate < 4096; ++candidate)
            {
                if (used[static_cast<size_t>(candidate)] || candidateWeights[static_cast<size_t>(candidate)] <= 0.0)
                    continue;

                const RgbColor candidateColor = DecodeRgb12Word(static_cast<std::uint16_t>(candidate));
                int nearestDistance = INT_MAX;
                for (size_t selected = 0; selected < index; ++selected)
                {
                    nearestDistance = std::min(nearestDistance,
                        ColorDistance(candidateColor, DecodeRgb12Word(baseWords[selected])));
                }
                const double score = candidateWeights[static_cast<size_t>(candidate)] *
                    static_cast<double>(nearestDistance + 1);
                if (score > bestScore)
                {
                    bestScore = score;
                    bestWord = candidate;
                }
            }

            if (bestWord >= 0)
                baseWords[index] = static_cast<std::uint16_t>(bestWord);
            used[baseWords[index] & 0x0fffU] = true;
        }
    }

    bool QuantizeRgbToEhb(const std::vector<RgbColor>& sourcePixels, int width, int height,
        EhbImageData& output, std::wstring& error)
    {
        if (width <= 0 || height <= 0 || sourcePixels.size() !=
            static_cast<size_t>(width) * static_cast<size_t>(height))
        {
            error = L"Source image dimensions do not match its pixel data.";
            return false;
        }

        const int paddedWidth = (width + 7) & ~7;
        std::vector<RgbColor> pixels(static_cast<size_t>(paddedWidth) * static_cast<size_t>(height));
        for (int y = 0; y < height; ++y)
        {
            std::copy(sourcePixels.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(y) * static_cast<size_t>(width)),
                sourcePixels.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(y + 1) * static_cast<size_t>(width)),
                pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(y) * static_cast<size_t>(paddedWidth)));
        }

        std::array<std::uint64_t, 4096> histogram{};
        std::array<double, 4096> candidateWeights{};
        for (const RgbColor& color : pixels)
        {
            const std::uint16_t word = EncodeRgb12Word(color);
            ++histogram[word];
            candidateWeights[word] += 1.0;

            const int red = static_cast<int>((word >> 8) & 0x0fU);
            const int green = static_cast<int>((word >> 4) & 0x0fU);
            const int blue = static_cast<int>(word & 0x0fU);
            const std::uint16_t doubled = static_cast<std::uint16_t>(
                (std::min(15, red * 2 + 1) << 8) |
                (std::min(15, green * 2 + 1) << 4) |
                std::min(15, blue * 2 + 1));
            candidateWeights[doubled] += 0.60;
        }

        std::array<std::uint16_t, 32> baseWords{};
        std::array<bool, 4096> selected{};
        const bool keepBlack = histogram[0] > 0;
        if (keepBlack)
        {
            baseWords[0] = 0;
            selected[0] = true;
        }
        else
        {
            const auto first = std::max_element(candidateWeights.begin(), candidateWeights.end());
            baseWords[0] = static_cast<std::uint16_t>(std::distance(candidateWeights.begin(), first));
            selected[baseWords[0]] = true;
        }

        std::array<int, 4096> nearestDistance{};
        const RgbColor firstColor = DecodeRgb12Word(baseWords[0]);
        for (int word = 0; word < 4096; ++word)
            nearestDistance[static_cast<size_t>(word)] = ColorDistance(DecodeRgb12Word(static_cast<std::uint16_t>(word)), firstColor);

        for (size_t paletteIndex = 1; paletteIndex < baseWords.size(); ++paletteIndex)
        {
            int bestWord = -1;
            double bestScore = -1.0;
            for (int word = 0; word < 4096; ++word)
            {
                if (selected[static_cast<size_t>(word)] || candidateWeights[static_cast<size_t>(word)] <= 0.0)
                    continue;
                const double score = candidateWeights[static_cast<size_t>(word)] *
                    static_cast<double>(nearestDistance[static_cast<size_t>(word)] + 1);
                if (score > bestScore)
                {
                    bestScore = score;
                    bestWord = word;
                }
            }
            if (bestWord < 0)
                bestWord = static_cast<int>(paletteIndex);

            baseWords[paletteIndex] = static_cast<std::uint16_t>(bestWord);
            selected[static_cast<size_t>(bestWord)] = true;
            const RgbColor selectedColor = DecodeRgb12Word(static_cast<std::uint16_t>(bestWord));
            for (int word = 0; word < 4096; ++word)
            {
                nearestDistance[static_cast<size_t>(word)] = std::min(
                    nearestDistance[static_cast<size_t>(word)],
                    ColorDistance(DecodeRgb12Word(static_cast<std::uint16_t>(word)), selectedColor));
            }
        }

        for (int iteration = 0; iteration < 10; ++iteration)
        {
            std::array<RgbColor, 32> bases{};
            for (size_t i = 0; i < baseWords.size(); ++i)
                bases[i] = DecodeRgb12Word(baseWords[i]);
            const std::array<RgbColor, 64> visible = BuildVisibleEhbColors(bases);

            std::array<double, 32> sumR{};
            std::array<double, 32> sumG{};
            std::array<double, 32> sumB{};
            std::array<double, 32> sumWeight{};

            for (int word = 0; word < 4096; ++word)
            {
                const std::uint64_t count = histogram[static_cast<size_t>(word)];
                if (count == 0)
                    continue;

                const RgbColor source = DecodeRgb12Word(static_cast<std::uint16_t>(word));
                const int visibleIndex = NearestVisibleIndex(source, visible);
                const int baseIndex = visibleIndex & 31;
                RgbColor target = source;
                if (visibleIndex >= 32)
                {
                    target.r = static_cast<std::uint8_t>(std::min(255, static_cast<int>(source.r) * 2));
                    target.g = static_cast<std::uint8_t>(std::min(255, static_cast<int>(source.g) * 2));
                    target.b = static_cast<std::uint8_t>(std::min(255, static_cast<int>(source.b) * 2));
                }

                const double weight = static_cast<double>(count);
                sumR[static_cast<size_t>(baseIndex)] += static_cast<double>(target.r) * weight;
                sumG[static_cast<size_t>(baseIndex)] += static_cast<double>(target.g) * weight;
                sumB[static_cast<size_t>(baseIndex)] += static_cast<double>(target.b) * weight;
                sumWeight[static_cast<size_t>(baseIndex)] += weight;
            }

            std::array<std::uint16_t, 32> updated = baseWords;
            for (size_t i = 0; i < updated.size(); ++i)
            {
                if (sumWeight[i] <= 0.0)
                    continue;
                RgbColor average;
                average.r = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(sumR[i] / sumWeight[i])), 0, 255));
                average.g = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(sumG[i] / sumWeight[i])), 0, 255));
                average.b = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(sumB[i] / sumWeight[i])), 0, 255));
                updated[i] = EncodeRgb12Word(average);
            }
            if (keepBlack)
                updated[0] = 0;
            EnsureUniqueBaseWords(updated, candidateWeights);
            if (updated == baseWords)
                break;
            baseWords = updated;
        }

        output.width = paddedWidth;
        output.height = height;
        output.directEhb = false;
        for (size_t i = 0; i < baseWords.size(); ++i)
            output.baseColors[i] = DecodeRgb12Word(baseWords[i]);

        const std::array<RgbColor, 64> visible = BuildVisibleEhbColors(output.baseColors);
        output.pixels.resize(pixels.size());
        long double squaredError = 0.0;
        for (size_t i = 0; i < pixels.size(); ++i)
        {
            const int index = NearestVisibleIndex(pixels[i], visible);
            output.pixels[i] = static_cast<std::uint8_t>(index);
            squaredError += static_cast<long double>(ColorDistance(pixels[i], visible[static_cast<size_t>(index)]));
        }
        output.rmse = std::sqrt(static_cast<double>(squaredError /
            static_cast<long double>(std::max<size_t>(1, pixels.size() * 3U))));
        return true;
    }

    bool PrepareEhbFromIlbm(const IlbmImage& image, EhbImageData& output, std::wstring& error)
    {
        const bool isDirectEhb = image.depth == 6 && image.cmap.size() >= 32U * 3U &&
            ((image.hasCamg && (image.camg & kCamgEhb) != 0) || image.cmap.size() == 32U * 3U);
        if (isDirectEhb)
        {
            output.width = (image.width + 7) & ~7;
            output.height = image.height;
            output.directEhb = true;
            output.rmse = 0.0;
            output.pixels.assign(static_cast<size_t>(output.width) * static_cast<size_t>(output.height), 0);
            for (int y = 0; y < image.height; ++y)
            {
                std::copy(image.pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(y) * static_cast<size_t>(image.width)),
                    image.pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(y + 1) * static_cast<size_t>(image.width)),
                    output.pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(y) * static_cast<size_t>(output.width)));
            }
            for (size_t index = 0; index < 32; ++index)
            {
                output.baseColors[index].r = image.cmap[index * 3 + 0];
                output.baseColors[index].g = image.cmap[index * 3 + 1];
                output.baseColors[index].b = image.cmap[index * 3 + 2];
            }
            return true;
        }

        std::vector<RgbColor> rgb;
        if (!IlbmToRgbPixels(image, rgb, error))
            return false;
        return QuantizeRgbToEhb(rgb, image.width, image.height, output, error);
    }

    bool EncodeEhbRuntime(const EhbImageData& image, std::vector<std::uint8_t>& runtimeData,
        std::vector<std::uint8_t>& paletteData, std::wstring& error)
    {
        IlbmImage indexed;
        indexed.width = image.width;
        indexed.height = image.height;
        indexed.depth = 6;
        indexed.pixels = image.pixels;
        if (!EncodeTrimmedPicture(indexed, runtimeData, error))
            return false;

        paletteData = BuildEhbPalette(image.baseColors);
        std::wstring paletteError;
        if (!ValidatePalette128(paletteData, paletteError))
        {
            error = paletteError;
            return false;
        }

        RuntimePictureInfo roundtrip;
        std::wstring roundtripError;
        if (!DecodeTrimmedPicture(runtimeData, roundtrip, roundtripError) ||
            roundtrip.width != image.width || roundtrip.height != image.height ||
            roundtrip.depth != 6 || roundtrip.pixels != image.pixels)
        {
            error = L"Generated EHB runtime picture failed its pixel roundtrip";
            if (!roundtripError.empty())
                error += L": " + roundtripError;
            return false;
        }
        return true;
    }

    bool IsPngFile(const fs::path& file)
    {
        std::vector<std::uint8_t> bytes;
        if (!ReadFile(file, bytes) || bytes.size() < 8)
            return false;
        static const std::uint8_t signature[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
        return std::equal(std::begin(signature), std::end(signature), bytes.begin());
    }

#ifdef _WIN32
    class ScopedGdiplus
    {
    public:
        ScopedGdiplus()
        {
            Gdiplus::GdiplusStartupInput input;
            status_ = Gdiplus::GdiplusStartup(&token_, &input, nullptr);
        }

        ~ScopedGdiplus()
        {
            if (status_ == Gdiplus::Ok)
                Gdiplus::GdiplusShutdown(token_);
        }

        bool Ok() const { return status_ == Gdiplus::Ok; }

    private:
        ULONG_PTR token_ = 0;
        Gdiplus::Status status_ = Gdiplus::GenericError;
    };

    bool LoadPngRgb(const fs::path& file, int& width, int& height,
        std::vector<RgbColor>& pixels, std::wstring& error)
    {
        ScopedGdiplus gdiplus;
        if (!gdiplus.Ok())
        {
            error = L"Could not initialise GDI+ for PNG conversion.";
            return false;
        }

        Gdiplus::Bitmap bitmap(file.wstring().c_str());
        if (bitmap.GetLastStatus() != Gdiplus::Ok)
        {
            error = L"Could not open PNG: " + file.wstring();
            return false;
        }

        width = static_cast<int>(bitmap.GetWidth());
        height = static_cast<int>(bitmap.GetHeight());
        if (width <= 0 || width > 2048 || height <= 0 || height > 2048)
        {
            error = L"PNG dimensions must be between 1 and 2048 pixels.";
            return false;
        }

        pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                Gdiplus::Color color;
                if (bitmap.GetPixel(x, y, &color) != Gdiplus::Ok)
                {
                    error = L"Could not read PNG pixels.";
                    return false;
                }
                const int alpha = static_cast<int>(color.GetA());
                RgbColor out;
                out.r = static_cast<std::uint8_t>((static_cast<int>(color.GetR()) * alpha) / 255);
                out.g = static_cast<std::uint8_t>((static_cast<int>(color.GetG()) * alpha) / 255);
                out.b = static_cast<std::uint8_t>((static_cast<int>(color.GetB()) * alpha) / 255);
                pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = out;
            }
        }
        return true;
    }
#else
    bool LoadPngRgb(const fs::path&, int&, int&, std::vector<RgbColor>&, std::wstring& error)
    {
        error = L"PNG conversion is available in the Visual Studio Windows build.";
        return false;
    }
#endif

    bool ValidatePalette128(const std::vector<std::uint8_t>& data, std::wstring& error)
    {
        if (data.size() != 128)
        {
            error = L"Palette must contain 128 bytes / 64 RGB12 words.";
            return false;
        }

        for (size_t index = 0; index < 32; ++index)
        {
            const std::uint16_t base = ReadBe16(data, index * 2);
            const std::uint16_t half = ReadBe16(data, (index + 32) * 2);
            if ((base & 0xf000U) != 0 || (half & 0xf000U) != 0)
            {
                error = L"Palette contains a value outside Amiga RGB12.";
                return false;
            }

            const std::uint16_t expected = static_cast<std::uint16_t>(
                (((base >> 8) & 0xfU) >> 1) << 8 |
                (((base >> 4) & 0xfU) >> 1) << 4 |
                ((base & 0xfU) >> 1));
            if (half != expected)
            {
                std::wstringstream message;
                message << L"Half-Brite entry " << (index + 32) << L" does not match base entry " << index << L".";
                error = message.str();
                return false;
            }
        }
        return true;
    }

    bool ValidatePalette6(const std::vector<std::uint8_t>& data, std::wstring& error)
    {
        if (data.size() != 64)
        {
            error = L"palette_6 must contain exactly 64 bytes / 32 RGB12 words.";
            return false;
        }
        for (size_t offset = 0; offset < data.size(); offset += 2)
        {
            if ((ReadBe16(data, offset) & 0xf000U) != 0)
            {
                error = L"palette_6 contains a value outside Amiga RGB12.";
                return false;
            }
        }
        return true;
    }

    bool ValidateRemap6(const std::vector<std::uint8_t>& data, std::wstring& error)
    {
        if (data.size() != 4096)
        {
            error = L"remap_6 must contain exactly 4096 bytes.";
            return false;
        }
        const auto invalid = std::find_if(data.begin(), data.end(), [](std::uint8_t value) { return value > 63; });
        if (invalid != data.end())
        {
            error = L"remap_6 contains an index above 63.";
            return false;
        }
        return true;
    }

    bool HasReferenceLayout(const fs::path& root)
    {
        return Exists(root / L"assets_source" / L"iff") &&
            Exists(root / L"misc") && Exists(root / L"pics_ehb");
    }

    fs::path FindSourceRoot(const fs::path& selectedRoot)
    {
        std::vector<fs::path> candidates;
        candidates.push_back(selectedRoot);
        candidates.push_back(selectedRoot / L"GloomAmiga-cleaned");
        if (!selectedRoot.parent_path().empty())
        {
            candidates.push_back(selectedRoot.parent_path());
            candidates.push_back(selectedRoot.parent_path() / L"GloomAmiga-cleaned");
        }

        for (const fs::path& candidate : candidates)
        {
            if (HasReferenceLayout(candidate))
                return candidate;
        }
        return {};
    }


    struct SharedTitleSource
    {
        int width = 0;
        int height = 0;
        int sourceDepth = 0;
        std::vector<RgbColor> pixels;
        std::vector<std::uint8_t> opaque;
        fs::path sourcePath;
        std::wstring sourceKind;
    };

    bool LooksLikeCrM2(const std::vector<std::uint8_t>& data)
    {
        return data.size() >= 14 &&
            data[0] == 'C' && data[1] == 'r' && data[2] == 'M' && data[3] == '2';
    }

    bool ReadMaybeCrM2(const fs::path& file, std::vector<std::uint8_t>& data, bool& wasCrM2,
        std::wstring& error)
    {
        std::vector<std::uint8_t> packed;
        if (!ReadFile(file, packed))
        {
            error = L"Cannot read " + file.wstring();
            return false;
        }

        wasCrM2 = LooksLikeCrM2(packed);
        if (!wasCrM2)
        {
            data = std::move(packed);
            return true;
        }

        const unsigned int unpackedSize = GetSize(packed.data());
        const unsigned int headroom = GetSecDist(packed.data());
        if (unpackedSize == 0 || unpackedSize > 32U * 1024U * 1024U)
        {
            error = L"Invalid CrM2 header in " + file.wstring();
            return false;
        }

        const size_t workSize = std::max(packed.size(),
            static_cast<size_t>(unpackedSize) + static_cast<size_t>(headroom) + 32U);
        std::vector<std::uint8_t> work(workSize, 0);
        std::vector<std::uint8_t> unpacked(unpackedSize, 0);
        std::copy(packed.begin(), packed.end(), work.begin());

        if (Decrunch(work.data(), unpacked.data()) == nullptr)
        {
            error = L"CrM2 decompression failed for " + file.wstring();
            return false;
        }

        data = std::move(unpacked);
        return true;
    }

    bool DecodeRuntimePictureFile(const fs::path& file, RuntimePictureInfo& picture,
        bool& wasCrM2, std::wstring& error)
    {
        std::vector<std::uint8_t> data;
        if (!ReadMaybeCrM2(file, data, wasCrM2, error))
            return false;
        return DecodeTrimmedPicture(data, picture, error);
    }

    bool ReadRuntimePaletteFile(const fs::path& file, size_t neededColors,
        std::vector<RgbColor>& colors, bool& wasCrM2, std::wstring& error)
    {
        std::vector<std::uint8_t> data;
        if (!ReadMaybeCrM2(file, data, wasCrM2, error))
            return false;
        if (data.empty())
        {
            error = L"Palette is empty: " + file.wstring();
            return false;
        }

        // Retail ZGloom palettes normally contain four bytes per entry in the
        // engine's split-nibble layout.  Generated ECS/EHB palettes contain
        // two big-endian RGB12 words per visible color.  The source picture's
        // depth disambiguates the otherwise similar byte patterns safely.
        if ((data.size() % 4U) == 0 && data.size() / 4U >= neededColors && data.size() / 4U <= 256U)
        {
            const size_t entries = std::min<size_t>(data.size() / 4U, 256U);
            colors.resize(entries);
            for (size_t index = 0; index < entries; ++index)
            {
                const size_t offset = index * 4U;
                colors[index].r = static_cast<std::uint8_t>(
                    ((data[offset + 0] & 0x0fU) << 4) | (data[offset + 2] & 0x0fU));
                colors[index].g = static_cast<std::uint8_t>(
                    (data[offset + 1] & 0xf0U) | ((data[offset + 3] >> 4) & 0x0fU));
                colors[index].b = static_cast<std::uint8_t>(
                    ((data[offset + 1] & 0x0fU) << 4) | (data[offset + 3] & 0x0fU));
            }
            return true;
        }

        if ((data.size() % 2U) == 0 && data.size() / 2U >= neededColors && data.size() / 2U <= 256U)
        {
            const size_t entries = std::min<size_t>(data.size() / 2U, 256U);
            colors.resize(entries);
            for (size_t index = 0; index < entries; ++index)
            {
                const std::uint16_t word = ReadBe16(data, index * 2U);
                if ((word & 0xf000U) != 0)
                {
                    error = L"Palette contains a value outside RGB12: " + file.wstring();
                    return false;
                }
                colors[index] = DecodeRgb12Word(word);
            }
            return true;
        }

        if ((data.size() % 3U) == 0 && data.size() / 3U >= neededColors && data.size() / 3U <= 256U)
        {
            const size_t entries = std::min<size_t>(data.size() / 3U, 256U);
            colors.resize(entries);
            std::uint8_t maximum = 0;
            for (size_t index = 0; index < entries * 3U; ++index)
                maximum = std::max(maximum, data[index]);
            const bool nibbleValues = maximum <= 15U;
            for (size_t index = 0; index < entries; ++index)
            {
                const size_t offset = index * 3U;
                auto expand = [nibbleValues](std::uint8_t value) {
                    return nibbleValues ? static_cast<std::uint8_t>((value & 0x0fU) * 17U) : value;
                };
                colors[index] = { expand(data[offset + 0]), expand(data[offset + 1]), expand(data[offset + 2]) };
            }
            return true;
        }

        std::wstringstream message;
        message << L"Unsupported retail palette layout: " << data.size() << L" bytes; picture needs "
            << neededColors << L" colors.\n" << file.wstring();
        error = message.str();
        return false;
    }

    bool LoadIlbmSource(const fs::path& file, SharedTitleSource& source, std::wstring& error)
    {
        IlbmImage image;
        if (!DecodeIlbm(file, image, error))
            return false;

        std::vector<RgbColor> rgb;
        if (!IlbmToRgbPixels(image, rgb, error))
            return false;

        source.width = image.width;
        source.height = image.height;
        source.sourceDepth = image.depth;
        source.pixels = std::move(rgb);
        source.opaque = image.opaque;
        if (source.opaque.size() != source.pixels.size())
            source.opaque.assign(source.pixels.size(), 1);
        source.sourcePath = file;
        source.sourceKind = L"FORM/ILBM";
        return true;
    }

    bool LoadRuntimeSource(const fs::path& imageFile, const fs::path& paletteFile,
        bool transparentIndexZero, SharedTitleSource& source, std::wstring& error)
    {
        RuntimePictureInfo picture;
        bool pictureCrM2 = false;
        if (!DecodeRuntimePictureFile(imageFile, picture, pictureCrM2, error))
        {
            error = L"Cannot decode runtime picture " + imageFile.wstring() + L": " + error;
            return false;
        }

        const size_t neededColors = static_cast<size_t>(1U << picture.depth);
        std::vector<RgbColor> palette;
        bool paletteCrM2 = false;
        if (!ReadRuntimePaletteFile(paletteFile, neededColors, palette, paletteCrM2, error))
        {
            error = L"Cannot read runtime palette " + paletteFile.wstring() + L": " + error;
            return false;
        }

        if (palette.size() < neededColors)
        {
            std::wstringstream message;
            message << L"Palette " << paletteFile.wstring() << L" has " << palette.size()
                << L" entries, but the runtime image needs " << neededColors << L".";
            error = message.str();
            return false;
        }

        source.width = picture.width;
        source.height = picture.height;
        source.sourceDepth = picture.depth;
        source.pixels.resize(picture.pixels.size());
        source.opaque.assign(picture.pixels.size(), 1);
        for (size_t pixel = 0; pixel < picture.pixels.size(); ++pixel)
        {
            const std::uint8_t index = picture.pixels[pixel];
            source.pixels[pixel] = palette[index];
            if (transparentIndexZero && index == 0)
                source.opaque[pixel] = 0;
        }
        source.sourcePath = imageFile;
        source.sourceKind = pictureCrM2 ? L"CrM2 Gloom runtime picture" : L"Gloom runtime picture";
        if (paletteCrM2)
            source.sourceKind += L" + CrM2 palette";
        return true;
    }

    fs::path FindFastTitleIffRoot(const fs::path& selectedRoot)
    {
        std::vector<fs::path> candidates = {
            selectedRoot / L"assets_source" / L"iff",
            selectedRoot / L"GloomAmiga-cleaned" / L"assets_source" / L"iff"
        };
        if (!selectedRoot.parent_path().empty())
        {
            candidates.push_back(selectedRoot.parent_path() / L"assets_source" / L"iff");
            candidates.push_back(selectedRoot.parent_path() / L"GloomAmiga-cleaned" / L"assets_source" / L"iff");
        }

        for (const fs::path& candidate : candidates)
        {
            if (Exists(candidate / L"title.iff"))
                return candidate;
        }
        return {};
    }

    bool LoadFastTitleInput(const fs::path& gameRoot, const fs::path& explicitTitleSource,
        SharedTitleSource& title, fs::path& palettePath, std::wstring& error)
    {
        auto loadSelectedTitle = [&](const fs::path& source) -> bool
        {
            if (!Exists(source))
            {
                error = L"The selected title source does not exist:\n" + source.wstring();
                return false;
            }

            if (IsPngFile(source))
            {
                int width = 0;
                int height = 0;
                std::vector<RgbColor> pixels;
                if (!LoadPngRgb(source, width, height, pixels, error))
                    return false;

                title.width = width;
                title.height = height;
                title.sourceDepth = 0;
                title.pixels = std::move(pixels);
                title.opaque.assign(title.pixels.size(), 1);
                title.sourcePath = source;
                title.sourceKind = L"PNG";
                palettePath.clear();
                return true;
            }

            std::vector<std::uint8_t> header;
            if (!ReadFile(source, header))
            {
                error = L"Cannot read the selected title source:\n" + source.wstring();
                return false;
            }
            if (header.size() >= 12 &&
                std::string(reinterpret_cast<const char*>(header.data()), 4) == "FORM" &&
                std::string(reinterpret_cast<const char*>(header.data() + 8), 4) == "ILBM")
            {
                palettePath.clear();
                return LoadIlbmSource(source, title, error);
            }

            // Gloom runtime pictures have no filename extension.  Their palette
            // normally sits beside them as <filename>.pal, e.g. pics/title.pal.
            std::vector<fs::path> paletteCandidates;
            paletteCandidates.push_back(source.parent_path() / (source.filename().wstring() + L".pal"));
            if (source.has_extension())
            {
                fs::path replaced = source;
                replaced.replace_extension(L".pal");
                paletteCandidates.push_back(replaced);
            }
            paletteCandidates.push_back(gameRoot / L"pics" / L"title.pal");
            paletteCandidates.push_back(gameRoot / L"data" / L"pics" / L"title.pal");

            fs::path palette;
            for (const fs::path& candidate : paletteCandidates)
            {
                if (Exists(candidate))
                {
                    palette = candidate;
                    break;
                }
            }
            if (palette.empty())
            {
                error =
                    L"The selected runtime title has no matching palette.\n\n"
                    L"Expected title.pal beside:\n" + source.wstring();
                return false;
            }

            palettePath = palette;
            return LoadRuntimeSource(source, palette, false, title, error);
        };

        if (!explicitTitleSource.empty())
            return loadSelectedTitle(explicitTitleSource);

        // Backward-compatible automatic lookup for callers that do not supply
        // a title explicitly.  Campaigns are not required to contain
        // assets_source/iff; pics/title + pics/title.pal is the normal path.
        const fs::path roots[] = { gameRoot, gameRoot / L"data" };
        for (const fs::path& root : roots)
        {
            const fs::path image = root / L"pics" / L"title";
            const fs::path palette = root / L"pics" / L"title.pal";
            if (Exists(image) && Exists(palette))
            {
                palettePath = palette;
                return LoadRuntimeSource(image, palette, false, title, error);
            }
        }

        const fs::path iffRoot = FindFastTitleIffRoot(gameRoot);
        if (!iffRoot.empty() && Exists(iffRoot / L"title.iff"))
        {
            if (!LoadIlbmSource(iffRoot / L"title.iff", title, error))
                return false;
            palettePath.clear();
            return true;
        }

        error =
            L"No title source was selected or found.\n\n"
            L"Select the campaign file pics/title; title.pal must be in the same folder.";
        return false;
    }

    bool LoadFastBrushInput(const fs::path& gameRoot, const fs::path& titlePalettePath,
        const fs::path& explicitBrushSource, SharedTitleSource& brush, std::wstring& error)
    {
        // v32b: use the original transparent logo-only ILBM explicitly.
        // assets_source/iff/gloom.iff is 320x66, uses transparent colour 0
        // and contains only the logo.  gloombrush.iff/runtime composites can
        // be fully opaque and cannot produce a valid cookie-cut mask.
        if (!explicitBrushSource.empty())
        {
            if (!Exists(explicitBrushSource))
            {
                error = L"The selected Gloom logo IFF does not exist:\n" + explicitBrushSource.wstring();
                return false;
            }
            if (!LoadIlbmSource(explicitBrushSource, brush, error))
            {
                error = L"Cannot decode the selected Gloom logo IFF:\n" +
                    explicitBrushSource.wstring() + L"\n\n" + error;
                return false;
            }
            return true;
        }

        const fs::path iffRoot = FindFastTitleIffRoot(gameRoot);
        if (!iffRoot.empty())
        {
            const fs::path candidates[] = {
                // gloom.iff retains the real transparent-color mask.  The
                // historical gloombrush.iff is already palette-remapped and
                // may contain an opaque copy of the title-strip background.
                iffRoot / L"gloom.iff",
                iffRoot / L"gloombrush.iff"
            };
            for (const fs::path& candidate : candidates)
            {
                if (Exists(candidate))
                    return LoadIlbmSource(candidate, brush, error);
            }
        }

        fs::path palette = titlePalettePath;
        if (palette.empty())
        {
            const fs::path rootPalette = gameRoot / L"pics" / L"title.pal";
            const fs::path dataPalette = gameRoot / L"data" / L"pics" / L"title.pal";
            if (Exists(rootPalette))
                palette = rootPalette;
            else if (Exists(dataPalette))
                palette = dataPalette;
        }
        if (palette.empty())
        {
            error = L"The runtime brush needs the title palette, but pics/title.pal was not found.";
            return false;
        }

        const fs::path candidates[] = {
            gameRoot / L"pics" / L"gloom",
            gameRoot / L"pics" / L"gloombrush",
            gameRoot / L"gloombrush",
            gameRoot / L"data" / L"pics" / L"gloom",
            gameRoot / L"data" / L"pics" / L"gloombrush"
        };
        for (const fs::path& candidate : candidates)
        {
            if (Exists(candidate))
                return LoadRuntimeSource(candidate, palette, true, brush, error);
        }

        error =
            L"No Gloom title brush was found.\nExpected assets_source/iff/gloombrush.iff, "
            L"assets_source/iff/gloom.iff, pics/gloom or pics/gloombrush.";
        return false;
    }

    std::array<RgbColor, 3> LoadFastTitleFontColors(const fs::path& gameRoot,
        bool& usedFallback, fs::path& sourcePath, std::wstring& warning)
    {
        usedFallback = false;
        const fs::path iffRoot = FindFastTitleIffRoot(gameRoot);
        if (!iffRoot.empty() && Exists(iffRoot / L"bigfont2.iff"))
        {
            IlbmImage font;
            std::wstring error;
            if (DecodeIlbm(iffRoot / L"bigfont2.iff", font, error) && font.cmap.size() >= 12)
            {
                std::array<RgbColor, 3> colors{};
                for (size_t index = 0; index < colors.size(); ++index)
                {
                    colors[index].r = font.cmap[(index + 1) * 3 + 0];
                    colors[index].g = font.cmap[(index + 1) * 3 + 1];
                    colors[index].b = font.cmap[(index + 1) * 3 + 2];
                }
                sourcePath = iffRoot / L"bigfont2.iff";
                return colors;
            }
            warning = L"bigfont2.iff could not be decoded; using the verified public-source yellow RGB12 values.";
        }
        else
        {
            warning = L"bigfont2.iff was not found; using the verified public-source yellow RGB12 values.";
        }

        usedFallback = true;
        sourcePath.clear();
        return {
            DecodeRgb12Word(0x0220),
            DecodeRgb12Word(0x0b92),
            DecodeRgb12Word(0x0ed2)
        };
    }

    int NearestAllowedVisibleIndex(const RgbColor& color,
        const std::array<RgbColor, 64>& visible)
    {
        int bestIndex = 0;
        int bestDistance = INT_MAX;
        for (int index = 0; index < 64; ++index)
        {
            if ((index >= 1 && index <= 3) || (index >= 33 && index <= 35))
                continue;
            const int distance = ColorDistance(color, visible[static_cast<size_t>(index)]);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestIndex = index;
                if (distance == 0)
                    break;
            }
        }
        return bestIndex;
    }

    void EnsureUniqueAdjustableBaseWords(std::array<std::uint16_t, 32>& baseWords,
        const std::array<double, 4096>& candidateWeights)
    {
        std::array<bool, 4096> used{};
        for (size_t index = 0; index < 4; ++index)
            used[baseWords[index] & 0x0fffU] = true;

        for (size_t index = 4; index < baseWords.size(); ++index)
        {
            std::uint16_t current = static_cast<std::uint16_t>(baseWords[index] & 0x0fffU);
            if (!used[current])
            {
                used[current] = true;
                continue;
            }

            int bestWord = -1;
            double bestScore = -1.0;
            for (int candidate = 0; candidate < 4096; ++candidate)
            {
                if (used[static_cast<size_t>(candidate)] || candidateWeights[static_cast<size_t>(candidate)] <= 0.0)
                    continue;

                const RgbColor candidateColor = DecodeRgb12Word(static_cast<std::uint16_t>(candidate));
                int nearestDistance = INT_MAX;
                for (size_t selected = 0; selected < index; ++selected)
                {
                    nearestDistance = std::min(nearestDistance,
                        ColorDistance(candidateColor, DecodeRgb12Word(baseWords[selected])));
                }
                const double score = candidateWeights[static_cast<size_t>(candidate)] *
                    static_cast<double>(nearestDistance + 1);
                if (score > bestScore)
                {
                    bestScore = score;
                    bestWord = candidate;
                }
            }

            if (bestWord >= 0)
                current = static_cast<std::uint16_t>(bestWord);
            else
            {
                for (int candidate = 0; candidate < 4096; ++candidate)
                {
                    if (!used[static_cast<size_t>(candidate)])
                    {
                        current = static_cast<std::uint16_t>(candidate);
                        break;
                    }
                }
            }
            baseWords[index] = current;
            used[current] = true;
        }
    }

    void AddSharedPaletteSamples(const SharedTitleSource& source, double weight,
        std::array<double, 4096>& histogram, std::array<double, 4096>& candidates)
    {
        for (size_t pixel = 0; pixel < source.pixels.size(); ++pixel)
        {
            if (!source.opaque.empty() && source.opaque[pixel] == 0)
                continue;

            const std::uint16_t word = EncodeRgb12Word(source.pixels[pixel]);
            histogram[word] += weight;
            candidates[word] += weight;

            const int red = static_cast<int>((word >> 8) & 0x0fU);
            const int green = static_cast<int>((word >> 4) & 0x0fU);
            const int blue = static_cast<int>(word & 0x0fU);
            const std::uint16_t doubled = static_cast<std::uint16_t>(
                (std::min(15, red * 2 + 1) << 8) |
                (std::min(15, green * 2 + 1) << 4) |
                std::min(15, blue * 2 + 1));
            candidates[doubled] += weight * 0.60;
        }
    }

    bool BuildSharedTitlePalette(const SharedTitleSource& title, const SharedTitleSource& brush,
        const std::array<RgbColor, 3>& fontColors, std::array<RgbColor, 32>& baseColors,
        std::wstring& error)
    {
        if (title.pixels.empty() || brush.pixels.empty())
        {
            error = L"Title or brush source contains no pixels.";
            return false;
        }

        std::array<double, 4096> histogram{};
        std::array<double, 4096> candidateWeights{};
        AddSharedPaletteSamples(title, 1.0, histogram, candidateWeights);
        AddSharedPaletteSamples(brush, 3.0, histogram, candidateWeights);

        std::array<std::uint16_t, 32> baseWords{};
        baseWords[0] = 0;
        baseWords[1] = EncodeRgb12Word(fontColors[0]);
        baseWords[2] = EncodeRgb12Word(fontColors[1]);
        baseWords[3] = EncodeRgb12Word(fontColors[2]);

        std::array<bool, 4096> selected{};
        for (size_t index = 0; index < 4; ++index)
            selected[baseWords[index]] = true;

        std::array<int, 4096> nearestDistance{};
        for (int word = 0; word < 4096; ++word)
        {
            const RgbColor color = DecodeRgb12Word(static_cast<std::uint16_t>(word));
            int distance = INT_MAX;
            for (size_t fixed = 0; fixed < 4; ++fixed)
                distance = std::min(distance, ColorDistance(color, DecodeRgb12Word(baseWords[fixed])));
            nearestDistance[static_cast<size_t>(word)] = distance;
        }

        for (size_t paletteIndex = 4; paletteIndex < baseWords.size(); ++paletteIndex)
        {
            int bestWord = -1;
            double bestScore = -1.0;
            for (int word = 0; word < 4096; ++word)
            {
                if (selected[static_cast<size_t>(word)] || candidateWeights[static_cast<size_t>(word)] <= 0.0)
                    continue;
                const double score = candidateWeights[static_cast<size_t>(word)] *
                    static_cast<double>(nearestDistance[static_cast<size_t>(word)] + 1);
                if (score > bestScore)
                {
                    bestScore = score;
                    bestWord = word;
                }
            }
            if (bestWord < 0)
            {
                for (int word = 0; word < 4096; ++word)
                {
                    if (!selected[static_cast<size_t>(word)])
                    {
                        bestWord = word;
                        break;
                    }
                }
            }

            baseWords[paletteIndex] = static_cast<std::uint16_t>(bestWord);
            selected[static_cast<size_t>(bestWord)] = true;
            const RgbColor selectedColor = DecodeRgb12Word(static_cast<std::uint16_t>(bestWord));
            for (int word = 0; word < 4096; ++word)
            {
                nearestDistance[static_cast<size_t>(word)] = std::min(
                    nearestDistance[static_cast<size_t>(word)],
                    ColorDistance(DecodeRgb12Word(static_cast<std::uint16_t>(word)), selectedColor));
            }
        }

        for (int iteration = 0; iteration < 10; ++iteration)
        {
            std::array<RgbColor, 32> bases{};
            for (size_t index = 0; index < baseWords.size(); ++index)
                bases[index] = DecodeRgb12Word(baseWords[index]);
            const std::array<RgbColor, 64> visible = BuildVisibleEhbColors(bases);

            std::array<double, 32> sumR{};
            std::array<double, 32> sumG{};
            std::array<double, 32> sumB{};
            std::array<double, 32> sumWeight{};

            for (int word = 0; word < 4096; ++word)
            {
                const double weight = histogram[static_cast<size_t>(word)];
                if (weight <= 0.0)
                    continue;

                const RgbColor source = DecodeRgb12Word(static_cast<std::uint16_t>(word));
                const int visibleIndex = NearestAllowedVisibleIndex(source, visible);
                const int baseIndex = visibleIndex & 31;
                RgbColor target = source;
                if (visibleIndex >= 32)
                {
                    target.r = static_cast<std::uint8_t>(std::min(255, static_cast<int>(source.r) * 2));
                    target.g = static_cast<std::uint8_t>(std::min(255, static_cast<int>(source.g) * 2));
                    target.b = static_cast<std::uint8_t>(std::min(255, static_cast<int>(source.b) * 2));
                }

                sumR[static_cast<size_t>(baseIndex)] += static_cast<double>(target.r) * weight;
                sumG[static_cast<size_t>(baseIndex)] += static_cast<double>(target.g) * weight;
                sumB[static_cast<size_t>(baseIndex)] += static_cast<double>(target.b) * weight;
                sumWeight[static_cast<size_t>(baseIndex)] += weight;
            }

            std::array<std::uint16_t, 32> updated = baseWords;
            for (size_t index = 4; index < updated.size(); ++index)
            {
                if (sumWeight[index] <= 0.0)
                    continue;
                RgbColor average;
                average.r = static_cast<std::uint8_t>(std::clamp(
                    static_cast<int>(std::lround(sumR[index] / sumWeight[index])), 0, 255));
                average.g = static_cast<std::uint8_t>(std::clamp(
                    static_cast<int>(std::lround(sumG[index] / sumWeight[index])), 0, 255));
                average.b = static_cast<std::uint8_t>(std::clamp(
                    static_cast<int>(std::lround(sumB[index] / sumWeight[index])), 0, 255));
                updated[index] = EncodeRgb12Word(average);
            }

            updated[0] = 0;
            updated[1] = EncodeRgb12Word(fontColors[0]);
            updated[2] = EncodeRgb12Word(fontColors[1]);
            updated[3] = EncodeRgb12Word(fontColors[2]);
            EnsureUniqueAdjustableBaseWords(updated, candidateWeights);
            if (updated == baseWords)
                break;
            baseWords = updated;
        }

        for (size_t index = 0; index < baseWords.size(); ++index)
            baseColors[index] = DecodeRgb12Word(baseWords[index]);
        return true;
    }

    bool MapSharedTitleSource(const SharedTitleSource& source,
        const std::array<RgbColor, 32>& baseColors, EhbImageData& output,
        std::vector<std::uint8_t>* outputMask, std::wstring& error)
    {
        if (source.width <= 0 || source.height <= 0 ||
            source.pixels.size() != static_cast<size_t>(source.width) * static_cast<size_t>(source.height))
        {
            error = L"Source dimensions do not match its pixel data.";
            return false;
        }

        const int paddedWidth = (source.width + 7) & ~7;
        output.width = paddedWidth;
        output.height = source.height;
        output.baseColors = baseColors;
        output.directEhb = false;
        output.pixels.assign(static_cast<size_t>(paddedWidth) * static_cast<size_t>(source.height), 0);
        if (outputMask)
            outputMask->assign(output.pixels.size(), 0);

        const std::array<RgbColor, 64> visible = BuildVisibleEhbColors(baseColors);
        long double squaredError = 0.0;
        size_t measuredChannels = 0;

        for (int y = 0; y < source.height; ++y)
        {
            for (int x = 0; x < source.width; ++x)
            {
                const size_t sourcePixel = static_cast<size_t>(y) * static_cast<size_t>(source.width) +
                    static_cast<size_t>(x);
                const size_t targetPixel = static_cast<size_t>(y) * static_cast<size_t>(paddedWidth) +
                    static_cast<size_t>(x);
                const bool opaque = source.opaque.empty() || source.opaque[sourcePixel] != 0;
                if (!opaque)
                    continue;

                const int index = NearestAllowedVisibleIndex(source.pixels[sourcePixel], visible);
                output.pixels[targetPixel] = static_cast<std::uint8_t>(index);
                if (outputMask)
                    (*outputMask)[targetPixel] = 1;
                squaredError += static_cast<long double>(
                    ColorDistance(source.pixels[sourcePixel], visible[static_cast<size_t>(index)]));
                measuredChannels += 3;
            }
        }

        output.rmse = std::sqrt(static_cast<double>(
            squaredError / static_cast<long double>(std::max<size_t>(1, measuredChannels))));
        return true;
    }

    bool EncodeMaskRuntime(int width, int height, const std::vector<std::uint8_t>& mask,
        std::vector<std::uint8_t>& runtimeData, std::wstring& error)
    {
        IlbmImage indexed;
        indexed.width = width;
        indexed.height = height;
        indexed.depth = 1;
        indexed.pixels = mask;
        if (!EncodeTrimmedPicture(indexed, runtimeData, error))
            return false;

        RuntimePictureInfo roundtrip;
        std::wstring roundtripError;
        if (!DecodeTrimmedPicture(runtimeData, roundtrip, roundtripError) ||
            roundtrip.width != width || roundtrip.height != height ||
            roundtrip.depth != 1 || roundtrip.pixels != mask)
        {
            error = L"Generated one-plane brush mask failed its pixel roundtrip";
            if (!roundtripError.empty())
                error += L": " + roundtripError;
            return false;
        }
        return true;
    }

    std::wstring LowerPathName(const fs::path& path)
    {
        std::wstring value = path.filename().wstring();
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
            return static_cast<wchar_t>(std::towlower(ch));
            });
        return value;
    }

    std::vector<fs::path> ListRetailFiles(const fs::path& folder)
    {
        std::vector<fs::path> files;
        std::error_code ec;
        if (!fs::is_directory(folder, ec) || ec)
            return files;

        for (const fs::directory_entry& entry : fs::directory_iterator(folder, ec))
        {
            if (ec)
                break;
            std::error_code itemError;
            if (entry.is_regular_file(itemError) && !itemError)
                files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end(), [](const fs::path& left, const fs::path& right) {
            return LowerPathName(left) < LowerPathName(right);
            });
        return files;
    }

    fs::path FindRetailFile(const fs::path& folder, const std::wstring& wantedName)
    {
        std::wstring wanted = wantedName;
        std::transform(wanted.begin(), wanted.end(), wanted.begin(), [](wchar_t ch) {
            return static_cast<wchar_t>(std::towlower(ch));
            });
        for (const fs::path& file : ListRetailFiles(folder))
        {
            if (LowerPathName(file) == wanted)
                return file;
        }
        return {};
    }

    bool IsRetailPictureCandidate(const fs::path& file)
    {
        const std::wstring name = LowerPathName(file);
        const std::wstring extension = [&]() {
            std::wstring value = file.extension().wstring();
            std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
                return static_cast<wchar_t>(std::towlower(ch));
                });
            return value;
            }();

        if (extension == L".pal" || extension == L".txt" || extension == L".info" ||
            extension == L".md" || extension == L".log")
            return false;
        if (name.size() >= 4 && name.substr(name.size() - 4) == L".bak")
            return false;
        if (name.find(L".pal.") != std::wstring::npos)
            return false;
        return true;
    }

    std::wstring RetailAssetName(const fs::path& file)
    {
        if (file.has_extension())
            return file.stem().wstring();
        return file.filename().wstring();
    }

    fs::path FindRetailPalette(const fs::path& imageFile)
    {
        const fs::path folder = imageFile.parent_path();
        const std::wstring filename = imageFile.filename().wstring();
        const std::wstring stem = imageFile.stem().wstring();
        const std::wstring asset = [&]() {
            std::wstring value = RetailAssetName(imageFile);
            std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
                return static_cast<wchar_t>(std::towlower(ch));
                });
            return value;
            }();

        const std::wstring candidates[] = {
            filename + L".pal",
            stem + L".pal"
        };
        for (const std::wstring& candidate : candidates)
        {
            const fs::path found = FindRetailFile(folder, candidate);
            if (!found.empty())
                return found;
        }

        if (asset == L"gloom" || asset == L"gloombrush")
        {
            const wchar_t* sharedCandidates[] = {
                L"gloom.pal", L"gloombrush.pal", L"title.pal", L"blackmagic.pal"
            };
            for (const wchar_t* candidate : sharedCandidates)
            {
                const fs::path found = FindRetailFile(folder, candidate);
                if (!found.empty())
                    return found;
            }
        }
        return {};
    }

    fs::path FindRetailPicture(const fs::path& folder, const std::wstring& wantedAsset)
    {
        std::wstring wanted = wantedAsset;
        std::transform(wanted.begin(), wanted.end(), wanted.begin(), [](wchar_t ch) {
            return static_cast<wchar_t>(std::towlower(ch));
            });
        for (const fs::path& file : ListRetailFiles(folder))
        {
            if (!IsRetailPictureCandidate(file))
                continue;
            std::wstring asset = RetailAssetName(file);
            std::transform(asset.begin(), asset.end(), asset.begin(), [](wchar_t ch) {
                return static_cast<wchar_t>(std::towlower(ch));
                });
            if (asset == wanted)
                return file;
        }
        return {};
    }

    void ReportIlbm(std::wstringstream& report, const wchar_t* label, const IlbmImage& image)
    {
        report << L"[OK] " << label << L": " << image.width << L"x" << image.height
            << L", " << image.depth << L" planes, CMAP=" << (image.cmap.size() / 3) << L" colors";
        if (image.hasCamg)
        {
            report << L", CAMG=$" << std::uppercase << std::hex << std::setw(8) << std::setfill(L'0')
                << image.camg << std::dec << std::nouppercase << std::setfill(L' ')
                << ((image.camg & kCamgEhb) ? L" (EHB)" : L" (EHB bit missing)");
        }
        else
        {
            report << L", CAMG missing";
        }
        report << L".\n";
    }
}

namespace EhbExport
{
    Result ExportReferenceSet(const fs::path& gameRoot, const fs::path& outputRoot)
    {
        Result result;
        const fs::path sourceRoot = FindSourceRoot(gameRoot);
        if (sourceRoot.empty())
        {
            result.errors = 1;
            result.message =
                L"The selected game root does not contain the published ECS/EHB reference sources.\n\n"
                L"Expected: assets_source/iff, pics_ehb and misc.\n"
                L"Select the extracted GloomAmiga-cleaned source root first.";
            return result;
        }
        if (outputRoot.empty())
        {
            result.errors = 1;
            result.message = L"No ECS/EHB output folder was selected.";
            return result;
        }

        const fs::path iffRoot = sourceRoot / L"assets_source" / L"iff";
        const fs::path picturesOutput = outputRoot / L"pics_ehb";
        const fs::path miscOutput = outputRoot / L"misc";

        std::error_code ec;
        fs::create_directories(picturesOutput, ec);
        if (!ec)
            fs::create_directories(miscOutput, ec);
        if (ec)
        {
            result.errors = 1;
            result.message = L"Cannot create the ECS/EHB output folders:\n" + outputRoot.wstring();
            return result;
        }

        std::wstringstream report;
        report << L"ZGloomEditor 1.2.0 - ECS/EHB reference export\n";
        report << L"Source: " << sourceRoot.wstring() << L"\n";
        report << L"Target: " << outputRoot.wstring() << L"\n\n";
        report << L"This export preserves the original palette_6/remap_6 pair, copies verified EHB references and converts the missing AGA theend ILBM with EHB-aware quantization.\n\n";

        auto validateAndCopyPalette128 = [&](const fs::path& source, const fs::path& target, const wchar_t* label) {
            std::vector<std::uint8_t> data;
            std::wstring validationError;
            if (!ReadFile(source, data))
            {
                ++result.errors;
                report << L"[ERROR] Missing " << label << L": " << source.wstring() << L"\n";
                return false;
            }
            if (!ValidatePalette128(data, validationError))
            {
                ++result.errors;
                report << L"[ERROR] Invalid " << label << L": " << validationError << L"\n";
                return false;
            }
            if (!CopyFileSafe(source, target))
            {
                ++result.errors;
                report << L"[ERROR] Cannot copy " << label << L" to " << target.wstring() << L"\n";
                return false;
            }
            ++result.copiedFiles;
            report << L"[OK] " << label << L": copied original 128-byte EHB palette.\n";
            return true;
        };

        // Preserve the original title runtime pair whenever it is available.
        const fs::path originalTitle = sourceRoot / L"pics_ehb" / L"title";
        const fs::path originalTitlePalette = sourceRoot / L"pics_ehb" / L"title.pal";
        bool titleReady = false;
        if (Exists(originalTitle) && Exists(originalTitlePalette))
        {
            std::vector<std::uint8_t> runtimeData;
            std::vector<std::uint8_t> paletteData;
            RuntimePictureInfo decoded;
            std::wstring runtimeError;
            std::wstring paletteError;

            const bool runtimeValid = ReadFile(originalTitle, runtimeData) &&
                DecodeTrimmedPicture(runtimeData, decoded, runtimeError) && decoded.depth == 6;
            const bool paletteValid = ReadFile(originalTitlePalette, paletteData) &&
                ValidatePalette128(paletteData, paletteError);

            if (runtimeValid && paletteValid &&
                CopyFileSafe(originalTitle, picturesOutput / L"title") &&
                CopyFileSafe(originalTitlePalette, picturesOutput / L"title.pal"))
            {
                result.copiedFiles += 2;
                ++result.roundtripChecks;
                titleReady = true;
                report << L"[OK] title: copied original runtime image and palette ("
                    << decoded.width << L"x" << decoded.height << L", 6 planes).\n";
            }
            else
            {
                ++result.warnings;
                report << L"[WARN] Original title runtime pair could not be validated/copied; title_ehb.iff fallback will be attempted.\n";
                if (!runtimeError.empty())
                    report << L"       Runtime: " << runtimeError << L"\n";
                if (!paletteError.empty())
                    report << L"       Palette: " << paletteError << L"\n";
            }
        }

        auto generatePicture = [&](const wchar_t* sourceName, const wchar_t* targetName, bool required) {
            const fs::path source = iffRoot / sourceName;
            if (!Exists(source))
            {
                if (required)
                {
                    ++result.warnings;
                    report << L"[WARN] Missing EHB source ILBM: " << source.wstring() << L"\n";
                }
                return false;
            }

            IlbmImage image;
            std::wstring decodeError;
            if (!DecodeIlbm(source, image, decodeError))
            {
                ++result.errors;
                report << L"[ERROR] " << decodeError << L"\n";
                return false;
            }
            ReportIlbm(report, sourceName, image);
            if (!image.hasCamg || (image.camg & kCamgEhb) == 0)
            {
                ++result.warnings;
                report << L"       [WARN] ILBM is six-plane but does not declare the EHB CAMG bit.\n";
            }

            std::vector<std::uint8_t> runtimeData;
            std::wstring encodeError;
            if (!EncodeTrimmedPicture(image, runtimeData, encodeError))
            {
                ++result.errors;
                report << L"[ERROR] Cannot encode " << targetName << L": " << encodeError << L"\n";
                return false;
            }

            const std::vector<std::uint8_t> paletteData = BuildEhbPalette(image.cmap);
            std::wstring paletteError;
            if (!ValidatePalette128(paletteData, paletteError))
            {
                ++result.errors;
                report << L"[ERROR] Generated palette for " << targetName << L" is invalid: " << paletteError << L"\n";
                return false;
            }

            RuntimePictureInfo roundtrip;
            std::wstring roundtripError;
            if (!DecodeTrimmedPicture(runtimeData, roundtrip, roundtripError) ||
                roundtrip.width != image.width || roundtrip.height != image.height ||
                roundtrip.depth != 6 || roundtrip.pixels != image.pixels)
            {
                ++result.errors;
                report << L"[ERROR] Runtime roundtrip failed for " << targetName;
                if (!roundtripError.empty())
                    report << L": " << roundtripError;
                report << L"\n";
                return false;
            }

            const fs::path imageTarget = picturesOutput / targetName;
            const fs::path paletteTarget = picturesOutput / (std::wstring(targetName) + L".pal");
            if (!WriteFile(imageTarget, runtimeData) || !WriteFile(paletteTarget, paletteData))
            {
                ++result.errors;
                report << L"[ERROR] Cannot write runtime image/palette for " << targetName << L".\n";
                return false;
            }

            ++result.generatedPictures;
            ++result.roundtripChecks;
            report << L"[OK] " << targetName << L": runtime image " << runtimeData.size()
                << L" bytes, palette 128 bytes, pixel-exact roundtrip passed.\n";
            return true;
        };

        auto generateConvertedPicture = [&](const fs::path& source, const wchar_t* targetName, bool required) {
            if (!Exists(source))
            {
                if (required)
                {
                    ++result.warnings;
                    report << L"[WARN] Missing AGA/ILBM conversion source: " << source.wstring() << L"\n";
                }
                return false;
            }

            IlbmImage sourceImage;
            std::wstring decodeError;
            if (!DecodeIlbm(source, sourceImage, decodeError))
            {
                ++result.errors;
                report << L"[ERROR] " << decodeError << L"\n";
                return false;
            }
            const std::wstring sourceLabel = source.filename().wstring();
            ReportIlbm(report, sourceLabel.c_str(), sourceImage);

            EhbImageData ehbImage;
            std::wstring conversionError;
            if (!PrepareEhbFromIlbm(sourceImage, ehbImage, conversionError))
            {
                ++result.errors;
                report << L"[ERROR] Cannot convert " << source.filename().wstring()
                    << L" to EHB: " << conversionError << L"\n";
                return false;
            }

            std::vector<std::uint8_t> runtimeData;
            std::vector<std::uint8_t> paletteData;
            if (!EncodeEhbRuntime(ehbImage, runtimeData, paletteData, conversionError))
            {
                ++result.errors;
                report << L"[ERROR] Cannot encode " << targetName << L": " << conversionError << L"\n";
                return false;
            }

            const fs::path imageTarget = picturesOutput / targetName;
            const fs::path paletteTarget = picturesOutput / (std::wstring(targetName) + L".pal");
            if (!WriteFile(imageTarget, runtimeData) || !WriteFile(paletteTarget, paletteData))
            {
                ++result.errors;
                report << L"[ERROR] Cannot write runtime image/palette for " << targetName << L".\n";
                return false;
            }

            ++result.generatedPictures;
            ++result.roundtripChecks;
            report << L"[OK] " << targetName << L": " << sourceImage.depth << L"-plane ILBM -> 6-plane EHB, "
                << ehbImage.width << L"x" << ehbImage.height << L", runtime " << runtimeData.size()
                << L" bytes, RMSE " << std::fixed << std::setprecision(2) << ehbImage.rmse
                << std::defaultfloat << L", pixel roundtrip passed.\n";
            return true;
        };

        if (!titleReady)
            titleReady = generatePicture(L"title_ehb.iff", L"title", true);

        generatePicture(L"hulk_ehb.iff", L"spacehulk", true);
        generatePicture(L"gothic_ehb.iff", L"gothic", true);
        generatePicture(L"hell_ehb.iff", L"hell", true);
        generatePicture(L"combat_ehb.iff", L"combat", false);

        // BlackMagic deliberately keeps the shared pics/blackmagic image. Only its
        // verified original ECS palette belongs in pics_ehb.
        validateAndCopyPalette128(sourceRoot / L"pics_ehb" / L"blackmagic.pal",
            picturesOutput / L"blackmagic.pal", L"blackmagic.pal");

        const fs::path blackmagicSource = sourceRoot / L"pics" / L"blackmagic";
        if (Exists(blackmagicSource))
        {
            std::vector<std::uint8_t> data;
            RuntimePictureInfo picture;
            std::wstring error;
            if (ReadFile(blackmagicSource, data) && DecodeTrimmedPicture(data, picture, error))
            {
                ++result.roundtripChecks;
                report << L"[OK] pics/blackmagic shared runtime image: " << picture.width << L"x"
                    << picture.height << L", " << picture.depth << L" planes. It is not duplicated under pics_ehb.\n";
            }
            else
            {
                ++result.warnings;
                report << L"[WARN] Shared pics/blackmagic could not be validated: " << error << L"\n";
            }
        }
        else
        {
            ++result.warnings;
            report << L"[WARN] Shared pics/blackmagic is missing. Current gloom2.s still loads this image for ECS.\n";
        }

        // Analyze the historical BlackMagic EHB ILBM, but never substitute its
        // palette for the shared runtime image automatically.
        const fs::path blackmagicIlbm = iffRoot / L"blackmagic_ehb.iff";
        if (Exists(blackmagicIlbm))
        {
            IlbmImage image;
            std::wstring error;
            if (DecodeIlbm(blackmagicIlbm, image, error))
            {
                ReportIlbm(report, L"blackmagic_ehb.iff (reference only)", image);
                report << L"       Runtime rule: keep pics/blackmagic + original pics_ehb/blackmagic.pal.\n";
            }
            else
            {
                ++result.warnings;
                report << L"[WARN] " << error << L"\n";
            }
        }

        auto copyGameplayFile = [&](const wchar_t* name, bool palette) {
            const fs::path source = sourceRoot / L"misc" / name;
            const fs::path target = miscOutput / name;
            std::vector<std::uint8_t> data;
            std::wstring validationError;
            if (!ReadFile(source, data))
            {
                ++result.errors;
                report << L"[ERROR] Missing misc/" << name << L".\n";
                return;
            }

            const bool valid = palette ? ValidatePalette6(data, validationError) : ValidateRemap6(data, validationError);
            if (!valid)
            {
                ++result.errors;
                report << L"[ERROR] Invalid misc/" << name << L": " << validationError << L"\n";
                return;
            }
            if (!CopyFileSafe(source, target))
            {
                ++result.errors;
                report << L"[ERROR] Cannot copy misc/" << name << L".\n";
                return;
            }

            ++result.copiedFiles;
            report << L"[OK] misc/" << name << L": copied and validated (" << data.size() << L" bytes).\n";
        };

        copyGameplayFile(L"palette_6", true);
        copyGameplayFile(L"remap_6", false);

        bool theEndReady = false;
        if (Exists(iffRoot / L"theend_ehb.iff"))
            theEndReady = generatePicture(L"theend_ehb.iff", L"theend", true);
        else
        {
            const fs::path candidates[] = {
                iffRoot / L"theend.iff",
                sourceRoot / L"assets_source" / L"iff_no_extension" / L"theend"
            };
            for (const fs::path& candidate : candidates)
            {
                if (Exists(candidate))
                {
                    theEndReady = generateConvertedPicture(candidate, L"theend", true);
                    break;
                }
            }
        }
        if (!theEndReady)
        {
            ++result.warnings;
            report << L"[WARN] No usable theend EHB/AGA ILBM source was found; pics_ehb/theend was not generated.\n";
        }

        report << L"\nRuntime notes\n";
        report << L"-------------\n";
        report << L"- hulk_ehb.iff is exported as pics_ehb/spacehulk for the script command pict_spacehulk.\n";
        report << L"- BlackMagic keeps pics/blackmagic and receives only pics_ehb/blackmagic.pal.\n";
        report << L"- combat is exported under pics_ehb for the ECS branch; the published gloom2.s still uses pics/combat.\n";
        report << L"- AGA/full ILBM and PNG files can now be converted with Campaign > Convert PNG/ILBM to Amiga ECS/EHB.\n";
        report << L"- palette_6/remap_6 are still preserved from the original ECS reference set and are not regenerated.\n";

        result.reportFile = outputRoot / L"ecs_export_report.txt";
        std::wofstream reportOutput(result.reportFile, std::ios::trunc);
        if (reportOutput)
        {
            reportOutput << report.str();
        }
        else
        {
            ++result.errors;
        }

        result.success = result.errors == 0 && titleReady &&
            (result.generatedPictures > 0 || result.copiedFiles > 0);

        std::wstringstream message;
        message << (result.success ? L"ECS/EHB reference export completed." : L"ECS/EHB reference export completed with errors.") << L"\n\n";
        message << L"Generated EHB pictures: " << result.generatedPictures << L"\n";
        message << L"Copied original files: " << result.copiedFiles << L"\n";
        message << L"Roundtrip/format checks: " << result.roundtripChecks << L"\n";
        message << L"Warnings: " << result.warnings << L"\n";
        message << L"Errors: " << result.errors << L"\n\n";
        message << L"Report: " << result.reportFile.wstring();
        result.message = message.str();
        return result;
    }

    Result ExportRetailPictureSet(const fs::path& gameRoot, const fs::path& pictureDir,
        const fs::path& outputRoot, bool composeGloomTitleBrush)
    {
        Result result;
        if (gameRoot.empty() || !Exists(gameRoot))
        {
            result.errors = 1;
            result.message = L"The selected game root does not exist.";
            return result;
        }
        if (pictureDir.empty() || !Exists(pictureDir))
        {
            result.errors = 1;
            result.message = L"The detected retail picture folder does not exist:\n" + pictureDir.wstring();
            return result;
        }
        if (outputRoot.empty())
        {
            result.errors = 1;
            result.message = L"No Retail ECS picture output folder was selected.";
            return result;
        }

        const bool zombieMassacre = LowerPathName(pictureDir) == L"pixs";
        const fs::path picturesOutput = outputRoot / (zombieMassacre ? L"pixs_ehb" : L"pics_ehb");
        std::error_code ec;
        fs::create_directories(picturesOutput, ec);
        if (ec)
        {
            result.errors = 1;
            result.message = L"Cannot create the output folder:\n" + picturesOutput.wstring();
            return result;
        }

        bool fontFallback = false;
        fs::path fontSource;
        std::wstring fontWarning;
        const std::array<RgbColor, 3> fontColors =
            LoadFastTitleFontColors(gameRoot, fontFallback, fontSource, fontWarning);
        if (fontFallback)
            ++result.warnings;

        std::wstringstream report;
        report << L"ZGloomEditor 1.2.0 - Retail ECS/EHB picture export\n";
        report << L"Game root: " << gameRoot.wstring() << L"\n";
        report << L"Retail pictures: " << pictureDir.wstring() << L"\n";
        report << L"Target: " << picturesOutput.wstring() << L"\n\n";
        report << L"Every supported retail runtime picture is decoded, converted to a ready-to-load\n";
        report << L"six-plane EHB picture and written with its own 128-byte RGB12 palette.\n";
        report << L"Palette indices 1-3 are reserved for the verified Bigfont yellow shades, so\n";
        report << L"Gloom Reforged does not need per-pixel remapping at runtime.\n\n";
        if (!fontSource.empty())
            report << L"Font color source: " << fontSource.wstring() << L"\n";
        else
            report << L"Font color source: verified built-in RGB12 fallback\n";
        if (!fontWarning.empty())
            report << L"[WARN] " << fontWarning << L"\n";
        report << L"\nConverted retail files\n----------------------\n";

        struct ConvertedPicture
        {
            fs::path sourceFile;
            fs::path paletteFile;
            SharedTitleSource source;
            std::wstring targetName;
            std::vector<std::uint8_t> runtimeData;
            std::vector<std::uint8_t> paletteData;
            double rmse = 0.0;
        };

        std::vector<ConvertedPicture> converted;
        for (const fs::path& sourceFile : ListRetailFiles(pictureDir))
        {
            if (!IsRetailPictureCandidate(sourceFile))
                continue;

            const fs::path paletteFile = FindRetailPalette(sourceFile);
            if (paletteFile.empty())
            {
                ++result.warnings;
                report << L"[SKIP] " << sourceFile.filename().wstring()
                    << L": no matching retail palette was found.\n";
                continue;
            }

            SharedTitleSource source;
            std::wstring error;
            if (!LoadRuntimeSource(sourceFile, paletteFile, false, source, error))
            {
                ++result.warnings;
                report << L"[SKIP] " << sourceFile.filename().wstring() << L": " << error << L"\n";
                continue;
            }

            std::array<RgbColor, 32> baseColors{};
            if (!BuildSharedTitlePalette(source, source, fontColors, baseColors, error))
            {
                ++result.warnings;
                report << L"[SKIP] " << sourceFile.filename().wstring()
                    << L": EHB palette generation failed: " << error << L"\n";
                continue;
            }

            EhbImageData ehb;
            if (!MapSharedTitleSource(source, baseColors, ehb, nullptr, error))
            {
                ++result.warnings;
                report << L"[SKIP] " << sourceFile.filename().wstring()
                    << L": EHB remapping failed: " << error << L"\n";
                continue;
            }

            ConvertedPicture item;
            item.sourceFile = sourceFile;
            item.paletteFile = paletteFile;
            item.source = source;
            item.targetName = RetailAssetName(sourceFile);
            item.rmse = ehb.rmse;
            if (!EncodeEhbRuntime(ehb, item.runtimeData, item.paletteData, error))
            {
                ++result.warnings;
                report << L"[SKIP] " << sourceFile.filename().wstring()
                    << L": ECS runtime encoding failed: " << error << L"\n";
                continue;
            }

            const fs::path imageTarget = picturesOutput / item.targetName;
            const fs::path paletteTarget = picturesOutput / (item.targetName + L".pal");
            if (!WriteFile(imageTarget, item.runtimeData) || !WriteFile(paletteTarget, item.paletteData))
            {
                ++result.errors;
                report << L"[ERROR] Cannot write " << item.targetName << L" and its palette.\n";
                continue;
            }

            ++result.generatedPictures;
            ++result.roundtripChecks;
            report << L"[OK] " << sourceFile.filename().wstring() << L" -> " << item.targetName
                << L", " << source.width << L"x" << source.height << L"x6, "
                << item.runtimeData.size() << L" bytes, RMSE " << std::fixed << std::setprecision(2)
                << item.rmse << std::defaultfloat << L"\n";
            converted.push_back(std::move(item));
        }

        // Gloom and Gloom Deluxe draw the retail Gloom strip over the base title.
        // Build the final title here once so the ECS path only loads and displays it.
        // Gloom 3 and Zombie Massacre have no such overlay and keep the direct title export.
        if (composeGloomTitleBrush && !zombieMassacre)
        {
            const fs::path titleFile = FindRetailPicture(pictureDir, L"title");
            fs::path brushFile = FindRetailPicture(pictureDir, L"gloom");
            if (brushFile.empty())
                brushFile = FindRetailPicture(pictureDir, L"gloombrush");

            if (!titleFile.empty() && !brushFile.empty())
            {
                const fs::path titlePalette = FindRetailPalette(titleFile);
                const fs::path brushPalette = FindRetailPalette(brushFile);
                SharedTitleSource title;
                SharedTitleSource brush;
                std::wstring error;
                if (!titlePalette.empty() && !brushPalette.empty() &&
                    LoadRuntimeSource(titleFile, titlePalette, false, title, error) &&
                    LoadRuntimeSource(brushFile, brushPalette, true, brush, error) &&
                    title.width == 320 && brush.width == 320 && brush.height > 0 &&
                    brush.height <= 72 && title.height >= 168 + brush.height)
                {
                    SharedTitleSource composite = title;
                    const size_t opaquePixels = static_cast<size_t>(std::count(
                        brush.opaque.begin(), brush.opaque.end(), static_cast<std::uint8_t>(1)));
                    const bool precompositedStrip = brush.opaque.empty() ||
                        opaquePixels * 100U >= brush.opaque.size() * 95U;

                    for (int y = 0; y < brush.height; ++y)
                    {
                        for (int x = 0; x < brush.width; ++x)
                        {
                            const size_t brushPixel = static_cast<size_t>(y) * static_cast<size_t>(brush.width) +
                                static_cast<size_t>(x);
                            if (!precompositedStrip && !brush.opaque.empty() && brush.opaque[brushPixel] == 0)
                                continue;
                            const size_t titlePixel = static_cast<size_t>(y + 168) * static_cast<size_t>(title.width) +
                                static_cast<size_t>(x);
                            composite.pixels[titlePixel] = brush.pixels[brushPixel];
                        }
                    }
                    composite.sourceKind = L"offline-composited retail title + Gloom strip";

                    std::array<RgbColor, 32> baseColors{};
                    EhbImageData ehb;
                    std::vector<std::uint8_t> runtimeData;
                    std::vector<std::uint8_t> paletteData;
                    if (BuildSharedTitlePalette(composite, composite, fontColors, baseColors, error) &&
                        MapSharedTitleSource(composite, baseColors, ehb, nullptr, error) &&
                        EncodeEhbRuntime(ehb, runtimeData, paletteData, error))
                    {
                        const fs::path finalTitle = picturesOutput / L"title";
                        const fs::path finalPalette = picturesOutput / L"title.pal";
                        const fs::path baseTitle = picturesOutput / L"title_base";
                        const fs::path basePalette = picturesOutput / L"title_base.pal";

                        for (const ConvertedPicture& item : converted)
                        {
                            std::wstring lower = item.targetName;
                            std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t ch) {
                                return static_cast<wchar_t>(std::towlower(ch));
                                });
                            if (lower == L"title")
                            {
                                WriteFile(baseTitle, item.runtimeData);
                                WriteFile(basePalette, item.paletteData);
                                break;
                            }
                        }

                        if (WriteFile(finalTitle, runtimeData) && WriteFile(finalPalette, paletteData))
                        {
                            ++result.roundtripChecks;
                            report << L"\n[OK] title: replaced with an offline-composited final ECS title (retail title + "
                                << brushFile.filename().wstring() << L" at Y=168).\n";
                            report << L"     The uncomposited conversion is retained as title_base/title_base.pal.\n";
                            report << L"     ECS runtime rule: load pics_ehb/title and do not overlay pics/gloom again.\n";
                        }
                        else
                        {
                            ++result.errors;
                            report << L"\n[ERROR] Could not write the offline-composited final title.\n";
                        }
                    }
                    else
                    {
                        ++result.warnings;
                        report << L"\n[WARN] Retail title/brush were found, but offline title composition failed: "
                            << error << L"\n";
                    }
                }
                else
                {
                    ++result.warnings;
                    report << L"\n[WARN] Retail title/brush pair is not suitable for the 320-pixel Gloom title strip; "
                        << L"the directly converted title remains active.\n";
                    if (!error.empty())
                        report << L"       " << error << L"\n";
                }
            }
        }

        report << L"\nRuntime contract\n----------------\n";
        report << L"- Load pictures from " << picturesOutput.filename().wstring() << L" before falling back to the retail folder.\n";
        report << L"- Each generated picture is already six-plane EHB and has a matching 128-byte palette.\n";
        report << L"- No runtime quantization, RGB search or 8-to-6-bit remap is required.\n";
        report << L"- For Gloom/Gloom Deluxe, generated title already contains the retail Gloom strip.\n";
        report << L"- Gloom 3 and Zombie Massacre use their converted retail title directly.\n";

        result.reportFile = outputRoot / L"retail_ecs_picture_report.txt";
        std::wofstream reportOutput(result.reportFile, std::ios::trunc);
        if (reportOutput)
            reportOutput << report.str();
        else
            ++result.errors;

        result.success = result.errors == 0 && result.generatedPictures > 0;
        const fs::path titleOutput = picturesOutput / L"title";
        if (Exists(titleOutput))
        {
            result.outputImage = titleOutput;
            result.outputPalette = picturesOutput / L"title.pal";
        }

        std::wstringstream message;
        message << (result.success ? L"Retail ECS picture package created." :
            L"Retail ECS picture package completed with errors.") << L"\n\n";
        message << L"Generated pictures: " << result.generatedPictures << L"\n";
        message << L"Roundtrip checks: " << result.roundtripChecks << L"\n";
        message << L"Warnings/skipped files: " << result.warnings << L"\n";
        message << L"Errors: " << result.errors << L"\n\n";
        message << L"Output: " << picturesOutput.wstring() << L"\n\n";
        message << L"Report: " << result.reportFile.wstring();
        result.message = message.str();
        return result;
    }

    Result ExportFastTitlePackage(const fs::path& gameRoot, const fs::path& titleSource,
        const fs::path& brushSource, const fs::path& outputRoot)
    {
        Result result;
        if (gameRoot.empty() || !Exists(gameRoot))
        {
            result.errors = 1;
            result.message = L"The selected game/source root does not exist.";
            return result;
        }
        if (outputRoot.empty())
        {
            result.errors = 1;
            result.message = L"No Fast ECS title output folder was selected.";
            return result;
        }

        SharedTitleSource title;
        SharedTitleSource brush;
        fs::path titlePalettePath;
        std::wstring error;
        if (!LoadFastTitleInput(gameRoot, titleSource, title, titlePalettePath, error))
        {
            result.errors = 1;
            result.message = L"Fast ECS title export could not load the title:\n\n" + error;
            return result;
        }
        if (!LoadFastBrushInput(gameRoot, titlePalettePath, brushSource, brush, error))
        {
            result.errors = 1;
            result.message = L"Fast ECS title export could not load the Gloom brush:\n\n" + error;
            return result;
        }

        if (title.width != 320 || (title.height != 240 && title.height != 256))
        {
            std::wstringstream message;
            message << L"Fast ECS title requires a 320x240 or 320x256 title source.\n\n"
                << L"Detected: " << title.width << L"x" << title.height << L"\n"
                << L"Source: " << title.sourcePath.wstring();
            result.errors = 1;
            result.message = message.str();
            return result;
        }
        if (brush.width != 320 || brush.height <= 0 || brush.height > 72)
        {
            std::wstringstream message;
            message << L"Fast ECS brush requires a 320-pixel-wide source with a maximum height of 72 pixels.\n\n"
                << L"Detected: " << brush.width << L"x" << brush.height << L"\n"
                << L"Source: " << brush.sourcePath.wstring();
            result.errors = 1;
            result.message = message.str();
            return result;
        }

        bool fontFallback = false;
        fs::path fontSource;
        std::wstring fontWarning;
        const std::array<RgbColor, 3> fontColors =
            LoadFastTitleFontColors(gameRoot, fontFallback, fontSource, fontWarning);
        if (fontFallback)
            ++result.warnings;

        std::array<RgbColor, 32> sharedBaseColors{};
        if (!BuildSharedTitlePalette(title, brush, fontColors, sharedBaseColors, error))
        {
            result.errors = 1;
            result.message = L"Could not create the shared title/brush/font EHB palette:\n\n" + error;
            return result;
        }

        EhbImageData titleEhb;
        EhbImageData brushEhb;
        std::vector<std::uint8_t> brushMask;
        if (!MapSharedTitleSource(title, sharedBaseColors, titleEhb, nullptr, error) ||
            !MapSharedTitleSource(brush, sharedBaseColors, brushEhb, &brushMask, error))
        {
            result.errors = 1;
            result.message = L"Could not remap the title package to the shared EHB palette:\n\n" + error;
            return result;
        }

        std::vector<std::uint8_t> titleRuntime;
        std::vector<std::uint8_t> titlePalette;
        std::vector<std::uint8_t> brushRuntime;
        std::vector<std::uint8_t> brushPaletteCheck;
        std::vector<std::uint8_t> maskRuntime;
        if (!EncodeEhbRuntime(titleEhb, titleRuntime, titlePalette, error) ||
            !EncodeEhbRuntime(brushEhb, brushRuntime, brushPaletteCheck, error) ||
            !EncodeMaskRuntime(brushEhb.width, brushEhb.height, brushMask, maskRuntime, error))
        {
            result.errors = 1;
            result.message = L"Could not encode the Fast ECS title package:\n\n" + error;
            return result;
        }
        if (brushPaletteCheck != titlePalette)
        {
            result.errors = 1;
            result.message = L"Internal error: title and brush did not receive the identical EHB palette.";
            return result;
        }

        const auto reservedUsed = [](const std::vector<std::uint8_t>& pixels) {
            return static_cast<size_t>(std::count_if(pixels.begin(), pixels.end(), [](std::uint8_t index) {
                return (index >= 1 && index <= 3) || (index >= 33 && index <= 35);
                }));
        };
        const size_t titleReservedPixels = reservedUsed(titleEhb.pixels);
        const size_t brushReservedPixels = reservedUsed(brushEhb.pixels);
        if (titleReservedPixels != 0 || brushReservedPixels != 0)
        {
            result.errors = 1;
            result.message = L"Internal error: title or brush pixels use font-reserved EHB indices.";
            return result;
        }

        const size_t opaqueBrushPixels = static_cast<size_t>(
            std::count(brushMask.begin(), brushMask.end(), static_cast<std::uint8_t>(1)));
        if (opaqueBrushPixels == 0)
        {
            result.errors = 1;
            result.message = L"The selected brush contains no opaque pixels.";
            return result;
        }
        if (opaqueBrushPixels * 100U >= brushMask.size() * 95U)
        {
            result.errors = 1;
            result.message =
                L"The selected brush is almost fully opaque and has no usable cookie-cut transparency.\n\n"
                L"Select assets_source/iff/gloom.iff.  It contains only the logo and uses transparent colour index 0.\n"
                L"Do not select gloombrush.iff or a precomposited runtime brush.";
            return result;
        }

        const fs::path picturesOutput = outputRoot / L"pics_ehb";
        std::error_code ec;
        fs::create_directories(picturesOutput, ec);
        if (ec)
        {
            result.errors = 1;
            result.message = L"Cannot create the output folder:\n" + picturesOutput.wstring();
            return result;
        }

        const fs::path titleTarget = picturesOutput / L"title";
        const fs::path paletteTarget = picturesOutput / L"title.pal";
        const fs::path brushTarget = picturesOutput / L"gloom";
        const fs::path maskTarget = picturesOutput / L"gloom.mask";
        if (!WriteFile(titleTarget, titleRuntime) ||
            !WriteFile(paletteTarget, titlePalette) ||
            !WriteFile(brushTarget, brushRuntime) ||
            !WriteFile(maskTarget, maskRuntime))
        {
            result.errors = 1;
            result.message = L"Could not write all Fast ECS title files.";
            return result;
        }

        std::wstringstream report;
        report << L"ZGloomEditor 1.2.0 - Fast ECS title package\n";
        report << L"Source root: " << gameRoot.wstring() << L"\n";
        report << L"Selected title source: " << titleSource.wstring() << L"\n";
        report << L"Output root: " << outputRoot.wstring() << L"\n\n";
        report << L"Purpose\n";
        report << L"-------\n";
        report << L"Title, Gloom brush and Bigfont yellow shades use one shared EHB palette.\n";
        report << L"The brush is already remapped and has a separate one-plane cookie-cut mask.\n";
        report << L"No runtime per-pixel color search is required.\n\n";

        report << L"Sources\n";
        report << L"-------\n";
        report << L"Title: " << title.sourcePath.wstring() << L" (" << title.sourceKind
            << L", " << title.width << L"x" << title.height << L", " << title.sourceDepth << L" planes)\n";
        report << L"Brush: " << brush.sourcePath.wstring() << L" (" << brush.sourceKind
            << L", " << brush.width << L"x" << brush.height << L", " << brush.sourceDepth << L" planes)\n";
        if (!fontSource.empty())
            report << L"Font colors: " << fontSource.wstring() << L"\n";
        else
            report << L"Font colors: verified built-in public-source RGB12 fallback\n";
        if (!fontWarning.empty())
            report << L"Warning: " << fontWarning << L"\n";
        report << L"\n";

        report << L"Palette contract\n";
        report << L"----------------\n";
        report << L"Index 0: black / transparent brush index\n";
        report << L"Indices 1-3: reserved Bigfont yellow shades\n";
        report << L"Indices 33-35: automatic EHB Half-Brite font shades\n";
        report << L"Indices 4-31 and 36-63: title and brush colors\n";
        report << L"Title pixels using reserved font indices: " << titleReservedPixels << L"\n";
        report << L"Brush pixels using reserved font indices: " << brushReservedPixels << L"\n";
        for (size_t index = 0; index < 4; ++index)
        {
            const std::uint16_t word = EncodeRgb12Word(sharedBaseColors[index]);
            report << L"Base index " << index << L": $" << std::uppercase << std::hex
                << std::setw(3) << std::setfill(L'0') << word << std::dec
                << std::nouppercase << std::setfill(L' ') << L"\n";
        }
        report << L"\n";

        report << L"Generated files\n";
        report << L"---------------\n";
        report << L"pics_ehb/title: " << titleRuntime.size() << L" bytes, "
            << titleEhb.width << L"x" << titleEhb.height << L"x6, RMSE "
            << std::fixed << std::setprecision(2) << titleEhb.rmse << L"\n";
        report << L"pics_ehb/title.pal: " << titlePalette.size()
            << L" bytes, 32 base + 32 Half-Brite colors\n";
        report << L"pics_ehb/gloom: " << brushRuntime.size() << L" bytes, "
            << brushEhb.width << L"x" << brushEhb.height << L"x6, RMSE "
            << std::fixed << std::setprecision(2) << brushEhb.rmse << L"\n";
        report << L"pics_ehb/gloom.mask: " << maskRuntime.size()
            << L" bytes, " << brushEhb.width << L"x" << brushEhb.height
            << L"x1 trimmed/ByteRun1 mask\n";
        report << L"Opaque brush pixels: " << opaqueBrushPixels << L"\n";
        report << L"Expected Gloom/Deluxe brush Y position: 168\n\n";

        report << L"Runtime integration\n";
        report << L"-------------------\n";
        report << L"- Load pics_ehb/title and pics_ehb/title.pal normally.\n";
        report << L"- Decode pics_ehb/gloom once into a six-plane brush buffer.\n";
        report << L"- Decode pics_ehb/gloom.mask once into a one-plane mask buffer.\n";
        report << L"- Cookie-cut the brush into each of the six destination planes with the Amiga blitter.\n";
        report << L"- ABOUT restores only the 320x" << brushEhb.height
            << L" background area; the full title cache is not required.\n";
        report << L"- Current older gloom2.s builds do not use gloom.mask automatically. "
            << L"They require the matching Fast ECS title-blitter patch.\n";

        result.reportFile = outputRoot / L"fast_ecs_title_report.txt";
        std::wofstream reportOutput(result.reportFile, std::ios::trunc);
        if (!reportOutput)
        {
            result.errors = 1;
            result.message = L"Files were generated, but fast_ecs_title_report.txt could not be written.";
            return result;
        }
        reportOutput << report.str();

        result.success = true;
        result.generatedPictures = 2;
        result.roundtripChecks = 3;
        result.outputImage = titleTarget;
        result.outputPalette = paletteTarget;

        std::wstringstream message;
        message << L"Fast ECS title package created.\n\n";
        message << L"Generated:\n";
        message << L"pics_ehb/title\n";
        message << L"pics_ehb/title.pal\n";
        message << L"pics_ehb/gloom\n";
        message << L"pics_ehb/gloom.mask\n\n";
        message << L"Title RMSE: " << std::fixed << std::setprecision(2) << titleEhb.rmse << L"\n";
        message << L"Brush RMSE: " << std::fixed << std::setprecision(2) << brushEhb.rmse << L"\n";
        message << L"Warnings: " << result.warnings << L"\n\n";
        message << L"Important: these files need the matching Fast ECS title-blitter patch in gloom2.s.\n\n";
        message << L"Report:\n" << result.reportFile.wstring();
        result.message = message.str();
        return result;
    }

    Result ConvertImageFile(const fs::path& sourceFile, const fs::path& outputFolder)
    {
        Result result;
        if (!Exists(sourceFile))
        {
            result.errors = 1;
            result.message = L"The selected PNG/ILBM file does not exist.";
            return result;
        }
        if (outputFolder.empty())
        {
            result.errors = 1;
            result.message = L"No ECS/EHB output folder was selected.";
            return result;
        }

        EhbImageData ehbImage;
        std::wstring conversionError;
        std::wstring sourceDescription;
        int sourceDepth = 0;

        std::vector<std::uint8_t> header;
        const bool readable = ReadFile(sourceFile, header);
        const bool isIlbm = readable && header.size() >= 12 &&
            std::string(reinterpret_cast<const char*>(header.data()), 4) == "FORM" &&
            std::string(reinterpret_cast<const char*>(header.data() + 8), 4) == "ILBM";

        if (isIlbm)
        {
            IlbmImage image;
            if (!DecodeIlbm(sourceFile, image, conversionError) ||
                !PrepareEhbFromIlbm(image, ehbImage, conversionError))
            {
                result.errors = 1;
                result.message = L"ILBM to ECS/EHB conversion failed:\n\n" + conversionError;
                return result;
            }
            sourceDepth = image.depth;
            sourceDescription = L"ILBM";
        }
        else if (IsPngFile(sourceFile))
        {
            int width = 0;
            int height = 0;
            std::vector<RgbColor> pixels;
            if (!LoadPngRgb(sourceFile, width, height, pixels, conversionError) ||
                !QuantizeRgbToEhb(pixels, width, height, ehbImage, conversionError))
            {
                result.errors = 1;
                result.message = L"PNG to ECS/EHB conversion failed:\n\n" + conversionError;
                return result;
            }
            sourceDescription = L"PNG";
        }
        else
        {
            result.errors = 1;
            result.message =
                L"Unsupported input file.\n\n"
                L"This converter accepts PNG images and complete FORM/ILBM files. "
                L"Gloom trimmed runtime pictures are intentionally not guessed because their palette/index layouts vary.";
            return result;
        }

        std::vector<std::uint8_t> runtimeData;
        std::vector<std::uint8_t> paletteData;
        if (!EncodeEhbRuntime(ehbImage, runtimeData, paletteData, conversionError))
        {
            result.errors = 1;
            result.message = L"Could not create the Gloom ECS/EHB runtime files:\n\n" + conversionError;
            return result;
        }

        std::error_code ec;
        fs::create_directories(outputFolder, ec);
        if (ec)
        {
            result.errors = 1;
            result.message = L"Could not create the selected output folder.";
            return result;
        }

        std::wstring targetName = sourceFile.has_extension()
            ? sourceFile.stem().wstring()
            : sourceFile.filename().wstring();
        if (targetName.empty())
            targetName = L"picture_ehb";

        fs::path imageTarget = outputFolder / targetName;
        ec.clear();
        if (Exists(imageTarget) && fs::equivalent(sourceFile, imageTarget, ec) && !ec)
        {
            targetName += L"_ehb";
            imageTarget = outputFolder / targetName;
        }
        const fs::path paletteTarget = outputFolder / (targetName + L".pal");

        if (!WriteFile(imageTarget, runtimeData) || !WriteFile(paletteTarget, paletteData))
        {
            result.errors = 1;
            result.message = L"Could not write the ECS/EHB image or palette.";
            return result;
        }

        result.success = true;
        result.generatedPictures = 1;
        result.roundtripChecks = 1;
        result.outputImage = imageTarget;
        result.outputPalette = paletteTarget;

        std::wstringstream message;
        message << sourceDescription << L" converted to Amiga ECS/EHB.\n\n";
        if (sourceDepth > 0)
            message << L"Source depth: " << sourceDepth << L" planes\n";
        message << L"Output: " << ehbImage.width << L"x" << ehbImage.height << L", 6 planes\n";
        message << L"Palette: 32 base colors + 32 Half-Brite colors\n";
        if (ehbImage.directEhb)
            message << L"Mode: original EHB indices preserved\n";
        else
            message << L"Mode: EHB-aware quantization, no dithering, RMSE "
                << std::fixed << std::setprecision(2) << ehbImage.rmse << L"\n";
        message << L"\nImage:\n" << imageTarget.wstring();
        message << L"\n\nPalette:\n" << paletteTarget.wstring();
        result.message = message.str();
        return result;
    }
}
