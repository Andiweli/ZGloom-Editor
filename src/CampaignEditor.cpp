#include "CampaignEditor.h"

#include "decrunchmania.h"
#include "resource.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <climits>
#include <cstdlib>
#include <cwctype>
#include <cwchar>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <array>
#include <utility>
#include <vector>

#include <commdlg.h>
#include <commctrl.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")

namespace fs = std::filesystem;

namespace
{
    enum class GameType
    {
        None,
        Gloom,
        GloomDeluxe,
        Gloom3,
        ZombieMassacre,
    };

    struct GameProfile
    {
        GameType type = GameType::None;
        fs::path root;
        fs::path mapsDir;
        fs::path picturesDir;
        fs::path scriptFile;
        fs::path musicDir;
        fs::path objectsDir;
        fs::path textureDir;
        bool valid = false;
        std::wstring warning;
    };

    struct PictureInfo
    {
        fs::path imageFile;
        fs::path paletteFile;
        bool exists = false;
        bool hasPalette = false;
        bool isCrM2 = false;
        bool decrunched = false;
        std::uintmax_t imageBytes = 0;
        std::uintmax_t rawBytes = 0;
        std::uintmax_t paletteBytes = 0;
        int paletteEntries = 0;
        int paletteEntryBytes = 2;
        bool paletteLooksNibble = false;
        int guessedWidth = 0;
        int guessedHeight = 0;
        int bitDepth = 0;
        bool isTrimmedIff = false;
        bool isFullIff = false;
        bool isRawPlanar = false;
        bool usesFallbackPalette = false;
        std::wstring error;
    };

    struct RgbColor
    {
        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;
    };

    struct PaletteData
    {
        std::vector<RgbColor> colors;
        int entryBytes = 4;
        bool nibbleSource = false;
        // 4-byte palette layout:
        //   rgbOffset 1/channelOrder 0 = 00 RR GG BB
        //   rgbOffset 0/channelOrder 0 = RR GG BB 00
        //   rgbOffset 1/channelOrder 1 = 00 BB GG RR
        //   rgbOffset 0/channelOrder 1 = BB GG RR 00
        int rgbOffset = 1;
        int channelOrder = 0;
        int decodeMode = 0;
        bool gloomSplitNibbleLayout = false;
    };

    struct PictureManagerDialog
    {
        HWND owner = nullptr;
        HWND hwnd = nullptr;
        HWND list = nullptr;
        HWND info = nullptr;
        HWND exportPng = nullptr;
        HWND importPng = nullptr;
        HWND paletteMode = nullptr;
        HWND indexMode = nullptr;
        HWND close = nullptr;
        HFONT font = nullptr;
        std::wstring title;
        fs::path pictureDir;
        std::vector<PictureInfo> pictures;
        int selectedIndex = -1;
        HBITMAP previewBitmap = nullptr;
        int previewWidth = 0;
        int previewHeight = 0;
        int previewScrollX = 0;
        int previewContentWidth = 0;
        bool titleOnly = false;
        int paletteModeIndex = 0;
        int indexModeIndex = 0;
    };



    enum class TextureAssetKind
    {
        Unknown,
        WallTextureSet,
        FlatTexture,
    };

    struct TextureInfo
    {
        fs::path file;
        TextureAssetKind kind = TextureAssetKind::Unknown;
        bool exists = false;
        bool isCrM2 = false;
        bool decrunched = false;
        std::uintmax_t storedBytes = 0;
        std::uintmax_t rawBytes = 0;
        int width = 0;
        int height = 0;
        int colors = 0;
        int wallColumns = 0;
        int wallTextureCount = 0;
        std::wstring error;
    };

    struct TextureImage
    {
        int width = 0;
        int height = 0;
        std::vector<RgbColor> rgba;
    };

    struct TextureManagerDialog
    {
        HWND owner = nullptr;
        HWND hwnd = nullptr;
        HWND list = nullptr;
        HWND info = nullptr;
        HWND exportPng = nullptr;
        HWND importPng = nullptr;
        HWND close = nullptr;
        HWND previewScroll = nullptr;
        HFONT font = nullptr;
        fs::path textureDir;
        std::vector<TextureInfo> textures;
        int selectedIndex = -1;
        HBITMAP previewBitmap = nullptr;
        int previewWidth = 0;
        int previewHeight = 0;
        int previewScrollX = 0;
        int previewContentWidth = 0;
        int previewMaxScrollX = 0;
        int previewWheelRemainder = 0;
        int previewScrollDragOffsetX = 0;
        bool previewScrollHover = false;
        bool previewScrollDragging = false;
    };


    enum class ScriptBlockType
    {
        Picture,
        Tile,
        Draw,
        Show,
        Text,
        Wait,
        Dark,
        PlayMap,
        Song,
        Rest,
        EndGame,
        Comment,
        Raw,
    };

    struct ScriptBlock
    {
        ScriptBlockType type = ScriptBlockType::Raw;
        std::wstring value;
        std::wstring rawLine;
        std::string originalLineBytes;
        std::string lineEndingBytes;
        bool hasOriginal = false;
    };

    enum class VisualItemType
    {
        Episode,
        Level,
        EndGame,
        Raw,
    };

    struct VisibleScriptItem
    {
        VisualItemType type = VisualItemType::Raw;
        int primaryIndex = -1;
        int pictureIndex = -1;
        int tileIndex = -1;
        int textIndex = -1;
        int playIndex = -1;
        int rawIndex = -1;
    };

    struct ScriptEditorDialog
    {
        HWND owner = nullptr;
        HWND hwnd = nullptr;
        HWND statusLabel = nullptr;
        HWND blockList = nullptr;
        HWND typeCombo = nullptr;        // Tile combo
        HWND valueLabel = nullptr;       // Legacy/section label
        HWND tileLabel = nullptr;
        HWND pictureLabel = nullptr;
        HWND mapLabel = nullptr;
        HWND levelTextLabel = nullptr;
        HWND valueEdit = nullptr;        // Level text / raw command editor
        HWND valueCombo = nullptr;       // Episode picture combo
        HWND mapCombo = nullptr;
        HWND helpLabel = nullptr;
        HWND addPicture = nullptr;
        HWND addTile = nullptr;
        HWND addText = nullptr;
        HWND addWait = nullptr;
        HWND addDraw = nullptr;
        HWND addShow = nullptr;
        HWND addDark = nullptr;
        HWND addPlayMap = nullptr;
        HWND addSong = nullptr;
        HWND addEndGame = nullptr;
        HWND addRaw = nullptr;
        HWND moveUp = nullptr;
        HWND moveDown = nullptr;
        HWND deleteBlock = nullptr;
        HWND save = nullptr;
        HWND close = nullptr;
        HWND openRaw = nullptr;
        HFONT font = nullptr;
        fs::path scriptFile;
        std::vector<ScriptBlock> blocks;
        std::vector<std::wstring> pictureNames;
        std::vector<std::wstring> mapNames;
        std::vector<std::wstring> musicNames;
        std::vector<std::wstring> tileValues;
        std::vector<VisibleScriptItem> visibleItems;
        std::vector<int> visibleBlockIndices;
        int selectedIndex = -1;
        bool updating = false;
        bool dirty = false;
    };

    GameProfile g_profile;

    constexpr int kRecentCampaignGameRootCount = 3;
    constexpr unsigned int kRecentCampaignGameRootBaseId = 65100;
    constexpr const wchar_t* kCampaignRegistryKey = L"Software\\ZGloomEditor";

    constexpr COLORREF kBg = RGB(30, 30, 30);
    constexpr COLORREF kPanelBg = RGB(37, 37, 38);
    constexpr COLORREF kEditBg = RGB(45, 45, 48);
    constexpr COLORREF kText = RGB(232, 232, 232);
    constexpr COLORREF kMutedText = RGB(172, 172, 172);
    constexpr COLORREF kAccent = RGB(86, 156, 214);

    constexpr int IDC_SCRIPT_BLOCK_LIST = 52001;
    constexpr int IDC_SCRIPT_TYPE_COMBO = 52002;
    constexpr int IDC_SCRIPT_VALUE_EDIT = 52003;
    constexpr int IDC_SCRIPT_VALUE_COMBO = 52004;
    constexpr int IDC_SCRIPT_ADD_PICTURE = 52005;
    constexpr int IDC_SCRIPT_ADD_TILE = 52006;
    constexpr int IDC_SCRIPT_ADD_TEXT = 52007;
    constexpr int IDC_SCRIPT_ADD_WAIT = 52008;
    constexpr int IDC_SCRIPT_ADD_DRAW = 52009;
    constexpr int IDC_SCRIPT_ADD_SHOW = 52010;
    constexpr int IDC_SCRIPT_ADD_DARK = 52011;
    constexpr int IDC_SCRIPT_ADD_PLAYMAP = 52012;
    constexpr int IDC_SCRIPT_ADD_SONG = 52013;
    constexpr int IDC_SCRIPT_ADD_RAW = 52014;
    constexpr int IDC_SCRIPT_MOVE_UP = 52015;
    constexpr int IDC_SCRIPT_MOVE_DOWN = 52016;
    constexpr int IDC_SCRIPT_DELETE = 52017;
    constexpr int IDC_SCRIPT_SAVE = 52018;
    constexpr int IDC_SCRIPT_CLOSE = 52019;
    constexpr int IDC_SCRIPT_OPEN_RAW = 52020;
    constexpr int IDC_SCRIPT_ADD_DONE = 52021;
    constexpr int IDC_SCRIPT_MAP_COMBO = 52022;

    constexpr int IDC_PICTURE_LIST = 52101;
    constexpr int IDC_PICTURE_INFO = 52102;
    constexpr int IDC_PICTURE_EXPORT_PNG = 52103;
    constexpr int IDC_PICTURE_IMPORT_PNG = 52104;
    constexpr int IDC_PICTURE_CLOSE = 52105;
    constexpr int IDC_PICTURE_PALETTE_MODE = 52106;
    constexpr int IDC_PICTURE_INDEX_MODE = 52107;

    constexpr int IDC_TEXTURE_LIST = 52201;
    constexpr int IDC_TEXTURE_INFO = 52202;
    constexpr int IDC_TEXTURE_EXPORT_PNG = 52203;
    constexpr int IDC_TEXTURE_IMPORT_PNG = 52204;
    constexpr int IDC_TEXTURE_CLOSE = 52205;
    constexpr int IDC_TEXTURE_HSCROLL = 52206;

    constexpr int IDC_NEW_CAMPAIGN_TEMPLATE = 52301;
    constexpr int IDC_NEW_CAMPAIGN_TEMPLATE_ROOT = 52302;
    constexpr int IDC_NEW_CAMPAIGN_BROWSE_TEMPLATE = 52303;
    constexpr int IDC_NEW_CAMPAIGN_TARGET_PARENT = 52304;
    constexpr int IDC_NEW_CAMPAIGN_BROWSE_TARGET = 52305;
    constexpr int IDC_NEW_CAMPAIGN_NAME = 52306;
    constexpr int IDC_NEW_CAMPAIGN_INFO = 52307;
    constexpr int IDC_NEW_CAMPAIGN_CREATE = 52308;
    constexpr int IDC_NEW_CAMPAIGN_CANCEL = 52309;

    enum class CampaignTemplateKind
    {
        GloomFamily,
        Gloom3,
        ZombieMassacre,
    };

    struct NewCampaignDialog
    {
        HWND owner = nullptr;
        HWND hwnd = nullptr;
        HWND templateCombo = nullptr;
        HWND templateRootEdit = nullptr;
        HWND templateBrowse = nullptr;
        HWND targetParentEdit = nullptr;
        HWND targetBrowse = nullptr;
        HWND campaignNameEdit = nullptr;
        HWND info = nullptr;
        HWND create = nullptr;
        HWND cancel = nullptr;
        HFONT font = nullptr;
        bool result = false;
        bool done = false;
    };


    std::wstring ToWideAcp(const std::string& text)
    {
        if (text.empty())
            return std::wstring();

        int len = MultiByteToWideChar(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
        if (len <= 0)
            return std::wstring(text.begin(), text.end());

        std::wstring out(static_cast<size_t>(len), L'\0');
        MultiByteToWideChar(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()), out.data(), len);
        return out;
    }

    std::string ToAcp(const std::wstring& text)
    {
        if (text.empty())
            return std::string();

        int len = WideCharToMultiByte(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (len <= 0)
        {
            std::string fallback;
            fallback.reserve(text.size());
            for (wchar_t ch : text)
                fallback.push_back((ch >= 0 && ch <= 0x7f) ? static_cast<char>(ch) : '?');
            return fallback;
        }

        std::string out(static_cast<size_t>(len), '\0');
        WideCharToMultiByte(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()), out.data(), len, nullptr, nullptr);
        return out;
    }

    std::wstring PathText(const fs::path& path)
    {
        return path.empty() ? L"(not set)" : path.wstring();
    }

    std::wstring Lower(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
            return static_cast<wchar_t>(std::towlower(c));
        });
        return value;
    }


    bool StartsWith(const std::wstring& value, const wchar_t* prefix)
    {
        const size_t prefixLen = std::wcslen(prefix);
        return value.size() >= prefixLen && value.compare(0, prefixLen, prefix) == 0;
    }

    bool EndsWith(const std::wstring& value, const wchar_t* suffix)
    {
        const size_t suffixLen = std::wcslen(suffix);
        return value.size() >= suffixLen && value.compare(value.size() - suffixLen, suffixLen, suffix) == 0;
    }

    bool FileExists(const fs::path& path)
    {
        std::error_code ec;
        return fs::exists(path, ec) && fs::is_regular_file(path, ec);
    }

    bool DirExists(const fs::path& path)
    {
        std::error_code ec;
        return fs::exists(path, ec) && fs::is_directory(path, ec);
    }

    std::uintmax_t FileSizeOrZero(const fs::path& path)
    {
        std::error_code ec;
        const std::uintmax_t size = fs::file_size(path, ec);
        return ec ? 0 : size;
    }

    bool ReadFileBinary(const fs::path& path, std::vector<std::uint8_t>& data)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return false;

        file.seekg(0, std::ios::end);
        const std::streamoff size = file.tellg();
        file.seekg(0, std::ios::beg);
        if (size <= 0)
        {
            data.clear();
            return true;
        }

        data.resize(static_cast<size_t>(size));
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        return !!file;
    }

    bool LooksLikeCrM2Bytes(const std::vector<std::uint8_t>& data)
    {
        return data.size() >= 14 &&
            data[0] == 'C' && data[1] == 'r' && data[2] == 'M' && data[3] == '2';
    }

    bool LoadMaybeCrM2(const fs::path& path, std::vector<std::uint8_t>& out, std::wstring& error)
    {
        std::vector<std::uint8_t> fileData;
        if (!ReadFileBinary(path, fileData))
        {
            error = L"Could not open file.";
            return false;
        }

        if (!LooksLikeCrM2Bytes(fileData))
        {
            error.clear();
            out = std::move(fileData);
            return true;
        }

        const unsigned int unpackedSize = GetSize(fileData.data());
        const unsigned int headroom = GetSecDist(fileData.data());
        if (unpackedSize == 0)
        {
            error = L"Invalid CrM2 header.";
            return false;
        }

        const size_t requiredWorkSize = static_cast<size_t>(unpackedSize) + static_cast<size_t>(headroom) + 32U;
        const size_t workSize = std::max(fileData.size(), requiredWorkSize);
        std::vector<std::uint8_t> work(workSize, 0);
        std::vector<std::uint8_t> unpacked(unpackedSize, 0);
        std::copy(fileData.begin(), fileData.end(), work.begin());

        if (Decrunch(work.data(), unpacked.data()) == nullptr)
        {
            error = L"CrM2 decompression failed.";
            return false;
        }

        error.clear();
        out = std::move(unpacked);
        return true;
    }

    bool IsCrM2File(const fs::path& path)
    {
        std::vector<std::uint8_t> data;
        if (!ReadFileBinary(path, data))
            return false;
        return LooksLikeCrM2Bytes(data);
    }

    std::wstring GameTypeName(GameType type)
    {
        switch (type)
        {
        case GameType::Gloom: return L"Gloom";
        case GameType::GloomDeluxe: return L"Gloom Deluxe";
        case GameType::Gloom3: return L"Gloom 3";
        case GameType::ZombieMassacre: return L"Zombie Massacre";
        default: return L"Unknown";
        }
    }

    GameType DetectGloomFamilyByRootName(const fs::path& root)
    {
        const std::wstring name = Lower(root.filename().wstring());
        const std::wstring full = Lower(root.wstring());

        if (name.find(L"deluxe") != std::wstring::npos || full.find(L"deluxe") != std::wstring::npos)
            return GameType::GloomDeluxe;

        if (name.find(L"gloom3") != std::wstring::npos ||
            name.find(L"gloom 3") != std::wstring::npos ||
            full.find(L"gloom3") != std::wstring::npos ||
            full.find(L"gloom 3") != std::wstring::npos)
            return GameType::Gloom3;

        return GameType::Gloom;
    }

    GameProfile DetectGameProfile(const fs::path& root)
    {
        GameProfile p;
        p.root = root;

        const fs::path gloomMaps = root / L"maps";
        const fs::path gloomPics = root / L"pics";
        const fs::path gloomScript = root / L"misc" / L"script";

        const fs::path massacreMaps = root / L"lvls";
        const fs::path massacrePics = root / L"pixs";
        const fs::path massacreScript = root / L"stuf" / L"stages";

        if (DirExists(massacreMaps) || DirExists(massacrePics) || FileExists(massacreScript))
        {
            p.type = GameType::ZombieMassacre;
            p.mapsDir = massacreMaps;
            p.picturesDir = massacrePics;
            p.scriptFile = massacreScript;
            p.musicDir = root / L"musi";
            p.objectsDir = root / L"char";
            p.textureDir = root / L"txts";
        }
        else
        {
            p.type = DetectGloomFamilyByRootName(root);
            p.mapsDir = gloomMaps;
            p.picturesDir = gloomPics;
            p.scriptFile = gloomScript;
            p.musicDir = root / L"sfxs";
            p.objectsDir = root / L"objs";
            p.textureDir = root / L"txts";
        }

        std::wstringstream warn;
        bool ok = true;

        if (!DirExists(p.mapsDir))
        {
            warn << L"Missing map folder: " << p.mapsDir.wstring() << L"\n";
            ok = false;
        }
        if (!DirExists(p.picturesDir))
        {
            warn << L"Missing picture folder: " << p.picturesDir.wstring() << L"\n";
            ok = false;
        }
        if (!FileExists(p.scriptFile))
        {
            warn << L"Missing campaign script: " << p.scriptFile.wstring() << L"\n";
            ok = false;
        }

        p.valid = ok;
        p.warning = warn.str();
        return p;
    }

    std::vector<fs::path> ListRegularFiles(const fs::path& folder);
    std::wstring AssetCommandName(const fs::path& file);
    bool ReadPalette(const fs::path& paletteFile, PaletteData& palette, std::wstring& error, int decodeMode = 0);

    struct TileSetRange
    {
        bool hasFloor = false;
        bool hasRoof = false;
        bool valid = false;
        int floorMin = 0;
        int floorMax = 0;
        int roofMin = 0;
        int roofMax = 0;
        int minValue = 0;
        int maxValue = 9;
    };

    bool ParseTrailingNumberAfterPrefix(const std::wstring& assetName, const wchar_t* prefix, int& number)
    {
        const std::wstring lowerName = Lower(assetName);
        if (!StartsWith(lowerName, prefix))
            return false;

        size_t end = lowerName.size();
        while (end > 0 && !std::iswdigit(lowerName[end - 1]))
            --end;
        size_t start = end;
        while (start > 0 && std::iswdigit(lowerName[start - 1]))
            --start;
        if (start == end)
            return false;

        try
        {
            number = std::stoi(lowerName.substr(start, end - start));
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    TileSetRange DetectTileSetRange(const fs::path& textureDir)
    {
        TileSetRange range;
        std::vector<int> floorValues;
        std::vector<int> roofValues;

        for (const fs::path& file : ListRegularFiles(textureDir))
        {
            const std::wstring assetName = AssetCommandName(file);
            int value = 0;
            if (ParseTrailingNumberAfterPrefix(assetName, L"floor", value))
                floorValues.push_back(value);
            if (ParseTrailingNumberAfterPrefix(assetName, L"roof", value))
                roofValues.push_back(value);
        }

        auto applyMinMax = [](const std::vector<int>& values, int& outMin, int& outMax) {
            const auto mm = std::minmax_element(values.begin(), values.end());
            outMin = *mm.first;
            outMax = *mm.second;
        };

        if (!floorValues.empty())
        {
            range.hasFloor = true;
            applyMinMax(floorValues, range.floorMin, range.floorMax);
        }
        if (!roofValues.empty())
        {
            range.hasRoof = true;
            applyMinMax(roofValues, range.roofMin, range.roofMax);
        }

        if (range.hasFloor && range.hasRoof)
        {
            range.minValue = std::max(range.floorMin, range.roofMin);
            range.maxValue = std::min(range.floorMax, range.roofMax);
            if (range.minValue > range.maxValue)
            {
                range.minValue = std::min(range.floorMin, range.roofMin);
                range.maxValue = std::max(range.floorMax, range.roofMax);
            }
            range.valid = true;
        }
        else if (range.hasFloor)
        {
            range.minValue = range.floorMin;
            range.maxValue = range.floorMax;
            range.valid = true;
        }
        else if (range.hasRoof)
        {
            range.minValue = range.roofMin;
            range.maxValue = range.roofMax;
            range.valid = true;
        }

        return range;
    }

    std::vector<std::wstring> TileValuesForTextureDir(const fs::path& textureDir)
    {
        const TileSetRange range = DetectTileSetRange(textureDir);
        std::vector<std::wstring> values;
        if (!range.valid)
        {
            for (int i = 0; i <= 9; ++i)
                values.push_back(std::to_wstring(i));
            return values;
        }

        const int maxCount = 128;
        const int end = std::min(range.maxValue, range.minValue + maxCount - 1);
        for (int i = range.minValue; i <= end; ++i)
            values.push_back(std::to_wstring(i));
        return values;
    }

    std::wstring TileRangeSummary(const fs::path& textureDir)
    {
        const TileSetRange range = DetectTileSetRange(textureDir);
        std::wstringstream s;
        if (!range.valid)
        {
            s << L"Tile sets: not detected, fallback 0-9";
            return s.str();
        }

        s << L"Tile sets: " << range.minValue << L"-" << range.maxValue;
        if (range.hasFloor)
            s << L"   floor " << range.floorMin << L"-" << range.floorMax;
        if (range.hasRoof)
            s << L"   roof " << range.roofMin << L"-" << range.roofMax;
        return s.str();
    }

    std::wstring ProfileSummary(const GameProfile& p)
    {
        std::wstringstream s;
        s << L"Game profile: " << GameTypeName(p.type) << L"\n\n";
        s << L"Root:\n  " << PathText(p.root) << L"\n\n";
        s << L"Maps:\n  " << PathText(p.mapsDir) << L"\n\n";
        s << L"Pictures:\n  " << PathText(p.picturesDir) << L"\n\n";
        s << L"Script:\n  " << PathText(p.scriptFile);
        if (IsCrM2File(p.scriptFile))
            s << L"\n  CrM2 compressed - will be decrunched when opened";
        s << L"\n\n";
        s << L"Objects:\n  " << PathText(p.objectsDir) << L"\n\n";
        s << L"Textures:\n  " << PathText(p.textureDir) << L"\n";
        s << L"  " << TileRangeSummary(p.textureDir) << L"\n";
        s << L"\nStatus: " << (p.valid ? L"ready" : L"incomplete") << L"\n";
        if (!p.warning.empty())
            s << L"\nWarnings:\n" << p.warning;
        return s.str();
    }

    bool BrowseFolderWithTitle(HWND owner, const wchar_t* title, std::wstring& outPath)
    {
        const HRESULT coResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const bool shouldUninit = SUCCEEDED(coResult);

        BROWSEINFOW bi{};
        bi.hwndOwner = owner;
        bi.lpszTitle = title ? title : L"Select folder";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

        PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
        if (!pidl)
        {
            if (shouldUninit)
                CoUninitialize();
            return false;
        }

        wchar_t buffer[MAX_PATH]{};
        const BOOL ok = SHGetPathFromIDListW(pidl, buffer);
        CoTaskMemFree(pidl);

        if (shouldUninit)
            CoUninitialize();

        if (!ok || buffer[0] == 0)
            return false;

        outPath = buffer;
        return true;
    }

    bool BrowseFolder(HWND owner, std::wstring& outPath)
    {
        return BrowseFolderWithTitle(owner, L"Select Gloom game root", outPath);
    }

    void ApplyDarkFrame(HWND hwnd)
    {
        if (!hwnd)
            return;
        BOOL useDark = TRUE;
        DwmSetWindowAttribute(hwnd, 20, &useDark, sizeof(useDark));
        DwmSetWindowAttribute(hwnd, 19, &useDark, sizeof(useDark));
        SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
    }

    std::wstring NormalizeMultilineForWindowsControl(const std::wstring& text)
    {
        std::wstring out;
        out.reserve(text.size() + 16);
        for (size_t i = 0; i < text.size(); ++i)
        {
            const wchar_t ch = text[i];
            if (ch == L'\r')
            {
                out += L"\r\n";
                if (i + 1 < text.size() && text[i + 1] == L'\n')
                    ++i;
            }
            else if (ch == L'\n')
            {
                out += L"\r\n";
            }
            else
            {
                out += ch;
            }
        }
        return out;
    }

    struct DarkMessageDialog
    {
        HWND owner = nullptr;
        HWND hwnd = nullptr;
        HWND textEdit = nullptr;
        HFONT font = nullptr;
        std::wstring title;
        std::wstring message;
        std::vector<std::pair<int, std::wstring>> buttons;
        bool textAsField = false;
        int result = IDCANCEL;
    };

    std::vector<std::pair<int, std::wstring>> ButtonsForMessageFlags(UINT flags)
    {
        switch (flags & MB_TYPEMASK)
        {
        case MB_YESNO:
            return { { IDYES, L"Yes" }, { IDNO, L"No" } };
        case MB_YESNOCANCEL:
            return { { IDYES, L"Yes" }, { IDNO, L"No" }, { IDCANCEL, L"Cancel" } };
        case MB_OKCANCEL:
            return { { IDOK, L"OK" }, { IDCANCEL, L"Cancel" } };
        default:
            return { { IDOK, L"OK" } };
        }
    }

    LRESULT DarkPopupCtlColor(WPARAM wParam, bool field)
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, kText);
        SetBkMode(hdc, TRANSPARENT);
        static HBRUSH bgBrush = CreateSolidBrush(kBg);
        static HBRUSH fieldBrush = CreateSolidBrush(kEditBg);
        if (field)
        {
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, kEditBg);
            return reinterpret_cast<LRESULT>(fieldBrush);
        }
        SetBkColor(hdc, kBg);
        return reinterpret_cast<LRESULT>(bgBrush);
    }

    bool DrawDarkPopupButton(DRAWITEMSTRUCT* draw)
    {
        if (!draw || draw->CtlType != ODT_BUTTON)
            return false;

        const bool disabled = (draw->itemState & ODS_DISABLED) != 0;
        const bool pressed = (draw->itemState & ODS_SELECTED) != 0;
        const bool focus = (draw->itemState & ODS_FOCUS) != 0;
        const COLORREF bg = disabled ? RGB(36, 36, 42) : (pressed ? RGB(54, 54, 64) : kPanelBg);
        const COLORREF border = focus ? kAccent : RGB(78, 78, 88);
        const COLORREF text = disabled ? kMutedText : kText;

        HBRUSH brush = CreateSolidBrush(bg);
        FillRect(draw->hDC, &draw->rcItem, brush);
        DeleteObject(brush);

        HPEN pen = CreatePen(PS_SOLID, 1, border);
        HGDIOBJ oldPen = SelectObject(draw->hDC, pen);
        HGDIOBJ oldBrush = SelectObject(draw->hDC, GetStockObject(NULL_BRUSH));
        Rectangle(draw->hDC, draw->rcItem.left, draw->rcItem.top, draw->rcItem.right, draw->rcItem.bottom);
        SelectObject(draw->hDC, oldBrush);
        SelectObject(draw->hDC, oldPen);
        DeleteObject(pen);

        wchar_t label[128]{};
        GetWindowTextW(draw->hwndItem, label, 128);
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, text);
        RECT rc = draw->rcItem;
        DrawTextW(draw->hDC, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return true;
    }


    bool DrawScriptCard(ScriptEditorDialog* dlg, DRAWITEMSTRUCT* draw)
    {
        if (!dlg || !draw || draw->CtlType != ODT_LISTBOX || draw->CtlID != IDC_SCRIPT_BLOCK_LIST || draw->itemID == static_cast<UINT>(-1))
            return false;

        const bool selected = (draw->itemState & ODS_SELECTED) != 0;
        RECT bgRc = draw->rcItem;
        HBRUSH bgBrush = CreateSolidBrush(kBg);
        FillRect(draw->hDC, &bgRc, bgBrush);
        DeleteObject(bgBrush);

        RECT cardRc = draw->rcItem;
        InflateRect(&cardRc, -6, -5);
        VisualItemType visualType = VisualItemType::Raw;
        if (draw->itemID < dlg->visibleItems.size())
            visualType = dlg->visibleItems[draw->itemID].type;

        const bool episode = visualType == VisualItemType::Episode;
        const COLORREF cardBg = selected
            ? (episode ? RGB(80, 55, 34) : RGB(47, 53, 62))
            : (episode ? RGB(54, 42, 32) : kPanelBg);
        const COLORREF border = selected
            ? (episode ? RGB(214, 137, 62) : kAccent)
            : (episode ? RGB(126, 82, 43) : RGB(72, 72, 78));
        HBRUSH cardBrush = CreateSolidBrush(cardBg);
        FillRect(draw->hDC, &cardRc, cardBrush);
        DeleteObject(cardBrush);

        HPEN pen = CreatePen(PS_SOLID, selected ? 2 : 1, border);
        HGDIOBJ oldPen = SelectObject(draw->hDC, pen);
        HGDIOBJ oldBrush = SelectObject(draw->hDC, GetStockObject(NULL_BRUSH));
        Rectangle(draw->hDC, cardRc.left, cardRc.top, cardRc.right, cardRc.bottom);
        SelectObject(draw->hDC, oldBrush);
        SelectObject(draw->hDC, oldPen);
        DeleteObject(pen);

        int len = static_cast<int>(SendMessageW(draw->hwndItem, LB_GETTEXTLEN, draw->itemID, 0));
        if (len < 0)
            len = 0;
        std::wstring text(static_cast<size_t>(len) + 1, L'\0');
        SendMessageW(draw->hwndItem, LB_GETTEXT, draw->itemID, reinterpret_cast<LPARAM>(text.data()));
        text.resize(static_cast<size_t>(len));

        RECT textRc = cardRc;
        textRc.left += 14;
        textRc.right -= 14;
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, selected ? RGB(245, 245, 245) : kText);
        DrawTextW(draw->hDC, text.c_str(), -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return true;
    }

    void LayoutDarkMessage(DarkMessageDialog* dlg)
    {
        if (!dlg || !dlg->hwnd)
            return;

        RECT rc{};
        GetClientRect(dlg->hwnd, &rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;
        const int margin = 18;
        const int buttonH = 30;
        const int buttonW = 96;
        const int gap = 10;
        const int buttonY = h - margin - buttonH;
        const int textH = buttonY - margin - 12;
        MoveWindow(dlg->textEdit, margin, margin, w - margin * 2, textH, TRUE);

        const int totalButtonsW = static_cast<int>(dlg->buttons.size()) * buttonW + static_cast<int>(dlg->buttons.size() - 1) * gap;
        int x = (w - totalButtonsW) / 2;
        for (const auto& button : dlg->buttons)
        {
            HWND hwndButton = GetDlgItem(dlg->hwnd, button.first);
            if (hwndButton)
            {
                MoveWindow(hwndButton, x, buttonY, buttonW, buttonH, TRUE);
                x += buttonW + gap;
            }
        }
    }

    LRESULT CALLBACK DarkMessageWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        DarkMessageDialog* dlg = reinterpret_cast<DarkMessageDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (msg)
        {
        case WM_NCCREATE:
        {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            dlg = reinterpret_cast<DarkMessageDialog*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dlg));
            dlg->hwnd = hwnd;
            return TRUE;
        }
        case WM_CREATE:
            if (dlg)
            {
                ApplyDarkFrame(hwnd);
                dlg->font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                const std::wstring normalizedMessage = NormalizeMultilineForWindowsControl(dlg->message);
                dlg->textAsField = (dlg->title == L"Campaign - Game Root");
                if (dlg->textAsField)
                {
                    dlg->textEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", normalizedMessage.c_str(),
                        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                        0, 0, 10, 10, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
                }
                else
                {
                    dlg->textEdit = CreateWindowExW(0, L"STATIC", normalizedMessage.c_str(),
                        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                        0, 0, 10, 10, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
                }
                SendMessageW(dlg->textEdit, WM_SETFONT, reinterpret_cast<WPARAM>(dlg->font), TRUE);
                ApplyDarkFrame(dlg->textEdit);

                for (const auto& button : dlg->buttons)
                {
                    HWND b = CreateWindowExW(0, L"BUTTON", button.second.c_str(),
                        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                        0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(button.first)),
                        GetModuleHandleW(nullptr), nullptr);
                    SendMessageW(b, WM_SETFONT, reinterpret_cast<WPARAM>(dlg->font), TRUE);
                    ApplyDarkFrame(b);
                }
                LayoutDarkMessage(dlg);
            }
            return 0;

        case WM_SIZE:
            LayoutDarkMessage(dlg);
            return 0;

        case WM_ERASEBKGND:
        {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            HBRUSH brush = CreateSolidBrush(kBg);
            FillRect(hdc, &rc, brush);
            DeleteObject(brush);
            return 1;
        }

        case WM_CTLCOLORSTATIC:
            return DarkPopupCtlColor(wParam, dlg && dlg->textAsField && reinterpret_cast<HWND>(lParam) == dlg->textEdit);
        case WM_CTLCOLORBTN:
            return DarkPopupCtlColor(wParam, false);
        case WM_CTLCOLOREDIT:
            return DarkPopupCtlColor(wParam, true);

        case WM_DRAWITEM:
            return DrawDarkPopupButton(reinterpret_cast<DRAWITEMSTRUCT*>(lParam)) ? TRUE : FALSE;

        case WM_COMMAND:
            if (dlg)
            {
                const int id = LOWORD(wParam);
                for (const auto& button : dlg->buttons)
                {
                    if (button.first == id)
                    {
                        dlg->result = id;
                        DestroyWindow(hwnd);
                        return 0;
                    }
                }
            }
            break;

        case WM_CLOSE:
            if (dlg)
            {
                if ((dlg->buttons.size() == 1 && dlg->buttons[0].first == IDOK))
                    dlg->result = IDOK;
                else
                    dlg->result = IDCANCEL;
            }
            DestroyWindow(hwnd);
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    int DarkMessageBox(HWND owner, const std::wstring& message, const std::wstring& title, UINT flags)
    {
        static bool registered = false;
        const wchar_t* className = L"ZGloomCampaignDarkMessageWindow";
        if (!registered)
        {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = className;
            wc.lpfnWndProc = DarkMessageWndProc;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = CreateSolidBrush(kBg);
            RegisterClassExW(&wc);
            registered = true;
        }

        DarkMessageDialog dlg;
        dlg.owner = owner;
        dlg.title = title;
        dlg.message = message;
        dlg.buttons = ButtonsForMessageFlags(flags);
        if ((flags & MB_TYPEMASK) == MB_YESNO)
            dlg.result = IDNO;
        else if ((flags & MB_TYPEMASK) == MB_OK)
            dlg.result = IDOK;

        RECT ownerRect{};
        if (owner)
            GetWindowRect(owner, &ownerRect);
        else
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &ownerRect, 0);

        int lineCount = 1;
        for (wchar_t ch : message)
        {
            if (ch == L'\n')
                ++lineCount;
        }

        const bool okOnly = ((flags & MB_TYPEMASK) == MB_OK);
        const bool yesNoCancel = ((flags & MB_TYPEMASK) == MB_YESNOCANCEL);
        const bool gameRootSummary = (title == L"Campaign - Game Root");
        const bool scriptPopup = (title == L"Campaign Script");

        int width = gameRootSummary ? 430 : (scriptPopup ? 440 : 500);
        int height = 170 + std::min(lineCount, 10) * 18;
        if (gameRootSummary)
            height = 348;
        else if (yesNoCancel)
            height = 178;
        else if (okOnly && scriptPopup)
            height = message.find(L"Backup:") != std::wstring::npos ? 238 : 172;
        else if (okOnly)
            height = std::clamp(height, 190, 330);

        const int ownerW = ownerRect.right - ownerRect.left;
        const int ownerH = ownerRect.bottom - ownerRect.top;
        const int x = ownerRect.left + std::max(0, (ownerW - width) / 2);
        const int y = ownerRect.top + std::max(0, (ownerH - height) / 2);

        HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE, className, title.c_str(),
            WS_CAPTION | WS_SYSMENU | WS_POPUP,
            x, y, width, height, owner, nullptr, GetModuleHandleW(nullptr), &dlg);
        if (!hwnd)
            return MessageBoxW(owner, message.c_str(), title.c_str(), flags);

        if (owner)
            EnableWindow(owner, FALSE);
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);

        MSG msg{};
        while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            if (!IsDialogMessageW(hwnd, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        if (owner)
        {
            EnableWindow(owner, TRUE);
            SetForegroundWindow(owner);
        }
        return dlg.result;
    }

    std::array<std::wstring, kRecentCampaignGameRootCount> LoadRecentCampaignGameRoots()
    {
        std::array<std::wstring, kRecentCampaignGameRootCount> roots{};
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kCampaignRegistryKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
            return roots;

        for (int i = 0; i < kRecentCampaignGameRootCount; ++i)
        {
            std::wstringstream valueName;
            valueName << L"CampaignGameRoot" << i;
            wchar_t buffer[MAX_PATH * 4]{};
            DWORD type = REG_SZ;
            DWORD bytes = sizeof(buffer);
            if (RegQueryValueExW(key, valueName.str().c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &bytes) == ERROR_SUCCESS && type == REG_SZ)
                roots[static_cast<size_t>(i)] = buffer;
        }

        RegCloseKey(key);
        return roots;
    }

    void SaveRecentCampaignGameRoots(const std::array<std::wstring, kRecentCampaignGameRootCount>& roots)
    {
        HKEY key = nullptr;
        DWORD disposition = 0;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kCampaignRegistryKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, &disposition) != ERROR_SUCCESS)
            return;

        for (int i = 0; i < kRecentCampaignGameRootCount; ++i)
        {
            std::wstringstream valueName;
            valueName << L"CampaignGameRoot" << i;
            const std::wstring& value = roots[static_cast<size_t>(i)];
            RegSetValueExW(key, valueName.str().c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
        }

        RegCloseKey(key);
    }

    void AddRecentCampaignGameRoot(const fs::path& root)
    {
        if (root.empty())
            return;

        const std::wstring path = root.wstring();
        if (path.empty())
            return;

        std::array<std::wstring, kRecentCampaignGameRootCount> current = LoadRecentCampaignGameRoots();
        std::array<std::wstring, kRecentCampaignGameRootCount> next{};
        next[0] = path;
        int out = 1;
        for (const std::wstring& item : current)
        {
            if (item.empty() || _wcsicmp(item.c_str(), path.c_str()) == 0)
                continue;
            if (out >= kRecentCampaignGameRootCount)
                break;
            next[static_cast<size_t>(out++)] = item;
        }
        SaveRecentCampaignGameRoots(next);
    }

    bool SelectCampaignGameRoot(HWND owner, const fs::path& root)
    {
        g_profile = DetectGameProfile(root);
        if (!g_profile.root.empty())
            AddRecentCampaignGameRoot(g_profile.root);
        DarkMessageBox(owner, ProfileSummary(g_profile), L"Campaign - Game Root", MB_OK | (g_profile.valid ? MB_ICONINFORMATION : MB_ICONWARNING));
        return !g_profile.root.empty();
    }

    bool EnsureProfile(HWND owner)
    {
        if (!g_profile.root.empty())
            return true;

        const int answer = DarkMessageBox(owner,
            L"No game root is selected yet.\n\nChoose one now?",
            L"Campaign", MB_YESNO | MB_ICONQUESTION);
        if (answer != IDYES)
            return false;

        std::wstring folder;
        if (!BrowseFolder(owner, folder))
            return false;

        return SelectCampaignGameRoot(owner, folder);
    }

    std::vector<fs::path> ListRegularFiles(const fs::path& folder)
    {
        std::vector<fs::path> files;
        std::error_code ec;

        if (!DirExists(folder))
            return files;

        for (const fs::directory_entry& entry : fs::directory_iterator(folder, ec))
        {
            if (ec)
                break;
            std::error_code itemEc;
            if (!entry.is_regular_file(itemEc))
                continue;
            files.push_back(entry.path());
        }

        std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
            return Lower(a.filename().wstring()) < Lower(b.filename().wstring());
        });
        return files;
    }

    fs::path FindCaseInsensitiveFile(const fs::path& folder, const std::wstring& wantedName)
    {
        const std::wstring wanted = Lower(wantedName);
        for (const fs::path& file : ListRegularFiles(folder))
        {
            if (Lower(file.filename().wstring()) == wanted)
                return file;
        }
        return {};
    }

    fs::path FindTitlePaletteForPicture(const fs::path& imageFile)
    {
        const std::wstring names[] = { L"title.pal", L"Title.pal", L"TITLE.PAL" };
        for (const std::wstring& name : names)
        {
            fs::path pal = FindCaseInsensitiveFile(imageFile.parent_path(), name);
            if (!pal.empty())
                return pal;
        }
        return {};
    }

    bool IsPaletteFile(const fs::path& file)
    {
        return Lower(file.extension().wstring()) == L".pal";
    }

    bool IsPaletteBackupFile(const fs::path& file)
    {
        const std::wstring name = Lower(file.filename().wstring());
        if (EndsWith(name, L".pal.bak"))
            return true;
        return EndsWith(name, L".bak") && name.find(L".pal.") != std::wstring::npos;
    }

    bool IsLikelyImageAsset(const fs::path& file)
    {
        if (IsPaletteFile(file) || IsPaletteBackupFile(file))
            return false;

        const std::wstring ext = Lower(file.extension().wstring());
        if (ext == L".txt" || ext == L".readme" || ext == L".info")
            return false;

        return true;
    }

    std::wstring AssetCommandName(const fs::path& file)
    {
        const std::wstring ext = Lower(file.extension().wstring());
        if (!ext.empty())
            return file.stem().wstring();
        return file.filename().wstring();
    }

    std::vector<std::wstring> ListAssetCommandNames(const fs::path& folder, bool imagesOnly)
    {
        std::vector<std::wstring> result;
        for (const fs::path& file : ListRegularFiles(folder))
        {
            if (imagesOnly && !IsLikelyImageAsset(file))
                continue;
            if (!imagesOnly && IsPaletteFile(file))
                continue;
            result.push_back(AssetCommandName(file));
        }
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    fs::path FindSiblingPalette(const fs::path& imageFile)
    {
        auto tryName = [&](const std::wstring& name) -> fs::path {
            if (name.empty())
                return {};
            fs::path candidate = imageFile.parent_path() / name;
            if (FileExists(candidate))
                return candidate;
            return FindCaseInsensitiveFile(imageFile.parent_path(), name);
        };

        const std::wstring filename = imageFile.filename().wstring();
        const std::wstring lowerFilename = Lower(filename);
        const std::wstring stem = imageFile.stem().wstring();
        const std::wstring lowerStem = Lower(stem);

        // Normal picture -> palette pairs. Some Gloom Deluxe assets use an
        // image extension such as .bin while the palette is still named by the
        // stem only: blackmagic.bin + blackmagic.pal.
        fs::path pal = tryName(filename + L".pal");
        if (!pal.empty())
            return pal;

        pal = tryName(filename + L".PAL");
        if (!pal.empty())
            return pal;

        pal = tryName(stem + L".pal");
        if (!pal.empty())
            return pal;

        pal = tryName(stem + L".PAL");
        if (!pal.empty())
            return pal;

        // Backup picture -> backup palette pairs generated by the editor:
        //   title.20260512-123456.bak
        //   title.pal.20260512-123456.bak
        // Also support blackmagic.bin backups where the live palette is
        // blackmagic.pal rather than blackmagic.bin.pal.
        if (EndsWith(lowerFilename, L".bak"))
        {
            const std::wstring withoutBak = filename.substr(0, filename.size() - 4U);
            const size_t stampDot = withoutBak.find_last_of(L'.');
            if (stampDot != std::wstring::npos)
            {
                const std::wstring originalFull = withoutBak.substr(0, stampDot);
                const std::wstring stamp = withoutBak.substr(stampDot + 1U);
                const std::wstring originalStem = fs::path(originalFull).stem().wstring();

                const std::wstring candidates[] = {
                    originalFull + L".pal." + stamp + L".bak",
                    originalFull + L".PAL." + stamp + L".bak",
                    originalStem + L".pal." + stamp + L".bak",
                    originalStem + L".PAL." + stamp + L".bak",
                    originalFull + L".pal",
                    originalStem + L".pal"
                };
                for (const std::wstring& candidate : candidates)
                {
                    pal = tryName(candidate);
                    if (!pal.empty())
                        return pal;
                }
            }
        }

        // Gloom Deluxe logo/brush assets named "gloom" or "gloombrush" are
        // drawn with the title screen palette. Do not pair them with a stale
        // gloom.pal from an earlier import, otherwise the logo preview/import
        // looks correct in the editor but wrong on the title screen/Amiga.
        const std::wstring lowerName = Lower(imageFile.filename().wstring());
        const std::wstring lowerPathStem = Lower(imageFile.stem().wstring());
        if (lowerPathStem == L"gloom" || lowerName == L"gloom" ||
            lowerPathStem == L"gloombrush" || lowerName == L"gloombrush")
        {
            if (g_profile.type == GameType::GloomDeluxe)
            {
                pal = FindTitlePaletteForPicture(imageFile);
                if (!pal.empty())
                    return pal;

                for (const fs::path& file : ListRegularFiles(imageFile.parent_path()))
                {
                    const std::wstring candidate = Lower(file.filename().wstring());
                    if (StartsWith(candidate, L"title.pal.") && EndsWith(candidate, L".bak"))
                        return file;
                }
            }
            else
            {
                const std::wstring preferredNames[] = {
                    L"gloom.pal", L"gloom.PAL",
                    L"gloombrush.pal", L"gloombrush.PAL",
                    L"Title.pal", L"title.pal", L"TITLE.PAL",
                    L"blackmagic.pal", L"blackmagic.PAL", L"BLACKMAGIC.PAL"
                };
                for (const std::wstring& preferred : preferredNames)
                {
                    pal = FindCaseInsensitiveFile(imageFile.parent_path(), preferred);
                    if (!pal.empty())
                        return pal;
                }

                for (const fs::path& file : ListRegularFiles(imageFile.parent_path()))
                {
                    const std::wstring candidate = Lower(file.filename().wstring());
                    if ((StartsWith(candidate, L"gloom.pal.") || StartsWith(candidate, L"gloombrush.pal.") ||
                         StartsWith(candidate, L"blackmagic.pal.")) && EndsWith(candidate, L".bak"))
                    {
                        return file;
                    }
                }
                for (const fs::path& file : ListRegularFiles(imageFile.parent_path()))
                {
                    const std::wstring candidate = Lower(file.filename().wstring());
                    if (StartsWith(candidate, L"title.pal.") && EndsWith(candidate, L".bak"))
                        return file;
                }
            }
        }

        return {};
    }

    int PaletteEntriesFromSize(std::uintmax_t bytes)
    {
        if (bytes == 0)
            return 0;

        // Most Gloom picture .pal files in the campaign data are 4 bytes per
        // color (usually 0,R,G,B). Prefer that before the 2-byte Amiga-word
        // fallback; otherwise 1024-byte palettes are misread as 512 words.
        if (bytes % 4 == 0)
            return std::clamp(static_cast<int>(bytes / 4), 1, 256);
        if (bytes % 3 == 0)
            return std::clamp(static_cast<int>(bytes / 3), 1, 256);
        if (bytes % 2 == 0)
            return std::clamp(static_cast<int>(bytes / 2), 1, 256);
        return 0;
    }

    void GuessRawDimensions(PictureInfo& info)
    {
        const std::uintmax_t bytes = info.rawBytes ? info.rawBytes : info.imageBytes;
        if (bytes == 0)
            return;

        struct Candidate { int w; int h; };
        const Candidate candidates[] = {
            {320, 256}, {320, 240}, {320, 200}, {320, 128}, {320, 64},
            {640, 256}, {160, 128}, {160, 100}
        };

        for (const Candidate& c : candidates)
        {
            if (bytes == static_cast<std::uintmax_t>(c.w) * static_cast<std::uintmax_t>(c.h))
            {
                info.guessedWidth = c.w;
                info.guessedHeight = c.h;
                return;
            }
        }

        if (bytes % 320 == 0)
        {
            info.guessedWidth = 320;
            info.guessedHeight = static_cast<int>(bytes / 320);
        }
    }


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

    void WriteBe16(std::vector<std::uint8_t>& data, std::uint16_t value)
    {
        data.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
        data.push_back(static_cast<std::uint8_t>(value & 0xff));
    }

    int DepthForColorCount(int colors)
    {
        int safeColors = std::clamp(colors, 2, 256);
        int depth = 1;
        int capacity = 2;
        while (capacity < safeColors && depth < 8)
        {
            ++depth;
            capacity <<= 1;
        }
        return depth;
    }

    bool DecodeTrimmedIffPicture(const std::vector<std::uint8_t>& data, std::vector<std::uint8_t>& pixels,
        int& width, int& height, int& depth, std::wstring& error)
    {
        pixels.clear();
        width = 0;
        height = 0;
        depth = 0;

        if (data.size() < 12)
        {
            error = L"Picture data is too small for trimmed IFF.";
            return false;
        }

        width = static_cast<int>(ReadBe16(data, 0));
        height = static_cast<int>(ReadBe16(data, 2));
        depth = static_cast<int>(ReadBe16(data, 4));

        if (width <= 0 || width > 1024 || height <= 0 || height > 1024 || depth <= 0 || depth > 8)
        {
            error = L"Picture does not look like a trimmed IFF/ILBM image.";
            return false;
        }

        const int widthBytes = (width + 7) / 8;
        const size_t expectedPixels = static_cast<size_t>(width) * static_cast<size_t>(height);
        if (expectedPixels == 0 || expectedPixels > 1024U * 1024U)
        {
            error = L"Trimmed IFF dimensions are out of range.";
            return false;
        }

        pixels.assign(expectedPixels, 0);
        std::vector<std::uint8_t> row(static_cast<size_t>(widthBytes), 0);
        size_t p = 12; // Gloom/ZGloom trimmed IFF: width, height, depth, then 6 skipped bytes.

        for (int y = 0; y < height; ++y)
        {
            for (int plane = 0; plane < depth; ++plane)
            {
                std::fill(row.begin(), row.end(), 0);
                int xpos = 0;
                while (xpos < widthBytes)
                {
                    if (p >= data.size())
                    {
                        error = L"Trimmed IFF ByteRun1 data ended unexpectedly.";
                        return false;
                    }

                    const std::uint8_t control = data[p++];
                    if (control > 128)
                    {
                        int repeat = 257 - static_cast<int>(control);
                        if (p >= data.size())
                        {
                            error = L"Trimmed IFF ByteRun1 run is incomplete.";
                            return false;
                        }
                        const std::uint8_t value = data[p++];
                        repeat = std::min(repeat, widthBytes - xpos);
                        std::fill(row.begin() + xpos, row.begin() + xpos + repeat, value);
                        xpos += repeat;
                    }
                    else if (control == 128)
                    {
                        break;
                    }
                    else
                    {
                        int count = static_cast<int>(control) + 1;
                        if (p + static_cast<size_t>(count) > data.size())
                        {
                            error = L"Trimmed IFF ByteRun1 literal copy is incomplete.";
                            return false;
                        }
                        const int copyCount = std::min(count, widthBytes - xpos);
                        std::copy(data.begin() + static_cast<std::ptrdiff_t>(p),
                            data.begin() + static_cast<std::ptrdiff_t>(p + static_cast<size_t>(copyCount)),
                            row.begin() + xpos);
                        p += static_cast<size_t>(count);
                        xpos += copyCount;
                    }
                }

                for (int x = 0; x < width; ++x)
                {
                    const std::uint8_t bit = static_cast<std::uint8_t>((row[static_cast<size_t>(x / 8)] >> (7 - (x % 8))) & 1);
                    if (bit)
                        pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] |= static_cast<std::uint8_t>(1U << plane);
                }
            }
        }

        error.clear();
        return true;
    }

    bool DecodeFullIffIlbmPicture(const std::vector<std::uint8_t>& data, std::vector<std::uint8_t>& pixels,
        int& width, int& height, int& depth, std::wstring& error)
    {
        pixels.clear();
        width = 0;
        height = 0;
        depth = 0;

        if (data.size() < 12 || data[0] != 'F' || data[1] != 'O' || data[2] != 'R' || data[3] != 'M')
        {
            error = L"Picture does not look like a full IFF/ILBM image.";
            return false;
        }
        if (data[8] != 'I' || data[9] != 'L' || data[10] != 'B' || data[11] != 'M')
        {
            error = L"IFF file is not ILBM.";
            return false;
        }

        size_t p = 12;
        size_t bmhdOffset = 0;
        size_t bmhdSize = 0;
        size_t bodyOffset = 0;
        size_t bodySize = 0;
        while (p + 8U <= data.size())
        {
            const std::uint32_t chunkSize = ReadBe32(data, p + 4U);
            const size_t chunkData = p + 8U;
            if (chunkData + static_cast<size_t>(chunkSize) > data.size())
                break;

            if (data[p + 0] == 'B' && data[p + 1] == 'M' && data[p + 2] == 'H' && data[p + 3] == 'D')
            {
                bmhdOffset = chunkData;
                bmhdSize = chunkSize;
            }
            else if (data[p + 0] == 'B' && data[p + 1] == 'O' && data[p + 2] == 'D' && data[p + 3] == 'Y')
            {
                bodyOffset = chunkData;
                bodySize = chunkSize;
            }

            p = chunkData + static_cast<size_t>(chunkSize) + (chunkSize & 1U);
        }

        if (bmhdOffset == 0 || bmhdSize < 20 || bodyOffset == 0 || bodySize == 0)
        {
            error = L"IFF/ILBM is missing BMHD or BODY.";
            return false;
        }

        width = static_cast<int>(ReadBe16(data, bmhdOffset + 0U));
        height = static_cast<int>(ReadBe16(data, bmhdOffset + 2U));
        depth = static_cast<int>(data[bmhdOffset + 8U]);
        const std::uint8_t compression = data[bmhdOffset + 10U];
        if (width <= 0 || width > 1024 || height <= 0 || height > 1024 || depth <= 0 || depth > 8)
        {
            error = L"IFF/ILBM dimensions are out of range.";
            return false;
        }

        const int widthBytes = (width + 7) / 8;
        const size_t expectedPixels = static_cast<size_t>(width) * static_cast<size_t>(height);
        pixels.assign(expectedPixels, 0);
        std::vector<std::uint8_t> row(static_cast<size_t>(widthBytes), 0);
        size_t body = bodyOffset;
        const size_t bodyEnd = bodyOffset + bodySize;

        for (int y = 0; y < height; ++y)
        {
            for (int plane = 0; plane < depth; ++plane)
            {
                std::fill(row.begin(), row.end(), 0);
                int xpos = 0;
                if (compression == 1)
                {
                    while (xpos < widthBytes)
                    {
                        if (body >= bodyEnd)
                        {
                            error = L"IFF/ILBM ByteRun1 data ended unexpectedly.";
                            return false;
                        }
                        const std::uint8_t control = data[body++];
                        if (control > 128)
                        {
                            int repeat = 257 - static_cast<int>(control);
                            if (body >= bodyEnd)
                            {
                                error = L"IFF/ILBM ByteRun1 run is incomplete.";
                                return false;
                            }
                            const std::uint8_t value = data[body++];
                            repeat = std::min(repeat, widthBytes - xpos);
                            std::fill(row.begin() + xpos, row.begin() + xpos + repeat, value);
                            xpos += repeat;
                        }
                        else if (control == 128)
                        {
                            break;
                        }
                        else
                        {
                            int count = static_cast<int>(control) + 1;
                            if (body + static_cast<size_t>(count) > bodyEnd)
                            {
                                error = L"IFF/ILBM ByteRun1 literal copy is incomplete.";
                                return false;
                            }
                            const int copyCount = std::min(count, widthBytes - xpos);
                            std::copy(data.begin() + static_cast<std::ptrdiff_t>(body),
                                data.begin() + static_cast<std::ptrdiff_t>(body + static_cast<size_t>(copyCount)),
                                row.begin() + xpos);
                            body += static_cast<size_t>(count);
                            xpos += copyCount;
                        }
                    }
                }
                else if (compression == 0)
                {
                    if (body + static_cast<size_t>(widthBytes) > bodyEnd)
                    {
                        error = L"IFF/ILBM raw BODY is incomplete.";
                        return false;
                    }
                    std::copy(data.begin() + static_cast<std::ptrdiff_t>(body),
                        data.begin() + static_cast<std::ptrdiff_t>(body + static_cast<size_t>(widthBytes)),
                        row.begin());
                    body += static_cast<size_t>(widthBytes);
                }
                else
                {
                    error = L"IFF/ILBM uses unsupported compression.";
                    return false;
                }

                for (int x = 0; x < width; ++x)
                {
                    const std::uint8_t bit = static_cast<std::uint8_t>((row[static_cast<size_t>(x / 8)] >> (7 - (x % 8))) & 1);
                    if (bit)
                        pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] |= static_cast<std::uint8_t>(1U << plane);
                }
            }
        }

        error.clear();
        return true;
    }

    bool DecodeRawPlanarPicture(const std::vector<std::uint8_t>& data, std::vector<std::uint8_t>& pixels,
        int& width, int& height, int& depth, std::wstring& error)
    {
        pixels.clear();
        width = 0;
        height = 0;
        depth = 0;

        if (data.size() < 12)
        {
            error = L"Picture data is too small for raw planar header.";
            return false;
        }

        width = static_cast<int>(ReadBe16(data, 0));
        height = static_cast<int>(ReadBe16(data, 2));
        depth = static_cast<int>(ReadBe16(data, 4));
        if (width <= 0 || width > 1024 || height <= 0 || height > 1024 || depth <= 0 || depth > 8)
        {
            error = L"Picture does not look like raw planar data with a Gloom mini header.";
            return false;
        }

        const int widthBytes = (width + 7) / 8;
        const size_t expectedPayload = static_cast<size_t>(widthBytes) * static_cast<size_t>(height) * static_cast<size_t>(depth);
        const size_t expectedPixels = static_cast<size_t>(width) * static_cast<size_t>(height);
        if (expectedPixels == 0 || expectedPixels > 1024U * 1024U)
        {
            error = L"Raw planar dimensions are out of range.";
            return false;
        }
        if (data.size() < 12U + expectedPayload)
        {
            error = L"Raw planar data is too small for its header dimensions.";
            return false;
        }

        pixels.assign(expectedPixels, 0);
        size_t p = 12;
        for (int y = 0; y < height; ++y)
        {
            for (int plane = 0; plane < depth; ++plane)
            {
                const size_t rowStart = p;
                p += static_cast<size_t>(widthBytes);
                for (int x = 0; x < width; ++x)
                {
                    const std::uint8_t bit = static_cast<std::uint8_t>((data[rowStart + static_cast<size_t>(x / 8)] >> (7 - (x % 8))) & 1);
                    if (bit)
                        pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] |= static_cast<std::uint8_t>(1U << plane);
                }
            }
        }

        error.clear();
        return true;
    }

    std::vector<std::uint8_t> EncodeRawPlanarPicture(const std::vector<std::uint8_t>& pixels, int width, int height, int depth)
    {
        std::vector<std::uint8_t> out;
        depth = std::clamp(depth, 1, 8);
        WriteBe16(out, static_cast<std::uint16_t>(std::clamp(width, 1, 1024)));
        WriteBe16(out, static_cast<std::uint16_t>(std::clamp(height, 1, 1024)));
        WriteBe16(out, static_cast<std::uint16_t>(depth));
        out.insert(out.end(), 6, 0);

        const int widthBytes = (width + 7) / 8;
        std::vector<std::uint8_t> row(static_cast<size_t>(widthBytes), 0);
        for (int y = 0; y < height; ++y)
        {
            for (int plane = 0; plane < depth; ++plane)
            {
                std::fill(row.begin(), row.end(), 0);
                for (int x = 0; x < width; ++x)
                {
                    const size_t pixelIndex = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                    const std::uint8_t index = pixelIndex < pixels.size() ? pixels[pixelIndex] : 0;
                    if ((index >> plane) & 1U)
                        row[static_cast<size_t>(x / 8)] |= static_cast<std::uint8_t>(1U << (7 - (x % 8)));
                }
                out.insert(out.end(), row.begin(), row.end());
            }
        }
        return out;
    }

    void AppendByteRunLiteral(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& row, size_t start, size_t count)
    {
        while (count > 0)
        {
            const size_t chunk = std::min<size_t>(count, 128);
            out.push_back(static_cast<std::uint8_t>(chunk - 1));
            out.insert(out.end(), row.begin() + static_cast<std::ptrdiff_t>(start), row.begin() + static_cast<std::ptrdiff_t>(start + chunk));
            start += chunk;
            count -= chunk;
        }
    }

    void AppendByteRunRepeat(std::vector<std::uint8_t>& out, std::uint8_t value, size_t count)
    {
        while (count > 0)
        {
            const size_t chunk = std::min<size_t>(count, 128);
            out.push_back(static_cast<std::uint8_t>(257 - static_cast<int>(chunk)));
            out.push_back(value);
            count -= chunk;
        }
    }

    void EncodeByteRun1Row(const std::vector<std::uint8_t>& row, std::vector<std::uint8_t>& out)
    {
        size_t i = 0;
        while (i < row.size())
        {
            size_t run = 1;
            while (i + run < row.size() && run < 128 && row[i + run] == row[i])
                ++run;

            if (run >= 3)
            {
                AppendByteRunRepeat(out, row[i], run);
                i += run;
                continue;
            }

            const size_t literalStart = i;
            i += run;
            while (i < row.size())
            {
                run = 1;
                while (i + run < row.size() && run < 128 && row[i + run] == row[i])
                    ++run;
                if (run >= 3)
                    break;
                i += run;
                if (i - literalStart >= 128)
                    break;
            }
            AppendByteRunLiteral(out, row, literalStart, i - literalStart);
        }
    }

    std::vector<std::uint8_t> EncodeTrimmedIffPicture(const std::vector<std::uint8_t>& pixels, int width, int height, int depth)
    {
        std::vector<std::uint8_t> out;
        depth = std::clamp(depth, 1, 8);
        WriteBe16(out, static_cast<std::uint16_t>(std::clamp(width, 1, 1024)));
        WriteBe16(out, static_cast<std::uint16_t>(std::clamp(height, 1, 1024)));
        WriteBe16(out, static_cast<std::uint16_t>(depth));
        out.insert(out.end(), 6, 0);

        const int widthBytes = (width + 7) / 8;
        std::vector<std::uint8_t> row(static_cast<size_t>(widthBytes), 0);
        for (int y = 0; y < height; ++y)
        {
            for (int plane = 0; plane < depth; ++plane)
            {
                std::fill(row.begin(), row.end(), 0);
                for (int x = 0; x < width; ++x)
                {
                    const size_t pixelIndex = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                    const std::uint8_t index = pixelIndex < pixels.size() ? pixels[pixelIndex] : 0;
                    if ((index >> plane) & 1U)
                        row[static_cast<size_t>(x / 8)] |= static_cast<std::uint8_t>(1U << (7 - (x % 8)));
                }
                EncodeByteRun1Row(row, out);
            }
        }
        return out;
    }

    PictureInfo AnalyzePicture(const fs::path& imageFile)
    {
        PictureInfo info;
        info.imageFile = imageFile;
        info.exists = FileExists(imageFile);
        if (!info.exists)
            return info;

        info.imageBytes = FileSizeOrZero(imageFile);
        info.isCrM2 = IsCrM2File(imageFile);

        std::vector<std::uint8_t> raw;
        std::wstring error;
        if (LoadMaybeCrM2(imageFile, raw, error))
        {
            info.rawBytes = raw.size();
            info.decrunched = info.isCrM2;

            std::vector<std::uint8_t> decodedPixels;
            int decodedWidth = 0;
            int decodedHeight = 0;
            int decodedDepth = 0;
            std::wstring decodeError;
            if (DecodeTrimmedIffPicture(raw, decodedPixels, decodedWidth, decodedHeight, decodedDepth, decodeError))
            {
                info.isTrimmedIff = true;
                info.guessedWidth = decodedWidth;
                info.guessedHeight = decodedHeight;
                info.bitDepth = decodedDepth;
            }
            else
            {
                std::wstring fullIffError;
                if (DecodeFullIffIlbmPicture(raw, decodedPixels, decodedWidth, decodedHeight, decodedDepth, fullIffError))
                {
                    info.isFullIff = true;
                    info.guessedWidth = decodedWidth;
                    info.guessedHeight = decodedHeight;
                    info.bitDepth = decodedDepth;
                }
                else
                {
                    std::wstring rawPlanarError;
                    if (DecodeRawPlanarPicture(raw, decodedPixels, decodedWidth, decodedHeight, decodedDepth, rawPlanarError))
                    {
                        info.isRawPlanar = true;
                        info.guessedWidth = decodedWidth;
                        info.guessedHeight = decodedHeight;
                        info.bitDepth = decodedDepth;
                    }
                }
            }
        }
        else
        {
            info.error = error;
        }

        info.paletteFile = FindSiblingPalette(imageFile);
        info.hasPalette = !info.paletteFile.empty();
        if (info.hasPalette)
        {
            info.paletteBytes = FileSizeOrZero(info.paletteFile);
            info.paletteEntries = PaletteEntriesFromSize(info.paletteBytes);

            std::vector<std::uint8_t> decodedPaletteBytes;
            std::wstring decodedPaletteError;
            if (LoadMaybeCrM2(info.paletteFile, decodedPaletteBytes, decodedPaletteError))
            {
                info.paletteBytes = decodedPaletteBytes.size();
                info.paletteEntries = PaletteEntriesFromSize(info.paletteBytes);
            }

            PaletteData palette;
            std::wstring paletteError;
            if (ReadPalette(info.paletteFile, palette, paletteError))
            {
                info.paletteEntries = static_cast<int>(palette.colors.size());
                info.paletteEntryBytes = palette.entryBytes;
                info.paletteLooksNibble = palette.nibbleSource;
            }
        }
        if (!info.isTrimmedIff && !info.isFullIff && !info.isRawPlanar)
            GuessRawDimensions(info);
        if (!info.hasPalette && info.bitDepth > 0)
        {
            info.usesFallbackPalette = true;
            info.paletteEntries = std::clamp(1 << std::clamp(info.bitDepth, 1, 8), 2, 256);
            info.paletteEntryBytes = 4;
        }
        return info;
    }

    std::wstring PictureSummary(const PictureInfo& info)
    {
        std::wstringstream s;
        if (!info.exists)
        {
            s << L"Picture file not found.";
            return s.str();
        }

        s << L"Image: " << info.imageFile.wstring() << L"\n";
        s << L"Stored bytes: " << info.imageBytes << L"\n";
        s << L"Compression: " << (info.isCrM2 ? L"CrM2 compressed" : L"raw/uncompressed") << L"\n";
        if (info.rawBytes > 0)
            s << L"Raw bytes: " << info.rawBytes << (info.decrunched ? L" (decrunched)" : L"") << L"\n";
        if (!info.error.empty())
            s << L"Read warning: " << info.error << L"\n";
        if (info.isTrimmedIff)
        {
            s << L"Picture format: trimmed IFF/ILBM, ByteRun1 planar\n";
            s << L"Image size: " << info.guessedWidth << L"x" << info.guessedHeight << L"\n";
            s << L"Bitplanes/depth: " << info.bitDepth << L" (" << (1 << std::clamp(info.bitDepth, 0, 8)) << L" color slots)\n";
        }
        else if (info.isFullIff)
        {
            s << L"Picture format: full IFF/ILBM, planar\n";
            s << L"Image size: " << info.guessedWidth << L"x" << info.guessedHeight << L"\n";
            s << L"Bitplanes/depth: " << info.bitDepth << L" (" << (1 << std::clamp(info.bitDepth, 0, 8)) << L" color slots)\n";
        }
        else if (info.isRawPlanar)
        {
            s << L"Picture format: raw planar with Gloom mini header\n";
            s << L"Image size: " << info.guessedWidth << L"x" << info.guessedHeight << L"\n";
            s << L"Bitplanes/depth: " << info.bitDepth << L" (" << (1 << std::clamp(info.bitDepth, 0, 8)) << L" color slots)\n";
        }
        else if (info.guessedWidth > 0 && info.guessedHeight > 0)
            s << L"Guessed raw size: " << info.guessedWidth << L"x" << info.guessedHeight << L"\n";
        else
            s << L"Raw size: unknown\n";

        s << L"Palette: " << (info.hasPalette ? info.paletteFile.wstring() : (info.usesFallbackPalette ? L"generated grayscale fallback" : L"not found")) << L"\n";
        if (info.hasPalette)
        {
            s << L"Palette bytes: " << info.paletteBytes << L"\n";
            s << L"Palette entries/colors: " << (info.paletteEntries > 0 ? std::to_wstring(info.paletteEntries) : L"unknown") << L"\n";
        }

        return s.str();
    }

    fs::path FindTitleImage(const fs::path& pictureDir)
    {
        if (!DirExists(pictureDir))
            return {};

        for (const fs::path& file : ListRegularFiles(pictureDir))
        {
            const std::wstring name = Lower(file.filename().wstring());
            if (name == L"title" || name == L"title.crm" || name == L"title.crm2")
                return file;
        }

        for (const fs::path& file : ListRegularFiles(pictureDir))
        {
            const std::wstring filename = Lower(file.filename().wstring());
            const std::wstring stem = Lower(file.stem().wstring());
            if (IsPaletteFile(file) || IsPaletteBackupFile(file))
                continue;
            if (stem == L"title")
                return file;
        }

        // ZGloom falls back to blackmagic when a dedicated title picture is
        // absent. Keep the same behaviour in the editor-side title manager.
        for (const fs::path& file : ListRegularFiles(pictureDir))
        {
            const std::wstring filename = Lower(file.filename().wstring());
            const std::wstring stem = Lower(file.stem().wstring());
            if (IsPaletteFile(file) || IsPaletteBackupFile(file))
                continue;
            if (stem == L"blackmagic" || filename == L"blackmagic")
                return file;
        }

        return {};
    }

    std::vector<PictureInfo> ListPictureAssets(const fs::path& pictureDir)
    {
        std::vector<PictureInfo> result;
        for (const fs::path& file : ListRegularFiles(pictureDir))
        {
            if (!IsLikelyImageAsset(file))
                continue;
            result.push_back(AnalyzePicture(file));
        }
        return result;
    }

    std::wstring PictureAssetListSummary(const std::vector<PictureInfo>& pictures, size_t maxItems)
    {
        std::wstringstream s;
        s << L"Picture assets found: " << pictures.size() << L"\n\n";

        const size_t count = std::min(maxItems, pictures.size());
        for (size_t i = 0; i < count; ++i)
        {
            const PictureInfo& p = pictures[i];
            s << L"- " << p.imageFile.filename().wstring();
            if (p.isCrM2)
                s << L" [CrM2]";
            if (p.paletteEntries > 0)
                s << L" [" << p.paletteEntries << L" colors]";
            if (p.guessedWidth > 0 && p.guessedHeight > 0)
                s << L" [" << p.guessedWidth << L"x" << p.guessedHeight << L"]";
            else if (p.rawBytes > 0)
                s << L" [raw bytes " << p.rawBytes << L"]";
            s << L"\n";
        }

        if (pictures.size() > count)
            s << L"\n..." << (pictures.size() - count) << L" more item(s).\n";

        return s.str();
    }

    std::wstring ReadTextPreview(const fs::path& file, size_t maxLines)
    {
        std::vector<std::uint8_t> bytes;
        std::wstring error;
        if (!LoadMaybeCrM2(file, bytes, error))
            return error.empty() ? L"Unable to read script file." : error;

        std::string rawText(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        std::istringstream in(rawText);
        std::ostringstream raw;
        std::string line;
        size_t lines = 0;
        while (lines < maxLines && std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            raw << line << "\r\n";
            ++lines;
        }
        return ToWideAcp(raw.str());
    }

    size_t CountLines(const fs::path& file)
    {
        std::vector<std::uint8_t> bytes;
        std::wstring error;
        if (!LoadMaybeCrM2(file, bytes, error))
            return 0;

        if (bytes.empty())
            return 0;

        size_t lines = 0;
        bool previousWasBreak = true;
        for (size_t i = 0; i < bytes.size(); ++i)
        {
            if (bytes[i] == '\r' || bytes[i] == '\n')
            {
                if (!previousWasBreak)
                    ++lines;
                previousWasBreak = true;
                if (bytes[i] == '\r' && i + 1 < bytes.size() && bytes[i + 1] == '\n')
                    ++i;
            }
            else
            {
                previousWasBreak = false;
            }
        }
        if (!previousWasBreak)
            ++lines;
        return lines;
    }

    bool IsEditableScriptBlockType(ScriptBlockType type)
    {
        return type == ScriptBlockType::Picture ||
            type == ScriptBlockType::Tile ||
            type == ScriptBlockType::Text ||
            type == ScriptBlockType::PlayMap ||
            type == ScriptBlockType::EndGame ||
            type == ScriptBlockType::Raw;
    }

    bool IsFixedScriptBlockType(ScriptBlockType type)
    {
        return !IsEditableScriptBlockType(type);
    }

    ScriptBlockType TypeFromIndex(int index)
    {
        switch (index)
        {
        case 0: return ScriptBlockType::Picture;
        case 1: return ScriptBlockType::Tile;
        case 2: return ScriptBlockType::Text;
        case 3: return ScriptBlockType::PlayMap;
        case 4: return ScriptBlockType::EndGame;
        default: return ScriptBlockType::Raw;
        }
    }

    int IndexFromType(ScriptBlockType type)
    {
        switch (type)
        {
        case ScriptBlockType::Picture: return 0;
        case ScriptBlockType::Tile: return 1;
        case ScriptBlockType::Text: return 2;
        case ScriptBlockType::PlayMap: return 3;
        case ScriptBlockType::EndGame: return 4;
        case ScriptBlockType::Raw: return 5;
        default: return -1;
        }
    }

    const wchar_t* TypeName(ScriptBlockType type)
    {
        switch (type)
        {
        case ScriptBlockType::Picture: return L"Picture";
        case ScriptBlockType::Tile: return L"Tile Set";
        case ScriptBlockType::Draw: return L"Draw";
        case ScriptBlockType::Show: return L"Show";
        case ScriptBlockType::Text: return L"Intermission Text";
        case ScriptBlockType::Wait: return L"Wait";
        case ScriptBlockType::Dark: return L"Dark/Fade";
        case ScriptBlockType::PlayMap: return L"Play Map";
        case ScriptBlockType::Song: return L"Song/Music";
        case ScriptBlockType::Rest: return L"Rest/Legacy Marker";
        case ScriptBlockType::EndGame: return L"End Game";
        case ScriptBlockType::Comment: return L"Script Header/Comment";
        default: return L"Raw Command";
        }
    }

    bool TypeUsesValue(ScriptBlockType type)
    {
        return type == ScriptBlockType::Picture ||
            type == ScriptBlockType::Tile ||
            type == ScriptBlockType::Text ||
            type == ScriptBlockType::PlayMap ||
            type == ScriptBlockType::EndGame ||
            type == ScriptBlockType::Raw;
    }

    bool TypeUsesMultiLineEdit(ScriptBlockType type)
    {
        return type == ScriptBlockType::Text || type == ScriptBlockType::Raw;
    }

    const wchar_t* ValueLabelForType(ScriptBlockType type)
    {
        switch (type)
        {
        case ScriptBlockType::Picture: return L"Picture name";
        case ScriptBlockType::Tile: return L"Tile set";
        case ScriptBlockType::Text: return L"Text";
        case ScriptBlockType::PlayMap: return L"Map";
        case ScriptBlockType::Song: return L"Song/Music";
        case ScriptBlockType::Rest: return L"Rest/legacy marker";
        case ScriptBlockType::EndGame: return L"End Game";
        case ScriptBlockType::Comment: return L"Script header/comment";
        case ScriptBlockType::Raw: return L"Raw script line";
        default: return L"Value";
        }
    }

    const wchar_t* HelpTextForType(ScriptBlockType type)
    {
        switch (type)
        {
        case ScriptBlockType::Picture:
            return L"Sets the intermission/title picture. It stays active until another pict_ command appears.";
        case ScriptBlockType::Tile:
            return L"Selects the floor/ceiling tile set for the following episode/level. The list is detected from txts/floor* and txts/roof*.";
        case ScriptBlockType::Draw:
            return L"Keeps the current screen/intermission draw step. No value is required.";
        case ScriptBlockType::Show:
            return L"Original-script command kept for compatibility. No value is required.";
        case ScriptBlockType::Text:
            return L"Sets the intermission text shown with the currently active picture.";
        case ScriptBlockType::Wait:
            return L"Waits on the current intermission screen before continuing.";
        case ScriptBlockType::Dark:
            return L"Original fade/dark command. Kept even if modern ports may ignore it.";
        case ScriptBlockType::PlayMap:
            return L"Loads the selected map/level as the next gameplay step.";
        case ScriptBlockType::Song:
            return L"Legacy song_ command, preserved for compatibility if present.";
        case ScriptBlockType::Rest:
            return L"Legacy rest_ marker. Preserved for original-script compatibility and hidden from the normal builder view.";
        case ScriptBlockType::EndGame:
            return L"Writes done_ and ends the campaign/script flow.";
        case ScriptBlockType::Comment:
            return L"Script header/comment. Preserved or generated automatically and hidden from the normal builder view.";
        default:
            return L"Unknown command preserved exactly as a raw script line.";
        }
    }

    std::wstring SanitizeScriptLineValue(std::wstring value)
    {
        for (wchar_t& ch : value)
        {
            if (ch == L'\r' || ch == L'\n')
                ch = L' ';
        }
        return value;
    }

    ScriptBlock ParseScriptLine(const std::string& originalBytes, const std::string& lineEndingBytes)
    {
        const std::wstring line = ToWideAcp(originalBytes);

        ScriptBlock block;
        block.rawLine = line;
        block.originalLineBytes = originalBytes;
        block.lineEndingBytes = lineEndingBytes;
        block.hasOriginal = true;

        const std::wstring lowerLine = Lower(line);
        if (!line.empty() && line[0] == L';')
        {
            block.type = ScriptBlockType::Comment;
            block.value = line;
        }
        else if (StartsWith(lowerLine, L"pict_"))
        {
            block.type = ScriptBlockType::Picture;
            block.value = line.substr(5);
        }
        else if (StartsWith(lowerLine, L"tile_"))
        {
            block.type = ScriptBlockType::Tile;
            block.value = line.substr(5);
        }
        else if (StartsWith(lowerLine, L"draw_"))
        {
            block.type = ScriptBlockType::Draw;
        }
        else if (StartsWith(lowerLine, L"show_"))
        {
            block.type = ScriptBlockType::Show;
        }
        else if (StartsWith(lowerLine, L"text_"))
        {
            block.type = ScriptBlockType::Text;
            block.value = line.substr(5);
        }
        else if (StartsWith(lowerLine, L"wait_"))
        {
            block.type = ScriptBlockType::Wait;
        }
        else if (StartsWith(lowerLine, L"dark_"))
        {
            block.type = ScriptBlockType::Dark;
        }
        else if (StartsWith(lowerLine, L"play_"))
        {
            block.type = ScriptBlockType::PlayMap;
            block.value = line.substr(5);
        }
        else if (StartsWith(lowerLine, L"song_"))
        {
            block.type = ScriptBlockType::Song;
            block.value = line.substr(5);
        }
        else if (StartsWith(lowerLine, L"rest_"))
        {
            block.type = ScriptBlockType::Rest;
            block.value = line.substr(5);
        }
        else if (StartsWith(lowerLine, L"done_"))
        {
            block.type = ScriptBlockType::EndGame;
        }
        else
        {
            block.type = ScriptBlockType::Raw;
            block.value = line;
        }

        return block;
    }

    ScriptBlock ParseNewScriptLine(const std::wstring& line)
    {
        return ParseScriptLine(ToAcp(line), std::string());
    }

    std::wstring ScriptLineFromBlock(const ScriptBlock& block)
    {
        switch (block.type)
        {
        case ScriptBlockType::Picture: return L"pict_" + SanitizeScriptLineValue(block.value);
        case ScriptBlockType::Tile: return L"tile_" + SanitizeScriptLineValue(block.value);
        case ScriptBlockType::Draw: return L"draw_";
        case ScriptBlockType::Show: return L"show_";
        case ScriptBlockType::Text: return L"text_" + SanitizeScriptLineValue(block.value);
        case ScriptBlockType::Wait: return L"wait_";
        case ScriptBlockType::Dark: return L"dark_";
        case ScriptBlockType::PlayMap: return L"play_" + SanitizeScriptLineValue(block.value);
        case ScriptBlockType::Song: return L"song_" + SanitizeScriptLineValue(block.value);
        case ScriptBlockType::Rest: return L"rest_" + SanitizeScriptLineValue(block.value);
        case ScriptBlockType::EndGame: return L"done_";
        case ScriptBlockType::Comment: return SanitizeScriptLineValue(block.value.empty() ? block.rawLine : block.value);
        default:
            return SanitizeScriptLineValue(block.value.empty() ? block.rawLine : block.value);
        }
    }

    bool CanPreserveOriginalLine(const ScriptBlock& block)
    {
        return block.hasOriginal && ScriptLineFromBlock(block) == block.rawLine;
    }

    std::wstring Shorten(const std::wstring& value, size_t maxLen)
    {
        if (value.size() <= maxLen)
            return value;
        if (maxLen <= 3)
            return value.substr(0, maxLen);
        return value.substr(0, maxLen - 3) + L"...";
    }

    std::wstring DisplayTextForBlock(const ScriptBlock& block, int oneBasedIndex)
    {
        std::wstringstream s;
        s << oneBasedIndex << L". ";
        switch (block.type)
        {
        case ScriptBlockType::Picture:
            s << L"Episode picture: " << (block.value.empty() ? L"(empty)" : block.value);
            break;
        case ScriptBlockType::Tile:
            s << L"Episode tile set: " << (block.value.empty() ? L"(empty)" : block.value);
            break;
        case ScriptBlockType::Text:
            s << L"Level text: " << Shorten(block.value, 74);
            break;
        case ScriptBlockType::PlayMap:
            s << L"Play level: " << (block.value.empty() ? L"(empty)" : block.value);
            break;
        case ScriptBlockType::Song:
            s << L"Music: " << (block.value.empty() ? L"(empty)" : block.value);
            break;
        case ScriptBlockType::Rest:
            s << L"Rest marker: " << (block.value.empty() ? L"(empty)" : block.value);
            break;
        case ScriptBlockType::EndGame:
            s << L"End game";
            break;
        case ScriptBlockType::Comment:
            s << L"Comment/header";
            break;
        case ScriptBlockType::Raw:
            s << L"Raw command: " << Shorten(ScriptLineFromBlock(block), 74);
            break;
        default:
            s << L"Fixed command: " << ScriptLineFromBlock(block);
            break;
        }
        return s.str();
    }

    std::string DetectFallbackLineEnding(const std::vector<ScriptBlock>& blocks)
    {
        size_t crlf = 0;
        size_t lf = 0;
        size_t cr = 0;
        for (const ScriptBlock& block : blocks)
        {
            if (block.lineEndingBytes == "\r\n")
                ++crlf;
            else if (block.lineEndingBytes == "\n")
                ++lf;
            else if (block.lineEndingBytes == "\r")
                ++cr;
        }
        if (crlf >= lf && crlf >= cr && crlf > 0)
            return "\r\n";
        if (lf >= cr && lf > 0)
            return "\n";
        if (cr > 0)
            return "\r";
        return "\n";
    }

    bool LoadScriptBlocks(const fs::path& path, std::vector<ScriptBlock>& blocks, std::wstring& error)
    {
        std::vector<std::uint8_t> bytes;
        if (!LoadMaybeCrM2(path, bytes, error))
        {
            if (error.empty())
                error = L"Could not open campaign script.";
            else
                error = L"Could not open campaign script:\n" + error;
            return false;
        }

        blocks.clear();
        size_t pos = 0;
        while (pos < bytes.size())
        {
            const size_t lineStart = pos;
            while (pos < bytes.size() && bytes[pos] != '\r' && bytes[pos] != '\n')
                ++pos;

            std::string lineBytes(reinterpret_cast<const char*>(bytes.data() + lineStart), pos - lineStart);
            std::string eol;
            if (pos < bytes.size())
            {
                if (bytes[pos] == '\r' && pos + 1 < bytes.size() && bytes[pos + 1] == '\n')
                {
                    eol = "\r\n";
                    pos += 2;
                }
                else
                {
                    eol.assign(1, static_cast<char>(bytes[pos]));
                    ++pos;
                }
            }

            blocks.push_back(ParseScriptLine(lineBytes, eol));
        }
        return true;
    }

    std::wstring TimestampForBackup()
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        wchar_t buffer[64]{};
        swprintf_s(buffer, L"%04u%02u%02u-%02u%02u%02u",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        return buffer;
    }

    bool BackupFile(const fs::path& path, fs::path& backupPath, std::wstring& error)
    {
        std::error_code ec;
        if (!FileExists(path))
            return true;

        backupPath = path;
        backupPath += L"." + TimestampForBackup() + L".bak";
        fs::copy_file(path, backupPath, fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            error = L"Could not create backup:\n" + backupPath.wstring() + L"\n\n" + ToWideAcp(ec.message());
            return false;
        }
        return true;
    }


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
            if (Ok())
                Gdiplus::GdiplusShutdown(token_);
        }
        bool Ok() const { return status_ == Gdiplus::Ok; }

    private:
        ULONG_PTR token_ = 0;
        Gdiplus::Status status_ = Gdiplus::GenericError;
    };

    int GetEncoderClsid(const WCHAR* format, CLSID* pClsid)
    {
        UINT num = 0;
        UINT size = 0;
        Gdiplus::GetImageEncodersSize(&num, &size);
        if (size == 0)
            return -1;

        std::vector<std::uint8_t> buffer(size);
        auto* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
        Gdiplus::GetImageEncoders(num, size, info);

        for (UINT i = 0; i < num; ++i)
        {
            if (wcscmp(info[i].MimeType, format) == 0)
            {
                *pClsid = info[i].Clsid;
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    const wchar_t* PngFileDialogFilter()
    {
        static const wchar_t filter[] = L"PNG image (*.png)\0*.png\0All files (*.*)\0*.*\0\0";
        return filter;
    }

    bool BrowseSavePng(HWND owner, const std::wstring& suggestedName, fs::path& outPath)
    {
        wchar_t fileName[MAX_PATH]{};
        wcsncpy_s(fileName, suggestedName.c_str(), _TRUNCATE);

        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = owner;
        ofn.lpstrFilter = PngFileDialogFilter();
        ofn.lpstrFile = fileName;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrDefExt = L"png";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&ofn))
            return false;
        outPath = fileName;
        return true;
    }

    bool BrowseOpenPng(HWND owner, fs::path& outPath)
    {
        wchar_t fileName[MAX_PATH]{};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = owner;
        ofn.lpstrFilter = PngFileDialogFilter();
        ofn.lpstrFile = fileName;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&ofn))
            return false;
        outPath = fileName;
        return true;
    }

    std::uint8_t ExpandPaletteComponent(std::uint8_t value, bool nibble)
    {
        if (nibble)
            return static_cast<std::uint8_t>((value & 0x0fU) * 17U);
        return value;
    }

    std::uint8_t CompressPaletteComponent(std::uint8_t value, bool nibble)
    {
        if (!nibble)
            return value;
        return static_cast<std::uint8_t>((static_cast<int>(value) + 8) / 17) & 0x0fU;
    }

    RgbColor DecodeAmigaPaletteWord(std::uint16_t word)
    {
        // Native Gloom/Amiga palette words are stored as 0RGB, big-endian.
        RgbColor c{};
        c.r = ExpandPaletteComponent(static_cast<std::uint8_t>((word >> 8) & 0x000fU), true);
        c.g = ExpandPaletteComponent(static_cast<std::uint8_t>((word >> 4) & 0x000fU), true);
        c.b = ExpandPaletteComponent(static_cast<std::uint8_t>(word & 0x000fU), true);
        return c;
    }

    std::uint16_t EncodeAmigaPaletteWord(const RgbColor& c)
    {
        const std::uint16_t r = static_cast<std::uint16_t>(CompressPaletteComponent(c.r, true));
        const std::uint16_t g = static_cast<std::uint16_t>(CompressPaletteComponent(c.g, true));
        const std::uint16_t b = static_cast<std::uint16_t>(CompressPaletteComponent(c.b, true));
        return static_cast<std::uint16_t>((r << 8) | (g << 4) | b);
    }

    bool LooksLikeAmigaWordPalette(const std::vector<std::uint8_t>& bytes)
    {
        if (bytes.size() < 2 || (bytes.size() % 2) != 0)
            return false;

        const size_t words = bytes.size() / 2U;
        size_t goodWords = 0;
        const size_t sampleWords = std::min<size_t>(words, 256U);
        for (size_t i = 0; i < sampleWords; ++i)
        {
            const std::uint16_t word = ReadBe16(bytes, i * 2U);
            if ((word & 0xf000U) == 0)
                ++goodWords;
        }

        return sampleWords > 0 && (goodWords * 10U >= sampleWords * 9U);
    }

    RgbColor DecodeAmigaPaletteWordLe(const std::vector<std::uint8_t>& bytes, size_t offset)
    {
        const std::uint16_t word = static_cast<std::uint16_t>(bytes[offset + 0] | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
        return DecodeAmigaPaletteWord(word);
    }

    // Palette decode modes used by the picture manager:
    // 0 = ZGloom/Gloom picture palette split-nibble format:
    //     byte0 low nibble = R high, byte1 high/low = G/B high,
    //     byte2 low nibble = R low, byte3 high/low = G/B low.
    // 1 = 4-byte 00 RR GG BB
    // 2 = 4-byte RR GG BB 00
    // 3 = 4-byte 00 BB GG RR
    // 4 = 4-byte BB GG RR 00
    // 5 = Amiga 0RGB words, big-endian
    // 6 = Amiga 0RGB words, little-endian
    // 7 = Auto/legacy detector
    std::wstring PaletteModeDisplayName(int mode)
    {
        switch (mode)
        {
        case 0: return L"ZGloom split-nibble 4-byte";
        case 1: return L"4-byte 00 RR GG BB";
        case 2: return L"4-byte RR GG BB 00";
        case 3: return L"4-byte 00 BB GG RR";
        case 4: return L"4-byte BB GG RR 00";
        case 5: return L"Amiga 0RGB words BE";
        case 6: return L"Amiga 0RGB words LE";
        default: return L"Auto / legacy guess";
        }
    }

    std::wstring PictureIndexModeDisplayName(int mode)
    {
        switch (mode)
        {
        case 1: return L"Reverse bitplane order";
        case 2: return L"Invert palette index";
        case 3: return L"Reverse + invert index";
        default: return L"Normal indexes";
        }
    }

    bool IsGloomLogoAsset(const fs::path& imageFile)
    {
        const std::wstring stem = Lower(imageFile.stem().wstring());
        const std::wstring name = Lower(imageFile.filename().wstring());
        return stem == L"gloom" || name == L"gloom" || stem == L"gloombrush" || name == L"gloombrush";
    }

    bool UsesGloomDeluxeTitlePaletteForImport(const fs::path& imageFile)
    {
        return g_profile.type == GameType::GloomDeluxe && IsGloomLogoAsset(imageFile);
    }

    int DefaultPictureIndexMode(const PictureInfo& info)
    {
        (void)info;
        // Imported/editor-normalised pictures should preview correctly with
        // normal palette indexes. Reverse bitplane order remains available
        // only as a manual inspection mode for original/legacy assets.
        return 0;
    }

    int PictureIndexDepth(const PictureInfo& info, const PaletteData& palette)
    {
        if (info.bitDepth > 0)
            return std::clamp(info.bitDepth, 1, 8);
        return DepthForColorCount(static_cast<int>(palette.colors.size()));
    }

    std::uint8_t ReverseLowerBits(std::uint8_t value, int depth)
    {
        const int safeDepth = std::clamp(depth, 1, 8);
        std::uint8_t out = 0;
        for (int bit = 0; bit < safeDepth; ++bit)
        {
            if (value & static_cast<std::uint8_t>(1U << bit))
                out |= static_cast<std::uint8_t>(1U << (safeDepth - 1 - bit));
        }
        return out;
    }

    std::uint8_t ApplyPictureIndexModeToValue(std::uint8_t value, int depth, int indexMode)
    {
        const int safeDepth = std::clamp(depth, 1, 8);
        const std::uint8_t mask = static_cast<std::uint8_t>((1U << safeDepth) - 1U);
        std::uint8_t out = static_cast<std::uint8_t>(value & mask);
        if (indexMode == 1 || indexMode == 3)
            out = ReverseLowerBits(out, safeDepth);
        if (indexMode == 2 || indexMode == 3)
            out = static_cast<std::uint8_t>((~out) & mask);
        return out;
    }

    void ApplyPictureIndexMode(std::vector<std::uint8_t>& pixels, int depth, int indexMode)
    {
        if (indexMode <= 0)
            return;
        for (std::uint8_t& px : pixels)
            px = ApplyPictureIndexModeToValue(px, depth, indexMode);
    }

    std::uint8_t UndoPictureIndexModeValue(std::uint8_t value, int depth, int indexMode)
    {
        // The supported transformations are self-inverse when applied in the
        // reverse logical order. This keeps PNG import compatible with the
        // preview/export path.
        const int safeDepth = std::clamp(depth, 1, 8);
        const std::uint8_t mask = static_cast<std::uint8_t>((1U << safeDepth) - 1U);
        std::uint8_t out = static_cast<std::uint8_t>(value & mask);
        if (indexMode == 2 || indexMode == 3)
            out = static_cast<std::uint8_t>((~out) & mask);
        if (indexMode == 1 || indexMode == 3)
            out = ReverseLowerBits(out, safeDepth);
        return out;
    }

    void UndoPictureIndexMode(std::vector<std::uint8_t>& pixels, int depth, int indexMode)
    {
        if (indexMode <= 0)
            return;
        for (std::uint8_t& px : pixels)
            px = UndoPictureIndexModeValue(px, depth, indexMode);
    }

    bool ReadPaletteGloomSplitNibble4(const std::vector<std::uint8_t>& bytes, PaletteData& palette)
    {
        if (bytes.empty() || (bytes.size() % 4U) != 0)
            return false;

        palette.entryBytes = 4;
        palette.nibbleSource = false;
        palette.rgbOffset = 0;
        palette.channelOrder = 0;
        palette.gloomSplitNibbleLayout = true;

        const size_t entries = std::min<size_t>(bytes.size() / 4U, 256U);
        palette.colors.clear();
        palette.colors.reserve(entries);
        for (size_t i = 0; i < entries; ++i)
        {
            const size_t o = i * 4U;
            RgbColor out{};
            out.r = static_cast<std::uint8_t>(((bytes[o + 0] & 0x0fU) << 4) | (bytes[o + 2] & 0x0fU));
            out.g = static_cast<std::uint8_t>((bytes[o + 1] & 0xf0U) | ((bytes[o + 3] >> 4) & 0x0fU));
            out.b = static_cast<std::uint8_t>(((bytes[o + 1] & 0x0fU) << 4) | (bytes[o + 3] & 0x0fU));
            palette.colors.push_back(out);
        }
        return !palette.colors.empty();
    }

    bool ReadPalette4ByteLayout(const std::vector<std::uint8_t>& bytes, PaletteData& palette, int rgbOffset, int channelOrder)
    {
        if (bytes.empty() || (bytes.size() % 4U) != 0)
            return false;

        palette.entryBytes = 4;
        palette.rgbOffset = (rgbOffset == 0) ? 0 : 1;
        palette.channelOrder = (channelOrder == 1) ? 1 : 0;
        palette.gloomSplitNibbleLayout = false;
        const size_t entries = std::min<size_t>(bytes.size() / 4U, 256U);

        std::uint8_t maxRgb = 0;
        for (size_t i = 0; i < entries; ++i)
        {
            const size_t o = i * 4U + static_cast<size_t>(palette.rgbOffset);
            maxRgb = std::max(maxRgb, bytes[o + 0]);
            maxRgb = std::max(maxRgb, bytes[o + 1]);
            maxRgb = std::max(maxRgb, bytes[o + 2]);
        }
        palette.nibbleSource = maxRgb <= 15;

        palette.colors.clear();
        palette.colors.reserve(entries);
        for (size_t i = 0; i < entries; ++i)
        {
            const size_t o = i * 4U + static_cast<size_t>(palette.rgbOffset);
            const std::uint8_t a = bytes[o + 0];
            const std::uint8_t b = bytes[o + 1];
            const std::uint8_t c = bytes[o + 2];
            RgbColor out{};
            if (palette.channelOrder == 0)
            {
                out.r = ExpandPaletteComponent(a, palette.nibbleSource);
                out.g = ExpandPaletteComponent(b, palette.nibbleSource);
                out.b = ExpandPaletteComponent(c, palette.nibbleSource);
            }
            else
            {
                out.r = ExpandPaletteComponent(c, palette.nibbleSource);
                out.g = ExpandPaletteComponent(b, palette.nibbleSource);
                out.b = ExpandPaletteComponent(a, palette.nibbleSource);
            }
            palette.colors.push_back(out);
        }
        return !palette.colors.empty();
    }

    bool ReadPaletteAmigaWords(const std::vector<std::uint8_t>& bytes, PaletteData& palette, bool littleEndian)
    {
        if (bytes.empty() || (bytes.size() % 2U) != 0)
            return false;

        palette.entryBytes = 2;
        palette.nibbleSource = true;
        palette.rgbOffset = 0;
        palette.channelOrder = littleEndian ? 1 : 0;
        palette.gloomSplitNibbleLayout = false;
        const size_t entries = std::min<size_t>(bytes.size() / 2U, 256U);
        palette.colors.clear();
        palette.colors.reserve(entries);
        for (size_t i = 0; i < entries; ++i)
        {
            if (littleEndian)
                palette.colors.push_back(DecodeAmigaPaletteWordLe(bytes, i * 2U));
            else
                palette.colors.push_back(DecodeAmigaPaletteWord(ReadBe16(bytes, i * 2U)));
        }
        return !palette.colors.empty();
    }

    int DetectRgbOffset4(const std::vector<std::uint8_t>& bytes)
    {
        if (bytes.size() < 4 || (bytes.size() % 4) != 0)
            return 1;

        const size_t entries = std::min<size_t>(bytes.size() / 4U, 256U);
        size_t firstZeroish = 0;
        size_t lastZeroish = 0;
        for (size_t i = 0; i < entries; ++i)
        {
            const size_t o = i * 4U;
            if (bytes[o + 0] <= 1)
                ++firstZeroish;
            if (bytes[o + 3] <= 1)
                ++lastZeroish;
        }

        if (firstZeroish >= lastZeroish)
            return 1; // default to 00 RR GG BB on ties; this avoids the green shifted look.
        return 0;
    }

    bool ReadPalette(const fs::path& paletteFile, PaletteData& palette, std::wstring& error, int decodeMode)
    {
        std::vector<std::uint8_t> bytes;
        if (!LoadMaybeCrM2(paletteFile, bytes, error))
        {
            if (error.empty())
                error = L"Could not read palette.";
            error = L"Could not read/decrunch palette:\n" + paletteFile.wstring() + L"\n\n" + error;
            return false;
        }

        palette.colors.clear();
        palette.entryBytes = 4;
        palette.nibbleSource = false;
        palette.rgbOffset = 0;
        palette.channelOrder = 0;
        palette.decodeMode = decodeMode;
        palette.gloomSplitNibbleLayout = false;

        const int mode = std::clamp(decodeMode, 0, 7);
        if (mode == 0)
        {
            if (!ReadPaletteGloomSplitNibble4(bytes, palette))
            {
                error = L"Palette is not a ZGloom split-nibble 4-byte palette: " + std::to_wstring(bytes.size()) + L" bytes.";
                return false;
            }
            palette.decodeMode = mode;
            return true;
        }
        if (mode == 1)
        {
            if (!ReadPalette4ByteLayout(bytes, palette, 1, 0))
            {
                error = L"Palette is not a 4-byte 00 RR GG BB palette: " + std::to_wstring(bytes.size()) + L" bytes.";
                return false;
            }
            palette.decodeMode = mode;
            return true;
        }
        if (mode == 2)
        {
            if (!ReadPalette4ByteLayout(bytes, palette, 0, 0))
            {
                error = L"Palette is not a 4-byte RR GG BB 00 palette: " + std::to_wstring(bytes.size()) + L" bytes.";
                return false;
            }
            palette.decodeMode = mode;
            return true;
        }
        if (mode == 3)
        {
            if (!ReadPalette4ByteLayout(bytes, palette, 1, 1))
            {
                error = L"Palette is not a 4-byte 00 BB GG RR palette: " + std::to_wstring(bytes.size()) + L" bytes.";
                return false;
            }
            palette.decodeMode = mode;
            return true;
        }
        if (mode == 4)
        {
            if (!ReadPalette4ByteLayout(bytes, palette, 0, 1))
            {
                error = L"Palette is not a 4-byte BB GG RR 00 palette: " + std::to_wstring(bytes.size()) + L" bytes.";
                return false;
            }
            palette.decodeMode = mode;
            return true;
        }
        if (mode == 5 || mode == 6)
        {
            if (!ReadPaletteAmigaWords(bytes, palette, mode == 6))
            {
                error = L"Palette is not a 2-byte Amiga word palette: " + std::to_wstring(bytes.size()) + L" bytes.";
                return false;
            }
            palette.decodeMode = mode;
            return true;
        }

        // Auto/legacy fallback. ZGloom itself decodes picture .pal files as
        // split nibbles across four bytes, so try that first.
        if (!bytes.empty() && bytes.size() % 4 == 0)
        {
            if (ReadPaletteGloomSplitNibble4(bytes, palette))
            {
                palette.decodeMode = 7;
                return true;
            }

            const int offset = DetectRgbOffset4(bytes);
            if (ReadPalette4ByteLayout(bytes, palette, offset, 0))
            {
                palette.decodeMode = 7;
                return true;
            }
        }

        if (LooksLikeAmigaWordPalette(bytes) && ReadPaletteAmigaWords(bytes, palette, false))
        {
            palette.decodeMode = 7;
            return true;
        }

        if (!bytes.empty() && bytes.size() % 3 == 0)
        {
            std::uint8_t maxRgb = 0;
            for (size_t i = 0; i + 3U <= bytes.size(); i += 3U)
            {
                maxRgb = std::max(maxRgb, bytes[i + 0]);
                maxRgb = std::max(maxRgb, bytes[i + 1]);
                maxRgb = std::max(maxRgb, bytes[i + 2]);
            }

            palette.entryBytes = 3;
            palette.nibbleSource = maxRgb <= 15;
            palette.rgbOffset = 0;
            palette.channelOrder = 0;
            palette.gloomSplitNibbleLayout = false;
            const size_t maxEntries = std::min<size_t>(bytes.size() / 3U, 256U);
            palette.colors.clear();
            palette.colors.reserve(maxEntries);
            for (size_t n = 0, i = 0; n < maxEntries && i + 3U <= bytes.size(); ++n, i += 3U)
            {
                RgbColor c;
                c.r = ExpandPaletteComponent(bytes[i + 0], palette.nibbleSource);
                c.g = ExpandPaletteComponent(bytes[i + 1], palette.nibbleSource);
                c.b = ExpandPaletteComponent(bytes[i + 2], palette.nibbleSource);
                palette.colors.push_back(c);
            }
            palette.decodeMode = 7;
            return !palette.colors.empty();
        }

        error = L"Unsupported palette size: " + std::to_wstring(bytes.size()) + L" bytes.";
        return false;
    }

    void WritePalette(const fs::path& paletteFile, const std::vector<RgbColor>& colors, int entries, int entryBytes, bool nibble, int rgbOffset = 1, int channelOrder = 0, bool gloomSplitNibbleLayout = false)
    {
        const int safeEntries = std::clamp(entries, 2, 256);
        const int safeEntryBytes = (entryBytes == 2 || entryBytes == 3 || entryBytes == 4) ? entryBytes : 4;
        const int safeRgbOffset = (safeEntryBytes == 4 && rgbOffset == 0) ? 0 : 1;
        const int safeChannelOrder = (channelOrder == 1) ? 1 : 0;
        std::vector<std::uint8_t> bytes;

        if (gloomSplitNibbleLayout && safeEntryBytes == 4)
        {
            bytes.reserve(static_cast<size_t>(safeEntries) * 4U);
            for (int i = 0; i < safeEntries; ++i)
            {
                RgbColor c{};
                if (i < static_cast<int>(colors.size()))
                    c = colors[static_cast<size_t>(i)];

                const std::uint8_t rHi = static_cast<std::uint8_t>((c.r >> 4) & 0x0fU);
                const std::uint8_t gHi = static_cast<std::uint8_t>((c.g >> 4) & 0x0fU);
                const std::uint8_t bHi = static_cast<std::uint8_t>((c.b >> 4) & 0x0fU);
                const std::uint8_t rLo = static_cast<std::uint8_t>(c.r & 0x0fU);
                const std::uint8_t gLo = static_cast<std::uint8_t>(c.g & 0x0fU);
                const std::uint8_t bLo = static_cast<std::uint8_t>(c.b & 0x0fU);

                bytes.push_back(rHi);
                bytes.push_back(static_cast<std::uint8_t>((gHi << 4) | bHi));
                bytes.push_back(rLo);
                bytes.push_back(static_cast<std::uint8_t>((gLo << 4) | bLo));
            }
        }
        else if (safeEntryBytes == 2)
        {
            bytes.reserve(static_cast<size_t>(safeEntries) * 2U);
            for (int i = 0; i < safeEntries; ++i)
            {
                RgbColor c{};
                if (i < static_cast<int>(colors.size()))
                    c = colors[static_cast<size_t>(i)];
                WriteBe16(bytes, EncodeAmigaPaletteWord(c));
            }
        }
        else if (safeEntryBytes == 4)
        {
            bytes.reserve(static_cast<size_t>(safeEntries) * 4U);
            for (int i = 0; i < safeEntries; ++i)
            {
                RgbColor c{};
                if (i < static_cast<int>(colors.size()))
                    c = colors[static_cast<size_t>(i)];

                const std::uint8_t first = CompressPaletteComponent(safeChannelOrder == 0 ? c.r : c.b, nibble);
                const std::uint8_t middle = CompressPaletteComponent(c.g, nibble);
                const std::uint8_t last = CompressPaletteComponent(safeChannelOrder == 0 ? c.b : c.r, nibble);
                if (safeRgbOffset == 1)
                {
                    bytes.push_back(0);
                    bytes.push_back(first);
                    bytes.push_back(middle);
                    bytes.push_back(last);
                }
                else
                {
                    bytes.push_back(first);
                    bytes.push_back(middle);
                    bytes.push_back(last);
                    bytes.push_back(0);
                }
            }
        }
        else
        {
            bytes.reserve(static_cast<size_t>(safeEntries) * 3U);
            for (int i = 0; i < safeEntries; ++i)
            {
                RgbColor c{};
                if (i < static_cast<int>(colors.size()))
                    c = colors[static_cast<size_t>(i)];
                bytes.push_back(CompressPaletteComponent(c.r, nibble));
                bytes.push_back(CompressPaletteComponent(c.g, nibble));
                bytes.push_back(CompressPaletteComponent(c.b, nibble));
            }
        }

        std::ofstream out(paletteFile, std::ios::binary | std::ios::trunc);
        if (out)
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    PaletteData BuildFallbackPalette(int entries)
    {
        PaletteData palette;
        const int safeEntries = std::clamp(entries, 2, 256);
        palette.colors.resize(static_cast<size_t>(safeEntries));
        for (int i = 0; i < safeEntries; ++i)
        {
            const std::uint8_t v = static_cast<std::uint8_t>((safeEntries <= 1) ? 0 : (i * 255) / (safeEntries - 1));
            palette.colors[static_cast<size_t>(i)] = { v, v, v };
        }
        palette.entryBytes = 4;
        palette.nibbleSource = false;
        palette.rgbOffset = 0;
        palette.channelOrder = 0;
        palette.decodeMode = 0;
        palette.gloomSplitNibbleLayout = true;
        return palette;
    }

    bool LoadPicturePixels(const PictureInfo& info, std::vector<std::uint8_t>& pixels, PaletteData& palette, int& width, int& height, std::wstring& error, int paletteMode = 0)
    {
        if (!info.exists)
        {
            error = L"Picture file not found.";
            return false;
        }

        std::vector<std::uint8_t> raw;
        if (!LoadMaybeCrM2(info.imageFile, raw, error))
            return false;

        int depth = 0;
        std::wstring iffError;
        if (DecodeTrimmedIffPicture(raw, pixels, width, height, depth, iffError))
        {
            if (info.hasPalette)
            {
                if (!ReadPalette(info.paletteFile, palette, error, paletteMode))
                    return false;
            }
            else
            {
                palette = BuildFallbackPalette(1 << std::clamp(depth, 1, 8));
            }
            error.clear();
            return true;
        }

        std::wstring fullIffError;
        if (DecodeFullIffIlbmPicture(raw, pixels, width, height, depth, fullIffError))
        {
            if (info.hasPalette)
            {
                if (!ReadPalette(info.paletteFile, palette, error, paletteMode))
                    return false;
            }
            else
            {
                palette = BuildFallbackPalette(1 << std::clamp(depth, 1, 8));
            }
            error.clear();
            return true;
        }

        std::wstring rawPlanarError;
        if (DecodeRawPlanarPicture(raw, pixels, width, height, depth, rawPlanarError))
        {
            if (info.hasPalette)
            {
                if (!ReadPalette(info.paletteFile, palette, error, paletteMode))
                    return false;
            }
            else
            {
                palette = BuildFallbackPalette(1 << std::clamp(depth, 1, 8));
            }
            error.clear();
            return true;
        }

        width = info.guessedWidth;
        height = info.guessedHeight;
        if (width <= 0 || height <= 0)
        {
            error = L"Could not determine picture size. Trimmed IFF decode failed: " + iffError;
            return false;
        }

        const size_t expected = static_cast<size_t>(width) * static_cast<size_t>(height);
        if (raw.size() < expected)
        {
            error = L"Raw picture data is too small. Trimmed IFF decode failed: " + iffError;
            return false;
        }

        if (info.hasPalette)
        {
            if (!ReadPalette(info.paletteFile, palette, error, paletteMode))
                return false;
        }
        else
        {
            palette = BuildFallbackPalette(256);
        }

        pixels.assign(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(expected));
        error.clear();
        return true;
    }

    HBITMAP CreatePicturePreviewBitmap(const PictureInfo& info, int& outW, int& outH, int paletteMode, int indexMode)
    {
        std::vector<std::uint8_t> pixels;
        PaletteData palette;
        std::wstring error;
        int width = 0;
        int height = 0;
        if (!LoadPicturePixels(info, pixels, palette, width, height, error, paletteMode))
            return nullptr;
        ApplyPictureIndexMode(pixels, PictureIndexDepth(info, palette), indexMode);

        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = width;
        bi.bmiHeader.biHeight = -height;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HDC hdc = GetDC(nullptr);
        HBITMAP bitmap = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, hdc);
        if (!bitmap || !bits)
            return bitmap;

        auto* dst = static_cast<std::uint8_t*>(bits);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const size_t i = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                RgbColor c{};
                const std::uint8_t index = pixels[i];
                if (index < palette.colors.size())
                    c = palette.colors[index];
                const size_t o = i * 4U;
                dst[o + 0] = c.b;
                dst[o + 1] = c.g;
                dst[o + 2] = c.r;
                dst[o + 3] = 255;
            }
        }

        outW = width;
        outH = height;
        return bitmap;
    }

    bool ExportPictureToPng(HWND owner, const PictureInfo& info, fs::path& outPath, std::wstring& error, int paletteMode, int indexMode)
    {
        std::vector<std::uint8_t> pixels;
        PaletteData palette;
        int width = 0;
        int height = 0;
        if (!LoadPicturePixels(info, pixels, palette, width, height, error, paletteMode))
            return false;
        ApplyPictureIndexMode(pixels, PictureIndexDepth(info, palette), indexMode);

        ScopedGdiplus gdiplus;
        if (!gdiplus.Ok())
        {
            error = L"Could not initialise GDI+ for PNG export.";
            return false;
        }

        Gdiplus::Bitmap bitmap(width, height, PixelFormat32bppARGB);
        Gdiplus::Rect rect(0, 0, width, height);
        Gdiplus::BitmapData data{};
        if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &data) != Gdiplus::Ok)
        {
            error = L"Could not write PNG bitmap data.";
            return false;
        }

        auto* dst = static_cast<std::uint8_t*>(data.Scan0);
        for (int y = 0; y < height; ++y)
        {
            auto* row = dst + static_cast<size_t>(y) * static_cast<size_t>(data.Stride);
            for (int x = 0; x < width; ++x)
            {
                const size_t i = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                RgbColor c{};
                const std::uint8_t index = pixels[i];
                if (index < palette.colors.size())
                    c = palette.colors[index];
                const size_t o = static_cast<size_t>(x) * 4U;
                row[o + 0] = c.b;
                row[o + 1] = c.g;
                row[o + 2] = c.r;
                row[o + 3] = 255;
            }
        }
        bitmap.UnlockBits(&data);

        CLSID pngClsid{};
        if (GetEncoderClsid(L"image/png", &pngClsid) < 0)
        {
            error = L"PNG encoder not found.";
            return false;
        }

        std::wstring suggested = info.imageFile.filename().wstring() + L".png";
        if (!BrowseSavePng(owner, suggested, outPath))
            return false;

        if (bitmap.Save(outPath.wstring().c_str(), &pngClsid, nullptr) != Gdiplus::Ok)
        {
            error = L"Could not save PNG:\n" + outPath.wstring();
            return false;
        }
        return true;
    }

    struct QuantBin
    {
        std::uint32_t count = 0;
        std::uint64_t r = 0;
        std::uint64_t g = 0;
        std::uint64_t b = 0;
    };

    int NearestPaletteIndex(const RgbColor& c, const std::vector<RgbColor>& palette)
    {
        int best = 0;
        int bestDist = INT_MAX;
        for (int i = 0; i < static_cast<int>(palette.size()); ++i)
        {
            const int dr = static_cast<int>(c.r) - static_cast<int>(palette[static_cast<size_t>(i)].r);
            const int dg = static_cast<int>(c.g) - static_cast<int>(palette[static_cast<size_t>(i)].g);
            const int db = static_cast<int>(c.b) - static_cast<int>(palette[static_cast<size_t>(i)].b);
            const int dist = dr * dr + dg * dg + db * db;
            if (dist < bestDist)
            {
                bestDist = dist;
                best = i;
                if (dist == 0)
                    break;
            }
        }
        return best;
    }

    void QuantizeColors(const std::vector<RgbColor>& input, int maxColors, std::vector<RgbColor>& palette, std::vector<std::uint8_t>& indexed)
    {
        const int entries = std::clamp(maxColors, 2, 256);
        std::vector<QuantBin> bins(32768);
        for (const RgbColor& c : input)
        {
            const int key = ((c.r >> 3) << 10) | ((c.g >> 3) << 5) | (c.b >> 3);
            QuantBin& bin = bins[static_cast<size_t>(key)];
            ++bin.count;
            bin.r += c.r;
            bin.g += c.g;
            bin.b += c.b;
        }

        std::vector<int> keys;
        keys.reserve(32768);
        for (int i = 0; i < static_cast<int>(bins.size()); ++i)
        {
            if (bins[static_cast<size_t>(i)].count > 0)
                keys.push_back(i);
        }
        std::sort(keys.begin(), keys.end(), [&](int a, int b) {
            return bins[static_cast<size_t>(a)].count > bins[static_cast<size_t>(b)].count;
        });

        palette.clear();
        const int count = std::min(entries, static_cast<int>(keys.size()));
        for (int i = 0; i < count; ++i)
        {
            const QuantBin& bin = bins[static_cast<size_t>(keys[static_cast<size_t>(i)])];
            RgbColor c;
            c.r = static_cast<std::uint8_t>(bin.r / bin.count);
            c.g = static_cast<std::uint8_t>(bin.g / bin.count);
            c.b = static_cast<std::uint8_t>(bin.b / bin.count);
            palette.push_back(c);
        }
        while (palette.size() < static_cast<size_t>(entries))
            palette.push_back({ 0, 0, 0 });

        indexed.resize(input.size());
        for (size_t i = 0; i < input.size(); ++i)
            indexed[i] = static_cast<std::uint8_t>(NearestPaletteIndex(input[i], palette));
    }

    constexpr int kGloomDeluxePictureFirstImageSlot = 64;
    constexpr int kGloomDeluxeFontFixedSlots = 4;

    const std::array<RgbColor, kGloomDeluxeFontFixedSlots> kGloomDeluxeFontColors = {{
        { 0x00, 0x00, 0x30 }, // shadow, Amiga RGB4 $003
        { 0x20, 0x20, 0x00 }, // mask/key color, Amiga RGB4 $220
        { 0xC0, 0xA0, 0x20 }, // dark yellow, Amiga RGB4 $CA2
        { 0xF0, 0xE0, 0x20 }, // bright yellow, Amiga RGB4 $FE2
    }};

    bool ShouldLockGloomDeluxePictureFontSlots(int colorCount)
    {
        // Original Gloom Deluxe title/intermission pictures keep their image
        // colors in the upper half of the 7-bit palette. The lower half is
        // left free for font/UI colors and Amiga-side overlay effects.
        return g_profile.type == GameType::GloomDeluxe && colorCount > kGloomDeluxePictureFirstImageSlot;
    }

    void BuildGloomDeluxeLockedPicturePalette(const std::vector<RgbColor>& sourcePixels, int paletteEntries, int colorCount, const PaletteData& oldPalette, std::vector<RgbColor>& newPalette, std::vector<std::uint8_t>& indexed)
    {
        const int safePaletteEntries = std::clamp(paletteEntries, 2, 256);
        const int safeColorCount = std::clamp(colorCount, 2, safePaletteEntries);
        const int firstImageIndex = kGloomDeluxePictureFirstImageSlot;
        const int imageColorCount = std::max(2, safeColorCount - firstImageIndex);

        newPalette.assign(static_cast<size_t>(safePaletteEntries), { 0, 0, 0 });
        for (int i = 0; i < safePaletteEntries && i < static_cast<int>(oldPalette.colors.size()); ++i)
            newPalette[static_cast<size_t>(i)] = oldPalette.colors[static_cast<size_t>(i)];

        for (int i = 0; i < kGloomDeluxeFontFixedSlots && i < safePaletteEntries; ++i)
            newPalette[static_cast<size_t>(i)] = kGloomDeluxeFontColors[static_cast<size_t>(i)];

        std::vector<RgbColor> imagePalette;
        std::vector<std::uint8_t> localIndexed;
        QuantizeColors(sourcePixels, imageColorCount, imagePalette, localIndexed);

        const int writableImageColors = std::min(imageColorCount, safePaletteEntries - firstImageIndex);
        for (int i = 0; i < writableImageColors; ++i)
            newPalette[static_cast<size_t>(firstImageIndex + i)] = imagePalette[static_cast<size_t>(i)];

        indexed.resize(localIndexed.size());
        for (size_t i = 0; i < localIndexed.size(); ++i)
        {
            int value = firstImageIndex + static_cast<int>(localIndexed[i]);
            if (value >= safeColorCount)
                value = safeColorCount - 1;
            indexed[i] = static_cast<std::uint8_t>(std::clamp(value, 0, 255));
        }
    }

    int NearestPaletteIndexInRange(const RgbColor& c, const std::vector<RgbColor>& palette, int firstIndex, int count)
    {
        const int safeCount = std::clamp(count, 1, static_cast<int>(palette.size()));
        const int safeFirst = std::clamp(firstIndex, 0, safeCount - 1);
        int best = safeFirst;
        int bestDist = INT_MAX;
        for (int i = safeFirst; i < safeCount; ++i)
        {
            const RgbColor& p = palette[static_cast<size_t>(i)];
            const int dr = static_cast<int>(c.r) - static_cast<int>(p.r);
            const int dg = static_cast<int>(c.g) - static_cast<int>(p.g);
            const int db = static_cast<int>(c.b) - static_cast<int>(p.b);
            const int dist = dr * dr + dg * dg + db * db;
            if (dist < bestDist)
            {
                bestDist = dist;
                best = i;
                if (dist == 0)
                    break;
            }
        }
        return best;
    }

    void MapColorsToExistingPaletteRange(const std::vector<RgbColor>& sourcePixels, const std::vector<RgbColor>& palette, int colorCount, int firstIndex, std::vector<std::uint8_t>& indexed)
    {
        const int safeColorCount = std::clamp(colorCount, 1, std::max(1, static_cast<int>(palette.size())));
        const int safeFirstIndex = (firstIndex < safeColorCount) ? std::max(0, firstIndex) : 0;
        indexed.resize(sourcePixels.size());
        for (size_t i = 0; i < sourcePixels.size(); ++i)
            indexed[i] = static_cast<std::uint8_t>(NearestPaletteIndexInRange(sourcePixels[i], palette, safeFirstIndex, safeColorCount));
    }

    bool ImportPngToPicture(HWND owner, const PictureInfo& info, fs::path& importedPath, fs::path& backupImage, fs::path& backupPalette, std::wstring& error, int paletteMode, int indexMode)
    {
        if (!info.exists)
        {
            error = L"Picture file not found.";
            return false;
        }
        const bool useTitlePaletteForLogo = UsesGloomDeluxeTitlePaletteForImport(info.imageFile);
        fs::path paletteFileForImport = info.paletteFile;
        if (useTitlePaletteForLogo)
        {
            paletteFileForImport = FindTitlePaletteForPicture(info.imageFile);
            if (paletteFileForImport.empty())
            {
                error = L"Gloom Deluxe logo import needs title.pal in the same pics folder. Import/save the title picture first, then import gloom.";
                return false;
            }
        }
        else if (!info.hasPalette)
        {
            error = L"Palette file not found. Import needs the existing palette file to infer color count and format.";
            return false;
        }
        if (info.guessedWidth <= 0 || info.guessedHeight <= 0)
        {
            error = L"Could not determine picture dimensions.";
            return false;
        }

        PaletteData oldPalette;
        if (!ReadPalette(paletteFileForImport, oldPalette, error, paletteMode))
            return false;

        if (!BrowseOpenPng(owner, importedPath))
            return false;

        ScopedGdiplus gdiplus;
        if (!gdiplus.Ok())
        {
            error = L"Could not initialise GDI+ for PNG import.";
            return false;
        }

        Gdiplus::Bitmap source(importedPath.wstring().c_str());
        if (source.GetLastStatus() != Gdiplus::Ok)
        {
            error = L"Could not open PNG:\n" + importedPath.wstring();
            return false;
        }

        const int width = info.guessedWidth;
        const int height = info.guessedHeight;
        std::vector<RgbColor> pixels(static_cast<size_t>(width) * static_cast<size_t>(height));
        const UINT srcW = source.GetWidth();
        const UINT srcH = source.GetHeight();
        for (int y = 0; y < height; ++y)
        {
            const UINT sy = std::min<UINT>(srcH - 1, static_cast<UINT>((static_cast<std::uint64_t>(y) * srcH) / static_cast<UINT>(height)));
            for (int x = 0; x < width; ++x)
            {
                const UINT sx = std::min<UINT>(srcW - 1, static_cast<UINT>((static_cast<std::uint64_t>(x) * srcW) / static_cast<UINT>(width)));
                Gdiplus::Color c;
                source.GetPixel(sx, sy, &c);
                const std::uint8_t a = c.GetA();
                RgbColor out;
                out.r = static_cast<std::uint8_t>((static_cast<int>(c.GetR()) * a) / 255);
                out.g = static_cast<std::uint8_t>((static_cast<int>(c.GetG()) * a) / 255);
                out.b = static_cast<std::uint8_t>((static_cast<int>(c.GetB()) * a) / 255);
                pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = out;
            }
        }

        const int paletteEntries = std::clamp(useTitlePaletteForLogo ? static_cast<int>(oldPalette.colors.size()) : (info.paletteEntries > 0 ? info.paletteEntries : static_cast<int>(oldPalette.colors.size())), 2, 256);
        const int targetDepth = (info.bitDepth > 0) ? std::clamp(info.bitDepth, 1, 8) : DepthForColorCount(paletteEntries);
        const int colorCount = std::min(paletteEntries, 1 << targetDepth);
        const int effectiveIndexMode = indexMode;
        std::vector<RgbColor> newPalette;
        std::vector<std::uint8_t> indexed;
        if (useTitlePaletteForLogo)
        {
            newPalette = oldPalette.colors;
            if (newPalette.size() < static_cast<size_t>(paletteEntries))
                newPalette.resize(static_cast<size_t>(paletteEntries), { 0, 0, 0 });
            MapColorsToExistingPaletteRange(pixels, newPalette, colorCount, kGloomDeluxePictureFirstImageSlot, indexed);
        }
        else if (ShouldLockGloomDeluxePictureFontSlots(colorCount))
        {
            BuildGloomDeluxeLockedPicturePalette(pixels, paletteEntries, colorCount, oldPalette, newPalette, indexed);
        }
        else
        {
            QuantizeColors(pixels, colorCount, newPalette, indexed);
        }

        UndoPictureIndexMode(indexed, targetDepth, effectiveIndexMode);

        if (!BackupFile(info.imageFile, backupImage, error))
            return false;
        if (!useTitlePaletteForLogo)
        {
            if (!BackupFile(info.paletteFile, backupPalette, error))
                return false;
        }
        else
        {
            backupPalette.clear();
        }

        std::vector<std::uint8_t> outputBytes;
        if (info.isRawPlanar)
            outputBytes = EncodeRawPlanarPicture(indexed, width, height, targetDepth);
        else if (info.isTrimmedIff || info.bitDepth > 0)
            outputBytes = EncodeTrimmedIffPicture(indexed, width, height, targetDepth);
        else
            outputBytes = indexed;

        std::ofstream rawOut(info.imageFile, std::ios::binary | std::ios::trunc);
        if (!rawOut)
        {
            error = L"Could not write raw picture:\n" + info.imageFile.wstring();
            return false;
        }
        rawOut.write(reinterpret_cast<const char*>(outputBytes.data()), static_cast<std::streamsize>(outputBytes.size()));
        rawOut.close();

        if (!useTitlePaletteForLogo)
            WritePalette(info.paletteFile, newPalette, paletteEntries, oldPalette.entryBytes, oldPalette.nibbleSource, oldPalette.rgbOffset, oldPalette.channelOrder, oldPalette.gloomSplitNibbleLayout);
        return true;
    }


    std::uint16_t ReadBE16(const std::uint8_t* p)
    {
        return (static_cast<std::uint16_t>(p[0]) << 8) | static_cast<std::uint16_t>(p[1]);
    }

    std::uint32_t ReadBE32(const std::uint8_t* p)
    {
        return (static_cast<std::uint32_t>(p[0]) << 24) |
            (static_cast<std::uint32_t>(p[1]) << 16) |
            (static_cast<std::uint32_t>(p[2]) << 8) |
            static_cast<std::uint32_t>(p[3]);
    }

    RgbColor DecodeAmiga12(std::uint16_t value)
    {
        RgbColor c;
        c.r = static_cast<std::uint8_t>((value >> 8) & 0x0F);
        c.g = static_cast<std::uint8_t>((value >> 4) & 0x0F);
        c.b = static_cast<std::uint8_t>(value & 0x0F);
        c.r = static_cast<std::uint8_t>(c.r | (c.r << 4));
        c.g = static_cast<std::uint8_t>(c.g | (c.g << 4));
        c.b = static_cast<std::uint8_t>(c.b | (c.b << 4));
        return c;
    }

    bool IsTextureBackupOrExport(const fs::path& file)
    {
        const std::wstring name = Lower(file.filename().wstring());
        return EndsWith(name, L".bak") || EndsWith(name, L".png") || EndsWith(name, L".txt");
    }

    TextureInfo AnalyzeTextureAsset(const fs::path& file)
    {
        TextureInfo info;
        info.file = file;
        info.exists = FileExists(file);
        info.storedBytes = FileSizeOrZero(file);
        info.isCrM2 = IsCrM2File(file);

        std::vector<std::uint8_t> raw;
        std::wstring error;
        if (!LoadMaybeCrM2(file, raw, error))
        {
            info.error = error.empty() ? L"Could not read texture." : error;
            return info;
        }
        info.rawBytes = raw.size();
        info.decrunched = info.isCrM2;

        const std::wstring lowerName = Lower(file.filename().wstring());
        const bool nameLooksFlat = StartsWith(lowerName, L"floor") || StartsWith(lowerName, L"roof");
        if (nameLooksFlat && raw.size() >= 128U * 128U + 2U)
        {
            info.kind = TextureAssetKind::FlatTexture;
            info.width = 128;
            info.height = 128;
            info.colors = static_cast<int>((raw.size() - 128U * 128U) / 2U);
            return info;
        }

        if (raw.size() >= 8)
        {
            const std::uint32_t paletteOffset = ReadBE32(raw.data());
            if (paletteOffset >= 4 && paletteOffset + 2 <= raw.size() && ((paletteOffset - 4) % 65) == 0)
            {
                const std::uint16_t entries = ReadBE16(raw.data() + paletteOffset);
                const size_t paletteEnd = static_cast<size_t>(paletteOffset) + 2U + static_cast<size_t>(entries) * 2U;
                if (paletteEnd <= raw.size())
                {
                    info.kind = TextureAssetKind::WallTextureSet;
                    info.wallColumns = static_cast<int>((paletteOffset - 4) / 65);
                    info.wallTextureCount = std::max(1, (info.wallColumns + 63) / 64);
                    info.width = 64;
                    info.height = info.wallTextureCount * 64;
                    info.colors = static_cast<int>(entries) + 1;
                    return info;
                }
            }
        }

        if (!nameLooksFlat)
            info.error = L"Unsupported texture format.";
        return info;
    }

    std::vector<TextureInfo> ListTextureAssets(const fs::path& textureDir)
    {
        std::vector<TextureInfo> result;
        for (const fs::path& file : ListRegularFiles(textureDir))
        {
            if (IsTextureBackupOrExport(file))
                continue;
            TextureInfo info = AnalyzeTextureAsset(file);
            if (info.kind != TextureAssetKind::Unknown)
                result.push_back(std::move(info));
        }
        std::sort(result.begin(), result.end(), [](const TextureInfo& a, const TextureInfo& b) {
            return Lower(a.file.filename().wstring()) < Lower(b.file.filename().wstring());
        });
        return result;
    }

    bool DecodeFlatTexture(const std::vector<std::uint8_t>& raw, TextureImage& image, std::wstring& error)
    {
        if (raw.size() < 128U * 128U + 2U)
        {
            error = L"Flat texture data is too small.";
            return false;
        }

        std::vector<RgbColor> palette;
        const size_t paletteBytes = raw.size() - 128U * 128U;
        const int entries = static_cast<int>(paletteBytes / 2U);
        palette.reserve(static_cast<size_t>(entries));
        for (int i = 0; i < entries; ++i)
            palette.push_back(DecodeAmiga12(ReadBE16(raw.data() + 128U * 128U + static_cast<size_t>(i) * 2U)));
        if (palette.empty())
            palette.push_back({ 0, 0, 0 });

        image.width = 128;
        image.height = 128;
        image.rgba.assign(static_cast<size_t>(image.width) * static_cast<size_t>(image.height), {});
        for (int y = 0; y < 128; ++y)
        {
            for (int x = 0; x < 128; ++x)
            {
                const std::uint8_t idx = raw[static_cast<size_t>(y) + static_cast<size_t>(x) * 128U];
                RgbColor c{};
                if (idx < palette.size())
                    c = palette[idx];
                image.rgba[static_cast<size_t>(y) * 128U + static_cast<size_t>(x)] = c;
            }
        }
        error.clear();
        return true;
    }

    bool DecodeWallTextureSet(const std::vector<std::uint8_t>& raw, TextureImage& image, std::wstring& error)
    {
        if (raw.size() < 8)
        {
            error = L"Wall texture data is too small.";
            return false;
        }

        const std::uint32_t paletteOffset = ReadBE32(raw.data());
        if (paletteOffset < 4 || paletteOffset + 2 > raw.size() || ((paletteOffset - 4) % 65) != 0)
        {
            error = L"Invalid wall texture palette offset.";
            return false;
        }

        const int columns = static_cast<int>((paletteOffset - 4) / 65);
        const std::uint16_t entries = ReadBE16(raw.data() + paletteOffset);
        const size_t paletteEnd = static_cast<size_t>(paletteOffset) + 2U + static_cast<size_t>(entries) * 2U;
        if (paletteEnd > raw.size())
        {
            error = L"Wall texture palette exceeds file size.";
            return false;
        }

        std::vector<RgbColor> palette;
        palette.reserve(static_cast<size_t>(entries) + 1U);
        palette.push_back({ 0, 0, 0 });
        for (int i = 0; i < static_cast<int>(entries); ++i)
            palette.push_back(DecodeAmiga12(ReadBE16(raw.data() + paletteOffset + 2U + static_cast<size_t>(i) * 2U)));

        const int textureCount = std::max(1, (columns + 63) / 64);
        image.width = 64;
        image.height = textureCount * 64;
        image.rgba.assign(static_cast<size_t>(image.width) * static_cast<size_t>(image.height), {});

        for (int t = 0; t < textureCount; ++t)
        {
            for (int x = 0; x < 64; ++x)
            {
                const int columnIndex = t * 64 + x;
                if (columnIndex >= columns)
                    continue;
                const size_t base = 4U + static_cast<size_t>(columnIndex) * 65U + 1U;
                for (int y = 0; y < 64; ++y)
                {
                    const std::uint8_t idx = raw[base + static_cast<size_t>(y)];
                    RgbColor c{};
                    if (idx < palette.size())
                        c = palette[idx];
                    const int outY = t * 64 + y;
                    image.rgba[static_cast<size_t>(outY) * 64U + static_cast<size_t>(x)] = c;
                }
            }
        }

        error.clear();
        return true;
    }

    bool DecodeTextureAsset(const TextureInfo& info, TextureImage& image, std::wstring& error)
    {
        std::vector<std::uint8_t> raw;
        if (!LoadMaybeCrM2(info.file, raw, error))
            return false;

        if (info.kind == TextureAssetKind::FlatTexture)
            return DecodeFlatTexture(raw, image, error);
        if (info.kind == TextureAssetKind::WallTextureSet)
            return DecodeWallTextureSet(raw, image, error);

        error = L"Unsupported texture format.";
        return false;
    }

    TextureImage BuildTexturePreviewImage(const TextureInfo& info, const TextureImage& decoded)
    {
        if (info.kind != TextureAssetKind::WallTextureSet || decoded.width != 64 || decoded.height <= 64)
        {
            TextureImage image = decoded;
            if (info.kind == TextureAssetKind::FlatTexture && decoded.width == 128 && decoded.height == 128)
            {
                constexpr int scale = 2;
                image.width = decoded.width * scale;
                image.height = decoded.height * scale;
                image.rgba.assign(static_cast<size_t>(image.width) * static_cast<size_t>(image.height), {});
                for (int y = 0; y < image.height; ++y)
                {
                    for (int x = 0; x < image.width; ++x)
                    {
                        image.rgba[static_cast<size_t>(y) * static_cast<size_t>(image.width) + static_cast<size_t>(x)] =
                            decoded.rgba[static_cast<size_t>(y / scale) * static_cast<size_t>(decoded.width) + static_cast<size_t>(x / scale)];
                    }
                }
            }
            return image;
        }

        const int textureCount = std::max(1, decoded.height / 64);
        constexpr int tileScale = 3;
        constexpr int tileSrc = 64;
        constexpr int tileDst = tileSrc * tileScale;
        constexpr int gap = 0;

        TextureImage image;
        image.width = textureCount * tileDst + std::max(0, textureCount - 1) * gap;
        image.height = tileDst;
        image.rgba.assign(static_cast<size_t>(image.width) * static_cast<size_t>(image.height), { 18, 18, 18 });

        for (int t = 0; t < textureCount; ++t)
        {
            const int dstBaseX = t * (tileDst + gap);
            const int srcBaseY = t * tileSrc;
            for (int y = 0; y < tileDst; ++y)
            {
                const int sy = srcBaseY + y / tileScale;
                if (sy >= decoded.height)
                    continue;
                for (int x = 0; x < tileDst; ++x)
                {
                    const int sx = x / tileScale;
                    const RgbColor c = decoded.rgba[static_cast<size_t>(sy) * static_cast<size_t>(decoded.width) + static_cast<size_t>(sx)];
                    image.rgba[static_cast<size_t>(y) * static_cast<size_t>(image.width) + static_cast<size_t>(dstBaseX + x)] = c;
                }
            }
        }
        return image;
    }

    HBITMAP CreateTexturePreviewBitmap(const TextureInfo& info, int& outW, int& outH)
    {
        TextureImage decoded;
        std::wstring error;
        if (!DecodeTextureAsset(info, decoded, error) || decoded.width <= 0 || decoded.height <= 0)
            return nullptr;

        TextureImage image = BuildTexturePreviewImage(info, decoded);
        if (image.width <= 0 || image.height <= 0)
            return nullptr;

        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = image.width;
        bi.bmiHeader.biHeight = -image.height;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HDC hdc = GetDC(nullptr);
        HBITMAP bitmap = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, hdc);
        if (!bitmap || !bits)
            return bitmap;

        auto* dst = static_cast<std::uint8_t*>(bits);
        for (size_t i = 0; i < image.rgba.size(); ++i)
        {
            const RgbColor c = image.rgba[i];
            const size_t o = i * 4U;
            dst[o + 0] = c.b;
            dst[o + 1] = c.g;
            dst[o + 2] = c.r;
            dst[o + 3] = 255;
        }

        outW = image.width;
        outH = image.height;
        return bitmap;
    }

    bool ExportTextureToPng(HWND owner, const TextureInfo& info, fs::path& outPath, std::wstring& error)
    {
        TextureImage image;
        if (!DecodeTextureAsset(info, image, error))
            return false;

        ScopedGdiplus gdiplus;
        if (!gdiplus.Ok())
        {
            error = L"Could not initialise GDI+ for PNG export.";
            return false;
        }

        Gdiplus::Bitmap bitmap(image.width, image.height, PixelFormat32bppARGB);
        Gdiplus::Rect rect(0, 0, image.width, image.height);
        Gdiplus::BitmapData data{};
        if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &data) != Gdiplus::Ok)
        {
            error = L"Could not write PNG bitmap data.";
            return false;
        }

        auto* dst = static_cast<std::uint8_t*>(data.Scan0);
        for (int y = 0; y < image.height; ++y)
        {
            auto* row = dst + static_cast<size_t>(y) * static_cast<size_t>(data.Stride);
            for (int x = 0; x < image.width; ++x)
            {
                const size_t i = static_cast<size_t>(y) * static_cast<size_t>(image.width) + static_cast<size_t>(x);
                const RgbColor c = image.rgba[i];
                const size_t o = static_cast<size_t>(x) * 4U;
                row[o + 0] = c.b;
                row[o + 1] = c.g;
                row[o + 2] = c.r;
                row[o + 3] = 255;
            }
        }
        bitmap.UnlockBits(&data);

        CLSID pngClsid{};
        if (GetEncoderClsid(L"image/png", &pngClsid) < 0)
        {
            error = L"PNG encoder not found.";
            return false;
        }

        std::wstring suggested = info.file.filename().wstring() + L".png";
        if (!BrowseSavePng(owner, suggested, outPath))
            return false;

        if (bitmap.Save(outPath.wstring().c_str(), &pngClsid, nullptr) != Gdiplus::Ok)
        {
            error = L"Could not save PNG:\n" + outPath.wstring();
            return false;
        }

        error.clear();
        return true;
    }

    std::uint16_t EncodeAmiga12(const RgbColor& c)
    {
        const std::uint16_t r = static_cast<std::uint16_t>(((static_cast<int>(c.r) + 8) / 17) & 0x0F);
        const std::uint16_t g = static_cast<std::uint16_t>(((static_cast<int>(c.g) + 8) / 17) & 0x0F);
        const std::uint16_t b = static_cast<std::uint16_t>(((static_cast<int>(c.b) + 8) / 17) & 0x0F);
        return static_cast<std::uint16_t>((r << 8) | (g << 4) | b);
    }

    void WriteBE16(std::vector<std::uint8_t>& out, size_t pos, std::uint16_t value)
    {
        if (pos + 1 >= out.size())
            return;
        out[pos + 0] = static_cast<std::uint8_t>((value >> 8) & 0xffU);
        out[pos + 1] = static_cast<std::uint8_t>(value & 0xffU);
    }

    void WriteBE32(std::vector<std::uint8_t>& out, size_t pos, std::uint32_t value)
    {
        if (pos + 3 >= out.size())
            return;
        out[pos + 0] = static_cast<std::uint8_t>((value >> 24) & 0xffU);
        out[pos + 1] = static_cast<std::uint8_t>((value >> 16) & 0xffU);
        out[pos + 2] = static_cast<std::uint8_t>((value >> 8) & 0xffU);
        out[pos + 3] = static_cast<std::uint8_t>(value & 0xffU);
    }

    bool LoadPngScaled(HWND owner, const fs::path& pngPath, int width, int height, std::vector<RgbColor>& pixels, std::vector<std::uint8_t>& alpha, std::wstring& error)
    {
        (void)owner;
        if (width <= 0 || height <= 0)
        {
            error = L"Invalid target texture size.";
            return false;
        }

        ScopedGdiplus gdiplus;
        if (!gdiplus.Ok())
        {
            error = L"Could not initialise GDI+ for PNG import.";
            return false;
        }

        Gdiplus::Bitmap source(pngPath.wstring().c_str());
        if (source.GetLastStatus() != Gdiplus::Ok)
        {
            error = L"Could not open PNG:\n" + pngPath.wstring();
            return false;
        }

        const UINT srcW = source.GetWidth();
        const UINT srcH = source.GetHeight();
        if (srcW == 0 || srcH == 0)
        {
            error = L"PNG has invalid dimensions.";
            return false;
        }

        pixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height), {});
        alpha.assign(pixels.size(), 255);
        for (int y = 0; y < height; ++y)
        {
            const UINT sy = std::min<UINT>(srcH - 1, static_cast<UINT>((static_cast<std::uint64_t>(y) * srcH) / static_cast<UINT>(height)));
            for (int x = 0; x < width; ++x)
            {
                const UINT sx = std::min<UINT>(srcW - 1, static_cast<UINT>((static_cast<std::uint64_t>(x) * srcW) / static_cast<UINT>(width)));
                Gdiplus::Color c;
                source.GetPixel(sx, sy, &c);
                const size_t i = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                alpha[i] = c.GetA();
                pixels[i] = { static_cast<std::uint8_t>(c.GetR()), static_cast<std::uint8_t>(c.GetG()), static_cast<std::uint8_t>(c.GetB()) };
            }
        }
        error.clear();
        return true;
    }

    bool LoadPngForWallTexture(HWND owner, const fs::path& pngPath, int textureCount, int width, int height, std::vector<RgbColor>& pixels, std::vector<std::uint8_t>& alpha, std::wstring& error)
    {
        if (textureCount <= 1)
            return LoadPngScaled(owner, pngPath, width, height, pixels, alpha, error);

        ScopedGdiplus gdiplus;
        if (!gdiplus.Ok())
        {
            error = L"Could not initialise GDI+ for PNG import.";
            return false;
        }

        Gdiplus::Bitmap source(pngPath.wstring().c_str());
        if (source.GetLastStatus() != Gdiplus::Ok)
        {
            error = L"Could not open PNG:\n" + pngPath.wstring();
            return false;
        }

        const UINT srcW = source.GetWidth();
        const UINT srcH = source.GetHeight();
        if (srcW == 0 || srcH == 0)
        {
            error = L"PNG has invalid dimensions.";
            return false;
        }

        // Normal export is 64 x (N*64). For convenience also accept a horizontal
        // strip/grid like the preview, e.g. (N*64) x 64 or any very wide strip.
        const bool looksHorizontal = (textureCount > 1) && (srcW >= static_cast<UINT>(std::max(2, textureCount)) * 48U) && (srcW > srcH * 2U);
        if (!looksHorizontal)
            return LoadPngScaled(owner, pngPath, width, height, pixels, alpha, error);

        pixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height), {});
        alpha.assign(pixels.size(), 255);
        const UINT tileSrcW = std::max<UINT>(1U, srcW / static_cast<UINT>(textureCount));
        for (int t = 0; t < textureCount; ++t)
        {
            for (int y = 0; y < 64; ++y)
            {
                const UINT sy = std::min<UINT>(srcH - 1, static_cast<UINT>((static_cast<std::uint64_t>(y) * srcH) / 64U));
                for (int x = 0; x < 64; ++x)
                {
                    UINT sx = static_cast<UINT>(t) * tileSrcW + static_cast<UINT>((static_cast<std::uint64_t>(x) * tileSrcW) / 64U);
                    sx = std::min<UINT>(srcW - 1, sx);
                    Gdiplus::Color c;
                    source.GetPixel(sx, sy, &c);
                    const int outY = t * 64 + y;
                    const size_t i = static_cast<size_t>(outY) * 64U + static_cast<size_t>(x);
                    alpha[i] = c.GetA();
                    pixels[i] = { static_cast<std::uint8_t>(c.GetR()), static_cast<std::uint8_t>(c.GetG()), static_cast<std::uint8_t>(c.GetB()) };
                }
            }
        }
        error.clear();
        return true;
    }

    void QuantizeColorsReservedZero(const std::vector<RgbColor>& input, const std::vector<std::uint8_t>& alpha, int maxColorsIncludingZero, std::vector<RgbColor>& palette, std::vector<std::uint8_t>& indexed)
    {
        const int storedColors = std::clamp(maxColorsIncludingZero - 1, 1, 255);
        std::vector<RgbColor> opaque;
        opaque.reserve(input.size());
        for (size_t i = 0; i < input.size(); ++i)
        {
            if (i >= alpha.size() || alpha[i] >= 128)
                opaque.push_back(input[i]);
        }
        if (opaque.empty())
            opaque.push_back({ 0, 0, 0 });

        std::vector<RgbColor> qpal;
        std::vector<std::uint8_t> unused;
        QuantizeColors(opaque, storedColors, qpal, unused);

        palette.clear();
        palette.reserve(static_cast<size_t>(storedColors) + 1U);
        palette.push_back({ 0, 0, 0 });
        for (int i = 0; i < storedColors; ++i)
        {
            if (i < static_cast<int>(qpal.size()))
                palette.push_back(qpal[static_cast<size_t>(i)]);
            else
                palette.push_back({ 0, 0, 0 });
        }

        indexed.resize(input.size());
        for (size_t i = 0; i < input.size(); ++i)
        {
            if (i < alpha.size() && alpha[i] < 128)
            {
                indexed[i] = 0;
                continue;
            }
            int best = 0;
            int bestDist = INT_MAX;
            for (int p = 1; p < static_cast<int>(palette.size()); ++p)
            {
                const RgbColor& pc = palette[static_cast<size_t>(p)];
                const int dr = static_cast<int>(input[i].r) - static_cast<int>(pc.r);
                const int dg = static_cast<int>(input[i].g) - static_cast<int>(pc.g);
                const int db = static_cast<int>(input[i].b) - static_cast<int>(pc.b);
                const int dist = dr * dr + dg * dg + db * db;
                if (dist < bestDist)
                {
                    bestDist = dist;
                    best = p;
                    if (dist == 0)
                        break;
                }
            }
            indexed[i] = static_cast<std::uint8_t>(best);
        }
    }

    bool ImportPngToTexture(HWND owner, const TextureInfo& info, fs::path& importedPath, fs::path& backupPath, std::wstring& error)
    {
        if (!info.exists)
        {
            error = L"Texture file not found.";
            return false;
        }

        std::vector<std::uint8_t> raw;
        if (!LoadMaybeCrM2(info.file, raw, error))
            return false;

        if (!BrowseOpenPng(owner, importedPath))
            return false;

        std::vector<RgbColor> pixels;
        std::vector<std::uint8_t> alpha;
        std::vector<std::uint8_t> indexed;
        std::vector<RgbColor> palette;
        std::vector<std::uint8_t> out;

        if (info.kind == TextureAssetKind::WallTextureSet)
        {
            if (raw.size() < 8)
            {
                error = L"Wall texture data is too small.";
                return false;
            }
            const std::uint32_t paletteOffset = ReadBE32(raw.data());
            if (paletteOffset < 4 || paletteOffset + 2 > raw.size() || ((paletteOffset - 4) % 65) != 0)
            {
                error = L"Invalid wall texture format.";
                return false;
            }
            const int columns = static_cast<int>((paletteOffset - 4) / 65);
            const int textureCount = std::max(1, (columns + 63) / 64);
            const std::uint16_t storedEntries = ReadBE16(raw.data() + paletteOffset);
            const size_t oldPaletteEnd = static_cast<size_t>(paletteOffset) + 2U + static_cast<size_t>(storedEntries) * 2U;
            if (oldPaletteEnd > raw.size())
            {
                error = L"Wall texture palette exceeds file size.";
                return false;
            }

            if (!LoadPngForWallTexture(owner, importedPath, textureCount, 64, textureCount * 64, pixels, alpha, error))
                return false;
            QuantizeColorsReservedZero(pixels, alpha, static_cast<int>(storedEntries) + 1, palette, indexed);

            const std::uint32_t newPaletteOffset = static_cast<std::uint32_t>(4 + columns * 65);
            out.assign(static_cast<size_t>(newPaletteOffset) + 2U + static_cast<size_t>(storedEntries) * 2U, 0);
            WriteBE32(out, 0, newPaletteOffset);
            for (int column = 0; column < columns; ++column)
            {
                const size_t srcBase = 4U + static_cast<size_t>(column) * 65U;
                const size_t dstBase = 4U + static_cast<size_t>(column) * 65U;
                out[dstBase] = (srcBase < raw.size()) ? raw[srcBase] : 0;
                const int tile = column / 64;
                const int x = column % 64;
                for (int y = 0; y < 64; ++y)
                {
                    const size_t pi = static_cast<size_t>(tile * 64 + y) * 64U + static_cast<size_t>(x);
                    out[dstBase + 1U + static_cast<size_t>(y)] = (pi < indexed.size()) ? indexed[pi] : 0;
                }
            }
            WriteBE16(out, newPaletteOffset, storedEntries);
            for (int i = 0; i < static_cast<int>(storedEntries); ++i)
            {
                const RgbColor c = (i + 1 < static_cast<int>(palette.size())) ? palette[static_cast<size_t>(i + 1)] : RgbColor{};
                WriteBE16(out, static_cast<size_t>(newPaletteOffset) + 2U + static_cast<size_t>(i) * 2U, EncodeAmiga12(c));
            }
        }
        else if (info.kind == TextureAssetKind::FlatTexture)
        {
            if (raw.size() < 128U * 128U + 2U)
            {
                error = L"Flat texture data is too small.";
                return false;
            }
            const int entries = std::max<int>(1, static_cast<int>((raw.size() - 128U * 128U) / 2U));
            if (!LoadPngScaled(owner, importedPath, 128, 128, pixels, alpha, error))
                return false;
            QuantizeColors(pixels, entries, palette, indexed);

            out.assign(128U * 128U + static_cast<size_t>(entries) * 2U, 0);
            for (int y = 0; y < 128; ++y)
            {
                for (int x = 0; x < 128; ++x)
                {
                    out[static_cast<size_t>(y) + static_cast<size_t>(x) * 128U] = indexed[static_cast<size_t>(y) * 128U + static_cast<size_t>(x)];
                }
            }
            for (int i = 0; i < entries; ++i)
            {
                const RgbColor c = (i < static_cast<int>(palette.size())) ? palette[static_cast<size_t>(i)] : RgbColor{};
                WriteBE16(out, 128U * 128U + static_cast<size_t>(i) * 2U, EncodeAmiga12(c));
            }
        }
        else
        {
            error = L"Unsupported texture type.";
            return false;
        }

        if (!BackupFile(info.file, backupPath, error))
            return false;

        std::ofstream rawOut(info.file, std::ios::binary | std::ios::trunc);
        if (!rawOut)
        {
            error = L"Could not write texture:\n" + info.file.wstring();
            return false;
        }
        rawOut.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
        rawOut.close();
        error.clear();
        return true;
    }


    void DestroyTexturePreview(TextureManagerDialog* dlg)
    {
        if (dlg && dlg->previewBitmap)
        {
            DeleteObject(dlg->previewBitmap);
            dlg->previewBitmap = nullptr;
            dlg->previewWidth = 0;
            dlg->previewHeight = 0;
        }
    }

    std::wstring TextureKindName(TextureAssetKind kind)
    {
        switch (kind)
        {
        case TextureAssetKind::WallTextureSet: return L"Wall texture set";
        case TextureAssetKind::FlatTexture: return L"Floor/roof flat texture";
        default: return L"Unknown";
        }
    }

    std::wstring TextureInfoText(const TextureInfo& info)
    {
        std::wstringstream s;
        s << L"Texture: " << info.file.wstring() << L"\n";
        s << L"Type: " << TextureKindName(info.kind) << L"\n";
        s << L"Stored bytes: " << info.storedBytes << L"\n";
        s << L"Compression: " << (info.isCrM2 ? L"CrM2 compressed" : L"raw/uncompressed") << L"\n";
        if (info.isCrM2)
            s << L"Raw bytes: " << info.rawBytes << L" (decrunched)\n";
        else
            s << L"Raw bytes: " << info.rawBytes << L"\n";
        if (info.width > 0 && info.height > 0)
            s << L"Preview size: " << info.width << L"x" << info.height << L"\n";
        if (info.kind == TextureAssetKind::WallTextureSet)
        {
            s << L"Columns: " << info.wallColumns << L"\n";
            s << L"64x64 textures: " << info.wallTextureCount << L"\n";
        }
        if (info.colors > 0)
            s << L"Palette entries/colors: " << info.colors << L"\n";
        if (!info.error.empty())
            s << L"\nWarning: " << info.error << L"\n";
        s << L"\nRead path: CrM2 decrypt -> native Gloom texture decoder -> preview/export.\n";
        s << L"Import path: PNG -> indexed Gloom texture -> raw/uncompressed file.\n";
        s << L"CrM2 repacking is disabled; imports are written uncompressed.";
        return s.str();
    }


    RECT TexturePreviewRect(HWND hwnd);
    RECT TexturePreviewViewportRect(HWND hwnd);
    bool HandleTextureManagerMouseWheel(TextureManagerDialog* dlg, int wheelDelta);

    void DrawSlimHorizontalScrollBarLocal(HDC hdc, const RECT& rc, const RECT& thumbRc, bool hover, bool dragging)
    {
        if (!hdc || rc.right <= rc.left || rc.bottom <= rc.top)
            return;

        const int trackHeight = (hover || dragging) ? 8 : 4;
        const int thumbHeight = (hover || dragging) ? 8 : 4;
        const int trackY = rc.top + ((rc.bottom - rc.top) - trackHeight) / 2;
        RECT trackRc{ rc.left, trackY, rc.right, trackY + trackHeight };

        HBRUSH trackBrush = CreateSolidBrush((hover || dragging) ? RGB(48, 48, 56) : RGB(38, 38, 44));
        FillRect(hdc, &trackRc, trackBrush);
        DeleteObject(trackBrush);

        if (thumbRc.right > thumbRc.left)
        {
            const int thumbY = rc.top + ((rc.bottom - rc.top) - thumbHeight) / 2;
            RECT slimThumb{ thumbRc.left, thumbY, thumbRc.right, thumbY + thumbHeight };
            HBRUSH thumbBrush = CreateSolidBrush((hover || dragging) ? RGB(142, 142, 154) : RGB(104, 104, 116));
            FillRect(hdc, &slimThumb, thumbBrush);
            DeleteObject(thumbBrush);
        }
    }

    RECT TextureManagerScrollThumbRect(HWND scroll, TextureManagerDialog* dlg)
    {
        RECT rc{};
        GetClientRect(scroll, &rc);
        if (!dlg || dlg->previewMaxScrollX <= 0)
            return RECT{ rc.left, rc.top, rc.left, rc.top };

        const int trackW = std::max<int>(1, static_cast<int>(rc.right - rc.left - 4));
        RECT area = TexturePreviewViewportRect(dlg->hwnd);
        const int areaW = std::max<int>(1, static_cast<int>(area.right - area.left));
        const int contentW = std::max<int>(areaW, dlg->previewContentWidth);
        const int thumbW = std::max<int>(24, std::min<int>(trackW, static_cast<int>(std::lround(static_cast<double>(trackW) * static_cast<double>(areaW) / static_cast<double>(std::max<int>(1, contentW))))));
        const int thumbRange = std::max<int>(0, trackW - thumbW);
        const int thumbX = rc.left + 2 + (dlg->previewMaxScrollX > 0 ? static_cast<int>(std::lround(static_cast<double>(thumbRange) * static_cast<double>(dlg->previewScrollX) / static_cast<double>(dlg->previewMaxScrollX))) : 0);
        return RECT{ thumbX, rc.top + 2, thumbX + thumbW, rc.bottom - 2 };
    }

    void SetTextureManagerScrollFromX(HWND scroll, TextureManagerDialog* dlg, int sx, int thumbOffsetX)
    {
        if (!scroll || !dlg || dlg->previewMaxScrollX <= 0)
            return;

        RECT rc{};
        GetClientRect(scroll, &rc);
        const RECT thumbRc = TextureManagerScrollThumbRect(scroll, dlg);
        const int thumbW = std::max<int>(1, static_cast<int>(thumbRc.right - thumbRc.left));
        const int trackW = std::max<int>(1, static_cast<int>(rc.right - rc.left - 4));
        const int thumbRange = std::max<int>(1, trackW - thumbW);
        const int targetX = std::max<int>(0, std::min<int>(sx - (rc.left + 2) - thumbOffsetX, thumbRange));
        dlg->previewScrollX = std::max<int>(0, std::min<int>(static_cast<int>(std::lround(static_cast<double>(dlg->previewMaxScrollX) * static_cast<double>(targetX) / static_cast<double>(thumbRange))), dlg->previewMaxScrollX));

        RECT dirty = TexturePreviewRect(dlg->hwnd);
        InvalidateRect(dlg->hwnd, &dirty, FALSE);
        InvalidateRect(scroll, nullptr, TRUE);
    }

    LRESULT CALLBACK TextureManagerScrollProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR refData)
    {
        UNREFERENCED_PARAMETER(wParam);
        UNREFERENCED_PARAMETER(subclassId);
        auto* dlg = reinterpret_cast<TextureManagerDialog*>(refData);
        switch (msg)
        {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            HBRUSH bg = CreateSolidBrush(kBg);
            FillRect(hdc, &rc, bg);
            DeleteObject(bg);
            if (dlg && dlg->previewMaxScrollX > 0)
            {
                const RECT thumbRc = TextureManagerScrollThumbRect(hwnd, dlg);
                DrawSlimHorizontalScrollBarLocal(hdc, rc, thumbRc, dlg->previewScrollHover, dlg->previewScrollDragging);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_MOUSEWHEEL:
            if (HandleTextureManagerMouseWheel(dlg, GET_WHEEL_DELTA_WPARAM(wParam)))
                return 0;
            break;

        case WM_LBUTTONDOWN:
            if (dlg && dlg->previewMaxScrollX > 0)
            {
                const int sx = GET_X_LPARAM(lParam);
                const int sy = GET_Y_LPARAM(lParam);
                RECT rc{};
                GetClientRect(hwnd, &rc);
                if (sx >= rc.left && sx < rc.right && sy >= rc.top && sy < rc.bottom)
                {
                    const RECT thumbRc = TextureManagerScrollThumbRect(hwnd, dlg);
                    if (sx >= thumbRc.left && sx < thumbRc.right)
                        dlg->previewScrollDragOffsetX = sx - thumbRc.left;
                    else
                    {
                        dlg->previewScrollDragOffsetX = std::max<int>(1, static_cast<int>(thumbRc.right - thumbRc.left)) / 2;
                        SetTextureManagerScrollFromX(hwnd, dlg, sx, dlg->previewScrollDragOffsetX);
                    }
                    dlg->previewScrollDragging = true;
                    dlg->previewScrollHover = true;
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                }
            }
            break;

        case WM_MOUSEMOVE:
            if (dlg)
            {
                if (!dlg->previewScrollHover)
                {
                    TRACKMOUSEEVENT tme{};
                    tme.cbSize = sizeof(tme);
                    tme.dwFlags = TME_LEAVE;
                    tme.hwndTrack = hwnd;
                    TrackMouseEvent(&tme);
                    dlg->previewScrollHover = true;
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
                if (dlg->previewScrollDragging)
                {
                    SetTextureManagerScrollFromX(hwnd, dlg, GET_X_LPARAM(lParam), dlg->previewScrollDragOffsetX);
                    return 0;
                }
            }
            break;

        case WM_LBUTTONUP:
            if (dlg && dlg->previewScrollDragging)
            {
                dlg->previewScrollDragging = false;
                if (GetCapture() == hwnd)
                    ReleaseCapture();
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
            break;

        case WM_MOUSELEAVE:
            if (dlg && !dlg->previewScrollDragging)
            {
                dlg->previewScrollHover = false;
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            break;

        case WM_CAPTURECHANGED:
            if (dlg && reinterpret_cast<HWND>(lParam) != hwnd)
            {
                dlg->previewScrollDragging = false;
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            break;

        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, TextureManagerScrollProc, subclassId);
            break;
        }
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

    RECT TexturePreviewRect(HWND hwnd)
    {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        RECT preview{};
        preview.left = 332;
        preview.top = 36;
        preview.right = rc.right - 20;
        preview.bottom = rc.bottom - 232;
        if (preview.bottom < preview.top + 80)
            preview.bottom = preview.top + 80;
        return preview;
    }

    RECT TexturePreviewViewportRect(HWND hwnd)
    {
        RECT preview = TexturePreviewRect(hwnd);
        InflateRect(&preview, -8, -8);
        if (preview.right < preview.left + 1)
            preview.right = preview.left + 1;
        if (preview.bottom < preview.top + 1)
            preview.bottom = preview.top + 1;
        return preview;
    }

    int TexturePreviewScaledWidth(TextureManagerDialog* dlg)
    {
        if (!dlg || dlg->previewWidth <= 0 || dlg->previewHeight <= 0)
            return 0;
        RECT area = TexturePreviewViewportRect(dlg->hwnd);
        const int areaH = std::max<int>(1, static_cast<int>(area.bottom - area.top));
        const double scale = std::min(1.0, static_cast<double>(areaH) / static_cast<double>(dlg->previewHeight));
        return std::max<int>(1, static_cast<int>(dlg->previewWidth * scale));
    }

    void UpdateTexturePreviewScroll(TextureManagerDialog* dlg)
    {
        if (!dlg || !dlg->previewScroll)
            return;

        RECT area = TexturePreviewViewportRect(dlg->hwnd);
        const int areaW = std::max<int>(1, static_cast<int>(area.right - area.left));
        dlg->previewContentWidth = TexturePreviewScaledWidth(dlg);
        const int maxScroll = std::max<int>(0, dlg->previewContentWidth - areaW);
        dlg->previewMaxScrollX = maxScroll;
        dlg->previewScrollX = std::max<int>(0, std::min<int>(dlg->previewScrollX, maxScroll));

        ShowWindow(dlg->previewScroll, SW_SHOW);
        EnableWindow(dlg->previewScroll, maxScroll > 0 ? TRUE : FALSE);
        InvalidateRect(dlg->previewScroll, nullptr, TRUE);
    }

    bool HandleTextureManagerMouseWheel(TextureManagerDialog* dlg, int wheelDelta)
    {
        if (!dlg || !dlg->previewScroll || wheelDelta == 0)
            return false;

        RECT area = TexturePreviewViewportRect(dlg->hwnd);
        const int areaW = std::max<int>(1, static_cast<int>(area.right - area.left));
        dlg->previewContentWidth = TexturePreviewScaledWidth(dlg);
        const int maxScroll = std::max<int>(0, dlg->previewContentWidth - areaW);
        dlg->previewMaxScrollX = maxScroll;
        if (maxScroll <= 0)
            return false;

        dlg->previewWheelRemainder += wheelDelta;
        constexpr int kPreviewWheelPixelsPerNotch = 32;
        const int pixelDelta = (dlg->previewWheelRemainder * kPreviewWheelPixelsPerNotch) / WHEEL_DELTA;
        if (pixelDelta == 0)
            return true;

        dlg->previewWheelRemainder -= (pixelDelta * WHEEL_DELTA) / kPreviewWheelPixelsPerNotch;
        dlg->previewScrollX = std::max<int>(0, std::min<int>(dlg->previewScrollX - pixelDelta, maxScroll));

        InvalidateRect(dlg->previewScroll, nullptr, TRUE);
        RECT dirty = TexturePreviewRect(dlg->hwnd);
        InvalidateRect(dlg->hwnd, &dirty, FALSE);
        return true;
    }

    void RefreshTextureManagerSelection(TextureManagerDialog* dlg)
    {
        if (!dlg)
            return;
        DestroyTexturePreview(dlg);

        const int sel = static_cast<int>(SendMessageW(dlg->list, LB_GETCURSEL, 0, 0));
        dlg->selectedIndex = sel;
        if (sel < 0 || sel >= static_cast<int>(dlg->textures.size()))
        {
            SetWindowTextW(dlg->info, L"");
            EnableWindow(dlg->exportPng, FALSE);
            EnableWindow(dlg->importPng, FALSE);
            dlg->previewScrollX = 0;
            dlg->previewWheelRemainder = 0;
            dlg->previewContentWidth = 0;
            UpdateTexturePreviewScroll(dlg);
            InvalidateRect(dlg->hwnd, nullptr, TRUE);
            return;
        }

        const TextureInfo& info = dlg->textures[static_cast<size_t>(sel)];
        const std::wstring text = NormalizeMultilineForWindowsControl(TextureInfoText(info));
        SetWindowTextW(dlg->info, text.c_str());
        dlg->previewBitmap = CreateTexturePreviewBitmap(info, dlg->previewWidth, dlg->previewHeight);
        dlg->previewScrollX = 0;
        dlg->previewWheelRemainder = 0;
        UpdateTexturePreviewScroll(dlg);
        EnableWindow(dlg->exportPng, dlg->previewBitmap != nullptr);
        EnableWindow(dlg->importPng, info.kind == TextureAssetKind::WallTextureSet || info.kind == TextureAssetKind::FlatTexture);
        InvalidateRect(dlg->hwnd, nullptr, TRUE);
    }

    LRESULT TextureManagerCtlColor(WPARAM wParam, bool edit)
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, kText);
        SetBkColor(hdc, edit ? kEditBg : kPanelBg);
        static HBRUSH editBrush = CreateSolidBrush(kEditBg);
        static HBRUSH panelBrush = CreateSolidBrush(kPanelBg);
        return reinterpret_cast<LRESULT>(edit ? editBrush : panelBrush);
    }

    void LayoutTextureManager(TextureManagerDialog* dlg)
    {
        if (!dlg || !dlg->hwnd)
            return;

        RECT rc{};
        GetClientRect(dlg->hwnd, &rc);

        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;
        const int margin = 16;
        const int listW = 292;
        const int buttonW = 128;
        const int buttonH = 30;
        const int gap = 10;

        const int listTop = 36;
        const int buttonY = h - margin - buttonH;
        const int infoTop = h - 172;
        const int infoH = 94;
        const int rightX = listW + margin * 2 + 8;
        const int rightW = w - rightX - margin;

        MoveWindow(dlg->list, margin, listTop, listW, h - listTop - margin, TRUE);
        MoveWindow(dlg->info, rightX, infoTop, rightW, infoH, TRUE);

        if (dlg->previewScroll)
        {
            RECT preview = TexturePreviewRect(dlg->hwnd);
            MoveWindow(dlg->previewScroll, preview.left, preview.bottom + 8, preview.right - preview.left, 18, TRUE);
            UpdateTexturePreviewScroll(dlg);
        }

        int x = rightX;
        MoveWindow(dlg->exportPng, x, buttonY, buttonW, buttonH, TRUE);
        x += buttonW + gap;
        MoveWindow(dlg->importPng, x, buttonY, buttonW, buttonH, TRUE);
        x += buttonW + gap;
        MoveWindow(dlg->close, x, buttonY, buttonW, buttonH, TRUE);
    }

    void DrawTexturePreview(TextureManagerDialog* dlg, HDC hdc)
    {
        if (!dlg)
            return;

        RECT preview = TexturePreviewRect(dlg->hwnd);
        HBRUSH panel = CreateSolidBrush(kPanelBg);
        FillRect(hdc, &preview, panel);
        DeleteObject(panel);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, kMutedText);
        RECT label = preview;
        label.bottom = label.top - 6;
        label.top -= 24;
        DrawTextW(hdc, L"Preview", -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(70, 70, 70));
        HGDIOBJ oldPen = SelectObject(hdc, borderPen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, preview.left, preview.top, preview.right, preview.bottom);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(borderPen);

        if (!dlg->previewBitmap || dlg->previewWidth <= 0 || dlg->previewHeight <= 0)
        {
            RECT textRc = preview;
            DrawTextW(hdc, L"No texture preview available", -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            return;
        }

        RECT area = TexturePreviewViewportRect(dlg->hwnd);
        const int areaW = std::max<int>(1, static_cast<int>(area.right - area.left));
        const int areaH = std::max<int>(1, static_cast<int>(area.bottom - area.top));
        const double scale = std::min(1.0, static_cast<double>(areaH) / static_cast<double>(dlg->previewHeight));
        const int drawW = std::max<int>(1, static_cast<int>(dlg->previewWidth * scale));
        const int drawH = std::max<int>(1, static_cast<int>(dlg->previewHeight * scale));
        const int maxScroll = std::max<int>(0, drawW - areaW);
        dlg->previewScrollX = std::max<int>(0, std::min<int>(dlg->previewScrollX, maxScroll));

        const int x = (drawW < areaW) ? (area.left + (areaW - drawW) / 2) : (area.left - dlg->previewScrollX);
        const int y = area.top + (areaH - drawH) / 2;

        int saved = SaveDC(hdc);
        IntersectClipRect(hdc, area.left, area.top, area.right, area.bottom);
        HDC mem = CreateCompatibleDC(hdc);
        HGDIOBJ old = SelectObject(mem, dlg->previewBitmap);
        SetStretchBltMode(hdc, COLORONCOLOR);
        StretchBlt(hdc, x, y, drawW, drawH, mem, 0, 0, dlg->previewWidth, dlg->previewHeight, SRCCOPY);
        SelectObject(mem, old);
        DeleteDC(mem);
        RestoreDC(hdc, saved);
    }

    LRESULT CALLBACK TextureManagerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        TextureManagerDialog* dlg = reinterpret_cast<TextureManagerDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (msg)
        {
        case WM_NCCREATE:
        {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            dlg = reinterpret_cast<TextureManagerDialog*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dlg));
            if (dlg)
                dlg->hwnd = hwnd;
            return TRUE;
        }
        case WM_CREATE:
            if (dlg)
            {
                ApplyDarkFrame(hwnd);
                dlg->font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                dlg->list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                    WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP,
                    0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TEXTURE_LIST)), GetModuleHandleW(nullptr), nullptr);
                dlg->info = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                    0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TEXTURE_INFO)), GetModuleHandleW(nullptr), nullptr);
                dlg->exportPng = CreateWindowExW(0, L"BUTTON", L"Export PNG", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                    0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TEXTURE_EXPORT_PNG)), GetModuleHandleW(nullptr), nullptr);
                dlg->importPng = CreateWindowExW(0, L"BUTTON", L"Import PNG", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                    0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TEXTURE_IMPORT_PNG)), GetModuleHandleW(nullptr), nullptr);
                dlg->close = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                    0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TEXTURE_CLOSE)), GetModuleHandleW(nullptr), nullptr);
                dlg->previewScroll = CreateWindowExW(0, L"SCROLLBAR", L"", WS_CHILD | WS_VISIBLE | SBS_HORZ,
                    0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TEXTURE_HSCROLL)), GetModuleHandleW(nullptr), nullptr);
                if (dlg->previewScroll)
                    SetWindowSubclass(dlg->previewScroll, TextureManagerScrollProc, 1, reinterpret_cast<DWORD_PTR>(dlg));

                const HWND controls[] = { dlg->list, dlg->info, dlg->exportPng, dlg->importPng, dlg->close, dlg->previewScroll };
                for (HWND c : controls)
                {
                    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(dlg->font), TRUE);
                    ApplyDarkFrame(c);
                }

                for (const TextureInfo& t : dlg->textures)
                    SendMessageW(dlg->list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t.file.filename().wstring().c_str()));
                SendMessageW(dlg->list, LB_SETCURSEL, 0, 0);
                LayoutTextureManager(dlg);
                RefreshTextureManagerSelection(dlg);
            }
            return 0;
        case WM_SIZE:
            LayoutTextureManager(dlg);
            return 0;
        case WM_ERASEBKGND:
        {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            HBRUSH brush = CreateSolidBrush(kBg);
            FillRect(hdc, &rc, brush);
            DeleteObject(brush);
            return 1;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            DrawTexturePreview(dlg, hdc);
            if (dlg)
            {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, kMutedText);

                RECT listLabel{ 16, 12, 308, 34 };
                DrawTextW(hdc, L"Textures", -1, &listLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                RECT infoLabel{ 332, rc.bottom - 196, rc.right - 20, rc.bottom - 174 };
                DrawTextW(hdc, L"Information", -1, &infoLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CTLCOLORSTATIC:
            return TextureManagerCtlColor(wParam, false);
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            return TextureManagerCtlColor(wParam, true);
        case WM_DRAWITEM:
            return DrawDarkPopupButton(reinterpret_cast<DRAWITEMSTRUCT*>(lParam)) ? TRUE : FALSE;
        case WM_MOUSEWHEEL:
            if (HandleTextureManagerMouseWheel(dlg, GET_WHEEL_DELTA_WPARAM(wParam)))
                return 0;
            break;

        case WM_HSCROLL:
            if (dlg && reinterpret_cast<HWND>(lParam) == dlg->previewScroll)
            {
                RECT area = TexturePreviewViewportRect(dlg->hwnd);
                const int areaW = std::max<int>(1, static_cast<int>(area.right - area.left));
                const int maxScroll = std::max<int>(0, TexturePreviewScaledWidth(dlg) - areaW);
                int pos = dlg->previewScrollX;
                switch (LOWORD(wParam))
                {
                case SB_LINELEFT: pos -= 32; break;
                case SB_LINERIGHT: pos += 32; break;
                case SB_PAGELEFT: pos -= areaW; break;
                case SB_PAGERIGHT: pos += areaW; break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION:
                {
                    SCROLLINFO si{};
                    si.cbSize = sizeof(si);
                    si.fMask = SIF_TRACKPOS;
                    GetScrollInfo(dlg->previewScroll, SB_CTL, &si);
                    pos = si.nTrackPos;
                    break;
                }
                default:
                    break;
                }
                dlg->previewScrollX = std::max<int>(0, std::min<int>(pos, maxScroll));
                SetScrollPos(dlg->previewScroll, SB_CTL, dlg->previewScrollX, TRUE);
                RECT dirty = TexturePreviewRect(dlg->hwnd);
                InvalidateRect(dlg->hwnd, &dirty, FALSE);
                return 0;
            }
            break;
        case WM_COMMAND:
            if (dlg)
            {
                const int id = LOWORD(wParam);
                const int code = HIWORD(wParam);
                if (id == IDC_TEXTURE_LIST && code == LBN_SELCHANGE)
                {
                    RefreshTextureManagerSelection(dlg);
                    return 0;
                }
                if (id == IDC_TEXTURE_EXPORT_PNG)
                {
                    if (dlg->selectedIndex >= 0 && dlg->selectedIndex < static_cast<int>(dlg->textures.size()))
                    {
                        fs::path outPath;
                        std::wstring error;
                        if (ExportTextureToPng(hwnd, dlg->textures[static_cast<size_t>(dlg->selectedIndex)], outPath, error))
                            DarkMessageBox(hwnd, L"PNG exported:\n" + outPath.wstring(), L"Textures", MB_OK | MB_ICONINFORMATION);
                        else if (!error.empty())
                            DarkMessageBox(hwnd, error, L"Textures", MB_OK | MB_ICONERROR);
                    }
                    return 0;
                }
                if (id == IDC_TEXTURE_IMPORT_PNG)
                {
                    if (dlg->selectedIndex >= 0 && dlg->selectedIndex < static_cast<int>(dlg->textures.size()))
                    {
                        fs::path imported;
                        fs::path backup;
                        std::wstring error;
                        TextureInfo current = dlg->textures[static_cast<size_t>(dlg->selectedIndex)];
                        if (ImportPngToTexture(hwnd, current, imported, backup, error))
                        {
                            dlg->textures[static_cast<size_t>(dlg->selectedIndex)] = AnalyzeTextureAsset(current.file);
                            RefreshTextureManagerSelection(dlg);
                            std::wstring msg = L"PNG imported as raw/uncompressed texture.\n\n";
                            msg += L"Source PNG:\n" + imported.wstring() + L"\n\n";
                            msg += L"Backup:\n" + backup.wstring();
                            if (current.isCrM2)
                                msg += L"\n\nOriginal texture was CrM2 compressed. The imported file is intentionally written raw/uncompressed for compatibility testing.";
                            DarkMessageBox(hwnd, msg, L"Textures", MB_OK | MB_ICONINFORMATION);
                        }
                        else if (!error.empty())
                        {
                            DarkMessageBox(hwnd, error, L"Textures", MB_OK | MB_ICONERROR);
                        }
                    }
                    return 0;
                }
                if (id == IDC_TEXTURE_CLOSE)
                {
                    DestroyWindow(hwnd);
                    return 0;
                }
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            DestroyTexturePreview(dlg);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void ShowTextureManager(HWND owner)
    {
        if (!EnsureProfile(owner))
            return;

        TextureManagerDialog dlg;
        dlg.owner = owner;
        dlg.textureDir = g_profile.textureDir;
        dlg.textures = ListTextureAssets(g_profile.textureDir);

        if (dlg.textures.empty())
        {
            DarkMessageBox(owner, L"No readable texture assets found in:\n" + g_profile.textureDir.wstring(), L"Textures", MB_OK | MB_ICONWARNING);
            return;
        }

        static bool registered = false;
        const wchar_t* className = L"ZGloomCampaignTextureManagerWindow";
        if (!registered)
        {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = TextureManagerWndProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = CreateSolidBrush(kBg);
            wc.lpszClassName = className;
            RegisterClassExW(&wc);
            registered = true;
        }

        RECT ownerRc{};
        if (owner)
            GetWindowRect(owner, &ownerRc);
        else
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &ownerRc, 0);

        const int width = 860;
        const int height = 620;
        const int ownerW = ownerRc.right - ownerRc.left;
        const int ownerH = ownerRc.bottom - ownerRc.top;
        const int x = ownerRc.left + std::max(0, (ownerW - width) / 2);
        const int y = ownerRc.top + std::max(0, (ownerH - height) / 2);

        HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE, className, L"Textures",
            WS_CAPTION | WS_SYSMENU | WS_POPUP,
            x, y, width, height,
            owner, nullptr, GetModuleHandleW(nullptr), &dlg);
        if (!dialog)
        {
            DarkMessageBox(owner, L"Could not create the Texture Manager window.", L"Textures", MB_OK | MB_ICONERROR);
            return;
        }

        EnableWindow(owner, FALSE);
        ShowWindow(dialog, SW_SHOW);
        UpdateWindow(dialog);

        MSG msg{};
        while (IsWindow(dialog) && GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            if (!IsDialogMessageW(dialog, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }


    RECT PicturePreviewRect(HWND hwnd)
    {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        RECT preview{};
        preview.left = 332;
        preview.top = 36;
        preview.right = rc.right - 20;
        preview.bottom = 300;
        return preview;
    }

    void DestroyPicturePreview(PictureManagerDialog* dlg)
    {
        if (dlg && dlg->previewBitmap)
        {
            DeleteObject(dlg->previewBitmap);
            dlg->previewBitmap = nullptr;
            dlg->previewWidth = 0;
            dlg->previewHeight = 0;
        }
    }

    std::wstring PictureManagerInfoText(const PictureInfo& info, int paletteMode, int indexMode)
    {
        std::wstringstream s;
        s << PictureSummary(info) << L"\n";
        s << L"Palette mode: " << PaletteModeDisplayName(paletteMode) << L"\n";
        s << L"Index mode: " << PictureIndexModeDisplayName(indexMode) << L"\n";
        s << L"Workflow:\n";
        s << L"- Export PNG decrunches CrM2 and writes a normal PNG.\n";
        s << L"- Import PNG writes the Gloom picture format uncompressed by CrM2, plus .pal.\n";
        s << L"- CrM2 repacking is still disabled.\n";
        if (info.paletteEntries > 0)
            s << L"- Import color limit: " << info.paletteEntries << L" colors.\n";
        if (g_profile.type == GameType::GloomDeluxe)
        {
            s << L"- Gloom Deluxe import keeps palette slots 0-63 reserved for font/UI/overlays; image colors use 64-127.\n";
            s << L"- Font slots 0-3 stay fixed to the yellow Deluxe font colors.\n";
            if (IsGloomLogoAsset(info.imageFile))
                s << L"- Gloom Deluxe logo import uses title.pal directly, writes no separate logo palette, and stores normal palette indexes by default. Import/save title first.\n";
        }
        if (info.paletteLooksNibble)
            s << L"- Palette output keeps 4-bit Amiga nibble values.\n";
        return s.str();
    }

    void RefreshPictureManagerSelection(PictureManagerDialog* dlg)
    {
        if (!dlg || dlg->pictures.empty())
            return;

        int sel = static_cast<int>(SendMessageW(dlg->list, LB_GETCURSEL, 0, 0));
        if (sel < 0 || sel >= static_cast<int>(dlg->pictures.size()))
            sel = 0;
        const bool selectionChanged = dlg->selectedIndex != sel;
        dlg->selectedIndex = sel;

        if (selectionChanged && dlg->indexMode)
        {
            const PictureInfo& selected = dlg->pictures[static_cast<size_t>(sel)];
            dlg->indexModeIndex = DefaultPictureIndexMode(selected);
            SendMessageW(dlg->indexMode, CB_SETCURSEL, dlg->indexModeIndex, 0);
        }

        DestroyPicturePreview(dlg);
        dlg->previewBitmap = CreatePicturePreviewBitmap(dlg->pictures[static_cast<size_t>(sel)], dlg->previewWidth, dlg->previewHeight, dlg->paletteModeIndex, dlg->indexModeIndex);
        const std::wstring infoText = NormalizeMultilineForWindowsControl(PictureManagerInfoText(dlg->pictures[static_cast<size_t>(sel)], dlg->paletteModeIndex, dlg->indexModeIndex));
        SetWindowTextW(dlg->info, infoText.c_str());
        InvalidateRect(dlg->hwnd, nullptr, TRUE);
    }

    LRESULT PictureManagerCtlColor(WPARAM wParam, bool edit)
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, kText);
        SetBkMode(hdc, edit ? OPAQUE : TRANSPARENT);
        SetBkColor(hdc, edit ? kEditBg : kBg);
        static HBRUSH bgBrush = CreateSolidBrush(kBg);
        static HBRUSH editBrush = CreateSolidBrush(kEditBg);
        return reinterpret_cast<LRESULT>(edit ? editBrush : bgBrush);
    }

    void LayoutPictureManager(PictureManagerDialog* dlg)
    {
        if (!dlg || !dlg->hwnd)
            return;
        RECT rc{};
        GetClientRect(dlg->hwnd, &rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;
        const int margin = 16;
        const int listW = 292;
        const int buttonW = 128;
        const int buttonH = 30;
        const int gap = 10;
        const int buttonY = h - margin - buttonH;
        const int indexComboY = buttonY - buttonH - 8;
        const int paletteComboY = indexComboY - buttonH - 8;

        MoveWindow(dlg->list, margin, margin, listW, h - margin * 2, TRUE);
        MoveWindow(dlg->info, 332, 314, w - 352, h - 450, TRUE);

        MoveWindow(dlg->paletteMode, 332, paletteComboY, 300, buttonH + 240, TRUE);
        MoveWindow(dlg->indexMode, 332, indexComboY, 300, buttonH + 140, TRUE);

        int x = 332;
        MoveWindow(dlg->exportPng, x, buttonY, buttonW, buttonH, TRUE);
        x += buttonW + gap;
        MoveWindow(dlg->importPng, x, buttonY, buttonW, buttonH, TRUE);
        MoveWindow(dlg->close, w - margin - buttonW, buttonY, buttonW, buttonH, TRUE);
    }

    void DrawPicturePreview(PictureManagerDialog* dlg, HDC hdc)
    {
        if (!dlg)
            return;

        RECT preview = PicturePreviewRect(dlg->hwnd);
        HBRUSH panel = CreateSolidBrush(kPanelBg);
        FillRect(hdc, &preview, panel);
        DeleteObject(panel);

        SetTextColor(hdc, kMutedText);
        SetBkMode(hdc, TRANSPARENT);
        RECT label = preview;
        label.bottom = label.top - 6;
        label.top -= 24;
        DrawTextW(hdc, L"Preview", -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(70, 70, 70));
        HGDIOBJ oldPen = SelectObject(hdc, borderPen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, preview.left, preview.top, preview.right, preview.bottom);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(borderPen);

        if (!dlg->previewBitmap || dlg->previewWidth <= 0 || dlg->previewHeight <= 0)
        {
            RECT textRc = preview;
            DrawTextW(hdc, L"No preview available", -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            return;
        }

        const int previewW = static_cast<int>(preview.right - preview.left);
        const int previewH = static_cast<int>(preview.bottom - preview.top);
        const int areaW = (previewW > 17) ? (previewW - 16) : 1;
        const int areaH = (previewH > 17) ? (previewH - 16) : 1;
        double scale = std::min(static_cast<double>(areaW) / static_cast<double>(dlg->previewWidth), static_cast<double>(areaH) / static_cast<double>(dlg->previewHeight));
        if (scale <= 0.0)
            scale = 1.0;
        const int drawW = std::max<int>(1, static_cast<int>(dlg->previewWidth * scale));
        const int drawH = std::max<int>(1, static_cast<int>(dlg->previewHeight * scale));
        const int x = preview.left + ((preview.right - preview.left) - drawW) / 2;
        const int y = preview.top + ((preview.bottom - preview.top) - drawH) / 2;

        HDC mem = CreateCompatibleDC(hdc);
        HGDIOBJ oldBitmap = SelectObject(mem, dlg->previewBitmap);
        SetStretchBltMode(hdc, HALFTONE);
        StretchBlt(hdc, x, y, drawW, drawH, mem, 0, 0, dlg->previewWidth, dlg->previewHeight, SRCCOPY);
        SelectObject(mem, oldBitmap);
        DeleteDC(mem);
    }

    LRESULT CALLBACK PictureManagerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        PictureManagerDialog* dlg = reinterpret_cast<PictureManagerDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (msg)
        {
        case WM_NCCREATE:
        {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            dlg = reinterpret_cast<PictureManagerDialog*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dlg));
            dlg->hwnd = hwnd;
            return TRUE;
        }
        case WM_CREATE:
            if (dlg)
            {
                ApplyDarkFrame(hwnd);
                dlg->font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

                dlg->list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                    0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PICTURE_LIST)), GetModuleHandleW(nullptr), nullptr);
                dlg->info = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                    0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PICTURE_INFO)), GetModuleHandleW(nullptr), nullptr);
                dlg->exportPng = CreateWindowExW(0, L"BUTTON", L"Export PNG", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                    0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PICTURE_EXPORT_PNG)), GetModuleHandleW(nullptr), nullptr);
                dlg->importPng = CreateWindowExW(0, L"BUTTON", L"Import PNG", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                    0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PICTURE_IMPORT_PNG)), GetModuleHandleW(nullptr), nullptr);
                dlg->paletteMode = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                    0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PICTURE_PALETTE_MODE)), GetModuleHandleW(nullptr), nullptr);
                dlg->indexMode = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                    0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PICTURE_INDEX_MODE)), GetModuleHandleW(nullptr), nullptr);
                dlg->close = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                    0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PICTURE_CLOSE)), GetModuleHandleW(nullptr), nullptr);

                const HWND controls[] = { dlg->list, dlg->info, dlg->exportPng, dlg->importPng, dlg->paletteMode, dlg->indexMode, dlg->close };
                for (HWND c : controls)
                {
                    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(dlg->font), TRUE);
                    ApplyDarkFrame(c);
                }

                for (int i = 0; i <= 7; ++i)
                {
                    const std::wstring modeName = PaletteModeDisplayName(i);
                    SendMessageW(dlg->paletteMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(modeName.c_str()));
                }
                dlg->paletteModeIndex = 0;
                SendMessageW(dlg->paletteMode, CB_SETCURSEL, dlg->paletteModeIndex, 0);

                for (int i = 0; i <= 3; ++i)
                {
                    const std::wstring modeName = PictureIndexModeDisplayName(i);
                    SendMessageW(dlg->indexMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(modeName.c_str()));
                }
                dlg->indexModeIndex = 0;
                SendMessageW(dlg->indexMode, CB_SETCURSEL, dlg->indexModeIndex, 0);

                for (const PictureInfo& p : dlg->pictures)
                    SendMessageW(dlg->list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(p.imageFile.filename().wstring().c_str()));
                SendMessageW(dlg->list, LB_SETCURSEL, 0, 0);
                LayoutPictureManager(dlg);
                RefreshPictureManagerSelection(dlg);
            }
            return 0;

        case WM_SIZE:
            LayoutPictureManager(dlg);
            return 0;

        case WM_ERASEBKGND:
        {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            HBRUSH brush = CreateSolidBrush(kBg);
            FillRect(hdc, &rc, brush);
            DeleteObject(brush);
            return 1;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            DrawPicturePreview(dlg, hdc);
            if (dlg)
            {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, kMutedText);
                RECT paletteLabel{ 332, rc.bottom - 122, 632, rc.bottom - 100 };
                DrawTextW(hdc, L"Palette mode", -1, &paletteLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                RECT indexLabel{ 332, rc.bottom - 84, 632, rc.bottom - 62 };
                DrawTextW(hdc, L"Index mode", -1, &indexLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CTLCOLORSTATIC:
            return PictureManagerCtlColor(wParam, false);
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            return PictureManagerCtlColor(wParam, true);
        case WM_DRAWITEM:
            return DrawDarkPopupButton(reinterpret_cast<DRAWITEMSTRUCT*>(lParam)) ? TRUE : FALSE;

        case WM_COMMAND:
            if (dlg)
            {
                const int id = LOWORD(wParam);
                const int code = HIWORD(wParam);
                if (id == IDC_PICTURE_LIST && code == LBN_SELCHANGE)
                {
                    RefreshPictureManagerSelection(dlg);
                    return 0;
                }
                if (id == IDC_PICTURE_PALETTE_MODE && code == CBN_SELCHANGE)
                {
                    const int sel = static_cast<int>(SendMessageW(dlg->paletteMode, CB_GETCURSEL, 0, 0));
                    dlg->paletteModeIndex = (sel >= 0 && sel <= 7) ? sel : 0;
                    RefreshPictureManagerSelection(dlg);
                    return 0;
                }
                if (id == IDC_PICTURE_INDEX_MODE && code == CBN_SELCHANGE)
                {
                    const int sel = static_cast<int>(SendMessageW(dlg->indexMode, CB_GETCURSEL, 0, 0));
                    dlg->indexModeIndex = (sel >= 0 && sel <= 3) ? sel : 0;
                    RefreshPictureManagerSelection(dlg);
                    return 0;
                }
                if (id == IDC_PICTURE_EXPORT_PNG)
                {
                    if (dlg->selectedIndex >= 0 && dlg->selectedIndex < static_cast<int>(dlg->pictures.size()))
                    {
                        fs::path outPath;
                        std::wstring error;
                        if (ExportPictureToPng(hwnd, dlg->pictures[static_cast<size_t>(dlg->selectedIndex)], outPath, error, dlg->paletteModeIndex, dlg->indexModeIndex))
                        {
                            DarkMessageBox(hwnd, L"PNG exported:\n" + outPath.wstring(), dlg->title, MB_OK | MB_ICONINFORMATION);
                        }
                        else if (!error.empty())
                        {
                            DarkMessageBox(hwnd, error, dlg->title, MB_OK | MB_ICONERROR);
                        }
                    }
                    return 0;
                }
                if (id == IDC_PICTURE_IMPORT_PNG)
                {
                    if (dlg->selectedIndex >= 0 && dlg->selectedIndex < static_cast<int>(dlg->pictures.size()))
                    {
                        fs::path imported;
                        fs::path backupImage;
                        fs::path backupPalette;
                        std::wstring error;
                        PictureInfo current = dlg->pictures[static_cast<size_t>(dlg->selectedIndex)];
                        if (ImportPngToPicture(hwnd, current, imported, backupImage, backupPalette, error, dlg->paletteModeIndex, dlg->indexModeIndex))
                        {
                            dlg->pictures[static_cast<size_t>(dlg->selectedIndex)] = AnalyzePicture(current.imageFile);
                            RefreshPictureManagerSelection(dlg);
                            std::wstring msg = L"PNG imported as raw/uncompressed picture.\n\n";
                            msg += L"Source PNG:\n" + imported.wstring() + L"\n\n";
                            if (!backupImage.empty())
                                msg += L"Picture backup:\n" + backupImage.wstring() + L"\n\n";
                            if (!backupPalette.empty())
                                msg += L"Palette backup:\n" + backupPalette.wstring() + L"\n\n";
                            msg += L"CrM2 repacking is still disabled.";
                            DarkMessageBox(hwnd, msg, dlg->title, MB_OK | MB_ICONINFORMATION);
                        }
                        else if (!error.empty())
                        {
                            DarkMessageBox(hwnd, error, dlg->title, MB_OK | MB_ICONERROR);
                        }
                    }
                    return 0;
                }
                if (id == IDC_PICTURE_CLOSE)
                {
                    DestroyWindow(hwnd);
                    return 0;
                }
            }
            break;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            DestroyPicturePreview(dlg);
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void ShowPictureManager(HWND owner, bool titleOnly)
    {
        if (!EnsureProfile(owner))
            return;

        PictureManagerDialog dlg;
        dlg.owner = owner;
        dlg.pictureDir = g_profile.picturesDir;
        dlg.titleOnly = titleOnly;
        dlg.title = titleOnly ? L"Title Screen" : L"Title and Intermission Screens";

        if (titleOnly)
        {
            const fs::path title = FindTitleImage(g_profile.picturesDir);
            if (!title.empty())
                dlg.pictures.push_back(AnalyzePicture(title));
        }
        else
        {
            dlg.pictures = ListPictureAssets(g_profile.picturesDir);
        }

        if (dlg.pictures.empty())
        {
            const std::wstring msg = L"No picture assets found in:\n" + g_profile.picturesDir.wstring();
            DarkMessageBox(owner, msg, dlg.title, MB_OK | MB_ICONWARNING);
            return;
        }

        static bool registered = false;
        const wchar_t* className = L"ZGloomCampaignPictureManagerWindow";
        if (!registered)
        {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = className;
            wc.lpfnWndProc = PictureManagerWndProc;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = CreateSolidBrush(kBg);
            RegisterClassExW(&wc);
            registered = true;
        }

        RECT ownerRect{};
        if (owner)
            GetWindowRect(owner, &ownerRect);
        else
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &ownerRect, 0);

        const int width = 860;
        const int height = 620;
        const int ownerW = ownerRect.right - ownerRect.left;
        const int ownerH = ownerRect.bottom - ownerRect.top;
        const int x = ownerRect.left + std::max(0, (ownerW - width) / 2);
        const int y = ownerRect.top + std::max(0, (ownerH - height) / 2);

        HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE, className, dlg.title.c_str(),
            WS_CAPTION | WS_SYSMENU | WS_POPUP,
            x, y, width, height, owner, nullptr, GetModuleHandleW(nullptr), &dlg);
        if (!hwnd)
        {
            DarkMessageBox(owner, L"Could not create picture manager window.", dlg.title, MB_OK | MB_ICONERROR);
            return;
        }

        if (owner)
            EnableWindow(owner, FALSE);
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);

        MSG msg{};
        while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            if (!IsDialogMessageW(hwnd, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        if (owner)
        {
            EnableWindow(owner, TRUE);
            SetForegroundWindow(owner);
        }
    }

    bool SaveScriptBlocks(const fs::path& path, const std::vector<ScriptBlock>& blocks, fs::path& backupPath, std::wstring& error)
    {
        if (!BackupFile(path, backupPath, error))
            return false;

        const std::string fallbackEol = DetectFallbackLineEnding(blocks);
        auto hasScriptHeader = [&]() -> bool
        {
            for (const ScriptBlock& block : blocks)
            {
                if (block.type != ScriptBlockType::Comment)
                    continue;
                const std::wstring line = Lower(ScriptLineFromBlock(block));
                if (line == L";" || line.find(L"script for gloom game") != std::wstring::npos)
                    return true;
            }
            return false;
        };

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            error = L"Could not write campaign script:\n" + path.wstring();
            return false;
        }

        if (!hasScriptHeader())
        {
            const std::string h1 = ";";
            const std::string h2 = ";script for gloom game";
            out.write(h1.data(), static_cast<std::streamsize>(h1.size()));
            out.write(fallbackEol.data(), static_cast<std::streamsize>(fallbackEol.size()));
            out.write(h2.data(), static_cast<std::streamsize>(h2.size()));
            out.write(fallbackEol.data(), static_cast<std::streamsize>(fallbackEol.size()));
        }

        for (const ScriptBlock& block : blocks)
        {
            if (CanPreserveOriginalLine(block))
            {
                out.write(block.originalLineBytes.data(), static_cast<std::streamsize>(block.originalLineBytes.size()));
                out.write(block.lineEndingBytes.data(), static_cast<std::streamsize>(block.lineEndingBytes.size()));
                continue;
            }

            const std::string line = ToAcp(ScriptLineFromBlock(block));
            out.write(line.data(), static_cast<std::streamsize>(line.size()));

            if (block.hasOriginal)
            {
                out.write(block.lineEndingBytes.data(), static_cast<std::streamsize>(block.lineEndingBytes.size()));
            }
            else
            {
                out.write(fallbackEol.data(), static_cast<std::streamsize>(fallbackEol.size()));
            }
        }

        if (!out)
        {
            error = L"Write failed while saving campaign script:\n" + path.wstring();
            return false;
        }
        return true;
    }

    std::wstring GetWindowTextString(HWND hwnd)
    {
        const int len = GetWindowTextLengthW(hwnd);
        std::wstring buffer(static_cast<size_t>(len) + 1, L'\0');
        GetWindowTextW(hwnd, buffer.data(), len + 1);
        buffer.resize(static_cast<size_t>(len));
        return buffer;
    }

    void SetControlFont(HWND hwnd, HFONT font)
    {
        if (hwnd && font)
            SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }

    HWND MakeControl(ScriptEditorDialog* dlg, const wchar_t* cls, const wchar_t* text, DWORD style, DWORD exStyle, int id)
    {
        HWND hwnd = CreateWindowExW(exStyle, cls, text, style, 0, 0, 10, 10, dlg->hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        SetControlFont(hwnd, dlg->font);
        return hwnd;
    }

    void AddComboString(HWND combo, const std::wstring& text)
    {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }

    void SelectComboText(HWND combo, const std::wstring& text)
    {
        const LRESULT index = SendMessageW(combo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(text.c_str()));
        if (index >= 0)
            SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
        else
            SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    }

    void FillComboValues(HWND combo, const std::vector<std::wstring>& values, const std::wstring& currentValue)
    {
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        for (const std::wstring& value : values)
            AddComboString(combo, value);

        const bool hasValues = !values.empty();
        if (hasValues)
            SelectComboText(combo, currentValue);
        else
            SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    }

    void FillTypeCombo(HWND combo)
    {
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    }

    std::wstring BlockValueOrEmpty(const std::vector<ScriptBlock>& blocks, int index)
    {
        if (index < 0 || index >= static_cast<int>(blocks.size()))
            return std::wstring();
        return blocks[index].value;
    }

    int PrimaryIndexForItem(const VisibleScriptItem& item)
    {
        if (item.primaryIndex >= 0)
            return item.primaryIndex;
        if (item.pictureIndex >= 0)
            return item.pictureIndex;
        if (item.tileIndex >= 0)
            return item.tileIndex;
        if (item.textIndex >= 0)
            return item.textIndex;
        if (item.playIndex >= 0)
            return item.playIndex;
        if (item.rawIndex >= 0)
            return item.rawIndex;
        return -1;
    }

    bool IsLevelBoundary(ScriptBlockType type)
    {
        return type == ScriptBlockType::Picture ||
            type == ScriptBlockType::Text ||
            type == ScriptBlockType::PlayMap ||
            type == ScriptBlockType::EndGame ||
            type == ScriptBlockType::Raw ||
            type == ScriptBlockType::Rest ||
            type == ScriptBlockType::Comment ||
            type == ScriptBlockType::Song;
    }

    bool IsBlankRawBlock(const ScriptBlock& block)
    {
        if (block.type != ScriptBlockType::Raw)
            return false;

        const std::wstring line = ScriptLineFromBlock(block);
        return std::all_of(line.begin(), line.end(), [](wchar_t ch) {
            return ch == L' ' || ch == L'\t';
        });
    }

    bool IsHiddenBuilderBlock(const ScriptBlock& block)
    {
        return block.type == ScriptBlockType::Comment ||
            block.type == ScriptBlockType::Rest ||
            IsBlankRawBlock(block);
    }


    int NextNonHiddenBlockIndex(const ScriptEditorDialog* dlg, int startIndex)
    {
        for (int i = startIndex; i < static_cast<int>(dlg->blocks.size()); ++i)
        {
            if (!IsHiddenBuilderBlock(dlg->blocks[i]))
                return i;
        }
        return -1;
    }

    int PreviousNonHiddenBlockIndex(const ScriptEditorDialog* dlg, int startIndex)
    {
        for (int i = startIndex; i >= 0; --i)
        {
            if (!IsHiddenBuilderBlock(dlg->blocks[i]))
                return i;
        }
        return -1;
    }

    void BuildVisibleItems(ScriptEditorDialog* dlg)
    {
        dlg->visibleItems.clear();
        dlg->visibleBlockIndices.clear();

        std::vector<bool> consumed(dlg->blocks.size(), false);
        for (int i = 0; i < static_cast<int>(dlg->blocks.size()); ++i)
        {
            if (consumed[i])
                continue;

            const ScriptBlockType type = dlg->blocks[i].type;

            if (IsHiddenBuilderBlock(dlg->blocks[i]))
            {
                consumed[i] = true;
                continue;
            }

            if (type == ScriptBlockType::Picture)
            {
                VisibleScriptItem item;
                item.type = VisualItemType::Episode;
                item.primaryIndex = i;
                item.pictureIndex = i;
                consumed[i] = true;

                const int nextIndex = NextNonHiddenBlockIndex(dlg, i + 1);
                if (nextIndex >= 0 && dlg->blocks[nextIndex].type == ScriptBlockType::Tile)
                {
                    item.tileIndex = nextIndex;
                    consumed[nextIndex] = true;
                }

                dlg->visibleItems.push_back(item);
                dlg->visibleBlockIndices.push_back(PrimaryIndexForItem(item));
                continue;
            }

            if (type == ScriptBlockType::Tile)
            {
                const int previousIndex = PreviousNonHiddenBlockIndex(dlg, i - 1);
                if (previousIndex >= 0 && dlg->blocks[previousIndex].type == ScriptBlockType::Picture)
                {
                    consumed[i] = true;
                    continue;
                }

                VisibleScriptItem item;
                item.type = VisualItemType::Episode;
                item.primaryIndex = i;
                item.tileIndex = i;
                consumed[i] = true;
                dlg->visibleItems.push_back(item);
                dlg->visibleBlockIndices.push_back(PrimaryIndexForItem(item));
                continue;
            }

            if (type == ScriptBlockType::Text)
            {
                VisibleScriptItem item;
                item.type = VisualItemType::Level;
                item.primaryIndex = i;
                item.textIndex = i;
                consumed[i] = true;
                for (int j = i + 1; j < static_cast<int>(dlg->blocks.size()); ++j)
                {
                    if (IsHiddenBuilderBlock(dlg->blocks[j]))
                        continue;
                    if (dlg->blocks[j].type == ScriptBlockType::PlayMap)
                    {
                        item.playIndex = j;
                        consumed[j] = true;
                        break;
                    }
                    if (IsLevelBoundary(dlg->blocks[j].type))
                    {
                        break;
                    }
                }
                dlg->visibleItems.push_back(item);
                dlg->visibleBlockIndices.push_back(PrimaryIndexForItem(item));
                continue;
            }

            if (type == ScriptBlockType::PlayMap)
            {
                VisibleScriptItem item;
                item.type = VisualItemType::Level;
                item.primaryIndex = i;
                item.playIndex = i;
                consumed[i] = true;
                dlg->visibleItems.push_back(item);
                dlg->visibleBlockIndices.push_back(PrimaryIndexForItem(item));
                continue;
            }

            if (type == ScriptBlockType::EndGame)
            {
                VisibleScriptItem item;
                item.type = VisualItemType::EndGame;
                item.primaryIndex = i;
                consumed[i] = true;
                dlg->visibleItems.push_back(item);
                dlg->visibleBlockIndices.push_back(PrimaryIndexForItem(item));
                continue;
            }

            if (type == ScriptBlockType::Raw || type == ScriptBlockType::Song)
            {
                VisibleScriptItem item;
                item.type = VisualItemType::Raw;
                item.primaryIndex = i;
                item.rawIndex = i;
                consumed[i] = true;
                dlg->visibleItems.push_back(item);
                dlg->visibleBlockIndices.push_back(PrimaryIndexForItem(item));
                continue;
            }
        }
    }

    int VisibleListIndexForBlockIndex(const ScriptEditorDialog* dlg, int blockIndex)
    {
        for (size_t i = 0; i < dlg->visibleItems.size(); ++i)
        {
            const VisibleScriptItem& item = dlg->visibleItems[i];
            if (item.primaryIndex == blockIndex || item.pictureIndex == blockIndex || item.tileIndex == blockIndex ||
                item.textIndex == blockIndex || item.playIndex == blockIndex || item.rawIndex == blockIndex)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int SelectedVisibleIndex(const ScriptEditorDialog* dlg)
    {
        return VisibleListIndexForBlockIndex(dlg, dlg->selectedIndex);
    }

    const VisibleScriptItem* SelectedVisibleItem(const ScriptEditorDialog* dlg)
    {
        const int visibleIndex = SelectedVisibleIndex(dlg);
        if (visibleIndex < 0 || visibleIndex >= static_cast<int>(dlg->visibleItems.size()))
            return nullptr;
        return &dlg->visibleItems[visibleIndex];
    }

    std::wstring DisplayTextForVisibleItem(const ScriptEditorDialog* dlg, const VisibleScriptItem& item, int oneBasedIndex)
    {
        std::wstringstream s;
        s << oneBasedIndex << L". ";
        switch (item.type)
        {
        case VisualItemType::Episode:
        {
            const std::wstring picture = BlockValueOrEmpty(dlg->blocks, item.pictureIndex);
            const std::wstring tile = BlockValueOrEmpty(dlg->blocks, item.tileIndex);
            s << L"Episode  |  Picture: " << (picture.empty() ? L"(empty)" : picture)
                << L"  |  Tile: " << (tile.empty() ? L"(empty)" : tile);
            break;
        }
        case VisualItemType::Level:
        {
            const std::wstring map = BlockValueOrEmpty(dlg->blocks, item.playIndex);
            const std::wstring text = BlockValueOrEmpty(dlg->blocks, item.textIndex);
            s << L"Level  |  Map: " << (map.empty() ? L"(empty)" : map)
                << L"  |  Text: " << Shorten(text, 56);
            break;
        }
        case VisualItemType::EndGame:
            s << L"End Game  |  done_";
            break;
        default:
        {
            const int rawIndex = item.rawIndex >= 0 ? item.rawIndex : item.primaryIndex;
            if (rawIndex >= 0 && rawIndex < static_cast<int>(dlg->blocks.size()))
                s << L"Raw  |  " << Shorten(ScriptLineFromBlock(dlg->blocks[rawIndex]), 74);
            else
                s << L"Raw";
            break;
        }
        }
        return s.str();
    }

    int FirstVisibleBlockIndexNear(const ScriptEditorDialog* dlg, int preferredIndex)
    {
        if (dlg->visibleItems.empty())
            return -1;

        int bestIndex = -1;
        int bestDistance = INT_MAX;
        for (const VisibleScriptItem& item : dlg->visibleItems)
        {
            const int index = PrimaryIndexForItem(item);
            if (index < 0)
                continue;
            const int distance = std::abs(index - preferredIndex);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestIndex = index;
            }
        }
        return bestIndex;
    }

    std::pair<int, int> GroupRangeForItem(const ScriptEditorDialog* dlg, const VisibleScriptItem& item)
    {
        int start = INT_MAX;
        int end = -1;
        auto includeIndex = [&](int index)
        {
            if (index >= 0 && index < static_cast<int>(dlg->blocks.size()))
            {
                start = std::min(start, index);
                end = std::max(end, index);
            }
        };

        includeIndex(item.primaryIndex);
        includeIndex(item.pictureIndex);
        includeIndex(item.tileIndex);
        includeIndex(item.textIndex);
        includeIndex(item.playIndex);
        includeIndex(item.rawIndex);

        if (start == INT_MAX || end < 0)
            return { -1, -1 };


        if (item.type == VisualItemType::Level)
        {
            while (start > 0 && (dlg->blocks[start - 1].type == ScriptBlockType::Draw || dlg->blocks[start - 1].type == ScriptBlockType::Show))
                --start;
            while (end + 1 < static_cast<int>(dlg->blocks.size()) &&
                (dlg->blocks[end + 1].type == ScriptBlockType::Wait || dlg->blocks[end + 1].type == ScriptBlockType::Dark))
            {
                ++end;
            }
        }

        return { start, end };
    }

    void RefreshBlockList(ScriptEditorDialog* dlg)
    {
        BuildVisibleItems(dlg);
        dlg->updating = true;
        SendMessageW(dlg->blockList, LB_RESETCONTENT, 0, 0);

        for (size_t i = 0; i < dlg->visibleItems.size(); ++i)
        {
            const std::wstring text = DisplayTextForVisibleItem(dlg, dlg->visibleItems[i], static_cast<int>(i + 1));
            SendMessageW(dlg->blockList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        }

        if (!dlg->visibleItems.empty())
        {
            int visibleIndex = VisibleListIndexForBlockIndex(dlg, dlg->selectedIndex);
            if (visibleIndex < 0)
            {
                dlg->selectedIndex = PrimaryIndexForItem(dlg->visibleItems.front());
                visibleIndex = 0;
            }
            SendMessageW(dlg->blockList, LB_SETCURSEL, static_cast<WPARAM>(visibleIndex), 0);
            if (visibleIndex == 0)
                SendMessageW(dlg->blockList, LB_SETTOPINDEX, 0, 0);
        }
        else
        {
            dlg->selectedIndex = -1;
        }
        dlg->updating = false;
    }

    void RefreshOneListItem(ScriptEditorDialog* dlg, int index)
    {
        (void)index;
        RefreshBlockList(dlg);
    }

    void LayoutScriptEditor(ScriptEditorDialog* dlg);

    void FillSelectionEditor(ScriptEditorDialog* dlg)
    {
        dlg->updating = true;

        const VisibleScriptItem* item = SelectedVisibleItem(dlg);
        const bool hasSelection = item != nullptr;
        EnableWindow(dlg->deleteBlock, hasSelection ? TRUE : FALSE);

        const int visibleIndex = SelectedVisibleIndex(dlg);
        EnableWindow(dlg->moveUp, hasSelection && visibleIndex > 0 ? TRUE : FALSE);
        EnableWindow(dlg->moveDown, hasSelection && visibleIndex >= 0 && visibleIndex + 1 < static_cast<int>(dlg->visibleItems.size()) ? TRUE : FALSE);

        ShowWindow(dlg->typeCombo, SW_SHOW);
        ShowWindow(dlg->valueCombo, SW_SHOW);
        ShowWindow(dlg->mapCombo, SW_SHOW);
        ShowWindow(dlg->valueEdit, SW_SHOW);
        ShowWindow(dlg->tileLabel, SW_SHOW);
        ShowWindow(dlg->pictureLabel, SW_SHOW);
        ShowWindow(dlg->mapLabel, SW_SHOW);
        ShowWindow(dlg->levelTextLabel, SW_SHOW);
        ShowWindow(dlg->valueLabel, SW_HIDE);

        EnableWindow(dlg->typeCombo, FALSE);
        EnableWindow(dlg->valueCombo, FALSE);
        EnableWindow(dlg->mapCombo, FALSE);
        EnableWindow(dlg->valueEdit, FALSE);
        SendMessageW(dlg->typeCombo, CB_RESETCONTENT, 0, 0);
        SendMessageW(dlg->valueCombo, CB_RESETCONTENT, 0, 0);
        SendMessageW(dlg->mapCombo, CB_RESETCONTENT, 0, 0);
        SetWindowTextW(dlg->valueEdit, L"");
        SetWindowTextW(dlg->tileLabel, L"Floor and roof tile");
        SetWindowTextW(dlg->pictureLabel, L"Episode picture");
        SetWindowTextW(dlg->mapLabel, L"Map");
        SetWindowTextW(dlg->levelTextLabel, L"Level text");

        if (!hasSelection)
        {
            FillComboValues(dlg->typeCombo, dlg->tileValues, L"");
            FillComboValues(dlg->valueCombo, dlg->pictureNames, L"");
            FillComboValues(dlg->mapCombo, dlg->mapNames, L"");
            EnableWindow(dlg->typeCombo, FALSE);
            EnableWindow(dlg->valueCombo, FALSE);
            EnableWindow(dlg->mapCombo, FALSE);
            EnableWindow(dlg->valueEdit, FALSE);
            SetWindowTextW(dlg->helpLabel, L"Add an Episode, Level, End Game marker, or Raw command.");
            dlg->updating = false;
            LayoutScriptEditor(dlg);
            return;
        }

        switch (item->type)
        {
        case VisualItemType::Episode:
        {
            FillComboValues(dlg->typeCombo, dlg->tileValues, BlockValueOrEmpty(dlg->blocks, item->tileIndex));
            FillComboValues(dlg->valueCombo, dlg->pictureNames, BlockValueOrEmpty(dlg->blocks, item->pictureIndex));
            FillComboValues(dlg->mapCombo, dlg->mapNames, L"");
            SetWindowTextW(dlg->valueEdit, L"");
            EnableWindow(dlg->typeCombo, dlg->tileValues.empty() ? FALSE : TRUE);
            EnableWindow(dlg->valueCombo, dlg->pictureNames.empty() ? FALSE : TRUE);
            EnableWindow(dlg->mapCombo, FALSE);
            EnableWindow(dlg->valueEdit, FALSE);
            SetWindowTextW(dlg->helpLabel, L"Episode card: choose the floor/roof tile and the intermission picture. The picture remains active until the next Episode card.");
            break;
        }
        case VisualItemType::Level:
        {
            FillComboValues(dlg->typeCombo, dlg->tileValues, L"");
            FillComboValues(dlg->valueCombo, dlg->pictureNames, L"");
            FillComboValues(dlg->mapCombo, dlg->mapNames, BlockValueOrEmpty(dlg->blocks, item->playIndex));
            SetWindowTextW(dlg->valueEdit, BlockValueOrEmpty(dlg->blocks, item->textIndex).c_str());
            EnableWindow(dlg->typeCombo, FALSE);
            EnableWindow(dlg->valueCombo, FALSE);
            EnableWindow(dlg->mapCombo, dlg->mapNames.empty() ? FALSE : TRUE);
            EnableWindow(dlg->valueEdit, TRUE);
            SetWindowTextW(dlg->helpLabel, L"Level card: choose the map and enter the intermission text. draw_/show_/wait_/dark_ are generated and preserved automatically.");
            break;
        }
        case VisualItemType::EndGame:
            FillComboValues(dlg->typeCombo, dlg->tileValues, L"");
            FillComboValues(dlg->valueCombo, dlg->pictureNames, L"");
            FillComboValues(dlg->mapCombo, dlg->mapNames, L"");
            SetWindowTextW(dlg->valueEdit, L"done_");
            EnableWindow(dlg->typeCombo, FALSE);
            EnableWindow(dlg->valueCombo, FALSE);
            EnableWindow(dlg->mapCombo, FALSE);
            EnableWindow(dlg->valueEdit, FALSE);
            SetWindowTextW(dlg->helpLabel, L"Writes done_ to end the campaign/script flow.");
            break;
        default:
        {
            const int rawIndex = item->rawIndex >= 0 ? item->rawIndex : item->primaryIndex;
            FillComboValues(dlg->typeCombo, dlg->tileValues, L"");
            FillComboValues(dlg->valueCombo, dlg->pictureNames, L"");
            FillComboValues(dlg->mapCombo, dlg->mapNames, L"");
            SetWindowTextW(dlg->levelTextLabel, L"Raw script line");
            if (rawIndex >= 0 && rawIndex < static_cast<int>(dlg->blocks.size()))
                SetWindowTextW(dlg->valueEdit, ScriptLineFromBlock(dlg->blocks[rawIndex]).c_str());
            else
                SetWindowTextW(dlg->valueEdit, L"");
            EnableWindow(dlg->typeCombo, FALSE);
            EnableWindow(dlg->valueCombo, FALSE);
            EnableWindow(dlg->mapCombo, FALSE);
            EnableWindow(dlg->valueEdit, TRUE);
            SetWindowTextW(dlg->helpLabel, L"Unknown or legacy command. It is preserved as a raw script line.");
            break;
        }
        }

        dlg->updating = false;
        LayoutScriptEditor(dlg);
    }

    bool ApplySelection(ScriptEditorDialog* dlg)
    {
        if (dlg->updating)
            return false;

        const VisibleScriptItem* item = SelectedVisibleItem(dlg);
        if (!item)
            return false;

        bool changed = false;
        auto setValue = [&](int index, const std::wstring& value)
        {
            if (index < 0 || index >= static_cast<int>(dlg->blocks.size()))
                return;
            if (dlg->blocks[index].value != value)
            {
                dlg->blocks[index].value = value;
                changed = true;
            }
        };

        switch (item->type)
        {
        case VisualItemType::Episode:
        {
            const int pictureSel = static_cast<int>(SendMessageW(dlg->valueCombo, CB_GETCURSEL, 0, 0));
            if (pictureSel >= 0)
            {
                const int len = static_cast<int>(SendMessageW(dlg->valueCombo, CB_GETLBTEXTLEN, static_cast<WPARAM>(pictureSel), 0));
                if (len >= 0)
                {
                    std::wstring picture(static_cast<size_t>(len) + 1, L'\0');
                    SendMessageW(dlg->valueCombo, CB_GETLBTEXT, static_cast<WPARAM>(pictureSel), reinterpret_cast<LPARAM>(picture.data()));
                    picture.resize(static_cast<size_t>(len));
                    setValue(item->pictureIndex, picture);
                }
            }

            const int tileSel = static_cast<int>(SendMessageW(dlg->typeCombo, CB_GETCURSEL, 0, 0));
            if (tileSel >= 0)
            {
                const int len = static_cast<int>(SendMessageW(dlg->typeCombo, CB_GETLBTEXTLEN, static_cast<WPARAM>(tileSel), 0));
                if (len >= 0)
                {
                    std::wstring tile(static_cast<size_t>(len) + 1, L'\0');
                    SendMessageW(dlg->typeCombo, CB_GETLBTEXT, static_cast<WPARAM>(tileSel), reinterpret_cast<LPARAM>(tile.data()));
                    tile.resize(static_cast<size_t>(len));
                    setValue(item->tileIndex, tile);
                }
            }
            break;
        }
        case VisualItemType::Level:
        {
            setValue(item->textIndex, GetWindowTextString(dlg->valueEdit));
            const int mapSel = static_cast<int>(SendMessageW(dlg->mapCombo, CB_GETCURSEL, 0, 0));
            if (mapSel >= 0)
            {
                const int len = static_cast<int>(SendMessageW(dlg->mapCombo, CB_GETLBTEXTLEN, static_cast<WPARAM>(mapSel), 0));
                if (len >= 0)
                {
                    std::wstring map(static_cast<size_t>(len) + 1, L'\0');
                    SendMessageW(dlg->mapCombo, CB_GETLBTEXT, static_cast<WPARAM>(mapSel), reinterpret_cast<LPARAM>(map.data()));
                    map.resize(static_cast<size_t>(len));
                    setValue(item->playIndex, map);
                }
            }
            break;
        }
        case VisualItemType::Raw:
        {
            const int rawIndex = item->rawIndex >= 0 ? item->rawIndex : item->primaryIndex;
            if (rawIndex >= 0 && rawIndex < static_cast<int>(dlg->blocks.size()))
            {
                const std::wstring newValue = GetWindowTextString(dlg->valueEdit);
                if (dlg->blocks[rawIndex].value != newValue || dlg->blocks[rawIndex].type != ScriptBlockType::Raw)
                {
                    dlg->blocks[rawIndex].type = ScriptBlockType::Raw;
                    dlg->blocks[rawIndex].value = newValue;
                    dlg->blocks[rawIndex].rawLine = newValue;
                    changed = true;
                }
            }
            break;
        }
        default:
            break;
        }

        if (changed)
        {
            dlg->dirty = true;
            RefreshOneListItem(dlg, dlg->selectedIndex);
        }
        return changed;
    }

    ScriptBlock MakeDefaultBlock(ScriptBlockType type, const ScriptEditorDialog* dlg)
    {
        ScriptBlock block;
        block.type = type;
        switch (type)
        {
        case ScriptBlockType::Picture:
            block.value = dlg->pictureNames.empty() ? L"spacehulk" : dlg->pictureNames.front();
            break;
        case ScriptBlockType::Tile:
            block.value = dlg->tileValues.empty() ? L"1" : dlg->tileValues.front();
            break;
        case ScriptBlockType::Text:
            block.value = L"new intermission text";
            break;
        case ScriptBlockType::PlayMap:
            block.value = dlg->mapNames.empty() ? L"map1_1" : dlg->mapNames.front();
            break;
        case ScriptBlockType::Song:
            block.value = dlg->musicNames.empty() ? L"title" : dlg->musicNames.front();
            break;
        case ScriptBlockType::EndGame:
            break;
        case ScriptBlockType::Raw:
            block.value = L"raw_command";
            block.rawLine = block.value;
            break;
        default:
            break;
        }
        return block;
    }

    void AddBlock(ScriptEditorDialog* dlg, ScriptBlockType type)
    {
        ApplySelection(dlg);
        const VisibleScriptItem* selectedItem = SelectedVisibleItem(dlg);
        const int insertAt = selectedItem ? GroupRangeForItem(dlg, *selectedItem).second + 1 :
            ((dlg->selectedIndex >= 0) ? dlg->selectedIndex + 1 : static_cast<int>(dlg->blocks.size()));
        dlg->blocks.insert(dlg->blocks.begin() + insertAt, MakeDefaultBlock(type, dlg));
        dlg->selectedIndex = insertAt;
        dlg->dirty = true;
        RefreshBlockList(dlg);
        FillSelectionEditor(dlg);
        SetFocus(dlg->valueEdit);
    }

    void AddEpisodeSequence(ScriptEditorDialog* dlg)
    {
        ApplySelection(dlg);
        const VisibleScriptItem* selectedItem = SelectedVisibleItem(dlg);
        const int insertAt = selectedItem ? GroupRangeForItem(dlg, *selectedItem).second + 1 : static_cast<int>(dlg->blocks.size());
        std::vector<ScriptBlock> sequence;
        sequence.push_back(MakeDefaultBlock(ScriptBlockType::Picture, dlg));
        sequence.push_back(MakeDefaultBlock(ScriptBlockType::Tile, dlg));
        dlg->blocks.insert(dlg->blocks.begin() + insertAt, sequence.begin(), sequence.end());
        dlg->selectedIndex = insertAt;
        dlg->dirty = true;
        RefreshBlockList(dlg);
        FillSelectionEditor(dlg);
        SetFocus(dlg->valueEdit);
    }

    void AddLevelSequence(ScriptEditorDialog* dlg)
    {
        ApplySelection(dlg);
        const VisibleScriptItem* selectedItem = SelectedVisibleItem(dlg);
        const int insertAt = selectedItem ? GroupRangeForItem(dlg, *selectedItem).second + 1 : static_cast<int>(dlg->blocks.size());
        std::vector<ScriptBlock> sequence;
        sequence.push_back(MakeDefaultBlock(ScriptBlockType::Draw, dlg));
        sequence.push_back(MakeDefaultBlock(ScriptBlockType::Show, dlg));
        sequence.push_back(MakeDefaultBlock(ScriptBlockType::Text, dlg));
        sequence.push_back(MakeDefaultBlock(ScriptBlockType::Wait, dlg));
        sequence.push_back(MakeDefaultBlock(ScriptBlockType::Dark, dlg));
        sequence.push_back(MakeDefaultBlock(ScriptBlockType::PlayMap, dlg));
        dlg->blocks.insert(dlg->blocks.begin() + insertAt, sequence.begin(), sequence.end());
        dlg->selectedIndex = insertAt + 2;
        dlg->dirty = true;
        RefreshBlockList(dlg);
        FillSelectionEditor(dlg);
        SetFocus(dlg->valueEdit);
    }

    void AddEndGameBlock(ScriptEditorDialog* dlg)
    {
        ApplySelection(dlg);
        const VisibleScriptItem* selectedItem = SelectedVisibleItem(dlg);
        const int insertAt = selectedItem ? GroupRangeForItem(dlg, *selectedItem).second + 1 : static_cast<int>(dlg->blocks.size());
        dlg->blocks.insert(dlg->blocks.begin() + insertAt, MakeDefaultBlock(ScriptBlockType::EndGame, dlg));
        dlg->selectedIndex = insertAt;
        dlg->dirty = true;
        RefreshBlockList(dlg);
        FillSelectionEditor(dlg);
    }

    void DeleteSelectedBlock(ScriptEditorDialog* dlg)
    {
        const VisibleScriptItem* item = SelectedVisibleItem(dlg);
        if (!item)
            return;
        const auto range = GroupRangeForItem(dlg, *item);
        if (range.first < 0 || range.second < range.first)
            return;
        const int oldIndex = range.first;
        dlg->blocks.erase(dlg->blocks.begin() + range.first, dlg->blocks.begin() + range.second + 1);
        dlg->dirty = true;
        BuildVisibleItems(dlg);
        dlg->selectedIndex = FirstVisibleBlockIndexNear(dlg, oldIndex);
        RefreshBlockList(dlg);
        FillSelectionEditor(dlg);
    }

    void MoveSelectedBlock(ScriptEditorDialog* dlg, int delta)
    {
        ApplySelection(dlg);
        BuildVisibleItems(dlg);
        const int visibleIndex = SelectedVisibleIndex(dlg);
        if (visibleIndex < 0)
            return;
        const int targetVisible = visibleIndex + delta;
        if (targetVisible < 0 || targetVisible >= static_cast<int>(dlg->visibleItems.size()))
            return;

        const VisibleScriptItem current = dlg->visibleItems[visibleIndex];
        const VisibleScriptItem target = dlg->visibleItems[targetVisible];
        const auto currentRange = GroupRangeForItem(dlg, current);
        const auto targetRange = GroupRangeForItem(dlg, target);
        if (currentRange.first < 0 || targetRange.first < 0)
            return;

        std::vector<ScriptBlock> segment(dlg->blocks.begin() + currentRange.first, dlg->blocks.begin() + currentRange.second + 1);
        const int selectedOffset = std::max(0, dlg->selectedIndex - currentRange.first);
        const int segmentLen = currentRange.second - currentRange.first + 1;

        int insertAt = targetRange.first;
        if (delta > 0)
            insertAt = targetRange.second - segmentLen + 1;

        dlg->blocks.erase(dlg->blocks.begin() + currentRange.first, dlg->blocks.begin() + currentRange.second + 1);
        if (delta < 0)
        {
            insertAt = targetRange.first;
        }
        else
        {
            insertAt = targetRange.second - segmentLen + 1;
        }
        insertAt = std::clamp(insertAt, 0, static_cast<int>(dlg->blocks.size()));
        dlg->blocks.insert(dlg->blocks.begin() + insertAt, segment.begin(), segment.end());
        dlg->selectedIndex = insertAt + selectedOffset;
        dlg->dirty = true;
        RefreshBlockList(dlg);
        FillSelectionEditor(dlg);
    }

    void UseKnownValue(ScriptEditorDialog* dlg)
    {
        if (dlg->updating)
            return;
        ApplySelection(dlg);
    }

    bool SaveScriptEditor(ScriptEditorDialog* dlg)
    {
        ApplySelection(dlg);

        const bool wasCrM2 = IsCrM2File(dlg->scriptFile);

        fs::path backup;
        std::wstring error;
        if (!SaveScriptBlocks(dlg->scriptFile, dlg->blocks, backup, error))
        {
            DarkMessageBox(dlg->hwnd, error, L"Campaign Script", MB_OK | MB_ICONERROR);
            return false;
        }

        dlg->dirty = false;
        std::wstring msg = L"Campaign script saved.";
        if (wasCrM2)
            msg += L"\n\nOriginal file was CrM2 compressed. This save wrote a raw/uncompressed script for compatibility testing.";
        if (!backup.empty())
            msg += L"\n\nBackup:\n" + backup.wstring();
        DarkMessageBox(dlg->hwnd, msg, L"Campaign Script", MB_OK | MB_ICONINFORMATION);
        return true;
    }

    bool ConfirmCloseScriptEditor(ScriptEditorDialog* dlg)
    {
        ApplySelection(dlg);
        if (!dlg->dirty)
            return true;

        const int answer = DarkMessageBox(dlg->hwnd,
            L"The campaign script has unsaved changes.\n\nSave before closing?",
            L"Campaign Script", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (answer == IDCANCEL)
            return false;
        if (answer == IDYES)
            return SaveScriptEditor(dlg);
        return true;
    }

    void LayoutScriptEditor(ScriptEditorDialog* dlg)
    {
        RECT rc{};
        GetClientRect(dlg->hwnd, &rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;
        const int pad = 14;
        const int top = 48;
        const int bottom = 58;
        const int leftW = std::max(410, std::min(560, w / 2));
        const int rightX = pad + leftW + pad;
        const int rightW = std::max(240, w - rightX - pad);

        MoveWindow(dlg->statusLabel, pad, 14, w - pad * 2, 24, TRUE);
        MoveWindow(dlg->blockList, pad, top, leftW, h - top - bottom, TRUE);

        const VisibleScriptItem* item = SelectedVisibleItem(dlg);
        const bool rawSelected = item && item->type == VisualItemType::Raw;
        const int comboH = 24;
        const int labelH = 18;
        const int editH = rawSelected ? 56 : 72;
        const int helpH = 74;
        const int rowGap = 8;

        int y = top;
        MoveWindow(dlg->valueLabel, rightX, y, rightW, 20, TRUE);
        ShowWindow(dlg->valueLabel, SW_HIDE);

        MoveWindow(dlg->tileLabel, rightX, y, rightW, labelH, TRUE);
        y += labelH + 3;
        MoveWindow(dlg->typeCombo, rightX, y, rightW, 220, TRUE);
        y += comboH + rowGap;

        MoveWindow(dlg->pictureLabel, rightX, y, rightW, labelH, TRUE);
        y += labelH + 3;
        MoveWindow(dlg->valueCombo, rightX, y, rightW, 220, TRUE);
        y += comboH + rowGap;

        MoveWindow(dlg->mapLabel, rightX, y, rightW, labelH, TRUE);
        y += labelH + 3;
        MoveWindow(dlg->mapCombo, rightX, y, rightW, 220, TRUE);
        y += comboH + rowGap;

        MoveWindow(dlg->levelTextLabel, rightX, y, rightW, labelH, TRUE);
        y += labelH + 3;
        MoveWindow(dlg->valueEdit, rightX, y, rightW, editH, TRUE);
        y += editH + 10;

        MoveWindow(dlg->helpLabel, rightX, y, rightW, helpH, TRUE);
        y += helpH + 12;

        if (dlg->addTile) ShowWindow(dlg->addTile, SW_HIDE);
        if (dlg->addText) ShowWindow(dlg->addText, SW_HIDE);
        if (dlg->addWait) ShowWindow(dlg->addWait, SW_HIDE);
        if (dlg->addDraw) ShowWindow(dlg->addDraw, SW_HIDE);
        if (dlg->addShow) ShowWindow(dlg->addShow, SW_HIDE);
        if (dlg->addDark) ShowWindow(dlg->addDark, SW_HIDE);
        if (dlg->addSong) ShowWindow(dlg->addSong, SW_HIDE);

        const int btnH = 28;
        const int gap = 8;
        const int addBtnW = 94;
        const int moveBtnW = 82;
        int x = rightX;
        MoveWindow(dlg->addPicture, x, y, addBtnW, btnH, TRUE); x += addBtnW + gap;
        MoveWindow(dlg->addPlayMap, x, y, addBtnW, btnH, TRUE); x += addBtnW + gap;
        MoveWindow(dlg->addEndGame, x, y, addBtnW, btnH, TRUE); x += addBtnW + gap;
        MoveWindow(dlg->addRaw, x, y, addBtnW, btnH, TRUE);

        y += btnH + 8;
        x = rightX;
        MoveWindow(dlg->moveUp, x, y, moveBtnW, btnH, TRUE); x += moveBtnW + gap;
        MoveWindow(dlg->moveDown, x, y, moveBtnW, btnH, TRUE); x += moveBtnW + gap;
        MoveWindow(dlg->deleteBlock, x, y, moveBtnW, btnH, TRUE);

        const int actionY = h - 40;
        const int actionBtnW = 128;
        const int actionGap = 12;
        const int actionTotalW = actionBtnW * 3 + actionGap * 2;
        int actionX = w - pad - actionTotalW;
        MoveWindow(dlg->openRaw, actionX, actionY, actionBtnW, 30, TRUE); actionX += actionBtnW + actionGap;
        MoveWindow(dlg->save, actionX, actionY, actionBtnW, 30, TRUE); actionX += actionBtnW + actionGap;
        MoveWindow(dlg->close, actionX, actionY, actionBtnW, 30, TRUE);
    }

    void ApplyDarkThemeToChild(HWND hwnd)
    {
        if (!hwnd)
            return;
        SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
    }

    void CreateScriptEditorControls(ScriptEditorDialog* dlg)
    {
        dlg->font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        dlg->statusLabel = MakeControl(dlg, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, -1);
        dlg->blockList = MakeControl(dlg, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
            WS_EX_CLIENTEDGE, IDC_SCRIPT_BLOCK_LIST);
        dlg->typeCombo = MakeControl(dlg, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, IDC_SCRIPT_TYPE_COMBO);
        dlg->valueLabel = MakeControl(dlg, L"STATIC", L"Value", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, -1);
        dlg->tileLabel = MakeControl(dlg, L"STATIC", L"Floor and roof tile", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, -1);
        dlg->pictureLabel = MakeControl(dlg, L"STATIC", L"Episode picture", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, -1);
        dlg->mapLabel = MakeControl(dlg, L"STATIC", L"Map", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, -1);
        dlg->levelTextLabel = MakeControl(dlg, L"STATIC", L"Level text", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, -1);
        dlg->valueEdit = MakeControl(dlg, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
            WS_EX_CLIENTEDGE, IDC_SCRIPT_VALUE_EDIT);
        dlg->valueCombo = MakeControl(dlg, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, IDC_SCRIPT_VALUE_COMBO);
        dlg->mapCombo = MakeControl(dlg, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, IDC_SCRIPT_MAP_COMBO);
        dlg->helpLabel = MakeControl(dlg, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, -1);

        dlg->addPicture = MakeControl(dlg, L"BUTTON", L"Add Episode", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, IDC_SCRIPT_ADD_PICTURE);
        dlg->addPlayMap = MakeControl(dlg, L"BUTTON", L"Add Level", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, IDC_SCRIPT_ADD_PLAYMAP);
        dlg->addEndGame = MakeControl(dlg, L"BUTTON", L"End Game", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, IDC_SCRIPT_ADD_DONE);
        dlg->addRaw = MakeControl(dlg, L"BUTTON", L"Add Raw", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, IDC_SCRIPT_ADD_RAW);
        dlg->moveUp = MakeControl(dlg, L"BUTTON", L"Up", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, IDC_SCRIPT_MOVE_UP);
        dlg->moveDown = MakeControl(dlg, L"BUTTON", L"Down", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, IDC_SCRIPT_MOVE_DOWN);
        dlg->deleteBlock = MakeControl(dlg, L"BUTTON", L"Delete", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, IDC_SCRIPT_DELETE);
        dlg->openRaw = MakeControl(dlg, L"BUTTON", L"Open Raw", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, IDC_SCRIPT_OPEN_RAW);
        dlg->save = MakeControl(dlg, L"BUTTON", L"Save Script", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, IDC_SCRIPT_SAVE);
        dlg->close = MakeControl(dlg, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, IDC_SCRIPT_CLOSE);

        ApplyDarkThemeToChild(dlg->blockList);
        SendMessageW(dlg->blockList, LB_SETITEMHEIGHT, 0, 54);
        ApplyDarkThemeToChild(dlg->typeCombo);
        ApplyDarkThemeToChild(dlg->valueEdit);
        ApplyDarkThemeToChild(dlg->valueCombo);
        ApplyDarkThemeToChild(dlg->mapCombo);

        FillTypeCombo(dlg->typeCombo);
    }

    void InitializeScriptEditorData(ScriptEditorDialog* dlg)
    {
        dlg->pictureNames = ListAssetCommandNames(g_profile.picturesDir, true);
        dlg->mapNames = ListAssetCommandNames(g_profile.mapsDir, false);
        dlg->mapNames.erase(std::remove_if(dlg->mapNames.begin(), dlg->mapNames.end(), [](const std::wstring& name) {
            return StartsWith(Lower(name), L"com_");
        }), dlg->mapNames.end());
        dlg->musicNames = ListAssetCommandNames(g_profile.musicDir, false);
        dlg->tileValues = TileValuesForTextureDir(g_profile.textureDir);

        std::wstring error;
        if (!LoadScriptBlocks(dlg->scriptFile, dlg->blocks, error))
        {
            DarkMessageBox(dlg->owner, error, L"Campaign Script", MB_OK | MB_ICONERROR);
        }

        dlg->selectedIndex = -1;
        std::wstringstream status;
        status << L"Script: " << dlg->scriptFile.wstring();
        if (IsCrM2File(dlg->scriptFile))
            status << L"    CrM2 decrunched for editing";
        status << L"    Blocks: " << dlg->blocks.size();
        status << L"    Profile: " << GameTypeName(g_profile.type);
        status << L"    " << TileRangeSummary(g_profile.textureDir);
        SetWindowTextW(dlg->statusLabel, status.str().c_str());
        RefreshBlockList(dlg);
        SendMessageW(dlg->blockList, LB_SETTOPINDEX, 0, 0);
        FillSelectionEditor(dlg);
    }

    LRESULT HandleDarkCtlColor(WPARAM wParam)
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, kText);
        SetBkColor(hdc, kBg);
        static HBRUSH bgBrush = CreateSolidBrush(kBg);
        static HBRUSH editBrush = CreateSolidBrush(kEditBg);
        HWND ctl = WindowFromDC(hdc);
        wchar_t className[32]{};
        if (ctl)
            GetClassNameW(ctl, className, 32);
        if (_wcsicmp(className, L"Edit") == 0 || _wcsicmp(className, L"ListBox") == 0 || _wcsicmp(className, L"ComboBox") == 0)
        {
            SetBkColor(hdc, kEditBg);
            return reinterpret_cast<LRESULT>(editBrush);
        }
        return reinterpret_cast<LRESULT>(bgBrush);
    }

    LRESULT CALLBACK ScriptEditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        ScriptEditorDialog* dlg = reinterpret_cast<ScriptEditorDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (msg)
        {
        case WM_NCCREATE:
        {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            dlg = reinterpret_cast<ScriptEditorDialog*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dlg));
            dlg->hwnd = hwnd;
            return TRUE;
        }
        case WM_CREATE:
            if (dlg)
            {
                ApplyDarkFrame(hwnd);
                CreateScriptEditorControls(dlg);
                InitializeScriptEditorData(dlg);
                LayoutScriptEditor(dlg);
            }
            return 0;

        case WM_SIZE:
            if (dlg)
                LayoutScriptEditor(dlg);
            return 0;

        case WM_ERASEBKGND:
        {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            HBRUSH brush = CreateSolidBrush(kBg);
            FillRect(hdc, &rc, brush);
            DeleteObject(brush);
            return 1;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORBTN:
            return HandleDarkCtlColor(wParam);

        case WM_MEASUREITEM:
        {
            auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (measure && measure->CtlID == IDC_SCRIPT_BLOCK_LIST)
            {
                measure->itemHeight = 54;
                return TRUE;
            }
            break;
        }

        case WM_DRAWITEM:
        {
            auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (DrawScriptCard(dlg, draw))
                return TRUE;
            return DrawDarkPopupButton(draw) ? TRUE : FALSE;
        }

        case WM_COMMAND:
            if (!dlg)
                break;
            switch (LOWORD(wParam))
            {
            case IDC_SCRIPT_BLOCK_LIST:
                if (HIWORD(wParam) == LBN_SELCHANGE && !dlg->updating)
                {
                    ApplySelection(dlg);
                    const int visibleIndex = static_cast<int>(SendMessageW(dlg->blockList, LB_GETCURSEL, 0, 0));
                    if (visibleIndex >= 0 && visibleIndex < static_cast<int>(dlg->visibleBlockIndices.size()))
                        dlg->selectedIndex = dlg->visibleBlockIndices[visibleIndex];
                    else
                        dlg->selectedIndex = -1;
                    FillSelectionEditor(dlg);
                }
                return 0;

            case IDC_SCRIPT_TYPE_COMBO:
                if (HIWORD(wParam) == CBN_SELCHANGE)
                {
                    UseKnownValue(dlg);
                }
                return 0;

            case IDC_SCRIPT_VALUE_COMBO:
                if (HIWORD(wParam) == CBN_SELCHANGE)
                {
                    UseKnownValue(dlg);
                }
                return 0;

            case IDC_SCRIPT_MAP_COMBO:
                if (HIWORD(wParam) == CBN_SELCHANGE)
                {
                    UseKnownValue(dlg);
                }
                return 0;

            case IDC_SCRIPT_VALUE_EDIT:
                if (HIWORD(wParam) == EN_CHANGE && !dlg->updating)
                {
                    ApplySelection(dlg);
                }
                return 0;

            case IDC_SCRIPT_ADD_PICTURE: AddEpisodeSequence(dlg); return 0;
            case IDC_SCRIPT_ADD_PLAYMAP: AddLevelSequence(dlg); return 0;
            case IDC_SCRIPT_ADD_DONE: AddEndGameBlock(dlg); return 0;
            case IDC_SCRIPT_ADD_RAW: AddBlock(dlg, ScriptBlockType::Raw); return 0;
            case IDC_SCRIPT_MOVE_UP: MoveSelectedBlock(dlg, -1); return 0;
            case IDC_SCRIPT_MOVE_DOWN: MoveSelectedBlock(dlg, 1); return 0;
            case IDC_SCRIPT_DELETE: DeleteSelectedBlock(dlg); return 0;
            case IDC_SCRIPT_SAVE: SaveScriptEditor(dlg); return 0;
            case IDC_SCRIPT_OPEN_RAW:
                ShellExecuteW(hwnd, L"open", dlg->scriptFile.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                return 0;
            case IDC_SCRIPT_CLOSE:
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            break;

        case WM_CLOSE:
            if (!dlg || ConfirmCloseScriptEditor(dlg))
                DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void ShowScriptEditor(HWND owner)
    {
        if (!EnsureProfile(owner))
            return;

        if (!FileExists(g_profile.scriptFile))
        {
            std::wstring msg = L"Campaign script not found:\n" + g_profile.scriptFile.wstring();
            DarkMessageBox(owner, msg, L"Campaign Script", MB_OK | MB_ICONWARNING);
            return;
        }

        static bool registered = false;
        const wchar_t* className = L"ZGloomCampaignScriptEditorWindow";
        if (!registered)
        {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = className;
            wc.lpfnWndProc = ScriptEditorWndProc;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = CreateSolidBrush(kBg);
            RegisterClassExW(&wc);
            registered = true;
        }

        RECT ownerRect{};
        GetWindowRect(owner, &ownerRect);
        const int width = 1000;
        const int height = 650;
        const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
        const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;

        ScriptEditorDialog dlg;
        dlg.owner = owner;
        dlg.scriptFile = g_profile.scriptFile;

        HWND dialog = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
            className,
            L"Campaign Script Editor",
            WS_CAPTION | WS_SYSMENU | WS_POPUP,
            x, y, width, height,
            owner, nullptr, GetModuleHandleW(nullptr), &dlg);
        if (!dialog)
        {
            DarkMessageBox(owner, L"Could not create the Campaign Script Editor window.", L"Campaign Script", MB_OK | MB_ICONERROR);
            return;
        }

        EnableWindow(owner, FALSE);
        ShowWindow(dialog, SW_SHOW);
        UpdateWindow(dialog);

        MSG msg{};
        while (IsWindow(dialog) && GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            if (!IsDialogMessageW(dialog, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }

    std::wstring TrimText(std::wstring value)
    {
        while (!value.empty() && std::iswspace(value.front()))
            value.erase(value.begin());
        while (!value.empty() && std::iswspace(value.back()))
            value.pop_back();
        return value;
    }

    bool IsDirectoryEmpty(const fs::path& path)
    {
        std::error_code ec;
        if (!DirExists(path))
            return true;
        return fs::directory_iterator(path, ec) == fs::directory_iterator();
    }

    bool IsValidCampaignFolderName(const std::wstring& name, std::wstring& error)
    {
        if (name.empty())
        {
            error = L"Please enter a campaign folder name.";
            return false;
        }

        static const wchar_t* forbidden = L"\\/:*?\"<>|";
        if (name.find_first_of(forbidden) != std::wstring::npos)
        {
            error = L"Campaign folder name contains invalid filename characters.";
            return false;
        }

        if (name.back() == L'.' || name.back() == L' ')
        {
            error = L"Campaign folder name must not end with a dot or space.";
            return false;
        }

        return true;
    }

    CampaignTemplateKind SelectedTemplateKind(const NewCampaignDialog* dlg)
    {
        if (!dlg || !dlg->templateCombo)
            return CampaignTemplateKind::GloomFamily;
        const LRESULT sel = SendMessageW(dlg->templateCombo, CB_GETCURSEL, 0, 0);
        if (sel == 1)
            return CampaignTemplateKind::Gloom3;
        if (sel == 2)
            return CampaignTemplateKind::ZombieMassacre;
        return CampaignTemplateKind::GloomFamily;
    }

    std::wstring TemplateKindName(CampaignTemplateKind kind)
    {
        switch (kind)
        {
        case CampaignTemplateKind::Gloom3: return L"Gloom 3";
        case CampaignTemplateKind::ZombieMassacre: return L"Zombie Massacre";
        default: return L"Gloom / Gloom Deluxe";
        }
    }

    bool IsTemplateRootPlausible(const fs::path& root, CampaignTemplateKind kind, std::wstring& warning)
    {
        warning.clear();
        if (!DirExists(root))
        {
            warning = L"Template root folder does not exist.";
            return false;
        }

        const bool zombieLike = DirExists(root / L"pixs") || DirExists(root / L"char") || DirExists(root / L"musi") || DirExists(root / L"lvls") || DirExists(root / L"stuf");
        const bool gloomLike = DirExists(root / L"pics") || DirExists(root / L"objs") || DirExists(root / L"sfxs") || DirExists(root / L"maps") || DirExists(root / L"misc");

        if (kind == CampaignTemplateKind::ZombieMassacre)
        {
            if (!zombieLike)
            {
                warning = L"Selected template type is Zombie Massacre, but the folder does not look like a Zombie Massacre game root.";
                return false;
            }
            return true;
        }

        if (!gloomLike)
        {
            warning = L"Selected template type is Gloom based, but the folder does not look like a Gloom game root.";
            return false;
        }
        return true;
    }

    bool CopyDirectoryTreeIfPresent(const fs::path& src, const fs::path& dst, std::wstringstream& warnings)
    {
        std::error_code ec;
        fs::create_directories(dst, ec);
        if (ec)
        {
            warnings << L"Could not create folder: " << dst.wstring() << L"\n";
            return false;
        }

        if (!DirExists(src))
        {
            warnings << L"Template folder missing, created empty folder instead: " << src.filename().wstring() << L"\n";
            return true;
        }

        fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            warnings << L"Could not copy " << src.wstring() << L" -> " << dst.wstring() << L"\n  " << ToWideAcp(ec.message()) << L"\n";
            return false;
        }
        return true;
    }

    std::wstring PickDefaultIntermissionPicture(const fs::path& pictureDir)
    {
        std::vector<std::wstring> names = ListAssetCommandNames(pictureDir, true);
        if (names.empty())
            return L"";

        auto findName = [&](const wchar_t* wanted) -> std::wstring {
            for (const std::wstring& name : names)
            {
                if (_wcsicmp(name.c_str(), wanted) == 0)
                    return name;
            }
            return L"";
        };

        const wchar_t* preferred[] = { L"intro", L"spacehulk", L"words1", L"gothic", L"hell", L"theend", L"title", L"blackmagic" };
        for (const wchar_t* wanted : preferred)
        {
            std::wstring found = findName(wanted);
            if (!found.empty())
                return found;
        }

        for (const std::wstring& name : names)
        {
            const std::wstring lower = Lower(name);
            if (lower == L"gloom" || lower == L"gloombrush")
                continue;
            return name;
        }
        return names.front();
    }

    bool WriteMinimalCampaignScript(const fs::path& scriptFile, const std::wstring& pictureName, std::wstringstream& warnings)
    {
        std::error_code ec;
        fs::create_directories(scriptFile.parent_path(), ec);
        if (ec)
        {
            warnings << L"Could not create script folder: " << scriptFile.parent_path().wstring() << L"\n";
            return false;
        }

        std::ofstream out(scriptFile, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            warnings << L"Could not write script file: " << scriptFile.wstring() << L"\n";
            return false;
        }

        out << ";script for gloom game\n";
        if (!pictureName.empty())
            out << "pict_" << ToAcp(pictureName) << "\n";
        out << "tile_1\n";
        out << "draw_\n";
        out << "show_\n";
        out << "text_welcome to your new gloom campaign\n";
        out << "wait_\n";
        out << "dark_\n";
        out << "done_\n";
        return !!out;
    }

    bool CreateCampaignFromTemplate(HWND owner, CampaignTemplateKind kind, const fs::path& templateRoot, const fs::path& targetRoot, std::wstring& outMessage)
    {
        std::wstringstream warnings;
        std::error_code ec;

        if (fs::equivalent(templateRoot, targetRoot, ec))
        {
            outMessage = L"Target campaign folder must be different from the template root.";
            return false;
        }
        ec.clear();

        if (DirExists(targetRoot) && !IsDirectoryEmpty(targetRoot))
        {
            outMessage = L"Target campaign folder already exists and is not empty:\n" + targetRoot.wstring();
            return false;
        }

        fs::create_directories(targetRoot, ec);
        if (ec)
        {
            outMessage = L"Could not create target campaign folder:\n" + targetRoot.wstring() + L"\n\n" + ToWideAcp(ec.message());
            return false;
        }

        bool ok = true;
        fs::path pictureDir;
        fs::path scriptFile;

        if (kind == CampaignTemplateKind::ZombieMassacre)
        {
            ok = CopyDirectoryTreeIfPresent(templateRoot / L"pixs", targetRoot / L"pixs", warnings) && ok;
            ok = CopyDirectoryTreeIfPresent(templateRoot / L"stuf", targetRoot / L"stuf", warnings) && ok;
            ok = CopyDirectoryTreeIfPresent(templateRoot / L"txts", targetRoot / L"txts", warnings) && ok;
            ok = CopyDirectoryTreeIfPresent(templateRoot / L"char", targetRoot / L"char", warnings) && ok;
            ok = CopyDirectoryTreeIfPresent(templateRoot / L"musi", targetRoot / L"musi", warnings) && ok;
            fs::create_directories(targetRoot / L"lvls", ec);
            if (ec)
            {
                warnings << L"Could not create lvls folder.\n";
                ok = false;
                ec.clear();
            }
            pictureDir = targetRoot / L"pixs";
            scriptFile = targetRoot / L"stuf" / L"stages";
        }
        else
        {
            ok = CopyDirectoryTreeIfPresent(templateRoot / L"pics", targetRoot / L"pics", warnings) && ok;
            ok = CopyDirectoryTreeIfPresent(templateRoot / L"misc", targetRoot / L"misc", warnings) && ok;
            ok = CopyDirectoryTreeIfPresent(templateRoot / L"txts", targetRoot / L"txts", warnings) && ok;
            ok = CopyDirectoryTreeIfPresent(templateRoot / L"objs", targetRoot / L"objs", warnings) && ok;
            ok = CopyDirectoryTreeIfPresent(templateRoot / L"sfxs", targetRoot / L"sfxs", warnings) && ok;
            fs::create_directories(targetRoot / L"maps", ec);
            if (ec)
            {
                warnings << L"Could not create maps folder.\n";
                ok = false;
                ec.clear();
            }
            pictureDir = targetRoot / L"pics";
            scriptFile = targetRoot / L"misc" / L"script";
        }

        const std::wstring defaultPicture = PickDefaultIntermissionPicture(pictureDir);
        if (!WriteMinimalCampaignScript(scriptFile, defaultPicture, warnings))
            ok = false;

        std::wstringstream msg;
        msg << L"Campaign created:\n" << targetRoot.wstring() << L"\n\n";
        msg << L"Template: " << TemplateKindName(kind) << L"\n";
        msg << L"Maps folder is empty by design. Create maps with the Level Editor.\n";
        if (!defaultPicture.empty())
            msg << L"Initial intermission picture: " << defaultPicture << L"\n";
        if (!warnings.str().empty())
            msg << L"\nWarnings:\n" << warnings.str();

        outMessage = msg.str();
        return ok;
    }

    void SetNewCampaignInfo(NewCampaignDialog* dlg)
    {
        if (!dlg || !dlg->info)
            return;

        const CampaignTemplateKind kind = SelectedTemplateKind(dlg);
        std::wstringstream info;
        info << L"Creates a new campaign folder from a template game.\r\n\r\n";
        info << L"Template: " << TemplateKindName(kind) << L"\r\n";
        if (kind == CampaignTemplateKind::ZombieMassacre)
        {
            info << L"Folders: lvls, pixs, stuf, txts, char, musi\r\n";
            info << L"Generated script: stuf\\stages\r\n";
        }
        else
        {
            info << L"Folders: maps, pics, misc, txts, objs, sfxs\r\n";
            info << L"Generated script: misc\\script\r\n";
        }
        info << L"\r\nMusic/SFX, objects/characters, textures and pictures are copied from the template.\r\n";
        info << L"Maps are intentionally left empty.";
        SetWindowTextW(dlg->info, info.str().c_str());
    }

    void LayoutNewCampaignDialog(NewCampaignDialog* dlg)
    {
        if (!dlg || !dlg->hwnd)
            return;

        RECT rc{};
        GetClientRect(dlg->hwnd, &rc);
        const int margin = 18;
        const int labelW = 130;
        const int editW = (rc.right - rc.left) - margin * 2 - labelW - 94;
        const int browseW = 82;
        const int rowH = 24;
        int y = 18;

        auto move = [](HWND h, int x, int y, int w, int hgt) {
            if (h) MoveWindow(h, x, y, w, hgt, TRUE);
        };
        // Labels are created once in CreateNewCampaignControls; this layout only moves input controls.
        move(dlg->templateCombo, margin + labelW, y, editW + browseW, 160); y += 38;
        move(dlg->templateRootEdit, margin + labelW, y, editW, rowH);
        move(dlg->templateBrowse, margin + labelW + editW + 8, y - 1, browseW, 26); y += 38;
        move(dlg->targetParentEdit, margin + labelW, y, editW, rowH);
        move(dlg->targetBrowse, margin + labelW + editW + 8, y - 1, browseW, 26); y += 38;
        move(dlg->campaignNameEdit, margin + labelW, y, editW + browseW, rowH); y += 42;
        move(dlg->info, margin, y, rc.right - margin * 2, 118); y = rc.bottom - 48;
        move(dlg->create, rc.right - margin - 200, y, 94, 28);
        move(dlg->cancel, rc.right - margin - 98, y, 94, 28);
    }

    void CreateNewCampaignControls(NewCampaignDialog* dlg)
    {
        dlg->font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const int margin = 18;
        const int labelW = 130;
        auto label = [&](const wchar_t* text, int y) {
            HWND h = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT, margin, y + 4, labelW, 20, dlg->hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(dlg->font), TRUE);
        };

        int y = 18;
        label(L"Template", y);
        dlg->templateCombo = CreateWindowW(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 0, 0, dlg->hwnd, reinterpret_cast<HMENU>(IDC_NEW_CAMPAIGN_TEMPLATE), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(dlg->templateCombo, WM_SETFONT, reinterpret_cast<WPARAM>(dlg->font), TRUE);
        SendMessageW(dlg->templateCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Gloom / Gloom Deluxe"));
        SendMessageW(dlg->templateCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Gloom 3"));
        SendMessageW(dlg->templateCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Zombie Massacre"));
        SendMessageW(dlg->templateCombo, CB_SETCURSEL, 0, 0);
        y += 38;

        label(L"Template root", y);
        dlg->templateRootEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, dlg->hwnd, reinterpret_cast<HMENU>(IDC_NEW_CAMPAIGN_TEMPLATE_ROOT), GetModuleHandleW(nullptr), nullptr);
        dlg->templateBrowse = CreateWindowW(L"BUTTON", L"Browse", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, dlg->hwnd, reinterpret_cast<HMENU>(IDC_NEW_CAMPAIGN_BROWSE_TEMPLATE), GetModuleHandleW(nullptr), nullptr);
        y += 38;

        label(L"Target parent", y);
        dlg->targetParentEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, dlg->hwnd, reinterpret_cast<HMENU>(IDC_NEW_CAMPAIGN_TARGET_PARENT), GetModuleHandleW(nullptr), nullptr);
        dlg->targetBrowse = CreateWindowW(L"BUTTON", L"Browse", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, dlg->hwnd, reinterpret_cast<HMENU>(IDC_NEW_CAMPAIGN_BROWSE_TARGET), GetModuleHandleW(nullptr), nullptr);
        y += 38;

        label(L"Campaign name", y);
        dlg->campaignNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"MyGloomCampaign", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, dlg->hwnd, reinterpret_cast<HMENU>(IDC_NEW_CAMPAIGN_NAME), GetModuleHandleW(nullptr), nullptr);

        dlg->info = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0, 0, 0, dlg->hwnd, reinterpret_cast<HMENU>(IDC_NEW_CAMPAIGN_INFO), GetModuleHandleW(nullptr), nullptr);
        dlg->create = CreateWindowW(L"BUTTON", L"Create", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, dlg->hwnd, reinterpret_cast<HMENU>(IDC_NEW_CAMPAIGN_CREATE), GetModuleHandleW(nullptr), nullptr);
        dlg->cancel = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, dlg->hwnd, reinterpret_cast<HMENU>(IDC_NEW_CAMPAIGN_CANCEL), GetModuleHandleW(nullptr), nullptr);

        HWND controls[] = { dlg->templateRootEdit, dlg->templateBrowse, dlg->targetParentEdit, dlg->targetBrowse, dlg->campaignNameEdit, dlg->info, dlg->create, dlg->cancel };
        for (HWND h : controls)
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(dlg->font), TRUE);

        if (!g_profile.root.empty())
        {
            SetWindowTextW(dlg->templateRootEdit, g_profile.root.wstring().c_str());
            if (!g_profile.root.parent_path().empty())
                SetWindowTextW(dlg->targetParentEdit, g_profile.root.parent_path().wstring().c_str());
            if (g_profile.type == GameType::ZombieMassacre)
                SendMessageW(dlg->templateCombo, CB_SETCURSEL, 2, 0);
            else if (g_profile.type == GameType::Gloom3)
                SendMessageW(dlg->templateCombo, CB_SETCURSEL, 1, 0);
        }

        SetNewCampaignInfo(dlg);
        LayoutNewCampaignDialog(dlg);
    }

    bool TryCreateNewCampaign(NewCampaignDialog* dlg)
    {
        if (!dlg)
            return false;

        const CampaignTemplateKind kind = SelectedTemplateKind(dlg);
        const fs::path templateRoot = TrimText(GetWindowTextString(dlg->templateRootEdit));
        const fs::path targetParent = TrimText(GetWindowTextString(dlg->targetParentEdit));
        const std::wstring campaignName = TrimText(GetWindowTextString(dlg->campaignNameEdit));

        std::wstring error;
        if (!IsValidCampaignFolderName(campaignName, error))
        {
            DarkMessageBox(dlg->hwnd, error, L"New Campaign", MB_OK | MB_ICONWARNING);
            return false;
        }

        std::wstring templateWarning;
        if (!IsTemplateRootPlausible(templateRoot, kind, templateWarning))
        {
            DarkMessageBox(dlg->hwnd, templateWarning, L"New Campaign", MB_OK | MB_ICONWARNING);
            return false;
        }

        if (!DirExists(targetParent))
        {
            DarkMessageBox(dlg->hwnd, L"Target parent folder does not exist.", L"New Campaign", MB_OK | MB_ICONWARNING);
            return false;
        }

        const fs::path targetRoot = targetParent / campaignName;
        std::wstring message;
        if (!CreateCampaignFromTemplate(dlg->hwnd, kind, templateRoot, targetRoot, message))
        {
            DarkMessageBox(dlg->hwnd, message, L"New Campaign", MB_OK | MB_ICONERROR);
            return false;
        }

        DarkMessageBox(dlg->hwnd, message, L"New Campaign", MB_OK | MB_ICONINFORMATION);
        SelectCampaignGameRoot(dlg->hwnd, targetRoot);
        return true;
    }

    LRESULT CALLBACK NewCampaignWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        NewCampaignDialog* dlg = reinterpret_cast<NewCampaignDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (msg)
        {
        case WM_NCCREATE:
        {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            dlg = reinterpret_cast<NewCampaignDialog*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dlg));
            dlg->hwnd = hwnd;
            return TRUE;
        }
        case WM_CREATE:
            if (dlg)
            {
                ApplyDarkFrame(hwnd);
                CreateNewCampaignControls(dlg);
            }
            return 0;
        case WM_SIZE:
            if (dlg)
                LayoutNewCampaignDialog(dlg);
            return 0;
        case WM_ERASEBKGND:
        {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            HBRUSH brush = CreateSolidBrush(kBg);
            FillRect(hdc, &rc, brush);
            DeleteObject(brush);
            return 1;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORBTN:
            return HandleDarkCtlColor(wParam);
        case WM_DRAWITEM:
            return DrawDarkPopupButton(reinterpret_cast<DRAWITEMSTRUCT*>(lParam)) ? TRUE : FALSE;
        case WM_COMMAND:
            if (!dlg)
                break;
            switch (LOWORD(wParam))
            {
            case IDC_NEW_CAMPAIGN_TEMPLATE:
                if (HIWORD(wParam) == CBN_SELCHANGE)
                    SetNewCampaignInfo(dlg);
                return 0;
            case IDC_NEW_CAMPAIGN_BROWSE_TEMPLATE:
            {
                std::wstring folder;
                if (BrowseFolderWithTitle(hwnd, L"Select template game root", folder))
                {
                    SetWindowTextW(dlg->templateRootEdit, folder.c_str());
                    fs::path parent = fs::path(folder).parent_path();
                    if (!parent.empty() && GetWindowTextLengthW(dlg->targetParentEdit) == 0)
                        SetWindowTextW(dlg->targetParentEdit, parent.wstring().c_str());
                }
                return 0;
            }
            case IDC_NEW_CAMPAIGN_BROWSE_TARGET:
            {
                std::wstring folder;
                if (BrowseFolderWithTitle(hwnd, L"Select target parent folder", folder))
                    SetWindowTextW(dlg->targetParentEdit, folder.c_str());
                return 0;
            }
            case IDC_NEW_CAMPAIGN_CREATE:
                if (TryCreateNewCampaign(dlg))
                {
                    dlg->result = true;
                    DestroyWindow(hwnd);
                }
                return 0;
            case IDC_NEW_CAMPAIGN_CANCEL:
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (dlg)
                dlg->done = true;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void NewCampaign(HWND owner)
    {
        static bool registered = false;
        const wchar_t* className = L"ZGloomNewCampaignWindow";
        if (!registered)
        {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = className;
            wc.lpfnWndProc = NewCampaignWndProc;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = CreateSolidBrush(kBg);
            RegisterClassExW(&wc);
            registered = true;
        }

        RECT ownerRect{};
        GetWindowRect(owner, &ownerRect);
        const int width = 760;
        const int height = 390;
        const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
        const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;

        NewCampaignDialog dlg;
        dlg.owner = owner;

        HWND dialog = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
            className,
            L"New Campaign",
            WS_CAPTION | WS_SYSMENU | WS_POPUP,
            x, y, width, height,
            owner, nullptr, GetModuleHandleW(nullptr), &dlg);
        if (!dialog)
        {
            DarkMessageBox(owner, L"Could not create the New Campaign window.", L"New Campaign", MB_OK | MB_ICONERROR);
            return;
        }

        EnableWindow(owner, FALSE);
        ShowWindow(dialog, SW_SHOW);
        UpdateWindow(dialog);

        MSG msg{};
        while (IsWindow(dialog) && GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            if (!IsDialogMessageW(dialog, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }

    void OpenGameRoot(HWND owner)
    {
        std::wstring folder;
        if (!BrowseFolder(owner, folder))
            return;

        SelectCampaignGameRoot(owner, folder);
    }

    void OpenRecentGameRoot(HWND owner, int recentIndex)
    {
        if (recentIndex < 0 || recentIndex >= kRecentCampaignGameRootCount)
            return;

        const auto roots = LoadRecentCampaignGameRoots();
        const std::wstring& root = roots[static_cast<size_t>(recentIndex)];
        if (root.empty())
        {
            DarkMessageBox(owner, L"This recent campaign game root slot is empty.", L"Campaign", MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (!DirExists(fs::path(root)))
        {
            DarkMessageBox(owner, L"Recent campaign game root not found:\n" + root, L"Campaign", MB_OK | MB_ICONWARNING);
            return;
        }

        SelectCampaignGameRoot(owner, root);
    }

    void CampaignScript(HWND owner)
    {
        ShowScriptEditor(owner);
    }

    void TitleScreen(HWND owner)
    {
        ShowPictureManager(owner, true);
    }

    void IntermissionScreens(HWND owner)
    {
        ShowPictureManager(owner, false);
    }

    void TextureAssets(HWND owner)
    {
        ShowTextureManager(owner);
    }

    void ExportGamePackage(HWND owner)
    {
        if (!EnsureProfile(owner))
            return;

        std::wstringstream msg;
        msg << L"Export Game Package is reserved for a later phase.\n\n";
        msg << L"Current detected structure:\n" << ProfileSummary(g_profile) << L"\n";
        msg << L"Phase 1 avoids rewriting full game packages until script and picture workflows are proven safe.";
        DarkMessageBox(owner, msg.str(), L"Export Game Package", MB_OK | MB_ICONINFORMATION);
    }

    void CampaignSettings(HWND owner)
    {
        if (!EnsureProfile(owner))
            return;

        std::wstringstream msg;
        msg << ProfileSummary(g_profile) << L"\n";
        msg << L"Script editor: internal block editor enabled\n";
        msg << L"Write mode: raw/uncompressed picture files only\n";
        msg << L"CrM2 read/decrunch: enabled for analysis\n";
        msg << L"CrM2 repack: disabled\n";
        msg << L"Menu ellipses: disabled";
        DarkMessageBox(owner, msg.str(), L"Campaign Settings", MB_OK | MB_ICONINFORMATION);
    }
}

namespace CampaignEditor
{
    bool HandleCommand(HWND owner, unsigned int commandId)
    {
        if (commandId >= kRecentCampaignGameRootBaseId && commandId < kRecentCampaignGameRootBaseId + kRecentCampaignGameRootCount)
        {
            OpenRecentGameRoot(owner, static_cast<int>(commandId - kRecentCampaignGameRootBaseId));
            return true;
        }

        switch (commandId)
        {
        case IDM_CAMPAIGN_OPEN_GAME_ROOT:
            OpenGameRoot(owner);
            return true;
        case IDM_CAMPAIGN_SCRIPT:
            CampaignScript(owner);
            return true;
        case IDM_CAMPAIGN_TITLE_SCREEN:
            TitleScreen(owner);
            return true;
        case IDM_CAMPAIGN_INTERMISSION_SCREENS:
            IntermissionScreens(owner);
            return true;
        case IDM_CAMPAIGN_TITLE_MUSIC:
            TextureAssets(owner);
            return true;
        case IDM_CAMPAIGN_EXPORT_GAME_PACKAGE:
            NewCampaign(owner);
            return true;
        case IDM_CAMPAIGN_SETTINGS:
            CampaignSettings(owner);
            return true;
        default:
            return false;
        }
    }
}
