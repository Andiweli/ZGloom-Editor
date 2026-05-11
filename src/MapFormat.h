#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace mapfmt
{
    enum class ZoneType : int16_t
    {
        Wall = 1,
        MonsterZone = 2,
        EventTrigger = 3,
    };

    enum class CommandType : int16_t
    {
        End = 0,
        AddMonster = 1,
        OpenDoor = 2,
        Teleport = 3,
        LoadObjects = 4,
        ChangeTexture = 5,
        RotatePoly = 6,
        UnknownRaw = -32768,
    };

    struct Zone
    {
        int16_t ztype = static_cast<int16_t>(ZoneType::Wall);
        int16_t x1 = 0;
        int16_t z1 = 0;
        int16_t x2 = 0;
        int16_t z2 = 0;
        int16_t a = 0;
        int16_t b = 0;
        int16_t na = 0;
        int16_t nb = 0;
        int16_t ln = 0;
        std::array<uint8_t, 8> textures{};
        int16_t sc = 0;
        int16_t ev = 0;
    };

    struct EventCommand
    {
        CommandType type = CommandType::AddMonster;
        std::array<int16_t, 5> params{};
        std::vector<int16_t> listValues;
        int16_t rawOpcode = 0;
        uint32_t rawByteCount = 0;

        std::string ToDisplayString() const;
    };

    struct EventScript
    {
        std::vector<EventCommand> commands;
        bool hasUnsupportedRaw = false;
        std::vector<uint8_t> rawBytes;
    };

    struct AnimationEntry
    {
        uint16_t frames = 0;
        uint16_t first = 0;
        uint16_t delay = 0;
        uint16_t current = 0;

        bool Contains(int textureIndex) const
        {
            return frames > 0 && textureIndex >= static_cast<int>(first) && textureIndex < static_cast<int>(first + frames);
        }

        int Last() const
        {
            return frames == 0 ? static_cast<int>(first) : static_cast<int>(first + frames - 1);
        }
    };

    struct Bounds
    {
        int32_t minX = 0;
        int32_t minZ = 0;
        int32_t maxX = 1024;
        int32_t maxZ = 1024;
        bool valid = false;
    };

    void RecalculateWallMetadata(Zone& zone);

    class MapDocument
    {
    public:
        static constexpr int kEventCount = 24;
        static constexpr int kTextureSlotCount = 8;
        static constexpr int kGridSize = 32;
        static constexpr int kZoneSize = 32;

        void NewBlank();
        bool LoadFromFile(const std::string& path, std::string& errorMessage);
        bool SaveToFile(const std::string& path, std::string& errorMessage) const;
        bool ExportSvg(const std::string& path, std::string& errorMessage) const;
        std::vector<std::string> Validate() const;
        Bounds ComputeBounds() const;

        std::vector<Zone> zones;
        std::array<std::string, kTextureSlotCount> textureNames;
        std::array<EventScript, kEventCount> events;
        std::vector<uint8_t> animationBlock;
        std::vector<AnimationEntry> animations;
        using CollisionCell = std::vector<uint16_t>;
        using CollisionPlane = std::array<std::array<CollisionCell, kGridSize>, kGridSize>;
        std::array<CollisionPlane, 2> collisionGrid{};
        bool hasCollisionGrid = false;
        std::vector<Zone> originalZonesForGrid;

        std::string sourcePath;
        bool dirty = false;
    };
}
