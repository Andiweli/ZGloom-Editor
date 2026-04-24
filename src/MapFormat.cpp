#include "MapFormat.h"

#include "decrunchmania.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace
{
    uint16_t ReadBE16(const uint8_t* p)
    {
        return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));
    }

    uint32_t ReadBE32(const uint8_t* p)
    {
        return (static_cast<uint32_t>(p[0]) << 24) |
               (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8) |
               static_cast<uint32_t>(p[3]);
    }

    void WriteBE16(std::vector<uint8_t>& out, uint16_t value)
    {
        out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(value & 0xFF));
    }

    void WriteBE32(std::vector<uint8_t>& out, uint32_t value)
    {
        out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(value & 0xFF));
    }

    void WriteBE16At(std::vector<uint8_t>& out, size_t offset, uint16_t value)
    {
        out[offset + 0] = static_cast<uint8_t>((value >> 8) & 0xFF);
        out[offset + 1] = static_cast<uint8_t>(value & 0xFF);
    }

    void WriteBE32At(std::vector<uint8_t>& out, size_t offset, uint32_t value)
    {
        out[offset + 0] = static_cast<uint8_t>((value >> 24) & 0xFF);
        out[offset + 1] = static_cast<uint8_t>((value >> 16) & 0xFF);
        out[offset + 2] = static_cast<uint8_t>((value >> 8) & 0xFF);
        out[offset + 3] = static_cast<uint8_t>(value & 0xFF);
    }

    bool ReadFileBinary(const std::string& path, std::vector<uint8_t>& data, std::string& errorMessage)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            errorMessage = "Could not open file.";
            return false;
        }

        file.seekg(0, std::ios::end);
        const auto size = file.tellg();
        file.seekg(0, std::ios::beg);

        if (size <= 0)
        {
            errorMessage = "The file is empty.";
            return false;
        }

        data.resize(static_cast<size_t>(size));
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!file)
        {
            errorMessage = "Failed to read the file.";
            return false;
        }

        return true;
    }

    bool LoadMaybeCrm(const std::string& path, std::vector<uint8_t>& out, std::string& errorMessage)
    {
        std::vector<uint8_t> fileData;
        if (!ReadFileBinary(path, fileData, errorMessage))
        {
            return false;
        }

        if (GetSize(fileData.data()) == 0)
        {
            out = std::move(fileData);
            return true;
        }

        const unsigned int unpackedSize = GetSize(fileData.data());
        const unsigned int headroom = GetSecDist(fileData.data());
        std::vector<uint8_t> work(unpackedSize + headroom + 32, 0);
        std::vector<uint8_t> unpacked(unpackedSize, 0);

        std::copy(fileData.begin(), fileData.end(), work.begin());
        if (Decrunch(work.data(), unpacked.data()) == nullptr)
        {
            errorMessage = "CrM2 decompression failed.";
            return false;
        }

        out = std::move(unpacked);
        return true;
    }

    int ClampCell(int value)
    {
        if (value < 0) return 0;
        if (value > 31) return 31;
        return value;
    }

    int WorldToCell(int value)
    {
        return ClampCell(value / 1024);
    }

    std::string EscapeXml(const std::string& value)
    {
        std::string out;
        out.reserve(value.size() + 16);
        for (char ch : value)
        {
            switch (ch)
            {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out.push_back(ch); break;
            }
        }
        return out;
    }
}

namespace mapfmt
{
    std::string EventCommand::ToDisplayString() const
    {
        std::ostringstream os;
        switch (type)
        {
        case CommandType::AddMonster:
            os << "Add Monster  type=" << params[0]
               << " x=" << params[1]
               << " y=" << params[2]
               << " z=" << params[3]
               << " rot=" << params[4];
            break;
        case CommandType::OpenDoor:
            os << "Open Door  zone=" << params[0];
            break;
        case CommandType::Teleport:
            os << "Teleport  x=" << params[0]
               << " y=" << params[1]
               << " z=" << params[2]
               << " rot=" << params[3];
            break;
        case CommandType::LoadObjects:
            os << "Load Objects  ids=";
            for (size_t i = 0; i < listValues.size(); ++i)
            {
                if (i) os << ",";
                os << listValues[i];
            }
            break;
        case CommandType::ChangeTexture:
            os << "Change Texture  zone=" << params[0]
               << " newTexture=" << params[1];
            break;
        case CommandType::RotatePoly:
            os << "Rotate Poly  poly=" << params[0]
               << " count=" << params[1]
               << " speed=" << params[2]
               << " flags=" << params[3];
            break;
        default:
            os << "Unknown";
            break;
        }
        return os.str();
    }

    void MapDocument::NewBlank()
    {
        zones.clear();
        for (auto& name : textureNames)
        {
            name.clear();
        }
        for (auto& script : events)
        {
            script.commands.clear();
        }
        animationBlock.clear();
        sourcePath.clear();
        dirty = false;
    }

    bool MapDocument::LoadFromFile(const std::string& path, std::string& errorMessage)
    {
        std::vector<uint8_t> raw;
        if (!LoadMaybeCrm(path, raw, errorMessage))
        {
            return false;
        }

        if (raw.size() < 20 + (kEventCount * 4))
        {
            errorMessage = "Map file is too small to contain a valid header.";
            return false;
        }

        const uint32_t gridoff = ReadBE32(raw.data() + 0);
        const uint32_t polyoff = ReadBE32(raw.data() + 4);
        const uint32_t polypnt = ReadBE32(raw.data() + 8);
        const uint32_t animpnt = ReadBE32(raw.data() + 12);
        const uint32_t txtnames = ReadBE32(raw.data() + 16);

        if (gridoff >= raw.size() || polyoff >= raw.size() || polypnt > raw.size() || txtnames >= raw.size())
        {
            errorMessage = "One or more map section offsets are outside the file.";
            return false;
        }

        if (polyoff < gridoff || polypnt < polyoff)
        {
            errorMessage = "Map section offsets are inconsistent.";
            return false;
        }

        std::array<uint32_t, kEventCount> eventPointers{};
        for (int i = 0; i < kEventCount; ++i)
        {
            eventPointers[i] = ReadBE32(raw.data() + 20 + (i * 4));
            if (eventPointers[i] >= raw.size())
            {
                errorMessage = "An event pointer is outside the file.";
                return false;
            }
        }

        zones.clear();
        const size_t zoneBytes = polypnt - polyoff;
        if ((zoneBytes % kZoneSize) != 0)
        {
            errorMessage = "Zone block size is not divisible by 32 bytes.";
            return false;
        }

        const size_t zoneCount = zoneBytes / kZoneSize;
        zones.reserve(zoneCount);
        for (size_t i = 0; i < zoneCount; ++i)
        {
            const uint8_t* z = raw.data() + polyoff + (i * kZoneSize);
            Zone zone;
            zone.ztype = static_cast<int16_t>(ReadBE16(z + 0));
            zone.x1 = static_cast<int16_t>(ReadBE16(z + 2));
            zone.z1 = static_cast<int16_t>(ReadBE16(z + 4));
            zone.x2 = static_cast<int16_t>(ReadBE16(z + 6));
            zone.z2 = static_cast<int16_t>(ReadBE16(z + 8));
            zone.a = static_cast<int16_t>(ReadBE16(z + 10));
            zone.b = static_cast<int16_t>(ReadBE16(z + 12));
            zone.na = static_cast<int16_t>(ReadBE16(z + 14));
            zone.nb = static_cast<int16_t>(ReadBE16(z + 16));
            zone.ln = static_cast<int16_t>(ReadBE16(z + 18));
            for (int t = 0; t < kTextureSlotCount; ++t)
            {
                zone.textures[t] = z[20 + t];
            }
            zone.sc = static_cast<int16_t>(ReadBE16(z + 28));
            zone.ev = static_cast<int16_t>(ReadBE16(z + 30));
            zones.push_back(zone);
        }

        for (auto& name : textureNames)
        {
            name.clear();
        }

        size_t namePos = txtnames;
        for (int i = 0; i < kTextureSlotCount; ++i)
        {
            while (namePos < raw.size() && raw[namePos] != 0)
            {
                textureNames[i].push_back(static_cast<char>(raw[namePos]));
                ++namePos;
            }
            if (namePos >= raw.size())
            {
                errorMessage = "Texture name block ran past end of file.";
                return false;
            }
            ++namePos;
        }

        animationBlock.clear();
        if (animpnt != 0)
        {
            if (animpnt >= raw.size())
            {
                errorMessage = "Animation block offset is outside the file.";
                return false;
            }

            size_t animPos = animpnt;
            while ((animPos + 8) <= raw.size())
            {
                const uint16_t frames = ReadBE16(raw.data() + animPos);
                for (size_t i = 0; i < 8; ++i)
                {
                    animationBlock.push_back(raw[animPos + i]);
                }
                animPos += 8;
                if (frames == 0)
                {
                    break;
                }
            }
        }

        for (auto& script : events)
        {
            script.commands.clear();
        }

        for (int evIndex = 0; evIndex < kEventCount; ++evIndex)
        {
            size_t pos = eventPointers[evIndex];
            while ((pos + 2) <= raw.size())
            {
                const auto op = static_cast<CommandType>(static_cast<int16_t>(ReadBE16(raw.data() + pos)));
                pos += 2;
                if (op == CommandType::End)
                {
                    break;
                }

                EventCommand cmd;
                cmd.type = op;

                switch (op)
                {
                case CommandType::AddMonster:
                    if ((pos + 10) > raw.size())
                    {
                        errorMessage = "Add Monster command is truncated.";
                        return false;
                    }
                    for (int i = 0; i < 5; ++i)
                    {
                        cmd.params[i] = static_cast<int16_t>(ReadBE16(raw.data() + pos));
                        pos += 2;
                    }
                    break;
                case CommandType::OpenDoor:
                    if ((pos + 2) > raw.size())
                    {
                        errorMessage = "Open Door command is truncated.";
                        return false;
                    }
                    cmd.params[0] = static_cast<int16_t>(ReadBE16(raw.data() + pos));
                    pos += 2;
                    break;
                case CommandType::Teleport:
                    if ((pos + 8) > raw.size())
                    {
                        errorMessage = "Teleport command is truncated.";
                        return false;
                    }
                    for (int i = 0; i < 4; ++i)
                    {
                        cmd.params[i] = static_cast<int16_t>(ReadBE16(raw.data() + pos));
                        pos += 2;
                    }
                    break;
                case CommandType::LoadObjects:
                    while ((pos + 2) <= raw.size())
                    {
                        const int16_t value = static_cast<int16_t>(ReadBE16(raw.data() + pos));
                        pos += 2;
                        if (value < 0)
                        {
                            break;
                        }
                        cmd.listValues.push_back(value);
                    }
                    break;
                case CommandType::ChangeTexture:
                    if ((pos + 4) > raw.size())
                    {
                        errorMessage = "Change Texture command is truncated.";
                        return false;
                    }
                    cmd.params[0] = static_cast<int16_t>(ReadBE16(raw.data() + pos)); pos += 2;
                    cmd.params[1] = static_cast<int16_t>(ReadBE16(raw.data() + pos)); pos += 2;
                    break;
                case CommandType::RotatePoly:
                    if ((pos + 8) > raw.size())
                    {
                        errorMessage = "Rotate Poly command is truncated.";
                        return false;
                    }
                    for (int i = 0; i < 4; ++i)
                    {
                        cmd.params[i] = static_cast<int16_t>(ReadBE16(raw.data() + pos));
                        pos += 2;
                    }
                    break;
                default:
                    errorMessage = "Unsupported event opcode encountered while loading.";
                    return false;
                }

                events[evIndex].commands.push_back(cmd);
            }
        }

        sourcePath = path;
        dirty = false;
        return true;
    }

    bool MapDocument::SaveToFile(const std::string& path, std::string& errorMessage) const
    {
        constexpr uint32_t headerSize = 20 + (kEventCount * 4);
        constexpr uint32_t gridBytes = kGridSize * kGridSize * 8;

        struct GridCell
        {
            std::vector<uint16_t> zoneIndices;
            uint16_t polyOffset = 0;
        };

        GridCell cells[2][kGridSize][kGridSize];
        std::vector<uint16_t> polyList;
        polyList.reserve(zones.size() * 16);

        for (size_t zoneIndex = 0; zoneIndex < zones.size(); ++zoneIndex)
        {
            const Zone& zone = zones[zoneIndex];
            const int xMin = std::min<int>(zone.x1, zone.x2);
            const int xMax = std::max<int>(zone.x1, zone.x2);
            const int zMin = std::min<int>(zone.z1, zone.z2);
            const int zMax = std::max<int>(zone.z1, zone.z2);

            const int cellX1 = WorldToCell(xMin);
            const int cellX2 = WorldToCell(xMax);
            const int cellZ1 = WorldToCell(zMin);
            const int cellZ2 = WorldToCell(zMax);

            int plane = -1;
            if (zone.ztype == static_cast<int16_t>(ZoneType::Wall))
            {
                plane = 0;
            }
            else if (zone.ztype == static_cast<int16_t>(ZoneType::EventTrigger))
            {
                plane = 1;
            }

            if (plane >= 0)
            {
                for (int cz = cellZ1; cz <= cellZ2; ++cz)
                {
                    for (int cx = cellX1; cx <= cellX2; ++cx)
                    {
                        cells[plane][cx][cz].zoneIndices.push_back(static_cast<uint16_t>(zoneIndex));
                    }
                }
            }
        }

        std::vector<uint8_t> out;
        out.resize(headerSize, 0);

        const uint32_t gridoff = headerSize;
        const uint32_t polyoff = gridoff + gridBytes;
        const uint32_t polypnt = polyoff + static_cast<uint32_t>(zones.size() * kZoneSize);

        WriteBE32At(out, 0, gridoff);
        WriteBE32At(out, 4, polyoff);
        WriteBE32At(out, 8, polypnt);

        out.resize(polyoff, 0);

        for (const Zone& zone : zones)
        {
            WriteBE16(out, static_cast<uint16_t>(zone.ztype));
            WriteBE16(out, static_cast<uint16_t>(zone.x1));
            WriteBE16(out, static_cast<uint16_t>(zone.z1));
            WriteBE16(out, static_cast<uint16_t>(zone.x2));
            WriteBE16(out, static_cast<uint16_t>(zone.z2));
            WriteBE16(out, static_cast<uint16_t>(zone.a));
            WriteBE16(out, static_cast<uint16_t>(zone.b));
            WriteBE16(out, static_cast<uint16_t>(zone.na));
            WriteBE16(out, static_cast<uint16_t>(zone.nb));
            WriteBE16(out, static_cast<uint16_t>(zone.ln));
            for (uint8_t tex : zone.textures)
            {
                out.push_back(tex);
            }
            WriteBE16(out, static_cast<uint16_t>(zone.sc));
            WriteBE16(out, static_cast<uint16_t>(zone.ev));
        }

        for (int plane = 0; plane < 2; ++plane)
        {
            for (int z = 0; z < kGridSize; ++z)
            {
                for (int x = 0; x < kGridSize; ++x)
                {
                    auto& cell = cells[plane][x][z];
                    cell.polyOffset = static_cast<uint16_t>(polyList.size());
                    polyList.insert(polyList.end(), cell.zoneIndices.begin(), cell.zoneIndices.end());
                }
            }
        }

        out.resize(polypnt, 0);
        const size_t gridStart = gridoff;
        size_t gridCursor = gridStart;

        for (int z = 0; z < kGridSize; ++z)
        {
            for (int x = 0; x < kGridSize; ++x)
            {
                for (int plane = 0; plane < 2; ++plane)
                {
                    const auto& cell = cells[plane][x][z];
                    const uint16_t storedCount = cell.zoneIndices.empty()
                        ? 0xFFFF
                        : static_cast<uint16_t>(cell.zoneIndices.size() - 1);
                    WriteBE16At(out, gridCursor, storedCount);
                    WriteBE16At(out, gridCursor + 2, cell.polyOffset);
                    gridCursor += 4;
                }
            }
        }

        for (uint16_t value : polyList)
        {
            WriteBE16(out, value);
        }

        uint32_t animpnt = 0;
        if (!animationBlock.empty())
        {
            animpnt = static_cast<uint32_t>(out.size());
            out.insert(out.end(), animationBlock.begin(), animationBlock.end());
        }
        WriteBE32At(out, 12, animpnt);

        const uint32_t txtnames = static_cast<uint32_t>(out.size());
        WriteBE32At(out, 16, txtnames);
        for (const std::string& name : textureNames)
        {
            out.insert(out.end(), name.begin(), name.end());
            out.push_back(0);
        }

        std::array<uint32_t, kEventCount> eventOffsets{};
        for (int eventIndex = 0; eventIndex < kEventCount; ++eventIndex)
        {
            eventOffsets[eventIndex] = static_cast<uint32_t>(out.size());
            const auto& commands = events[eventIndex].commands;
            for (const auto& cmd : commands)
            {
                WriteBE16(out, static_cast<uint16_t>(cmd.type));
                switch (cmd.type)
                {
                case CommandType::AddMonster:
                    for (int i = 0; i < 5; ++i) WriteBE16(out, static_cast<uint16_t>(cmd.params[i]));
                    break;
                case CommandType::OpenDoor:
                    WriteBE16(out, static_cast<uint16_t>(cmd.params[0]));
                    break;
                case CommandType::Teleport:
                    for (int i = 0; i < 4; ++i) WriteBE16(out, static_cast<uint16_t>(cmd.params[i]));
                    break;
                case CommandType::LoadObjects:
                    for (int16_t value : cmd.listValues) WriteBE16(out, static_cast<uint16_t>(value));
                    WriteBE16(out, 0xFFFF);
                    break;
                case CommandType::ChangeTexture:
                    WriteBE16(out, static_cast<uint16_t>(cmd.params[0]));
                    WriteBE16(out, static_cast<uint16_t>(cmd.params[1]));
                    break;
                case CommandType::RotatePoly:
                    for (int i = 0; i < 4; ++i) WriteBE16(out, static_cast<uint16_t>(cmd.params[i]));
                    break;
                default:
                    errorMessage = "An unsupported event command exists in the current document.";
                    return false;
                }
            }
            WriteBE16(out, 0);
            WriteBE32At(out, 20 + (eventIndex * 4), eventOffsets[eventIndex]);
        }

        std::ofstream file(path, std::ios::binary);
        if (!file)
        {
            errorMessage = "Could not create output file.";
            return false;
        }

        file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
        if (!file)
        {
            errorMessage = "Failed while writing the output file.";
            return false;
        }

        return true;
    }

    std::vector<std::string> MapDocument::Validate() const
    {
        std::vector<std::string> issues;

        for (size_t i = 0; i < zones.size(); ++i)
        {
            const Zone& zone = zones[i];
            if (zone.ztype < 1 || zone.ztype > 3)
            {
                issues.push_back("Zone " + std::to_string(i) + " uses an invalid zone type.");
            }
            if (zone.ev < 0 || zone.ev > kEventCount)
            {
                issues.push_back("Zone " + std::to_string(i) + " references an event outside 0..24.");
            }
            for (size_t t = 0; t < zone.textures.size(); ++t)
            {
                if (zone.textures[t] > 159)
                {
                    issues.push_back("Zone " + std::to_string(i) + " texture slot T" + std::to_string(t) + " is above 159.");
                }
            }
        }

        for (size_t ev = 0; ev < events.size(); ++ev)
        {
            for (size_t cmdIndex = 0; cmdIndex < events[ev].commands.size(); ++cmdIndex)
            {
                const auto& cmd = events[ev].commands[cmdIndex];
                if (cmd.type == CommandType::OpenDoor || cmd.type == CommandType::ChangeTexture || cmd.type == CommandType::RotatePoly)
                {
                    const int zoneIndex = cmd.params[0];
                    if (zoneIndex < 0 || zoneIndex >= static_cast<int>(zones.size()))
                    {
                        issues.push_back("Event " + std::to_string(ev + 1) + ", command " + std::to_string(cmdIndex + 1) + " references a zone index outside the map.");
                    }
                }
            }
        }

        if (issues.empty())
        {
            issues.push_back("No issues found.");
        }

        return issues;
    }

    Bounds MapDocument::ComputeBounds() const
    {
        Bounds bounds;
        for (const Zone& zone : zones)
        {
            const int32_t xMin = std::min<int32_t>(zone.x1, zone.x2);
            const int32_t xMax = std::max<int32_t>(zone.x1, zone.x2);
            const int32_t zMin = std::min<int32_t>(zone.z1, zone.z2);
            const int32_t zMax = std::max<int32_t>(zone.z1, zone.z2);

            if (!bounds.valid)
            {
                bounds.minX = xMin;
                bounds.maxX = xMax;
                bounds.minZ = zMin;
                bounds.maxZ = zMax;
                bounds.valid = true;
            }
            else
            {
                bounds.minX = std::min(bounds.minX, xMin);
                bounds.maxX = std::max(bounds.maxX, xMax);
                bounds.minZ = std::min(bounds.minZ, zMin);
                bounds.maxZ = std::max(bounds.maxZ, zMax);
            }
        }

        if (!bounds.valid)
        {
            bounds.minX = 0;
            bounds.minZ = 0;
            bounds.maxX = 1024;
            bounds.maxZ = 1024;
            bounds.valid = true;
        }

        if (bounds.minX == bounds.maxX) bounds.maxX += 1024;
        if (bounds.minZ == bounds.maxZ) bounds.maxZ += 1024;
        return bounds;
    }

    bool MapDocument::ExportSvg(const std::string& path, std::string& errorMessage) const
    {
        Bounds bounds = ComputeBounds();
        const int32_t width = std::max<int32_t>(1024, bounds.maxX - bounds.minX + 512);
        const int32_t height = std::max<int32_t>(1024, bounds.maxZ - bounds.minZ + 512);

        std::ofstream file(path, std::ios::binary);
        if (!file)
        {
            errorMessage = "Could not create SVG file.";
            return false;
        }

        file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        file << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width << "\" height=\"" << height << "\" viewBox=\""
             << bounds.minX - 256 << " " << bounds.minZ - 256 << " " << width << " " << height << "\">\n";
        file << "<rect x=\"" << (bounds.minX - 256) << "\" y=\"" << (bounds.minZ - 256)
             << "\" width=\"" << width << "\" height=\"" << height << "\" fill=\"#121212\"/>\n";

        for (size_t i = 0; i < zones.size(); ++i)
        {
            const Zone& zone = zones[i];
            if (zone.ztype == static_cast<int16_t>(ZoneType::Wall))
            {
                file << "<line x1=\"" << zone.x1 << "\" y1=\"" << zone.z1 << "\" x2=\"" << zone.x2 << "\" y2=\"" << zone.z2
                     << "\" stroke=\"#69a7ff\" stroke-width=\"24\"/>\n";
            }
            else
            {
                const int x = std::min<int>(zone.x1, zone.x2);
                const int y = std::min<int>(zone.z1, zone.z2);
                const int w = std::max<int>(1, std::abs(zone.x2 - zone.x1));
                const int h = std::max<int>(1, std::abs(zone.z2 - zone.z1));
                const char* fill = (zone.ztype == static_cast<int16_t>(ZoneType::MonsterZone)) ? "#2ea04355" : "#ff4d4d55";
                const char* stroke = (zone.ztype == static_cast<int16_t>(ZoneType::MonsterZone)) ? "#2ea043" : "#ff4d4d";
                file << "<rect x=\"" << x << "\" y=\"" << y << "\" width=\"" << w << "\" height=\"" << h
                     << "\" fill=\"" << fill << "\" stroke=\"" << stroke << "\" stroke-width=\"16\"/>\n";
            }
            file << "<text x=\"" << std::min<int>(zone.x1, zone.x2) << "\" y=\"" << std::min<int>(zone.z1, zone.z2) - 16
                 << "\" fill=\"#ffffff\" font-family=\"Segoe UI, Arial\" font-size=\"96\">Z" << i << " E" << zone.ev << "</text>\n";
        }

        for (int i = 0; i < kTextureSlotCount; ++i)
        {
            file << "<text x=\"" << (bounds.minX + 32) << "\" y=\"" << (bounds.minZ + 96 + (i * 96))
                 << "\" fill=\"#cccccc\" font-family=\"Segoe UI, Arial\" font-size=\"72\">T" << i << ": "
                 << EscapeXml(textureNames[i]) << "</text>\n";
        }

        file << "</svg>\n";
        return true;
    }
}
