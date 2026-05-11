#include "MapFormat.h"

#include "decrunchmania.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstddef>
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

    size_t FindEventRawEnd(const std::array<uint32_t, mapfmt::MapDocument::kEventCount>& eventPointers, size_t eventStart, size_t rawSize)
    {
        size_t eventEnd = rawSize;
        for (uint32_t pointer : eventPointers)
        {
            const size_t candidate = static_cast<size_t>(pointer);
            if (candidate > eventStart && candidate < eventEnd)
            {
                eventEnd = candidate;
            }
        }
        return eventEnd;
    }

    bool ContainsUnsupportedRawEvent(const std::array<mapfmt::EventScript, mapfmt::MapDocument::kEventCount>& scripts)
    {
        for (const auto& script : scripts)
        {
            if (script.hasUnsupportedRaw)
            {
                return true;
            }
        }
        return false;
    }

    bool IsMoveWallGroupGuideCommand(const mapfmt::EventCommand& command)
    {
        if (command.type != mapfmt::CommandType::RotatePoly)
        {
            return false;
        }

        const int distance = std::abs(static_cast<int>(command.params[2]));
        const int flags = static_cast<int>(command.params[3]);
        return distance == 384 && (flags == 1 || flags == 3);
    }

    std::vector<uint8_t> BuildMoveWallGroupGuideMask(
        const std::array<mapfmt::EventScript, mapfmt::MapDocument::kEventCount>& scripts,
        size_t zoneCount)
    {
        std::vector<uint8_t> guideMask(zoneCount, 0);
        for (const auto& script : scripts)
        {
            if (script.hasUnsupportedRaw)
            {
                continue;
            }

            for (const auto& command : script.commands)
            {
                if (!IsMoveWallGroupGuideCommand(command))
                {
                    continue;
                }

                const int first = static_cast<int>(command.params[0]);
                const int count = std::max(1, static_cast<int>(command.params[1]));
                const int guideFirst = first + count;
                if (first < 0 || guideFirst < 0 || guideFirst + count > static_cast<int>(zoneCount))
                {
                    continue;
                }

                for (int i = 0; i < count; ++i)
                {
                    guideMask[static_cast<size_t>(guideFirst + i)] = 1;
                }
            }
        }
        return guideMask;
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

    constexpr int kCollisionCellSize = 256;
    constexpr double kFixedPointOne = 32766.0;

    int ClampCell(int value)
    {
        if (value < 0) return 0;
        if (value > 31) return 31;
        return value;
    }

    int ClampToInt16Value(int value)
    {
        if (value < -32768) return -32768;
        if (value > 32767) return 32767;
        return value;
    }

    int WorldToCell(int value)
    {
        // ZGloom/Gloom queries collision cells with x / 256 and z / 256.
        // Use floor division for negative coordinates; C++ integer division truncates
        // toward zero and would otherwise place negative walls into the wrong cell.
        int cell = value / kCollisionCellSize;
        if (value < 0 && (value % kCollisionCellSize) != 0)
        {
            --cell;
        }
        return ClampCell(cell);
    }

    void AddUniqueZoneIndex(std::vector<uint16_t>& indices, uint16_t zoneIndex)
    {
        if (std::find(indices.begin(), indices.end(), zoneIndex) == indices.end())
        {
            indices.push_back(zoneIndex);
        }
    }

    bool SameGridRelevantZoneShape(const mapfmt::Zone& a, const mapfmt::Zone& b)
    {
        return a.ztype == b.ztype &&
               a.x1 == b.x1 && a.z1 == b.z1 &&
               a.x2 == b.x2 && a.z2 == b.z2;
    }

    bool ExactReverseWallPair(const mapfmt::Zone& a, const mapfmt::Zone& b)
    {
        return a.ztype == static_cast<int16_t>(mapfmt::ZoneType::Wall) &&
               b.ztype == static_cast<int16_t>(mapfmt::ZoneType::Wall) &&
               a.x1 == b.x2 && a.z1 == b.z2 &&
               a.x2 == b.x1 && a.z2 == b.z1;
    }

    bool CollisionGridContainsZone(const std::array<mapfmt::MapDocument::CollisionPlane, 2>& grid, uint16_t zoneIndex)
    {
        for (const auto& plane : grid)
        {
            for (const auto& xColumn : plane)
            {
                for (const auto& cell : xColumn)
                {
                    if (std::find(cell.begin(), cell.end(), zoneIndex) != cell.end())
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool CollisionGridLooksAmigaSafe(const std::array<mapfmt::MapDocument::CollisionPlane, 2>& grid, const std::vector<mapfmt::Zone>& zones)
    {
        // Older editor saves used a broad rectangle expansion for every wall and
        // sometimes preserved only the front half of exact reverse/backface pairs.
        // ZGloom's preview tolerates that, but real Gloom Deluxe appears to use
        // the grid for render lookup as well; overcrowded cells or missing
        // backfaces can become solid invisible walls. Shipped Amiga maps observed
        // so far keep wall cell lists small, so suspicious grids are rebuilt.
        constexpr size_t kMaxSafeWallCellEntries = 16;
        for (const auto& xColumn : grid[0])
        {
            for (const auto& cell : xColumn)
            {
                if (cell.size() > kMaxSafeWallCellEntries)
                {
                    return false;
                }
            }
        }

        for (size_t i = 0; i < zones.size(); ++i)
        {
            if (zones[i].ztype != static_cast<int16_t>(mapfmt::ZoneType::Wall))
            {
                continue;
            }

            for (size_t j = i + 1; j < zones.size(); ++j)
            {
                if (!ExactReverseWallPair(zones[i], zones[j]))
                {
                    continue;
                }

                const bool iInGrid = CollisionGridContainsZone(grid, static_cast<uint16_t>(i));
                const bool jInGrid = CollisionGridContainsZone(grid, static_cast<uint16_t>(j));
                if (iInGrid != jInGrid)
                {
                    return false;
                }
            }
        }

        return true;
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
    void RecalculateWallMetadata(Zone& zone)
    {
        if (zone.ztype != static_cast<int16_t>(ZoneType::Wall) &&
            zone.ztype != static_cast<int16_t>(ZoneType::MonsterZone) &&
            zone.ztype != static_cast<int16_t>(ZoneType::EventTrigger))
        {
            return;
        }

        int dx = static_cast<int>(zone.x2) - static_cast<int>(zone.x1);
        int dz = static_cast<int>(zone.z2) - static_cast<int>(zone.z1);
        double length = std::hypot(static_cast<double>(dx), static_cast<double>(dz));

        if (length < 1.0)
        {
            // Avoid zero-length walls. A wall with ln=0 and zero normals is drawn
            // by the editor, but it cannot collide in the engine.
            zone.x2 = static_cast<int16_t>(ClampToInt16Value(static_cast<int>(zone.x1) + 1));
            zone.z2 = zone.z1;
            dx = static_cast<int>(zone.x2) - static_cast<int>(zone.x1);
            dz = static_cast<int>(zone.z2) - static_cast<int>(zone.z1);
            length = std::hypot(static_cast<double>(dx), static_cast<double>(dz));
        }

        const int normalX = ClampToInt16Value(static_cast<int>(std::lround((static_cast<double>(dx) / length) * kFixedPointOne)));
        const int normalZ = ClampToInt16Value(static_cast<int>(std::lround((static_cast<double>(dz) / length) * kFixedPointOne)));

        zone.na = static_cast<int16_t>(normalX);
        zone.nb = static_cast<int16_t>(normalZ);
        zone.a = static_cast<int16_t>(ClampToInt16Value(-normalZ));
        zone.b = static_cast<int16_t>(normalX);
        zone.ln = static_cast<int16_t>(ClampToInt16Value(static_cast<int>(std::lround(length))));
    }

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
        case CommandType::UnknownRaw:
            os << "Advanced/Raw MAPED command block preserved"
               << "  opcode=" << rawOpcode
               << " bytes=" << rawByteCount;
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
            script.hasUnsupportedRaw = false;
            script.rawBytes.clear();
        }
        animationBlock.clear();
        animations.clear();
        for (auto& plane : collisionGrid)
        {
            for (auto& xColumn : plane)
            {
                for (auto& cell : xColumn)
                {
                    cell.clear();
                }
            }
        }
        hasCollisionGrid = false;
        originalZonesForGrid.clear();
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

        for (auto& plane : collisionGrid)
        {
            for (auto& xColumn : plane)
            {
                for (auto& cell : xColumn)
                {
                    cell.clear();
                }
            }
        }
        hasCollisionGrid = false;
        if (gridoff + (kGridSize * kGridSize * 8) <= raw.size())
        {
            bool gridOk = true;
            for (int z = 0; z < kGridSize && gridOk; ++z)
            {
                for (int x = 0; x < kGridSize && gridOk; ++x)
                {
                    for (int plane = 0; plane < 2 && gridOk; ++plane)
                    {
                        const size_t cellOffset = gridoff + static_cast<size_t>((x + z * kGridSize) * 8 + plane * 4);
                        const uint16_t storedCount = ReadBE16(raw.data() + cellOffset);
                        const uint16_t storedOffset = ReadBE16(raw.data() + cellOffset + 2);
                        const uint32_t count = static_cast<uint16_t>(storedCount + 1);
                        if (count == 0)
                        {
                            continue;
                        }
                        const size_t listStart = polypnt + static_cast<size_t>(storedOffset) * 2;
                        if (listStart + static_cast<size_t>(count) * 2 > raw.size())
                        {
                            gridOk = false;
                            break;
                        }
                        for (uint32_t i = 0; i < count; ++i)
                        {
                            const uint16_t zoneIndex = ReadBE16(raw.data() + listStart + static_cast<size_t>(i) * 2);
                            if (zoneIndex < zones.size())
                            {
                                collisionGrid[plane][x][z].push_back(zoneIndex);
                            }
                        }
                    }
                }
            }
            hasCollisionGrid = gridOk;
        }
        originalZonesForGrid = zones;

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
        animations.clear();
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
                const uint16_t first = ReadBE16(raw.data() + animPos + 2);
                const uint16_t delay = ReadBE16(raw.data() + animPos + 4);
                const uint16_t current = ReadBE16(raw.data() + animPos + 6);
                for (size_t i = 0; i < 8; ++i)
                {
                    animationBlock.push_back(raw[animPos + i]);
                }
                animPos += 8;
                if (frames == 0)
                {
                    break;
                }
                AnimationEntry entry{};
                entry.frames = frames;
                entry.first = first;
                entry.delay = delay;
                entry.current = current;
                animations.push_back(entry);
            }
        }

        for (auto& script : events)
        {
            script.commands.clear();
            script.hasUnsupportedRaw = false;
            script.rawBytes.clear();
        }

        for (int evIndex = 0; evIndex < kEventCount; ++evIndex)
        {
            auto& script = events[evIndex];
            const size_t eventStart = static_cast<size_t>(eventPointers[evIndex]);
            const size_t eventEnd = FindEventRawEnd(eventPointers, eventStart, raw.size());
            if (eventStart <= raw.size() && eventEnd >= eventStart)
            {
                script.rawBytes.assign(raw.begin() + static_cast<std::ptrdiff_t>(eventStart), raw.begin() + static_cast<std::ptrdiff_t>(eventEnd));
            }

            size_t pos = eventStart;
            while ((pos + 2) <= eventEnd)
            {
                const size_t commandStart = pos;
                const int16_t rawOpcode = static_cast<int16_t>(ReadBE16(raw.data() + pos));
                const auto op = static_cast<CommandType>(rawOpcode);
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
                    if ((pos + 10) > eventEnd)
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
                    if ((pos + 2) > eventEnd)
                    {
                        errorMessage = "Open Door command is truncated.";
                        return false;
                    }
                    cmd.params[0] = static_cast<int16_t>(ReadBE16(raw.data() + pos));
                    pos += 2;
                    break;
                case CommandType::Teleport:
                    if ((pos + 8) > eventEnd)
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
                    while ((pos + 2) <= eventEnd)
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
                    if ((pos + 4) > eventEnd)
                    {
                        errorMessage = "Change Texture command is truncated.";
                        return false;
                    }
                    cmd.params[0] = static_cast<int16_t>(ReadBE16(raw.data() + pos)); pos += 2;
                    cmd.params[1] = static_cast<int16_t>(ReadBE16(raw.data() + pos)); pos += 2;
                    break;
                case CommandType::RotatePoly:
                    if ((pos + 8) > eventEnd)
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
                    // Original MAPED maps can contain commands we do not safely
                    // understand yet (for example morph/lock-style logic). Keep
                    // the whole event byte block verbatim so loading and saving
                    // never destroys that logic. The editor exposes it as an
                    // advanced/raw entry and prevents high-level editing of that slot.
                    script.commands.clear();
                    script.hasUnsupportedRaw = true;
                    EventCommand rawCommand;
                    rawCommand.type = CommandType::UnknownRaw;
                    rawCommand.rawOpcode = rawOpcode;
                    rawCommand.rawByteCount = static_cast<uint32_t>(script.rawBytes.size());
                    script.commands.push_back(rawCommand);
                    pos = eventEnd;
                    break;
                }

                if (script.hasUnsupportedRaw)
                {
                    break;
                }

                events[evIndex].commands.push_back(cmd);
                if (pos <= commandStart)
                {
                    break;
                }
            }

            if (!script.hasUnsupportedRaw)
            {
                script.rawBytes.clear();
            }
        }

        sourcePath = path;
        dirty = false;
        return true;
    }


    int CountListValue(const std::vector<int16_t>& values, int16_t value)
    {
        return static_cast<int>(std::count(values.begin(), values.end(), value));
    }

    std::vector<int16_t> BuildRequiredLoadObjectList(const std::array<EventScript, MapDocument::kEventCount>& scripts)
    {
        std::vector<int16_t> required;
        for (const auto& script : scripts)
        {
            if (script.hasUnsupportedRaw) continue;
            for (const auto& command : script.commands)
            {
                if (command.type == CommandType::AddMonster)
                {
                    required.push_back(command.params[0]);
                }
            }
        }
        return required;
    }

    std::vector<int16_t> MergeLoadObjectListPreservingOriginalOrder(
        const std::vector<int16_t>& existing,
        const std::vector<int16_t>& required)
    {
        std::vector<int16_t> merged;
        merged.reserve(required.size());

        for (int16_t value : existing)
        {
            if (CountListValue(merged, value) < CountListValue(required, value))
            {
                merged.push_back(value);
            }
        }

        // Older editor saves could lose the LoadObjects command entirely. The
        // original Amiga engine still expects event 1 to preload every object
        // type that later appears in AddMonster commands; otherwise enemies can
        // instantiate from stale/invalid object slots and appear outside the map.
        // Append missing entries in real spawn order while keeping shipped maps'
        // existing preload order unchanged whenever possible.
        for (int16_t value : required)
        {
            if (CountListValue(merged, value) < CountListValue(required, value))
            {
                merged.push_back(value);
            }
        }

        return merged;
    }

    void NormalizeLoadObjectsForAmigaSave(std::array<EventScript, MapDocument::kEventCount>& scripts)
    {
        const std::vector<int16_t> required = BuildRequiredLoadObjectList(scripts);

        std::vector<int16_t> existing;
        for (const auto& script : scripts)
        {
            if (script.hasUnsupportedRaw) continue;
            for (const auto& command : script.commands)
            {
                if (command.type == CommandType::LoadObjects)
                {
                    existing.insert(existing.end(), command.listValues.begin(), command.listValues.end());
                }
            }
        }

        for (auto& script : scripts)
        {
            if (script.hasUnsupportedRaw) continue;
            script.commands.erase(std::remove_if(script.commands.begin(), script.commands.end(), [](const EventCommand& command)
            {
                return command.type == CommandType::LoadObjects;
            }), script.commands.end());
        }

        if (required.empty())
        {
            return;
        }

        EventCommand loadCommand;
        loadCommand.type = CommandType::LoadObjects;
        loadCommand.listValues = MergeLoadObjectListPreservingOriginalOrder(existing, required);
        scripts[0].commands.insert(scripts[0].commands.begin(), loadCommand);
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
        std::vector<Zone> saveZones = zones;
        auto eventsToWrite = events;
        if (!ContainsUnsupportedRawEvent(eventsToWrite))
        {
            NormalizeLoadObjectsForAmigaSave(eventsToWrite);
        }
        const std::vector<uint8_t> moveWallGuideMask = BuildMoveWallGroupGuideMask(eventsToWrite, saveZones.size());
        for (size_t zoneIndex = 0; zoneIndex < saveZones.size(); ++zoneIndex)
        {
            Zone& zone = saveZones[zoneIndex];
            const bool lineZone = zone.ztype == static_cast<int16_t>(ZoneType::Wall) ||
                zone.ztype == static_cast<int16_t>(ZoneType::MonsterZone) ||
                zone.ztype == static_cast<int16_t>(ZoneType::EventTrigger);
            const bool moveWallGuide =
                zoneIndex < moveWallGuideMask.size() &&
                moveWallGuideMask[zoneIndex] != 0 &&
                zone.ztype == static_cast<int16_t>(ZoneType::Wall);

            // map1b-style move targets intentionally use a wall-shaped guide run
            // with a=0/b=0.  It is the hidden destination polygon for RotatePoly
            // and must not be recalculated into a visible wall during save.
            // Force the values here too, so maps saved once with an older editor
            // build are repaired by simply opening and saving them again.
            if (moveWallGuide)
            {
                zone.a = 0;
                zone.b = 0;
                zone.na = 0;
                zone.nb = 0;
                zone.ln = 0;
                zone.textures.fill(0);
                zone.sc = 0;
                zone.ev = 0;
            }

            if (lineZone && !moveWallGuide &&
                (zone.ln <= 0 || (zone.na == 0 && zone.nb == 0) || (zone.a == 0 && zone.b == 0)))
            {
                RecalculateWallMetadata(zone);
            }
        }

        auto addLineZoneToGrid = [&](int plane, const Zone& zone, uint16_t zoneIndex)
        {
            plane = std::max(0, std::min(1, plane));
            const int dx = static_cast<int>(zone.x2) - static_cast<int>(zone.x1);
            const int dz = static_cast<int>(zone.z2) - static_cast<int>(zone.z1);
            const int steps = std::max(1, static_cast<int>(std::ceil(static_cast<double>(std::max(std::abs(dx), std::abs(dz))) / 64.0)));
            for (int i = 0; i <= steps; ++i)
            {
                const double t = static_cast<double>(i) / static_cast<double>(steps);
                const int wx = static_cast<int>(std::lround(static_cast<double>(zone.x1) + static_cast<double>(dx) * t));
                const int wz = static_cast<int>(std::lround(static_cast<double>(zone.z1) + static_cast<double>(dz) * t));
                AddUniqueZoneIndex(cells[plane][WorldToCell(wx)][WorldToCell(wz)].zoneIndices, zoneIndex);
            }
        };

        // Start with the original Amiga grid when available and sane. This grid is
        // used by the original Gloom/Gloom Deluxe engine not only for
        // blocking/collision, but also for visibility/render lookup. Preserve exact
        // shipped cell coverage, but rebuild grids created by older editor builds
        // when they are overcrowded or lost one half of exact backface pairs.
        const bool preserveOriginalGrid = hasCollisionGrid &&
            CollisionGridLooksAmigaSafe(collisionGrid, saveZones);

        if (preserveOriginalGrid)
        {
            for (int plane = 0; plane < 2; ++plane)
            {
                for (int x = 0; x < kGridSize; ++x)
                {
                    for (int z = 0; z < kGridSize; ++z)
                    {
                        for (uint16_t zoneIndex : collisionGrid[plane][x][z])
                        {
                            if (zoneIndex < saveZones.size() &&
                                zoneIndex < originalZonesForGrid.size() &&
                                SameGridRelevantZoneShape(saveZones[zoneIndex], originalZonesForGrid[zoneIndex]))
                            {
                                AddUniqueZoneIndex(cells[plane][x][z].zoneIndices, zoneIndex);
                            }
                        }
                    }
                }
            }
        }

        for (size_t zoneIndex = 0; zoneIndex < saveZones.size(); ++zoneIndex)
        {
            const Zone& zone = saveZones[zoneIndex];
            const bool originalGridAlreadyPreserved =
                preserveOriginalGrid &&
                zoneIndex < originalZonesForGrid.size() &&
                SameGridRelevantZoneShape(zone, originalZonesForGrid[zoneIndex]);

            if (originalGridAlreadyPreserved)
            {
                // The exact original cell coverage was copied above. Do not add an
                // approximated, expanded coverage a second time: shipped maps use
                // very specific wall/trigger/door cell ranges, and widening them
                // changes real Amiga render/collision/trigger behaviour after a
                // plain save.
                continue;
            }

            if (zone.ztype == static_cast<int16_t>(ZoneType::Wall))
            {
                const bool moveWallGuide =
                    zoneIndex < moveWallGuideMask.size() &&
                    moveWallGuideMask[zoneIndex] != 0;
                if (moveWallGuide)
                {
                    // Hidden destination guides are addressed directly by the
                    // RotatePoly command. They should not become render/collision
                    // grid entries of their own.
                    continue;
                }

                // Amiga-compatible export for new/edited walls. Do not use the
                // old broad rectangle expansion here: it can put 20+ walls into a
                // single small-map cell, while original Amiga maps commonly keep
                // wall lists very small. Real Gloom Deluxe then may skip render
                // entries while collision still blocks. Follow the line itself and
                // include exact reverse/backface zones as their own entries.
                addLineZoneToGrid(0, zone, static_cast<uint16_t>(zoneIndex));
                continue;
            }

            if (zone.ztype == static_cast<int16_t>(ZoneType::MonsterZone) ||
                zone.ztype == static_cast<int16_t>(ZoneType::EventTrigger))
            {
                // Trigger/monster helper lines also follow the line itself. Keeping
                // them narrow avoids firing doors/exits from neighbouring cells.
                addLineZoneToGrid(1, zone, static_cast<uint16_t>(zoneIndex));
            }
        }

        std::vector<uint16_t> polyList;
        polyList.reserve(saveZones.size() * 32);

        std::vector<uint8_t> out;
        out.resize(headerSize, 0);

        const uint32_t gridoff = headerSize;
        const uint32_t polyoff = gridoff + gridBytes;
        const uint32_t polypnt = polyoff + static_cast<uint32_t>(saveZones.size() * kZoneSize);

        WriteBE32At(out, 0, gridoff);
        WriteBE32At(out, 4, polyoff);
        WriteBE32At(out, 8, polypnt);

        out.resize(polyoff, 0);

        for (const Zone& zone : saveZones)
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
                    if (cell.zoneIndices.size() > 0x10000u || polyList.size() > 0xFFFFu)
                    {
                        errorMessage = "Collision grid is too large for the original 16-bit map format.";
                        return false;
                    }
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
            const auto& script = eventsToWrite[eventIndex];
            if (script.hasUnsupportedRaw)
            {
                if (script.rawBytes.empty())
                {
                    errorMessage = "An advanced/raw event command exists but its preserved byte block is empty.";
                    return false;
                }
                out.insert(out.end(), script.rawBytes.begin(), script.rawBytes.end());
                WriteBE32At(out, 20 + (eventIndex * 4), eventOffsets[eventIndex]);
                continue;
            }

            const auto& commands = script.commands;
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
        const std::vector<uint8_t> moveWallGuideMask = BuildMoveWallGroupGuideMask(events, zones.size());

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
            if (zone.ztype == static_cast<int16_t>(ZoneType::Wall))
            {
                const bool moveWallGuide =
                    i < moveWallGuideMask.size() &&
                    moveWallGuideMask[i] != 0;
                if (!moveWallGuide &&
                    (zone.ln <= 0 || (zone.na == 0 && zone.nb == 0) || (zone.a == 0 && zone.b == 0)))
                {
                    issues.push_back("Zone " + std::to_string(i) + " is a wall but has missing collision metadata; it will be recalculated on save.");
                }
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
            if (events[ev].hasUnsupportedRaw)
            {
                issues.push_back("Event " + std::to_string(ev + 1) + " contains advanced/raw MAPED commands and will be preserved verbatim.");
                continue;
            }

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

        for (const auto& anim : animations)
        {
            if (anim.frames == 0)
            {
                continue;
            }
            if (anim.first >= 160 || anim.Last() >= 160)
            {
                issues.push_back("Animation texture range " + std::to_string(anim.first) + ".." + std::to_string(anim.Last()) + " is outside 0..159.");
            }
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
                const char* stroke = (zone.ztype == static_cast<int16_t>(ZoneType::MonsterZone)) ? "#2ea043" : "#ff4d4d";
                file << "<line x1=\"" << zone.x1 << "\" y1=\"" << zone.z1 << "\" x2=\"" << zone.x2 << "\" y2=\"" << zone.z2
                     << "\" stroke=\"" << stroke << "\" stroke-width=\"20\" stroke-dasharray=\"48,32\"/>\n";
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
