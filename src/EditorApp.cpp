#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <type_traits>
#include <vector>

#include "MapFormat.h"
#include "decrunchmania.h"
#include "resource.h"

#pragma comment(lib, "comctl32.lib")

namespace
{
    const wchar_t* kMainWindowClass = L"ZGloomEditorMainWindow";
    const wchar_t* kCanvasClass = L"ZGloomEditorCanvas";
    const wchar_t* kInfoPanelClass = L"ZGloomEditorInfoPanel";
    constexpr int kGridStep = 256;
    constexpr double kCanvasZoomMin = 0.25;
    constexpr double kCanvasZoomMax = 16.0;

    enum class InsertMode
    {
        None,
        Wall,
        MonsterZone,
        EventTrigger,
    };

    struct TexturePreviewImage
    {
        std::string name;
        std::string loadedPath;
        std::string error;
        int width = 0;
        int height = 0;
        std::vector<uint32_t> pixels;
    };

    struct AppState
    {
        HINSTANCE instance = nullptr;
        HWND mainWindow = nullptr;
        HWND zoneList = nullptr;
        HWND canvas = nullptr;
        HWND infoPanel = nullptr;
        HWND statusBar = nullptr;
        HWND btnAddWall = nullptr;
        HWND btnAddMonster = nullptr;
        HWND btnAddTrigger = nullptr;
        HWND btnEdit = nullptr;
        HWND btnDelete = nullptr;
        HWND btnUp = nullptr;
        HWND btnDown = nullptr;
        HWND btnEvents = nullptr;
        HWND btnTextures = nullptr;
        mapfmt::MapDocument document;
        int selectedZone = -1;
        int previewTextureBand = 0;
        int previewTextureIndex = 0;
        int previewTextureSlot = 0;
        int previewTextureStrip = 0;
        int previewScrollX = 0;
        bool previewAutoScrollPending = false;
        InsertMode insertMode = InsertMode::None;
        bool isDrawing = false;
        bool viewInitialized = false;
        double canvasZoom = 1.0;
        double viewCenterX = 512.0;
        double viewCenterZ = 512.0;
        POINT drawStartWorld{};
        POINT drawCurrentWorld{};
        TexturePreviewImage previewImage;
    };

    struct CanvasMetrics
    {
        mapfmt::Bounds baseBounds{};
        mapfmt::Bounds viewBounds{};
        double originX = 0.0;
        double originY = 0.0;
        double scale = 1.0;
        double margin = 24.0;
    };

    struct PreviewMetrics
    {
        RECT outer{};
        RECT inner{};
        int viewportWidth = 0;
        int viewportHeight = 0;
        int scaledWidth = 0;
        int scaledHeight = 0;
        int maxScrollX = 0;
    };

    AppState g_app;

    void RefreshStatus();
    void UpdateInfoPanelScrollBar(HWND hwnd);
    void FitViewToDocument();
    POINT WorldToScreen(const RECT& rc, const mapfmt::Bounds& bounds, int x, int z);
    int GetZonePreviewSlotCount(const mapfmt::Zone& zone);

    template <typename A, typename B>
    constexpr auto MinValue(const A& a, const B& b) -> typename std::common_type<A, B>::type
    {
        using R = typename std::common_type<A, B>::type;
        const R ar = static_cast<R>(a);
        const R br = static_cast<R>(b);
        return (br < ar) ? br : ar;
    }

    template <typename A, typename B>
    constexpr auto MaxValue(const A& a, const B& b) -> typename std::common_type<A, B>::type
    {
        using R = typename std::common_type<A, B>::type;
        const R ar = static_cast<R>(a);
        const R br = static_cast<R>(b);
        return (ar < br) ? br : ar;
    }

    template <typename V, typename L, typename H>
    constexpr auto ClampValue(const V& value, const L& low, const H& high) -> typename std::common_type<V, L, H>::type
    {
        using R = typename std::common_type<V, L, H>::type;
        return MinValue<R>(static_cast<R>(high), MaxValue<R>(static_cast<R>(low), static_cast<R>(value)));
    }

    std::wstring Utf8ToWide(const std::string& input)
    {
        if (input.empty()) return std::wstring();
        const int size = MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, nullptr, 0);
        std::wstring buffer(size > 0 ? size : 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, buffer.data(), size);
        if (!buffer.empty() && buffer.back() == L'\0') buffer.pop_back();
        return buffer;
    }

    std::string WideToUtf8(const std::wstring& input)
    {
        if (input.empty()) return std::string();
        const int size = WideCharToMultiByte(CP_UTF8, 0, input.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string buffer(size > 0 ? size : 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, input.c_str(), -1, buffer.data(), size, nullptr, nullptr);
        if (!buffer.empty() && buffer.back() == '\0') buffer.pop_back();
        return buffer;
    }

    std::wstring FormatPreviewSourcePath(const std::string& path)
    {
        if (path.empty()) return std::wstring();

        std::wstring wide = Utf8ToWide(path);
        std::replace(wide.begin(), wide.end(), L'\\', L'/');

        const std::wstring marker = L"txts/";
        const std::size_t pos = wide.find(marker);
        if (pos != std::wstring::npos)
        {
            return wide.substr(pos);
        }

        const std::size_t slash = wide.find_last_of(L'/');
        return (slash == std::wstring::npos) ? wide : wide.substr(slash + 1);
    }

    uint16_t ReadBE16Local(const uint8_t* p)
    {
        return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));
    }

    uint32_t ReadBE32Local(const uint8_t* p)
    {
        return (static_cast<uint32_t>(p[0]) << 24) |
               (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8) |
               static_cast<uint32_t>(p[3]);
    }

    int FloorToGrid(int value)
    {
        if (value >= 0) return (value / kGridStep) * kGridStep;
        return -(((-value) + (kGridStep - 1)) / kGridStep) * kGridStep;
    }

    int CeilToGrid(int value)
    {
        if (value >= 0) return ((value + (kGridStep - 1)) / kGridStep) * kGridStep;
        return -((-value) / kGridStep) * kGridStep;
    }

    mapfmt::Bounds GetAlignedDocumentBounds(const mapfmt::Bounds& rawBounds)
    {
        mapfmt::Bounds aligned = rawBounds;
        aligned.minX = FloorToGrid(rawBounds.minX) - kGridStep;
        aligned.minZ = FloorToGrid(rawBounds.minZ) - kGridStep;
        aligned.maxX = CeilToGrid(rawBounds.maxX) + kGridStep;
        aligned.maxZ = CeilToGrid(rawBounds.maxZ) + kGridStep;
        if (aligned.maxX <= aligned.minX) aligned.maxX = aligned.minX + (kGridStep * 4);
        if (aligned.maxZ <= aligned.minZ) aligned.maxZ = aligned.minZ + (kGridStep * 4);
        aligned.valid = true;
        return aligned;
    }

    void ClampViewCenterToDocument(const mapfmt::Bounds& rawBounds)
    {
        const mapfmt::Bounds baseBounds = GetAlignedDocumentBounds(rawBounds);
        const double baseWidth = MaxValue(1.0, static_cast<double>(baseBounds.maxX - baseBounds.minX));
        const double baseHeight = MaxValue(1.0, static_cast<double>(baseBounds.maxZ - baseBounds.minZ));
        const double visibleWidth = MaxValue(static_cast<double>(kGridStep), baseWidth / MaxValue(kCanvasZoomMin, g_app.canvasZoom));
        const double visibleHeight = MaxValue(static_cast<double>(kGridStep), baseHeight / MaxValue(kCanvasZoomMin, g_app.canvasZoom));

        if (visibleWidth >= baseWidth)
        {
            g_app.viewCenterX = (static_cast<double>(baseBounds.minX) + static_cast<double>(baseBounds.maxX)) * 0.5;
        }
        else
        {
            const double minCenterX = static_cast<double>(baseBounds.minX) + visibleWidth * 0.5;
            const double maxCenterX = static_cast<double>(baseBounds.maxX) - visibleWidth * 0.5;
            g_app.viewCenterX = ClampValue(g_app.viewCenterX, minCenterX, maxCenterX);
        }

        if (visibleHeight >= baseHeight)
        {
            g_app.viewCenterZ = (static_cast<double>(baseBounds.minZ) + static_cast<double>(baseBounds.maxZ)) * 0.5;
        }
        else
        {
            const double minCenterZ = static_cast<double>(baseBounds.minZ) + visibleHeight * 0.5;
            const double maxCenterZ = static_cast<double>(baseBounds.maxZ) - visibleHeight * 0.5;
            g_app.viewCenterZ = ClampValue(g_app.viewCenterZ, minCenterZ, maxCenterZ);
        }
    }

    void FitViewToDocument()
    {
        const mapfmt::Bounds rawBounds = g_app.document.ComputeBounds();
        const mapfmt::Bounds baseBounds = GetAlignedDocumentBounds(rawBounds);
        g_app.canvasZoom = 1.0;
        g_app.viewCenterX = (static_cast<double>(baseBounds.minX) + static_cast<double>(baseBounds.maxX)) * 0.5;
        g_app.viewCenterZ = (static_cast<double>(baseBounds.minZ) + static_cast<double>(baseBounds.maxZ)) * 0.5;
        g_app.viewInitialized = true;
        ClampViewCenterToDocument(rawBounds);
    }

    CanvasMetrics GetCanvasMetrics(const RECT& rc, const mapfmt::Bounds& rawBounds)
    {
        CanvasMetrics metrics;
        metrics.baseBounds = GetAlignedDocumentBounds(rawBounds);
        metrics.viewBounds = metrics.baseBounds;

        const double canvasWidth = MaxValue(1.0, static_cast<double>(rc.right - rc.left) - metrics.margin * 2.0);
        const double canvasHeight = MaxValue(1.0, static_cast<double>(rc.bottom - rc.top) - metrics.margin * 2.0);

        double centerX = g_app.viewInitialized ? g_app.viewCenterX :
            (static_cast<double>(metrics.baseBounds.minX) + static_cast<double>(metrics.baseBounds.maxX)) * 0.5;
        double centerZ = g_app.viewInitialized ? g_app.viewCenterZ :
            (static_cast<double>(metrics.baseBounds.minZ) + static_cast<double>(metrics.baseBounds.maxZ)) * 0.5;

        const double baseWidth = MaxValue(1.0, static_cast<double>(metrics.baseBounds.maxX - metrics.baseBounds.minX));
        const double baseHeight = MaxValue(1.0, static_cast<double>(metrics.baseBounds.maxZ - metrics.baseBounds.minZ));
        const double zoom = ClampValue(g_app.canvasZoom, kCanvasZoomMin, kCanvasZoomMax);
        const double visibleWidth = MaxValue(static_cast<double>(kGridStep), baseWidth / zoom);
        const double visibleHeight = MaxValue(static_cast<double>(kGridStep), baseHeight / zoom);

        if (visibleWidth < baseWidth)
        {
            const double minCenterX = static_cast<double>(metrics.baseBounds.minX) + visibleWidth * 0.5;
            const double maxCenterX = static_cast<double>(metrics.baseBounds.maxX) - visibleWidth * 0.5;
            centerX = ClampValue(centerX, minCenterX, maxCenterX);
        }
        else
        {
            centerX = (static_cast<double>(metrics.baseBounds.minX) + static_cast<double>(metrics.baseBounds.maxX)) * 0.5;
        }

        if (visibleHeight < baseHeight)
        {
            const double minCenterZ = static_cast<double>(metrics.baseBounds.minZ) + visibleHeight * 0.5;
            const double maxCenterZ = static_cast<double>(metrics.baseBounds.maxZ) - visibleHeight * 0.5;
            centerZ = ClampValue(centerZ, minCenterZ, maxCenterZ);
        }
        else
        {
            centerZ = (static_cast<double>(metrics.baseBounds.minZ) + static_cast<double>(metrics.baseBounds.maxZ)) * 0.5;
        }

        metrics.viewBounds.minX = static_cast<int32_t>(std::floor(centerX - visibleWidth * 0.5));
        metrics.viewBounds.maxX = static_cast<int32_t>(std::ceil(centerX + visibleWidth * 0.5));
        metrics.viewBounds.minZ = static_cast<int32_t>(std::floor(centerZ - visibleHeight * 0.5));
        metrics.viewBounds.maxZ = static_cast<int32_t>(std::ceil(centerZ + visibleHeight * 0.5));
        metrics.viewBounds.valid = true;

        const double worldWidth = MaxValue(1.0, static_cast<double>(metrics.viewBounds.maxX - metrics.viewBounds.minX));
        const double worldHeight = MaxValue(1.0, static_cast<double>(metrics.viewBounds.maxZ - metrics.viewBounds.minZ));
        metrics.scale = MaxValue(0.0001, MinValue(canvasWidth / worldWidth, canvasHeight / worldHeight));

        const double usedWidth = worldWidth * metrics.scale;
        const double usedHeight = worldHeight * metrics.scale;
        metrics.originX = metrics.margin + (canvasWidth - usedWidth) * 0.5;
        metrics.originY = metrics.margin + (canvasHeight - usedHeight) * 0.5;
        return metrics;
    }

    int GetZonePreviewSlotCount(const mapfmt::Zone& zone)
    {
        int scaleInt = 0;
        if (zone.sc < 0)
        {
            scaleInt = 1;
        }
        else
        {
            scaleInt = static_cast<int>(zone.sc) / 2;
            if (scaleInt == 0) scaleInt = 1;
        }
        return ClampValue(scaleInt, 1, 8);
    }

    PreviewMetrics GetPreviewMetrics(const RECT& panelRc)
    {
        PreviewMetrics metrics{};
        const int leftPad = 12;
        const int rightPad = 12;
        const int previewTop = 244;
        const int previewHeight = 96;
        const int previewBottomLimit = MaxValue(previewTop + 32, panelRc.bottom - 56);
        const int previewBottom = MinValue(previewBottomLimit, previewTop + previewHeight);

        metrics.outer = { leftPad, previewTop, panelRc.right - rightPad, previewBottom };
        metrics.inner = { metrics.outer.left + 4, metrics.outer.top + 4, metrics.outer.right - 4, metrics.outer.bottom - 4 };
        metrics.viewportWidth = MaxValue(1, metrics.inner.right - metrics.inner.left);
        metrics.viewportHeight = MaxValue(1, metrics.inner.bottom - metrics.inner.top);

        if (!g_app.previewImage.pixels.empty() && g_app.previewImage.width > 0 && g_app.previewImage.height > 0)
        {
            const double scale = MaxValue(1.0, static_cast<double>(metrics.viewportHeight) / static_cast<double>(g_app.previewImage.height));
            metrics.scaledHeight = metrics.viewportHeight;
            metrics.scaledWidth = MaxValue(1, static_cast<int>(std::lround(static_cast<double>(g_app.previewImage.width) * scale)));
            metrics.maxScrollX = MaxValue(0, metrics.scaledWidth - metrics.viewportWidth);
        }
        return metrics;
    }

    std::string DirName(const std::string& path)
    {
        const size_t slash = path.find_last_of("\\/");
        if (slash == std::string::npos) return {};
        return path.substr(0, slash);
    }

    std::string JoinPath(const std::string& base, const std::string& leaf)
    {
        if (base.empty()) return leaf;
        const char last = base.back();
        if (last == '/' || last == '\\') return base + leaf;
        return base + "/" + leaf;
    }

    bool ReadFileBinaryLocal(const std::string& path, std::vector<uint8_t>& data)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        file.seekg(0, std::ios::end);
        const auto size = file.tellg();
        file.seekg(0, std::ios::beg);
        if (size <= 0) return false;
        data.resize(static_cast<size_t>(size));
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        return !!file;
    }

    bool LoadMaybeCrmLocal(const std::string& path, std::vector<uint8_t>& out, std::string& error)
    {
        std::vector<uint8_t> fileData;
        if (!ReadFileBinaryLocal(path, fileData))
        {
            error = "Could not open file.";
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
            error = "CrM2 decompression failed.";
            return false;
        }

        out = std::move(unpacked);
        return true;
    }

    std::wstring InsertModeToText(InsertMode mode)
    {
        switch (mode)
        {
        case InsertMode::Wall: return L"Draw Wall";
        case InsertMode::MonsterZone: return L"Draw Monster Zone";
        case InsertMode::EventTrigger: return L"Draw Event Trigger";
        default: return L"Select";
        }
    }

    void InvalidateEditorViews()
    {
        if (g_app.canvas) InvalidateRect(g_app.canvas, nullptr, TRUE);
        if (g_app.infoPanel) InvalidateRect(g_app.infoPanel, nullptr, TRUE);
    }

    void UpdateModeButtons()
    {
        if (!g_app.btnAddWall) return;
        SetWindowTextW(g_app.btnAddWall, g_app.insertMode == InsertMode::Wall ? L"Drawing Wall..." : L"Draw Wall");
        SetWindowTextW(g_app.btnAddMonster, g_app.insertMode == InsertMode::MonsterZone ? L"Drawing Monster..." : L"Draw Monster");
        SetWindowTextW(g_app.btnAddTrigger, g_app.insertMode == InsertMode::EventTrigger ? L"Drawing Trigger..." : L"Draw Trigger");
    }

    void SetInsertMode(InsertMode mode)
    {
        g_app.insertMode = (g_app.insertMode == mode) ? InsertMode::None : mode;
        g_app.isDrawing = false;
        UpdateModeButtons();
        if (g_app.mainWindow) RefreshStatus();
        InvalidateEditorViews();
    }

    bool ResolveTexturePath(const std::string& textureName, std::string& outPath)
    {
        if (textureName.empty()) return false;

        std::vector<std::string> candidates;
        auto addCandidate = [&](const std::string& candidate)
        {
            if (candidate.empty()) return;
            if (std::find(candidates.begin(), candidates.end(), candidate) == candidates.end())
            {
                candidates.push_back(candidate);
            }
        };

        const std::string mapDir = DirName(g_app.document.sourcePath);
        const std::string projectDir = DirName(mapDir);
        addCandidate(JoinPath(JoinPath(mapDir, "txts"), textureName));
        addCandidate(JoinPath(JoinPath(projectDir, "txts"), textureName));
        addCandidate(JoinPath("txts", textureName));
        addCandidate(textureName);

        std::vector<uint8_t> test;
        for (const auto& candidate : candidates)
        {
            if (ReadFileBinaryLocal(candidate, test))
            {
                outPath = candidate;
                return true;
            }
        }
        return false;
    }

    bool LoadTexturePreviewImage(const std::string& textureName, TexturePreviewImage& outImage)
    {
        outImage = {};
        outImage.name = textureName;
        if (textureName.empty())
        {
            outImage.error = "No texture name assigned for this slot.";
            return false;
        }

        std::string resolvedPath;
        if (!ResolveTexturePath(textureName, resolvedPath))
        {
            outImage.error = "Texture file not found in txts/.";
            return false;
        }

        std::string error;
        std::vector<uint8_t> raw;
        if (!LoadMaybeCrmLocal(resolvedPath, raw, error))
        {
            outImage.error = error;
            return false;
        }

        if (raw.size() < 6)
        {
            outImage.error = "Texture file is too small.";
            return false;
        }

        const uint32_t paletteOffset = ReadBE32Local(raw.data());
        if (paletteOffset < 4 || paletteOffset >= raw.size())
        {
            outImage.error = "Texture palette offset is invalid.";
            return false;
        }

        const int columnCount = static_cast<int>((paletteOffset - 4) / 65);
        if (columnCount <= 0)
        {
            outImage.error = "Texture contains no columns.";
            return false;
        }

        std::array<std::array<uint8_t, 3>, 256> palette{};
        const uint16_t entries = (paletteOffset + 2 <= raw.size()) ? ReadBE16Local(raw.data() + paletteOffset) : 0;
        for (uint16_t p = 0; p < entries && (paletteOffset + 2 + (p * 2) + 1) < raw.size(); ++p)
        {
            const uint16_t colval = ReadBE16Local(raw.data() + paletteOffset + 2 + (p * 2));
            uint8_t r = static_cast<uint8_t>((colval >> 8) & 0xF);
            uint8_t g = static_cast<uint8_t>((colval >> 4) & 0xF);
            uint8_t b = static_cast<uint8_t>((colval >> 0) & 0xF);
            palette[p + 1][0] = static_cast<uint8_t>(r | (r << 4));
            palette[p + 1][1] = static_cast<uint8_t>(g | (g << 4));
            palette[p + 1][2] = static_cast<uint8_t>(b | (b << 4));
        }

        outImage.width = columnCount;
        outImage.height = 64;
        outImage.loadedPath = resolvedPath;
        outImage.pixels.assign(static_cast<size_t>(outImage.width) * static_cast<size_t>(outImage.height), 0xFF000000u);

        for (int x = 0; x < columnCount; ++x)
        {
            const size_t base = 4 + static_cast<size_t>(x) * 65;
            if ((base + 65) > raw.size()) break;
            const uint8_t* column = raw.data() + base + 1;
            for (int y = 0; y < 64; ++y)
            {
                const uint8_t idx = column[y];
                const auto& rgb = palette[idx];
                outImage.pixels[static_cast<size_t>(y) * static_cast<size_t>(outImage.width) + static_cast<size_t>(x)] =
                    0xFF000000u | (static_cast<uint32_t>(rgb[0]) << 16) | (static_cast<uint32_t>(rgb[1]) << 8) | static_cast<uint32_t>(rgb[2]);
            }
        }

        return true;
    }

    void AutoScrollPreviewToSelectedStrip()
    {
        if (!g_app.infoPanel) return;
        if (!g_app.previewAutoScrollPending) return;
        if (g_app.previewImage.width <= 0 || g_app.previewImage.height <= 0) return;

        RECT rc{};
        GetClientRect(g_app.infoPanel, &rc);
        const PreviewMetrics metrics = GetPreviewMetrics(rc);
        if (metrics.viewportWidth <= 0 || metrics.viewportHeight <= 0) return;

        const double scale = MaxValue(1.0, static_cast<double>(metrics.viewportHeight) / static_cast<double>(g_app.previewImage.height));
        const int stripStartColumn = ClampValue(g_app.previewTextureStrip, 0, 19) * 64;
        const int stripCenterColumn = stripStartColumn + 32;
        const int targetX = static_cast<int>(std::lround(static_cast<double>(stripCenterColumn) * scale)) - (metrics.viewportWidth / 2);
        g_app.previewScrollX = ClampValue(targetX, 0, metrics.maxScrollX);
        g_app.previewAutoScrollPending = false;
    }

    void RefreshPreviewImage()
    {
        g_app.previewImage = {};
        g_app.previewScrollX = 0;
        g_app.previewTextureIndex = 0;
        g_app.previewTextureSlot = 0;
        g_app.previewTextureStrip = 0;
        g_app.previewAutoScrollPending = false;

        if (g_app.selectedZone >= 0 && g_app.selectedZone < static_cast<int>(g_app.document.zones.size()))
        {
            const auto& zone = g_app.document.zones[g_app.selectedZone];
            const int activeSlotCount = GetZonePreviewSlotCount(zone);
            const int band = ClampValue(g_app.previewTextureBand, 0, activeSlotCount - 1);
            const int textureIndex = static_cast<int>(zone.textures[band]);
            const int slot = ClampValue(textureIndex / 20, 0, static_cast<int>(mapfmt::MapDocument::kTextureSlotCount) - 1);
            const int strip = ClampValue(textureIndex % 20, 0, 19);

            g_app.previewTextureIndex = textureIndex;
            g_app.previewTextureSlot = slot;
            g_app.previewTextureStrip = strip;

            LoadTexturePreviewImage(g_app.document.textureNames[slot], g_app.previewImage);
            g_app.previewAutoScrollPending = true;
        }

        if (g_app.infoPanel)
        {
            AutoScrollPreviewToSelectedStrip();
            UpdateInfoPanelScrollBar(g_app.infoPanel);
        }
    }

    std::wstring GetWindowTextString(HWND hwnd)
    {
        const int len = GetWindowTextLengthW(hwnd);
        std::wstring buffer(len + 1, L'\0');
        GetWindowTextW(hwnd, buffer.data(), len + 1);
        buffer.resize(len);
        return buffer;
    }

    void SetWindowTextUtf8(HWND hwnd, const std::string& text)
    {
        SetWindowTextW(hwnd, Utf8ToWide(text).c_str());
    }

    std::string GetOpenOrSavePath(HWND owner, bool saveMode, const wchar_t* title, const wchar_t* defaultExt = L"map")
    {
        std::array<wchar_t, MAX_PATH> buffer{};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = owner;
        ofn.lpstrFilter = L"Map Files\0*.*\0All Files\0*.*\0\0";
        ofn.lpstrFile = buffer.data();
        ofn.nMaxFile = static_cast<DWORD>(buffer.size());
        ofn.lpstrTitle = title;
        ofn.lpstrDefExt = defaultExt;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_EXPLORER;
        if (saveMode)
        {
            ofn.Flags |= OFN_OVERWRITEPROMPT;
            if (!GetSaveFileNameW(&ofn)) return {};
        }
        else
        {
            ofn.Flags |= OFN_FILEMUSTEXIST;
            if (!GetOpenFileNameW(&ofn)) return {};
        }
        return WideToUtf8(std::wstring(buffer.data()));
    }

    void UpdateTitle()
    {
        std::wstring title = L"ZGloom Editor";
        if (!g_app.document.sourcePath.empty())
        {
            title += L" - ";
            const auto widePath = Utf8ToWide(g_app.document.sourcePath);
            const size_t slash = widePath.find_last_of(L"\\/");
            title += (slash == std::wstring::npos) ? widePath : widePath.substr(slash + 1);
        }
        else
        {
            title += L" - Untitled";
        }
        if (g_app.document.dirty) title += L" *";
        SetWindowTextW(g_app.mainWindow, title.c_str());
    }

    std::wstring ZoneTypeToText(int ztype)
    {
        switch (static_cast<mapfmt::ZoneType>(ztype))
        {
        case mapfmt::ZoneType::Wall: return L"Wall";
        case mapfmt::ZoneType::MonsterZone: return L"Monster Zone";
        case mapfmt::ZoneType::EventTrigger: return L"Event Trigger";
        default: return L"Unknown";
        }
    }

    std::wstring ZoneToListText(const mapfmt::Zone& zone, int index)
    {
        std::wstringstream ss;
        ss << L"[" << index << L"] " << ZoneTypeToText(zone.ztype)
           << L"  E" << zone.ev
           << L"  (" << zone.x1 << L"," << zone.z1 << L") -> (" << zone.x2 << L"," << zone.z2 << L")";
        return ss.str();
    }

    void RefreshStatus()
    {
        if (!g_app.statusBar) return;
        std::wstringstream ss;
        if (g_app.selectedZone >= 0 && g_app.selectedZone < static_cast<int>(g_app.document.zones.size()))
        {
            const auto& zone = g_app.document.zones[g_app.selectedZone];
            ss << L"Selected zone " << g_app.selectedZone
               << L"  Type: " << ZoneTypeToText(zone.ztype)
               << L"  Event: " << zone.ev
               << L"  Mode: " << InsertModeToText(g_app.insertMode)
               << L"  Zoom: " << static_cast<int>(std::lround(g_app.canvasZoom * 100.0)) << L"%";
        }
        else
        {
            ss << L"Ready  Mode: " << InsertModeToText(g_app.insertMode)
               << L"  Zoom: " << static_cast<int>(std::lround(g_app.canvasZoom * 100.0)) << L"%";
        }

        if (g_app.isDrawing)
        {
            ss << L"  Drag on the map to place the new zone";
        }
        else if (g_app.insertMode != InsertMode::None)
        {
            ss << L"  Click and drag on the canvas to draw";
        }

        const std::wstring text = ss.str();
        SendMessageW(g_app.statusBar, SB_SETTEXT, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }

    void RefreshZoneList()
    {
        SendMessageW(g_app.zoneList, LB_RESETCONTENT, 0, 0);
        for (int i = 0; i < static_cast<int>(g_app.document.zones.size()); ++i)
        {
            const auto text = ZoneToListText(g_app.document.zones[i], i);
            SendMessageW(g_app.zoneList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        }

        if (g_app.selectedZone >= static_cast<int>(g_app.document.zones.size()))
        {
            g_app.selectedZone = static_cast<int>(g_app.document.zones.size()) - 1;
        }
        if (g_app.selectedZone >= 0)
        {
            SendMessageW(g_app.zoneList, LB_SETCURSEL, g_app.selectedZone, 0);
        }
        RefreshPreviewImage();
        RefreshStatus();
        InvalidateEditorViews();
    }

    void MarkDirty()
    {
        g_app.document.dirty = true;
        UpdateTitle();
    }

    bool ConfirmDiscardChanges()
    {
        if (!g_app.document.dirty) return true;
        const int result = MessageBoxW(
            g_app.mainWindow,
            L"The current document has unsaved changes. Discard them?",
            L"ZGloom Editor",
            MB_ICONWARNING | MB_YESNO);
        return result == IDYES;
    }

    bool SaveDocument(bool saveAs)
    {
        std::string path = g_app.document.sourcePath;
        if (saveAs || path.empty())
        {
            path = GetOpenOrSavePath(g_app.mainWindow, true, L"Save Map As");
            if (path.empty()) return false;
        }

        std::string error;
        if (!g_app.document.SaveToFile(path, error))
        {
            MessageBoxW(g_app.mainWindow, Utf8ToWide(error).c_str(), L"Save Failed", MB_OK | MB_ICONERROR);
            return false;
        }

        g_app.document.sourcePath = path;
        g_app.document.dirty = false;
        UpdateTitle();
        RefreshStatus();
        return true;
    }

    void NewDocument()
    {
        if (!ConfirmDiscardChanges()) return;
        g_app.document.NewBlank();
        g_app.selectedZone = -1;
        g_app.previewTextureBand = 0;
        g_app.isDrawing = false;
        FitViewToDocument();
        RefreshZoneList();
        UpdateTitle();
    }

    void OpenDocument()
    {
        if (!ConfirmDiscardChanges()) return;
        const std::string path = GetOpenOrSavePath(g_app.mainWindow, false, L"Open Map");
        if (path.empty()) return;

        std::string error;
        if (!g_app.document.LoadFromFile(path, error))
        {
            MessageBoxW(g_app.mainWindow, Utf8ToWide(error).c_str(), L"Open Failed", MB_OK | MB_ICONERROR);
            return;
        }

        g_app.selectedZone = g_app.document.zones.empty() ? -1 : 0;
        g_app.previewTextureBand = 0;
        g_app.isDrawing = false;
        FitViewToDocument();
        RefreshZoneList();
        UpdateTitle();
    }

    void ExportSvg()
    {
        const std::string path = GetOpenOrSavePath(g_app.mainWindow, true, L"Export SVG Overview", L"svg");
        if (path.empty()) return;

        std::string error;
        if (!g_app.document.ExportSvg(path, error))
        {
            MessageBoxW(g_app.mainWindow, Utf8ToWide(error).c_str(), L"SVG Export Failed", MB_OK | MB_ICONERROR);
            return;
        }

        MessageBoxW(g_app.mainWindow, L"SVG export completed.", L"ZGloom Editor", MB_OK | MB_ICONINFORMATION);
    }

    int ReadIntFromDialog(HWND dlg, int controlId)
    {
        return GetDlgItemInt(dlg, controlId, nullptr, TRUE);
    }

    void SetIntToDialog(HWND dlg, int controlId, int value)
    {
        SetDlgItemInt(dlg, controlId, value, TRUE);
    }

    struct ZoneDialogState
    {
        mapfmt::Zone zone;
        bool accepted = false;
        std::wstring caption = L"Zone";
    };

    INT_PTR CALLBACK ZoneDialogProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<ZoneDialogState*>(GetWindowLongPtrW(dlg, DWLP_USER));
        if (msg == WM_INITDIALOG)
        {
            state = reinterpret_cast<ZoneDialogState*>(lParam);
            SetWindowLongPtrW(dlg, DWLP_USER, lParam);
            SetWindowTextW(dlg, state->caption.c_str());

            SendDlgItemMessageW(dlg, IDC_ZONE_TYPE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Wall"));
            SendDlgItemMessageW(dlg, IDC_ZONE_TYPE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Monster Zone"));
            SendDlgItemMessageW(dlg, IDC_ZONE_TYPE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Event Trigger"));
            SendDlgItemMessageW(dlg, IDC_ZONE_TYPE, CB_SETCURSEL, MaxValue(0, MinValue(2, state->zone.ztype - 1)), 0);

            SetIntToDialog(dlg, IDC_ZONE_X1, state->zone.x1);
            SetIntToDialog(dlg, IDC_ZONE_Z1, state->zone.z1);
            SetIntToDialog(dlg, IDC_ZONE_X2, state->zone.x2);
            SetIntToDialog(dlg, IDC_ZONE_Z2, state->zone.z2);
            SetIntToDialog(dlg, IDC_ZONE_A, state->zone.a);
            SetIntToDialog(dlg, IDC_ZONE_B, state->zone.b);
            SetIntToDialog(dlg, IDC_ZONE_NA, state->zone.na);
            SetIntToDialog(dlg, IDC_ZONE_NB, state->zone.nb);
            SetIntToDialog(dlg, IDC_ZONE_LN, state->zone.ln);
            SetIntToDialog(dlg, IDC_ZONE_SC, state->zone.sc);
            SetIntToDialog(dlg, IDC_ZONE_EV, state->zone.ev);
            const int texIds[8] = { IDC_ZONE_T0, IDC_ZONE_T1, IDC_ZONE_T2, IDC_ZONE_T3, IDC_ZONE_T4, IDC_ZONE_T5, IDC_ZONE_T6, IDC_ZONE_T7 };
            for (int i = 0; i < 8; ++i) SetIntToDialog(dlg, texIds[i], state->zone.textures[i]);
            return TRUE;
        }

        if (!state) return FALSE;

        switch (msg)
        {
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IDOK:
            {
                state->zone.ztype = static_cast<int16_t>(SendDlgItemMessageW(dlg, IDC_ZONE_TYPE, CB_GETCURSEL, 0, 0) + 1);
                state->zone.x1 = static_cast<int16_t>(ReadIntFromDialog(dlg, IDC_ZONE_X1));
                state->zone.z1 = static_cast<int16_t>(ReadIntFromDialog(dlg, IDC_ZONE_Z1));
                state->zone.x2 = static_cast<int16_t>(ReadIntFromDialog(dlg, IDC_ZONE_X2));
                state->zone.z2 = static_cast<int16_t>(ReadIntFromDialog(dlg, IDC_ZONE_Z2));
                state->zone.a = static_cast<int16_t>(ReadIntFromDialog(dlg, IDC_ZONE_A));
                state->zone.b = static_cast<int16_t>(ReadIntFromDialog(dlg, IDC_ZONE_B));
                state->zone.na = static_cast<int16_t>(ReadIntFromDialog(dlg, IDC_ZONE_NA));
                state->zone.nb = static_cast<int16_t>(ReadIntFromDialog(dlg, IDC_ZONE_NB));
                state->zone.ln = static_cast<int16_t>(ReadIntFromDialog(dlg, IDC_ZONE_LN));
                state->zone.sc = static_cast<int16_t>(ReadIntFromDialog(dlg, IDC_ZONE_SC));
                state->zone.ev = static_cast<int16_t>(ReadIntFromDialog(dlg, IDC_ZONE_EV));
                const int texIds[8] = { IDC_ZONE_T0, IDC_ZONE_T1, IDC_ZONE_T2, IDC_ZONE_T3, IDC_ZONE_T4, IDC_ZONE_T5, IDC_ZONE_T6, IDC_ZONE_T7 };
                for (int i = 0; i < 8; ++i)
                {
                    const int value = ReadIntFromDialog(dlg, texIds[i]);
                    state->zone.textures[i] = static_cast<uint8_t>(MaxValue(0, MinValue(255, value)));
                }
                state->accepted = true;
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            case IDCANCEL:
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
        return FALSE;
    }

    bool ShowZoneDialog(mapfmt::Zone& zone, const std::wstring& caption)
    {
        ZoneDialogState state;
        state.zone = zone;
        state.caption = caption;
        if (DialogBoxParamW(g_app.instance, MAKEINTRESOURCEW(IDD_ZONE), g_app.mainWindow, ZoneDialogProc, reinterpret_cast<LPARAM>(&state)) == IDOK && state.accepted)
        {
            zone = state.zone;
            return true;
        }
        return false;
    }

    struct CommandDialogState
    {
        mapfmt::EventCommand command;
        bool accepted = false;
    };

    void UpdateCommandDialogLayout(HWND dlg)
    {
        const auto type = static_cast<mapfmt::CommandType>(SendDlgItemMessageW(dlg, IDC_CMD_TYPE, CB_GETCURSEL, 0, 0) + 1);
        const int labels[5] = { IDC_CMD_L1, IDC_CMD_L2, IDC_CMD_L3, IDC_CMD_L4, IDC_CMD_L5 };
        const int edits[5] = { IDC_CMD_V1, IDC_CMD_V2, IDC_CMD_V3, IDC_CMD_V4, IDC_CMD_V5 };

        auto setLabel = [&](int index, const wchar_t* text, bool visible)
        {
            SetDlgItemTextW(dlg, labels[index], text);
            ShowWindow(GetDlgItem(dlg, labels[index]), visible ? SW_SHOW : SW_HIDE);
            ShowWindow(GetDlgItem(dlg, edits[index]), visible ? SW_SHOW : SW_HIDE);
        };

        ShowWindow(GetDlgItem(dlg, IDC_CMD_RAW_LABEL), SW_HIDE);
        ShowWindow(GetDlgItem(dlg, IDC_CMD_RAW), SW_HIDE);

        switch (type)
        {
        case mapfmt::CommandType::AddMonster:
            setLabel(0, L"Object type", true);
            setLabel(1, L"X", true);
            setLabel(2, L"Y", true);
            setLabel(3, L"Z", true);
            setLabel(4, L"Rotation", true);
            break;
        case mapfmt::CommandType::OpenDoor:
            setLabel(0, L"Zone index", true);
            setLabel(1, L"", false);
            setLabel(2, L"", false);
            setLabel(3, L"", false);
            setLabel(4, L"", false);
            break;
        case mapfmt::CommandType::Teleport:
            setLabel(0, L"X", true);
            setLabel(1, L"Y", true);
            setLabel(2, L"Z", true);
            setLabel(3, L"Rotation", true);
            setLabel(4, L"", false);
            break;
        case mapfmt::CommandType::LoadObjects:
            setLabel(0, L"", false);
            setLabel(1, L"", false);
            setLabel(2, L"", false);
            setLabel(3, L"", false);
            setLabel(4, L"", false);
            ShowWindow(GetDlgItem(dlg, IDC_CMD_RAW_LABEL), SW_SHOW);
            ShowWindow(GetDlgItem(dlg, IDC_CMD_RAW), SW_SHOW);
            break;
        case mapfmt::CommandType::ChangeTexture:
            setLabel(0, L"Zone index", true);
            setLabel(1, L"New texture", true);
            setLabel(2, L"", false);
            setLabel(3, L"", false);
            setLabel(4, L"", false);
            break;
        case mapfmt::CommandType::RotatePoly:
            setLabel(0, L"Polygon / zone", true);
            setLabel(1, L"Count", true);
            setLabel(2, L"Speed", true);
            setLabel(3, L"Flags", true);
            setLabel(4, L"", false);
            break;
        default:
            break;
        }
    }

    std::vector<int16_t> ParseIntList(const std::wstring& text)
    {
        std::wstring normalized = text;
        for (auto& ch : normalized)
        {
            if (ch == L',' || ch == L';' || ch == L'\n' || ch == L'\r' || ch == L'\t') ch = L' ';
        }
        std::wstringstream ss(normalized);
        std::vector<int16_t> values;
        int value = 0;
        while (ss >> value)
        {
            values.push_back(static_cast<int16_t>(value));
        }
        return values;
    }

    INT_PTR CALLBACK CommandDialogProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<CommandDialogState*>(GetWindowLongPtrW(dlg, DWLP_USER));
        if (msg == WM_INITDIALOG)
        {
            state = reinterpret_cast<CommandDialogState*>(lParam);
            SetWindowLongPtrW(dlg, DWLP_USER, lParam);

            const wchar_t* items[] = {
                L"Add Monster",
                L"Open Door",
                L"Teleport",
                L"Load Objects",
                L"Change Texture",
                L"Rotate Poly"
            };
            for (const wchar_t* item : items)
            {
                SendDlgItemMessageW(dlg, IDC_CMD_TYPE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
            }
            SendDlgItemMessageW(dlg, IDC_CMD_TYPE, CB_SETCURSEL, MaxValue(0, static_cast<int>(state->command.type) - 1), 0);
            SetIntToDialog(dlg, IDC_CMD_V1, state->command.params[0]);
            SetIntToDialog(dlg, IDC_CMD_V2, state->command.params[1]);
            SetIntToDialog(dlg, IDC_CMD_V3, state->command.params[2]);
            SetIntToDialog(dlg, IDC_CMD_V4, state->command.params[3]);
            SetIntToDialog(dlg, IDC_CMD_V5, state->command.params[4]);

            std::wstringstream raw;
            for (size_t i = 0; i < state->command.listValues.size(); ++i)
            {
                if (i) raw << L", ";
                raw << state->command.listValues[i];
            }
            SetDlgItemTextW(dlg, IDC_CMD_RAW, raw.str().c_str());
            UpdateCommandDialogLayout(dlg);
            return TRUE;
        }

        if (!state) return FALSE;

        switch (msg)
        {
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IDC_CMD_TYPE:
                if (HIWORD(wParam) == CBN_SELCHANGE)
                {
                    UpdateCommandDialogLayout(dlg);
                }
                break;
            case IDOK:
            {
                state->command.type = static_cast<mapfmt::CommandType>(SendDlgItemMessageW(dlg, IDC_CMD_TYPE, CB_GETCURSEL, 0, 0) + 1);
                state->command.params[0] = static_cast<int16_t>(ReadIntFromDialog(dlg, IDC_CMD_V1));
                state->command.params[1] = static_cast<int16_t>(ReadIntFromDialog(dlg, IDC_CMD_V2));
                state->command.params[2] = static_cast<int16_t>(ReadIntFromDialog(dlg, IDC_CMD_V3));
                state->command.params[3] = static_cast<int16_t>(ReadIntFromDialog(dlg, IDC_CMD_V4));
                state->command.params[4] = static_cast<int16_t>(ReadIntFromDialog(dlg, IDC_CMD_V5));
                state->command.listValues = ParseIntList(GetWindowTextString(GetDlgItem(dlg, IDC_CMD_RAW)));
                state->accepted = true;
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            case IDCANCEL:
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
        return FALSE;
    }

    bool ShowCommandDialog(mapfmt::EventCommand& command)
    {
        CommandDialogState state;
        state.command = command;
        if (DialogBoxParamW(g_app.instance, MAKEINTRESOURCEW(IDD_COMMAND), g_app.mainWindow, CommandDialogProc, reinterpret_cast<LPARAM>(&state)) == IDOK && state.accepted)
        {
            command = state.command;
            return true;
        }
        return false;
    }

    struct TextureDialogState
    {
        std::array<std::string, mapfmt::MapDocument::kTextureSlotCount> names;
        bool accepted = false;
    };

    INT_PTR CALLBACK TextureDialogProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<TextureDialogState*>(GetWindowLongPtrW(dlg, DWLP_USER));
        if (msg == WM_INITDIALOG)
        {
            state = reinterpret_cast<TextureDialogState*>(lParam);
            SetWindowLongPtrW(dlg, DWLP_USER, lParam);
            const int ids[8] = { IDC_TEX_0, IDC_TEX_1, IDC_TEX_2, IDC_TEX_3, IDC_TEX_4, IDC_TEX_5, IDC_TEX_6, IDC_TEX_7 };
            for (int i = 0; i < 8; ++i)
            {
                SetWindowTextUtf8(GetDlgItem(dlg, ids[i]), state->names[i]);
            }
            return TRUE;
        }

        if (!state) return FALSE;

        switch (msg)
        {
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IDOK:
            {
                const int ids[8] = { IDC_TEX_0, IDC_TEX_1, IDC_TEX_2, IDC_TEX_3, IDC_TEX_4, IDC_TEX_5, IDC_TEX_6, IDC_TEX_7 };
                for (int i = 0; i < 8; ++i)
                {
                    state->names[i] = WideToUtf8(GetWindowTextString(GetDlgItem(dlg, ids[i])));
                }
                state->accepted = true;
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            case IDCANCEL:
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
        return FALSE;
    }

    void ShowTextureDialog()
    {
        TextureDialogState state;
        state.names = g_app.document.textureNames;
        if (DialogBoxParamW(g_app.instance, MAKEINTRESOURCEW(IDD_MAPSETTINGS), g_app.mainWindow, TextureDialogProc, reinterpret_cast<LPARAM>(&state)) == IDOK && state.accepted)
        {
            g_app.document.textureNames = state.names;
            MarkDirty();
            RefreshPreviewImage();
            InvalidateEditorViews();
        }
    }

    struct EventDialogState
    {
        mapfmt::MapDocument* document = nullptr;
        int slotIndex = 0;
        bool changed = false;
    };

    void PopulateEventCommandList(HWND dlg, EventDialogState* state)
    {
        HWND list = GetDlgItem(dlg, IDC_EVENT_COMMANDS);
        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        if (!state || !state->document) return;
        const auto& commands = state->document->events[state->slotIndex].commands;
        for (const auto& cmd : commands)
        {
            const auto text = Utf8ToWide(cmd.ToDisplayString());
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        }
    }

    INT_PTR CALLBACK EventDialogProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<EventDialogState*>(GetWindowLongPtrW(dlg, DWLP_USER));
        if (msg == WM_INITDIALOG)
        {
            state = reinterpret_cast<EventDialogState*>(lParam);
            SetWindowLongPtrW(dlg, DWLP_USER, lParam);
            for (int i = 0; i < mapfmt::MapDocument::kEventCount; ++i)
            {
                std::wstringstream ss;
                ss << (i + 1);
                SendDlgItemMessageW(dlg, IDC_EVENT_SLOT, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ss.str().c_str()));
            }
            SendDlgItemMessageW(dlg, IDC_EVENT_SLOT, CB_SETCURSEL, state->slotIndex, 0);
            PopulateEventCommandList(dlg, state);
            return TRUE;
        }

        if (!state) return FALSE;

        switch (msg)
        {
        case WM_COMMAND:
        {
            HWND list = GetDlgItem(dlg, IDC_EVENT_COMMANDS);
            auto& commands = state->document->events[state->slotIndex].commands;
            switch (LOWORD(wParam))
            {
            case IDC_EVENT_SLOT:
                if (HIWORD(wParam) == CBN_SELCHANGE)
                {
                    state->slotIndex = static_cast<int>(SendDlgItemMessageW(dlg, IDC_EVENT_SLOT, CB_GETCURSEL, 0, 0));
                    PopulateEventCommandList(dlg, state);
                }
                break;
            case IDC_EVENT_ADD:
            {
                mapfmt::EventCommand cmd;
                cmd.type = mapfmt::CommandType::AddMonster;
                if (ShowCommandDialog(cmd))
                {
                    commands.push_back(cmd);
                    state->changed = true;
                    PopulateEventCommandList(dlg, state);
                    SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(commands.size() - 1), 0);
                }
                break;
            }
            case IDC_EVENT_EDIT:
            {
                const int sel = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
                if (sel >= 0 && sel < static_cast<int>(commands.size()))
                {
                    auto cmd = commands[sel];
                    if (ShowCommandDialog(cmd))
                    {
                        commands[sel] = cmd;
                        state->changed = true;
                        PopulateEventCommandList(dlg, state);
                        SendMessageW(list, LB_SETCURSEL, sel, 0);
                    }
                }
                break;
            }
            case IDC_EVENT_DELETE:
            {
                const int sel = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
                if (sel >= 0 && sel < static_cast<int>(commands.size()))
                {
                    commands.erase(commands.begin() + sel);
                    state->changed = true;
                    PopulateEventCommandList(dlg, state);
                }
                break;
            }
            case IDC_EVENT_UP:
            {
                const int sel = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
                if (sel > 0 && sel < static_cast<int>(commands.size()))
                {
                    std::swap(commands[sel], commands[sel - 1]);
                    state->changed = true;
                    PopulateEventCommandList(dlg, state);
                    SendMessageW(list, LB_SETCURSEL, sel - 1, 0);
                }
                break;
            }
            case IDC_EVENT_DOWN:
            {
                const int sel = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
                if (sel >= 0 && sel + 1 < static_cast<int>(commands.size()))
                {
                    std::swap(commands[sel], commands[sel + 1]);
                    state->changed = true;
                    PopulateEventCommandList(dlg, state);
                    SendMessageW(list, LB_SETCURSEL, sel + 1, 0);
                }
                break;
            }
            case IDOK:
            case IDCANCEL:
                EndDialog(dlg, LOWORD(wParam));
                return TRUE;
            }
            break;
        }
        }
        return FALSE;
    }

    void ShowEventEditor()
    {
        EventDialogState state;
        state.document = &g_app.document;
        state.slotIndex = 0;
        if (DialogBoxParamW(g_app.instance, MAKEINTRESOURCEW(IDD_EVENT), g_app.mainWindow, EventDialogProc, reinterpret_cast<LPARAM>(&state)) >= 0 && state.changed)
        {
            MarkDirty();
            InvalidateEditorViews();
        }
    }

    void AddZone(int zoneType)
    {
        mapfmt::Zone zone;
        zone.ztype = static_cast<int16_t>(zoneType);
        zone.x1 = 0;
        zone.z1 = 0;
        zone.x2 = (zoneType == static_cast<int>(mapfmt::ZoneType::Wall)) ? 1024 : 2048;
        zone.z2 = (zoneType == static_cast<int>(mapfmt::ZoneType::Wall)) ? 0 : 2048;
        if (ShowZoneDialog(zone, L"Add Zone"))
        {
            g_app.document.zones.push_back(zone);
            g_app.selectedZone = static_cast<int>(g_app.document.zones.size()) - 1;
            MarkDirty();
            RefreshZoneList();
        }
    }

    void EditSelectedZone()
    {
        if (g_app.selectedZone < 0 || g_app.selectedZone >= static_cast<int>(g_app.document.zones.size())) return;
        auto zone = g_app.document.zones[g_app.selectedZone];
        if (ShowZoneDialog(zone, L"Edit Zone"))
        {
            g_app.document.zones[g_app.selectedZone] = zone;
            MarkDirty();
            RefreshZoneList();
        }
    }

    void DeleteSelectedZone()
    {
        if (g_app.selectedZone < 0 || g_app.selectedZone >= static_cast<int>(g_app.document.zones.size())) return;
        if (MessageBoxW(g_app.mainWindow, L"Delete the selected zone?", L"ZGloom Editor", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
        g_app.document.zones.erase(g_app.document.zones.begin() + g_app.selectedZone);
        if (g_app.selectedZone >= static_cast<int>(g_app.document.zones.size())) g_app.selectedZone = static_cast<int>(g_app.document.zones.size()) - 1;
        MarkDirty();
        RefreshZoneList();
    }

    void MoveZoneUp()
    {
        if (g_app.selectedZone > 0)
        {
            std::swap(g_app.document.zones[g_app.selectedZone], g_app.document.zones[g_app.selectedZone - 1]);
            --g_app.selectedZone;
            MarkDirty();
            RefreshZoneList();
        }
    }

    void MoveZoneDown()
    {
        if (g_app.selectedZone >= 0 && g_app.selectedZone + 1 < static_cast<int>(g_app.document.zones.size()))
        {
            std::swap(g_app.document.zones[g_app.selectedZone], g_app.document.zones[g_app.selectedZone + 1]);
            ++g_app.selectedZone;
            MarkDirty();
            RefreshZoneList();
        }
    }

    void ShowValidationReport()
    {
        const auto issues = g_app.document.Validate();
        std::wstringstream ss;
        for (const auto& item : issues)
        {
            ss << Utf8ToWide(item) << L"\r\n";
        }
        MessageBoxW(g_app.mainWindow, ss.str().c_str(), L"Validation", MB_OK | MB_ICONINFORMATION);
    }

    HMENU BuildMainMenu()
    {
        HMENU menuBar = CreateMenu();
        HMENU fileMenu = CreatePopupMenu();
        HMENU editMenu = CreatePopupMenu();
        HMENU insertMenu = CreatePopupMenu();
        HMENU mapMenu = CreatePopupMenu();
        HMENU toolsMenu = CreatePopupMenu();
        HMENU helpMenu = CreatePopupMenu();

        AppendMenuW(fileMenu, MF_STRING, IDM_FILE_NEW, L"&New");
        AppendMenuW(fileMenu, MF_STRING, IDM_FILE_OPEN, L"&Open...");
        AppendMenuW(fileMenu, MF_STRING, IDM_FILE_SAVE, L"&Save");
        AppendMenuW(fileMenu, MF_STRING, IDM_FILE_SAVE_AS, L"Save &As...");
        AppendMenuW(fileMenu, MF_STRING, IDM_FILE_EXPORT_SVG, L"Export &SVG Overview...");
        AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileMenu, MF_STRING, IDM_FILE_EXIT, L"E&xit");

        AppendMenuW(editMenu, MF_STRING, IDM_EDIT_ZONE, L"&Edit Selected Zone...");
        AppendMenuW(editMenu, MF_STRING, IDM_EDIT_DELETE, L"&Delete Selected Zone");
        AppendMenuW(editMenu, MF_STRING, IDM_EDIT_MOVE_UP, L"Move Zone &Up");
        AppendMenuW(editMenu, MF_STRING, IDM_EDIT_MOVE_DOWN, L"Move Zone &Down");

        AppendMenuW(insertMenu, MF_STRING, IDM_INSERT_WALL, L"Draw &Wall");
        AppendMenuW(insertMenu, MF_STRING, IDM_INSERT_MONSTER_ZONE, L"Draw &Monster Zone");
        AppendMenuW(insertMenu, MF_STRING, IDM_INSERT_EVENT_TRIGGER, L"Draw &Event Trigger");

        AppendMenuW(mapMenu, MF_STRING, IDM_MAP_EVENTS, L"&Events...");
        AppendMenuW(mapMenu, MF_STRING, IDM_MAP_TEXTURES, L"&Texture Slots...");

        AppendMenuW(toolsMenu, MF_STRING, IDM_TOOLS_VALIDATE, L"&Validate Map");
        AppendMenuW(helpMenu, MF_STRING, IDM_HELP_ABOUT, L"&About");

        AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), L"&File");
        AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(editMenu), L"&Edit");
        AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(insertMenu), L"&Insert");
        AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(mapMenu), L"&Map");
        AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(toolsMenu), L"&Tools");
        AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(helpMenu), L"&Help");
        return menuBar;
    }

    void LayoutChildren(HWND hwnd)
    {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        RECT statusRc{};
        SendMessageW(g_app.statusBar, WM_SIZE, 0, 0);
        GetWindowRect(g_app.statusBar, &statusRc);
        const int statusHeight = statusRc.bottom - statusRc.top;

        const int padding = 8;
        const int buttonHeight = 28;
        const int leftWidth = 372;
        const int infoWidth = 320;
        const int contentHeight = rc.bottom - statusHeight;
        const int listHeight = MaxValue(96, contentHeight - (buttonHeight * 5) - (padding * 7));

        MoveWindow(g_app.zoneList, padding, padding, leftWidth - (padding * 2), listHeight, TRUE);

        int y = padding + listHeight + padding;
        const int buttonWidth = (leftWidth - (padding * 3)) / 2;
        MoveWindow(g_app.btnAddWall, padding, y, buttonWidth, buttonHeight, TRUE);
        MoveWindow(g_app.btnAddMonster, padding * 2 + buttonWidth, y, buttonWidth, buttonHeight, TRUE);
        y += buttonHeight + padding;
        MoveWindow(g_app.btnAddTrigger, padding, y, buttonWidth, buttonHeight, TRUE);
        MoveWindow(g_app.btnEdit, padding * 2 + buttonWidth, y, buttonWidth, buttonHeight, TRUE);
        y += buttonHeight + padding;
        MoveWindow(g_app.btnDelete, padding, y, buttonWidth, buttonHeight, TRUE);
        MoveWindow(g_app.btnEvents, padding * 2 + buttonWidth, y, buttonWidth, buttonHeight, TRUE);
        y += buttonHeight + padding;
        MoveWindow(g_app.btnUp, padding, y, buttonWidth, buttonHeight, TRUE);
        MoveWindow(g_app.btnDown, padding * 2 + buttonWidth, y, buttonWidth, buttonHeight, TRUE);
        y += buttonHeight + padding;
        MoveWindow(g_app.btnTextures, padding, y, leftWidth - (padding * 2), buttonHeight, TRUE);

        const int rightAreaX = leftWidth;
        const int rightAreaWidth = MaxValue(280, rc.right - rightAreaX - padding);
        const int desiredInfoWidth = leftWidth;
        const int actualInfoWidth = MinValue(desiredInfoWidth, MaxValue(240, rightAreaWidth - 240));
        const int canvasWidth = MaxValue(180, rightAreaWidth - actualInfoWidth - padding);
        const int panelHeight = rc.bottom - statusHeight - (padding * 2);

        MoveWindow(g_app.canvas, rightAreaX, padding, canvasWidth, panelHeight, TRUE);
        MoveWindow(g_app.infoPanel, rightAreaX + canvasWidth + padding, padding, actualInfoWidth - padding, panelHeight, TRUE);
        UpdateInfoPanelScrollBar(g_app.infoPanel);
    }

    POINT WorldToScreen(const RECT& rc, const mapfmt::Bounds& bounds, int x, int z)
    {
        const CanvasMetrics metrics = GetCanvasMetrics(rc, bounds);
        POINT pt{};
        pt.x = static_cast<LONG>(std::lround(metrics.originX + (static_cast<double>(x) - static_cast<double>(metrics.viewBounds.minX)) * metrics.scale));
        pt.y = static_cast<LONG>(std::lround(metrics.originY + (static_cast<double>(z) - static_cast<double>(metrics.viewBounds.minZ)) * metrics.scale));
        return pt;
    }

    POINT ScreenToWorld(const RECT& rc, const mapfmt::Bounds& bounds, int sx, int sy)
    {
        const CanvasMetrics metrics = GetCanvasMetrics(rc, bounds);
        POINT pt{};
        pt.x = static_cast<LONG>(std::lround(static_cast<double>(metrics.viewBounds.minX) + (static_cast<double>(sx) - metrics.originX) / metrics.scale));
        pt.y = static_cast<LONG>(std::lround(static_cast<double>(metrics.viewBounds.minZ) + (static_cast<double>(sy) - metrics.originY) / metrics.scale));
        return pt;
    }

    int SnapWorldCoordinate(int value)
    {
        const double step = static_cast<double>(kGridStep);
        return static_cast<int>(std::lround(static_cast<double>(value) / step) * step);
    }

    mapfmt::Zone BuildZoneFromDrawPoints(InsertMode mode, POINT a, POINT b)
    {
        mapfmt::Zone zone;
        if (g_app.selectedZone >= 0 && g_app.selectedZone < static_cast<int>(g_app.document.zones.size()))
        {
            zone = g_app.document.zones[g_app.selectedZone];
        }

        zone.ztype = static_cast<int16_t>(
            mode == InsertMode::Wall ? static_cast<int>(mapfmt::ZoneType::Wall) :
            mode == InsertMode::MonsterZone ? static_cast<int>(mapfmt::ZoneType::MonsterZone) :
            static_cast<int>(mapfmt::ZoneType::EventTrigger));

        a.x = SnapWorldCoordinate(a.x);
        a.y = SnapWorldCoordinate(a.y);
        b.x = SnapWorldCoordinate(b.x);
        b.y = SnapWorldCoordinate(b.y);

        if (mode == InsertMode::Wall)
        {
            zone.x1 = static_cast<int16_t>(a.x);
            zone.z1 = static_cast<int16_t>(a.y);
            zone.x2 = static_cast<int16_t>(b.x);
            zone.z2 = static_cast<int16_t>(b.y);
            if (zone.x1 == zone.x2 && zone.z1 == zone.z2)
            {
                zone.x2 = static_cast<int16_t>(zone.x2 + 1024);
            }
        }
        else
        {
            int x1 = MinValue(a.x, b.x);
            int x2 = MaxValue(a.x, b.x);
            int z1 = MinValue(a.y, b.y);
            int z2 = MaxValue(a.y, b.y);
            if (x1 == x2) x2 += 1024;
            if (z1 == z2) z2 += 1024;
            zone.x1 = static_cast<int16_t>(x1);
            zone.z1 = static_cast<int16_t>(z1);
            zone.x2 = static_cast<int16_t>(x2);
            zone.z2 = static_cast<int16_t>(z2);
        }

        return zone;
    }

    void CommitDrawnZone()
    {
        if (!g_app.isDrawing || g_app.insertMode == InsertMode::None) return;
        const auto zone = BuildZoneFromDrawPoints(g_app.insertMode, g_app.drawStartWorld, g_app.drawCurrentWorld);
        g_app.document.zones.push_back(zone);
        g_app.selectedZone = static_cast<int>(g_app.document.zones.size()) - 1;
        g_app.previewTextureBand = 0;
        g_app.isDrawing = false;
        MarkDirty();
        RefreshZoneList();
    }

    void UpdatePreviewTextureBandFromPoint(const RECT& rc, int sx, int sy)
    {
        if (g_app.selectedZone < 0 || g_app.selectedZone >= static_cast<int>(g_app.document.zones.size())) return;
        const auto bounds = g_app.document.ComputeBounds();
        const auto& zone = g_app.document.zones[g_app.selectedZone];
        POINT p1 = WorldToScreen(rc, bounds, zone.x1, zone.z1);
        POINT p2 = WorldToScreen(rc, bounds, zone.x2, zone.z2);
        int band = 0;
        const int activeSlotCount = GetZonePreviewSlotCount(zone);

        if (zone.ztype == static_cast<int>(mapfmt::ZoneType::Wall))
        {
            const double vx = static_cast<double>(p2.x - p1.x);
            const double vy = static_cast<double>(p2.y - p1.y);
            const double len2 = vx * vx + vy * vy;
            if (len2 > 0.0)
            {
                const double t = ClampValue(((static_cast<double>(sx - p1.x) * vx) + (static_cast<double>(sy - p1.y) * vy)) / len2, 0.0, 0.999999);
                band = static_cast<int>(t * static_cast<double>(activeSlotCount));
            }
        }
        else
        {
            const int left = MinValue(p1.x, p2.x);
            const int right = MaxValue(p1.x, p2.x);
            const int width = MaxValue(1, right - left + 1);
            const double t = ClampValue(static_cast<double>(sx - left) / static_cast<double>(width), 0.0, 0.999999);
            band = static_cast<int>(t * static_cast<double>(activeSlotCount));
        }

        g_app.previewTextureBand = ClampValue(band, 0, activeSlotCount - 1);
        RefreshPreviewImage();
    }

    double DistancePointToSegment(double px, double py, double x1, double y1, double x2, double y2)
    {
        const double vx = x2 - x1;
        const double vy = y2 - y1;
        const double wx = px - x1;
        const double wy = py - y1;
        const double c1 = vx * wx + vy * wy;
        if (c1 <= 0.0) return std::hypot(px - x1, py - y1);
        const double c2 = vx * vx + vy * vy;
        if (c2 <= c1) return std::hypot(px - x2, py - y2);
        const double b = c1 / c2;
        const double bx = x1 + b * vx;
        const double by = y1 + b * vy;
        return std::hypot(px - bx, py - by);
    }

    int HitTestZone(int sx, int sy, const RECT& rc)
    {
        const auto bounds = g_app.document.ComputeBounds();
        int bestIndex = -1;
        double bestDistance = 1e30;

        for (int i = 0; i < static_cast<int>(g_app.document.zones.size()); ++i)
        {
            const auto& zone = g_app.document.zones[i];
            POINT p1 = WorldToScreen(rc, bounds, zone.x1, zone.z1);
            POINT p2 = WorldToScreen(rc, bounds, zone.x2, zone.z2);
            double distance = 1e30;

            if (zone.ztype == static_cast<int>(mapfmt::ZoneType::Wall))
            {
                distance = DistancePointToSegment(static_cast<double>(sx), static_cast<double>(sy), p1.x, p1.y, p2.x, p2.y);
            }
            else
            {
                RECT zr{};
                zr.left = MinValue(p1.x, p2.x);
                zr.top = MinValue(p1.y, p2.y);
                zr.right = MaxValue(p1.x, p2.x);
                zr.bottom = MaxValue(p1.y, p2.y);
                if (sx >= zr.left && sx <= zr.right && sy >= zr.top && sy <= zr.bottom)
                {
                    distance = 0.0;
                }
                else
                {
                    const int cx = ClampValue(sx, static_cast<int>(zr.left), static_cast<int>(zr.right));
                    const int cy = ClampValue(sy, static_cast<int>(zr.top), static_cast<int>(zr.bottom));
                    distance = std::hypot(static_cast<double>(sx - cx), static_cast<double>(sy - cy));
                }
            }

            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestIndex = i;
            }
        }

        if (bestDistance > 20.0) return -1;
        return bestIndex;
    }

    void PaintCanvas(HWND hwnd)
    {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc{};
        GetClientRect(hwnd, &rc);

        HBRUSH backBrush = CreateSolidBrush(RGB(25, 25, 28));
        FillRect(hdc, &rc, backBrush);
        DeleteObject(backBrush);

        SetBkMode(hdc, TRANSPARENT);
        const auto bounds = g_app.document.ComputeBounds();

        const CanvasMetrics metrics = GetCanvasMetrics(rc, bounds);
        const int gridStartX = FloorToGrid(metrics.viewBounds.minX);
        const int gridEndX = CeilToGrid(metrics.viewBounds.maxX);
        const int gridStartZ = FloorToGrid(metrics.viewBounds.minZ);
        const int gridEndZ = CeilToGrid(metrics.viewBounds.maxZ);

        HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(48, 48, 56));
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, gridPen));
        for (int gx = gridStartX; gx <= gridEndX; gx += kGridStep)
        {
            POINT p1 = WorldToScreen(rc, bounds, gx, metrics.viewBounds.minZ);
            POINT p2 = WorldToScreen(rc, bounds, gx, metrics.viewBounds.maxZ);
            MoveToEx(hdc, p1.x, p1.y, nullptr);
            LineTo(hdc, p2.x, p2.y);
        }
        for (int gz = gridStartZ; gz <= gridEndZ; gz += kGridStep)
        {
            POINT p1 = WorldToScreen(rc, bounds, metrics.viewBounds.minX, gz);
            POINT p2 = WorldToScreen(rc, bounds, metrics.viewBounds.maxX, gz);
            MoveToEx(hdc, p1.x, p1.y, nullptr);
            LineTo(hdc, p2.x, p2.y);
        }
        SelectObject(hdc, oldPen);
        DeleteObject(gridPen);

        HFONT font = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, font));

        for (int i = 0; i < static_cast<int>(g_app.document.zones.size()); ++i)
        {
            const auto& zone = g_app.document.zones[i];
            const bool selected = (i == g_app.selectedZone);
            POINT p1 = WorldToScreen(rc, bounds, zone.x1, zone.z1);
            POINT p2 = WorldToScreen(rc, bounds, zone.x2, zone.z2);

            if (zone.ztype == static_cast<int>(mapfmt::ZoneType::Wall))
            {
                HPEN pen = CreatePen(PS_SOLID, selected ? 4 : 2, selected ? RGB(255, 220, 64) : RGB(96, 170, 255));
                HPEN prev = static_cast<HPEN>(SelectObject(hdc, pen));
                MoveToEx(hdc, p1.x, p1.y, nullptr);
                LineTo(hdc, p2.x, p2.y);
                SelectObject(hdc, prev);
                DeleteObject(pen);
            }
            else
            {
                RECT zr{};
                zr.left = MinValue(p1.x, p2.x);
                zr.top = MinValue(p1.y, p2.y);
                zr.right = MaxValue(p1.x, p2.x);
                zr.bottom = MaxValue(p1.y, p2.y);
                COLORREF fillColor = (zone.ztype == static_cast<int>(mapfmt::ZoneType::MonsterZone)) ? RGB(38, 92, 52) : RGB(96, 40, 40);
                COLORREF edgeColor = selected ? RGB(255, 220, 64)
                    : ((zone.ztype == static_cast<int>(mapfmt::ZoneType::MonsterZone)) ? RGB(76, 210, 110) : RGB(255, 92, 92));
                HBRUSH fill = CreateSolidBrush(fillColor);
                HPEN pen = CreatePen(PS_SOLID, selected ? 3 : 2, edgeColor);
                HGDIOBJ oldBrush = SelectObject(hdc, fill);
                HPEN oldPen2 = static_cast<HPEN>(SelectObject(hdc, pen));
                Rectangle(hdc, zr.left, zr.top, zr.right, zr.bottom);
                SelectObject(hdc, oldBrush);
                SelectObject(hdc, oldPen2);
                DeleteObject(fill);
                DeleteObject(pen);
            }

            std::wstringstream tag;
            tag << L"Z" << i << L" E" << zone.ev;
            const std::wstring tagText = tag.str();
            SetTextColor(hdc, selected ? RGB(255, 235, 150) : RGB(220, 220, 220));
            TextOutW(hdc, MinValue(p1.x, p2.x) + 4, MinValue(p1.y, p2.y) + 4, tagText.c_str(), static_cast<int>(tagText.size()));
        }

        if (g_app.isDrawing && g_app.insertMode != InsertMode::None)
        {
            const auto previewZone = BuildZoneFromDrawPoints(g_app.insertMode, g_app.drawStartWorld, g_app.drawCurrentWorld);
            POINT p1 = WorldToScreen(rc, bounds, previewZone.x1, previewZone.z1);
            POINT p2 = WorldToScreen(rc, bounds, previewZone.x2, previewZone.z2);
            HPEN pen = CreatePen(PS_DOT, 1, RGB(255, 255, 255));
            HGDIOBJ oldPreviewPen = SelectObject(hdc, pen);
            HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
            if (previewZone.ztype == static_cast<int>(mapfmt::ZoneType::Wall))
            {
                MoveToEx(hdc, p1.x, p1.y, nullptr);
                LineTo(hdc, p2.x, p2.y);
            }
            else
            {
                Rectangle(hdc, MinValue(p1.x, p2.x), MinValue(p1.y, p2.y), MaxValue(p1.x, p2.x), MaxValue(p1.y, p2.y));
            }
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPreviewPen);
            DeleteObject(pen);
        }

        if (g_app.document.zones.empty())
        {
            const wchar_t* text = L"Open a map or use Draw Wall / Draw Monster / Draw Trigger.";
            SetTextColor(hdc, RGB(210, 210, 210));
            TextOutW(hdc, 24, 24, text, lstrlenW(text));
        }

        SelectObject(hdc, oldFont);
        DeleteObject(font);
        EndPaint(hwnd, &ps);
    }

    void UpdateInfoPanelScrollBar(HWND hwnd)
    {
        AutoScrollPreviewToSelectedStrip();
        if (!hwnd) return;
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const PreviewMetrics metrics = GetPreviewMetrics(rc);

        SCROLLINFO si{};
        si.cbSize = sizeof(SCROLLINFO);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0;
        si.nMax = MaxValue(0, metrics.scaledWidth - 1);
        si.nPage = static_cast<UINT>(metrics.viewportWidth);
        g_app.previewScrollX = ClampValue(g_app.previewScrollX, 0, metrics.maxScrollX);
        si.nPos = g_app.previewScrollX;
        SetScrollInfo(hwnd, SB_HORZ, &si, TRUE);
    }

    void PaintInfoPanel(HWND hwnd)
    {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);

        HBRUSH bg = CreateSolidBrush(RGB(31, 31, 36));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);
        SetBkMode(hdc, TRANSPARENT);

        HFONT titleFont = CreateFontW(22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT modeFont = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT textFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, titleFont));
        SetTextColor(hdc, RGB(236, 236, 236));

        RECT titleRc{ 12, 10, rc.right - 12, rc.bottom - 12 };
        DrawTextW(hdc, L"Inspector", -1, &titleRc, DT_LEFT | DT_TOP | DT_SINGLELINE);

        const PreviewMetrics preview = GetPreviewMetrics(rc);

        SelectObject(hdc, modeFont);
        RECT modeRc{ 12, 46, rc.right - 12, preview.outer.top - 12 };
        std::wstring modeText = L"Mode: " + InsertModeToText(g_app.insertMode);
        SetTextColor(hdc, RGB(232, 232, 232));
        DrawTextW(hdc, modeText.c_str(), -1, &modeRc, DT_LEFT | DT_TOP | DT_SINGLELINE);

        SelectObject(hdc, textFont);
        int y = 76;
        std::wstring detailText;
        std::wstring noteText;

        if (g_app.selectedZone >= 0 && g_app.selectedZone < static_cast<int>(g_app.document.zones.size()))
        {
            const auto& zone = g_app.document.zones[g_app.selectedZone];
            const int band = ClampValue(g_app.previewTextureBand, 0, 7);
            std::wstringstream details;
            details << L"Selected: Z" << g_app.selectedZone << L"\r\n";
            details << L"Type: " << ZoneTypeToText(zone.ztype) << L"\r\n";
            details << L"Coords: (" << zone.x1 << L", " << zone.z1 << L") -> (" << zone.x2 << L", " << zone.z2 << L")\r\n";
            details << L"Event: " << zone.ev << L"\r\n";
            details << L"Preview band: T" << band << L"\r\n";
            details << L"Texture index: " << g_app.previewTextureIndex
                    << L"  Slot file: " << g_app.previewTextureSlot
                    << L"  Strip: " << g_app.previewTextureStrip << L"/20";
            detailText = details.str();
            noteText = L"Note: map textures are grouped as 8 files with up to 20 strips each.";
        }
        else
        {
            detailText = L"No zone selected.\r\n\r\nClick a zone to inspect it.\r\nUse the Draw buttons to place new walls or zones.";
        }

        RECT detailRc{ 12, y, rc.right - 12, preview.outer.top - 34 };
        SetTextColor(hdc, RGB(220, 220, 220));
        DrawTextW(hdc, detailText.c_str(), -1, &detailRc, DT_LEFT | DT_TOP | DT_WORDBREAK);

        if (!noteText.empty())
        {
            RECT detailMeasureRc = detailRc;
            DrawTextW(hdc, detailText.c_str(), -1, &detailMeasureRc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_CALCRECT);
            RECT noteRc{ 12, detailMeasureRc.bottom + 8, rc.right - 12, preview.outer.top - 10 };
            SetTextColor(hdc, RGB(188, 188, 188));
            DrawTextW(hdc, noteText.c_str(), -1, &noteRc, DT_LEFT | DT_TOP | DT_WORDBREAK);
        }

        HGDIOBJ oldPreviewBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(hdc, preview.outer.left, preview.outer.top, preview.outer.right, preview.outer.bottom);
        SelectObject(hdc, oldPreviewBrush);

        if (!g_app.previewImage.pixels.empty() && g_app.previewImage.width > 0 && g_app.previewImage.height > 0)
        {
            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = g_app.previewImage.width;
            bmi.bmiHeader.biHeight = -g_app.previewImage.height;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            const int scrollX = ClampValue(g_app.previewScrollX, 0, preview.maxScrollX);
            const int drawX = preview.inner.left - scrollX;
            const int drawY = preview.inner.top + (preview.viewportHeight - preview.scaledHeight) / 2;
            const int activeStrip = ClampValue(g_app.previewTextureStrip, 0, 19);
            const int stripSrcX = activeStrip * 64;
            const int stripSrcWidth = MaxValue(1, MinValue(64, g_app.previewImage.width - stripSrcX));
            const double previewScaleX = static_cast<double>(preview.scaledWidth) / static_cast<double>(MaxValue(1, g_app.previewImage.width));
            const int stripDrawX = drawX + static_cast<int>(std::lround(static_cast<double>(stripSrcX) * previewScaleX));
            const int stripDrawWidth = MaxValue(1, static_cast<int>(std::lround(static_cast<double>(stripSrcWidth) * previewScaleX)));

            std::vector<uint32_t> shadedPixels = g_app.previewImage.pixels;
            for (uint32_t& px : shadedPixels)
            {
                const uint32_t a = px & 0xFF000000u;
                const uint32_t r = (px >> 16) & 0xFFu;
                const uint32_t g = (px >> 8) & 0xFFu;
                const uint32_t b = px & 0xFFu;
                const uint32_t dr = (r * 35u) / 100u;
                const uint32_t dg = (g * 35u) / 100u;
                const uint32_t db = (b * 35u) / 100u;
                px = a | (dr << 16) | (dg << 8) | db;
            }

            SaveDC(hdc);
            IntersectClipRect(hdc, preview.inner.left, preview.inner.top, preview.inner.right, preview.inner.bottom);
            StretchDIBits(hdc, drawX, drawY, preview.scaledWidth, preview.scaledHeight, 0, 0,
                g_app.previewImage.width, g_app.previewImage.height,
                shadedPixels.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
            StretchDIBits(hdc, stripDrawX, drawY, stripDrawWidth, preview.scaledHeight,
                stripSrcX, 0, stripSrcWidth, g_app.previewImage.height,
                g_app.previewImage.pixels.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);

            HPEN activePen = CreatePen(PS_SOLID, 1, RGB(255, 220, 120));
            HGDIOBJ oldPen = SelectObject(hdc, activePen);
            HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
            Rectangle(hdc, stripDrawX, preview.inner.top, stripDrawX + stripDrawWidth, preview.inner.bottom);
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(activePen);
            RestoreDC(hdc, -1);
        }
        else
        {
            RECT msgRc{ preview.inner.left + 4, preview.inner.top + 4, preview.inner.right - 4, preview.inner.bottom - 4 };
            std::wstring message = g_app.previewImage.error.empty() ? L"No preview available." : Utf8ToWide(g_app.previewImage.error);
            DrawTextW(hdc, message.c_str(), -1, &msgRc, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        }

        RECT sourceRc{ 12, preview.outer.bottom + 8, rc.right - 12, rc.bottom - 8 };
        std::wstring sourceText = !g_app.previewImage.loadedPath.empty()
            ? L"Source: " + FormatPreviewSourcePath(g_app.previewImage.loadedPath)
            : L"Source: preview file not resolved.";
        SetTextColor(hdc, RGB(182, 182, 182));
        DrawTextW(hdc, sourceText.c_str(), -1, &sourceRc, DT_LEFT | DT_TOP | DT_WORDBREAK);

        RECT sourceMeasureRc = sourceRc;
        DrawTextW(hdc, sourceText.c_str(), -1, &sourceMeasureRc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_CALCRECT);
        RECT descRc{ 12, sourceMeasureRc.bottom + 8, rc.right - 12, rc.bottom - 8 };
        std::wstring descText = L"The preview auto-jumps to the selected strip. Unused areas are dimmed, and you can still inspect the full file with the scrollbar or wheel.";
        SetTextColor(hdc, RGB(160, 160, 160));
        DrawTextW(hdc, descText.c_str(), -1, &descRc, DT_LEFT | DT_TOP | DT_WORDBREAK);

        SelectObject(hdc, oldFont);
        DeleteObject(titleFont);
        DeleteObject(modeFont);
        DeleteObject(textFont);
        EndPaint(hwnd, &ps);
    }

    LRESULT CALLBACK InfoPanelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_SIZE:
            UpdateInfoPanelScrollBar(hwnd);
            return 0;
        case WM_HSCROLL:
        {
            SCROLLINFO si{};
            si.cbSize = sizeof(SCROLLINFO);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_HORZ, &si);
            int pos = si.nPos;
            switch (LOWORD(wParam))
            {
            case SB_LINELEFT: pos -= 32; break;
            case SB_LINERIGHT: pos += 32; break;
            case SB_PAGELEFT: pos -= static_cast<int>(si.nPage > 0 ? si.nPage : 64); break;
            case SB_PAGERIGHT: pos += static_cast<int>(si.nPage > 0 ? si.nPage : 64); break;
            case SB_THUMBPOSITION:
            case SB_THUMBTRACK: pos = si.nTrackPos; break;
            case SB_LEFT: pos = si.nMin; break;
            case SB_RIGHT: pos = si.nMax; break;
            default: break;
            }
            g_app.previewScrollX = ClampValue(pos, si.nMin, MaxValue(si.nMin, si.nMax - static_cast<int>(si.nPage) + 1));
            UpdateInfoPanelScrollBar(hwnd);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        case WM_MOUSEWHEEL:
        {
            const short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (delta == 0) return 0;
            g_app.previewScrollX -= (delta / WHEEL_DELTA) * 80;
            UpdateInfoPanelScrollBar(hwnd);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        case WM_PAINT:
            PaintInfoPanel(hwnd);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    LRESULT CALLBACK CanvasProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_LBUTTONDOWN:
        {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            if (g_app.insertMode != InsertMode::None)
            {
                SetFocus(hwnd);
                SetCapture(hwnd);
                g_app.isDrawing = true;
                g_app.drawStartWorld = ScreenToWorld(rc, g_app.document.ComputeBounds(), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                g_app.drawCurrentWorld = g_app.drawStartWorld;
                RefreshStatus();
                InvalidateEditorViews();
                return 0;
            }

            const int zone = HitTestZone(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), rc);
            g_app.selectedZone = zone;
            if (zone >= 0) UpdatePreviewTextureBandFromPoint(rc, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            RefreshZoneList();
            return 0;
        }
        case WM_MOUSEMOVE:
            if (g_app.isDrawing)
            {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                g_app.drawCurrentWorld = ScreenToWorld(rc, g_app.document.ComputeBounds(), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                InvalidateEditorViews();
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (g_app.isDrawing)
            {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                g_app.drawCurrentWorld = ScreenToWorld(rc, g_app.document.ComputeBounds(), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                if (GetCapture() == hwnd) ReleaseCapture();
                CommitDrawnZone();
                return 0;
            }
            break;
        case WM_CAPTURECHANGED:
            if (g_app.isDrawing && reinterpret_cast<HWND>(lParam) != hwnd)
            {
                g_app.isDrawing = false;
                RefreshStatus();
                InvalidateEditorViews();
            }
            break;
        case WM_LBUTTONDBLCLK:
            if (g_app.insertMode == InsertMode::None)
            {
                EditSelectedZone();
            }
            return 0;
        case WM_MOUSEWHEEL:
        {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            const mapfmt::Bounds rawBounds = g_app.document.ComputeBounds();
            POINT clientPt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &clientPt);
            const POINT before = ScreenToWorld(rc, rawBounds, clientPt.x, clientPt.y);

            const short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (delta > 0)
            {
                g_app.canvasZoom = MinValue(kCanvasZoomMax, g_app.canvasZoom * 1.2);
            }
            else if (delta < 0)
            {
                g_app.canvasZoom = MaxValue(kCanvasZoomMin, g_app.canvasZoom / 1.2);
            }

            const POINT after = ScreenToWorld(rc, rawBounds, clientPt.x, clientPt.y);
            g_app.viewCenterX += static_cast<double>(before.x - after.x);
            g_app.viewCenterZ += static_cast<double>(before.y - after.y);
            g_app.viewInitialized = true;
            ClampViewCenterToDocument(rawBounds);
            RefreshStatus();
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        case WM_PAINT:
            PaintCanvas(hwnd);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void CreateChildWindows(HWND hwnd)
    {
        g_app.zoneList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL,
            0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(IDC_ZONE_LIST), g_app.instance, nullptr);

        g_app.canvas = CreateWindowExW(WS_EX_CLIENTEDGE, kCanvasClass, nullptr,
            WS_CHILD | WS_VISIBLE,
            0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(IDC_CANVAS), g_app.instance, nullptr);

        g_app.infoPanel = CreateWindowExW(WS_EX_CLIENTEDGE, kInfoPanelClass, nullptr,
            WS_CHILD | WS_VISIBLE | WS_HSCROLL,
            0, 0, 100, 100, hwnd, nullptr, g_app.instance, nullptr);

        g_app.btnAddWall = CreateWindowW(L"BUTTON", L"Draw Wall", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_ADD_WALL), g_app.instance, nullptr);
        g_app.btnAddMonster = CreateWindowW(L"BUTTON", L"Draw Monster", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_ADD_MONSTER), g_app.instance, nullptr);
        g_app.btnAddTrigger = CreateWindowW(L"BUTTON", L"Draw Trigger", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_ADD_TRIGGER), g_app.instance, nullptr);
        g_app.btnEdit = CreateWindowW(L"BUTTON", L"Edit Zone", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_EDIT_ZONE), g_app.instance, nullptr);
        g_app.btnDelete = CreateWindowW(L"BUTTON", L"Delete Zone", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_DELETE_ZONE), g_app.instance, nullptr);
        g_app.btnUp = CreateWindowW(L"BUTTON", L"Move Up", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_MOVE_UP), g_app.instance, nullptr);
        g_app.btnDown = CreateWindowW(L"BUTTON", L"Move Down", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_MOVE_DOWN), g_app.instance, nullptr);
        g_app.btnEvents = CreateWindowW(L"BUTTON", L"Events...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_EVENTS), g_app.instance, nullptr);
        g_app.btnTextures = CreateWindowW(L"BUTTON", L"Texture Slots...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_TEXTURES), g_app.instance, nullptr);

        g_app.statusBar = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr,
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
            0, 0, 0, 0, hwnd, nullptr, g_app.instance, nullptr);

        UpdateModeButtons();
    }

    LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_CREATE:
            SetMenu(hwnd, BuildMainMenu());
            CreateChildWindows(hwnd);
            FitViewToDocument();
            LayoutChildren(hwnd);
            return 0;

        case WM_SIZE:
            LayoutChildren(hwnd);
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IDC_ZONE_LIST:
                if (HIWORD(wParam) == LBN_SELCHANGE)
                {
                    g_app.selectedZone = static_cast<int>(SendMessageW(g_app.zoneList, LB_GETCURSEL, 0, 0));
                    g_app.previewTextureBand = 0;
                    RefreshPreviewImage();
                    RefreshStatus();
                    InvalidateEditorViews();
                }
                else if (HIWORD(wParam) == LBN_DBLCLK)
                {
                    EditSelectedZone();
                }
                return 0;

            case IDM_FILE_NEW: NewDocument(); return 0;
            case IDM_FILE_OPEN: OpenDocument(); return 0;
            case IDM_FILE_SAVE: SaveDocument(false); return 0;
            case IDM_FILE_SAVE_AS: SaveDocument(true); return 0;
            case IDM_FILE_EXPORT_SVG: ExportSvg(); return 0;
            case IDM_FILE_EXIT: PostMessageW(hwnd, WM_CLOSE, 0, 0); return 0;
            case IDM_EDIT_ZONE:
            case IDC_BTN_EDIT_ZONE: EditSelectedZone(); return 0;
            case IDM_EDIT_DELETE:
            case IDC_BTN_DELETE_ZONE: DeleteSelectedZone(); return 0;
            case IDM_EDIT_MOVE_UP:
            case IDC_BTN_MOVE_UP: MoveZoneUp(); return 0;
            case IDM_EDIT_MOVE_DOWN:
            case IDC_BTN_MOVE_DOWN: MoveZoneDown(); return 0;
            case IDM_INSERT_WALL:
            case IDC_BTN_ADD_WALL: SetInsertMode(InsertMode::Wall); return 0;
            case IDM_INSERT_MONSTER_ZONE:
            case IDC_BTN_ADD_MONSTER: SetInsertMode(InsertMode::MonsterZone); return 0;
            case IDM_INSERT_EVENT_TRIGGER:
            case IDC_BTN_ADD_TRIGGER: SetInsertMode(InsertMode::EventTrigger); return 0;
            case IDM_MAP_EVENTS:
            case IDC_BTN_EVENTS: ShowEventEditor(); return 0;
            case IDM_MAP_TEXTURES:
            case IDC_BTN_TEXTURES: ShowTextureDialog(); return 0;
            case IDM_TOOLS_VALIDATE: ShowValidationReport(); return 0;
            case IDM_HELP_ABOUT:
                MessageBoxW(hwnd,
                    L"ZGloom Editor\n\nStandalone Win32 editor for Gloom-compatible maps.\nFocused on practical editing, clean Visual Studio setup and x86 builds.",
                    L"About ZGloom Editor", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            break;

        case WM_CLOSE:
            if (ConfirmDiscardChanges())
            {
                DestroyWindow(hwnd);
            }
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCmd)
{
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    g_app.instance = instance;
    g_app.document.NewBlank();

    WNDCLASSEXW mainClass{};
    mainClass.cbSize = sizeof(mainClass);
    mainClass.hInstance = instance;
    mainClass.lpszClassName = kMainWindowClass;
    mainClass.lpfnWndProc = MainWndProc;
    mainClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    mainClass.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    mainClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassExW(&mainClass);

    WNDCLASSEXW canvasClass{};
    canvasClass.cbSize = sizeof(canvasClass);
    canvasClass.hInstance = instance;
    canvasClass.lpszClassName = kCanvasClass;
    canvasClass.lpfnWndProc = CanvasProc;
    canvasClass.hCursor = LoadCursor(nullptr, IDC_CROSS);
    canvasClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    canvasClass.style = CS_DBLCLKS;
    RegisterClassExW(&canvasClass);

    WNDCLASSEXW infoClass{};
    infoClass.cbSize = sizeof(infoClass);
    infoClass.hInstance = instance;
    infoClass.lpszClassName = kInfoPanelClass;
    infoClass.lpfnWndProc = InfoPanelProc;
    infoClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    infoClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassExW(&infoClass);

    g_app.mainWindow = CreateWindowExW(
        0,
        kMainWindowClass,
        L"ZGloom Editor",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 820,
        nullptr, nullptr, instance, nullptr);

    if (!g_app.mainWindow)
    {
        return 0;
    }

    UpdateTitle();
    ShowWindow(g_app.mainWindow, showCmd);
    UpdateWindow(g_app.mainWindow);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
