#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cwctype>
#include <cwchar>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <type_traits>
#include <utility>
#include <vector>

#include "MapFormat.h"
#include "decrunchmania.h"
#include "resource.h"
#include "CampaignEditor.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace
{
    const wchar_t* kMainWindowClass = L"ZGloomEditorMainWindow";
    const wchar_t* kCanvasClass = L"ZGloomEditorCanvas";
    const wchar_t* kInfoPanelClass = L"ZGloomEditorInfoPanel";
    constexpr int kGridStep = 256;
    constexpr int kGridSnapSubdivisions = 8;
    constexpr int kGridSnapStep = kGridStep / kGridSnapSubdivisions;
    constexpr double kGridPointSnapWorldRadius = static_cast<double>(kGridSnapStep) * 0.44;
    constexpr double kCanvasZoomMin = 0.25;
    constexpr double kCanvasZoomMax = 16.0;
    constexpr int kRecentFileCount = 5;
    constexpr int kRecentCampaignGameRootCount = 3;
    constexpr int kMaxUndoSteps = 20;
    constexpr double kCornerSnapWorldRadius = static_cast<double>(kGridSnapStep) * 0.44;
    constexpr double kCornerStrongSnapWorldRadius = static_cast<double>(kGridSnapStep) * 0.44;
    constexpr double kCornerWeldWorldRadius = 2.0;
    constexpr double kCornerClusterWeldWorldRadius = 2.0;
    constexpr double kEndpointExactWorldRadius = 2.0;
    constexpr double kTextureElementWorldLength = 256.0;
    constexpr double kWallTextureScaleUnitWorldLength = 64.0;
    constexpr int kRepeatWallScaleGameUnits = static_cast<int>(kTextureElementWorldLength / kWallTextureScaleUnitWorldLength);
    constexpr double kTextureLengthSnapWorldRadius = 40.0;
    constexpr int kWalkPreviewDragDirectionPixels = 24;
    constexpr int kWalkPreviewDirectionCount = 8;
    constexpr int kWalkPreviewRotationUnitsPerStep = 32; // 45 degrees in Gloom's 0..255 rotation scale.
    constexpr int kDefaultWallScale = 2;
    constexpr int kInitialEventIndex = 0;
    constexpr int kFirstAutoTriggerEventIndex = 1; // Event 1 is reserved for map-start/player/object commands.
    constexpr int kPlayer1ObjectType = 0;
    constexpr int kPlayer2ObjectType = 1;
    constexpr int kLevelEndEventValue = 24;
    constexpr UINT kRecentFileBaseId = 40100;
    constexpr UINT kRecentCampaignGameRootBaseId = 65100;
    constexpr UINT kCmdLinkEnemyObjects = 65010; // local toolbar/menu command; keeps resource.h unchanged
    constexpr UINT kCmdLinkMoveWallGroup = 65011; // local toolbar/menu command; keeps resource.h unchanged
    constexpr UINT kCmdToolbarScrollUp = 65012; // local toolbar scroll command; keeps resource.h unchanged
    constexpr UINT kCmdToolbarScrollDown = 65013; // local toolbar scroll command; keeps resource.h unchanged
    constexpr UINT kCmdViewEventGraphOverlay = 65014; // local view toggle; keeps resource.h unchanged

    enum class InsertMode
    {
        None,
        Wall,
        MonsterZone,
        EventTrigger,
        ObjectSpawn,
        PlayerStart,
        LevelEnd,
        LinkEventToZone,
        LinkEventToSwitchTexture,
        LinkEventToEnemyObject,
        DeleteLinkEventToEnemyObject,
        LinkEventToRotateClockwise,
        LinkEventToRotateCounterClockwise,
        LinkEventToMoveWallGroup,
        SetTeleportTarget,
    };

    enum class SurfaceTextureTarget
    {
        Floor,
        Ceiling,
    };

    enum class WallTextureMode
    {
        Stretch,
        Repeat1To1,
        Clip2Of8,
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

    struct ObjectPreviewImage
    {
        int objectType = -1;
        std::wstring name;
        std::wstring category;
        std::string loadedPath;
        std::string error;
        int frameIndex = 0;
        int frameCount = 0;
        int width = 0;
        int height = 0;
        std::vector<uint32_t> pixels;
    };

    enum class ObjectPlacementGroup
    {
        Enemy,
        Weapon,
        Pickup,
    };

    struct ObjectTypeInfo
    {
        int type = -1;
        const wchar_t* name = L"Object";
        const wchar_t* category = L"Object";
        const char* normalPath = nullptr;
        const char* zombiePath = nullptr;
        int defaultFrame = 0;
        bool rotatable = false;
    };

    struct OwnerDrawMenuItem
    {
        std::wstring text;
        UINT_PTR id = 0;
        bool topLevel = false;
        bool separator = false;
    };

    struct EditorSnapshot
    {
        mapfmt::MapDocument document;
        int selectedZone = -1;
    };

    struct MonsterSpawnSelection
    {
        int eventIndex = -1;
        int commandIndex = -1;
        int markerIndex = -1;

        bool IsSet() const
        {
            return eventIndex >= 0 && commandIndex >= 0;
        }

        void Clear()
        {
            eventIndex = -1;
            commandIndex = -1;
            markerIndex = -1;
        }
    };

    struct TeleportSelection
    {
        int eventIndex = -1;
        int commandIndex = -1;
        int markerIndex = -1;

        bool IsSet() const
        {
            return eventIndex >= 0 && commandIndex >= 0;
        }

        void Clear()
        {
            eventIndex = -1;
            commandIndex = -1;
            markerIndex = -1;
        }
    };

    enum class GameProfileKind
    {
        Unknown,
        Gloom,
        GloomDeluxe,
        Gloom3,
        ZombieMassacre,
    };

    struct GameProfile
    {
        GameProfileKind kind = GameProfileKind::Unknown;
        std::wstring title = L"Unknown Game";
        std::string rootPath;
        std::string mapFolder = "maps";
        std::string objectFolder = "objs";
        std::string imageFolder = "pics";
        std::string soundFolder = "sfxs";
        std::string scriptPath = "misc/script";

        bool IsKnown() const
        {
            return kind != GameProfileKind::Unknown;
        }
    };

    struct MapValidationReportSections
    {
        std::wstring profile;
        std::wstring intelligence;
        std::wstring checks;
        std::wstring suggestions;
        std::wstring technical;
        std::wstring full;
        std::vector<int> safeNeutralGuideZones;
        int okCount = 0;
        int infoCount = 0;
        int warnCount = 0;
        int saveWarnCount = 0;

        bool HasSafeRepairs() const
        {
            return !safeNeutralGuideZones.empty();
        }

        bool HasWarnings() const
        {
            return warnCount > 0;
        }

        bool HasSaveWarnings() const
        {
            return saveWarnCount > 0;
        }
    };

    struct AppState
    {
        HINSTANCE instance = nullptr;
        HWND mainWindow = nullptr;
        HWND zoneList = nullptr;
        HWND canvas = nullptr;
        HWND infoPanel = nullptr;
        HWND statusBar = nullptr;
        WNDPROC statusBarOldProc = nullptr;
        std::wstring statusText;
        HMENU fileMenu = nullptr;
        HMENU campaignMenu = nullptr;
        std::vector<std::unique_ptr<OwnerDrawMenuItem>> menuItems;
        HWND btnAddWall = nullptr;
        HWND btnAddMonster = nullptr;
        HWND btnAddTrigger = nullptr;
        HWND btnPlaceEnemy = nullptr;
        HWND btnPlaceWeapon = nullptr;
        HWND btnPlacePickup = nullptr;
        HWND btnEdit = nullptr;
        HWND btnDelete = nullptr;
        HWND btnUp = nullptr;
        HWND btnDown = nullptr;
        HWND btnEvents = nullptr;
        HWND btnTextures = nullptr;
        HWND btnPlayerStart = nullptr;
        HWND btnLevelEnd = nullptr;
        HWND btnLinkEvent = nullptr;
        HWND btnLinkSwitchTexture = nullptr;
        HWND btnLinkEnemyObjects = nullptr;
        HWND btnLinkRotateCW = nullptr;
        HWND btnLinkRotateCCW = nullptr;
        HWND btnLinkMoveWallGroup = nullptr;
        HWND btnDeleteLinkEvent = nullptr;
        HWND btnSetTeleportTarget = nullptr;
        HWND btnFlipDoorDirection = nullptr;
        HWND btnValidateMap = nullptr;
        HWND btnToolbarScrollUp = nullptr;
        HWND btnToolbarScrollDown = nullptr;
        HWND lineValidateTop = nullptr;
        int toolbarScrollY = 0;
        int toolbarScrollMaxY = 0;
        bool toolbarScrollMode = false;
        HWND lineZonesLeft = nullptr;
        HWND labelZones = nullptr;
        HWND lineZonesRight = nullptr;
        HWND lineObjectsLeft = nullptr;
        HWND labelObjects = nullptr;
        HWND lineObjectsRight = nullptr;
        HWND lineEventLinksLeft = nullptr;
        HWND labelEventLinks = nullptr;
        HWND lineEventLinksRight = nullptr;
        HWND lineOrderLeft = nullptr;
        HWND labelOrder = nullptr;
        HWND lineOrderRight = nullptr;
        HWND lineMapMarkersLeft = nullptr;
        HWND labelMapMarkers = nullptr;
        HWND lineMapMarkersRight = nullptr;
        mapfmt::MapDocument document;
        GameProfile gameProfile;
        bool showEventGraphOverlay = false;
        int selectedZone = -1;
        MonsterSpawnSelection selectedMonsterSpawn{};
        TeleportSelection selectedTeleportTarget{};
        int previewTextureBand = 0;
        int previewTextureIndex = 0;
        int previewTextureSlot = 0;
        int previewTextureStrip = 0;
        int activeWallTextureSlot = 0;
        int activeWallTextureStrip = 0;
        WallTextureMode activeWallTextureMode = WallTextureMode::Repeat1To1;
        int activeFloorTextureIndex = 0;
        int activeCeilingTextureIndex = 0;
        int activeFloorTextureChoice = 0;
        int activeCeilingTextureChoice = 0;
        std::vector<std::string> floorTextureNames;
        std::vector<std::string> ceilingTextureNames;
        SurfaceTextureTarget activeSurfaceTextureTarget = SurfaceTextureTarget::Floor;
        int previewScrollX = 0;
        bool previewScrollDragging = false;
        bool previewScrollHover = false;
        int previewScrollDragOffsetX = 0;
        int previewScrollDragStartMouseX = 0;
        int previewScrollDragStartScrollX = 0;
        int previewScrollDragThumbRange = 1;
        int previewScrollDragMaxScrollX = 0;
        int previewWheelRemainder = 0;
        bool previewAutoScrollPending = false;
        int placeObjectType = 10;
        int placeObjectEvent = 0;
        int linkEventTriggerZone = -1;
        int linkEventIndex = -1;
        int pendingSwitchTextureTriggerZone = -1;
        int pendingSwitchTextureEventIndex = -1;
        int pendingSwitchTextureIndex = -1;
        bool pendingSwitchTextureValid = false;
        int pendingMoveWallGroupSourceZone = -1;
        bool pendingMoveWallGroupTargetPlacementActive = false;
        int pendingMoveWallGroupGuideFirst = -1;
        int pendingMoveWallGroupGuideCount = 0;
        POINT pendingMoveWallGroupSourcePickWorld{};
        POINT pendingMoveWallGroupTargetPickWorld{};
        POINT pendingMoveWallGroupTargetOffset{};
        bool teleportTargetAwaitDirection = false;
        POINT teleportTargetWorld{};
        int teleportTargetRotation = 0;
        ObjectPreviewImage objectPreviewImage;
        std::unordered_map<std::string, ObjectPreviewImage> objectPreviewCache;
        InsertMode insertMode = InsertMode::None;
        bool isDrawing = false;
        bool isPanning = false;
        bool monsterSpawnDragging = false;
        bool monsterSpawnDragMoved = false;
        bool monsterSpawnDragSnapshotTaken = false;
        MonsterSpawnSelection monsterSpawnDragSelection{};
        POINT monsterSpawnDragStartClient{};
        POINT monsterSpawnDragStartWorld{};
        int monsterSpawnDragStartX = 0;
        int monsterSpawnDragStartZ = 0;
        bool drawWallAngleLock = false;
        bool drawWallLengthSnapLock = false;
        bool viewInitialized = false;
        double canvasZoom = 1.0;
        double canvasBaseScale = 0.25;
        double viewCenterX = 512.0;
        double viewCenterZ = 512.0;
        POINT drawStartWorld{};
        POINT drawCurrentWorld{};
        POINT panStartClient{};
        double panStartCenterX = 0.0;
        double panStartCenterZ = 0.0;
        std::array<std::string, kRecentFileCount> recentFiles{};
        std::string textureDataPath;
        bool walkPreviewInitialized = false;
        double walkPreviewX = 0.0;
        double walkPreviewZ = 0.0;
        int walkPreviewDir = 0;
        bool walkPreviewRightDrag = false;
        POINT walkPreviewRightDragStartClient{};
        POINT walkPreviewRightDragLastClient{};
        int walkPreviewRightDragBaseDir = 0;
        std::vector<EditorSnapshot> undoStack;
        TexturePreviewImage previewImage;
    };

    AppState g_app;

    constexpr bool kEditorDarkMode = true;
    constexpr COLORREF kDarkWindowBg = RGB(32, 32, 36);
    constexpr COLORREF kDarkPanelBg = RGB(25, 25, 28);
    constexpr COLORREF kDarkControlBg = RGB(44, 44, 52);
    constexpr COLORREF kDarkFieldBg = RGB(28, 28, 32);
    constexpr COLORREF kDarkText = RGB(232, 232, 236);
    constexpr COLORREF kDarkListText = RGB(221, 221, 221);
    constexpr COLORREF kDarkMutedText = RGB(130, 130, 140);
    constexpr COLORREF kDarkLinkText = RGB(105, 170, 255);
    constexpr COLORREF kDarkBorder = RGB(78, 78, 88);
    constexpr COLORREF kValidationOkText = RGB(92, 214, 132);
    constexpr COLORREF kValidationInfoText = RGB(246, 211, 84);
    constexpr COLORREF kValidationWarnText = RGB(255, 105, 105);

    HBRUSH DarkWindowBrush()
    {
        static HBRUSH brush = CreateSolidBrush(kDarkWindowBg);
        return brush;
    }

    HBRUSH DarkPanelBrush()
    {
        static HBRUSH brush = CreateSolidBrush(kDarkPanelBg);
        return brush;
    }

    HBRUSH DarkControlBrush()
    {
        static HBRUSH brush = CreateSolidBrush(kDarkControlBg);
        return brush;
    }

    HBRUSH DarkFieldBrush()
    {
        static HBRUSH brush = CreateSolidBrush(kDarkFieldBg);
        return brush;
    }

    HFONT NormalEditorListFont()
    {
        static HFONT font = []() -> HFONT
        {
            LOGFONTW lf{};
            if (GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof(lf), &lf) == 0)
            {
                lf.lfHeight = -12;
                lf.lfCharSet = DEFAULT_CHARSET;
                wcscpy_s(lf.lfFaceName, L"Segoe UI");
            }
            lf.lfWeight = FW_NORMAL;
            lf.lfItalic = FALSE;
            lf.lfUnderline = FALSE;
            return CreateFontIndirectW(&lf);
        }();
        return font ? font : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }

    void EnableEditorDarkModeForApp()
    {
        if (!kEditorDarkMode) return;
        HMODULE ux = LoadLibraryW(L"uxtheme.dll");
        if (!ux) return;

        using SetPreferredAppModeFn = int (WINAPI*)(int);
        using FlushMenuThemesFn = void (WINAPI*)();

        auto setPreferredAppMode = reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(ux, MAKEINTRESOURCEA(135)));
        if (setPreferredAppMode)
        {
            // 2 = ForceDark on supported Windows 10/11 builds. If unsupported, the call is simply ignored.
            setPreferredAppMode(2);
        }

        auto flushMenuThemes = reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(ux, MAKEINTRESOURCEA(136)));
        if (flushMenuThemes)
        {
            flushMenuThemes();
        }
    }

    void EnableEditorDarkModeForWindow(HWND hwnd)
    {
        if (!kEditorDarkMode || !hwnd) return;

        HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
        if (!ux) ux = LoadLibraryW(L"uxtheme.dll");
        if (ux)
        {
            using AllowDarkModeForWindowFn = BOOL (WINAPI*)(HWND, BOOL);
            auto allowDarkModeForWindow = reinterpret_cast<AllowDarkModeForWindowFn>(GetProcAddress(ux, MAKEINTRESOURCEA(133)));
            if (allowDarkModeForWindow)
            {
                allowDarkModeForWindow(hwnd, TRUE);
            }
        }

        BOOL useDark = TRUE;
        // Windows 10 20H1+/Windows 11. Attribute 19 is the older Insider/1903 fallback.
        DwmSetWindowAttribute(hwnd, 20, &useDark, sizeof(useDark));
        DwmSetWindowAttribute(hwnd, 19, &useDark, sizeof(useDark));

        SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
    }

    bool IsButtonClass(HWND hwnd)
    {
        wchar_t cls[64]{};
        GetClassNameW(hwnd, cls, static_cast<int>(ARRAYSIZE(cls)));
        return _wcsicmp(cls, L"Button") == 0;
    }

    void MakeButtonOwnerDraw(HWND hwnd)
    {
        if (!hwnd || !IsButtonClass(hwnd)) return;
        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        const LONG_PTR type = style & BS_TYPEMASK;
        if (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON || type == BS_OWNERDRAW)
        {
            style &= ~BS_TYPEMASK;
            style |= BS_OWNERDRAW;
            SetWindowLongPtrW(hwnd, GWL_STYLE, style);
            InvalidateRect(hwnd, nullptr, TRUE);
        }
    }

    bool DrawButtonLabelWithChevron(HDC hdc, RECT rc, const wchar_t* rawText, COLORREF textColor, bool pressed)
    {
        if (!hdc || !rawText) return false;

        std::wstring text = rawText;
        std::size_t pos = text.find(L" > ");
        std::size_t tokenLen = 3;
        if (pos == std::wstring::npos)
        {
            pos = text.find(L">");
            tokenLen = 1;
        }
        if (pos == std::wstring::npos)
        {
            return false;
        }

        auto trim = [](std::wstring s) -> std::wstring
        {
            while (!s.empty() && iswspace(s.front())) s.erase(s.begin());
            while (!s.empty() && iswspace(s.back())) s.pop_back();
            return s;
        };

        const std::wstring left = trim(text.substr(0, pos));
        const std::wstring right = trim(text.substr(pos + tokenLen));
        if (left.empty() || right.empty())
        {
            return false;
        }

        SIZE leftSz{};
        SIZE rightSz{};
        GetTextExtentPoint32W(hdc, left.c_str(), static_cast<int>(left.size()), &leftSz);
        GetTextExtentPoint32W(hdc, right.c_str(), static_cast<int>(right.size()), &rightSz);

        const int iconW = 7;
        const int iconH = 7;
        const int gap = 8;
        const int totalW = leftSz.cx + gap + iconW + gap + rightSz.cx;
        int totalH = (leftSz.cy > rightSz.cy) ? leftSz.cy : rightSz.cy;
        if (totalH < 12) totalH = 12;
        int startX = rc.left + ((rc.right - rc.left - totalW) / 2);
        int baseY = rc.top + ((rc.bottom - rc.top - totalH) / 2);
        if (pressed)
        {
            ++startX;
            ++baseY;
        }

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, textColor);
        TextOutW(hdc, startX, baseY + ((totalH - leftSz.cy) / 2), left.c_str(), static_cast<int>(left.size()));

        const int iconX = startX + leftSz.cx + gap;
        const int iconTop = baseY + ((totalH - iconH) / 2);
        POINT arrow[3] =
        {
            { iconX + 1, iconTop },
            { iconX + 1, iconTop + iconH },
            { iconX + 6, iconTop + (iconH / 2) }
        };
        HBRUSH arrowBrush = CreateSolidBrush(textColor);
        HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(hdc, arrowBrush));
        HPEN arrowPen = CreatePen(PS_SOLID, 1, textColor);
        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(hdc, arrowPen));
        Polygon(hdc, arrow, static_cast<int>(ARRAYSIZE(arrow)));
        if (oldPen) SelectObject(hdc, oldPen);
        if (oldBrush) SelectObject(hdc, oldBrush);
        DeleteObject(arrowPen);
        DeleteObject(arrowBrush);

        const int rightX = iconX + iconW + gap;
        TextOutW(hdc, rightX, baseY + ((totalH - rightSz.cy) / 2), right.c_str(), static_cast<int>(right.size()));
        return true;
    }

    bool DrawDarkButton(DRAWITEMSTRUCT* draw)
    {
        if (!draw || draw->CtlType != ODT_BUTTON || !draw->hwndItem) return false;

        RECT rc = draw->rcItem;
        HDC hdc = draw->hDC;
        const bool pressed = (draw->itemState & ODS_SELECTED) != 0;
        const bool disabled = (draw->itemState & ODS_DISABLED) != 0 || !IsWindowEnabled(draw->hwndItem);
        const bool focus = (draw->itemState & ODS_FOCUS) != 0;

        const COLORREF bg = disabled ? RGB(36, 36, 42) : (pressed ? RGB(54, 54, 64) : kDarkControlBg);
        const COLORREF border = focus ? RGB(105, 170, 255) : kDarkBorder;
        const COLORREF text = disabled ? RGB(118, 118, 128) : kDarkText;

        HBRUSH bgBrush = CreateSolidBrush(bg);
        FillRect(hdc, &rc, bgBrush);
        DeleteObject(bgBrush);

        HPEN pen = CreatePen(PS_SOLID, 1, border);
        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(hdc, pen));
        HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
        Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
        if (oldBrush) SelectObject(hdc, oldBrush);
        if (oldPen) SelectObject(hdc, oldPen);
        DeleteObject(pen);

        const int controlId = GetDlgCtrlID(draw->hwndItem);
        if (controlId == static_cast<int>(kCmdToolbarScrollUp) || controlId == static_cast<int>(kCmdToolbarScrollDown))
        {
            const bool up = controlId == static_cast<int>(kCmdToolbarScrollUp);
            RECT iconRc = rc;
            if (pressed) OffsetRect(&iconRc, 1, 1);
            const int cx = (iconRc.left + iconRc.right) / 2;
            const int cy = (iconRc.top + iconRc.bottom) / 2;
            POINT tri[3]{};
            if (up)
            {
                tri[0] = POINT{ cx, cy - 5 };
                tri[1] = POINT{ cx - 6, cy + 4 };
                tri[2] = POINT{ cx + 6, cy + 4 };
            }
            else
            {
                tri[0] = POINT{ cx - 6, cy - 4 };
                tri[1] = POINT{ cx + 6, cy - 4 };
                tri[2] = POINT{ cx, cy + 5 };
            }
            HBRUSH arrowBrush = CreateSolidBrush(text);
            HBRUSH oldArrowBrush = reinterpret_cast<HBRUSH>(SelectObject(hdc, arrowBrush));
            HPEN arrowPen = CreatePen(PS_SOLID, 1, text);
            HPEN oldArrowPen = reinterpret_cast<HPEN>(SelectObject(hdc, arrowPen));
            Polygon(hdc, tri, 3);
            if (oldArrowPen) SelectObject(hdc, oldArrowPen);
            if (oldArrowBrush) SelectObject(hdc, oldArrowBrush);
            DeleteObject(arrowPen);
            DeleteObject(arrowBrush);
            return true;
        }

        wchar_t textBuf[256]{};
        GetWindowTextW(draw->hwndItem, textBuf, static_cast<int>(ARRAYSIZE(textBuf)));
        HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT)));
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, text);
        RECT textRc = rc;
        if (!DrawButtonLabelWithChevron(hdc, textRc, textBuf, text, pressed))
        {
            if (pressed) OffsetRect(&textRc, 1, 1);
            DrawTextW(hdc, textBuf, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        if (oldFont) SelectObject(hdc, oldFont);
        return true;
    }

    bool DrawDarkStaticLine(DRAWITEMSTRUCT* draw)
    {
        if (!draw || draw->CtlType != ODT_STATIC) return false;
        RECT rc = draw->rcItem;
        HDC hdc = draw->hDC;
        FillRect(hdc, &rc, DarkWindowBrush());
        const int y = rc.top + ((rc.bottom - rc.top) / 2);
        HPEN pen = CreatePen(PS_SOLID, 1, kDarkBorder);
        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(hdc, pen));
        MoveToEx(hdc, rc.left, y, nullptr);
        LineTo(hdc, rc.right, y);
        if (oldPen) SelectObject(hdc, oldPen);
        DeleteObject(pen);
        return true;
    }

    bool DrawDarkOwnerDrawControl(DRAWITEMSTRUCT* draw)
    {
        if (!draw) return false;
        if (draw->CtlType == ODT_BUTTON) return DrawDarkButton(draw);
        if (draw->CtlType == ODT_STATIC) return DrawDarkStaticLine(draw);
        return false;
    }

    BOOL CALLBACK ApplyEditorDarkModeToChild(HWND child, LPARAM)
    {
        EnableEditorDarkModeForWindow(child);
        wchar_t cls[64]{};
        GetClassNameW(child, cls, static_cast<int>(ARRAYSIZE(cls)));
        if (_wcsicmp(cls, L"Button") == 0)
        {
            SetWindowTheme(child, L"DarkMode_Explorer", nullptr);
            MakeButtonOwnerDraw(child);
        }
        else if (_wcsicmp(cls, L"Static") == 0 ||
            _wcsicmp(cls, L"Edit") == 0 || _wcsicmp(cls, L"ListBox") == 0 ||
            _wcsicmp(cls, L"ComboBox") == 0 || _wcsicmp(cls, WC_LISTVIEWW) == 0 ||
            _wcsicmp(cls, STATUSCLASSNAMEW) == 0)
        {
            SetWindowTheme(child, L"DarkMode_Explorer", nullptr);
        }
        return TRUE;
    }

    void ApplyEditorDarkModeToWindowTree(HWND hwnd)
    {
        if (!kEditorDarkMode || !hwnd) return;
        EnableEditorDarkModeForWindow(hwnd);
        EnumChildWindows(hwnd, ApplyEditorDarkModeToChild, 0);
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }

    LRESULT HandleDarkCtlColor(UINT msg, WPARAM wParam)
    {
        if (!kEditorDarkMode) return 0;
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, msg == WM_CTLCOLORLISTBOX ? kDarkListText : kDarkText);

        switch (msg)
        {
        case WM_CTLCOLORSTATIC:
            SetBkMode(hdc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(DarkWindowBrush());
        case WM_CTLCOLORBTN:
            SetBkMode(hdc, TRANSPARENT);
            SetBkColor(hdc, kDarkControlBg);
            return reinterpret_cast<LRESULT>(DarkControlBrush());
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, kDarkFieldBg);
            return reinterpret_cast<LRESULT>(DarkFieldBrush());
        case WM_CTLCOLORSCROLLBAR:
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, kDarkControlBg);
            return reinterpret_cast<LRESULT>(DarkControlBrush());
        case WM_CTLCOLORDLG:
            SetBkMode(hdc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(DarkWindowBrush());
        default:
            return 0;
        }
    }

    void ApplyDarkMenuHints(HMENU menu)
    {
        if (!kEditorDarkMode || !menu) return;
        MENUINFO mi{};
        mi.cbSize = sizeof(mi);
        mi.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
        mi.hbrBack = DarkPanelBrush();
        SetMenuInfo(menu, &mi);
    }

    void DrawDarkStatusBar(HWND hwnd, HDC hdc)
    {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, DarkPanelBrush());

        HPEN pen = CreatePen(PS_SOLID, 1, kDarkBorder);
        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(hdc, pen));
        MoveToEx(hdc, rc.left, rc.top, nullptr);
        LineTo(hdc, rc.right, rc.top);
        if (oldPen) SelectObject(hdc, oldPen);
        DeleteObject(pen);

        std::wstring text = g_app.statusText;
        if (text.empty())
        {
            wchar_t buf[512]{};
            GetWindowTextW(hwnd, buf, static_cast<int>(ARRAYSIZE(buf)));
            text = buf;
        }

        HFONT defaultFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HFONT boldFont = nullptr;
        LOGFONTW lf{};
        if (defaultFont && GetObjectW(defaultFont, sizeof(lf), &lf) == sizeof(lf))
        {
            lf.lfWeight = FW_BOLD;
            boldFont = CreateFontIndirectW(&lf);
        }

        HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(hdc, defaultFont));
        SetBkMode(hdc, TRANSPARENT);

        std::wstring gameTitle = g_app.gameProfile.title.empty() ? L"Unknown Game" : g_app.gameProfile.title;
        RECT gameRc = rc;
        gameRc.left += 8;
        gameRc.right -= 10;
        int gameWidth = 0;
        if (!gameTitle.empty())
        {
            HFONT oldGameFont = reinterpret_cast<HFONT>(SelectObject(hdc, boldFont ? boldFont : defaultFont));
            SIZE gameSize{};
            GetTextExtentPoint32W(hdc, gameTitle.c_str(), static_cast<int>(gameTitle.size()), &gameSize);
            gameWidth = gameSize.cx + 18;
            gameRc.left = std::max(gameRc.left, gameRc.right - gameWidth);
            SetTextColor(hdc, RGB(255, 255, 255));
            DrawTextW(hdc, gameTitle.c_str(), -1, &gameRc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(hdc, oldGameFont);
            SelectObject(hdc, defaultFont);
        }

        SetTextColor(hdc, kDarkMutedText);
        RECT textRc = rc;
        textRc.left += 8;
        textRc.right -= std::max(8, gameWidth + 12);
        if (textRc.right > textRc.left + 20)
        {
            DrawTextW(hdc, text.c_str(), -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        if (oldFont) SelectObject(hdc, oldFont);
        if (boldFont) DeleteObject(boldFont);
    }

    LRESULT CALLBACK DarkStatusBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            DrawDarkStatusBar(hwnd, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SETTEXT:
        {
            // Keep the status bar fully owner-painted. Letting the native status
            // control repaint its own text caused stale game-title remnants after
            // horizontal window resizes.
            g_app.statusText = lParam ? reinterpret_cast<LPCWSTR>(lParam) : L"";
            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
            return TRUE;
        }
        case WM_SIZE:
        case WM_WINDOWPOSCHANGED:
        {
            const LRESULT result = g_app.statusBarOldProc
                ? CallWindowProcW(g_app.statusBarOldProc, hwnd, msg, wParam, lParam)
                : DefWindowProcW(hwnd, msg, wParam, lParam);
            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
            return result;
        }
        case WM_NCDESTROY:
        {
            WNDPROC oldProc = g_app.statusBarOldProc;
            g_app.statusBarOldProc = nullptr;
            if (oldProc)
            {
                SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(oldProc));
                return CallWindowProcW(oldProc, hwnd, msg, wParam, lParam);
            }
            break;
        }
        default:
            break;
        }
        return g_app.statusBarOldProc ? CallWindowProcW(g_app.statusBarOldProc, hwnd, msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    struct CanvasMetrics
    {
        mapfmt::Bounds baseBounds{};
        mapfmt::Bounds viewBounds{};
        double centerPixelX = 0.0;
        double centerPixelY = 0.0;
        double centerWorldX = 0.0;
        double centerWorldZ = 0.0;
        double scale = 1.0;
        double visibleWorldWidth = 1.0;
        double visibleWorldHeight = 1.0;
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

    void RefreshStatus();
    void RefreshGameProfile();
    void ShowAboutDialog(HWND owner);
    void ShowEditorSettingsDialog();
    void InvalidateEditorViews();
    void InvalidateEditorViewsIncludingWalkPreview();
    bool ShouldShowWalkPreview(const RECT& panelRc);
    RECT GetWalkPreviewRect(const RECT& panelRc);
    RECT GetWalkPreviewControlsRect(const RECT& panelRc);
    void InvalidateWalkPreview();
    void InvalidateTexturePreviewOnly(HWND hwnd);
    void EnsureWalkPreviewCamera();
    int NormalizeWalkPreviewDir(int dir);
    int WalkPreviewDirFromScreenDrag(int dx, int dy);
    int WalkPreviewDirFromRotationUnits(int rot);
    void WalkPreviewDirectionVectors(int dir, double& fx, double& fz, double& rx, double& rz);
    void UpdateModeButtons();
    void RefreshZoneList();
    void MarkDirty();
    void UpdateInfoPanelScrollBar(HWND hwnd);
    void UpdateCanvasScrollBars(HWND hwnd);
    void FitViewToDocument();
    void ClearInsertMode();
    bool SaveDocument(bool saveAs);
    void OpenDocument();
    void CloseDocument();
    void DeleteSelectedZone();
    bool DeleteSelectedMonsterSpawn();
    void DeleteSelectedItem();
    void PushUndoSnapshot();
    void UndoLastChange();
    void RefreshPreviewImage();
    bool LoadTexturePreviewImage(const std::string& textureName, TexturePreviewImage& outImage);
    std::string DirName(const std::string& path);
    std::string JoinPath(const std::string& base, const std::string& leaf);
    std::string TrimTrailingSlashes(std::string path);
    bool DirectoryExistsLocal(const std::string& path);
    bool FileExistsLocal(const std::string& path);
    std::string BaseNameNoSlash(const std::string& path);
    GameProfile DetectGameProfileForMapPath(const std::string& path);
    std::wstring BuildMapValidationReport();
    bool ReadFileBinaryLocal(const std::string& path, std::vector<uint8_t>& data);
    bool LoadMaybeCrmLocal(const std::string& path, std::vector<uint8_t>& out, std::string& error);
    bool ResolveTexturePath(const std::string& textureName, std::string& outPath);
    std::string GetConfiguredTextureFolder();
    std::vector<std::string> BuildTextureSlotNameList();
    std::string LowerAscii(std::string value);
    bool LoadSurfaceTextureMetadataForMap(const std::string& path);
    bool SaveSurfaceTextureMetadataForMap(const std::string& path, std::string& errorMessage);
    void WeldWallCornersAroundZone(int zoneIndex);
    void WeldWallEndpointClusterToPoint(POINT anchor);
    void OrientWallForConnectedEndpoints(int zoneIndex);
    void ReverseWallDirectionPreservingTexture(mapfmt::Zone& zone);
    bool IsWallZone(const mapfmt::Zone& zone);
    int FindReverseWallPairIndex(int zoneIndex);
    bool IsVisualBackfaceWallIndex(int zoneIndex);
    int GetCanonicalWallIndex(int zoneIndex);
    void NormalizeSelectedWallToCanonical();
    void NormalizeCollinearConnectedWallsAroundZone(int zoneIndex);
    void CleanupWallsForGameplaySave();
    void SyncBackfaceWallFromFront(int frontIndex, int backIndex);
    void EnsureBackfaceForWallAtIndex(int zoneIndex);
    POINT WorldToScreen(const RECT& rc, const mapfmt::Bounds& bounds, int x, int z);
    POINT ScreenToWorldPrecise(const RECT& rc, const mapfmt::Bounds& bounds, int sx, int sy);
    POINT ScreenToWorld(const RECT& rc, const mapfmt::Bounds& bounds, int sx, int sy);
    POINT ApplyWallEndpointSnap(const RECT& rc, POINT worldPoint);
    POINT SnapWorldPointToFineGrid(POINT point);
    POINT ConstrainWallEndpoint45(POINT start, POINT current);
    POINT PrepareWallDrawEndpoint(const RECT& rc, POINT rawPoint);
    POINT PrepareLineZoneDrawEndpoint(POINT rawPoint);
    int16_t ClampWorldToInt16(int value);
    bool IsTinyDrawSegment(POINT a, POINT b);
    int GetZonePreviewSlotCount(const mapfmt::Zone& zone);
    int GetActiveTextureBandForZone(const mapfmt::Zone& zone);
    double GetWallLengthWorld(const mapfmt::Zone& zone);
    int GetWallTextureScaleUnits(const mapfmt::Zone& zone);
    int CalculateWallTextureBandCountForLength(double wallLength);
    WallTextureMode GetWallTextureModeForZone(const mapfmt::Zone& zone);
    std::wstring WallTextureModeLabel(WallTextureMode mode);
    void ApplyWallTextureModeToZone(mapfmt::Zone& zone, WallTextureMode mode);
    void UpdateWallTextureBandCountFromLength(mapfmt::Zone& zone);
    bool IsSelectedWall();
    bool IsSwitchTexturePickerContext();
    bool IsWallTextureMappingPickerActive();
    bool IsWallTexturePickerActive();
    bool IsSurfaceTexturePickerActive();
    bool IsTexturePickerActive();
    void RefreshWallTexturePickerPreviewFromActive();
    void RefreshSurfaceTextureChoices();
    int GetActiveSurfaceTextureIndex();
    void SetActiveSurfaceTextureIndex(int textureIndex);
    int GetActiveSurfaceTextureChoice();
    void SetActiveSurfaceTextureChoice(int textureChoice);
    std::string GetActiveSurfaceTextureName();
    std::wstring ActiveSurfaceTextureLabel();
    void SyncActiveWallTextureFromSelectedWall();
    int GetActiveWallTextureIndex();
    uint8_t MakeTextureIndex(int slot, int strip);
    bool ResolveAnimationForTextureIndex(int textureIndex, mapfmt::AnimationEntry& outAnimation);
    bool EnsureAnimationEntryForTextureIndex(int textureIndex);
    int GetActiveWallAssignedTextureIndex();
    int NormalizeStripToAnimationStart(int slot, int strip);
    std::wstring FormatAnimationInfoForTexture(int textureIndex);
    void ApplyActiveWallTextureToZoneBand(mapfmt::Zone& zone, int band);
    void InitializeNewWallTextureSequence(mapfmt::Zone& zone);
    RECT GetWallTextureModeRowRect(const RECT& panelRc);
    bool HandleWallTextureModeClick(HWND hwnd, int sx, int sy);
    int CountZonesOfType(mapfmt::ZoneType type);
    int CountEventCommandsOfType(mapfmt::CommandType type);
    int CountMonsterSpawns();
    int ComputeMonsterSpawnMarkerIndex(int eventIndex, int commandIndex);
    bool IsSelectedMonsterSpawnValid();
    void ClearSelectedMonsterSpawn();
    const mapfmt::EventCommand* GetSelectedMonsterSpawnCommand();
    mapfmt::EventCommand* GetSelectedMonsterSpawnCommandMutable();
    bool RotateSelectedMonsterSpawnByDegrees(int degrees);
    bool IsSelectedTeleportTargetValid();
    void ClearSelectedTeleportTarget();
    const mapfmt::EventCommand* GetSelectedTeleportTargetCommand();
    mapfmt::EventCommand* GetSelectedTeleportTargetCommandMutable();
    bool RotateSelectedTeleportTargetByDegrees(int degrees);
    std::wstring MonsterTypeName(int monsterType);
    const ObjectTypeInfo* GetObjectTypeInfo(int objectType);
    std::wstring ObjectPlacementLabel(int objectType);
    void SetObjectPlacementMode(int objectType);
    void ShowObjectPlacementMenu(ObjectPlacementGroup group, HWND anchorButton);
    void PlaceObjectAtWorld(POINT worldPoint);
    mapfmt::EventCommand* FindPlayerStartCommand(int playerType);
    const mapfmt::EventCommand* FindPlayerStartCommandConst(int playerType);
    MonsterSpawnSelection FindPlayerStartSelection(int playerType);
    void SetPlayerStartAtWorld(POINT worldPoint);
    bool IsLevelEndZone(const mapfmt::Zone& zone);
    bool IsEventTriggerLineZone(const mapfmt::Zone& zone);
    bool IsSelectedEventTriggerOrLevelEndZone();
    bool IsSwitchTextureSourceLineZone(const mapfmt::Zone& zone);
    bool IsSwitchTextureSourceZoneIndex(int zoneIndex);
    int EventSlotFromZoneEventValue(int evValue);
    void AddUniqueIndex(std::vector<int>& values, int value);
    bool EventCommandTargetsZone(const mapfmt::EventCommand& command, int zoneIndex);
    void RemoveEventReferencesForDeletedZoneIndex(int deletedZoneIndex);
    void SwapEventZoneReferences(int firstZoneIndex, int secondZoneIndex);
    void RemapEventZoneReferencesAfterReorder(const std::vector<int>& oldToNew);
    std::vector<int> GetEventTargetZones(int eventIndex);
    std::vector<int> GetTriggerZonesForEvent(int eventIndex);
    std::vector<int> GetEventsControllingZone(int zoneIndex);
    struct SwitchTextureCommandInfo
    {
        int commandIndex = -1;
        int rawTargetZoneIndex = -1;
        int displayTargetZoneIndex = -1;
        int newTextureIndex = -1;
    };
    double ZoneCenterDistanceSquared(int firstZoneIndex, int secondZoneIndex);
    bool ZoneContainsTextureIndex(const mapfmt::Zone& zone, int textureIndex);
    int GetSwitchOffTextureIndexForTargetZone(int targetZoneIndex);
    int GetSwitchAutoOnTextureIndexForTargetZone(int targetZoneIndex);
    bool TryGetSwitchTextureCommandForEvent(int eventIndex, int triggerZoneIndex, SwitchTextureCommandInfo& outInfo);
    bool TryGetSwitchTextureCommandForTriggerZone(int triggerZoneIndex, SwitchTextureCommandInfo& outInfo);
    bool TryGetFirstSwitchTextureCommandForEvent(int eventIndex, int& targetZoneIndex, int& newTextureIndex);
    bool TryGetPendingSwitchTextureChoice(int triggerZoneIndex, int& textureIndex);
    void RememberPendingSwitchTextureChoice(int triggerZoneIndex, int textureIndex);
    void ClearPendingSwitchTextureChoice();
    int GetSwitchTextureIndexForEventWrite(int triggerZoneIndex);
    void SyncActiveSwitchTextureFromTriggerZone(int triggerZoneIndex);
    int GetActiveSwitchTextureTriggerZone();
    bool PersistActiveSwitchTextureChoiceToTrigger(int triggerZoneIndex);
    bool PersistActiveSwitchTextureChoiceForCurrentContext();
    std::wstring EventSlotRoleText(int eventIndex);
    std::wstring EventSlotComboLabel(int eventIndex);
    bool EventScriptHasAdvancedRaw(int eventIndex);
    bool WarnIfEventScriptAdvancedRaw(int eventIndex);
    std::wstring FormatEventLogicSummaryForZone(int zoneIndex);
    std::wstring FormatSelectedZoneValidationSummary(int zoneIndex);
    MapValidationReportSections BuildMapValidationReportSections();
    bool ApplySafeValidationRepairsNoPrompt(const MapValidationReportSections& sections, bool pushUndoSnapshot);
    bool ConfirmSaveWithValidationAndMaybeFixes(const std::string& path);
    void DrawEventGraphOverlay(HDC hdc, const RECT& rc, const mapfmt::Bounds& bounds);
    void DrawEventLogicOverlay(HDC hdc, const RECT& rc, const mapfmt::Bounds& bounds);
    void DrawMoveWallGroupEventPaths(HDC hdc, const RECT& rc, const mapfmt::Bounds& bounds, int eventIndex);
    void DrawAllMoveWallGroupEventPaths(HDC hdc, const RECT& rc, const mapfmt::Bounds& bounds);
    int FindAvailableEventSlotForTrigger(int triggerZoneIndex);
    int EnsureTriggerHasEventSlot(int triggerZoneIndex);
    bool AddOpenDoorLinkToEvent(int eventIndex, int targetZoneIndex);
    bool AddMoveWallGroupLinkToEvent(int eventIndex, int sourceZoneIndex, int guideZoneIndex, int& targetZoneIndex);
    bool AddSwitchTextureLinkToEvent(int eventIndex, int targetZoneIndex);
    bool IsZoneControlledByOpenDoor(int zoneIndex);
    void FlipSelectedDoorDirection();
    void StartLinkEventToZoneTool();
    void StartLinkEventToSwitchTextureTool();
    void StartLinkEventToEnemyObjectTool();
    void StartDeleteLinkEventTool();
    bool EventHasActiveTrigger(int eventIndex);
    bool CanDeleteSelectedEventLink();
    bool DeleteSelectedEventLink();
    void StartLinkEventToRotateTool(bool clockwise);
    void StartLinkEventToMoveWallGroupTool();
    void StartSetTeleportTargetTool();
    void ResetPendingMoveWallGroup();
    void MakeMoveWallGroupGuideAiNeutral(mapfmt::Zone& guide);
    bool BeginMoveWallGroupTargetPlacement(int targetZoneIndex, POINT pickWorld);
    bool UpdateMoveWallGroupTargetPlacement(POINT world);
    bool CommitMoveWallGroupTargetPlacement(POINT world);
    void DrawMoveWallGroupPlacementPreview(HDC hdc, const RECT& rc, const mapfmt::Bounds& bounds);
    void ResetPendingTeleportTarget();
    bool FinishLinkEventToZone(int targetZoneIndex);
    bool FinishLinkEventToSwitchTexture(int targetZoneIndex);
    bool FinishLinkEventToEnemyObject(const MonsterSpawnSelection& targetSpawn);
    bool FinishDeleteLinkEventToEnemyObject(const MonsterSpawnSelection& targetSpawn);
    bool FinishLinkEventToRotate(int targetZoneIndex);
    bool FinishLinkEventToMoveWallGroup(int targetZoneIndex);
    bool PointsTouchForRotateRun(const mapfmt::Zone& a, const mapfmt::Zone& b);
    bool IsCanonicalRotateWallIndex(int zoneIndex);
    void CollectCanonicalRotateWallComponent(int targetZoneIndex, std::vector<int>& outFronts);
    void DetectConsecutiveRotatingRun(int targetZoneIndex, int& firstZoneIndex, int& zoneCount);
    struct RotateWallRunInfo
    {
        int frontFirst = -1;
        int frontCount = 0;
        int backFirst = -1;
        int backCount = 0;
    };
    struct MoveWallGroupRunInfo
    {
        int activeFirst = -1;
        int activeCount = 0;
        int guideFirst = -1;
        int guideCount = 0;
    };
    bool PrepareRotateWallRunForEvent(int& targetZoneIndex, RotateWallRunInfo* outInfo = nullptr);
    bool PrepareMoveWallGroupRunForEvent(int& targetZoneIndex, MoveWallGroupRunInfo* outInfo = nullptr);
    bool PrepareMoveWallGroupRunForEvent(int& sourceZoneIndex, int guideZoneIndex, MoveWallGroupRunInfo* outInfo = nullptr);
    bool RotateCommandIntersectsRun(const mapfmt::EventCommand& command, const RotateWallRunInfo& run);
    void AddRotateCommand(std::vector<mapfmt::EventCommand>& commands, int first, int count, int speed, int flags);
    bool FinishSetTeleportTarget(POINT world);
    bool CommitPendingTeleportTarget();
    bool RotatePendingTeleportTargetByDegrees(int degrees);
    void DrawTeleportDirectionSelectionOverlay(HDC hdc, const RECT& rc, const mapfmt::Bounds& bounds);
    void DrawTeleportTargetOverlays(HDC hdc, const RECT& rc, const mapfmt::Bounds& bounds);
    std::wstring GetCanvasHelpText();
    void DrawCanvasHelpOverlay(HDC hdc, const RECT& rc);
    int FindLevelEndZoneIndex();
    void SetLevelEndFromDrawPoints(POINT a, POINT b);
    bool LoadObjectPreviewImage(int objectType, ObjectPreviewImage& outImage);
    bool IsLinearZoneType(int ztype);
    bool IsLineInsertMode(InsertMode mode);
    void CancelCurrentTool();
    void DrawZoneOverlayLabel(HDC hdc, const std::wstring& label, int x, int y, COLORREF textColor);
    void DrawEventMonsterSpawnOverlays(HDC hdc, const RECT& rc, const mapfmt::Bounds& bounds);
    void DrawEventSpawnConnectionLines(HDC hdc, const RECT& rc, const mapfmt::Bounds& bounds, int eventIndex, const POINT& from);
    MonsterSpawnSelection HitTestMonsterSpawn(int sx, int sy, const RECT& rc);
    void CancelMonsterSpawnDrag(bool refreshViews);
    bool BeginMonsterSpawnDrag(const MonsterSpawnSelection& selection, POINT clientPoint, const RECT& rc);
    bool UpdateMonsterSpawnDrag(POINT clientPoint, const RECT& rc);
    bool FinishMonsterSpawnDrag();
    TeleportSelection HitTestTeleportTarget(int sx, int sy, const RECT& rc);
    RECT GetTextureSlotBarRect(const RECT& panelRc);

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

        const std::array<std::wstring, 3> markers = { L"txts/", L"objs/", L"char/" };
        for (const auto& marker : markers)
        {
            const std::size_t pos = wide.find(marker);
            if (pos != std::wstring::npos)
            {
                return wide.substr(pos);
            }
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

    void DecodeGloomPaletteWord(uint16_t col, uint8_t& r, uint8_t& g, uint8_t& b)
    {
        // Match the ZGloom runtime: packed Amiga palette words are 0RGB.
        const uint8_t r4 = static_cast<uint8_t>((col >> 8) & 0x000Fu);
        const uint8_t g4 = static_cast<uint8_t>((col >> 4) & 0x000Fu);
        const uint8_t b4 = static_cast<uint8_t>(col & 0x000Fu);
        r = static_cast<uint8_t>(r4 | (r4 << 4));
        g = static_cast<uint8_t>(g4 | (g4 << 4));
        b = static_cast<uint8_t>(b4 | (b4 << 4));
    }

    uint32_t MakeArgbFromGloomPaletteWord(uint16_t col)
    {
        uint8_t r = 0, g = 0, b = 0;
        DecodeGloomPaletteWord(col, r, g, b);
        return 0xFF000000u |
            (static_cast<uint32_t>(r) << 16) |
            (static_cast<uint32_t>(g) << 8) |
            static_cast<uint32_t>(b);
    }

    void StoreGloomPaletteWord(std::array<std::array<uint8_t, 3>, 256>& palette, size_t index, uint16_t col)
    {
        if (index >= palette.size()) return;
        DecodeGloomPaletteWord(col, palette[index][0], palette[index][1], palette[index][2]);
    }

    bool TryReadCountedGloomPaletteHeader(const std::vector<uint8_t>& raw, size_t paletteOffset, uint16_t& entries)
    {
        entries = 0;
        if (paletteOffset + 2u > raw.size()) return false;
        entries = ReadBE16Local(raw.data() + paletteOffset);
        if (entries == 0 || entries > 256) return false;
        return paletteOffset + 2u + static_cast<size_t>(entries) * 2u <= raw.size();
    }

    bool ReadGloomSpritePaletteColor(const std::vector<uint8_t>& raw, size_t paletteOffset, uint8_t idx, uint16_t& outColor)
    {
        // Index 0 is transparent for object sprites; never read it as a visible color.
        if (idx == 0) return false;

        uint16_t entries = 0;
        size_t colorOffset = 0;
        if (TryReadCountedGloomPaletteHeader(raw, paletteOffset, entries))
        {
            // Two palette layouts are seen in the wild:
            //   count < 256: count word + colors for indices 1..count (index 0 is omitted/mask)
            //   count = 256: count word + colors for indices 0..255
            if (entries >= 256)
            {
                colorOffset = paletteOffset + 2u + static_cast<size_t>(idx) * 2u;
            }
            else
            {
                if (idx > entries) return false;
                colorOffset = paletteOffset + 2u + static_cast<size_t>(idx - 1u) * 2u;
            }
        }
        else
        {
            // ZGloom runtime-style object palettes: raw palette words start at paletteOffset.
            colorOffset = paletteOffset + static_cast<size_t>(idx) * 2u;
        }

        if (colorOffset + 1u >= raw.size()) return false;
        outColor = ReadBE16Local(raw.data() + colorOffset);
        return true;
    }

    void LoadGloomTexturePalette(const std::vector<uint8_t>& raw, size_t paletteOffset, std::array<std::array<uint8_t, 3>, 256>& palette)
    {
        palette = {};
        uint16_t entries = 0;
        if (TryReadCountedGloomPaletteHeader(raw, paletteOffset, entries))
        {
            if (entries >= 256)
            {
                // Counted 256-color layout includes palette index 0 after the count word.
                for (size_t p = 0; p < 256u; ++p)
                {
                    const size_t off = paletteOffset + 2u + p * 2u;
                    StoreGloomPaletteWord(palette, p, ReadBE16Local(raw.data() + off));
                }
            }
            else
            {
                // Original/ZGloom wall texture layout omits index 0; stored color 0 maps to palette index 1.
                const size_t safeEntries = MinValue<size_t>(static_cast<size_t>(entries), 255u);
                for (size_t p = 0; p < safeEntries; ++p)
                {
                    const size_t off = paletteOffset + 2u + p * 2u;
                    StoreGloomPaletteWord(palette, p + 1u, ReadBE16Local(raw.data() + off));
                }
            }
            return;
        }

        const size_t paletteEntries = MinValue<size_t>(256u, (raw.size() - paletteOffset) / 2u);
        for (size_t p = 0; p < paletteEntries; ++p)
        {
            const size_t off = paletteOffset + p * 2u;
            StoreGloomPaletteWord(palette, p, ReadBE16Local(raw.data() + off));
        }
    }

    uint32_t ApplyGloomRuntimeDim(uint32_t pixel, double depth)
    {
        const int dimPalette = ClampValue(static_cast<int>(depth / 128.0), 0, 15);
        auto dimChannel = [dimPalette](uint32_t channel) -> uint32_t
        {
            const uint32_t nibble = (channel >> 4) & 0x0Fu;
            const uint32_t dimmed = (nibble * static_cast<uint32_t>(16 - dimPalette)) / 16u;
            return dimmed | (dimmed << 4);
        };

        const uint32_t r = dimChannel((pixel >> 16) & 0xFFu);
        const uint32_t g = dimChannel((pixel >> 8) & 0xFFu);
        const uint32_t b = dimChannel(pixel & 0xFFu);
        return (r << 16) | (g << 8) | b;
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

    int FloorToFineGrid(int value)
    {
        if (value >= 0) return (value / kGridSnapStep) * kGridSnapStep;
        return -(((-value) + (kGridSnapStep - 1)) / kGridSnapStep) * kGridSnapStep;
    }

    int CeilToFineGrid(int value)
    {
        if (value >= 0) return ((value + (kGridSnapStep - 1)) / kGridSnapStep) * kGridSnapStep;
        return -((-value) / kGridSnapStep) * kGridSnapStep;
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

    double GetCurrentCanvasScale()
    {
        return MaxValue(0.0001, g_app.canvasBaseScale * ClampValue(g_app.canvasZoom, kCanvasZoomMin, kCanvasZoomMax));
    }

    void ClampViewCenterToDocument(const mapfmt::Bounds& rawBounds)
    {
        RECT rc{};
        if (g_app.canvas)
        {
            GetClientRect(g_app.canvas, &rc);
        }
        else
        {
            rc.right = 800;
            rc.bottom = 600;
        }

        const mapfmt::Bounds baseBounds = GetAlignedDocumentBounds(rawBounds);
        const double baseWidth = MaxValue(1.0, static_cast<double>(baseBounds.maxX - baseBounds.minX));
        const double baseHeight = MaxValue(1.0, static_cast<double>(baseBounds.maxZ - baseBounds.minZ));
        const double scale = GetCurrentCanvasScale();
        const double visibleWidth = MaxValue(static_cast<double>(kGridStep), static_cast<double>(MaxValue(1, rc.right - rc.left)) / scale);
        const double visibleHeight = MaxValue(static_cast<double>(kGridStep), static_cast<double>(MaxValue(1, rc.bottom - rc.top)) / scale);

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
        RECT rc{};
        if (g_app.canvas)
        {
            GetClientRect(g_app.canvas, &rc);
        }
        if ((rc.right - rc.left) <= 1 || (rc.bottom - rc.top) <= 1)
        {
            rc.right = 800;
            rc.bottom = 600;
        }

        const double baseWidth = MaxValue(1.0, static_cast<double>(baseBounds.maxX - baseBounds.minX));
        const double baseHeight = MaxValue(1.0, static_cast<double>(baseBounds.maxZ - baseBounds.minZ));
        const double fitScaleX = static_cast<double>(MaxValue(1, rc.right - rc.left)) / baseWidth;
        const double fitScaleY = static_cast<double>(MaxValue(1, rc.bottom - rc.top)) / baseHeight;
        g_app.canvasBaseScale = ClampValue(MinValue(fitScaleX, fitScaleY), 0.001, 8.0);
        g_app.canvasZoom = 1.0;
        g_app.viewCenterX = (static_cast<double>(baseBounds.minX) + static_cast<double>(baseBounds.maxX)) * 0.5;
        g_app.viewCenterZ = (static_cast<double>(baseBounds.minZ) + static_cast<double>(baseBounds.maxZ)) * 0.5;
        g_app.viewInitialized = true;
        ClampViewCenterToDocument(rawBounds);
        UpdateCanvasScrollBars(g_app.canvas);
    }

    CanvasMetrics GetCanvasMetrics(const RECT& rc, const mapfmt::Bounds& rawBounds)
    {
        CanvasMetrics metrics;
        metrics.baseBounds = GetAlignedDocumentBounds(rawBounds);
        metrics.centerPixelX = static_cast<double>(rc.left + rc.right) * 0.5;
        metrics.centerPixelY = static_cast<double>(rc.top + rc.bottom) * 0.5;
        metrics.scale = GetCurrentCanvasScale();
        metrics.visibleWorldWidth = MaxValue(static_cast<double>(kGridStep), static_cast<double>(MaxValue(1, rc.right - rc.left)) / metrics.scale);
        metrics.visibleWorldHeight = MaxValue(static_cast<double>(kGridStep), static_cast<double>(MaxValue(1, rc.bottom - rc.top)) / metrics.scale);

        double centerX = g_app.viewInitialized ? g_app.viewCenterX :
            (static_cast<double>(metrics.baseBounds.minX) + static_cast<double>(metrics.baseBounds.maxX)) * 0.5;
        double centerZ = g_app.viewInitialized ? g_app.viewCenterZ :
            (static_cast<double>(metrics.baseBounds.minZ) + static_cast<double>(metrics.baseBounds.maxZ)) * 0.5;

        const double baseWidth = MaxValue(1.0, static_cast<double>(metrics.baseBounds.maxX - metrics.baseBounds.minX));
        const double baseHeight = MaxValue(1.0, static_cast<double>(metrics.baseBounds.maxZ - metrics.baseBounds.minZ));

        if (metrics.visibleWorldWidth < baseWidth)
        {
            const double minCenterX = static_cast<double>(metrics.baseBounds.minX) + metrics.visibleWorldWidth * 0.5;
            const double maxCenterX = static_cast<double>(metrics.baseBounds.maxX) - metrics.visibleWorldWidth * 0.5;
            centerX = ClampValue(centerX, minCenterX, maxCenterX);
        }
        else
        {
            centerX = (static_cast<double>(metrics.baseBounds.minX) + static_cast<double>(metrics.baseBounds.maxX)) * 0.5;
        }

        if (metrics.visibleWorldHeight < baseHeight)
        {
            const double minCenterZ = static_cast<double>(metrics.baseBounds.minZ) + metrics.visibleWorldHeight * 0.5;
            const double maxCenterZ = static_cast<double>(metrics.baseBounds.maxZ) - metrics.visibleWorldHeight * 0.5;
            centerZ = ClampValue(centerZ, minCenterZ, maxCenterZ);
        }
        else
        {
            centerZ = (static_cast<double>(metrics.baseBounds.minZ) + static_cast<double>(metrics.baseBounds.maxZ)) * 0.5;
        }

        metrics.centerWorldX = centerX;
        metrics.centerWorldZ = centerZ;
        metrics.viewBounds.minX = static_cast<int32_t>(std::floor(centerX - metrics.visibleWorldWidth * 0.5));
        metrics.viewBounds.maxX = static_cast<int32_t>(std::ceil(centerX + metrics.visibleWorldWidth * 0.5));
        metrics.viewBounds.minZ = static_cast<int32_t>(std::floor(centerZ - metrics.visibleWorldHeight * 0.5));
        metrics.viewBounds.maxZ = static_cast<int32_t>(std::ceil(centerZ + metrics.visibleWorldHeight * 0.5));
        metrics.viewBounds.valid = true;
        return metrics;
    }

    double GetWallLengthWorld(const mapfmt::Zone& zone)
    {
        const double dx = static_cast<double>(zone.x2) - static_cast<double>(zone.x1);
        const double dz = static_cast<double>(zone.z2) - static_cast<double>(zone.z1);
        return std::hypot(dx, dz);
    }

    int GetWallTextureScaleUnits(const mapfmt::Zone& zone)
    {
        int scaleUnits = std::abs(static_cast<int>(zone.sc));
        if (scaleUnits <= 0)
        {
            scaleUnits = kDefaultWallScale;
        }
        return ClampValue(scaleUnits, 1, 16);
    }

    int GetPositiveWallTextureRepeatCount(const mapfmt::Zone& zone)
    {
        if (zone.sc <= 0)
        {
            return 1;
        }

        int repeatCount = static_cast<int>(zone.sc) / 2;
        if (repeatCount <= 0)
        {
            repeatCount = 1;
        }
        return ClampValue(repeatCount, 1, 8);
    }

    bool WallTextureSlotsAreSame(const mapfmt::Zone& zone, int count)
    {
        count = ClampValue(count, 1, static_cast<int>(zone.textures.size()));
        const uint8_t texture = zone.textures[0];
        for (int i = 1; i < count; ++i)
        {
            if (zone.textures[i] != texture)
            {
                return false;
            }
        }
        return true;
    }

    bool TryCalculateShortWallClipScale(double wallLength, int& scale)
    {
        scale = 0;
        if (wallLength < 1.0 || wallLength >= kTextureElementWorldLength)
        {
            return false;
        }

        // ZGloom's renderer path is:
        //   scale = (sc < 0) ? 1 : sc / 2;
        //   column = frac(texpos * scale) * 64;
        //   if (sc < 0) column /= (-sc * 2);
        // Negative sc is therefore the only game-compatible way to clip a
        // short wall instead of stretching a full 64px texture strip onto it.
        // For the 8-point raster this gives: 128u -> -1, 64u -> -2, 32u -> -4.
        const double rawAbsScale = (kTextureElementWorldLength * 0.5) / wallLength;
        const int absScale = ClampValue(static_cast<int>(std::lround(rawAbsScale)), 1, 16);
        const double representedLength = (kTextureElementWorldLength * 0.5) / static_cast<double>(absScale);
        if (std::fabs(representedLength - wallLength) > 2.0)
        {
            return false;
        }

        scale = -absScale;
        return true;
    }

    int CalculateRepeatModeScaleForLength(double wallLength)
    {
        int shortWallScale = 0;
        if (TryCalculateShortWallClipScale(wallLength, shortWallScale))
        {
            return shortWallScale;
        }

        const int repeatCount = ClampValue(static_cast<int>(std::ceil(MaxValue(1.0, wallLength) / kTextureElementWorldLength)), 1, 8);
        return ClampValue(repeatCount * 2, kDefaultWallScale, 16);
    }

    bool ZoneUsesTwoEighthClipTextureMode(const mapfmt::Zone& zone)
    {
        if (zone.ztype != static_cast<int16_t>(mapfmt::ZoneType::Wall))
        {
            return false;
        }

        // Original Gloom maps use sc=-2 on 32u end-cap walls to sample the
        // first quarter of a 64px texture strip.  This is the common lamp-row
        // case: a 2/8-wide texture detail is squeezed onto a 1/8-thick wall
        // front.  Do not classify 64u walls this way; for those sc=-2 is a
        // normal 1:1 quarter-texture clip.
        return zone.sc == -2 && GetWallLengthWorld(zone) <= static_cast<double>(kGridSnapStep + 2);
    }

    bool ZoneUsesGameNativeRepeatTextureMode(const mapfmt::Zone& zone)
    {
        if (zone.ztype != static_cast<int16_t>(mapfmt::ZoneType::Wall))
        {
            return false;
        }

        if (zone.sc < 0)
        {
            return true;
        }

        const int repeatCount = GetPositiveWallTextureRepeatCount(zone);
        return repeatCount > 1 && WallTextureSlotsAreSame(zone, repeatCount);
    }

    bool ZoneUsesHalfScaleTextureMode(const mapfmt::Zone& zone)
    {
        (void)zone;
        // sc=1 looked promising in map dumps, but ZGloom's renderer evaluates
        // sc/2 with integer division and then clamps zero to one. In practice
        // sc=1 renders exactly like sc=2: one full texture stretched over the
        // complete wall. Keep this helper false so loaded sc=1 walls are shown
        // as Stretch instead of offering a fake 1:2 mode.
        return false;
    }

    double GetWallTextureWorldLength(const mapfmt::Zone& zone)
    {
        if (zone.sc < 0)
        {
            const int divisor = MaxValue(1, std::abs(static_cast<int>(zone.sc)) * 2);
            return GetWallLengthWorld(zone) * static_cast<double>(divisor);
        }

        const int repeatCount = GetPositiveWallTextureRepeatCount(zone);
        return MaxValue(1.0, GetWallLengthWorld(zone) / static_cast<double>(repeatCount));
    }

    int GetZonePreviewSlotCount(const mapfmt::Zone& zone)
    {
        if (zone.ztype != static_cast<int16_t>(mapfmt::ZoneType::Wall))
        {
            return 1;
        }

        if (zone.sc < 0)
        {
            return 1;
        }

        return GetPositiveWallTextureRepeatCount(zone);
    }

    int GetActiveTextureBandForZone(const mapfmt::Zone& zone)
    {
        return ClampValue(g_app.previewTextureBand, 0, GetZonePreviewSlotCount(zone) - 1);
    }

    int CalculateWallTextureBandCountForLength(double wallLength)
    {
        if (wallLength < 1.0)
        {
            return 1;
        }

        const int bands = static_cast<int>(std::ceil(wallLength / kTextureElementWorldLength));
        return ClampValue(bands, 1, 8);
    }

    WallTextureMode GetWallTextureModeForZone(const mapfmt::Zone& zone)
    {
        if (zone.ztype != static_cast<int16_t>(mapfmt::ZoneType::Wall))
        {
            return g_app.activeWallTextureMode;
        }
        if (ZoneUsesTwoEighthClipTextureMode(zone))
        {
            return WallTextureMode::Clip2Of8;
        }
        return ZoneUsesGameNativeRepeatTextureMode(zone) ? WallTextureMode::Repeat1To1 : WallTextureMode::Stretch;
    }

    std::wstring WallTextureModeLabel(WallTextureMode mode)
    {
        switch (mode)
        {
            case WallTextureMode::Repeat1To1: return L"1:1 Clip/Repeat";
            case WallTextureMode::Clip2Of8: return L"2/8 Clip";
            case WallTextureMode::Stretch:
            default: return L"Stretch";
        }
    }

    void FillWallTextureSlotsWithFirstTexture(mapfmt::Zone& zone)
    {
        const uint8_t texture = static_cast<uint8_t>(ClampValue(static_cast<int>(zone.textures[0]), 0, 159));
        for (uint8_t& value : zone.textures)
        {
            value = texture;
        }
    }

    void ApplyWallTextureModeToZone(mapfmt::Zone& zone, WallTextureMode mode)
    {
        if (zone.ztype != static_cast<int16_t>(mapfmt::ZoneType::Wall))
        {
            return;
        }

        const int slot = ClampValue(static_cast<int>(zone.textures[0]) / 20, 0, static_cast<int>(mapfmt::MapDocument::kTextureSlotCount) - 1);
        const int firstTexture = ClampValue(static_cast<int>(zone.textures[0]), 0, 159);
        const int firstStrip = ClampValue(firstTexture % 20, 0, 19);

        if (mode == WallTextureMode::Clip2Of8)
        {
            zone.textures[0] = static_cast<uint8_t>(firstTexture);
            FillWallTextureSlotsWithFirstTexture(zone);
            // Game-native 2/8 horizontal clip: sample the first quarter of the
            // selected 64px strip.  On 32u walls this reproduces the original
            // lamp-row end-cap mapping found in map1_1 zones 47/48.
            zone.sc = static_cast<int16_t>(-2);
        }
        else if (mode == WallTextureMode::Repeat1To1)
        {
            zone.textures[0] = static_cast<uint8_t>(firstTexture);
            FillWallTextureSlotsWithFirstTexture(zone);
            zone.sc = static_cast<int16_t>(CalculateRepeatModeScaleForLength(GetWallLengthWorld(zone)));
        }
        else
        {
            zone.textures[0] = static_cast<uint8_t>(firstTexture);
            for (int i = 1; i < static_cast<int>(zone.textures.size()); ++i)
            {
                zone.textures[i] = MakeTextureIndex(slot, (firstStrip + i) % 20);
            }
            // Positive sc=2 is the original stretch mode: one chosen texture strip
            // is mapped across the whole wall width.
            zone.sc = static_cast<int16_t>(kDefaultWallScale);
        }
    }

    void UpdateWallTextureBandCountFromLength(mapfmt::Zone& zone)
    {
        if (zone.ztype != static_cast<int16_t>(mapfmt::ZoneType::Wall))
        {
            return;
        }

        const WallTextureMode mode = GetWallTextureModeForZone(zone);
        if (mode == WallTextureMode::Clip2Of8)
        {
            FillWallTextureSlotsWithFirstTexture(zone);
            zone.sc = static_cast<int16_t>(-2);
        }
        else if (mode == WallTextureMode::Repeat1To1)
        {
            FillWallTextureSlotsWithFirstTexture(zone);
            zone.sc = static_cast<int16_t>(CalculateRepeatModeScaleForLength(GetWallLengthWorld(zone)));
        }
        else if (zone.sc <= 0 || zone.sc == 1)
        {
            zone.sc = static_cast<int16_t>(kDefaultWallScale);
        }
    }

    bool IsSelectedWall()
    {
        return g_app.selectedZone >= 0 &&
            g_app.selectedZone < static_cast<int>(g_app.document.zones.size()) &&
            g_app.document.zones[g_app.selectedZone].ztype == static_cast<int16_t>(mapfmt::ZoneType::Wall);
    }

    bool IsSwitchTexturePickerContext()
    {
        // v24/v40: switch buttons no longer store a manually picked ON texture
        // on the event trigger. The rule is target based: static panels write
        // OFF+1; animated switch panels write the first texture after the OFF
        // animation, so a red 2-frame OFF panel advances to its green ON frame.
        // The right-hand T0-T7 picker is intentionally not shown for triggers.
        return false;
    }

    bool IsWallTextureMappingPickerActive()
    {
        return g_app.insertMode == InsertMode::Wall || IsSelectedWall();
    }

    bool IsWallTexturePickerActive()
    {
        // v24/v26: switch texture links are derived from the clicked OFF
        // wall/panel, so event-trigger and level-end lines intentionally do
        // not show the right-hand T0-T7/strip picker.
        return IsWallTextureMappingPickerActive() || IsSwitchTexturePickerContext();
    }

    bool IsSurfaceTexturePickerActive()
    {
        // Disabled: floor/ceiling textures are not stored in the game-compatible
        // map data, so the editor no longer exposes an editor-only picker.
        return false;
    }

    bool IsTexturePickerActive()
    {
        return IsWallTexturePickerActive();
    }

    std::string LowerAscii(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return value;
    }

    bool StartsWithNoCase(const std::string& value, const char* prefix)
    {
        const std::string lowerValue = LowerAscii(value);
        const std::string lowerPrefix = LowerAscii(prefix ? std::string(prefix) : std::string());
        return lowerValue.rfind(lowerPrefix, 0) == 0;
    }

    int TrailingSurfaceNumber(const std::string& value, const char* prefix)
    {
        const std::string lower = LowerAscii(value);
        const std::string lowerPrefix = LowerAscii(prefix ? std::string(prefix) : std::string());
        if (lower.rfind(lowerPrefix, 0) != 0) return 100000;
        const std::string suffix = lower.substr(lowerPrefix.size());
        if (suffix.empty()) return 0;
        bool numeric = true;
        for (char ch : suffix)
        {
            if (!std::isdigit(static_cast<unsigned char>(ch)))
            {
                numeric = false;
                break;
            }
        }
        if (numeric)
        {
            return std::atoi(suffix.c_str());
        }
        if (suffix.size() == 1 && suffix[0] >= 'a' && suffix[0] <= 'z')
        {
            return 1000 + (suffix[0] - 'a');
        }
        return 2000;
    }

    void AddUniqueSurfaceName(std::vector<std::string>& names, const std::string& name, const char* prefix)
    {
        if (name.empty()) return;
        if (!StartsWithNoCase(name, prefix)) return;
        if (std::find(names.begin(), names.end(), name) == names.end())
        {
            names.push_back(name);
        }
    }

    void ScanSurfaceTextureDirectory(const std::string& dir, const char* prefix, std::vector<std::string>& names)
    {
        if (dir.empty()) return;
        const std::string pattern = JoinPath(dir, std::string(prefix) + "*");
        WIN32_FIND_DATAA data{};
        HANDLE findHandle = FindFirstFileA(pattern.c_str(), &data);
        if (findHandle == INVALID_HANDLE_VALUE) return;
        do
        {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                AddUniqueSurfaceName(names, data.cFileName, prefix);
            }
        } while (FindNextFileA(findHandle, &data));
        FindClose(findHandle);
    }

    std::vector<std::string> BuildSurfaceTextureNameList(const char* prefix)
    {
        std::vector<std::string> names;
        const std::string configuredTxtsDir = GetConfiguredTextureFolder();
        ScanSurfaceTextureDirectory(configuredTxtsDir, prefix, names);
        const std::string mapDir = DirName(g_app.document.sourcePath);
        const std::string projectDir = DirName(mapDir);
        ScanSurfaceTextureDirectory(JoinPath(mapDir, "txts"), prefix, names);
        ScanSurfaceTextureDirectory(JoinPath(projectDir, "txts"), prefix, names);
        ScanSurfaceTextureDirectory("txts", prefix, names);

        // Fallbacks keep the picker useful before a map is saved/opened or when
        // the editor is launched outside the game data folder.
        if (names.empty())
        {
            const std::array<const char*, 8> suffixes{{"1", "2", "3", "4", "5", "6", "a", "b"}};
            for (const char* suffix : suffixes)
            {
                std::string candidate = std::string(prefix) + suffix;
                std::string resolved;
                if (ResolveTexturePath(candidate, resolved))
                {
                    AddUniqueSurfaceName(names, candidate, prefix);
                }
            }
        }
        if (names.empty())
        {
            names.push_back(std::string(prefix) + "1");
        }

        std::sort(names.begin(), names.end(), [prefix](const std::string& a, const std::string& b)
        {
            const int na = TrailingSurfaceNumber(a, prefix);
            const int nb = TrailingSurfaceNumber(b, prefix);
            if (na != nb) return na < nb;
            return LowerAscii(a) < LowerAscii(b);
        });
        return names;
    }

    void RefreshSurfaceTextureChoices()
    {
        g_app.floorTextureNames = BuildSurfaceTextureNameList("floor");
        g_app.ceilingTextureNames = BuildSurfaceTextureNameList("roof");
        g_app.activeFloorTextureChoice = ClampValue(g_app.activeFloorTextureChoice, 0, MaxValue(0, static_cast<int>(g_app.floorTextureNames.size()) - 1));
        g_app.activeCeilingTextureChoice = ClampValue(g_app.activeCeilingTextureChoice, 0, MaxValue(0, static_cast<int>(g_app.ceilingTextureNames.size()) - 1));
        g_app.activeFloorTextureIndex = ClampValue(g_app.activeFloorTextureIndex, 0, 19);
        g_app.activeCeilingTextureIndex = ClampValue(g_app.activeCeilingTextureIndex, 0, 19);
    }

    bool IsLikelyTextureSlotFileName(const std::string& name)
    {
        if (name.empty()) return false;
        if (name == "." || name == "..") return false;
        const std::string lower = LowerAscii(name);
        if (StartsWithNoCase(lower, "floor") || StartsWithNoCase(lower, "roof")) return false;
        if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".bak") return false;
        if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".txt") return false;
        if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".png") return false;
        if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".jpg") return false;
        if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".jpeg") return false;
        if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".xml") return false;
        return true;
    }

    void AddUniqueTextureSlotName(std::vector<std::string>& names, const std::string& name)
    {
        if (!IsLikelyTextureSlotFileName(name)) return;
        const std::string lowerName = LowerAscii(name);
        const auto it = std::find_if(names.begin(), names.end(), [&](const std::string& existing)
        {
            return LowerAscii(existing) == lowerName;
        });
        if (it == names.end())
        {
            names.push_back(name);
        }
    }

    void ScanTextureSlotDirectory(const std::string& dir, std::vector<std::string>& names)
    {
        if (dir.empty()) return;
        const std::string pattern = JoinPath(dir, "*");
        WIN32_FIND_DATAA data{};
        HANDLE findHandle = FindFirstFileA(pattern.c_str(), &data);
        if (findHandle == INVALID_HANDLE_VALUE) return;
        do
        {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                AddUniqueTextureSlotName(names, data.cFileName);
            }
        } while (FindNextFileA(findHandle, &data));
        FindClose(findHandle);
    }

    std::vector<std::string> BuildTextureSlotNameList()
    {
        std::vector<std::string> names;
        ScanTextureSlotDirectory(GetConfiguredTextureFolder(), names);

        if (names.empty())
        {
            const std::string mapDir = DirName(g_app.document.sourcePath);
            const std::string projectDir = DirName(mapDir);
            ScanTextureSlotDirectory(JoinPath(mapDir, "txts"), names);
            ScanTextureSlotDirectory(JoinPath(projectDir, "txts"), names);
            ScanTextureSlotDirectory("txts", names);
        }

        std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b)
        {
            return LowerAscii(a) < LowerAscii(b);
        });
        return names;
    }

    const std::vector<std::string>& ActiveSurfaceTextureList()
    {
        return g_app.activeSurfaceTextureTarget == SurfaceTextureTarget::Floor
            ? g_app.floorTextureNames
            : g_app.ceilingTextureNames;
    }

    int GetActiveSurfaceTextureIndex()
    {
        return ClampValue(g_app.activeSurfaceTextureTarget == SurfaceTextureTarget::Floor
            ? g_app.activeFloorTextureIndex
            : g_app.activeCeilingTextureIndex, 0, 19);
    }

    void SetActiveSurfaceTextureIndex(int textureIndex)
    {
        textureIndex = ClampValue(textureIndex, 0, 19);
        if (g_app.activeSurfaceTextureTarget == SurfaceTextureTarget::Floor)
        {
            g_app.activeFloorTextureIndex = textureIndex;
        }
        else
        {
            g_app.activeCeilingTextureIndex = textureIndex;
        }
    }

    int GetActiveSurfaceTextureChoice()
    {
        const int maxChoice = MaxValue(0, static_cast<int>(ActiveSurfaceTextureList().size()) - 1);
        return ClampValue(g_app.activeSurfaceTextureTarget == SurfaceTextureTarget::Floor
            ? g_app.activeFloorTextureChoice
            : g_app.activeCeilingTextureChoice, 0, maxChoice);
    }

    void SetActiveSurfaceTextureChoice(int textureChoice)
    {
        const int maxChoice = MaxValue(0, static_cast<int>(ActiveSurfaceTextureList().size()) - 1);
        textureChoice = ClampValue(textureChoice, 0, maxChoice);
        if (g_app.activeSurfaceTextureTarget == SurfaceTextureTarget::Floor)
        {
            g_app.activeFloorTextureChoice = textureChoice;
        }
        else
        {
            g_app.activeCeilingTextureChoice = textureChoice;
        }
    }

    bool StepActiveSurfaceTextureChoice(int delta)
    {
        const int count = static_cast<int>(ActiveSurfaceTextureList().size());
        if (count <= 0) return false;
        const int oldChoice = GetActiveSurfaceTextureChoice();
        const int newChoice = ClampValue(oldChoice + delta, 0, count - 1);
        if (newChoice == oldChoice) return false;
        SetActiveSurfaceTextureChoice(newChoice);
        return true;
    }

    std::string GetActiveSurfaceTextureName()
    {
        const auto& names = ActiveSurfaceTextureList();
        if (names.empty()) return g_app.activeSurfaceTextureTarget == SurfaceTextureTarget::Floor ? "floor1" : "roof1";
        return names[GetActiveSurfaceTextureChoice()];
    }

    std::wstring ActiveSurfaceTextureLabel()
    {
        return g_app.activeSurfaceTextureTarget == SurfaceTextureTarget::Floor ? L"Floor" : L"Ceiling";
    }

    std::string GetSurfaceTextureNameFor(SurfaceTextureTarget target)
    {
        const auto& names = target == SurfaceTextureTarget::Floor ? g_app.floorTextureNames : g_app.ceilingTextureNames;
        if (names.empty()) return target == SurfaceTextureTarget::Floor ? "floor1" : "roof1";
        const int choice = ClampValue(target == SurfaceTextureTarget::Floor
            ? g_app.activeFloorTextureChoice
            : g_app.activeCeilingTextureChoice, 0, static_cast<int>(names.size()) - 1);
        return names[choice];
    }

    int FindSurfaceTextureChoiceByName(const std::vector<std::string>& names, const std::string& wanted)
    {
        const std::string wantedLower = LowerAscii(wanted);
        for (int i = 0; i < static_cast<int>(names.size()); ++i)
        {
            if (LowerAscii(names[i]) == wantedLower)
            {
                return i;
            }
        }
        return -1;
    }

    std::string SurfaceTextureMetaPathForMapPath(const std::string& path)
    {
        return path.empty() ? std::string() : (path + ".zgmeta");
    }

    bool LoadSurfaceTextureMetadataForMap(const std::string& path)
    {
        const std::string metaPath = SurfaceTextureMetaPathForMapPath(path);
        if (metaPath.empty()) return false;

        std::ifstream file(metaPath);
        if (!file) return false;

        std::string floorName;
        std::string ceilingName;
        std::string line;
        while (std::getline(file, line))
        {
            if (line.rfind("FloorTexture=", 0) == 0)
            {
                floorName = line.substr(13);
            }
            else if (line.rfind("CeilingTexture=", 0) == 0)
            {
                ceilingName = line.substr(15);
            }
        }

        bool changed = false;
        if (!floorName.empty())
        {
            int idx = FindSurfaceTextureChoiceByName(g_app.floorTextureNames, floorName);
            std::string resolvedPath;
            if (idx < 0 && ResolveTexturePath(floorName, resolvedPath))
            {
                g_app.floorTextureNames.push_back(floorName);
                idx = static_cast<int>(g_app.floorTextureNames.size()) - 1;
            }
            if (idx >= 0)
            {
                g_app.activeFloorTextureChoice = idx;
                changed = true;
            }
        }
        if (!ceilingName.empty())
        {
            int idx = FindSurfaceTextureChoiceByName(g_app.ceilingTextureNames, ceilingName);
            std::string resolvedPath;
            if (idx < 0 && ResolveTexturePath(ceilingName, resolvedPath))
            {
                g_app.ceilingTextureNames.push_back(ceilingName);
                idx = static_cast<int>(g_app.ceilingTextureNames.size()) - 1;
            }
            if (idx >= 0)
            {
                g_app.activeCeilingTextureChoice = idx;
                changed = true;
            }
        }
        return changed;
    }

    bool SaveSurfaceTextureMetadataForMap(const std::string& path, std::string& errorMessage)
    {
        const std::string metaPath = SurfaceTextureMetaPathForMapPath(path);
        if (metaPath.empty()) return true;

        std::ofstream file(metaPath, std::ios::binary);
        if (!file)
        {
            errorMessage = "Could not create surface texture metadata file: " + metaPath;
            return false;
        }

        file << "ZGloomEditorMeta=1\n";
        file << "FloorTexture=" << GetSurfaceTextureNameFor(SurfaceTextureTarget::Floor) << "\n";
        file << "CeilingTexture=" << GetSurfaceTextureNameFor(SurfaceTextureTarget::Ceiling) << "\n";
        if (!file)
        {
            errorMessage = "Failed while writing surface texture metadata file: " + metaPath;
            return false;
        }
        return true;
    }

    void SyncActiveWallTextureFromSelectedWall()
    {
        if (g_app.insertMode == InsertMode::Wall || !IsSelectedWall())
        {
            return;
        }

        const auto& zone = g_app.document.zones[g_app.selectedZone];
        g_app.activeWallTextureMode = GetWallTextureModeForZone(zone);
        const int band = GetActiveTextureBandForZone(zone);
        int textureIndex = ClampValue(static_cast<int>(zone.textures[band]), 0, 159);
        mapfmt::AnimationEntry anim{};
        if (ResolveAnimationForTextureIndex(textureIndex, anim))
        {
            textureIndex = ClampValue(static_cast<int>(anim.first), 0, 159);
        }
        g_app.activeWallTextureSlot = ClampValue(textureIndex / 20, 0, static_cast<int>(mapfmt::MapDocument::kTextureSlotCount) - 1);
        g_app.activeWallTextureStrip = ClampValue(textureIndex % 20, 0, 19);
    }

    int GetActiveWallTextureIndex()
    {
        const int slot = ClampValue(g_app.activeWallTextureSlot, 0, static_cast<int>(mapfmt::MapDocument::kTextureSlotCount) - 1);
        const int strip = ClampValue(g_app.activeWallTextureStrip, 0, 19);
        return slot * 20 + strip;
    }

    uint8_t MakeTextureIndex(int slot, int strip)
    {
        slot = ClampValue(slot, 0, static_cast<int>(mapfmt::MapDocument::kTextureSlotCount) - 1);
        strip = ClampValue(strip, 0, 19);
        return static_cast<uint8_t>(slot * 20 + strip);
    }

    std::string NormalizeTextureNameForAnimLookup(std::string name)
    {
        std::replace(name.begin(), name.end(), '\\', '/');
        const size_t slash = name.find_last_of('/');
        if (slash != std::string::npos)
        {
            name.erase(0, slash + 1);
        }
        const size_t dot = name.find_last_of('.');
        if (dot != std::string::npos)
        {
            name.erase(dot);
        }
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch)
        {
            return static_cast<char>(std::tolower(ch));
        });
        return name;
    }

    bool TextureSlotNameMatches(int slot, const char* expected)
    {
        slot = ClampValue(slot, 0, static_cast<int>(mapfmt::MapDocument::kTextureSlotCount) - 1);
        return NormalizeTextureNameForAnimLookup(g_app.document.textureNames[slot]) == expected;
    }

    struct KnownTextureAnimation
    {
        const char* textureName;
        int firstSectionOneBased;
        int frames;
    };

    const std::array<KnownTextureAnimation, 3> kKnownTextureAnimations = {{
        // Gloom's common animated wall bands are not always present in every
        // map's animation block. The editor therefore knows the common texture
        // file/section ranges and injects the missing animation entry on use.
        { "txt1_1", 11, 4 },
        { "txt1_3", 11, 4 },
        { "txt1_2", 16, 4 },
    }};

    struct SmartTextureAnimationCacheEntry
    {
        bool found = false;
        mapfmt::AnimationEntry animation{};
    };

    struct TextureStripSimilarityStats
    {
        double averageDiff = 999.0;
        double rmsDiff = 999.0;
        double similarRatio = 0.0;
        double changedRatio = 1.0;
        double strongChangedRatio = 1.0;
    };

    TextureStripSimilarityStats CompareTextureStrips(const TexturePreviewImage& image, int firstStrip, int secondStrip)
    {
        TextureStripSimilarityStats stats{};
        if (image.width <= 0 || image.height <= 0 || image.pixels.empty()) return stats;

        const int firstX = firstStrip * 64;
        const int secondX = secondStrip * 64;
        if (firstX < 0 || secondX < 0 || firstX + 64 > image.width || secondX + 64 > image.width)
        {
            return stats;
        }

        double total = 0.0;
        double totalSq = 0.0;
        int samples = 0;
        int similar = 0;
        int changed = 0;
        int strongChanged = 0;

        // Check the whole 64px wall section, not just a few columns. Animated
        // Gloom wall bands are usually the same wall with only a rotor/screen/
        // light area changing; unrelated wall sections differ across much more
        // of the image.
        for (int y = 0; y < image.height; y += 2)
        {
            const size_t row = static_cast<size_t>(y) * static_cast<size_t>(image.width);
            for (int x = 0; x < 64; x += 2)
            {
                const uint32_t a = image.pixels[row + static_cast<size_t>(firstX + x)];
                const uint32_t b = image.pixels[row + static_cast<size_t>(secondX + x)];
                const int ar = static_cast<int>((a >> 16) & 0xFFu);
                const int ag = static_cast<int>((a >> 8) & 0xFFu);
                const int ab = static_cast<int>(a & 0xFFu);
                const int br = static_cast<int>((b >> 16) & 0xFFu);
                const int bg = static_cast<int>((b >> 8) & 0xFFu);
                const int bb = static_cast<int>(b & 0xFFu);
                const double diff = static_cast<double>(std::abs(ar - br) + std::abs(ag - bg) + std::abs(ab - bb)) / 3.0;
                total += diff;
                totalSq += diff * diff;
                if (diff <= 10.0) ++similar;
                if (diff >= 22.0) ++changed;
                if (diff >= 56.0) ++strongChanged;
                ++samples;
            }
        }

        if (samples <= 0) return stats;
        stats.averageDiff = total / static_cast<double>(samples);
        stats.rmsDiff = std::sqrt(totalSq / static_cast<double>(samples));
        stats.similarRatio = static_cast<double>(similar) / static_cast<double>(samples);
        stats.changedRatio = static_cast<double>(changed) / static_cast<double>(samples);
        stats.strongChangedRatio = static_cast<double>(strongChanged) / static_cast<double>(samples);
        return stats;
    }

    bool TextureStripPairLooksLikeAnimationFrame(const TextureStripSimilarityStats& stats)
    {
        // Very close but not literally identical. This rejects the previous false
        // positives where neighbouring but unrelated wall panels were grouped
        // only because their average colour distance was not huge.
        return stats.averageDiff >= 1.0 &&
               stats.averageDiff <= 24.0 &&
               stats.rmsDiff <= 34.0 &&
               stats.similarRatio >= 0.56 &&
               stats.changedRatio <= 0.38 &&
               stats.strongChangedRatio <= 0.16;
    }

    bool TextureStripPairLooksLikeSameStaticWall(const TextureStripSimilarityStats& stats)
    {
        return stats.averageDiff < 1.0 ||
               (stats.averageDiff < 2.0 && stats.similarRatio > 0.92 && stats.changedRatio < 0.025);
    }

    bool TextureStripPairLooksLikeSameAnimationCluster(const TextureStripSimilarityStats& stats)
    {
        return stats.averageDiff <= 26.0 &&
               stats.rmsDiff <= 38.0 &&
               stats.similarRatio >= 0.52 &&
               stats.changedRatio <= 0.42 &&
               stats.strongChangedRatio <= 0.18;
    }

    bool DetectSmartTextureAnimationInImage(const TexturePreviewImage& image, int selectedStrip, int& outFirstStrip, int& outFrames)
    {
        const int stripCount = ClampValue(image.width / 64, 0, 20);
        selectedStrip = ClampValue(selectedStrip, 0, MaxValue(0, stripCount - 1));
        if (stripCount < 3 || image.height <= 0 || image.pixels.empty()) return false;

        struct Candidate
        {
            int first = 0;
            int frames = 0;
            double score = 999999.0;
        };

        Candidate best{};
        bool found = false;
        const int frameCounts[] = { 4, 3 };
        for (int frames : frameCounts)
        {
            if (frames > stripCount) continue;
            const int minStart = MaxValue(0, selectedStrip - frames + 1);
            const int maxStart = MinValue(selectedStrip, stripCount - frames);
            for (int start = minStart; start <= maxStart; ++start)
            {
                double totalAdjacent = 0.0;
                double maxAdjacent = 0.0;
                double minSimilar = 1.0;
                double maxChanged = 0.0;
                double maxStrongChanged = 0.0;
                bool allAdjacentValid = true;
                bool hasVisibleMotion = false;

                for (int i = 0; i < frames - 1; ++i)
                {
                    const TextureStripSimilarityStats stats = CompareTextureStrips(image, start + i, start + i + 1);
                    if (!TextureStripPairLooksLikeAnimationFrame(stats))
                    {
                        allAdjacentValid = false;
                        break;
                    }
                    totalAdjacent += stats.averageDiff;
                    maxAdjacent = MaxValue(maxAdjacent, stats.averageDiff);
                    minSimilar = MinValue(minSimilar, stats.similarRatio);
                    maxChanged = MaxValue(maxChanged, stats.changedRatio);
                    maxStrongChanged = MaxValue(maxStrongChanged, stats.strongChangedRatio);
                    if (!TextureStripPairLooksLikeSameStaticWall(stats))
                    {
                        hasVisibleMotion = true;
                    }
                }
                if (!allAdjacentValid || !hasVisibleMotion)
                {
                    continue;
                }

                bool allFromFirstValid = true;
                double maxFromFirst = 0.0;
                for (int i = 1; i < frames; ++i)
                {
                    const TextureStripSimilarityStats stats = CompareTextureStrips(image, start, start + i);
                    if (!TextureStripPairLooksLikeSameAnimationCluster(stats))
                    {
                        allFromFirstValid = false;
                        break;
                    }
                    maxFromFirst = MaxValue(maxFromFirst, stats.averageDiff);
                    minSimilar = MinValue(minSimilar, stats.similarRatio);
                    maxChanged = MaxValue(maxChanged, stats.changedRatio);
                    maxStrongChanged = MaxValue(maxStrongChanged, stats.strongChangedRatio);
                }
                if (!allFromFirstValid)
                {
                    continue;
                }

                // Reject long runs of almost identical wall sections. A true
                // animation is a compact group of 3 or 4 frames; if the section
                // right before/after looks equally close, it is probably just a
                // repeated wall pattern and not an animation band.
                int softBoundaryMatches = 0;
                if (start > 0)
                {
                    const TextureStripSimilarityStats before = CompareTextureStrips(image, start - 1, start);
                    if (TextureStripPairLooksLikeSameAnimationCluster(before))
                    {
                        ++softBoundaryMatches;
                    }
                }
                if (start + frames < stripCount)
                {
                    const TextureStripSimilarityStats after = CompareTextureStrips(image, start + frames - 1, start + frames);
                    if (TextureStripPairLooksLikeSameAnimationCluster(after))
                    {
                        ++softBoundaryMatches;
                    }
                }
                if (softBoundaryMatches >= 2)
                {
                    continue;
                }

                const double averageAdjacent = totalAdjacent / static_cast<double>(frames - 1);
                double score = averageAdjacent + (maxAdjacent * 0.35) + (maxFromFirst * 0.25);
                score += maxChanged * 18.0;
                score += maxStrongChanged * 28.0;
                score -= minSimilar * 10.0;
                if (frames == 4) score -= 4.0;
                if (softBoundaryMatches == 1) score += 8.0;

                if (!found || score < best.score)
                {
                    found = true;
                    best = Candidate{ start, frames, score };
                }
            }
        }

        if (!found) return false;
        outFirstStrip = best.first;
        outFrames = best.frames;
        return true;
    }

    bool ResolveSmartTextureAnimation(int textureIndex, mapfmt::AnimationEntry& outAnimation)
    {
        UNREFERENCED_PARAMETER(textureIndex);
        UNREFERENCED_PARAMETER(outAnimation);
        // Disabled on purpose: visual auto-detection was too unreliable for
        // Gloom's wall strips. Animations are now either explicit map entries,
        // the small known compatibility table below, or user-marked ranges in
        // the Texture Slots dialog.
        return false;
    }

    bool ResolveKnownTextureAnimation(int textureIndex, mapfmt::AnimationEntry& outAnimation)
    {
        textureIndex = ClampValue(textureIndex, 0, 159);
        const int slot = textureIndex / 20;
        const int strip = textureIndex % 20;
        for (const auto& known : kKnownTextureAnimations)
        {
            if (!TextureSlotNameMatches(slot, known.textureName))
            {
                continue;
            }
            const int firstStrip = ClampValue(known.firstSectionOneBased - 1, 0, 19);
            const int lastStripExclusive = firstStrip + MaxValue(1, known.frames);
            if (strip >= firstStrip && strip < lastStripExclusive)
            {
                outAnimation.frames = static_cast<uint16_t>(MaxValue(1, known.frames));
                outAnimation.first = static_cast<uint16_t>(slot * 20 + firstStrip);
                outAnimation.delay = 4;
                outAnimation.current = 0;
                return true;
            }
        }
        return false;
    }

    bool ResolveAnimationForTextureIndex(int textureIndex, mapfmt::AnimationEntry& outAnimation)
    {
        textureIndex = ClampValue(textureIndex, 0, 159);
        // Prefer explicit texture-name knowledge for the Gloom texture bands.
        // Some maps either omit these entries or use section numbering that is
        // easy to misread in an editor. The known table keeps txt1_1 section
        // 11..14 anchored to section 11 even when a loaded table is ambiguous.
        if (ResolveKnownTextureAnimation(textureIndex, outAnimation))
        {
            return true;
        }
        for (const auto& anim : g_app.document.animations)
        {
            if (anim.Contains(textureIndex))
            {
                outAnimation = anim;
                return true;
            }
        }
        // Manual animation ranges set in the Texture Slots dialog are stored in
        // document.animations above. Do not auto-detect here: the visual heuristic
        // was too risky and could group unrelated wall/window panels.
        return false;
    }

    uint16_t GuessAnimationDelayForEntry(const mapfmt::AnimationEntry& wanted)
    {
        for (const auto& anim : g_app.document.animations)
        {
            if (anim.frames == wanted.frames && anim.delay > 0)
            {
                return anim.delay;
            }
        }
        for (const auto& anim : g_app.document.animations)
        {
            if (anim.delay > 0)
            {
                return anim.delay;
            }
        }
        return 4;
    }

    void AppendBE16(std::vector<uint8_t>& out, uint16_t value)
    {
        out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(value & 0xFF));
    }

    void RebuildAnimationBlockFromDocumentAnimations()
    {
        auto& animations = g_app.document.animations;
        std::sort(animations.begin(), animations.end(), [](const mapfmt::AnimationEntry& a, const mapfmt::AnimationEntry& b)
        {
            if (a.first != b.first) return a.first < b.first;
            return a.frames < b.frames;
        });
        animations.erase(std::unique(animations.begin(), animations.end(), [](const mapfmt::AnimationEntry& a, const mapfmt::AnimationEntry& b)
        {
            return a.first == b.first && a.frames == b.frames;
        }), animations.end());

        g_app.document.animationBlock.clear();
        for (const auto& anim : animations)
        {
            if (anim.frames == 0 || anim.first >= 160 || anim.first + anim.frames > 160)
            {
                continue;
            }
            AppendBE16(g_app.document.animationBlock, anim.frames);
            AppendBE16(g_app.document.animationBlock, anim.first);
            AppendBE16(g_app.document.animationBlock, anim.delay);
            AppendBE16(g_app.document.animationBlock, anim.current);
        }
        // Terminator entry used by the original loader.
        for (int i = 0; i < 8; ++i)
        {
            g_app.document.animationBlock.push_back(0);
        }
    }

    bool EnsureAnimationEntryForTextureIndex(int textureIndex)
    {
        textureIndex = ClampValue(textureIndex, 0, 159);
        for (const auto& anim : g_app.document.animations)
        {
            if (anim.Contains(textureIndex))
            {
                return false;
            }
        }

        mapfmt::AnimationEntry resolved{};
        if (!ResolveKnownTextureAnimation(textureIndex, resolved))
        {
            // Static or manually configured texture: manual entries are already
            // present in document.animations, so no heuristic injection here.
            return false;
        }
        for (const auto& anim : g_app.document.animations)
        {
            if (anim.first == resolved.first && anim.frames == resolved.frames)
            {
                return false;
            }
        }
        resolved.delay = GuessAnimationDelayForEntry(resolved);
        g_app.document.animations.push_back(resolved);
        RebuildAnimationBlockFromDocumentAnimations();
        return true;
    }

    int NormalizeStripToAnimationStart(int slot, int strip)
    {
        const int textureIndex = static_cast<int>(MakeTextureIndex(slot, strip));
        mapfmt::AnimationEntry anim{};
        if (ResolveAnimationForTextureIndex(textureIndex, anim))
        {
            // Keep animation groups intact: clicking frame 12 of an 11-14
            // animation selects the first frame, because the game animates the
            // texture pointer starting at that first texture index.
            if ((anim.first / 20) == ClampValue(slot, 0, static_cast<int>(mapfmt::MapDocument::kTextureSlotCount) - 1))
            {
                return ClampValue(static_cast<int>(anim.first % 20), 0, 19);
            }
        }
        return ClampValue(strip, 0, 19);
    }

    int GetActiveWallAssignedTextureIndex()
    {
        const int textureIndex = GetActiveWallTextureIndex();
        mapfmt::AnimationEntry anim{};
        if (ResolveAnimationForTextureIndex(textureIndex, anim))
        {
            return ClampValue(static_cast<int>(anim.first), 0, 159);
        }
        return ClampValue(textureIndex, 0, 159);
    }

    std::wstring FormatAnimationInfoForTexture(int textureIndex)
    {
        mapfmt::AnimationEntry anim{};
        if (ResolveAnimationForTextureIndex(textureIndex, anim))
        {
            std::wstringstream ss;
            ss << L"Animated texture: sections " << ((anim.first % 20) + 1)
               << L"-" << ((anim.Last() % 20) + 1)
               << L"  (IDs " << anim.first << L"-" << anim.Last() << L")";
            if (std::none_of(g_app.document.animations.begin(), g_app.document.animations.end(), [&](const mapfmt::AnimationEntry& mapAnim)
                { return mapAnim.first == anim.first && mapAnim.frames == anim.frames; }))
            {
                ss << L"  [will be added to map]";
            }
            return ss.str();
        }
        return L"";
    }


    bool WallLooksLikeFaultyRepeatedTexture(const mapfmt::Zone& zone)
    {
        if (zone.ztype != static_cast<int16_t>(mapfmt::ZoneType::Wall))
        {
            return false;
        }
        if (zone.sc <= 16)
        {
            return false;
        }
        return std::all_of(zone.textures.begin() + 1, zone.textures.end(),
            [&](uint8_t value) { return value == zone.textures[0]; });
    }
    void ApplyActiveWallTextureToZoneBand(mapfmt::Zone& zone, int band)
    {
        if (zone.ztype != static_cast<int16_t>(mapfmt::ZoneType::Wall))
        {
            return;
        }

        if (WallLooksLikeFaultyRepeatedTexture(zone))
        {
            InitializeNewWallTextureSequence(zone);
            g_app.previewTextureBand = 0;
            return;
        }

        EnsureAnimationEntryForTextureIndex(GetActiveWallTextureIndex());
        const uint8_t texture = static_cast<uint8_t>(ClampValue(GetActiveWallAssignedTextureIndex(), 0, 159));

        if (g_app.activeWallTextureMode == WallTextureMode::Repeat1To1 ||
            g_app.activeWallTextureMode == WallTextureMode::Clip2Of8)
        {
            for (uint8_t& value : zone.textures)
            {
                value = texture;
            }
        }
        else
        {
            const int activeBandCount = GetZonePreviewSlotCount(zone);
            band = ClampValue(band, 0, activeBandCount - 1);
            zone.textures[band] = texture;
        }

        ApplyWallTextureModeToZone(zone, g_app.activeWallTextureMode);
    }

    void InitializeNewWallTextureSequence(mapfmt::Zone& zone)
    {
        if (zone.ztype != static_cast<int16_t>(mapfmt::ZoneType::Wall))
        {
            return;
        }

        const int slot = ClampValue(g_app.activeWallTextureSlot, 0, static_cast<int>(mapfmt::MapDocument::kTextureSlotCount) - 1);
        const int strip = ClampValue(g_app.activeWallTextureStrip, 0, 19);

        int textureIndex = static_cast<int>(MakeTextureIndex(slot, strip));
        mapfmt::AnimationEntry anim{};
        if (ResolveAnimationForTextureIndex(textureIndex, anim))
        {
            EnsureAnimationEntryForTextureIndex(textureIndex);
            textureIndex = ClampValue(static_cast<int>(anim.first), 0, 159);
        }

        zone.textures[0] = static_cast<uint8_t>(ClampValue(textureIndex, 0, 159));
        ApplyWallTextureModeToZone(zone, g_app.activeWallTextureMode);
    }

    int CountZonesOfType(mapfmt::ZoneType type)
    {
        const int zoneType = static_cast<int>(type);
        return static_cast<int>(std::count_if(g_app.document.zones.begin(), g_app.document.zones.end(),
            [&](const mapfmt::Zone& zone) { return zone.ztype == zoneType; }));
    }

    int CountEventCommandsOfType(mapfmt::CommandType type)
    {
        int count = 0;
        for (const auto& script : g_app.document.events)
        {
            count += static_cast<int>(std::count_if(script.commands.begin(), script.commands.end(),
                [&](const mapfmt::EventCommand& cmd) { return cmd.type == type; }));
        }
        return count;
    }

    int CountMonsterSpawns()
    {
        return CountEventCommandsOfType(mapfmt::CommandType::AddMonster);
    }

    int ComputeMonsterSpawnMarkerIndex(int eventIndex, int commandIndex)
    {
        int markerIndex = 0;
        for (int e = 0; e < static_cast<int>(g_app.document.events.size()); ++e)
        {
            const auto& commands = g_app.document.events[e].commands;
            for (int c = 0; c < static_cast<int>(commands.size()); ++c)
            {
                if (commands[c].type != mapfmt::CommandType::AddMonster) continue;
                if (e == eventIndex && c == commandIndex) return markerIndex;
                ++markerIndex;
            }
        }
        return -1;
    }

    const mapfmt::EventCommand* GetMonsterSpawnCommand(const MonsterSpawnSelection& selection)
    {
        if (!selection.IsSet()) return nullptr;
        if (selection.eventIndex < 0 || selection.eventIndex >= static_cast<int>(g_app.document.events.size())) return nullptr;
        const auto& commands = g_app.document.events[selection.eventIndex].commands;
        if (selection.commandIndex < 0 || selection.commandIndex >= static_cast<int>(commands.size())) return nullptr;
        if (commands[selection.commandIndex].type != mapfmt::CommandType::AddMonster) return nullptr;
        return &commands[selection.commandIndex];
    }

    bool IsSelectedMonsterSpawnValid()
    {
        return GetMonsterSpawnCommand(g_app.selectedMonsterSpawn) != nullptr;
    }

    void ClearSelectedMonsterSpawn()
    {
        g_app.selectedMonsterSpawn.Clear();
    }

    void ClearSelectedTeleportTarget()
    {
        g_app.selectedTeleportTarget.Clear();
    }

    const mapfmt::EventCommand* GetSelectedMonsterSpawnCommand()
    {
        return GetMonsterSpawnCommand(g_app.selectedMonsterSpawn);
    }

    mapfmt::EventCommand* GetSelectedMonsterSpawnCommandMutable()
    {
        if (!g_app.selectedMonsterSpawn.IsSet()) return nullptr;
        if (g_app.selectedMonsterSpawn.eventIndex < 0 || g_app.selectedMonsterSpawn.eventIndex >= static_cast<int>(g_app.document.events.size())) return nullptr;
        auto& commands = g_app.document.events[g_app.selectedMonsterSpawn.eventIndex].commands;
        if (g_app.selectedMonsterSpawn.commandIndex < 0 || g_app.selectedMonsterSpawn.commandIndex >= static_cast<int>(commands.size())) return nullptr;
        if (commands[g_app.selectedMonsterSpawn.commandIndex].type != mapfmt::CommandType::AddMonster) return nullptr;
        return &commands[g_app.selectedMonsterSpawn.commandIndex];
    }

    bool RotateSelectedMonsterSpawnByDegrees(int degrees)
    {
        mapfmt::EventCommand* command = GetSelectedMonsterSpawnCommandMutable();
        if (!command) return false;

        const int absStep = MaxValue(1, static_cast<int>(std::lround((std::abs(degrees) * 256.0) / 360.0)));
        const int step = degrees < 0 ? -absStep : absStep;
        PushUndoSnapshot();
        command->params[4] = static_cast<int16_t>((static_cast<int>(command->params[4]) + step) & 255);
        MarkDirty();
        RefreshPreviewImage();
        RefreshStatus();
        InvalidateEditorViews();
        return true;
    }

    const mapfmt::EventCommand* GetSelectedTeleportTargetCommand()
    {
        if (!g_app.selectedTeleportTarget.IsSet()) return nullptr;
        if (g_app.selectedTeleportTarget.eventIndex < 0 || g_app.selectedTeleportTarget.eventIndex >= static_cast<int>(g_app.document.events.size())) return nullptr;
        const auto& commands = g_app.document.events[g_app.selectedTeleportTarget.eventIndex].commands;
        if (g_app.selectedTeleportTarget.commandIndex < 0 || g_app.selectedTeleportTarget.commandIndex >= static_cast<int>(commands.size())) return nullptr;
        if (commands[g_app.selectedTeleportTarget.commandIndex].type != mapfmt::CommandType::Teleport) return nullptr;
        return &commands[g_app.selectedTeleportTarget.commandIndex];
    }

    mapfmt::EventCommand* GetSelectedTeleportTargetCommandMutable()
    {
        if (!g_app.selectedTeleportTarget.IsSet()) return nullptr;
        if (g_app.selectedTeleportTarget.eventIndex < 0 || g_app.selectedTeleportTarget.eventIndex >= static_cast<int>(g_app.document.events.size())) return nullptr;
        auto& commands = g_app.document.events[g_app.selectedTeleportTarget.eventIndex].commands;
        if (g_app.selectedTeleportTarget.commandIndex < 0 || g_app.selectedTeleportTarget.commandIndex >= static_cast<int>(commands.size())) return nullptr;
        if (commands[g_app.selectedTeleportTarget.commandIndex].type != mapfmt::CommandType::Teleport) return nullptr;
        return &commands[g_app.selectedTeleportTarget.commandIndex];
    }

    bool IsSelectedTeleportTargetValid()
    {
        return GetSelectedTeleportTargetCommand() != nullptr;
    }

    bool RotateSelectedTeleportTargetByDegrees(int degrees)
    {
        mapfmt::EventCommand* command = GetSelectedTeleportTargetCommandMutable();
        if (!command) return false;

        const int absStep = MaxValue(1, static_cast<int>(std::lround((std::abs(degrees) * 256.0) / 360.0)));
        const int step = degrees < 0 ? -absStep : absStep;
        PushUndoSnapshot();
        command->params[3] = static_cast<int16_t>((static_cast<int>(command->params[3]) + step) & 255);
        MarkDirty();
        RefreshPreviewImage();
        RefreshStatus();
        InvalidateEditorViews();
        return true;
    }

    std::wstring MonsterTypeName(int monsterType)
    {
        switch (monsterType)
        {
        case 0: return L"Player 1 start";
        case 1: return L"Player 2 start";
        case 2: return L"Health pickup";
        case 3: return L"Weapon / ammo pickup";
        case 4: return L"Thermo goggles";
        case 5: return L"Infra goggles";
        case 6: return L"Invisibility";
        case 7: return L"Invincibility";
        case 8: return L"Dragon";
        case 9: return L"Bouncy bonus";
        case 10: return L"Marine";
        case 11: return L"Baldy";
        case 12: return L"Terra";
        case 13: return L"Ghoul";
        case 14: return L"Phantom";
        case 15: return L"Demon";
        case 16: return L"Weapon 1";
        case 17: return L"Weapon 2";
        case 18: return L"Weapon 3";
        case 19: return L"Weapon 4";
        case 20: return L"Weapon 5";
        case 21: return L"Lizard";
        case 22: return L"Deathhead";
        case 23: return L"Troll";
        default:
        {
            std::wstringstream ss;
            ss << L"Unknown object " << monsterType;
            return ss.str();
        }
        }
    }

    std::wstring MonsterTypeCategory(int monsterType)
    {
        if (monsterType == 0 || monsterType == 1) return L"Player start";
        if ((monsterType >= 2 && monsterType <= 7) || monsterType == 9 || (monsterType >= 16 && monsterType <= 20)) return L"Pickup / item";
        if ((monsterType >= 8 && monsterType <= 15) || monsterType >= 21) return L"Enemy / actor";
        return L"Object";
    }

    int MonsterRotationDegrees(int rot)
    {
        const int normalized = rot & 255;
        return static_cast<int>(std::lround((static_cast<double>(normalized) * 360.0) / 256.0)) % 360;
    }

    COLORREF MonsterTypeColor(int monsterType)
    {
        if (monsterType == 0 || monsterType == 1) return RGB(110, 190, 255);
        if ((monsterType >= 2 && monsterType <= 7) || monsterType == 9 || (monsterType >= 16 && monsterType <= 20)) return RGB(90, 220, 120);
        if (monsterType == 8 || monsterType == 15 || monsterType == 22) return RGB(230, 80, 80);
        return RGB(180, 82, 230);
    }


    bool IsLinearZoneType(int ztype)
    {
        return ztype == static_cast<int>(mapfmt::ZoneType::Wall) ||
               ztype == static_cast<int>(mapfmt::ZoneType::MonsterZone) ||
               ztype == static_cast<int>(mapfmt::ZoneType::EventTrigger);
    }

    bool IsLineInsertMode(InsertMode mode)
    {
        return mode == InsertMode::Wall ||
               mode == InsertMode::MonsterZone ||
               mode == InsertMode::EventTrigger ||
               mode == InsertMode::LevelEnd;
    }

    const std::vector<ObjectTypeInfo>& GetObjectTypeTable()
    {
        static const std::vector<ObjectTypeInfo> table = {
            { 0,  L"Player 1 start",       L"Player start", nullptr,          nullptr,          0, false },
            { 1,  L"Player 2 start",       L"Player start", nullptr,          nullptr,          0, false },
            { 2,  L"Health pickup",        L"Pickup / item", "objs/tokens",   "char/pwrups",    2, false },
            { 3,  L"Weapon / ammo pickup", L"Weapon / ammo",   "objs/weapon",   "char/pwrups",    0, false },
            { 4,  L"Thermo goggles",       L"Pickup / item", "objs/tokens",   "char/pwrups",    0, false },
            { 5,  L"Infra goggles",        L"Pickup / item", "objs/tokens",   "char/pwrups",    0, false },
            { 6,  L"Invisibility",         L"Pickup / item", "objs/tokens",   "char/pwrups",    1, false },
            { 7,  L"Invincibility",        L"Pickup / item", "objs/tokens",   "char/pwrups",    2, false },
            { 8,  L"Dragon",               L"Enemy / actor", "objs/dragon",   "char/zombie",    0, true  },
            { 9,  L"Bouncy bonus",         L"Pickup / item", "objs/tokens",   "char/pwrups",    3, false },
            { 10, L"Marine",               L"Enemy / actor", "objs/marine",   "char/troopr",    0, true  },
            { 11, L"Baldy",                L"Enemy / actor", "objs/baldy",    "char/zombi",     0, true  },
            { 12, L"Terra",                L"Enemy / actor", "objs/terra",    "char/fatzo",     0, true  },
            { 13, L"Ghoul",                L"Enemy / actor", "objs/ghoul",    "char/ghost",     0, true  },
            { 14, L"Phantom",              L"Enemy / actor", "objs/phantom",  "char/zomboid",   0, true  },
            { 15, L"Demon",                L"Enemy / actor", "objs/demon",    "char/zocom",     0, true  },
            { 16, L"Weapon/ammo 1",        L"Weapon / ammo",   "objs/weapon1",  "char/pwrups",    0, false },
            { 17, L"Weapon/ammo 2",        L"Weapon / ammo",   "objs/weapon2",  "char/pwrups",    0, false },
            { 18, L"Weapon/ammo 3",        L"Weapon / ammo",   "objs/weapon3",  "char/pwrups",    0, false },
            { 19, L"Weapon/ammo 4",        L"Weapon / ammo",   "objs/weapon4",  "char/pwrups",    0, false },
            { 20, L"Weapon/ammo 5",        L"Weapon / ammo",   "objs/weapon5",  "char/pwrups",    0, false },
            { 21, L"Lizard",               L"Enemy / actor", "objs/lizard",   "char/skinny",    0, true  },
            { 22, L"Deathhead",            L"Enemy / actor", "objs/deathhead","char/dows-head", 0, true  },
            { 23, L"Troll",                L"Enemy / actor", "objs/troll",    "char/james",     0, true  },
        };
        return table;
    }

    const ObjectTypeInfo* GetObjectTypeInfo(int objectType)
    {
        const auto& table = GetObjectTypeTable();
        auto it = std::find_if(table.begin(), table.end(), [objectType](const ObjectTypeInfo& info) { return info.type == objectType; });
        return it == table.end() ? nullptr : &(*it);
    }

    std::wstring ObjectPlacementLabel(int objectType)
    {
        const ObjectTypeInfo* info = GetObjectTypeInfo(objectType);
        if (!info) return MonsterTypeName(objectType);
        return info->name;
    }

    bool IsWeaponObjectType(int objectType)
    {
        return objectType == 3 || (objectType >= 16 && objectType <= 20);
    }

    std::vector<const char*> ExtraObjectSpriteCandidates(int objectType)
    {
        switch (objectType)
        {
        case 3:
            return {
                "objs/weapon", "objs/weapons", "objs/upgrade", "objs/upgrades",
                "objs/pickups", "objs/ammo", "objs/tokens", "char/pwrups"
            };
        case 16:
            return { "objs/weapon1", "objs/wep1", "objs/wpn1", "objs/gun1", "objs/upgrade1", "objs/ammo1", "objs/tokens", "objs/bullet1", "char/pwrups" };
        case 17:
            return { "objs/weapon2", "objs/wep2", "objs/wpn2", "objs/gun2", "objs/upgrade2", "objs/ammo2", "objs/tokens", "objs/bullet2", "char/pwrups" };
        case 18:
            return { "objs/weapon3", "objs/wep3", "objs/wpn3", "objs/gun3", "objs/upgrade3", "objs/ammo3", "objs/tokens", "objs/bullet3", "char/pwrups" };
        case 19:
            return { "objs/weapon4", "objs/wep4", "objs/wpn4", "objs/gun4", "objs/upgrade4", "objs/ammo4", "objs/tokens", "objs/bullet4", "char/pwrups" };
        case 20:
            return { "objs/weapon5", "objs/wep5", "objs/wpn5", "objs/gun5", "objs/upgrade5", "objs/ammo5", "objs/tokens", "objs/bullet5", "char/pwrups" };
        default:
            return {};
        }
    }

    bool PathContainsObjectTokenSource(const std::string& path)
    {
        std::string normalized = path;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return normalized.find("objs/tokens") != std::string::npos ||
               normalized.find("/tokens") != std::string::npos ||
               normalized.find("char/pwrups") != std::string::npos ||
               normalized.find("/pwrups") != std::string::npos;
    }

    bool PathLooksLikeDedicatedWeaponSource(const std::string& path)
    {
        std::string normalized = path;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return normalized.find("/weapon") != std::string::npos ||
               normalized.find("/wep") != std::string::npos ||
               normalized.find("/wpn") != std::string::npos ||
               normalized.find("/gun") != std::string::npos ||
               normalized.find("/upgrade") != std::string::npos ||
               normalized.find("/ammo") != std::string::npos;
    }

    int PreferredObjectFrameForPath(int objectType, const ObjectTypeInfo& info, const std::string& path)
    {
        if (PathContainsObjectTokenSource(path))
        {
            // The generic token/powerup sheet stores several pickups in one file.
            // If no dedicated weapon/ammo file exists, choose a frame that is much closer
            // to the intended object than always showing token frame 0.
            switch (objectType)
            {
            case 2:  return 2; // health
            case 4:  return 0; // thermo
            case 5:  return 0; // infra uses the same base token in the original logic
            case 6:  return 1; // invisibility
            case 7:  return 2; // invincibility
            case 9:  return 3; // bouncy bonus
            case 3:  return 4; // generic weapon/ammo fallback
            case 16: return 4;
            case 17: return 5;
            case 18: return 6;
            case 19: return 7;
            case 20: return 8;
            default: break;
            }
        }
        if (PathLooksLikeDedicatedWeaponSource(path) && objectType >= 16 && objectType <= 20)
        {
            // Some data sets store all weapon upgrade graphics in a single weapon/ammo
            // sheet instead of separate weapon1..weapon5 files. In that case use a
            // stable per-upgrade frame rather than frame 0 for every weapon icon.
            return objectType - 16;
        }
        return info.defaultFrame;
    }

    int TryReadObjectFrameCount(const std::string& path)
    {
        std::string error;
        std::vector<uint8_t> raw;
        if (!LoadMaybeCrmLocal(path, raw, error) || raw.size() < 16) return -1;
        const uint32_t frameShift = ReadBE16Local(raw.data() + 0);
        const uint32_t frameBase = ReadBE16Local(raw.data() + 2);
        const uint32_t frameCount = (frameShift < 16) ? (frameBase << frameShift) : frameBase;
        if (frameCount == 0 || frameCount > 4096) return -1;
        return static_cast<int>(frameCount);
    }

    bool GenerateWeaponFallbackPreviewImage(int objectType, ObjectPreviewImage& outImage)
    {
        if (!IsWeaponObjectType(objectType)) return false;

        const ObjectTypeInfo* info = GetObjectTypeInfo(objectType);
        outImage = {};
        outImage.objectType = objectType;
        outImage.name = info ? info->name : MonsterTypeName(objectType);
        outImage.category = info ? info->category : MonsterTypeCategory(objectType);
        outImage.loadedPath = "generated weapon/ammo fallback";
        outImage.error = "No matching sprite file found; generated a distinct editor preview.";
        outImage.frameIndex = 0;
        outImage.frameCount = 1;
        outImage.width = 64;
        outImage.height = 64;
        outImage.pixels.assign(64 * 64, 0xFF26262Eu);

        auto put = [&](int x, int y, uint32_t col)
        {
            if (x >= 0 && x < 64 && y >= 0 && y < 64)
            {
                outImage.pixels[static_cast<size_t>(y) * 64 + static_cast<size_t>(x)] = col;
            }
        };
        auto fillRect = [&](int x1, int y1, int x2, int y2, uint32_t col)
        {
            for (int y = y1; y <= y2; ++y)
                for (int x = x1; x <= x2; ++x) put(x, y, col);
        };
        auto line = [&](int x1, int y1, int x2, int y2, uint32_t col)
        {
            const int steps = MaxValue(std::abs(x2 - x1), std::abs(y2 - y1));
            for (int i = 0; i <= steps; ++i)
            {
                const double t = steps == 0 ? 0.0 : static_cast<double>(i) / static_cast<double>(steps);
                put(static_cast<int>(std::lround(x1 + (x2 - x1) * t)),
                    static_cast<int>(std::lround(y1 + (y2 - y1) * t)), col);
            }
        };

        const uint32_t accent[] = { 0xFFFFD34Du, 0xFF63D2FFu, 0xFFFF775Eu, 0xFF9CFF6Au, 0xFFD08CFFu };
        const int idx = ClampValue(objectType == 3 ? 0 : objectType - 16, 0, 4);
        const uint32_t c = accent[idx];
        const uint32_t hi = 0xFFFFFFFFu;
        const uint32_t dark = 0xFF111118u;

        // Draw a small pseudo-pickup: base diamond + different barrel/upgrade marks.
        for (int y = 8; y <= 56; ++y)
        {
            for (int x = 8; x <= 56; ++x)
            {
                if (std::abs(x - 32) + std::abs(y - 32) <= 25) put(x, y, c);
                if (std::abs(x - 32) + std::abs(y - 32) == 26) put(x, y, hi);
            }
        }
        fillRect(18, 29, 46, 35, dark);
        fillRect(22, 24, 36, 28, dark);
        fillRect(38, 25, 48, 28, dark);
        fillRect(18, 36, 26, 40, dark);
        for (int n = 0; n <= idx; ++n)
        {
            fillRect(18 + n * 6, 45, 21 + n * 6, 49, hi);
        }
        line(18, 29, 46, 29, hi);
        line(38, 24, 49, 24, hi);
        return true;
    }

    bool GenerateObjectFallbackPreviewImage(int objectType, ObjectPreviewImage& outImage)
    {
        if (GenerateWeaponFallbackPreviewImage(objectType, outImage))
        {
            return true;
        }

        const ObjectTypeInfo* info = GetObjectTypeInfo(objectType);
        outImage = {};
        outImage.objectType = objectType;
        outImage.name = info ? info->name : MonsterTypeName(objectType);
        outImage.category = info ? info->category : MonsterTypeCategory(objectType);
        outImage.loadedPath = "generated editor fallback";
        outImage.error = "No matching sprite file found; generated a distinct editor preview.";
        outImage.frameIndex = 0;
        outImage.frameCount = 1;
        outImage.width = 64;
        outImage.height = 64;
        outImage.pixels.assign(64 * 64, 0xFF26262Eu);

        auto put = [&](int x, int y, uint32_t col)
        {
            if (x >= 0 && x < 64 && y >= 0 && y < 64)
            {
                outImage.pixels[static_cast<size_t>(y) * 64 + static_cast<size_t>(x)] = col;
            }
        };
        auto fillRect = [&](int x1, int y1, int x2, int y2, uint32_t col)
        {
            for (int y = y1; y <= y2; ++y)
            {
                for (int x = x1; x <= x2; ++x)
                {
                    put(x, y, col);
                }
            }
        };
        auto ellipse = [&](int cx, int cy, int rx, int ry, uint32_t col)
        {
            for (int y = cy - ry; y <= cy + ry; ++y)
            {
                for (int x = cx - rx; x <= cx + rx; ++x)
                {
                    const double nx = static_cast<double>(x - cx) / MaxValue(1.0, static_cast<double>(rx));
                    const double ny = static_cast<double>(y - cy) / MaxValue(1.0, static_cast<double>(ry));
                    if ((nx * nx + ny * ny) <= 1.0) put(x, y, col);
                }
            }
        };
        auto diamond = [&](int cx, int cy, int r, uint32_t col)
        {
            for (int y = cy - r; y <= cy + r; ++y)
            {
                for (int x = cx - r; x <= cx + r; ++x)
                {
                    if (std::abs(x - cx) + std::abs(y - cy) <= r) put(x, y, col);
                }
            }
        };

        const uint32_t outline = 0xFFFFFFFFu;
        const uint32_t dark = 0xFF101018u;
        const uint32_t color =
            (objectType == kPlayer1ObjectType || objectType == kPlayer2ObjectType) ? 0xFF73DC87u :
            (MonsterTypeCategory(objectType) == L"Pickup / item") ? 0xFF6EDCD2u :
            0xFFE66482u;

        if (objectType == kPlayer1ObjectType || objectType == kPlayer2ObjectType)
        {
            ellipse(32, 20, 10, 10, outline);
            ellipse(32, 20, 8, 8, color);
            fillRect(26, 30, 38, 50, color);
            fillRect(24, 31, 26, 48, outline);
            fillRect(38, 31, 40, 48, outline);
            fillRect(22, 50, 42, 54, outline);
        }
        else if (MonsterTypeCategory(objectType) == L"Pickup / item")
        {
            diamond(32, 32, 24, outline);
            diamond(32, 32, 21, color);
            fillRect(20, 29, 44, 35, dark);
            fillRect(29, 20, 35, 44, dark);
        }
        else
        {
            ellipse(32, 18, 13, 12, outline);
            ellipse(32, 18, 10, 9, color);
            fillRect(20, 29, 44, 52, color);
            fillRect(18, 30, 21, 50, outline);
            fillRect(43, 30, 46, 50, outline);
            fillRect(24, 22, 28, 26, dark);
            fillRect(36, 22, 40, 26, dark);
            fillRect(27, 43, 37, 47, dark);
        }

        return true;
    }

    bool ResolveObjectPath(const ObjectTypeInfo& info, std::string& outPath, int& outPreferredFrame)
    {
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
        auto addPathSet = [&](const char* rel)
        {
            if (!rel) return;
            const std::string configuredRoot = TrimTrailingSlashes(g_app.textureDataPath);
            if (!configuredRoot.empty())
            {
                addCandidate(JoinPath(configuredRoot, rel));
                // Many editor setups point the texture root directly at the txts
                // folder.  Object/monster graphics live beside txts in objs/ and
                // char/, so also try the parent game-data folder in that case.
                if (LowerAscii(BaseNameNoSlash(configuredRoot)) == "txts")
                {
                    addCandidate(JoinPath(DirName(configuredRoot), rel));
                }
            }
            if (!g_app.gameProfile.rootPath.empty())
            {
                // Prefer the detected game root. This keeps Zombie Massacre on
                // char/* sprites while classic Gloom profiles prefer objs/*.
                addCandidate(JoinPath(g_app.gameProfile.rootPath, rel));
            }
            addCandidate(JoinPath(mapDir, rel));
            addCandidate(JoinPath(projectDir, rel));
            addCandidate(rel);
        };

        if (g_app.gameProfile.kind == GameProfileKind::ZombieMassacre)
        {
            addPathSet(info.zombiePath);
            addPathSet(info.normalPath);
        }
        else
        {
            addPathSet(info.normalPath);
            addPathSet(info.zombiePath);
        }
        for (const char* rel : ExtraObjectSpriteCandidates(info.type))
        {
            addPathSet(rel);
        }

        std::vector<uint8_t> test;
        std::string firstExisting;
        int firstExistingFrame = info.defaultFrame;
        for (const auto& candidate : candidates)
        {
            if (!ReadFileBinaryLocal(candidate, test))
            {
                continue;
            }

            const int preferredFrame = PreferredObjectFrameForPath(info.type, info, candidate);
            if (firstExisting.empty())
            {
                firstExisting = candidate;
                firstExistingFrame = preferredFrame;
            }

            // For weapon upgrades, skip sprite sheets that do not actually contain
            // the requested frame if another candidate might. This avoids showing the
            // last token frame for every missing weapon/ammo icon.
            const int frameCount = TryReadObjectFrameCount(candidate);
            if (IsWeaponObjectType(info.type) && frameCount > 0 && preferredFrame >= frameCount)
            {
                continue;
            }

            outPath = candidate;
            outPreferredFrame = preferredFrame;
            return true;
        }

        if (!firstExisting.empty())
        {
            outPath = firstExisting;
            outPreferredFrame = firstExistingFrame;
            return true;
        }
        return false;
    }

    bool LoadObjectPreviewImage(int objectType, ObjectPreviewImage& outImage)
    {
        outImage = {};
        outImage.objectType = objectType;
        const ObjectTypeInfo* info = GetObjectTypeInfo(objectType);
        outImage.name = info ? info->name : MonsterTypeName(objectType);
        outImage.category = info ? info->category : MonsterTypeCategory(objectType);

        const std::string cachePrefix = TrimTrailingSlashes(g_app.textureDataPath) + "|" +
            TrimTrailingSlashes(g_app.gameProfile.rootPath) + "|" +
            std::to_string(static_cast<int>(g_app.gameProfile.kind)) + "|" +
            DirName(g_app.document.sourcePath) + "|";

        if (!info)
        {
            const std::string cacheKey = cachePrefix + std::to_string(objectType) + "|unknown";
            auto cacheIt = g_app.objectPreviewCache.find(cacheKey);
            if (cacheIt != g_app.objectPreviewCache.end())
            {
                outImage = cacheIt->second;
                return !outImage.pixels.empty();
            }
            outImage.error = "No object metadata is known for this type.";
            g_app.objectPreviewCache[cacheKey] = outImage;
            return false;
        }

        std::string resolvedPath;
        int preferredFrame = info->defaultFrame;
        if (!ResolveObjectPath(*info, resolvedPath, preferredFrame))
        {
            const std::string cacheKey = cachePrefix + std::to_string(objectType) + "|fallback";
            auto cacheIt = g_app.objectPreviewCache.find(cacheKey);
            if (cacheIt != g_app.objectPreviewCache.end())
            {
                outImage = cacheIt->second;
                return !outImage.pixels.empty();
            }
            if (GenerateObjectFallbackPreviewImage(objectType, outImage))
            {
                g_app.objectPreviewCache[cacheKey] = outImage;
                return true;
            }
            outImage.error = "Object sprite file not found. Expected files like objs/marine, objs/tokens or char/*.";
            g_app.objectPreviewCache[cacheKey] = outImage;
            return false;
        }

        const std::string cacheKey = cachePrefix + std::to_string(objectType) + "|" + resolvedPath + "|" + std::to_string(preferredFrame);
        auto cacheIt = g_app.objectPreviewCache.find(cacheKey);
        if (cacheIt != g_app.objectPreviewCache.end())
        {
            outImage = cacheIt->second;
            return !outImage.pixels.empty();
        }

        auto cacheGeneratedFallback = [&]() -> bool
        {
            const std::string decodeError = outImage.error;
            if (GenerateObjectFallbackPreviewImage(objectType, outImage))
            {
                if (!decodeError.empty())
                {
                    outImage.error = decodeError + " Generated editor fallback used.";
                }
                g_app.objectPreviewCache[cacheKey] = outImage;
                return true;
            }
            g_app.objectPreviewCache[cacheKey] = outImage;
            return false;
        };

        std::string error;
        std::vector<uint8_t> raw;
        if (!LoadMaybeCrmLocal(resolvedPath, raw, error))
        {
            outImage.error = error;
            return cacheGeneratedFallback();
        }
        if (raw.size() < 16)
        {
            outImage.error = "Object sprite file is too small.";
            return cacheGeneratedFallback();
        }

        const uint32_t frameShift = ReadBE16Local(raw.data() + 0);
        const uint32_t frameBase = ReadBE16Local(raw.data() + 2);
        uint32_t frameCount = (frameShift < 16) ? (frameBase << frameShift) : frameBase;
        const uint32_t paletteOffset = ReadBE32Local(raw.data() + 8);
        if (frameCount == 0 || frameCount > 4096 || paletteOffset >= raw.size())
        {
            outImage.error = "Object sprite header is invalid.";
            return cacheGeneratedFallback();
        }
        if ((12 + frameCount * 4) > raw.size())
        {
            outImage.error = "Object frame table is truncated.";
            return cacheGeneratedFallback();
        }

        int frameIndex = ClampValue(preferredFrame, 0, static_cast<int>(frameCount) - 1);
        const uint32_t frameOffset = ReadBE32Local(raw.data() + 12 + static_cast<size_t>(frameIndex) * 4);
        if (frameOffset + 8 > raw.size())
        {
            outImage.error = "Object frame offset is invalid.";
            return cacheGeneratedFallback();
        }

        const uint8_t* frame = raw.data() + frameOffset;
        const uint32_t width = ReadBE16Local(frame + 4);
        const uint32_t height = ReadBE16Local(frame + 6);
        if (width == 0 || height == 0 || width > 512 || height > 512 || frameOffset + 8 + width * height > raw.size())
        {
            outImage.error = "Object frame dimensions are invalid.";
            return cacheGeneratedFallback();
        }

        outImage.loadedPath = resolvedPath;
        outImage.frameIndex = frameIndex;
        outImage.frameCount = static_cast<int>(frameCount);
        outImage.width = static_cast<int>(width);
        outImage.height = static_cast<int>(height);
        outImage.pixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0xFF26262Eu);

        auto readSpritePaletteColor = [&](uint8_t idx, uint16_t& outColor) -> bool
        {
            return ReadGloomSpritePaletteColor(raw, static_cast<size_t>(paletteOffset), idx, outColor);
        };

        for (uint32_t x = 0; x < width; ++x)
        {
            for (uint32_t y = 0; y < height; ++y)
            {
                const uint8_t idx = frame[8 + static_cast<size_t>(y) + static_cast<size_t>(x) * height];
                uint32_t pixel = 0xFF26262Eu;
                uint16_t col = 0;
                if (readSpritePaletteColor(idx, col))
                {
                    pixel = MakeArgbFromGloomPaletteWord(col);
                }
                outImage.pixels[static_cast<size_t>(y) * width + x] = pixel;
            }
        }

        g_app.objectPreviewCache[cacheKey] = outImage;
        return true;
    }

    void SetObjectPlacementMode(int objectType)
    {
        g_app.placeObjectType = ClampValue(objectType, 0, 23);
        g_app.insertMode = InsertMode::ObjectSpawn;
        ClearSelectedMonsterSpawn();
        ClearSelectedTeleportTarget();
        g_app.selectedZone = -1;
        g_app.isDrawing = false;
        g_app.isPanning = false;
        if (g_app.monsterSpawnDragging)
        {
            FinishMonsterSpawnDrag();
        }
        g_app.drawWallAngleLock = false;
        g_app.drawWallLengthSnapLock = false;
        UpdateModeButtons();
        RefreshPreviewImage();
        RefreshStatus();
        InvalidateEditorViews();
    }

    void AppendObjectMenuItem(HMENU menu, UINT id, int objectType)
    {
        std::wstring label = ObjectPlacementLabel(objectType);
        AppendMenuW(menu, MF_STRING, id, label.c_str());
    }

    void ShowObjectPlacementMenu(ObjectPlacementGroup group, HWND anchorButton)
    {
        HMENU menu = CreatePopupMenu();
        if (!menu) return;
        constexpr UINT baseId = 47000;
        std::vector<int> types;
        switch (group)
        {
        case ObjectPlacementGroup::Enemy:
            types = { 10, 11, 12, 13, 14, 15, 21, 22, 8, 23 };
            break;
        case ObjectPlacementGroup::Weapon:
            types = { 16, 17, 18, 19, 20 };
            break;
        case ObjectPlacementGroup::Pickup:
            types = { 2, 4, 5, 6, 7, 9 };
            break;
        }
        for (size_t i = 0; i < types.size(); ++i)
        {
            AppendObjectMenuItem(menu, baseId + static_cast<UINT>(i), types[i]);
        }

        RECT br{};
        GetWindowRect(anchorButton ? anchorButton : g_app.mainWindow, &br);
        const UINT chosen = TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, br.left, br.bottom, 0, g_app.mainWindow, nullptr);
        if (chosen >= baseId && chosen < baseId + types.size())
        {
            SetObjectPlacementMode(types[chosen - baseId]);
        }
        DestroyMenu(menu);
    }

    void PlaceObjectAtWorld(POINT worldPoint)
    {
        const int eventIndex = ClampValue(g_app.placeObjectEvent, 0, static_cast<int>(mapfmt::MapDocument::kEventCount) - 1);
        mapfmt::EventCommand cmd;
        cmd.type = mapfmt::CommandType::AddMonster;
        cmd.params[0] = static_cast<int16_t>(ClampValue(g_app.placeObjectType, 0, 23));
        cmd.params[1] = ClampWorldToInt16(worldPoint.x);
        cmd.params[2] = 0;
        cmd.params[3] = ClampWorldToInt16(worldPoint.y);
        cmd.params[4] = 0;

        PushUndoSnapshot();
        auto& commands = g_app.document.events[eventIndex].commands;
        commands.push_back(cmd);
        g_app.selectedZone = -1;
        g_app.selectedMonsterSpawn.eventIndex = eventIndex;
        g_app.selectedMonsterSpawn.commandIndex = static_cast<int>(commands.size()) - 1;
        g_app.selectedMonsterSpawn.markerIndex = CountMonsterSpawns() - 1;
        RefreshPreviewImage();
        MarkDirty();
        RefreshZoneList();
        RefreshStatus();
        InvalidateEditorViews();
    }

    mapfmt::EventCommand* FindPlayerStartCommand(int playerType)
    {
        if (kInitialEventIndex < 0 || kInitialEventIndex >= static_cast<int>(g_app.document.events.size())) return nullptr;
        auto& commands = g_app.document.events[kInitialEventIndex].commands;
        for (auto& command : commands)
        {
            if (command.type == mapfmt::CommandType::AddMonster && command.params[0] == playerType)
            {
                return &command;
            }
        }
        return nullptr;
    }

    const mapfmt::EventCommand* FindPlayerStartCommandConst(int playerType)
    {
        if (kInitialEventIndex < 0 || kInitialEventIndex >= static_cast<int>(g_app.document.events.size())) return nullptr;
        const auto& commands = g_app.document.events[kInitialEventIndex].commands;
        for (const auto& command : commands)
        {
            if (command.type == mapfmt::CommandType::AddMonster && command.params[0] == playerType)
            {
                return &command;
            }
        }
        return nullptr;
    }

    MonsterSpawnSelection FindPlayerStartSelection(int playerType)
    {
        MonsterSpawnSelection result;
        int markerIndex = 0;
        for (int eventIndex = 0; eventIndex < static_cast<int>(g_app.document.events.size()); ++eventIndex)
        {
            const auto& commands = g_app.document.events[eventIndex].commands;
            for (int commandIndex = 0; commandIndex < static_cast<int>(commands.size()); ++commandIndex)
            {
                const auto& command = commands[commandIndex];
                if (command.type != mapfmt::CommandType::AddMonster)
                {
                    continue;
                }
                if (eventIndex == kInitialEventIndex && command.params[0] == playerType)
                {
                    result.eventIndex = eventIndex;
                    result.commandIndex = commandIndex;
                    result.markerIndex = markerIndex;
                    return result;
                }
                ++markerIndex;
            }
        }
        return result;
    }

    void SetPlayerStartAtWorld(POINT worldPoint)
    {
        PushUndoSnapshot();
        auto& commands = g_app.document.events[kInitialEventIndex].commands;
        mapfmt::EventCommand* command = FindPlayerStartCommand(kPlayer1ObjectType);
        if (!command)
        {
            mapfmt::EventCommand newCommand;
            newCommand.type = mapfmt::CommandType::AddMonster;
            newCommand.params[0] = kPlayer1ObjectType;
            newCommand.params[2] = 0;
            newCommand.params[4] = 0;
            commands.insert(commands.begin(), newCommand);
            command = &commands.front();
        }
        command->params[0] = kPlayer1ObjectType;
        command->params[1] = ClampWorldToInt16(worldPoint.x);
        command->params[2] = 0;
        command->params[3] = ClampWorldToInt16(worldPoint.y);
        command->params[4] = 0;

        // Keep the 3D preview in sync with a newly placed/moved Player1 marker
        // instead of waiting for the user to right-click the map preview camera.
        g_app.walkPreviewX = static_cast<double>(command->params[1]);
        g_app.walkPreviewZ = static_cast<double>(command->params[3]);
        g_app.walkPreviewDir = WalkPreviewDirFromRotationUnits(command->params[4]);
        g_app.walkPreviewInitialized = true;

        g_app.selectedZone = -1;
        g_app.selectedMonsterSpawn = FindPlayerStartSelection(kPlayer1ObjectType);
        MarkDirty();
        RefreshZoneList();
        RefreshPreviewImage();
        UpdateModeButtons();
        RefreshStatus();
        InvalidateEditorViews();
    }

    bool IsLevelEndZone(const mapfmt::Zone& zone)
    {
        return IsLinearZoneType(zone.ztype) && zone.ztype != static_cast<int16_t>(mapfmt::ZoneType::Wall) && zone.ev == kLevelEndEventValue;
    }

    int EventSlotFromZoneEventValue(int evValue)
    {
        if (evValue <= 0 || evValue > static_cast<int>(mapfmt::MapDocument::kEventCount))
        {
            return -1;
        }
        return evValue - 1;
    }

    bool IsEventTriggerLineZone(const mapfmt::Zone& zone)
    {
        return IsLinearZoneType(zone.ztype) &&
            zone.ztype != static_cast<int16_t>(mapfmt::ZoneType::Wall) &&
            EventSlotFromZoneEventValue(zone.ev) >= 0;
    }

    bool IsSelectedEventTriggerOrLevelEndZone()
    {
        if (g_app.selectedZone < 0 || g_app.selectedZone >= static_cast<int>(g_app.document.zones.size()))
        {
            return false;
        }
        const auto& zone = g_app.document.zones[g_app.selectedZone];
        return IsLevelEndZone(zone) || IsEventTriggerLineZone(zone);
    }

    bool IsSwitchTextureSourceLineZone(const mapfmt::Zone& zone)
    {
        // For the right-hand ON-texture picker we intentionally use the same
        // broad source rules as the Link Event tools: any normal non-wall line
        // can become an event trigger.  This keeps T0-T7 visible even before a
        // trigger has been assigned/confirmed as an event slot.
        return IsLinearZoneType(zone.ztype) &&
            zone.ztype != static_cast<int16_t>(mapfmt::ZoneType::Wall) &&
            !IsLevelEndZone(zone);
    }

    bool IsSwitchTextureSourceZoneIndex(int zoneIndex)
    {
        return zoneIndex >= 0 &&
            zoneIndex < static_cast<int>(g_app.document.zones.size()) &&
            IsSwitchTextureSourceLineZone(g_app.document.zones[zoneIndex]);
    }

    void AddUniqueIndex(std::vector<int>& values, int value)
    {
        if (value < 0) return;
        if (std::find(values.begin(), values.end(), value) == values.end())
        {
            values.push_back(value);
        }
    }

    int DisplayZoneIndexForEventTarget(int zoneIndex)
    {
        if (zoneIndex < 0 || zoneIndex >= static_cast<int>(g_app.document.zones.size()))
        {
            return zoneIndex;
        }
        if (IsWallZone(g_app.document.zones[zoneIndex]))
        {
            return GetCanonicalWallIndex(zoneIndex);
        }
        return zoneIndex;
    }

    bool EventCommandTargetsDisplayedZone(const mapfmt::EventCommand& command, int zoneIndex)
    {
        const int displayZone = DisplayZoneIndexForEventTarget(zoneIndex);
        if (displayZone < 0) return false;

        switch (command.type)
        {
        case mapfmt::CommandType::OpenDoor:
        case mapfmt::CommandType::ChangeTexture:
            return DisplayZoneIndexForEventTarget(command.params[0]) == displayZone;
        case mapfmt::CommandType::RotatePoly:
        {
            const int first = command.params[0];
            const int count = MaxValue(1, static_cast<int>(command.params[1]));
            for (int i = 0; i < count; ++i)
            {
                if (DisplayZoneIndexForEventTarget(first + i) == displayZone)
                {
                    return true;
                }
            }
            return false;
        }
        default:
            return false;
        }
    }

    bool EventCommandTargetsZone(const mapfmt::EventCommand& command, int zoneIndex)
    {
        if (zoneIndex < 0) return false;
        switch (command.type)
        {
        case mapfmt::CommandType::OpenDoor:
        case mapfmt::CommandType::ChangeTexture:
            return command.params[0] == zoneIndex;
        case mapfmt::CommandType::RotatePoly:
        {
            const int first = command.params[0];
            const int count = MaxValue(1, static_cast<int>(command.params[1]));
            return zoneIndex >= first && zoneIndex < (first + count);
        }
        default:
            return false;
        }
    }

    bool IsZoneControlledByOpenDoor(int zoneIndex)
    {
        if (zoneIndex < 0 || zoneIndex >= static_cast<int>(g_app.document.zones.size())) return false;
        for (const auto& script : g_app.document.events)
        {
            for (const auto& command : script.commands)
            {
                if (command.type == mapfmt::CommandType::OpenDoor &&
                    DisplayZoneIndexForEventTarget(command.params[0]) == DisplayZoneIndexForEventTarget(zoneIndex))
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool CommandHasSingleZoneTarget(const mapfmt::EventCommand& command)
    {
        return command.type == mapfmt::CommandType::OpenDoor ||
               command.type == mapfmt::CommandType::ChangeTexture;
    }

    bool AdjustCommandZoneReferencesAfterDelete(mapfmt::EventCommand& command, int deletedZoneIndex)
    {
        if (deletedZoneIndex < 0) return true;

        if (CommandHasSingleZoneTarget(command))
        {
            if (command.params[0] == deletedZoneIndex)
            {
                return false;
            }
            if (command.params[0] > deletedZoneIndex)
            {
                --command.params[0];
            }
            return true;
        }

        if (command.type == mapfmt::CommandType::RotatePoly)
        {
            int first = command.params[0];
            int count = MaxValue(1, static_cast<int>(command.params[1]));
            if (deletedZoneIndex < first)
            {
                --first;
            }
            else if (deletedZoneIndex >= first && deletedZoneIndex < first + count)
            {
                --count;
                if (count <= 0)
                {
                    return false;
                }
            }
            command.params[0] = static_cast<int16_t>(first);
            command.params[1] = static_cast<int16_t>(count);
            return true;
        }

        return true;
    }

    void RemoveEventReferencesForDeletedZoneIndex(int deletedZoneIndex)
    {
        if (deletedZoneIndex < 0) return;

        for (auto& script : g_app.document.events)
        {
            auto& commands = script.commands;
            for (auto it = commands.begin(); it != commands.end(); )
            {
                if (!AdjustCommandZoneReferencesAfterDelete(*it, deletedZoneIndex))
                {
                    it = commands.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
    }

    void SwapEventZoneReferences(int firstZoneIndex, int secondZoneIndex)
    {
        if (firstZoneIndex == secondZoneIndex || firstZoneIndex < 0 || secondZoneIndex < 0) return;

        for (auto& script : g_app.document.events)
        {
            for (auto& command : script.commands)
            {
                if (CommandHasSingleZoneTarget(command))
                {
                    if (command.params[0] == firstZoneIndex)
                    {
                        command.params[0] = static_cast<int16_t>(secondZoneIndex);
                    }
                    else if (command.params[0] == secondZoneIndex)
                    {
                        command.params[0] = static_cast<int16_t>(firstZoneIndex);
                    }
                }
                else if (command.type == mapfmt::CommandType::RotatePoly && command.params[1] <= 1)
                {
                    if (command.params[0] == firstZoneIndex)
                    {
                        command.params[0] = static_cast<int16_t>(secondZoneIndex);
                    }
                    else if (command.params[0] == secondZoneIndex)
                    {
                        command.params[0] = static_cast<int16_t>(firstZoneIndex);
                    }
                }
            }
        }
    }

    int RemapZoneIndex(const std::vector<int>& oldToNew, int oldIndex)
    {
        if (oldIndex < 0 || oldIndex >= static_cast<int>(oldToNew.size()))
        {
            return oldIndex;
        }
        return oldToNew[static_cast<size_t>(oldIndex)];
    }

    void RemapEventZoneReferencesAfterReorder(const std::vector<int>& oldToNew)
    {
        if (oldToNew.empty()) return;

        for (auto& script : g_app.document.events)
        {
            for (auto& command : script.commands)
            {
                if (CommandHasSingleZoneTarget(command))
                {
                    command.params[0] = static_cast<int16_t>(RemapZoneIndex(oldToNew, command.params[0]));
                }
                else if (command.type == mapfmt::CommandType::RotatePoly)
                {
                    const int oldFirst = command.params[0];
                    const int oldCount = MaxValue(1, static_cast<int>(command.params[1]));
                    int newFirst = -1;
                    int newLast = -1;
                    for (int i = 0; i < oldCount; ++i)
                    {
                        const int mapped = RemapZoneIndex(oldToNew, oldFirst + i);
                        if (mapped < 0) continue;
                        newFirst = (newFirst < 0) ? mapped : MinValue(newFirst, mapped);
                        newLast = MaxValue(newLast, mapped);
                    }
                    if (newFirst >= 0 && newLast >= newFirst)
                    {
                        command.params[0] = static_cast<int16_t>(newFirst);
                        command.params[1] = static_cast<int16_t>(MaxValue(1, newLast - newFirst + 1));
                    }
                }
            }
        }
    }

    std::vector<int> GetEventTargetZones(int eventIndex)
    {
        std::vector<int> result;
        if (eventIndex < 0 || eventIndex >= static_cast<int>(g_app.document.events.size())) return result;
        const auto& commands = g_app.document.events[eventIndex].commands;
        for (const auto& command : commands)
        {
            switch (command.type)
            {
            case mapfmt::CommandType::OpenDoor:
            case mapfmt::CommandType::ChangeTexture:
                AddUniqueIndex(result, DisplayZoneIndexForEventTarget(command.params[0]));
                break;
            case mapfmt::CommandType::RotatePoly:
            {
                const int first = command.params[0];
                const int count = MaxValue(1, static_cast<int>(command.params[1]));
                for (int i = 0; i < count; ++i)
                {
                    AddUniqueIndex(result, DisplayZoneIndexForEventTarget(first + i));
                }
                break;
            }
            default:
                break;
            }
        }
        result.erase(std::remove_if(result.begin(), result.end(), [](int zoneIndex)
        {
            return zoneIndex < 0 || zoneIndex >= static_cast<int>(g_app.document.zones.size());
        }), result.end());
        return result;
    }

    std::vector<int> GetTriggerZonesForEvent(int eventIndex)
    {
        std::vector<int> result;
        if (eventIndex < 0 || eventIndex >= static_cast<int>(mapfmt::MapDocument::kEventCount)) return result;
        const int evValue = eventIndex + 1;
        for (int i = 0; i < static_cast<int>(g_app.document.zones.size()); ++i)
        {
            const auto& zone = g_app.document.zones[i];
            if (IsEventTriggerLineZone(zone) && zone.ev == evValue)
            {
                AddUniqueIndex(result, i);
            }
        }
        return result;
    }

    std::vector<int> GetEventsControllingZone(int zoneIndex)
    {
        std::vector<int> result;
        if (zoneIndex < 0 || zoneIndex >= static_cast<int>(g_app.document.zones.size())) return result;
        for (int eventIndex = 0; eventIndex < static_cast<int>(g_app.document.events.size()); ++eventIndex)
        {
            const auto& commands = g_app.document.events[eventIndex].commands;
            if (std::any_of(commands.begin(), commands.end(), [&](const mapfmt::EventCommand& command)
            {
                return EventCommandTargetsDisplayedZone(command, zoneIndex);
            }))
            {
                AddUniqueIndex(result, eventIndex);
            }
        }
        return result;
    }

    bool EventHasActiveTrigger(int eventIndex)
    {
        return !GetTriggerZonesForEvent(eventIndex).empty();
    }

    bool MonsterSpawnHasActiveEventLink(const MonsterSpawnSelection& selection)
    {
        if (selection.eventIndex == kInitialEventIndex) return false;
        if (!GetMonsterSpawnCommand(selection)) return false;
        return EventHasActiveTrigger(selection.eventIndex);
    }

    int CountZoneEventLinkCommandsForDelete(int zoneIndex)
    {
        if (zoneIndex < 0 || zoneIndex >= static_cast<int>(g_app.document.zones.size())) return 0;
        const auto& zone = g_app.document.zones[zoneIndex];
        if (IsEventTriggerLineZone(zone) || IsLevelEndZone(zone)) return 0;

        int count = 0;
        const auto controllingEvents = GetEventsControllingZone(zoneIndex);
        for (int eventIndex : controllingEvents)
        {
            if (!EventHasActiveTrigger(eventIndex)) continue;
            if (eventIndex < 0 || eventIndex >= static_cast<int>(g_app.document.events.size())) continue;
            const auto& commands = g_app.document.events[eventIndex].commands;
            for (const auto& command : commands)
            {
                if (EventCommandTargetsDisplayedZone(command, zoneIndex))
                {
                    ++count;
                }
            }
        }
        return count;
    }

    bool ZoneHasActiveEventLinkForDelete(int zoneIndex)
    {
        return CountZoneEventLinkCommandsForDelete(zoneIndex) > 0;
    }

    bool CanDeleteSelectedEventLink()
    {
        if (IsSelectedMonsterSpawnValid())
        {
            return MonsterSpawnHasActiveEventLink(g_app.selectedMonsterSpawn);
        }
        if (g_app.selectedZone >= 0 && g_app.selectedZone < static_cast<int>(g_app.document.zones.size()))
        {
            return ZoneHasActiveEventLinkForDelete(g_app.selectedZone);
        }
        return false;
    }

    bool MoveMonsterSpawnToInitialEvent(const MonsterSpawnSelection& selection)
    {
        if (selection.eventIndex == kInitialEventIndex) return false;
        if (kInitialEventIndex < 0 || kInitialEventIndex >= static_cast<int>(g_app.document.events.size())) return false;
        if (selection.eventIndex < 0 || selection.eventIndex >= static_cast<int>(g_app.document.events.size())) return false;

        const mapfmt::EventCommand* command = GetMonsterSpawnCommand(selection);
        if (!command) return false;
        mapfmt::EventCommand movedCommand = *command;

        auto& sourceCommands = g_app.document.events[selection.eventIndex].commands;
        if (selection.commandIndex < 0 || selection.commandIndex >= static_cast<int>(sourceCommands.size())) return false;
        if (sourceCommands[selection.commandIndex].type != mapfmt::CommandType::AddMonster) return false;

        sourceCommands.erase(sourceCommands.begin() + selection.commandIndex);
        auto& destinationCommands = g_app.document.events[kInitialEventIndex].commands;
        destinationCommands.push_back(movedCommand);

        g_app.selectedMonsterSpawn.eventIndex = kInitialEventIndex;
        g_app.selectedMonsterSpawn.commandIndex = static_cast<int>(destinationCommands.size()) - 1;
        g_app.selectedMonsterSpawn.markerIndex = ComputeMonsterSpawnMarkerIndex(g_app.selectedMonsterSpawn.eventIndex, g_app.selectedMonsterSpawn.commandIndex);
        return true;
    }

    bool DeleteSelectedZoneEventLinks()
    {
        const int zoneIndex = g_app.selectedZone;
        if (CountZoneEventLinkCommandsForDelete(zoneIndex) <= 0) return false;

        PushUndoSnapshot();
        int removed = 0;
        const auto controllingEvents = GetEventsControllingZone(zoneIndex);
        for (int eventIndex : controllingEvents)
        {
            if (!EventHasActiveTrigger(eventIndex)) continue;
            if (eventIndex < 0 || eventIndex >= static_cast<int>(g_app.document.events.size())) continue;
            auto& commands = g_app.document.events[eventIndex].commands;
            const auto before = commands.size();
            commands.erase(std::remove_if(commands.begin(), commands.end(), [&](const mapfmt::EventCommand& command)
            {
                return EventCommandTargetsDisplayedZone(command, zoneIndex);
            }), commands.end());
            removed += static_cast<int>(before - commands.size());
        }

        if (removed <= 0)
        {
            UndoLastChange();
            return false;
        }

        g_app.insertMode = InsertMode::None;
        g_app.linkEventTriggerZone = -1;
        g_app.linkEventIndex = -1;
        MarkDirty();
        RefreshZoneList();
        RefreshPreviewImage();
        UpdateModeButtons();
        RefreshStatus();
        InvalidateEditorViews();
        return true;
    }

    bool DeleteSelectedMonsterSpawnEventLink()
    {
        if (!MonsterSpawnHasActiveEventLink(g_app.selectedMonsterSpawn)) return false;

        const MonsterSpawnSelection selection = g_app.selectedMonsterSpawn;
        PushUndoSnapshot();
        if (!MoveMonsterSpawnToInitialEvent(selection))
        {
            UndoLastChange();
            return false;
        }

        ClearSelectedTeleportTarget();
        g_app.selectedZone = -1;
        g_app.insertMode = InsertMode::None;
        g_app.linkEventTriggerZone = -1;
        g_app.linkEventIndex = -1;
        MarkDirty();
        RefreshZoneList();
        RefreshPreviewImage();
        UpdateModeButtons();
        RefreshStatus();
        InvalidateEditorViews();
        return true;
    }

    bool DeleteSelectedEventLink()
    {
        if (IsSelectedMonsterSpawnValid())
        {
            return DeleteSelectedMonsterSpawnEventLink();
        }
        if (g_app.selectedZone >= 0 && g_app.selectedZone < static_cast<int>(g_app.document.zones.size()))
        {
            return DeleteSelectedZoneEventLinks();
        }
        return false;
    }


    double ZoneCenterDistanceSquared(int firstZoneIndex, int secondZoneIndex)
    {
        if (firstZoneIndex < 0 || firstZoneIndex >= static_cast<int>(g_app.document.zones.size()) ||
            secondZoneIndex < 0 || secondZoneIndex >= static_cast<int>(g_app.document.zones.size()))
        {
            return 0.0;
        }

        const auto& a = g_app.document.zones[firstZoneIndex];
        const auto& b = g_app.document.zones[secondZoneIndex];
        const double ax = (static_cast<double>(a.x1) + static_cast<double>(a.x2)) * 0.5;
        const double az = (static_cast<double>(a.z1) + static_cast<double>(a.z2)) * 0.5;
        const double bx = (static_cast<double>(b.x1) + static_cast<double>(b.x2)) * 0.5;
        const double bz = (static_cast<double>(b.z1) + static_cast<double>(b.z2)) * 0.5;
        const double dx = ax - bx;
        const double dz = az - bz;
        return dx * dx + dz * dz;
    }

    bool ZoneContainsTextureIndex(const mapfmt::Zone& zone, int textureIndex)
    {
        textureIndex = ClampValue(textureIndex, 0, 159);
        for (uint8_t texture : zone.textures)
        {
            if (static_cast<int>(texture) == textureIndex)
            {
                return true;
            }
        }
        return false;
    }

    int GetSwitchOffTextureIndexForTargetZone(int targetZoneIndex)
    {
        if (targetZoneIndex < 0 || targetZoneIndex >= static_cast<int>(g_app.document.zones.size())) return -1;
        const auto& zone = g_app.document.zones[targetZoneIndex];
        if (!IsWallZone(zone)) return -1;
        return ClampValue(static_cast<int>(zone.textures[0]), 0, 159);
    }

    int GetSwitchAutoOnTextureIndexForTargetZone(int targetZoneIndex)
    {
        const int offTextureIndex = GetSwitchOffTextureIndexForTargetZone(targetZoneIndex);
        if (offTextureIndex < 0) return -1;

        mapfmt::AnimationEntry offAnimation{};
        if (ResolveAnimationForTextureIndex(offTextureIndex, offAnimation))
        {
            // Animated OFF panels normally occupy a compact range, e.g.
            // 37-38 for a red blinking switch.  The visible ON state is the
            // following still frame, e.g. 39 (green), not the first OFF frame.
            return ClampValue(static_cast<int>(offAnimation.first + offAnimation.frames), 0, 159);
        }

        return ClampValue(offTextureIndex + 1, 0, 159);
    }

    bool TryGetSwitchTextureCommandForEvent(int eventIndex, int triggerZoneIndex, SwitchTextureCommandInfo& outInfo)
    {
        outInfo = SwitchTextureCommandInfo{};
        if (eventIndex < 0 || eventIndex >= static_cast<int>(g_app.document.events.size())) return false;

        const auto& commands = g_app.document.events[eventIndex].commands;
        bool found = false;
        double bestScore = 0.0;

        for (int commandIndex = 0; commandIndex < static_cast<int>(commands.size()); ++commandIndex)
        {
            const auto& command = commands[commandIndex];
            if (command.type != mapfmt::CommandType::ChangeTexture)
            {
                continue;
            }

            const int rawTarget = static_cast<int>(command.params[0]);
            if (rawTarget < 0 || rawTarget >= static_cast<int>(g_app.document.zones.size()))
            {
                continue;
            }
            const int displayTarget = DisplayZoneIndexForEventTarget(rawTarget);
            if (displayTarget < 0 || displayTarget >= static_cast<int>(g_app.document.zones.size()))
            {
                continue;
            }
            if (!IsWallZone(g_app.document.zones[rawTarget]) && !IsWallZone(g_app.document.zones[displayTarget]))
            {
                continue;
            }

            // A real switch texture command changes the clicked switch/panel.
            // Static panels switch to the next texture; animated OFF panels
            // switch to the first still frame after the OFF animation.  This is
            // what makes a red 37-38 panel become green 39 in-game. Prefer that
            // visible ON pattern, then prefer targets closest to the trigger line.
            double score = 0.0;
            const int newTextureIndex = ClampValue(static_cast<int>(command.params[1]), 0, 159);
            const int expectedRawOnTexture = GetSwitchAutoOnTextureIndexForTargetZone(rawTarget);
            const int expectedDisplayOnTexture = rawTarget != displayTarget
                ? GetSwitchAutoOnTextureIndexForTargetZone(displayTarget)
                : expectedRawOnTexture;
            const bool matchesExpectedOnTexture =
                (expectedRawOnTexture >= 0 && newTextureIndex == expectedRawOnTexture) ||
                (expectedDisplayOnTexture >= 0 && newTextureIndex == expectedDisplayOnTexture);
            if (expectedRawOnTexture >= 0 || expectedDisplayOnTexture >= 0)
            {
                if (matchesExpectedOnTexture)
                {
                    score -= 1000000.0;
                }
                else
                {
                    score += 1000000.0;
                }
            }
            if (!matchesExpectedOnTexture && ZoneContainsTextureIndex(g_app.document.zones[rawTarget], newTextureIndex))
            {
                score += 500000000.0;
            }
            if (!matchesExpectedOnTexture && rawTarget != displayTarget && ZoneContainsTextureIndex(g_app.document.zones[displayTarget], newTextureIndex))
            {
                score += 250000000.0;
            }
            if (triggerZoneIndex >= 0)
            {
                score += ZoneCenterDistanceSquared(triggerZoneIndex, rawTarget);
            }
            else
            {
                score += static_cast<double>(commandIndex) * 1024.0;
            }

            if (!found || score < bestScore)
            {
                found = true;
                bestScore = score;
                outInfo.commandIndex = commandIndex;
                outInfo.rawTargetZoneIndex = rawTarget;
                outInfo.displayTargetZoneIndex = displayTarget;
                outInfo.newTextureIndex = newTextureIndex;
            }
        }
        return found;
    }

    bool TryGetSwitchTextureCommandForTriggerZone(int triggerZoneIndex, SwitchTextureCommandInfo& outInfo)
    {
        outInfo = SwitchTextureCommandInfo{};
        if (triggerZoneIndex < 0 || triggerZoneIndex >= static_cast<int>(g_app.document.zones.size())) return false;
        const auto& trigger = g_app.document.zones[triggerZoneIndex];
        if (!IsEventTriggerLineZone(trigger)) return false;
        return TryGetSwitchTextureCommandForEvent(EventSlotFromZoneEventValue(trigger.ev), triggerZoneIndex, outInfo);
    }

    bool TryGetFirstSwitchTextureCommandForEvent(int eventIndex, int& targetZoneIndex, int& newTextureIndex)
    {
        SwitchTextureCommandInfo info{};
        if (!TryGetSwitchTextureCommandForEvent(eventIndex, -1, info))
        {
            targetZoneIndex = -1;
            newTextureIndex = -1;
            return false;
        }
        targetZoneIndex = info.displayTargetZoneIndex;
        newTextureIndex = info.newTextureIndex;
        return true;
    }


    bool TryGetPendingSwitchTextureChoice(int triggerZoneIndex, int& textureIndex)
    {
        textureIndex = -1;
        if (!g_app.pendingSwitchTextureValid) return false;
        if (g_app.pendingSwitchTextureTriggerZone != triggerZoneIndex) return false;

        const int eventIndex = (triggerZoneIndex >= 0 && triggerZoneIndex < static_cast<int>(g_app.document.zones.size()))
            ? EventSlotFromZoneEventValue(g_app.document.zones[triggerZoneIndex].ev)
            : -1;
        if (eventIndex != g_app.pendingSwitchTextureEventIndex) return false;

        textureIndex = ClampValue(g_app.pendingSwitchTextureIndex, 0, 159);
        return true;
    }

    void RememberPendingSwitchTextureChoice(int triggerZoneIndex, int textureIndex)
    {
        if (triggerZoneIndex < 0 || triggerZoneIndex >= static_cast<int>(g_app.document.zones.size())) return;
        const int eventIndex = EventSlotFromZoneEventValue(g_app.document.zones[triggerZoneIndex].ev);
        if (eventIndex < 0 || eventIndex >= static_cast<int>(g_app.document.events.size())) return;

        g_app.pendingSwitchTextureTriggerZone = triggerZoneIndex;
        g_app.pendingSwitchTextureEventIndex = eventIndex;
        g_app.pendingSwitchTextureIndex = ClampValue(textureIndex, 0, 159);
        g_app.pendingSwitchTextureValid = true;
    }

    void ClearPendingSwitchTextureChoice()
    {
        g_app.pendingSwitchTextureTriggerZone = -1;
        g_app.pendingSwitchTextureEventIndex = -1;
        g_app.pendingSwitchTextureIndex = -1;
        g_app.pendingSwitchTextureValid = false;
    }

    int GetSwitchTextureIndexForEventWrite(int triggerZoneIndex)
    {
        int pendingTexture = -1;
        if (TryGetPendingSwitchTextureChoice(triggerZoneIndex, pendingTexture))
        {
            return ClampValue(pendingTexture, 0, 159);
        }
        return ClampValue(GetActiveWallAssignedTextureIndex(), 0, 159);
    }

    void SyncActiveSwitchTextureFromTriggerZone(int triggerZoneIndex)
    {
        if (triggerZoneIndex < 0 || triggerZoneIndex >= static_cast<int>(g_app.document.zones.size())) return;
        const auto& trigger = g_app.document.zones[triggerZoneIndex];
        if (!IsEventTriggerLineZone(trigger)) return;

        int textureIndex = -1;
        if (!TryGetPendingSwitchTextureChoice(triggerZoneIndex, textureIndex))
        {
            SwitchTextureCommandInfo switchInfo{};
            if (!TryGetSwitchTextureCommandForTriggerZone(triggerZoneIndex, switchInfo))
            {
                return;
            }
            textureIndex = switchInfo.newTextureIndex;
        }

        mapfmt::AnimationEntry anim{};
        if (ResolveAnimationForTextureIndex(textureIndex, anim))
        {
            textureIndex = ClampValue(static_cast<int>(anim.first), 0, 159);
        }

        g_app.activeWallTextureSlot = ClampValue(textureIndex / 20, 0, static_cast<int>(mapfmt::MapDocument::kTextureSlotCount) - 1);
        g_app.activeWallTextureStrip = ClampValue(textureIndex % 20, 0, 19);
    }

    int GetActiveSwitchTextureTriggerZone()
    {
        if (g_app.insertMode == InsertMode::LinkEventToSwitchTexture &&
            IsSwitchTextureSourceZoneIndex(g_app.linkEventTriggerZone))
        {
            return g_app.linkEventTriggerZone;
        }
        if (IsSwitchTextureSourceZoneIndex(g_app.selectedZone))
        {
            return g_app.selectedZone;
        }
        if (IsSwitchTextureSourceZoneIndex(g_app.linkEventTriggerZone))
        {
            return g_app.linkEventTriggerZone;
        }
        return -1;
    }

    bool PersistActiveSwitchTextureChoiceToTrigger(int triggerZoneIndex)
    {
        if (triggerZoneIndex < 0 || triggerZoneIndex >= static_cast<int>(g_app.document.zones.size())) return false;
        const auto& trigger = g_app.document.zones[triggerZoneIndex];
        if (!IsEventTriggerLineZone(trigger)) return false;

        const int eventIndex = EventSlotFromZoneEventValue(trigger.ev);
        if (eventIndex < 0 || eventIndex >= static_cast<int>(g_app.document.events.size())) return false;

        const int newTextureIndex = ClampValue(GetActiveWallAssignedTextureIndex(), 0, 159);
        RememberPendingSwitchTextureChoice(triggerZoneIndex, newTextureIndex);
        EnsureAnimationEntryForTextureIndex(newTextureIndex);

        auto& commands = g_app.document.events[eventIndex].commands;
        SwitchTextureCommandInfo switchInfo{};
        if (!TryGetSwitchTextureCommandForTriggerZone(triggerZoneIndex, switchInfo))
        {
            // Nothing to persist yet: the ON texture is kept in the picker and will
            // be written once Link Event > Switch/Trigger gets a target wall/panel.
            return false;
        }

        bool changed = false;
        for (auto& command : commands)
        {
            if (command.type != mapfmt::CommandType::ChangeTexture)
            {
                continue;
            }

            // Update the exact raw target zone that the event already uses.
            // Do not canonicalize here; editor-created switches should only
            // touch the clicked switch/panel wall.
            if (static_cast<int>(command.params[0]) != switchInfo.rawTargetZoneIndex)
            {
                continue;
            }
            if (ClampValue(static_cast<int>(command.params[1]), 0, 159) == newTextureIndex)
            {
                continue;
            }
            if (!changed)
            {
                PushUndoSnapshot();
                changed = true;
            }
            command.params[1] = static_cast<int16_t>(newTextureIndex);
        }

        if (changed)
        {
            MarkDirty();
            RefreshZoneList();
            RefreshStatus();
            InvalidateEditorViews();
        }
        return changed;
    }

    bool PersistActiveSwitchTextureChoiceForCurrentContext()
    {
        if (!IsSwitchTexturePickerContext()) return false;
        if (IsWallTextureMappingPickerActive()) return false;
        return PersistActiveSwitchTextureChoiceToTrigger(GetActiveSwitchTextureTriggerZone());
    }

    int FindAvailableEventSlotForTrigger(int triggerZoneIndex)
    {
        std::array<bool, mapfmt::MapDocument::kEventCount> usedByTrigger{};
        for (int i = 0; i < static_cast<int>(g_app.document.zones.size()); ++i)
        {
            if (i == triggerZoneIndex) continue;
            const auto& zone = g_app.document.zones[i];
            if (IsEventTriggerLineZone(zone) && zone.ev != kLevelEndEventValue)
            {
                const int slot = EventSlotFromZoneEventValue(zone.ev);
                if (slot >= 0 && slot < static_cast<int>(usedByTrigger.size()))
                {
                    usedByTrigger[slot] = true;
                }
            }
        }

        const int lastNormalEventSlot = MinValue(static_cast<int>(mapfmt::MapDocument::kEventCount) - 1, kLevelEndEventValue - 2);
        const int firstTriggerEventSlot = ClampValue(kFirstAutoTriggerEventIndex, 0, lastNormalEventSlot);

        // Event slot 0 is the editor's map-start bucket for PlayerStart and newly
        // placed objects.  Do not auto-assign it to trigger lines, otherwise a
        // newly placed trigger with E1 would also fire every object added later.
        for (int i = firstTriggerEventSlot; i <= lastNormalEventSlot; ++i)
        {
            if (!usedByTrigger[i] && g_app.document.events[i].commands.empty())
            {
                return i;
            }
        }
        for (int i = firstTriggerEventSlot; i <= lastNormalEventSlot; ++i)
        {
            if (g_app.document.events[i].commands.empty())
            {
                return i;
            }
        }
        for (int i = firstTriggerEventSlot; i <= lastNormalEventSlot; ++i)
        {
            if (!usedByTrigger[i])
            {
                return i;
            }
        }
        return firstTriggerEventSlot;
    }

    int EnsureTriggerHasEventSlot(int triggerZoneIndex)
    {
        if (triggerZoneIndex < 0 || triggerZoneIndex >= static_cast<int>(g_app.document.zones.size())) return -1;
        auto& zone = g_app.document.zones[triggerZoneIndex];
        if (IsLevelEndZone(zone)) return -1;

        int eventIndex = EventSlotFromZoneEventValue(zone.ev);
        if (eventIndex >= 0 && zone.ev != kLevelEndEventValue)
        {
            return eventIndex;
        }

        eventIndex = FindAvailableEventSlotForTrigger(triggerZoneIndex);
        zone.ev = static_cast<int16_t>(eventIndex + 1);
        return eventIndex;
    }

    bool ZoneUsesKnownAnimatedTexture(const mapfmt::Zone& zone, mapfmt::AnimationEntry& outAnimation)
    {
        for (uint8_t texture : zone.textures)
        {
            mapfmt::AnimationEntry anim{};
            if (ResolveAnimationForTextureIndex(static_cast<int>(texture), anim))
            {
                outAnimation = anim;
                return true;
            }
        }
        return false;
    }

    bool ActiveWallTextureIsKnownAnimated(mapfmt::AnimationEntry& outAnimation)
    {
        return ResolveAnimationForTextureIndex(GetActiveWallTextureIndex(), outAnimation);
    }

    bool PrepareZoneAsOpenDoorMover(int targetZoneIndex, bool allowActiveTextureConvenience = true)
    {
        if (targetZoneIndex < 0 || targetZoneIndex >= static_cast<int>(g_app.document.zones.size())) return false;

        auto& zone = g_app.document.zones[targetZoneIndex];
        if (zone.ztype != static_cast<int16_t>(mapfmt::ZoneType::Wall))
        {
            return false;
        }

        // An OpenDoor target does not have to use an animated door texture.
        // Normal walls can be event-controlled as well.  Therefore the mover
        // preparation is independent from texture animation: keep ordinary
        // textures as-is, but mark the wall with the same positive sc value
        // used by map1_1's sliding wall targets.  Negative sc values are
        // preserved because they are usually special existing behaviours.
        if (zone.sc >= 0)
        {
            zone.sc = 2;
        }

        mapfmt::AnimationEntry anim{};
        bool hasDoorLikeAnimatedTexture = ZoneUsesKnownAnimatedTexture(zone, anim);
        if (!hasDoorLikeAnimatedTexture && allowActiveTextureConvenience && ActiveWallTextureIsKnownAnimated(anim))
        {
            // Optional convenience only: if the user explicitly selected a
            // known animated band such as txt1_3 section 11..14 before linking,
            // apply that animation's first frame.  If not, leave the target's
            // normal wall texture untouched and it will still be event-driven.
            const int first = ClampValue(static_cast<int>(anim.first), 0, 159);
            const int bands = MaxValue(1, GetZonePreviewSlotCount(zone));
            for (int i = 0; i < static_cast<int>(zone.textures.size()); ++i)
            {
                zone.textures[i] = static_cast<uint8_t>(i < bands ? first : 0);
            }
            hasDoorLikeAnimatedTexture = true;
        }

        if (hasDoorLikeAnimatedTexture)
        {
            EnsureAnimationEntryForTextureIndex(static_cast<int>(anim.first));
            const int first = ClampValue(static_cast<int>(anim.first), 0, 159);
            for (uint8_t& texture : zone.textures)
            {
                mapfmt::AnimationEntry textureAnim{};
                if (ResolveAnimationForTextureIndex(static_cast<int>(texture), textureAnim))
                {
                    texture = static_cast<uint8_t>(first);
                }
            }
        }

        return true;
    }

    bool AddSingleOpenDoorCommandIfMissing(std::vector<mapfmt::EventCommand>& commands, int targetZoneIndex)
    {
        if (targetZoneIndex < 0 || targetZoneIndex >= static_cast<int>(g_app.document.zones.size())) return false;

        for (const auto& command : commands)
        {
            if (command.type == mapfmt::CommandType::OpenDoor && command.params[0] == targetZoneIndex)
            {
                return false;
            }
        }

        mapfmt::EventCommand command;
        command.type = mapfmt::CommandType::OpenDoor;
        command.params[0] = static_cast<int16_t>(targetZoneIndex);
        commands.push_back(command);
        return true;
    }

    bool AddOpenDoorLinkToEvent(int eventIndex, int targetZoneIndex)
    {
        if (EventScriptHasAdvancedRaw(eventIndex)) return false;
        if (eventIndex < 0 || eventIndex >= static_cast<int>(g_app.document.events.size())) return false;
        if (targetZoneIndex < 0 || targetZoneIndex >= static_cast<int>(g_app.document.zones.size())) return false;

        int primaryTarget = targetZoneIndex;
        if (IsWallZone(g_app.document.zones[targetZoneIndex]))
        {
            primaryTarget = GetCanonicalWallIndex(targetZoneIndex);
        }

        bool changed = PrepareZoneAsOpenDoorMover(primaryTarget);

        const int backfaceTarget = FindReverseWallPairIndex(primaryTarget);
        if (backfaceTarget >= 0)
        {
            // Editor-created two-sided walls have a visual reverse duplicate.
            // Gloom's OpenDoor moves only the zone referenced by the event. If
            // only the collision/front wall is linked, the door sound plays and
            // collision opens, but the reverse visual wall stays in place. Link
            // both halves so the visible two-sided door moves as one object.
            SyncBackfaceWallFromFront(primaryTarget, backfaceTarget);
            changed = PrepareZoneAsOpenDoorMover(backfaceTarget) || changed;
        }

        auto& commands = g_app.document.events[eventIndex].commands;
        changed = AddSingleOpenDoorCommandIfMissing(commands, primaryTarget) || changed;
        if (backfaceTarget >= 0)
        {
            changed = AddSingleOpenDoorCommandIfMissing(commands, backfaceTarget) || changed;
        }
        return changed;
    }

    bool MoveGroupPointsClose(POINT a, POINT b)
    {
        constexpr int tolerance = 4;
        return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance;
    }

    bool MoveGroupWallsShareEndpoint(const mapfmt::Zone& a, const mapfmt::Zone& b)
    {
        const POINT a1{ static_cast<LONG>(a.x1), static_cast<LONG>(a.z1) };
        const POINT a2{ static_cast<LONG>(a.x2), static_cast<LONG>(a.z2) };
        const POINT b1{ static_cast<LONG>(b.x1), static_cast<LONG>(b.z1) };
        const POINT b2{ static_cast<LONG>(b.x2), static_cast<LONG>(b.z2) };
        return MoveGroupPointsClose(a1, b1) || MoveGroupPointsClose(a1, b2) ||
               MoveGroupPointsClose(a2, b1) || MoveGroupPointsClose(a2, b2);
    }

    bool MoveGroupWallsAreNearlyCollinear(const mapfmt::Zone& a, const mapfmt::Zone& b)
    {
        const double ax = static_cast<double>(a.x2) - static_cast<double>(a.x1);
        const double az = static_cast<double>(a.z2) - static_cast<double>(a.z1);
        const double bx = static_cast<double>(b.x2) - static_cast<double>(b.x1);
        const double bz = static_cast<double>(b.z2) - static_cast<double>(b.z1);
        const double alen = std::hypot(ax, az);
        const double blen = std::hypot(bx, bz);
        if (alen < 1.0 || blen < 1.0)
        {
            return false;
        }

        const double cross = std::abs(ax * bz - az * bx);
        if (cross > (alen * blen * 0.035))
        {
            return false;
        }

        const double dot = (ax * bx + az * bz) / (alen * blen);
        if (dot < 0.85)
        {
            return false;
        }

        // Parallel alone is not enough: two opposite sides of a closed wall
        // block are parallel, but OpenDoor cannot translate that block as a
        // rigid object. Require the candidate to lie on the same line as the
        // selected moving face.
        const double pointCross = std::abs(ax * (static_cast<double>(b.x1) - static_cast<double>(a.x1)) -
                                           az * (static_cast<double>(b.z1) - static_cast<double>(a.z1)));
        return pointCross <= (alen * 4.0);
    }

    void CollectCanonicalMoveWallLineGroup(int targetZoneIndex, std::vector<int>& outFronts)
    {
        outFronts.clear();
        int target = GetCanonicalWallIndex(targetZoneIndex);
        if (target < 0 || target >= static_cast<int>(g_app.document.zones.size()) || !IsCanonicalRotateWallIndex(target))
        {
            return;
        }

        const int zoneTotal = static_cast<int>(g_app.document.zones.size());
        std::vector<uint8_t> used(static_cast<size_t>(zoneTotal), 0);
        outFronts.push_back(target);
        used[static_cast<size_t>(target)] = 1;

        for (size_t cursor = 0; cursor < outFronts.size(); ++cursor)
        {
            const int current = outFronts[cursor];
            const auto& currentZone = g_app.document.zones[current];
            for (int i = 0; i < zoneTotal; ++i)
            {
                if (used[static_cast<size_t>(i)] || !IsCanonicalRotateWallIndex(i)) continue;
                const auto& candidate = g_app.document.zones[i];
                if (!MoveGroupWallsShareEndpoint(currentZone, candidate)) continue;
                if (!MoveGroupWallsAreNearlyCollinear(g_app.document.zones[target], candidate)) continue;

                used[static_cast<size_t>(i)] = 1;
                outFronts.push_back(i);
            }
        }
    }

    std::vector<int> BuildOpenDoorRawTargetsForFronts(const std::vector<int>& fronts)
    {
        std::vector<int> targets;
        targets.reserve(fronts.size() * 2);
        for (int front : fronts)
        {
            AddUniqueIndex(targets, front);
            const int backface = FindReverseWallPairIndex(front);
            if (backface >= 0)
            {
                AddUniqueIndex(targets, backface);
            }
        }
        return targets;
    }

    bool RemoveOpenDoorCommandsTargeting(std::vector<mapfmt::EventCommand>& commands, const std::vector<int>& rawTargets)
    {
        if (rawTargets.empty()) return false;
        const auto oldSize = commands.size();
        commands.erase(std::remove_if(commands.begin(), commands.end(), [&](const mapfmt::EventCommand& command)
        {
            if (command.type != mapfmt::CommandType::OpenDoor) return false;
            return std::find(rawTargets.begin(), rawTargets.end(), static_cast<int>(command.params[0])) != rawTargets.end();
        }), commands.end());
        return commands.size() != oldSize;
    }

    bool AddMoveWallGroupLinkToEvent(int eventIndex, int sourceZoneIndex, int guideZoneIndex, int& targetZoneIndex)
    {
        if (EventScriptHasAdvancedRaw(eventIndex)) return false;
        if (eventIndex < 0 || eventIndex >= static_cast<int>(g_app.document.events.size())) return false;
        if (sourceZoneIndex < 0 || sourceZoneIndex >= static_cast<int>(g_app.document.zones.size())) return false;
        if (guideZoneIndex < 0 || guideZoneIndex >= static_cast<int>(g_app.document.zones.size())) return false;
        if (!IsWallZone(g_app.document.zones[sourceZoneIndex]) || !IsWallZone(g_app.document.zones[guideZoneIndex])) return false;

        int moveTargetZoneIndex = GetCanonicalWallIndex(sourceZoneIndex);
        int guideTargetZoneIndex = GetCanonicalWallIndex(guideZoneIndex);
        if (moveTargetZoneIndex < 0 || moveTargetZoneIndex >= static_cast<int>(g_app.document.zones.size())) return false;
        if (guideTargetZoneIndex < 0 || guideTargetZoneIndex >= static_cast<int>(g_app.document.zones.size())) return false;
        if (moveTargetZoneIndex == guideTargetZoneIndex) return false;

        // Zombie Massacre map1b stores a sliding block as two consecutive polygon
        // runs: the visible start block, directly followed by the same block at
        // the target position with a/b cleared to 0.  The target run is therefore
        // not guessed from the wall normal; it must come from a duplicate guide
        // block the mapper placed at the desired destination.
        MoveWallGroupRunInfo run;
        if (!PrepareMoveWallGroupRunForEvent(moveTargetZoneIndex, guideTargetZoneIndex, &run)) return false;
        targetZoneIndex = moveTargetZoneIndex;
        if (run.activeFirst < 0 || run.activeCount <= 0 || run.guideFirst < 0 || run.guideCount != run.activeCount) return false;

        auto& commands = g_app.document.events[eventIndex].commands;
        bool changed = false;

        std::vector<int> packedTargets;
        packedTargets.reserve(static_cast<size_t>(run.activeCount + run.guideCount));
        for (int i = 0; i < run.activeCount; ++i)
        {
            AddUniqueIndex(packedTargets, run.activeFirst + i);
        }
        for (int i = 0; i < run.guideCount; ++i)
        {
            AddUniqueIndex(packedTargets, run.guideFirst + i);
        }

        changed = RemoveOpenDoorCommandsTargeting(commands, packedTargets) || changed;

        RotateWallRunInfo removeRun;
        removeRun.frontFirst = run.activeFirst;
        removeRun.frontCount = run.activeCount;
        removeRun.backFirst = run.guideFirst;
        removeRun.backCount = run.guideCount;
        const auto oldSize = commands.size();
        commands.erase(std::remove_if(commands.begin(), commands.end(), [&](const mapfmt::EventCommand& command)
        {
            return RotateCommandIntersectsRun(command, removeRun) ||
                   (command.type == mapfmt::CommandType::RotatePoly && EventCommandTargetsZone(command, targetZoneIndex));
        }), commands.end());
        changed = changed || (commands.size() != oldSize);

        constexpr int kMoveWallGroupDistance = 384;
        constexpr int kMoveWallGroupFlags = 1;
        AddRotateCommand(commands, run.activeFirst, run.activeCount, kMoveWallGroupDistance, kMoveWallGroupFlags);
        changed = true;
        return changed;
    }

    bool UpsertSingleChangeTextureCommand(std::vector<mapfmt::EventCommand>& commands, int targetZoneIndex, int newTextureIndex)
    {
        if (targetZoneIndex < 0 || targetZoneIndex >= static_cast<int>(g_app.document.zones.size())) return false;
        newTextureIndex = ClampValue(newTextureIndex, 0, 159);

        for (auto& command : commands)
        {
            if (command.type == mapfmt::CommandType::ChangeTexture && command.params[0] == targetZoneIndex)
            {
                if (command.params[1] == newTextureIndex)
                {
                    return false;
                }
                command.params[1] = static_cast<int16_t>(newTextureIndex);
                return true;
            }
        }

        mapfmt::EventCommand command;
        command.type = mapfmt::CommandType::ChangeTexture;
        command.params[0] = static_cast<int16_t>(targetZoneIndex);
        command.params[1] = static_cast<int16_t>(newTextureIndex);
        commands.push_back(command);
        return true;
    }

    bool IsSwitchTextureMoverCommand(const mapfmt::EventCommand& command)
    {
        // map1_4 orders the switch-panel texture change before the geometry
        // action that the same trigger starts: AddMonster..., ChangeTexture,
        // RotatePoly.  Keep editor-created switch events in that order too;
        // some game builds do not visibly apply a panel ChangeTexture once the
        // rotating/moving geometry command has already taken over the event.
        return command.type == mapfmt::CommandType::OpenDoor ||
               command.type == mapfmt::CommandType::RotatePoly ||
               command.type == mapfmt::CommandType::Teleport;
    }

    bool ChangeTextureCommandTargetsSameDisplayedWall(const mapfmt::EventCommand& command, int targetZoneIndex)
    {
        if (command.type != mapfmt::CommandType::ChangeTexture) return false;
        const int rawTarget = static_cast<int>(command.params[0]);
        if (rawTarget == targetZoneIndex) return true;

        const int wantedDisplayTarget = DisplayZoneIndexForEventTarget(targetZoneIndex);
        if (wantedDisplayTarget < 0) return false;
        return DisplayZoneIndexForEventTarget(rawTarget) == wantedDisplayTarget;
    }

    bool SameEventCommands(const std::vector<mapfmt::EventCommand>& a, const std::vector<mapfmt::EventCommand>& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (a[i].type != b[i].type) return false;
            if (a[i].params != b[i].params) return false;
            if (a[i].listValues != b[i].listValues) return false;
        }
        return true;
    }

    bool UpsertSwitchChangeTextureCommand(std::vector<mapfmt::EventCommand>& commands, int targetZoneIndex, int newTextureIndex)
    {
        if (targetZoneIndex < 0 || targetZoneIndex >= static_cast<int>(g_app.document.zones.size())) return false;
        newTextureIndex = ClampValue(newTextureIndex, 0, 159);

        mapfmt::EventCommand switchCommand;
        switchCommand.type = mapfmt::CommandType::ChangeTexture;
        switchCommand.params[0] = static_cast<int16_t>(targetZoneIndex);
        switchCommand.params[1] = static_cast<int16_t>(newTextureIndex);

        std::vector<mapfmt::EventCommand> rebuilt;
        rebuilt.reserve(commands.size() + 1);
        for (const auto& command : commands)
        {
            if (ChangeTextureCommandTargetsSameDisplayedWall(command, targetZoneIndex))
            {
                // Drop old OFF-frame entries such as ChangeTexture Z76 -> 37,
                // and also collapse duplicate front/back/display-equivalent
                // switch commands into the one exact raw target clicked by user.
                continue;
            }
            rebuilt.push_back(command);
        }

        auto insertIt = std::find_if(rebuilt.begin(), rebuilt.end(), IsSwitchTextureMoverCommand);
        rebuilt.insert(insertIt, switchCommand);

        if (SameEventCommands(commands, rebuilt))
        {
            return false;
        }
        commands.swap(rebuilt);
        return true;
    }

    bool AddSwitchTextureLinkToEvent(int eventIndex, int targetZoneIndex)
    {
        if (EventScriptHasAdvancedRaw(eventIndex)) return false;
        if (eventIndex < 0 || eventIndex >= static_cast<int>(g_app.document.events.size())) return false;
        if (targetZoneIndex < 0 || targetZoneIndex >= static_cast<int>(g_app.document.zones.size())) return false;
        if (!IsWallZone(g_app.document.zones[targetZoneIndex])) return false;

        // ChangeTexture in the original maps addresses the exact target wall
        // zone, not a normalized/canonical front/back pair. Keep that behaviour
        // so an editor-created switch targets exactly the clicked wall/panel.
        // The event texture is derived from that OFF panel: static panels use
        // OFF+1; animated OFF panels use the first texture after the animation
        // range, e.g. 37-38 OFF animation -> 39 green ON frame. Insert this
        // before mover/rotator commands to match map1_4 event ordering.
        const int offTextureIndex = GetSwitchOffTextureIndexForTargetZone(targetZoneIndex);
        const int newTextureIndex = GetSwitchAutoOnTextureIndexForTargetZone(targetZoneIndex);
        if (newTextureIndex < 0) return false;
        if (offTextureIndex >= 0)
        {
            EnsureAnimationEntryForTextureIndex(offTextureIndex);
        }

        auto& commands = g_app.document.events[eventIndex].commands;
        return UpsertSwitchChangeTextureCommand(commands, targetZoneIndex, newTextureIndex);
    }

    void FlipSelectedDoorDirection()
    {
        if (g_app.selectedZone < 0 || g_app.selectedZone >= static_cast<int>(g_app.document.zones.size()))
        {
            MessageBoxW(g_app.mainWindow, L"Select the wall/door zone first.", L"ZGloom Editor", MB_OK | MB_ICONINFORMATION);
            return;
        }

        auto& zone = g_app.document.zones[g_app.selectedZone];
        if (zone.ztype != static_cast<int16_t>(mapfmt::ZoneType::Wall))
        {
            MessageBoxW(g_app.mainWindow, L"Door movement direction can only be flipped on wall/door zones.", L"ZGloom Editor", MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (!IsZoneControlledByOpenDoor(g_app.selectedZone))
        {
            const int result = MessageBoxW(g_app.mainWindow,
                L"This wall is not currently targeted by an OpenDoor event.\n\nFlip its line direction anyway?",
                L"ZGloom Editor", MB_YESNO | MB_ICONQUESTION);
            if (result != IDYES)
            {
                return;
            }
        }

        PushUndoSnapshot();
        const int canonical = GetCanonicalWallIndex(g_app.selectedZone);
        if (canonical >= 0 && canonical < static_cast<int>(g_app.document.zones.size()))
        {
            ReverseWallDirectionPreservingTexture(g_app.document.zones[canonical]);
            const int backface = FindReverseWallPairIndex(canonical);
            if (backface >= 0)
            {
                SyncBackfaceWallFromFront(canonical, backface);
            }
            g_app.selectedZone = canonical;
        }
        else
        {
            ReverseWallDirectionPreservingTexture(zone);
        }
        MarkDirty();
        RefreshZoneList();
        RefreshPreviewImage();
        RefreshStatus();
        InvalidateEditorViews();
    }

    void StartLinkEventToZoneTool()
    {
        ResetPendingTeleportTarget();
        ClearSelectedMonsterSpawn();
        ClearSelectedTeleportTarget();
        if (g_app.selectedZone < 0 || g_app.selectedZone >= static_cast<int>(g_app.document.zones.size()))
        {
            MessageBoxW(g_app.mainWindow, L"Select an Event Trigger line first, then choose Link Event > Wall/Door.", L"ZGloom Editor", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const auto& zone = g_app.document.zones[g_app.selectedZone];
        if (!IsLinearZoneType(zone.ztype) || zone.ztype == static_cast<int16_t>(mapfmt::ZoneType::Wall) || IsLevelEndZone(zone))
        {
            MessageBoxW(g_app.mainWindow, L"Select a normal Event Trigger line first. Level End and wall zones cannot be used as trigger sources.", L"ZGloom Editor", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const int eventIndex = EventSlotFromZoneEventValue(zone.ev);
        if (eventIndex >= 0 && WarnIfEventScriptAdvancedRaw(eventIndex))
        {
            return;
        }

        g_app.insertMode = InsertMode::LinkEventToZone;
        g_app.linkEventTriggerZone = g_app.selectedZone;
        g_app.linkEventIndex = eventIndex;
        ResetPendingMoveWallGroup();
        ResetPendingTeleportTarget();
        g_app.isDrawing = false;
        g_app.isPanning = false;
        g_app.drawWallAngleLock = false;
        g_app.drawWallLengthSnapLock = false;
        UpdateModeButtons();
        RefreshPreviewImage();
        RefreshStatus();
        InvalidateEditorViews();
    }

    bool FinishLinkEventToZone(int targetZoneIndex)
    {
        if (g_app.insertMode != InsertMode::LinkEventToZone) return false;
        if (g_app.linkEventTriggerZone < 0 || g_app.linkEventTriggerZone >= static_cast<int>(g_app.document.zones.size()))
        {
            ClearInsertMode();
            return true;
        }
        if (targetZoneIndex < 0 || targetZoneIndex >= static_cast<int>(g_app.document.zones.size())) return true;
        if (targetZoneIndex == g_app.linkEventTriggerZone) return true;

        const auto& target = g_app.document.zones[targetZoneIndex];
        if (IsEventTriggerLineZone(target) || IsLevelEndZone(target))
        {
            MessageBoxW(g_app.mainWindow, L"Choose the door/wall/target zone, not another trigger line.", L"ZGloom Editor", MB_OK | MB_ICONINFORMATION);
            return true;
        }

        PushUndoSnapshot();
        const int eventIndex = EnsureTriggerHasEventSlot(g_app.linkEventTriggerZone);
        if (eventIndex < 0)
        {
            UndoLastChange();
            MessageBoxW(g_app.mainWindow, L"Could not assign an event slot to this trigger.", L"ZGloom Editor", MB_OK | MB_ICONERROR);
            return true;
        }

        AddOpenDoorLinkToEvent(eventIndex, targetZoneIndex);
        g_app.linkEventIndex = eventIndex;
        g_app.selectedZone = targetZoneIndex;
        g_app.insertMode = InsertMode::None;
        g_app.linkEventTriggerZone = -1;
        MarkDirty();
        RefreshZoneList();
        RefreshPreviewImage();
        UpdateModeButtons();
        RefreshStatus();
        InvalidateEditorViews();
        return true;
    }


    bool CanUseSelectedEventTriggerAsSource(const wchar_t* message)
    {
        ClearSelectedMonsterSpawn();
        ClearSelectedTeleportTarget();
        if (g_app.selectedZone < 0 || g_app.selectedZone >= static_cast<int>(g_app.document.zones.size()))
        {
            MessageBoxW(g_app.mainWindow, message, L"ZGloom Editor", MB_OK | MB_ICONINFORMATION);
            return false;
        }

        const auto& zone = g_app.document.zones[g_app.selectedZone];
        if (!IsLinearZoneType(zone.ztype) || zone.ztype == static_cast<int16_t>(mapfmt::ZoneType::Wall) || IsLevelEndZone(zone))
        {
            MessageBoxW(g_app.mainWindow, L"Select a normal Event Trigger line first. Level End and wall zones cannot be used as trigger sources.", L"ZGloom Editor", MB_OK | MB_ICONINFORMATION);
            return false;
        }
        return true;
    }

    void BeginEventTriggerTargetTool(InsertMode mode, const wchar_t* message)
    {
        if (!CanUseSelectedEventTriggerAsSource(message)) return;
        const auto& zone = g_app.document.zones[g_app.selectedZone];
        const int eventIndex = EventSlotFromZoneEventValue(zone.ev);
        if (eventIndex >= 0 && WarnIfEventScriptAdvancedRaw(eventIndex))
        {
            return;
        }

        g_app.insertMode = mode;
        g_app.linkEventTriggerZone = g_app.selectedZone;
        g_app.linkEventIndex = eventIndex;
        ResetPendingMoveWallGroup();
        ResetPendingTeleportTarget();
        g_app.isDrawing = false;
        g_app.isPanning = false;
        g_app.drawWallAngleLock = false;
        g_app.drawWallLengthSnapLock = false;
        UpdateModeButtons();
        RefreshPreviewImage();
        RefreshStatus();
        InvalidateEditorViews();
    }

    void StartLinkEventToSwitchTextureTool()
    {
        BeginEventTriggerTargetTool(InsertMode::LinkEventToSwitchTexture,
            L"Select an Event Trigger line first, then choose Link Event > Switch/Trigger.");

        if (g_app.insertMode == InsertMode::LinkEventToSwitchTexture)
        {
            RefreshPreviewImage();
            RefreshStatus();
            InvalidateEditorViews();
        }
    }

    void StartLinkEventToEnemyObjectTool()
    {
        BeginEventTriggerTargetTool(InsertMode::LinkEventToEnemyObject,
            L"Select an Event Trigger line first, then choose Link Event > Enemy/Objects.");
    }

    void StartDeleteLinkEventTool()
    {
        if (!DeleteSelectedEventLink())
        {
            MessageBoxW(g_app.mainWindow,
                L"Select a linked enemy/object/weapon or a wall/door/switch target that is currently controlled by an Event Trigger.",
                L"ZGloom Editor", MB_OK | MB_ICONINFORMATION);
            UpdateModeButtons();
            RefreshStatus();
        }
    }

    void StartLinkEventToRotateTool(bool clockwise)
    {
        BeginEventTriggerTargetTool(clockwise ? InsertMode::LinkEventToRotateClockwise : InsertMode::LinkEventToRotateCounterClockwise,
            L"Select an Event Trigger line first, then choose Link Event > Rotate CW/CCW.");
    }

    void StartLinkEventToMoveWallGroupTool()
    {
        BeginEventTriggerTargetTool(InsertMode::LinkEventToMoveWallGroup,
            L"Select an Event Trigger line first, then choose Link Event > Move Wallblock.");
        if (g_app.insertMode == InsertMode::LinkEventToMoveWallGroup)
        {
            ResetPendingMoveWallGroup();
            RefreshStatus();
            UpdateModeButtons();
            InvalidateEditorViews();
        }
    }

    void StartSetTeleportTargetTool()
    {
        ResetPendingTeleportTarget();
        BeginEventTriggerTargetTool(InsertMode::SetTeleportTarget,
            L"Select an Event Trigger line first, then choose Set Teleport Target.");
    }

    void ResetPendingMoveWallGroup()
    {
        g_app.pendingMoveWallGroupSourceZone = -1;
        g_app.pendingMoveWallGroupTargetPlacementActive = false;
        g_app.pendingMoveWallGroupGuideFirst = -1;
        g_app.pendingMoveWallGroupGuideCount = 0;
        g_app.pendingMoveWallGroupSourcePickWorld = POINT{};
        g_app.pendingMoveWallGroupTargetPickWorld = POINT{};
        g_app.pendingMoveWallGroupTargetOffset = POINT{};
    }

    bool CollectPendingMoveWallGroupFronts(std::vector<int>& fronts)
    {
        fronts.clear();
        if (g_app.pendingMoveWallGroupSourceZone < 0 ||
            g_app.pendingMoveWallGroupSourceZone >= static_cast<int>(g_app.document.zones.size()))
        {
            return false;
        }

        CollectCanonicalRotateWallComponent(g_app.pendingMoveWallGroupSourceZone, fronts);
        std::sort(fronts.begin(), fronts.end());
        fronts.erase(std::unique(fronts.begin(), fronts.end()), fronts.end());
        return !fronts.empty();
    }

    POINT CalculateWallRunCenterWorld(const std::vector<int>& fronts, POINT offset = POINT{})
    {
        if (fronts.empty())
        {
            return POINT{};
        }

        long long sumX = 0;
        long long sumZ = 0;
        int count = 0;
        for (int index : fronts)
        {
            if (index < 0 || index >= static_cast<int>(g_app.document.zones.size())) continue;
            const auto& zone = g_app.document.zones[index];
            if (!IsWallZone(zone)) continue;
            sumX += static_cast<int>(zone.x1) + static_cast<int>(zone.x2) + static_cast<int>(offset.x) * 2;
            sumZ += static_cast<int>(zone.z1) + static_cast<int>(zone.z2) + static_cast<int>(offset.y) * 2;
            count += 2;
        }

        if (count <= 0)
        {
            return POINT{};
        }
        return POINT{ static_cast<LONG>(std::lround(static_cast<double>(sumX) / static_cast<double>(count))),
                      static_cast<LONG>(std::lround(static_cast<double>(sumZ) / static_cast<double>(count))) };
    }

    POINT CalculateZoneRangeCenterWorld(int first, int count)
    {
        std::vector<int> fronts;
        fronts.reserve(static_cast<size_t>(MaxValue(0, count)));
        for (int i = 0; i < count; ++i)
        {
            const int index = first + i;
            if (index >= 0 && index < static_cast<int>(g_app.document.zones.size()) && IsWallZone(g_app.document.zones[index]))
            {
                fronts.push_back(index);
            }
        }
        return CalculateWallRunCenterWorld(fronts);
    }

    bool BeginMoveWallGroupTargetPlacement(int targetZoneIndex, POINT pickWorld)
    {
        if (targetZoneIndex < 0 || targetZoneIndex >= static_cast<int>(g_app.document.zones.size()) ||
            !IsWallZone(g_app.document.zones[targetZoneIndex]))
        {
            MessageBoxW(g_app.mainWindow, L"Click one segment of the wall block that should move.", L"ZGloom Editor", MB_OK | MB_ICONINFORMATION);
            return false;
        }

        const int canonicalTarget = GetCanonicalWallIndex(targetZoneIndex);
        if (canonicalTarget >= 0 && canonicalTarget < static_cast<int>(g_app.document.zones.size()) &&
            g_app.document.zones[canonicalTarget].a == 0 && g_app.document.zones[canonicalTarget].b == 0)
        {
            MessageBoxW(g_app.mainWindow, L"This is a hidden move target guide. Click the visible source wall block instead.", L"ZGloom Editor", MB_OK | MB_ICONINFORMATION);
            return false;
        }

        std::vector<int> sourceFronts;
        CollectCanonicalRotateWallComponent(canonicalTarget, sourceFronts);
        if (sourceFronts.empty())
        {
            MessageBoxW(g_app.mainWindow, L"Could not detect a connected source wall block.", L"ZGloom Editor", MB_OK | MB_ICONERROR);
            return false;
        }

        g_app.pendingMoveWallGroupSourceZone = canonicalTarget;
        g_app.pendingMoveWallGroupTargetPlacementActive = true;
        g_app.pendingMoveWallGroupSourcePickWorld = SnapWorldPointToFineGrid(pickWorld);
        g_app.pendingMoveWallGroupTargetPickWorld = g_app.pendingMoveWallGroupSourcePickWorld;
        g_app.pendingMoveWallGroupTargetOffset = POINT{};
        g_app.selectedZone = canonicalTarget;
        ClearSelectedMonsterSpawn();
        ClearSelectedTeleportTarget();
        RefreshPreviewImage();
        UpdateModeButtons();
        RefreshStatus();
        InvalidateEditorViews();
        return true;
    }

    bool UpdateMoveWallGroupTargetPlacement(POINT world)
    {
        if (!g_app.pendingMoveWallGroupTargetPlacementActive)
        {
            return false;
        }

        const POINT target = SnapWorldPointToFineGrid(world);
        const POINT offset{
            static_cast<LONG>(target.x - g_app.pendingMoveWallGroupSourcePickWorld.x),
            static_cast<LONG>(target.y - g_app.pendingMoveWallGroupSourcePickWorld.y)
        };

        if (target.x == g_app.pendingMoveWallGroupTargetPickWorld.x &&
            target.y == g_app.pendingMoveWallGroupTargetPickWorld.y &&
            offset.x == g_app.pendingMoveWallGroupTargetOffset.x &&
            offset.y == g_app.pendingMoveWallGroupTargetOffset.y)
        {
            return true;
        }

        g_app.pendingMoveWallGroupTargetPickWorld = target;
        g_app.pendingMoveWallGroupTargetOffset = offset;
        RefreshStatus();
        InvalidateEditorViews();
        return true;
    }

    int AppendMoveWallGroupGuideZonesFromOffset(const std::vector<int>& sourceFronts, POINT offset)
    {
        if (sourceFronts.empty())
        {
            return -1;
        }

        const int guideFirst = static_cast<int>(g_app.document.zones.size());
        for (int front : sourceFronts)
        {
            if (front < 0 || front >= static_cast<int>(g_app.document.zones.size()) || !IsWallZone(g_app.document.zones[front]))
            {
                return -1;
            }

            mapfmt::Zone guide = g_app.document.zones[front];
            guide.x1 = ClampWorldToInt16(static_cast<int>(guide.x1) + static_cast<int>(offset.x));
            guide.z1 = ClampWorldToInt16(static_cast<int>(guide.z1) + static_cast<int>(offset.y));
            guide.x2 = ClampWorldToInt16(static_cast<int>(guide.x2) + static_cast<int>(offset.x));
            guide.z2 = ClampWorldToInt16(static_cast<int>(guide.z2) + static_cast<int>(offset.y));
            mapfmt::RecalculateWallMetadata(guide);
            UpdateWallTextureBandCountFromLength(guide);
            MakeMoveWallGroupGuideAiNeutral(guide);
            g_app.document.zones.push_back(guide);
        }

        g_app.pendingMoveWallGroupGuideFirst = guideFirst;
        g_app.pendingMoveWallGroupGuideCount = static_cast<int>(sourceFronts.size());
        return guideFirst;
    }

    bool CommitMoveWallGroupTargetPlacement(POINT world)
    {
        if (g_app.insertMode != InsertMode::LinkEventToMoveWallGroup || !g_app.pendingMoveWallGroupTargetPlacementActive)
        {
            return false;
        }
        if (g_app.linkEventTriggerZone < 0 || g_app.linkEventTriggerZone >= static_cast<int>(g_app.document.zones.size()))
        {
            ClearInsertMode();
            return true;
        }

        UpdateMoveWallGroupTargetPlacement(world);
        const POINT offset = g_app.pendingMoveWallGroupTargetOffset;
        if (offset.x == 0 && offset.y == 0)
        {
            MessageBoxW(g_app.mainWindow, L"Move the copied wall block to a different grid position before placing it.", L"ZGloom Editor", MB_OK | MB_ICONINFORMATION);
            return true;
        }

        std::vector<int> sourceFronts;
        if (!CollectPendingMoveWallGroupFronts(sourceFronts))
        {
            MessageBoxW(g_app.mainWindow, L"Could not detect the selected source wall block anymore.", L"ZGloom Editor", MB_OK | MB_ICONERROR);
            ResetPendingMoveWallGroup();
            UpdateModeButtons();
            RefreshStatus();
            InvalidateEditorViews();
            return true;
        }

        PushUndoSnapshot();
        const int eventIndex = EnsureTriggerHasEventSlot(g_app.linkEventTriggerZone);
        if (eventIndex < 0)
        {
            UndoLastChange();
            MessageBoxW(g_app.mainWindow, L"Could not assign an event slot to this trigger.", L"ZGloom Editor", MB_OK | MB_ICONERROR);
            return true;
        }

        const int guideFirst = AppendMoveWallGroupGuideZonesFromOffset(sourceFronts, offset);
        if (guideFirst < 0)
        {
            UndoLastChange();
            g_app.pendingMoveWallGroupGuideFirst = -1;
            g_app.pendingMoveWallGroupGuideCount = 0;
            MessageBoxW(g_app.mainWindow, L"Could not create the hidden target guide block.", L"ZGloom Editor", MB_OK | MB_ICONERROR);
            return true;
        }

        int moveTargetZoneIndex = g_app.pendingMoveWallGroupSourceZone;
        if (!AddMoveWallGroupLinkToEvent(eventIndex, g_app.pendingMoveWallGroupSourceZone, guideFirst, moveTargetZoneIndex))
        {
            UndoLastChange();
            g_app.pendingMoveWallGroupGuideFirst = -1;
            g_app.pendingMoveWallGroupGuideCount = 0;
            MessageBoxW(g_app.mainWindow,
                L"Could not build a map1b-style move wallblock from the selected block and target position.",
                L"ZGloom Editor", MB_OK | MB_ICONERROR);
            return true;
        }

        g_app.linkEventIndex = eventIndex;
        g_app.selectedZone = GetCanonicalWallIndex(moveTargetZoneIndex);
        g_app.insertMode = InsertMode::None;
        g_app.linkEventTriggerZone = -1;
        ResetPendingMoveWallGroup();
        if (GetCapture() == g_app.canvas) ReleaseCapture();
        MarkDirty();
        RefreshZoneList();
        RefreshPreviewImage();
        UpdateModeButtons();
        RefreshStatus();
        UpdateCanvasScrollBars(g_app.canvas);
        InvalidateEditorViews();
        return true;
    }

    bool IsMoveWallGroupRotateCommand(const mapfmt::EventCommand& command)
    {
        if (command.type != mapfmt::CommandType::RotatePoly)
        {
            return false;
        }

        const int distance = std::abs(static_cast<int>(command.params[2]));
        const int flags = static_cast<int>(command.params[3]);
        return distance == 384 && (flags == 1 || flags == 3);
    }

    std::vector<uint8_t> BuildMoveWallGroupGuideMaskForDocument(const mapfmt::MapDocument& document)
    {
        std::vector<uint8_t> guideMask(document.zones.size(), 0);
        for (const auto& script : document.events)
        {
            if (script.hasUnsupportedRaw)
            {
                continue;
            }

            for (const auto& command : script.commands)
            {
                if (!IsMoveWallGroupRotateCommand(command))
                {
                    continue;
                }

                const int first = static_cast<int>(command.params[0]);
                const int count = MaxValue(1, static_cast<int>(command.params[1]));
                const int guideFirst = first + count;
                if (first < 0 || guideFirst < 0 || guideFirst + count > static_cast<int>(document.zones.size()))
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

    bool IsMoveWallGroupGuideZoneIndex(const std::vector<uint8_t>& guideMask, int zoneIndex)
    {
        return zoneIndex >= 0 &&
               zoneIndex < static_cast<int>(guideMask.size()) &&
               guideMask[static_cast<size_t>(zoneIndex)] != 0;
    }

    void DrawDottedArrowLine(HDC hdc, const POINT& from, const POINT& to, COLORREF color)
    {
        HPEN dottedPen = CreatePen(PS_DOT, 1, color);
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, dottedPen));
        MoveToEx(hdc, from.x, from.y, nullptr);
        LineTo(hdc, to.x, to.y);
        SelectObject(hdc, oldPen);
        DeleteObject(dottedPen);

        const double dx = static_cast<double>(to.x - from.x);
        const double dy = static_cast<double>(to.y - from.y);
        const double len = std::sqrt(dx * dx + dy * dy);
        if (len < 3.0)
        {
            return;
        }

        const double ux = dx / len;
        const double uy = dy / len;
        const double px = -uy;
        const double py = ux;
        const int arrowLen = 12;
        const int arrowHalf = 5;
        POINT arrow[3] =
        {
            to,
            POINT{ static_cast<LONG>(std::lround(static_cast<double>(to.x) - ux * arrowLen + px * arrowHalf)),
                   static_cast<LONG>(std::lround(static_cast<double>(to.y) - uy * arrowLen + py * arrowHalf)) },
            POINT{ static_cast<LONG>(std::lround(static_cast<double>(to.x) - ux * arrowLen - px * arrowHalf)),
                   static_cast<LONG>(std::lround(static_cast<double>(to.y) - uy * arrowLen - py * arrowHalf)) }
        };

        HPEN arrowPen = CreatePen(PS_SOLID, 1, color);
        HBRUSH arrowBrush = CreateSolidBrush(color);
        HPEN oldArrowPen = static_cast<HPEN>(SelectObject(hdc, arrowPen));
        HBRUSH oldArrowBrush = static_cast<HBRUSH>(SelectObject(hdc, arrowBrush));
        Polygon(hdc, arrow, 3);
        SelectObject(hdc, oldArrowBrush);
        SelectObject(hdc, oldArrowPen);
        DeleteObject(arrowBrush);
        DeleteObject(arrowPen);
    }

    void DrawMoveWallGroupPlacementPreview(HDC hdc, const RECT& rc, const mapfmt::Bounds& bounds)
    {
        if (!g_app.pendingMoveWallGroupTargetPlacementActive)
        {
            return;
        }

        std::vector<int> fronts;
        if (!CollectPendingMoveWallGroupFronts(fronts))
        {
            return;
        }

        const POINT offset = g_app.pendingMoveWallGroupTargetOffset;
        const POINT sourceCenter = CalculateWallRunCenterWorld(fronts);
        const POINT targetCenter = CalculateWallRunCenterWorld(fronts, offset);
        DrawDottedArrowLine(hdc, WorldToScreen(rc, bounds, sourceCenter.x, sourceCenter.y),
            WorldToScreen(rc, bounds, targetCenter.x, targetCenter.y), RGB(145, 155, 170));

        HPEN sourcePen = CreatePen(PS_SOLID, 4, RGB(255, 220, 64));
        HPEN ghostPen = CreatePen(PS_DASH, 3, RGB(120, 132, 150));
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, sourcePen));
        for (int front : fronts)
        {
            if (front < 0 || front >= static_cast<int>(g_app.document.zones.size())) continue;
            const auto& zone = g_app.document.zones[front];
            POINT p1 = WorldToScreen(rc, bounds, zone.x1, zone.z1);
            POINT p2 = WorldToScreen(rc, bounds, zone.x2, zone.z2);
            MoveToEx(hdc, p1.x, p1.y, nullptr);
            LineTo(hdc, p2.x, p2.y);
        }

        SelectObject(hdc, ghostPen);
        for (int front : fronts)
        {
            if (front < 0 || front >= static_cast<int>(g_app.document.zones.size())) continue;
            const auto& zone = g_app.document.zones[front];
            POINT p1 = WorldToScreen(rc, bounds, static_cast<int>(zone.x1) + static_cast<int>(offset.x), static_cast<int>(zone.z1) + static_cast<int>(offset.y));
            POINT p2 = WorldToScreen(rc, bounds, static_cast<int>(zone.x2) + static_cast<int>(offset.x), static_cast<int>(zone.z2) + static_cast<int>(offset.y));
            MoveToEx(hdc, p1.x, p1.y, nullptr);
            LineTo(hdc, p2.x, p2.y);
        }

        SelectObject(hdc, oldPen);
        DeleteObject(ghostPen);
        DeleteObject(sourcePen);
    }

    void DrawMoveWallGroupEventPaths(HDC hdc, const RECT& rc, const mapfmt::Bounds& bounds, int eventIndex)
    {
        if (eventIndex < 0 || eventIndex >= static_cast<int>(g_app.document.events.size()))
        {
            return;
        }

        const auto& commands = g_app.document.events[eventIndex].commands;
        for (const auto& command : commands)
        {
            if (!IsMoveWallGroupRotateCommand(command))
            {
                continue;
            }

            const int first = static_cast<int>(command.params[0]);
            const int count = MaxValue(1, static_cast<int>(command.params[1]));
            const int guideFirst = first + count;
            if (first < 0 || guideFirst < 0 || guideFirst + count > static_cast<int>(g_app.document.zones.size()))
            {
                continue;
            }

            const POINT sourceCenter = CalculateZoneRangeCenterWorld(first, count);
            const POINT targetCenter = CalculateZoneRangeCenterWorld(guideFirst, count);
            DrawDottedArrowLine(hdc, WorldToScreen(rc, bounds, sourceCenter.x, sourceCenter.y),
                WorldToScreen(rc, bounds, targetCenter.x, targetCenter.y), RGB(145, 155, 170));
        }
    }

    void DrawAllMoveWallGroupEventPaths(HDC hdc, const RECT& rc, const mapfmt::Bounds& bounds)
    {
        for (int eventIndex = 0; eventIndex < static_cast<int>(g_app.document.events.size()); ++eventIndex)
        {
            DrawMoveWallGroupEventPaths(hdc, rc, bounds, eventIndex);
        }
    }

    void ResetPendingTeleportTarget()
    {
        g_app.teleportTargetAwaitDirection = false;
        g_app.teleportTargetWorld = POINT{};
        g_app.teleportTargetRotation = 0;
    }

    int RotationUnitsFromWorldDelta(double dx, double dz, int fallbackRot)
    {
        if ((dx * dx + dz * dz) < 1.0)
        {
            return fallbackRot & 255;
        }

        double angle = std::atan2(dx, dz);
        if (angle < 0.0)
        {
            angle += 6.28318530717958647692;
        }
        return static_cast<int>(std::lround((angle * 256.0) / 6.28318530717958647692)) & 255;
    }

    int TeleportRotationFromTargetToWorld(POINT target, POINT facingPoint)
    {
        const int fallbackRot = NormalizeWalkPreviewDir(g_app.walkPreviewDir) * kWalkPreviewRotationUnitsPerStep;
        return RotationUnitsFromWorldDelta(
            static_cast<double>(facingPoint.x) - static_cast<double>(target.x),
            static_cast<double>(facingPoint.y) - static_cast<double>(target.y),
            fallbackRot);
    }

    int WalkPreviewDirFromRotationUnits(int rot)
    {
        return NormalizeWalkPreviewDir(static_cast<int>(std::lround(static_cast<double>(rot & 255) / static_cast<double>(kWalkPreviewRotationUnitsPerStep))));
    }

    void BeginTeleportTargetPlacement(POINT world)
    {
        g_app.teleportTargetAwaitDirection = true;
        g_app.teleportTargetWorld = world;
        g_app.teleportTargetRotation = NormalizeWalkPreviewDir(g_app.walkPreviewDir) * kWalkPreviewRotationUnitsPerStep;
        ClearSelectedMonsterSpawn();
        ClearSelectedTeleportTarget();
        UpdateModeButtons();
        RefreshStatus();
        InvalidateEditorViews();
    }

    bool PointsAlmostSameForRotate(POINT a, POINT b, int tolerance = 4)
    {
        return std::abs(static_cast<int>(a.x) - static_cast<int>(b.x)) <= tolerance &&
               std::abs(static_cast<int>(a.y) - static_cast<int>(b.y)) <= tolerance;
    }

    bool RotateWallEndpointMatches(const mapfmt::Zone& zone, int endpointIndex, POINT point)
    {
        const POINT endpoint = endpointIndex == 0
            ? POINT{ static_cast<LONG>(zone.x1), static_cast<LONG>(zone.z1) }
            : POINT{ static_cast<LONG>(zone.x2), static_cast<LONG>(zone.z2) };
        return PointsAlmostSameForRotate(endpoint, point);
    }

    std::vector<int> OrderAndOrientRotateFronts(const std::vector<int>& fronts)
    {
        std::vector<int> ordered;
        if (fronts.empty())
        {
            return ordered;
        }

        std::vector<int> remaining = fronts;
        std::sort(remaining.begin(), remaining.end(), [&](int lhs, int rhs)
        {
            const auto& a = g_app.document.zones[lhs];
            const auto& b = g_app.document.zones[rhs];
            const int aminx = MinValue(static_cast<int>(a.x1), static_cast<int>(a.x2));
            const int bminx = MinValue(static_cast<int>(b.x1), static_cast<int>(b.x2));
            if (aminx != bminx) return aminx < bminx;
            const int aminz = MinValue(static_cast<int>(a.z1), static_cast<int>(a.z2));
            const int bminz = MinValue(static_cast<int>(b.z1), static_cast<int>(b.z2));
            if (aminz != bminz) return aminz < bminz;
            return lhs < rhs;
        });

        int startOffset = 0;
        for (int i = 0; i < static_cast<int>(remaining.size()); ++i)
        {
            const int candidate = remaining[static_cast<size_t>(i)];
            const auto& zone = g_app.document.zones[candidate];
            int degree1 = 0;
            int degree2 = 0;
            const POINT p1{ static_cast<LONG>(zone.x1), static_cast<LONG>(zone.z1) };
            const POINT p2{ static_cast<LONG>(zone.x2), static_cast<LONG>(zone.z2) };
            for (int otherIndex : remaining)
            {
                if (otherIndex == candidate) continue;
                const auto& other = g_app.document.zones[otherIndex];
                if (RotateWallEndpointMatches(other, 0, p1) || RotateWallEndpointMatches(other, 1, p1)) ++degree1;
                if (RotateWallEndpointMatches(other, 0, p2) || RotateWallEndpointMatches(other, 1, p2)) ++degree2;
            }
            if (degree1 <= 1 || degree2 <= 1)
            {
                startOffset = i;
                if (degree1 > degree2)
                {
                    ReverseWallDirectionPreservingTexture(g_app.document.zones[candidate]);
                }
                break;
            }
        }

        const int first = remaining[static_cast<size_t>(startOffset)];
        ordered.push_back(first);
        remaining.erase(remaining.begin() + startOffset);

        while (!remaining.empty())
        {
            const int lastIndex = ordered.back();
            const auto& lastZone = g_app.document.zones[lastIndex];
            const POINT currentEnd{ static_cast<LONG>(lastZone.x2), static_cast<LONG>(lastZone.z2) };

            int matchOffset = -1;
            bool reverseMatch = false;
            for (int i = 0; i < static_cast<int>(remaining.size()); ++i)
            {
                const auto& candidate = g_app.document.zones[remaining[static_cast<size_t>(i)]];
                if (RotateWallEndpointMatches(candidate, 0, currentEnd))
                {
                    matchOffset = i;
                    reverseMatch = false;
                    break;
                }
                if (RotateWallEndpointMatches(candidate, 1, currentEnd))
                {
                    matchOffset = i;
                    reverseMatch = true;
                    break;
                }
            }

            if (matchOffset < 0)
            {
                ordered.insert(ordered.end(), remaining.begin(), remaining.end());
                break;
            }

            const int nextIndex = remaining[static_cast<size_t>(matchOffset)];
            if (reverseMatch)
            {
                ReverseWallDirectionPreservingTexture(g_app.document.zones[nextIndex]);
            }
            ordered.push_back(nextIndex);
            remaining.erase(remaining.begin() + matchOffset);
        }

        double signedArea = 0.0;
        for (int index : ordered)
        {
            const auto& zone = g_app.document.zones[index];
            signedArea += (static_cast<double>(zone.x1) * static_cast<double>(zone.z2)) -
                          (static_cast<double>(zone.x2) * static_cast<double>(zone.z1));
            mapfmt::RecalculateWallMetadata(g_app.document.zones[index]);
            UpdateWallTextureBandCountFromLength(g_app.document.zones[index]);
        }

        if (ordered.size() > 2 && signedArea > 0.0)
        {
            std::reverse(ordered.begin(), ordered.end());
            for (int index : ordered)
            {
                ReverseWallDirectionPreservingTexture(g_app.document.zones[index]);
                mapfmt::RecalculateWallMetadata(g_app.document.zones[index]);
                UpdateWallTextureBandCountFromLength(g_app.document.zones[index]);
            }
        }

        return ordered;
    }

    double CalculateOrderedWallSignedArea(const std::vector<int>& ordered)
    {
        double signedArea = 0.0;
        for (int index : ordered)
        {
            if (index < 0 || index >= static_cast<int>(g_app.document.zones.size())) continue;
            const auto& zone = g_app.document.zones[index];
            signedArea += (static_cast<double>(zone.x1) * static_cast<double>(zone.z2)) -
                          (static_cast<double>(zone.x2) * static_cast<double>(zone.z1));
        }
        return signedArea;
    }

    std::vector<int> OrderAndOrientMoveGroupFronts(const std::vector<int>& fronts)
    {
        std::vector<int> ordered = OrderAndOrientRotateFronts(fronts);
        const double signedArea = CalculateOrderedWallSignedArea(ordered);

        // Zombie Massacre map1b stores moving wall blocks as one positive-area
        // polygon run (Z47..Z50 and Z55..Z58).  The normal rotate-link helper
        // intentionally flips that orientation for hinged rotations; the move
        // variant must keep the map1b orientation or Gloom treats the block like
        // a folding/rotating polygon.
        if (ordered.size() > 2 && signedArea < 0.0)
        {
            std::reverse(ordered.begin(), ordered.end());
            for (int index : ordered)
            {
                ReverseWallDirectionPreservingTexture(g_app.document.zones[index]);
                mapfmt::RecalculateWallMetadata(g_app.document.zones[index]);
                UpdateWallTextureBandCountFromLength(g_app.document.zones[index]);
            }
        }

        return ordered;
    }

    void MakeMoveWallGroupGuideAiNeutral(mapfmt::Zone& guide)
    {
        // Editor-created move-wallblock target guides are addressed by the
        // RotatePoly command through their endpoint coordinates only. Keep the
        // line endpoints intact, but clear wall-like collision/AI metadata so
        // ZGloom cannot treat the invisible guide as a real nearby obstacle.
        guide.a = 0;
        guide.b = 0;
        guide.na = 0;
        guide.nb = 0;
        guide.ln = 0;
        guide.sc = 0;
        guide.textures.fill(0);
        guide.ev = 0;
    }

    POINT CalculateMoveWallGroupGuideOffset(const mapfmt::Zone& selectedWall)
    {
        mapfmt::Zone basis = selectedWall;
        mapfmt::RecalculateWallMetadata(basis);

        constexpr int kMoveWallGroupGuideDistance = 384;
        constexpr double kFixedPointOne = 32766.0;
        int dx = static_cast<int>(std::lround((static_cast<double>(basis.a) / kFixedPointOne) * kMoveWallGroupGuideDistance));
        int dz = static_cast<int>(std::lround((static_cast<double>(basis.b) / kFixedPointOne) * kMoveWallGroupGuideDistance));

        if (std::abs(dx) < 1 && std::abs(dz) < 1)
        {
            dx = 0;
            dz = kMoveWallGroupGuideDistance;
        }

        const auto snapToFineGrid = [](int value)
        {
            if (value == 0) return 0;
            const int sign = value < 0 ? -1 : 1;
            const int absValue = std::abs(value);
            return sign * MaxValue(kGridSnapStep, static_cast<int>(std::lround(static_cast<double>(absValue) / static_cast<double>(kGridSnapStep))) * kGridSnapStep);
        };
        dx = snapToFineGrid(dx);
        dz = snapToFineGrid(dz);
        return POINT{ static_cast<LONG>(dx), static_cast<LONG>(dz) };
    }

    mapfmt::Zone MakeMoveWallGroupGuideZone(const mapfmt::Zone& active, POINT offset)
    {
        mapfmt::Zone guide = active;
        guide.x1 = static_cast<int16_t>(ClampValue(static_cast<int>(guide.x1) + static_cast<int>(offset.x), -32768, 32767));
        guide.z1 = static_cast<int16_t>(ClampValue(static_cast<int>(guide.z1) + static_cast<int>(offset.y), -32768, 32767));
        guide.x2 = static_cast<int16_t>(ClampValue(static_cast<int>(guide.x2) + static_cast<int>(offset.x), -32768, 32767));
        guide.z2 = static_cast<int16_t>(ClampValue(static_cast<int>(guide.z2) + static_cast<int>(offset.y), -32768, 32767));
        mapfmt::RecalculateWallMetadata(guide);
        UpdateWallTextureBandCountFromLength(guide);

        MakeMoveWallGroupGuideAiNeutral(guide);
        return guide;
    }

    bool PrepareMoveWallGroupRunForEvent(int& targetZoneIndex, MoveWallGroupRunInfo* outInfo)
    {
        if (targetZoneIndex < 0 || targetZoneIndex >= static_cast<int>(g_app.document.zones.size()) ||
            !IsWallZone(g_app.document.zones[targetZoneIndex]))
        {
            return false;
        }

        const int originalTarget = GetCanonicalWallIndex(targetZoneIndex);
        if (originalTarget < 0 || originalTarget >= static_cast<int>(g_app.document.zones.size()))
        {
            return false;
        }

        const POINT guideOffset = CalculateMoveWallGroupGuideOffset(g_app.document.zones[originalTarget]);

        std::vector<int> fronts;
        CollectCanonicalRotateWallComponent(originalTarget, fronts);
        if (fronts.empty())
        {
            return false;
        }

        std::sort(fronts.begin(), fronts.end());
        fronts.erase(std::unique(fronts.begin(), fronts.end()), fronts.end());

        // Remember editor-created reverse faces before ordering/orienting the
        // active polygon.  The ordering step may reverse individual wall lines,
        // after which an exact reverse-pair lookup would no longer find the old
        // visual backface.  Those slots are intentionally reused as map1b-style
        // guide/destination zones.
        std::vector<std::pair<int, int>> originalBackfacePairs;
        originalBackfacePairs.reserve(fronts.size());
        for (int front : fronts)
        {
            const int backface = FindReverseWallPairIndex(front);
            if (backface >= 0 && backface < static_cast<int>(g_app.document.zones.size()))
            {
                originalBackfacePairs.emplace_back(front, backface);
            }
        }

        fronts = OrderAndOrientMoveGroupFronts(fronts);
        if (fronts.empty())
        {
            return false;
        }

        std::vector<int> backfaceSlots;
        backfaceSlots.reserve(fronts.size());
        for (int front : fronts)
        {
            if (front >= 0 && front < static_cast<int>(g_app.document.zones.size()))
            {
                mapfmt::RecalculateWallMetadata(g_app.document.zones[front]);
                UpdateWallTextureBandCountFromLength(g_app.document.zones[front]);
            }

            int backface = -1;
            for (const auto& pair : originalBackfacePairs)
            {
                if (pair.first == front)
                {
                    backface = pair.second;
                    break;
                }
            }
            if (backface < 0)
            {
                backface = FindReverseWallPairIndex(front);
            }
            if (backface < 0 || backface >= static_cast<int>(g_app.document.zones.size()))
            {
                backface = -1;
            }
            backfaceSlots.push_back(backface);
        }

        std::vector<int> members;
        members.reserve(fronts.size() + backfaceSlots.size());
        for (int front : fronts) AddUniqueIndex(members, front);
        for (int slot : backfaceSlots) AddUniqueIndex(members, slot);

        const int oldCount = static_cast<int>(g_app.document.zones.size());
        std::vector<uint8_t> isMember(static_cast<size_t>(oldCount), 0);
        for (int index : members)
        {
            if (index >= 0 && index < oldCount)
            {
                isMember[static_cast<size_t>(index)] = 1;
            }
        }

        std::vector<mapfmt::Zone> reordered;
        reordered.reserve(g_app.document.zones.size() + fronts.size());
        std::vector<int> oldToNew(static_cast<size_t>(oldCount), -1);
        for (int i = 0; i < oldCount; ++i)
        {
            if (isMember[static_cast<size_t>(i)]) continue;
            oldToNew[static_cast<size_t>(i)] = static_cast<int>(reordered.size());
            reordered.push_back(g_app.document.zones[i]);
        }

        const int activeFirst = static_cast<int>(reordered.size());
        for (int oldIndex : fronts)
        {
            oldToNew[static_cast<size_t>(oldIndex)] = static_cast<int>(reordered.size());
            reordered.push_back(g_app.document.zones[oldIndex]);
        }

        const int guideFirst = static_cast<int>(reordered.size());
        for (size_t i = 0; i < fronts.size(); ++i)
        {
            const int frontOldIndex = fronts[i];
            const int guideNewIndex = static_cast<int>(reordered.size());
            if (i < backfaceSlots.size() && backfaceSlots[i] >= 0 && backfaceSlots[i] < oldCount)
            {
                oldToNew[static_cast<size_t>(backfaceSlots[i])] = guideNewIndex;
            }
            reordered.push_back(MakeMoveWallGroupGuideZone(g_app.document.zones[frontOldIndex], guideOffset));
        }

        g_app.document.zones = std::move(reordered);
        RemapEventZoneReferencesAfterReorder(oldToNew);
        if (g_app.selectedZone >= 0)
        {
            g_app.selectedZone = RemapZoneIndex(oldToNew, g_app.selectedZone);
        }
        if (g_app.linkEventTriggerZone >= 0)
        {
            g_app.linkEventTriggerZone = RemapZoneIndex(oldToNew, g_app.linkEventTriggerZone);
        }
        targetZoneIndex = RemapZoneIndex(oldToNew, originalTarget);

        if (outInfo)
        {
            outInfo->activeFirst = activeFirst;
            outInfo->activeCount = static_cast<int>(fronts.size());
            outInfo->guideFirst = guideFirst;
            outInfo->guideCount = static_cast<int>(fronts.size());
        }

        return targetZoneIndex >= activeFirst && targetZoneIndex < activeFirst + static_cast<int>(fronts.size());
    }

    int FindOriginalBackfaceForFront(const std::vector<std::pair<int, int>>& originalBackfacePairs, int frontIndex)
    {
        for (const auto& pair : originalBackfacePairs)
        {
            if (pair.first == frontIndex)
            {
                return pair.second;
            }
        }
        return -1;
    }

    mapfmt::Zone MakeMoveWallGroupGuideZoneFromTarget(const mapfmt::Zone& target)
    {
        mapfmt::Zone guide = target;
        mapfmt::RecalculateWallMetadata(guide);
        UpdateWallTextureBandCountFromLength(guide);
        MakeMoveWallGroupGuideAiNeutral(guide);
        return guide;
    }

    bool WallRunsShareAnyZone(const std::vector<int>& a, const std::vector<int>& b)
    {
        for (int ai : a)
        {
            if (std::find(b.begin(), b.end(), ai) != b.end())
            {
                return true;
            }
        }
        return false;
    }

    bool PrepareMoveWallGroupRunForEvent(int& sourceZoneIndex, int guideZoneIndex, MoveWallGroupRunInfo* outInfo)
    {
        if (sourceZoneIndex < 0 || sourceZoneIndex >= static_cast<int>(g_app.document.zones.size()) ||
            guideZoneIndex < 0 || guideZoneIndex >= static_cast<int>(g_app.document.zones.size()) ||
            !IsWallZone(g_app.document.zones[sourceZoneIndex]) || !IsWallZone(g_app.document.zones[guideZoneIndex]))
        {
            return false;
        }

        const int originalSource = GetCanonicalWallIndex(sourceZoneIndex);
        const int originalGuide = GetCanonicalWallIndex(guideZoneIndex);
        if (originalSource < 0 || originalGuide < 0 ||
            originalSource >= static_cast<int>(g_app.document.zones.size()) ||
            originalGuide >= static_cast<int>(g_app.document.zones.size()) ||
            originalSource == originalGuide)
        {
            return false;
        }

        std::vector<int> sourceFronts;
        std::vector<int> guideFronts;
        CollectCanonicalRotateWallComponent(originalSource, sourceFronts);
        CollectCanonicalRotateWallComponent(originalGuide, guideFronts);
        if (sourceFronts.empty() || guideFronts.empty())
        {
            return false;
        }

        std::sort(sourceFronts.begin(), sourceFronts.end());
        sourceFronts.erase(std::unique(sourceFronts.begin(), sourceFronts.end()), sourceFronts.end());
        std::sort(guideFronts.begin(), guideFronts.end());
        guideFronts.erase(std::unique(guideFronts.begin(), guideFronts.end()), guideFronts.end());
        if (sourceFronts.size() != guideFronts.size() || WallRunsShareAnyZone(sourceFronts, guideFronts))
        {
            return false;
        }

        std::vector<std::pair<int, int>> sourceBackfacePairs;
        sourceBackfacePairs.reserve(sourceFronts.size());
        for (int front : sourceFronts)
        {
            const int backface = FindReverseWallPairIndex(front);
            if (backface >= 0 && backface < static_cast<int>(g_app.document.zones.size()))
            {
                sourceBackfacePairs.emplace_back(front, backface);
            }
        }

        std::vector<std::pair<int, int>> guideBackfacePairs;
        guideBackfacePairs.reserve(guideFronts.size());
        for (int front : guideFronts)
        {
            const int backface = FindReverseWallPairIndex(front);
            if (backface >= 0 && backface < static_cast<int>(g_app.document.zones.size()))
            {
                guideBackfacePairs.emplace_back(front, backface);
            }
        }

        sourceFronts = OrderAndOrientMoveGroupFronts(sourceFronts);
        guideFronts = OrderAndOrientMoveGroupFronts(guideFronts);
        if (sourceFronts.empty() || guideFronts.empty() || sourceFronts.size() != guideFronts.size())
        {
            return false;
        }

        std::vector<int> sourceBackfaces;
        sourceBackfaces.reserve(sourceFronts.size());
        for (int front : sourceFronts)
        {
            sourceBackfaces.push_back(FindOriginalBackfaceForFront(sourceBackfacePairs, front));
        }
        std::vector<int> guideBackfaces;
        guideBackfaces.reserve(guideFronts.size());
        for (int front : guideFronts)
        {
            guideBackfaces.push_back(FindOriginalBackfaceForFront(guideBackfacePairs, front));
        }

        std::vector<int> members;
        members.reserve(sourceFronts.size() + guideFronts.size() + sourceBackfaces.size() + guideBackfaces.size());
        for (int front : sourceFronts) AddUniqueIndex(members, front);
        for (int front : guideFronts) AddUniqueIndex(members, front);
        for (int backface : sourceBackfaces) AddUniqueIndex(members, backface);
        for (int backface : guideBackfaces) AddUniqueIndex(members, backface);

        const int oldCount = static_cast<int>(g_app.document.zones.size());
        std::vector<uint8_t> isMember(static_cast<size_t>(oldCount), 0);
        for (int index : members)
        {
            if (index >= 0 && index < oldCount)
            {
                isMember[static_cast<size_t>(index)] = 1;
            }
        }

        std::vector<mapfmt::Zone> reordered;
        reordered.reserve(g_app.document.zones.size());
        std::vector<int> oldToNew(static_cast<size_t>(oldCount), -1);
        for (int i = 0; i < oldCount; ++i)
        {
            if (isMember[static_cast<size_t>(i)]) continue;
            oldToNew[static_cast<size_t>(i)] = static_cast<int>(reordered.size());
            reordered.push_back(g_app.document.zones[i]);
        }

        const int activeFirst = static_cast<int>(reordered.size());
        for (size_t i = 0; i < sourceFronts.size(); ++i)
        {
            const int oldIndex = sourceFronts[i];
            const int newIndex = static_cast<int>(reordered.size());
            oldToNew[static_cast<size_t>(oldIndex)] = newIndex;
            if (i < sourceBackfaces.size() && sourceBackfaces[i] >= 0 && sourceBackfaces[i] < oldCount)
            {
                oldToNew[static_cast<size_t>(sourceBackfaces[i])] = newIndex;
            }
            mapfmt::RecalculateWallMetadata(g_app.document.zones[oldIndex]);
            UpdateWallTextureBandCountFromLength(g_app.document.zones[oldIndex]);
            reordered.push_back(g_app.document.zones[oldIndex]);
        }

        const int guideFirst = static_cast<int>(reordered.size());
        for (size_t i = 0; i < guideFronts.size(); ++i)
        {
            const int oldIndex = guideFronts[i];
            const int newIndex = static_cast<int>(reordered.size());
            oldToNew[static_cast<size_t>(oldIndex)] = newIndex;
            if (i < guideBackfaces.size() && guideBackfaces[i] >= 0 && guideBackfaces[i] < oldCount)
            {
                oldToNew[static_cast<size_t>(guideBackfaces[i])] = newIndex;
            }
            reordered.push_back(MakeMoveWallGroupGuideZoneFromTarget(g_app.document.zones[oldIndex]));
        }

        g_app.document.zones = std::move(reordered);
        RemapEventZoneReferencesAfterReorder(oldToNew);
        if (g_app.selectedZone >= 0)
        {
            g_app.selectedZone = RemapZoneIndex(oldToNew, g_app.selectedZone);
        }
        if (g_app.linkEventTriggerZone >= 0)
        {
            g_app.linkEventTriggerZone = RemapZoneIndex(oldToNew, g_app.linkEventTriggerZone);
        }
        if (g_app.pendingMoveWallGroupSourceZone >= 0)
        {
            g_app.pendingMoveWallGroupSourceZone = RemapZoneIndex(oldToNew, g_app.pendingMoveWallGroupSourceZone);
        }
        sourceZoneIndex = RemapZoneIndex(oldToNew, originalSource);

        if (outInfo)
        {
            outInfo->activeFirst = activeFirst;
            outInfo->activeCount = static_cast<int>(sourceFronts.size());
            outInfo->guideFirst = guideFirst;
            outInfo->guideCount = static_cast<int>(guideFronts.size());
        }

        return sourceZoneIndex >= activeFirst && sourceZoneIndex < activeFirst + static_cast<int>(sourceFronts.size());
    }

    void CollectCanonicalRotateWallComponent(int targetZoneIndex, std::vector<int>& outFronts)
    {
        outFronts.clear();
        int target = targetZoneIndex;
        if (target >= 0 && target < static_cast<int>(g_app.document.zones.size()) && IsWallZone(g_app.document.zones[target]))
        {
            target = GetCanonicalWallIndex(target);
        }
        if (target < 0 || target >= static_cast<int>(g_app.document.zones.size()) ||
            !IsWallZone(g_app.document.zones[target]) || IsVisualBackfaceWallIndex(target))
        {
            return;
        }

        const int zoneTotal = static_cast<int>(g_app.document.zones.size());
        const int pendingGuideFirst = g_app.pendingMoveWallGroupGuideFirst;
        const int pendingGuideLast = pendingGuideFirst + g_app.pendingMoveWallGroupGuideCount - 1;
        const bool hasPendingGuideRun = pendingGuideFirst >= 0 && g_app.pendingMoveWallGroupGuideCount > 0;
        const bool targetIsPendingGuide = hasPendingGuideRun && target >= pendingGuideFirst && target <= pendingGuideLast;
        std::vector<uint8_t> used(static_cast<size_t>(zoneTotal), 0);
        outFronts.push_back(target);
        used[static_cast<size_t>(target)] = 1;

        for (size_t cursor = 0; cursor < outFronts.size(); ++cursor)
        {
            const int current = outFronts[cursor];
            const auto& curZone = g_app.document.zones[current];
            for (int i = 0; i < zoneTotal; ++i)
            {
                if (i < 0 || i >= static_cast<int>(used.size())) continue;
                if (used[static_cast<size_t>(i)] || !IsCanonicalRotateWallIndex(i)) continue;
                if (hasPendingGuideRun)
                {
                    const bool candidateIsPendingGuide = i >= pendingGuideFirst && i <= pendingGuideLast;
                    if (targetIsPendingGuide != candidateIsPendingGuide) continue;
                }
                if (PointsTouchForRotateRun(curZone, g_app.document.zones[i]))
                {
                    used[static_cast<size_t>(i)] = 1;
                    outFronts.push_back(i);
                }
            }
        }
    }

    bool PrepareRotateWallRunForEvent(int& targetZoneIndex, RotateWallRunInfo* outInfo)
    {
        if (targetZoneIndex < 0 || targetZoneIndex >= static_cast<int>(g_app.document.zones.size()) ||
            !IsWallZone(g_app.document.zones[targetZoneIndex]))
        {
            return false;
        }

        targetZoneIndex = GetCanonicalWallIndex(targetZoneIndex);

        std::vector<int> fronts;
        CollectCanonicalRotateWallComponent(targetZoneIndex, fronts);
        if (fronts.empty())
        {
            return false;
        }

        // A RotatePoly command can only address contiguous zone ranges. Build a
        // deterministic front polygon/path first, then place the visual reverse run
        // behind it in reverse order. Front+back must not be one single RotatePoly
        // polygon, otherwise Gloom can cull some sides while collision still exists.
        std::sort(fronts.begin(), fronts.end());
        fronts.erase(std::unique(fronts.begin(), fronts.end()), fronts.end());
        fronts = OrderAndOrientRotateFronts(fronts);

        std::vector<int> members;
        members.reserve(fronts.size() * 2);
        for (int front : fronts)
        {
            if (front >= 0 && front < static_cast<int>(g_app.document.zones.size()) && IsWallZone(g_app.document.zones[front]))
            {
                mapfmt::RecalculateWallMetadata(g_app.document.zones[front]);
                UpdateWallTextureBandCountFromLength(g_app.document.zones[front]);
                EnsureBackfaceForWallAtIndex(front);
                members.push_back(front);
            }
        }

        std::vector<int> backfaces;
        backfaces.reserve(fronts.size());
        for (auto it = fronts.rbegin(); it != fronts.rend(); ++it)
        {
            const int front = *it;
            const int backface = FindReverseWallPairIndex(front);
            if (backface >= 0 && backface < static_cast<int>(g_app.document.zones.size()))
            {
                SyncBackfaceWallFromFront(front, backface);
                backfaces.push_back(backface);
            }
        }
        members.insert(members.end(), backfaces.begin(), backfaces.end());
        members.erase(std::remove_if(members.begin(), members.end(), [&](int index)
        {
            return index < 0 || index >= static_cast<int>(g_app.document.zones.size());
        }), members.end());
        members.erase(std::unique(members.begin(), members.end()), members.end());
        if (members.empty())
        {
            return false;
        }

        const int oldCount = static_cast<int>(g_app.document.zones.size());
        std::vector<uint8_t> isMember(static_cast<size_t>(oldCount), 0);
        for (int index : members)
        {
            isMember[static_cast<size_t>(index)] = 1;
        }

        std::vector<mapfmt::Zone> reordered;
        reordered.reserve(g_app.document.zones.size());
        std::vector<int> oldToNew(static_cast<size_t>(oldCount), -1);
        for (int i = 0; i < oldCount; ++i)
        {
            if (isMember[static_cast<size_t>(i)]) continue;
            oldToNew[static_cast<size_t>(i)] = static_cast<int>(reordered.size());
            reordered.push_back(g_app.document.zones[i]);
        }

        const int newFirst = static_cast<int>(reordered.size());
        for (int oldIndex : members)
        {
            oldToNew[static_cast<size_t>(oldIndex)] = static_cast<int>(reordered.size());
            reordered.push_back(g_app.document.zones[oldIndex]);
        }

        g_app.document.zones = std::move(reordered);
        RemapEventZoneReferencesAfterReorder(oldToNew);
        if (g_app.selectedZone >= 0)
        {
            g_app.selectedZone = RemapZoneIndex(oldToNew, g_app.selectedZone);
        }
        if (g_app.linkEventTriggerZone >= 0)
        {
            g_app.linkEventTriggerZone = RemapZoneIndex(oldToNew, g_app.linkEventTriggerZone);
        }
        targetZoneIndex = RemapZoneIndex(oldToNew, targetZoneIndex);

        // Re-sync the newly packed block once more after the move. Fronts are in
        // path order. Backfaces are in reverse path order, therefore their own
        // RotatePoly command also receives a valid connected path.
        const int frontCount = static_cast<int>(fronts.size());
        const int backCount = static_cast<int>(backfaces.size());
        for (int i = 0; i < frontCount; ++i)
        {
            const int frontIndex = newFirst + i;
            const int backIndex = newFirst + frontCount + (frontCount - 1 - i);
            if (frontIndex >= 0 && backIndex >= 0 &&
                frontIndex < static_cast<int>(g_app.document.zones.size()) &&
                backIndex < static_cast<int>(g_app.document.zones.size()))
            {
                mapfmt::RecalculateWallMetadata(g_app.document.zones[frontIndex]);
                UpdateWallTextureBandCountFromLength(g_app.document.zones[frontIndex]);
                SyncBackfaceWallFromFront(frontIndex, backIndex);
            }
        }

        if (outInfo)
        {
            outInfo->frontFirst = newFirst;
            outInfo->frontCount = frontCount;
            outInfo->backFirst = backCount > 0 ? newFirst + frontCount : -1;
            outInfo->backCount = backCount;
        }

        return targetZoneIndex >= newFirst && targetZoneIndex < newFirst + frontCount;
    }

    bool PointsTouchForRotateRun(const mapfmt::Zone& a, const mapfmt::Zone& b)
    {
        constexpr int tolerance = 4;
        const POINT a1{ a.x1, a.z1 };
        const POINT a2{ a.x2, a.z2 };
        const POINT b1{ b.x1, b.z1 };
        const POINT b2{ b.x2, b.z2 };
        auto close = [](POINT p, POINT q)
        {
            constexpr int tolerance = 4;
            return std::abs(static_cast<int>(p.x) - static_cast<int>(q.x)) <= tolerance &&
                   std::abs(static_cast<int>(p.y) - static_cast<int>(q.y)) <= tolerance;
        };
        return close(a1, b1) || close(a1, b2) || close(a2, b1) || close(a2, b2);
    }

    bool IsCanonicalRotateWallIndex(int zoneIndex)
    {
        if (zoneIndex < 0 || zoneIndex >= static_cast<int>(g_app.document.zones.size())) return false;
        if (!IsWallZone(g_app.document.zones[zoneIndex])) return false;
        if (IsVisualBackfaceWallIndex(zoneIndex)) return false;
        return GetCanonicalWallIndex(zoneIndex) == zoneIndex;
    }

    void DetectConsecutiveRotatingRun(int targetZoneIndex, int& firstZoneIndex, int& zoneCount)
    {
        int target = targetZoneIndex;
        if (target >= 0 && target < static_cast<int>(g_app.document.zones.size()) && IsWallZone(g_app.document.zones[target]))
        {
            target = GetCanonicalWallIndex(target);
        }
        firstZoneIndex = target;
        zoneCount = 1;
        if (!IsCanonicalRotateWallIndex(target))
        {
            return;
        }

        // Rotating objects in Gloom are stored as one contiguous zone range.
        // Editor-created two-sided walls are often front/back interleaved, so the
        // old "previous endpoint -> next startpoint" scan only selected one wall.
        // Build a connected component of canonical front walls and then emit the
        // smallest zone range that contains those fronts and their visual backfaces.
        const int zoneTotal = static_cast<int>(g_app.document.zones.size());
        std::vector<int> group;
        std::vector<uint8_t> used(static_cast<size_t>(zoneTotal), 0);
        group.push_back(target);
        used[static_cast<size_t>(target)] = 1;

        for (size_t cursor = 0; cursor < group.size(); ++cursor)
        {
            const int current = group[cursor];
            const auto& curZone = g_app.document.zones[current];
            for (int i = 0; i < zoneTotal; ++i)
            {
                if (used[static_cast<size_t>(i)] || !IsCanonicalRotateWallIndex(i)) continue;
                if (PointsTouchForRotateRun(curZone, g_app.document.zones[i]))
                {
                    used[static_cast<size_t>(i)] = 1;
                    group.push_back(i);
                }
            }
        }

        int first = target;
        int last = target;
        for (int index : group)
        {
            first = MinValue(first, index);
            last = MaxValue(last, index);
            const int backface = FindReverseWallPairIndex(index);
            if (backface >= 0)
            {
                first = MinValue(first, backface);
                last = MaxValue(last, backface);
            }
        }

        firstZoneIndex = first;
        zoneCount = MaxValue(1, last - first + 1);
    }

    bool RotateCommandIntersectsRun(const mapfmt::EventCommand& command, const RotateWallRunInfo& run)
    {
        if (command.type != mapfmt::CommandType::RotatePoly)
        {
            return false;
        }
        const int first = static_cast<int>(command.params[0]);
        const int count = MaxValue(1, static_cast<int>(command.params[1]));
        const int last = first + count - 1;
        auto intersects = [&](int runFirst, int runCount)
        {
            if (runFirst < 0 || runCount <= 0) return false;
            const int runLast = runFirst + runCount - 1;
            return !(last < runFirst || first > runLast);
        };
        return intersects(run.frontFirst, run.frontCount) || intersects(run.backFirst, run.backCount);
    }

    void AddRotateCommand(std::vector<mapfmt::EventCommand>& commands, int first, int count, int speed, int flags)
    {
        if (first < 0 || count <= 0)
        {
            return;
        }
        mapfmt::EventCommand command;
        command.type = mapfmt::CommandType::RotatePoly;
        command.params[0] = static_cast<int16_t>(first);
        command.params[1] = static_cast<int16_t>(count);
        command.params[2] = static_cast<int16_t>(speed);
        command.params[3] = static_cast<int16_t>(flags);
        commands.push_back(command);
    }

    bool AddRotatePolyLinkToEvent(int eventIndex, int& targetZoneIndex, bool clockwise)
    {
        if (EventScriptHasAdvancedRaw(eventIndex)) return false;
        if (eventIndex < 0 || eventIndex >= static_cast<int>(g_app.document.events.size())) return false;
        int rotateTargetZoneIndex = targetZoneIndex;
        RotateWallRunInfo run;
        if (!PrepareRotateWallRunForEvent(rotateTargetZoneIndex, &run)) return false;
        targetZoneIndex = rotateTargetZoneIndex;
        if (run.frontFirst < 0 || run.frontCount <= 0) return false;

        auto& commands = g_app.document.events[eventIndex].commands;
        const int speed = clockwise ? 3 : -3;
        const int flags = 0;

        // Replace old combined front+back RotatePoly commands for this object.
        // Gloom renders each RotatePoly as one polygon/path; front faces followed by
        // reverse faces in the same range can render one-sided/transparent.
        commands.erase(std::remove_if(commands.begin(), commands.end(), [&](const mapfmt::EventCommand& command)
        {
            return RotateCommandIntersectsRun(command, run) ||
                   (command.type == mapfmt::CommandType::RotatePoly && EventCommandTargetsZone(command, targetZoneIndex));
        }), commands.end());

        AddRotateCommand(commands, run.frontFirst, run.frontCount, speed, flags);
        AddRotateCommand(commands, run.backFirst, run.backCount, speed, flags);
        return true;
    }

    bool FinishLinkEventToSwitchTexture(int targetZoneIndex)
    {
        if (g_app.insertMode != InsertMode::LinkEventToSwitchTexture) return false;
        if (g_app.linkEventTriggerZone < 0 || g_app.linkEventTriggerZone >= static_cast<int>(g_app.document.zones.size()))
        {
            ClearInsertMode();
            return true;
        }
        if (targetZoneIndex < 0 || targetZoneIndex >= static_cast<int>(g_app.document.zones.size())) return true;
        if (targetZoneIndex == g_app.linkEventTriggerZone) return true;

        const auto& target = g_app.document.zones[targetZoneIndex];
        if (!IsWallZone(target))
        {
            MessageBoxW(g_app.mainWindow, L"Choose the OFF switch/wall panel whose texture should change. The ON texture will be the next texture after its OFF frame/animation.", L"ZGloom Editor", MB_OK | MB_ICONINFORMATION);
            return true;
        }

        PushUndoSnapshot();
        const int eventIndex = EnsureTriggerHasEventSlot(g_app.linkEventTriggerZone);
        if (eventIndex < 0)
        {
            UndoLastChange();
            MessageBoxW(g_app.mainWindow, L"Could not assign an event slot to this trigger.", L"ZGloom Editor", MB_OK | MB_ICONERROR);
            return true;
        }

        AddSwitchTextureLinkToEvent(eventIndex, targetZoneIndex);
        ClearPendingSwitchTextureChoice();
        g_app.linkEventIndex = eventIndex;
        g_app.selectedZone = targetZoneIndex;
        g_app.insertMode = InsertMode::None;
        g_app.linkEventTriggerZone = -1;
        MarkDirty();
        RefreshZoneList();
        RefreshPreviewImage();
        UpdateModeButtons();
        RefreshStatus();
        InvalidateEditorViews();
        return true;
    }

    bool FinishLinkEventToEnemyObject(const MonsterSpawnSelection& targetSpawn)
    {
        if (g_app.insertMode != InsertMode::LinkEventToEnemyObject) return false;
        if (g_app.linkEventTriggerZone < 0 || g_app.linkEventTriggerZone >= static_cast<int>(g_app.document.zones.size()))
        {
            ClearInsertMode();
            return true;
        }

        const mapfmt::EventCommand* sourceCommand = GetMonsterSpawnCommand(targetSpawn);
        if (!sourceCommand)
        {
            MessageBoxW(g_app.mainWindow, L"Choose an existing enemy/object/ammo marker to trigger from this event.", L"ZGloom Editor", MB_OK | MB_ICONINFORMATION);
            return true;
        }

        PushUndoSnapshot();
        const int eventIndex = EnsureTriggerHasEventSlot(g_app.linkEventTriggerZone);
        if (eventIndex < 0)
        {
            UndoLastChange();
            MessageBoxW(g_app.mainWindow, L"Could not assign an event slot to this trigger.", L"ZGloom Editor", MB_OK | MB_ICONERROR);
            return true;
        }

        if (targetSpawn.eventIndex == eventIndex)
        {
            g_app.selectedMonsterSpawn = targetSpawn;
            g_app.selectedMonsterSpawn.markerIndex = ComputeMonsterSpawnMarkerIndex(targetSpawn.eventIndex, targetSpawn.commandIndex);
        }
        else
        {
            mapfmt::EventCommand movedCommand = *sourceCommand;
            auto& sourceCommands = g_app.document.events[targetSpawn.eventIndex].commands;
            if (targetSpawn.commandIndex < 0 || targetSpawn.commandIndex >= static_cast<int>(sourceCommands.size()))
            {
                UndoLastChange();
                MessageBoxW(g_app.mainWindow, L"The selected enemy/object command is no longer valid.", L"ZGloom Editor", MB_OK | MB_ICONERROR);
                return true;
            }

            sourceCommands.erase(sourceCommands.begin() + targetSpawn.commandIndex);
            auto& destinationCommands = g_app.document.events[eventIndex].commands;
            destinationCommands.push_back(movedCommand);

            g_app.selectedMonsterSpawn.eventIndex = eventIndex;
            g_app.selectedMonsterSpawn.commandIndex = static_cast<int>(destinationCommands.size()) - 1;
            g_app.selectedMonsterSpawn.markerIndex = ComputeMonsterSpawnMarkerIndex(g_app.selectedMonsterSpawn.eventIndex, g_app.selectedMonsterSpawn.commandIndex);
        }

        ClearSelectedTeleportTarget();
        g_app.selectedZone = -1;
        g_app.linkEventIndex = eventIndex;
        g_app.insertMode = InsertMode::None;
        g_app.linkEventTriggerZone = -1;
        MarkDirty();
        RefreshZoneList();
        RefreshPreviewImage();
        UpdateModeButtons();
        RefreshStatus();
        InvalidateEditorViews();
        return true;
    }

    bool FinishDeleteLinkEventToEnemyObject(const MonsterSpawnSelection& targetSpawn)
    {
        if (g_app.insertMode != InsertMode::DeleteLinkEventToEnemyObject) return false;
        if (g_app.linkEventTriggerZone < 0 || g_app.linkEventTriggerZone >= static_cast<int>(g_app.document.zones.size()))
        {
            ClearInsertMode();
            return true;
        }

        const auto& triggerZone = g_app.document.zones[g_app.linkEventTriggerZone];
        const int eventIndex = EventSlotFromZoneEventValue(triggerZone.ev);
        if (eventIndex < 0 || eventIndex >= static_cast<int>(g_app.document.events.size()))
        {
            MessageBoxW(g_app.mainWindow, L"This trigger currently has no linked enemy/object event to remove.", L"ZGloom Editor", MB_OK | MB_ICONINFORMATION);
            return true;
        }

        if (!targetSpawn.IsSet() || targetSpawn.eventIndex != eventIndex)
        {
            MessageBoxW(g_app.mainWindow, L"Choose an enemy/object/ammo marker that is currently linked to this trigger event.", L"ZGloom Editor", MB_OK | MB_ICONINFORMATION);
            return true;
        }

        auto& commands = g_app.document.events[eventIndex].commands;
        if (targetSpawn.commandIndex < 0 || targetSpawn.commandIndex >= static_cast<int>(commands.size()) ||
            commands[targetSpawn.commandIndex].type != mapfmt::CommandType::AddMonster)
        {
            MessageBoxW(g_app.mainWindow, L"The selected enemy/object command is no longer valid.", L"ZGloom Editor", MB_OK | MB_ICONERROR);
            return true;
        }

        PushUndoSnapshot();
        if (!MoveMonsterSpawnToInitialEvent(targetSpawn))
        {
            UndoLastChange();
            MessageBoxW(g_app.mainWindow, L"The selected enemy/object command could not be unlinked.", L"ZGloom Editor", MB_OK | MB_ICONERROR);
            return true;
        }
        ClearSelectedTeleportTarget();
        g_app.selectedZone = -1;
        g_app.linkEventIndex = eventIndex;
        g_app.insertMode = InsertMode::None;
        g_app.linkEventTriggerZone = -1;
        MarkDirty();
        RefreshZoneList();
        RefreshPreviewImage();
        UpdateModeButtons();
        RefreshStatus();
        InvalidateEditorViews();
        return true;
    }

    bool FinishLinkEventToRotate(int targetZoneIndex)
    {
        if (g_app.insertMode != InsertMode::LinkEventToRotateClockwise &&
            g_app.insertMode != InsertMode::LinkEventToRotateCounterClockwise)
        {
            return false;
        }
        if (g_app.linkEventTriggerZone < 0 || g_app.linkEventTriggerZone >= static_cast<int>(g_app.document.zones.size()))
        {
            ClearInsertMode();
            return true;
        }
        if (targetZoneIndex < 0 || targetZoneIndex >= static_cast<int>(g_app.document.zones.size())) return true;
        if (!IsWallZone(g_app.document.zones[targetZoneIndex]))
        {
            MessageBoxW(g_app.mainWindow, L"Choose a wall/poly zone that should rotate.", L"ZGloom Editor", MB_OK | MB_ICONINFORMATION);
            return true;
        }

        PushUndoSnapshot();
        const int eventIndex = EnsureTriggerHasEventSlot(g_app.linkEventTriggerZone);
        if (eventIndex < 0)
        {
            UndoLastChange();
            MessageBoxW(g_app.mainWindow, L"Could not assign an event slot to this trigger.", L"ZGloom Editor", MB_OK | MB_ICONERROR);
            return true;
        }

        const bool clockwise = (g_app.insertMode == InsertMode::LinkEventToRotateClockwise);
        int rotateTargetZoneIndex = targetZoneIndex;
        AddRotatePolyLinkToEvent(eventIndex, rotateTargetZoneIndex, clockwise);
        g_app.linkEventIndex = eventIndex;
        g_app.selectedZone = GetCanonicalWallIndex(rotateTargetZoneIndex);
        g_app.insertMode = InsertMode::None;
        g_app.linkEventTriggerZone = -1;
        MarkDirty();
        RefreshZoneList();
        RefreshPreviewImage();
        UpdateModeButtons();
        RefreshStatus();
        InvalidateEditorViews();
        return true;
    }

    bool FinishLinkEventToMoveWallGroup(int targetZoneIndex)
    {
        if (g_app.insertMode != InsertMode::LinkEventToMoveWallGroup) return false;
        if (g_app.linkEventTriggerZone < 0 || g_app.linkEventTriggerZone >= static_cast<int>(g_app.document.zones.size()))
        {
            ClearInsertMode();
            return true;
        }
        if (targetZoneIndex < 0 || targetZoneIndex >= static_cast<int>(g_app.document.zones.size())) return true;
        if (!IsWallZone(g_app.document.zones[targetZoneIndex]))
        {
            MessageBoxW(g_app.mainWindow,
                g_app.pendingMoveWallGroupSourceZone < 0
                    ? L"First click one segment of the wall block that should move."
                    : L"Now click one segment of the duplicate wall block at the target position.",
                L"ZGloom Editor", MB_OK | MB_ICONINFORMATION);
            return true;
        }

        const int canonicalTarget = GetCanonicalWallIndex(targetZoneIndex);
        if (g_app.pendingMoveWallGroupSourceZone < 0)
        {
            std::vector<int> sourceFronts;
            CollectCanonicalRotateWallComponent(canonicalTarget, sourceFronts);
            if (sourceFronts.empty())
            {
                MessageBoxW(g_app.mainWindow, L"Could not detect a connected source wall block.", L"ZGloom Editor", MB_OK | MB_ICONERROR);
                return true;
            }

            g_app.pendingMoveWallGroupSourceZone = canonicalTarget;
            g_app.selectedZone = canonicalTarget;
            RefreshPreviewImage();
            UpdateModeButtons();
            RefreshStatus();
            InvalidateEditorViews();
            return true;
        }

        std::vector<int> sourceFronts;
        std::vector<int> guideFronts;
        CollectCanonicalRotateWallComponent(g_app.pendingMoveWallGroupSourceZone, sourceFronts);
        CollectCanonicalRotateWallComponent(canonicalTarget, guideFronts);
        if (sourceFronts.empty() || guideFronts.empty() || WallRunsShareAnyZone(sourceFronts, guideFronts))
        {
            MessageBoxW(g_app.mainWindow,
                L"Click a separate duplicate of the same wall block at the destination position. The source block itself cannot be used as its own target guide.",
                L"ZGloom Editor", MB_OK | MB_ICONINFORMATION);
            return true;
        }
        if (sourceFronts.size() != guideFronts.size())
        {
            MessageBoxW(g_app.mainWindow,
                L"The target guide block must have the same number of wall segments as the moving source block.",
                L"ZGloom Editor", MB_OK | MB_ICONINFORMATION);
            return true;
        }

        PushUndoSnapshot();
        const int eventIndex = EnsureTriggerHasEventSlot(g_app.linkEventTriggerZone);
        if (eventIndex < 0)
        {
            UndoLastChange();
            MessageBoxW(g_app.mainWindow, L"Could not assign an event slot to this trigger.", L"ZGloom Editor", MB_OK | MB_ICONERROR);
            return true;
        }

        int moveTargetZoneIndex = g_app.pendingMoveWallGroupSourceZone;
        if (!AddMoveWallGroupLinkToEvent(eventIndex, g_app.pendingMoveWallGroupSourceZone, canonicalTarget, moveTargetZoneIndex))
        {
            UndoLastChange();
            MessageBoxW(g_app.mainWindow,
                L"Could not build a map1b-style move group. Draw/copy the same wall block at the final target position, then click source first and target second.",
                L"ZGloom Editor", MB_OK | MB_ICONERROR);
            return true;
        }

        g_app.linkEventIndex = eventIndex;
        g_app.selectedZone = GetCanonicalWallIndex(moveTargetZoneIndex);
        g_app.insertMode = InsertMode::None;
        g_app.linkEventTriggerZone = -1;
        ResetPendingMoveWallGroup();
        MarkDirty();
        RefreshZoneList();
        RefreshPreviewImage();
        UpdateModeButtons();
        RefreshStatus();
        InvalidateEditorViews();
        return true;
    }

    bool UpsertTeleportCommandToEvent(int eventIndex, POINT world, int rot)
    {
        if (EventScriptHasAdvancedRaw(eventIndex)) return false;
        if (eventIndex < 0 || eventIndex >= static_cast<int>(g_app.document.events.size())) return false;
        auto& commands = g_app.document.events[eventIndex].commands;
        for (auto& command : commands)
        {
            if (command.type == mapfmt::CommandType::Teleport)
            {
                command.params[0] = static_cast<int16_t>(world.x);
                command.params[1] = 0;
                command.params[2] = static_cast<int16_t>(world.y);
                command.params[3] = static_cast<int16_t>(rot & 255);
                return true;
            }
        }

        mapfmt::EventCommand command;
        command.type = mapfmt::CommandType::Teleport;
        command.params[0] = static_cast<int16_t>(world.x);
        command.params[1] = 0;
        command.params[2] = static_cast<int16_t>(world.y);
        command.params[3] = static_cast<int16_t>(rot & 255);
        commands.push_back(command);
        return true;
    }

    bool CommitTeleportTargetWithRotation(POINT targetWorld, int rot)
    {
        PushUndoSnapshot();
        const int eventIndex = EnsureTriggerHasEventSlot(g_app.linkEventTriggerZone);
        if (eventIndex < 0)
        {
            UndoLastChange();
            MessageBoxW(g_app.mainWindow, L"Could not assign an event slot to this trigger.", L"ZGloom Editor", MB_OK | MB_ICONERROR);
            return true;
        }

        UpsertTeleportCommandToEvent(eventIndex, targetWorld, rot);
        g_app.linkEventIndex = eventIndex;
        g_app.insertMode = InsertMode::None;
        g_app.linkEventTriggerZone = -1;
        ResetPendingTeleportTarget();
        g_app.walkPreviewX = static_cast<double>(targetWorld.x);
        g_app.walkPreviewZ = static_cast<double>(targetWorld.y);
        g_app.walkPreviewDir = WalkPreviewDirFromRotationUnits(rot);
        g_app.walkPreviewInitialized = true;
        MarkDirty();
        RefreshZoneList();
        RefreshPreviewImage();
        UpdateModeButtons();
        RefreshStatus();
        InvalidateEditorViews();
        return true;
    }

    bool CommitPendingTeleportTarget()
    {
        if (g_app.insertMode != InsertMode::SetTeleportTarget || !g_app.teleportTargetAwaitDirection) return false;
        return CommitTeleportTargetWithRotation(g_app.teleportTargetWorld, g_app.teleportTargetRotation);
    }

    bool RotatePendingTeleportTargetByDegrees(int degrees)
    {
        if (g_app.insertMode != InsertMode::SetTeleportTarget || !g_app.teleportTargetAwaitDirection) return false;
        const int absStep = MaxValue(1, static_cast<int>(std::lround((std::abs(degrees) * 256.0) / 360.0)));
        const int step = degrees < 0 ? -absStep : absStep;
        g_app.teleportTargetRotation = (g_app.teleportTargetRotation + step) & 255;
        RefreshStatus();
        InvalidateEditorViews();
        return true;
    }

    bool FinishSetTeleportTarget(POINT world)
    {
        if (g_app.insertMode != InsertMode::SetTeleportTarget) return false;
        if (g_app.linkEventTriggerZone < 0 || g_app.linkEventTriggerZone >= static_cast<int>(g_app.document.zones.size()))
        {
            ClearInsertMode();
            return true;
        }

        if (!g_app.teleportTargetAwaitDirection)
        {
            BeginTeleportTargetPlacement(world);
            return true;
        }

        return CommitPendingTeleportTarget();
    }

    std::wstring EventSlotRoleText(int eventIndex)
    {
        if (eventIndex == kInitialEventIndex)
        {
            return L"Start/init";
        }
        if (eventIndex == (kLevelEndEventValue - 1))
        {
            return L"Exit / retrigger";
        }
        if (eventIndex >= 18 && eventIndex <= 22)
        {
            return L"Retrigger";
        }
        if (eventIndex >= 1 && eventIndex <= 17)
        {
            return L"Once";
        }
        return L"Event";
    }

    std::wstring EventSlotComboLabel(int eventIndex)
    {
        std::wstringstream ss;
        ss << L"E" << (eventIndex + 1) << L" - " << EventSlotRoleText(eventIndex);
        return ss.str();
    }

    bool EventScriptHasAdvancedRaw(int eventIndex)
    {
        return eventIndex >= 0 &&
            eventIndex < static_cast<int>(g_app.document.events.size()) &&
            g_app.document.events[eventIndex].hasUnsupportedRaw;
    }

    bool WarnIfEventScriptAdvancedRaw(int eventIndex)
    {
        if (!EventScriptHasAdvancedRaw(eventIndex))
        {
            return false;
        }

        std::wstringstream message;
        message << L"Event " << (eventIndex + 1) << L" contains advanced/raw MAPED commands.\n\n"
                << L"This editor preserves the original byte block exactly, but high-level editing is disabled for this event slot so Morph/Lock/other original logic cannot be damaged.";
        MessageBoxW(g_app.mainWindow, message.str().c_str(), L"ZGloom Editor", MB_OK | MB_ICONINFORMATION);
        return true;
    }

    std::wstring FormatTextureIndexShort(int textureIndex)
    {
        textureIndex = ClampValue(textureIndex, 0, 159);
        std::wstringstream ss;
        ss << L"T" << (textureIndex / 20) << L" Section " << ((textureIndex % 20) + 1) << L"/20";
        return ss.str();
    }

    std::wstring FormatCommandTargetKind(const mapfmt::EventCommand& command)
    {
        switch (command.type)
        {
        case mapfmt::CommandType::OpenDoor: return L"opens/moves zone";
        case mapfmt::CommandType::ChangeTexture: return L"changes texture of zone";
        case mapfmt::CommandType::RotatePoly: return L"rotates zone/poly";
        default: return L"targets zone";
        }
    }

    std::wstring FormatSelectedZoneValidationSummary(int zoneIndex)
    {
        if (zoneIndex < 0 || zoneIndex >= static_cast<int>(g_app.document.zones.size())) return std::wstring();
        const auto& zone = g_app.document.zones[zoneIndex];
        std::wstringstream summary;
        bool wroteIssue = false;

        auto addLine = [&](const wchar_t* tag, const std::wstring& text)
        {
            summary << L"  " << tag << L" " << text << L"\r\n";
            wroteIssue = true;
        };

        if (IsLinearZoneType(zone.ztype) && zone.ztype != static_cast<int16_t>(mapfmt::ZoneType::Wall) &&
            zone.ev != 0 && !IsLevelEndZone(zone) && EventSlotFromZoneEventValue(zone.ev) < 0)
        {
            addLine(L"[WARN]", L"Invalid Event ID. Use 1-24, or 0 for an inert helper line.");
        }

        if (IsEventTriggerLineZone(zone) && !IsLevelEndZone(zone))
        {
            const int eventIndex = EventSlotFromZoneEventValue(zone.ev);
            if (eventIndex >= 0 && eventIndex < static_cast<int>(g_app.document.events.size()))
            {
                const auto& script = g_app.document.events[eventIndex];
                if (script.hasUnsupportedRaw)
                {
                    addLine(L"[INFO]", L"This event has preserved raw MAPED bytes; high-level editing stays conservative.");
                }
                else if (script.commands.empty())
                {
                    addLine(L"[INFO]", L"This trigger points to an empty event slot.");
                }
            }
        }

        const std::vector<uint8_t> guideMask = BuildMoveWallGroupGuideMaskForDocument(g_app.document);
        if (IsMoveWallGroupGuideZoneIndex(guideMask, zoneIndex))
        {
            const bool neutral = zone.a == 0 && zone.b == 0 && zone.na == 0 && zone.nb == 0 &&
                zone.ln == 0 && zone.sc == 0 && zone.ev == 0 &&
                std::all_of(zone.textures.begin(), zone.textures.end(), [](uint8_t t) { return t == 0; });
            addLine(neutral ? L"[OK]" : L"[WARN]",
                neutral ? L"Move-Wallblock guide is invisible and AI/collision-neutral."
                        : L"Move-Wallblock guide should be invisible and AI/collision-neutral. Validate Map can repair this safely.");
        }

        int outOfCorpusTextures = 0;
        for (uint8_t texture : zone.textures)
        {
            if (texture > 79) ++outOfCorpusTextures;
        }
        if (outOfCorpusTextures > 0)
        {
            std::wstringstream line;
            line << outOfCorpusTextures << L" texture reference(s) are outside the shipped 0-79 corpus range.";
            addLine(L"[INFO]", line.str());
        }

        if (!wroteIssue)
        {
            summary << L"  [OK] No immediate validation warning for the selected zone.\r\n";
        }
        return summary.str();
    }

    std::wstring FormatEventLogicSummaryForZone(int zoneIndex)
    {
        if (zoneIndex < 0 || zoneIndex >= static_cast<int>(g_app.document.zones.size())) return std::wstring();
        const auto& zone = g_app.document.zones[zoneIndex];
        std::wstringstream summary;

        if (IsLevelEndZone(zone))
        {
            summary << L"Logic: level exit trigger (special ev=24).\r\n";
            return summary.str();
        }

        if (IsEventTriggerLineZone(zone))
        {
            const int eventIndex = EventSlotFromZoneEventValue(zone.ev);
            summary << L"Logic: crossing this line fires Event " << zone.ev << L" (" << EventSlotRoleText(eventIndex) << L").\r\n";
            if (eventIndex >= 0 && g_app.document.events[eventIndex].hasUnsupportedRaw)
            {
                summary << L"  Advanced/raw MAPED command block is preserved verbatim. Editing disabled for safety.\r\n";
            }
            if (eventIndex >= 0 && !g_app.document.events[eventIndex].hasUnsupportedRaw)
            {
                const auto& commands = g_app.document.events[eventIndex].commands;

                std::vector<std::wstring> quickDetails;
                const auto targetZones = GetEventTargetZones(eventIndex);
                if (!targetZones.empty())
                {
                    std::wstringstream targets;
                    targets << L"targets ";
                    for (size_t targetIndex = 0; targetIndex < targetZones.size(); ++targetIndex)
                    {
                        if (targetIndex) targets << L", ";
                        targets << L"Z" << targetZones[targetIndex];
                    }
                    quickDetails.push_back(targets.str());
                }

                int addMonsterCount = 0;
                int targetCount = 0;
                int teleportCount = 0;
                int loadObjectCount = 0;
                for (const auto& command : commands)
                {
                    switch (command.type)
                    {
                    case mapfmt::CommandType::AddMonster:
                        ++addMonsterCount;
                        {
                            std::wstringstream spawn;
                            spawn << L"spawn t" << command.params[0]
                                  << L" x=" << command.params[1]
                                  << L" z=" << command.params[3]
                                  << L" r=" << (command.params[4] & 255);
                            quickDetails.push_back(spawn.str());
                        }
                        break;
                    case mapfmt::CommandType::LoadObjects:
                        ++loadObjectCount;
                        break;
                    case mapfmt::CommandType::Teleport:
                        ++teleportCount;
                        break;
                    case mapfmt::CommandType::OpenDoor:
                    case mapfmt::CommandType::ChangeTexture:
                    case mapfmt::CommandType::RotatePoly:
                        ++targetCount;
                        break;
                    default:
                        break;
                    }
                }
                if (!quickDetails.empty())
                {
                    summary << L"  Details: ";
                    for (size_t detailIndex = 0; detailIndex < quickDetails.size(); ++detailIndex)
                    {
                        if (detailIndex) summary << L" | ";
                        summary << quickDetails[detailIndex];
                    }
                    summary << L"\r\n";
                }
                if (loadObjectCount) summary << L"  Load object list command(s): " << loadObjectCount << L"\r\n";
                if (addMonsterCount) summary << L"  Spawns/adds objects: " << addMonsterCount << L" (shown as dashed yellow links)\r\n";
                if (teleportCount)
                {
                    summary << L"  Teleport command(s): " << teleportCount << L"\r\n";
                    for (const auto& command : commands)
                    {
                        if (command.type == mapfmt::CommandType::Teleport)
                        {
                            summary << L"    - to x=" << command.params[0] << L" z=" << command.params[2]
                                    << L" rot=" << command.params[3] << L"\r\n";
                        }
                    }
                }
                if (targetCount)
                {
                    summary << L"  Door/wall action command(s): " << targetCount << L"\r\n";
                    for (const auto& command : commands)
                    {
                        if (command.type == mapfmt::CommandType::OpenDoor)
                        {
                            summary << L"    - " << FormatCommandTargetKind(command) << L" Z" << command.params[0] << L"\r\n";
                        }
                        else if (command.type == mapfmt::CommandType::ChangeTexture)
                        {
                            summary << L"    - switch texture of Z" << command.params[0]
                                    << L" -> " << FormatTextureIndexShort(command.params[1]) << L"\r\n";
                        }
                        else if (command.type == mapfmt::CommandType::RotatePoly)
                        {
                            summary << L"    - rotates Z" << command.params[0] << L" count " << command.params[1]
                                << L" speed " << command.params[2]
                                << (command.params[2] < 0 ? L" (CCW)" : L" (CW)")
                                << L" flags " << command.params[3] << L"\r\n";
                        }
                    }
                }
            }
            return summary.str();
        }

        const auto controllingEvents = GetEventsControllingZone(zoneIndex);
        if (!controllingEvents.empty())
        {
            summary << L"Logic: this wall/zone is controlled by event command(s).\r\n";
            for (int eventIndex : controllingEvents)
            {
                const auto triggerZones = GetTriggerZonesForEvent(eventIndex);
                summary << L"  Event " << (eventIndex + 1) << L" (" << EventSlotRoleText(eventIndex) << L")";
                if (!triggerZones.empty())
                {
                    summary << L" from trigger" << (triggerZones.size() > 1 ? L"s " : L" ");
                    for (size_t i = 0; i < triggerZones.size(); ++i)
                    {
                        if (i) summary << L", ";
                        summary << L"Z" << triggerZones[i];
                    }
                }
                summary << L"\r\n";
                if (eventIndex >= 0 && eventIndex < static_cast<int>(g_app.document.events.size()) && g_app.document.events[eventIndex].hasUnsupportedRaw)
                {
                    summary << L"    Advanced/raw MAPED command block preserved.\r\n";
                }
                if (eventIndex >= 0 && eventIndex < static_cast<int>(g_app.document.events.size()) && !g_app.document.events[eventIndex].hasUnsupportedRaw)
                {
                    for (const auto& command : g_app.document.events[eventIndex].commands)
                    {
                        if (command.type == mapfmt::CommandType::ChangeTexture &&
                            DisplayZoneIndexForEventTarget(command.params[0]) == DisplayZoneIndexForEventTarget(zoneIndex))
                        {
                            summary << L"    Switch texture -> " << FormatTextureIndexShort(command.params[1]) << L"\r\n";
                        }
                    }
                }
            }
            if (IsWallZone(zone) && IsZoneControlledByOpenDoor(zoneIndex))
            {
                summary << L"  Door/wall is controlled by an OpenDoor event.\r\n";
            }
        }
        return summary.str();
    }

    int FindLevelEndZoneIndex()
    {
        for (int i = 0; i < static_cast<int>(g_app.document.zones.size()); ++i)
        {
            if (IsLevelEndZone(g_app.document.zones[i]))
            {
                return i;
            }
        }
        return -1;
    }

    void SetLevelEndFromDrawPoints(POINT a, POINT b)
    {
        if (IsTinyDrawSegment(a, b))
        {
            b.x = a.x + 256;
        }

        mapfmt::Zone zone;
        const int existing = FindLevelEndZoneIndex();
        if (existing >= 0 && existing < static_cast<int>(g_app.document.zones.size()))
        {
            zone = g_app.document.zones[existing];
        }
        // Shipped map1_1 uses zone type 2 with ev=24 for the exit/level-complete line.
        zone.ztype = static_cast<int16_t>(mapfmt::ZoneType::MonsterZone);
        zone.ev = kLevelEndEventValue;
        zone.x1 = ClampWorldToInt16(a.x);
        zone.z1 = ClampWorldToInt16(a.y);
        zone.x2 = ClampWorldToInt16(b.x);
        zone.z2 = ClampWorldToInt16(b.y);
        mapfmt::RecalculateWallMetadata(zone);

        PushUndoSnapshot();
        if (existing >= 0 && existing < static_cast<int>(g_app.document.zones.size()))
        {
            g_app.document.zones[existing] = zone;
            g_app.selectedZone = existing;
        }
        else
        {
            g_app.document.zones.push_back(zone);
            g_app.selectedZone = static_cast<int>(g_app.document.zones.size()) - 1;
        }
        ClearSelectedMonsterSpawn();
        ClearSelectedTeleportTarget();
        ClearPendingSwitchTextureChoice();
        g_app.previewTextureBand = 0;
        MarkDirty();
        RefreshZoneList();
        RefreshPreviewImage();
        UpdateModeButtons();
        RefreshStatus();
        InvalidateEditorViews();
    }

    void DrawZoneOverlayLabel(HDC hdc, const std::wstring& label, int x, int y, COLORREF textColor)
    {
        if (label.empty()) return;
        const COLORREF oldColor = SetTextColor(hdc, RGB(12, 12, 12));
        const int lineHeight = 16;

        size_t start = 0;
        int line = 0;
        while (start <= label.size())
        {
            const size_t end = label.find(L'\n', start);
            const std::wstring part = label.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
            if (!part.empty())
            {
                const int yy = y + line * lineHeight;
                SetTextColor(hdc, RGB(12, 12, 12));
                TextOutW(hdc, x + 1, yy + 1, part.c_str(), static_cast<int>(part.size()));
                SetTextColor(hdc, textColor);
                TextOutW(hdc, x, yy, part.c_str(), static_cast<int>(part.size()));
            }
            if (end == std::wstring::npos) break;
            start = end + 1;
            ++line;
        }

        SetTextColor(hdc, oldColor);
    }

    struct PendingZoneOverlayLabel
    {
        std::wstring text;
        int x = 0;
        int y = 0;
        POINT p1{};
        POINT p2{};
        COLORREF color = RGB(220, 220, 220);
    };

    SIZE MeasureZoneOverlayLabel(HDC hdc, const std::wstring& label)
    {
        constexpr int lineHeight = 16;
        SIZE result{ 0, lineHeight };
        if (label.empty()) return result;

        int lines = 0;
        size_t start = 0;
        while (start <= label.size())
        {
            const size_t end = label.find(L'\n', start);
            const std::wstring part = label.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
            SIZE partSize{ 0, lineHeight };
            if (!part.empty())
            {
                GetTextExtentPoint32W(hdc, part.c_str(), static_cast<int>(part.size()), &partSize);
            }
            result.cx = MaxValue(result.cx, partSize.cx);
            ++lines;
            if (end == std::wstring::npos) break;
            start = end + 1;
        }

        result.cy = MaxValue(lineHeight, lines * lineHeight);
        return result;
    }

    RECT MakeZoneOverlayLabelRect(HDC hdc, const std::wstring& label, POINT pos)
    {
        const SIZE size = MeasureZoneOverlayLabel(hdc, label);
        return RECT{ pos.x - 2, pos.y - 2, pos.x + size.cx + 6, pos.y + size.cy + 4 };
    }

    bool RectsOverlapWithPadding(const RECT& a, const RECT& b, int padding = 3)
    {
        RECT aa = a;
        InflateRect(&aa, padding, padding);
        RECT bb = b;
        InflateRect(&bb, padding, padding);
        return aa.left < bb.right && aa.right > bb.left && aa.top < bb.bottom && aa.bottom > bb.top;
    }

    bool ZoneOverlayLabelRectIsFree(const RECT& candidate, const std::vector<RECT>& placed)
    {
        for (const RECT& used : placed)
        {
            if (RectsOverlapWithPadding(candidate, used)) return false;
        }
        return true;
    }

    POINT ClampZoneOverlayLabelPoint(const RECT& canvasRc, const SIZE& size, POINT pos)
    {
        const int minX = canvasRc.left + 6;
        const int minY = canvasRc.top + 6;
        const int maxX = MaxValue(minX, canvasRc.right - size.cx - 8);
        const int maxY = MaxValue(minY, canvasRc.bottom - size.cy - 8);
        pos.x = ClampValue(pos.x, minX, maxX);
        pos.y = ClampValue(pos.y, minY, maxY);
        return pos;
    }

    int ZoneOverlayLabelOverlapArea(const RECT& candidate, const std::vector<RECT>& placed)
    {
        int area = 0;
        for (const RECT& used : placed)
        {
            RECT c = candidate;
            RECT u = used;
            InflateRect(&u, 3, 3);
            const int left = MaxValue(c.left, u.left);
            const int top = MaxValue(c.top, u.top);
            const int right = MinValue(c.right, u.right);
            const int bottom = MinValue(c.bottom, u.bottom);
            if (right > left && bottom > top)
            {
                area += (right - left) * (bottom - top);
            }
        }
        return area;
    }

    POINT ResolveZoneOverlayLabelPoint(HDC hdc, const RECT& canvasRc, const PendingZoneOverlayLabel& label, std::vector<RECT>& placed)
    {
        const SIZE size = MeasureZoneOverlayLabel(hdc, label.text);
        const int verticalStep = MaxValue(18, size.cy + 4);
        const int horizontalStep = MaxValue(42, size.cx + 10);
        std::vector<POINT> candidates;
        candidates.reserve(96);

        auto addCandidate = [&](int x, int y)
        {
            POINT p{ x, y };
            p = ClampZoneOverlayLabelPoint(canvasRc, size, p);
            for (const POINT& existing : candidates)
            {
                if (existing.x == p.x && existing.y == p.y) return;
            }
            candidates.push_back(p);
        };

        const double midX = (static_cast<double>(label.p1.x) + static_cast<double>(label.p2.x)) * 0.5;
        const double midY = (static_cast<double>(label.p1.y) + static_cast<double>(label.p2.y)) * 0.5;
        const double dx = static_cast<double>(label.p2.x - label.p1.x);
        const double dy = static_cast<double>(label.p2.y - label.p1.y);
        const double len = MaxValue(1.0, std::sqrt(dx * dx + dy * dy));
        const double nx = -dy / len;
        const double ny = dx / len;
        const double tx = dx / len;
        const double ty = dy / len;

        addCandidate(label.x, label.y);

        // Keep Z labels close to their real line. First try small offsets around
        // the line center, then a short stack at the original anchor. Avoid the
        // previous wide grid search because it could move labels far away from
        // the wall/trigger they belong to.
        const int normalSigns[] = { 1, -1 };
        const int normalSteps[] = { 14, 28, 42, 56 };
        const int tangentSteps[] = { 0, -36, 36, -72, 72 };
        for (int normalSign : normalSigns)
        {
            for (int normalStep : normalSteps)
            {
                for (int tangentStep : tangentSteps)
                {
                    const int cx = static_cast<int>(std::lround(midX + nx * normalSign * normalStep + tx * tangentStep));
                    const int cy = static_cast<int>(std::lround(midY + ny * normalSign * normalStep + ty * tangentStep));
                    addCandidate(cx, cy);
                }
            }
        }

        for (int step = 1; step <= 4; ++step)
        {
            addCandidate(label.x, label.y + step * verticalStep);
            addCandidate(label.x, label.y - step * verticalStep);
            addCandidate(label.x + horizontalStep, label.y + step * verticalStep);
            addCandidate(label.x - horizontalStep, label.y + step * verticalStep);
            addCandidate(label.x + horizontalStep, label.y - step * verticalStep);
            addCandidate(label.x - horizontalStep, label.y - step * verticalStep);
        }

        for (POINT candidate : candidates)
        {
            const RECT candidateRect = MakeZoneOverlayLabelRect(hdc, label.text, candidate);
            if (ZoneOverlayLabelRectIsFree(candidateRect, placed))
            {
                placed.push_back(candidateRect);
                return candidate;
            }
        }

        // Dense fallback: choose the least-overlapping local candidate instead of
        // jumping across the map. This keeps the label attached to its line even
        // in very crowded sections.
        POINT best = candidates.empty() ? ClampZoneOverlayLabelPoint(canvasRc, size, POINT{ label.x, label.y }) : candidates.front();
        int bestOverlap = 0x7fffffff;
        int bestDistance = 0x7fffffff;
        for (POINT candidate : candidates)
        {
            const RECT candidateRect = MakeZoneOverlayLabelRect(hdc, label.text, candidate);
            const int overlap = ZoneOverlayLabelOverlapArea(candidateRect, placed);
            const int dist = std::abs(candidate.x - label.x) + std::abs(candidate.y - label.y);
            if (overlap < bestOverlap || (overlap == bestOverlap && dist < bestDistance))
            {
                best = candidate;
                bestOverlap = overlap;
                bestDistance = dist;
                if (bestOverlap == 0) break;
            }
        }

        RECT fallbackRect = MakeZoneOverlayLabelRect(hdc, label.text, best);
        placed.push_back(fallbackRect);
        return best;
    }

    void DrawEventMonsterSpawnOverlays(HDC hdc, const RECT& rc, const mapfmt::Bounds& bounds)
    {
        int markerIndex = 0;
        for (int eventIndex = 0; eventIndex < static_cast<int>(g_app.document.events.size()); ++eventIndex)
        {
            const auto& script = g_app.document.events[eventIndex];
            for (int commandIndex = 0; commandIndex < static_cast<int>(script.commands.size()); ++commandIndex)
            {
                const auto& command = script.commands[commandIndex];
                if (command.type != mapfmt::CommandType::AddMonster)
                {
                    continue;
                }

                const int monsterType = command.params[0];
                const int x = command.params[1];
                const int z = command.params[3];
                const int rot = command.params[4] & 255;
                const POINT p = WorldToScreen(rc, bounds, x, z);
                const bool selected = g_app.selectedMonsterSpawn.eventIndex == eventIndex &&
                    g_app.selectedMonsterSpawn.commandIndex == commandIndex;

                const int radius = selected ? 10 : 7;
                HBRUSH fill = CreateSolidBrush(MonsterTypeColor(monsterType));
                HPEN edge = CreatePen(PS_SOLID, selected ? 3 : 2, selected ? RGB(255, 255, 255) : RGB(255, 194, 72));
                HGDIOBJ oldBrush = SelectObject(hdc, fill);
                HGDIOBJ oldPen = SelectObject(hdc, edge);
                Ellipse(hdc, p.x - radius, p.y - radius, p.x + radius, p.y + radius);

                const double angle = (static_cast<double>(rot) / 256.0) * 6.28318530717958647692;
                const int arrowX = p.x + static_cast<int>(std::lround(std::sin(angle) * 18.0));
                const int arrowY = p.y - static_cast<int>(std::lround(std::cos(angle) * 18.0));
                MoveToEx(hdc, p.x, p.y, nullptr);
                LineTo(hdc, arrowX, arrowY);

                SelectObject(hdc, oldPen);
                SelectObject(hdc, oldBrush);
                DeleteObject(edge);
                DeleteObject(fill);

                std::wstringstream label;
                if (eventIndex == kInitialEventIndex && monsterType == kPlayer1ObjectType)
                {
                    label << L"P1 Start";
                }
                else if (eventIndex == kInitialEventIndex && monsterType == kPlayer2ObjectType)
                {
                    label << L"P2 Start";
                }
                else
                {
                    label << L"M" << markerIndex << L" t" << monsterType << L" E" << (eventIndex + 1);
                }
                const std::wstring markerLabel = label.str();
                SIZE markerLabelSize = MeasureZoneOverlayLabel(hdc, markerLabel);
                POINT markerLabelPos = selected
                    ? POINT{ p.x - markerLabelSize.cx - 12, p.y - markerLabelSize.cy - 12 }
                    : POINT{ p.x + 10, p.y - 18 };
                markerLabelPos = ClampZoneOverlayLabelPoint(rc, markerLabelSize, markerLabelPos);
                DrawZoneOverlayLabel(hdc, markerLabel, markerLabelPos.x, markerLabelPos.y,
                    selected ? RGB(255, 255, 255) : (monsterType <= 1 ? RGB(145, 215, 255) : RGB(255, 218, 122)));
                ++markerIndex;
            }
        }
    }

    POINT ZoneCenterScreen(const mapfmt::Zone& zone, const RECT& rc, const mapfmt::Bounds& bounds)
    {
        return WorldToScreen(rc, bounds,
            static_cast<int>((static_cast<int>(zone.x1) + static_cast<int>(zone.x2)) / 2),
            static_cast<int>((static_cast<int>(zone.z1) + static_cast<int>(zone.z2)) / 2));
    }

    void DrawHighlightedZoneLine(HDC hdc, const RECT& rc, const mapfmt::Bounds& bounds, int zoneIndex, COLORREF color, const std::wstring& label)
    {
        if (zoneIndex < 0 || zoneIndex >= static_cast<int>(g_app.document.zones.size())) return;
        const auto& zone = g_app.document.zones[zoneIndex];
        if (!IsLinearZoneType(zone.ztype)) return;
        const POINT p1 = WorldToScreen(rc, bounds, zone.x1, zone.z1);
        const POINT p2 = WorldToScreen(rc, bounds, zone.x2, zone.z2);
        HPEN pen = CreatePen(PS_SOLID, 6, color);
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
        MoveToEx(hdc, p1.x, p1.y, nullptr);
        LineTo(hdc, p2.x, p2.y);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
        // v49: Selection/logic labels use the cleaner classic placement again:
        // left/above the affected line. For vertical or downward-running lines,
        // keep the label outside on the left instead of pushing it to the right.
        const SIZE labelSize = MeasureZoneOverlayLabel(hdc, label);
        const int minX = MinValue(p1.x, p2.x);
        const int minY = MinValue(p1.y, p2.y);
        const int dx = std::abs(p2.x - p1.x);
        const int dy = std::abs(p2.y - p1.y);
        const bool verticalOrDownward = dy > dx || p2.y > p1.y;
        POINT labelPos{};
        if (verticalOrDownward)
        {
            labelPos = POINT{ minX - labelSize.cx - 10, minY - labelSize.cy - 6 };
        }
        else
        {
            labelPos = POINT{ minX + 4, minY - labelSize.cy - 8 };
        }
        labelPos = ClampZoneOverlayLabelPoint(rc, labelSize, labelPos);
        DrawZoneOverlayLabel(hdc, label, labelPos.x, labelPos.y, RGB(255, 245, 190));
    }

    void DrawLogicConnectionLine(HDC hdc, const POINT& from, const POINT& to, COLORREF color)
    {
        HPEN pen = CreatePen(PS_DOT, 1, color);
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
        MoveToEx(hdc, from.x, from.y, nullptr);
        LineTo(hdc, to.x, to.y);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }

    void DrawEventSpawnConnectionLines(HDC hdc, const RECT& rc, const mapfmt::Bounds& bounds, int eventIndex, const POINT& from)
    {
        if (eventIndex < 0 || eventIndex >= static_cast<int>(g_app.document.events.size())) return;

        const auto& commands = g_app.document.events[eventIndex].commands;
        for (const auto& command : commands)
        {
            if (command.type != mapfmt::CommandType::AddMonster)
            {
                continue;
            }

            const POINT spawnPoint = WorldToScreen(rc, bounds, command.params[1], command.params[3]);
            DrawLogicConnectionLine(hdc, from, spawnPoint, RGB(255, 205, 86));
        }
    }

    void DrawEventGraphZoneLine(HDC hdc, const RECT& rc, const mapfmt::Bounds& bounds, int zoneIndex, COLORREF color)
    {
        if (zoneIndex < 0 || zoneIndex >= static_cast<int>(g_app.document.zones.size())) return;
        const auto& zone = g_app.document.zones[zoneIndex];
        if (!IsLinearZoneType(zone.ztype)) return;
        POINT p1 = WorldToScreen(rc, bounds, zone.x1, zone.z1);
        POINT p2 = WorldToScreen(rc, bounds, zone.x2, zone.z2);
        HPEN pen = CreatePen(PS_SOLID, 3, color);
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
        MoveToEx(hdc, p1.x, p1.y, nullptr);
        LineTo(hdc, p2.x, p2.y);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }

    POINT EventGraphRotateCommandCenterScreen(const mapfmt::EventCommand& command, const RECT& rc, const mapfmt::Bounds& bounds)
    {
        const int first = static_cast<int>(command.params[0]);
        const int count = MaxValue(1, static_cast<int>(command.params[1]));
        if (first < 0 || first >= static_cast<int>(g_app.document.zones.size()))
        {
            return POINT{0, 0};
        }
        const int safeCount = ClampValue(count, 1, static_cast<int>(g_app.document.zones.size()) - first);
        POINT world = CalculateZoneRangeCenterWorld(first, safeCount);
        return WorldToScreen(rc, bounds, world.x, world.y);
    }

    void DrawEventGraphOverlay(HDC hdc, const RECT& rc, const mapfmt::Bounds& bounds)
    {
        if (!g_app.showEventGraphOverlay) return;

        int drawnLabels = 0;
        constexpr int kMaxEventLabels = 28;
        for (int triggerZone = 0; triggerZone < static_cast<int>(g_app.document.zones.size()); ++triggerZone)
        {
            const auto& trigger = g_app.document.zones[triggerZone];
            if (!IsEventTriggerLineZone(trigger) || IsLevelEndZone(trigger))
            {
                continue;
            }

            const int eventIndex = EventSlotFromZoneEventValue(trigger.ev);
            if (eventIndex < 0 || eventIndex >= static_cast<int>(g_app.document.events.size()))
            {
                continue;
            }
            const auto& script = g_app.document.events[eventIndex];
            if (script.hasUnsupportedRaw || script.commands.empty())
            {
                continue;
            }

            const POINT from = ZoneCenterScreen(trigger, rc, bounds);
            bool drewConnection = false;
            for (const auto& command : script.commands)
            {
                switch (command.type)
                {
                case mapfmt::CommandType::OpenDoor:
                case mapfmt::CommandType::ChangeTexture:
                    if (command.params[0] >= 0 && command.params[0] < static_cast<int>(g_app.document.zones.size()))
                    {
                        DrawLogicConnectionLine(hdc, from, ZoneCenterScreen(g_app.document.zones[command.params[0]], rc, bounds), RGB(98, 190, 232));
                        drewConnection = true;
                    }
                    break;
                case mapfmt::CommandType::RotatePoly:
                    if (command.params[0] >= 0 && command.params[0] < static_cast<int>(g_app.document.zones.size()))
                    {
                        DrawLogicConnectionLine(hdc, from, EventGraphRotateCommandCenterScreen(command, rc, bounds), RGB(116, 205, 255));
                        drewConnection = true;
                    }
                    break;
                case mapfmt::CommandType::Teleport:
                    DrawLogicConnectionLine(hdc, from, WorldToScreen(rc, bounds, command.params[0], command.params[2]), RGB(182, 136, 255));
                    drewConnection = true;
                    break;
                case mapfmt::CommandType::AddMonster:
                    DrawLogicConnectionLine(hdc, from, WorldToScreen(rc, bounds, command.params[1], command.params[3]), RGB(255, 205, 86));
                    drewConnection = true;
                    break;
                default:
                    break;
                }
            }

            if (drewConnection)
            {
                DrawEventGraphZoneLine(hdc, rc, bounds, triggerZone, RGB(255, 176, 48));
                if (drawnLabels < kMaxEventLabels)
                {
                    const std::wstring label = L"E" + std::to_wstring(eventIndex + 1);
                    DrawZoneOverlayLabel(hdc, label, from.x + 5, from.y - 18, RGB(255, 235, 150));
                    ++drawnLabels;
                }
            }
        }
    }

    void DrawEventLogicOverlay(HDC hdc, const RECT& rc, const mapfmt::Bounds& bounds)
    {
        if ((g_app.insertMode == InsertMode::LinkEventToZone ||
             g_app.insertMode == InsertMode::LinkEventToSwitchTexture ||
             g_app.insertMode == InsertMode::LinkEventToEnemyObject ||
             g_app.insertMode == InsertMode::DeleteLinkEventToEnemyObject ||
             g_app.insertMode == InsertMode::LinkEventToRotateClockwise ||
             g_app.insertMode == InsertMode::LinkEventToRotateCounterClockwise ||
             g_app.insertMode == InsertMode::LinkEventToMoveWallGroup ||
             g_app.insertMode == InsertMode::SetTeleportTarget) &&
            g_app.linkEventTriggerZone >= 0 && g_app.linkEventTriggerZone < static_cast<int>(g_app.document.zones.size()))
        {
            DrawHighlightedZoneLine(hdc, rc, bounds, g_app.linkEventTriggerZone, RGB(255, 176, 48), L"Link source");
            return;
        }
        if (IsSelectedTeleportTargetValid())
        {
            const mapfmt::EventCommand* command = GetSelectedTeleportTargetCommand();
            const int eventIndex = g_app.selectedTeleportTarget.eventIndex;
            if (command && eventIndex >= 0 && eventIndex < static_cast<int>(g_app.document.events.size()))
            {
                const POINT targetCenter = WorldToScreen(rc, bounds, command->params[0], command->params[2]);
                const auto triggers = GetTriggerZonesForEvent(eventIndex);
                for (int triggerZone : triggers)
                {
                    if (triggerZone < 0 || triggerZone >= static_cast<int>(g_app.document.zones.size())) continue;
                    DrawHighlightedZoneLine(hdc, rc, bounds, triggerZone, RGB(255, 176, 48), L"Trigger E" + std::to_wstring(eventIndex + 1));
                    DrawLogicConnectionLine(hdc, ZoneCenterScreen(g_app.document.zones[triggerZone], rc, bounds), targetCenter, RGB(180, 130, 255));
                }
                if (triggers.empty())
                {
                    const std::wstring teleportLabel = L"Teleport E" + std::to_wstring(eventIndex + 1);
                    const SIZE teleportLabelSize = MeasureZoneOverlayLabel(hdc, teleportLabel);
                    POINT teleportLabelPos{ targetCenter.x - teleportLabelSize.cx - 12, targetCenter.y - teleportLabelSize.cy - 12 };
                    teleportLabelPos = ClampZoneOverlayLabelPoint(rc, teleportLabelSize, teleportLabelPos);
                    DrawZoneOverlayLabel(hdc, teleportLabel, teleportLabelPos.x, teleportLabelPos.y, RGB(220, 190, 255));
                }
            }
            return;
        }

        if (IsSelectedMonsterSpawnValid())
        {
            const mapfmt::EventCommand* command = GetSelectedMonsterSpawnCommand();
            const int eventIndex = g_app.selectedMonsterSpawn.eventIndex;
            if (command && eventIndex >= 0 && eventIndex < static_cast<int>(g_app.document.events.size()))
            {
                const POINT spawnCenter = WorldToScreen(rc, bounds, command->params[1], command->params[3]);
                const auto triggers = GetTriggerZonesForEvent(eventIndex);
                for (int triggerZone : triggers)
                {
                    if (triggerZone < 0 || triggerZone >= static_cast<int>(g_app.document.zones.size())) continue;
                    DrawHighlightedZoneLine(hdc, rc, bounds, triggerZone, RGB(255, 176, 48), L"Trigger E" + std::to_wstring(eventIndex + 1));
                    DrawLogicConnectionLine(hdc, ZoneCenterScreen(g_app.document.zones[triggerZone], rc, bounds), spawnCenter, RGB(255, 205, 86));
                }
                const std::wstring spawnLabel = L"Spawn E" + std::to_wstring(eventIndex + 1);
                const SIZE spawnLabelSize = MeasureZoneOverlayLabel(hdc, spawnLabel);
                POINT spawnLabelPos{ spawnCenter.x - spawnLabelSize.cx - 12, spawnCenter.y - spawnLabelSize.cy - 12 };
                spawnLabelPos = ClampZoneOverlayLabelPoint(rc, spawnLabelSize, spawnLabelPos);
                DrawZoneOverlayLabel(hdc, spawnLabel, spawnLabelPos.x, spawnLabelPos.y, RGB(255, 225, 145));
            }
            return;
        }

        if (g_app.selectedZone < 0 || g_app.selectedZone >= static_cast<int>(g_app.document.zones.size())) return;
        const auto& selected = g_app.document.zones[g_app.selectedZone];

        if (IsEventTriggerLineZone(selected))
        {
            const int eventIndex = EventSlotFromZoneEventValue(selected.ev);
            const auto targets = GetEventTargetZones(eventIndex);
            const POINT triggerCenter = ZoneCenterScreen(selected, rc, bounds);
            DrawHighlightedZoneLine(hdc, rc, bounds, g_app.selectedZone, RGB(255, 176, 48), L"Trigger E" + std::to_wstring(selected.ev));
            for (int targetZone : targets)
            {
                DrawHighlightedZoneLine(hdc, rc, bounds, targetZone, RGB(90, 210, 255), L"Target Z" + std::to_wstring(targetZone));
                DrawLogicConnectionLine(hdc, triggerCenter, ZoneCenterScreen(g_app.document.zones[targetZone], rc, bounds), RGB(120, 200, 255));
            }
            if (eventIndex >= 0 && eventIndex < static_cast<int>(g_app.document.events.size()))
            {
                for (const auto& command : g_app.document.events[eventIndex].commands)
                {
                    if (command.type == mapfmt::CommandType::Teleport)
                    {
                        POINT tp = WorldToScreen(rc, bounds, command.params[0], command.params[2]);
                        DrawLogicConnectionLine(hdc, triggerCenter, tp, RGB(180, 130, 255));
                    }
                }
                DrawEventSpawnConnectionLines(hdc, rc, bounds, eventIndex, triggerCenter);
                DrawMoveWallGroupEventPaths(hdc, rc, bounds, eventIndex);
            }
            return;
        }

        const auto controllingEvents = GetEventsControllingZone(g_app.selectedZone);
        if (!controllingEvents.empty())
        {
            DrawHighlightedZoneLine(hdc, rc, bounds, g_app.selectedZone, RGB(90, 210, 255), L"Controlled Z" + std::to_wstring(g_app.selectedZone));
            const POINT targetCenter = ZoneCenterScreen(selected, rc, bounds);
            for (int eventIndex : controllingEvents)
            {
                const auto triggers = GetTriggerZonesForEvent(eventIndex);
                for (int triggerZone : triggers)
                {
                    DrawHighlightedZoneLine(hdc, rc, bounds, triggerZone, RGB(255, 176, 48), L"Trigger E" + std::to_wstring(eventIndex + 1));
                    DrawLogicConnectionLine(hdc, ZoneCenterScreen(g_app.document.zones[triggerZone], rc, bounds), targetCenter, RGB(120, 200, 255));
                }
            }
        }
    }

    void CancelMonsterSpawnDrag(bool refreshViews)
    {
        g_app.monsterSpawnDragging = false;
        g_app.monsterSpawnDragMoved = false;
        g_app.monsterSpawnDragSnapshotTaken = false;
        g_app.monsterSpawnDragSelection.Clear();
        if (refreshViews)
        {
            RefreshStatus();
            InvalidateEditorViews();
        }
    }

    bool BeginMonsterSpawnDrag(const MonsterSpawnSelection& selection, POINT clientPoint, const RECT& rc)
    {
        if (!selection.IsSet()) return false;
        if (selection.eventIndex < 0 || selection.eventIndex >= static_cast<int>(g_app.document.events.size())) return false;
        auto& commands = g_app.document.events[selection.eventIndex].commands;
        if (selection.commandIndex < 0 || selection.commandIndex >= static_cast<int>(commands.size())) return false;
        auto& command = commands[selection.commandIndex];
        if (command.type != mapfmt::CommandType::AddMonster) return false;

        g_app.monsterSpawnDragging = true;
        g_app.monsterSpawnDragMoved = false;
        g_app.monsterSpawnDragSnapshotTaken = false;
        g_app.monsterSpawnDragSelection = selection;
        g_app.monsterSpawnDragStartClient = clientPoint;
        g_app.monsterSpawnDragStartWorld = ScreenToWorld(rc, g_app.document.ComputeBounds(), clientPoint.x, clientPoint.y);
        g_app.monsterSpawnDragStartX = command.params[1];
        g_app.monsterSpawnDragStartZ = command.params[3];
        return true;
    }

    bool UpdateMonsterSpawnDrag(POINT clientPoint, const RECT& rc)
    {
        if (!g_app.monsterSpawnDragging) return false;
        const int dxClient = clientPoint.x - g_app.monsterSpawnDragStartClient.x;
        const int dyClient = clientPoint.y - g_app.monsterSpawnDragStartClient.y;
        if (!g_app.monsterSpawnDragSnapshotTaken)
        {
            if ((dxClient * dxClient + dyClient * dyClient) < 9)
            {
                return true;
            }
            PushUndoSnapshot();
            g_app.monsterSpawnDragSnapshotTaken = true;
        }

        if (g_app.monsterSpawnDragSelection.eventIndex < 0 ||
            g_app.monsterSpawnDragSelection.eventIndex >= static_cast<int>(g_app.document.events.size()))
        {
            CancelMonsterSpawnDrag(false);
            return false;
        }

        auto& commands = g_app.document.events[g_app.monsterSpawnDragSelection.eventIndex].commands;
        if (g_app.monsterSpawnDragSelection.commandIndex < 0 ||
            g_app.monsterSpawnDragSelection.commandIndex >= static_cast<int>(commands.size()))
        {
            CancelMonsterSpawnDrag(false);
            return false;
        }

        auto& command = commands[g_app.monsterSpawnDragSelection.commandIndex];
        if (command.type != mapfmt::CommandType::AddMonster)
        {
            CancelMonsterSpawnDrag(false);
            return false;
        }

        const POINT world = ScreenToWorld(rc, g_app.document.ComputeBounds(), clientPoint.x, clientPoint.y);
        const int newX = g_app.monsterSpawnDragStartX + (world.x - g_app.monsterSpawnDragStartWorld.x);
        const int newZ = g_app.monsterSpawnDragStartZ + (world.y - g_app.monsterSpawnDragStartWorld.y);
        command.params[1] = ClampWorldToInt16(newX);
        command.params[3] = ClampWorldToInt16(newZ);
        g_app.monsterSpawnDragMoved = true;

        if (g_app.monsterSpawnDragSelection.eventIndex == kInitialEventIndex && command.params[0] == kPlayer1ObjectType)
        {
            g_app.walkPreviewX = static_cast<double>(command.params[1]);
            g_app.walkPreviewZ = static_cast<double>(command.params[3]);
            g_app.walkPreviewInitialized = true;
        }

        RefreshPreviewImage();
        RefreshStatus();
        InvalidateEditorViews();
        return true;
    }

    bool FinishMonsterSpawnDrag()
    {
        if (!g_app.monsterSpawnDragging) return false;
        const bool moved = g_app.monsterSpawnDragMoved;
        CancelMonsterSpawnDrag(false);
        if (moved)
        {
            MarkDirty();
            RefreshZoneList();
            RefreshPreviewImage();
            UpdateModeButtons();
            RefreshStatus();
            InvalidateEditorViews();
        }
        else
        {
            RefreshStatus();
        }
        return true;
    }

    MonsterSpawnSelection HitTestMonsterSpawn(int sx, int sy, const RECT& rc)
    {
        MonsterSpawnSelection result;
        const auto bounds = g_app.document.ComputeBounds();
        int markerIndex = 0;
        int bestDistanceSq = 15 * 15;
        for (int eventIndex = 0; eventIndex < static_cast<int>(g_app.document.events.size()); ++eventIndex)
        {
            const auto& script = g_app.document.events[eventIndex];
            for (int commandIndex = 0; commandIndex < static_cast<int>(script.commands.size()); ++commandIndex)
            {
                const auto& command = script.commands[commandIndex];
                if (command.type != mapfmt::CommandType::AddMonster)
                {
                    continue;
                }
                const POINT p = WorldToScreen(rc, bounds, command.params[1], command.params[3]);
                const int dx = sx - p.x;
                const int dy = sy - p.y;
                const int distSq = dx * dx + dy * dy;
                if (distSq <= bestDistanceSq)
                {
                    bestDistanceSq = distSq;
                    result.eventIndex = eventIndex;
                    result.commandIndex = commandIndex;
                    result.markerIndex = markerIndex;
                }
                ++markerIndex;
            }
        }
        return result;
    }

    TeleportSelection HitTestTeleportTarget(int sx, int sy, const RECT& rc)
    {
        TeleportSelection result;
        const auto bounds = g_app.document.ComputeBounds();
        int markerIndex = 0;
        int bestDistanceSq = 17 * 17;
        for (int eventIndex = 0; eventIndex < static_cast<int>(g_app.document.events.size()); ++eventIndex)
        {
            const auto& script = g_app.document.events[eventIndex];
            for (int commandIndex = 0; commandIndex < static_cast<int>(script.commands.size()); ++commandIndex)
            {
                const auto& command = script.commands[commandIndex];
                if (command.type != mapfmt::CommandType::Teleport)
                {
                    continue;
                }
                const POINT p = WorldToScreen(rc, bounds, command.params[0], command.params[2]);
                const int dx = sx - p.x;
                const int dy = sy - p.y;
                const int distSq = dx * dx + dy * dy;
                if (distSq <= bestDistanceSq)
                {
                    bestDistanceSq = distSq;
                    result.eventIndex = eventIndex;
                    result.commandIndex = commandIndex;
                    result.markerIndex = markerIndex;
                }
                ++markerIndex;
            }
        }
        return result;
    }

    RECT GetTextureSlotBarRect(const RECT& panelRc)
    {
        const int leftPad = 12;
        const int rightPad = 12;
        const int previewTop = 330;
        return RECT{ leftPad, previewTop - 34, panelRc.right - rightPad, previewTop - 10 };
    }

    PreviewMetrics GetPreviewMetrics(const RECT& panelRc)
    {
        PreviewMetrics metrics{};
        const int leftPad = 12;
        const int rightPad = 12;
        const int previewTop = 330;
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

    RECT GetPreviewScrollBarRect(const RECT& panelRc)
    {
        const PreviewMetrics preview = GetPreviewMetrics(panelRc);
        return RECT{ preview.outer.left + 2, preview.outer.bottom + 5, preview.outer.right - 2, preview.outer.bottom + 17 };
    }

    RECT GetPreviewScrollThumbRect(const RECT& scrollRc, const PreviewMetrics& preview)
    {
        if (preview.maxScrollX <= 0)
        {
            return RECT{ scrollRc.left, scrollRc.top, scrollRc.left, scrollRc.top };
        }
        const int trackW = MaxValue(1, scrollRc.right - scrollRc.left - 4);
        const int thumbW = ClampValue(static_cast<int>(std::lround(static_cast<double>(trackW) * static_cast<double>(preview.viewportWidth) / MaxValue(1.0, static_cast<double>(preview.scaledWidth)))), 24, trackW);
        const int thumbRange = MaxValue(0, trackW - thumbW);
        const int thumbX = scrollRc.left + 2 + (preview.maxScrollX > 0 ? static_cast<int>(std::lround(static_cast<double>(thumbRange) * static_cast<double>(g_app.previewScrollX) / static_cast<double>(preview.maxScrollX))) : 0);
        return RECT{ thumbX, scrollRc.top + 2, thumbX + thumbW, scrollRc.bottom - 2 };
    }

    void DrawSlimHorizontalScrollBar(HDC hdc, const RECT& rc, const RECT& thumbRc, bool hover, bool dragging)
    {
        if (!hdc || rc.right <= rc.left || rc.bottom <= rc.top) return;

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

    void SetPreviewScrollFromThumbX(HWND hwnd, int sx, int thumbOffsetX)
    {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const PreviewMetrics preview = GetPreviewMetrics(rc);
        const RECT scrollRc = GetPreviewScrollBarRect(rc);
        if (preview.maxScrollX <= 0) return;

        const RECT thumbRc = GetPreviewScrollThumbRect(scrollRc, preview);
        const int thumbW = MaxValue(1, thumbRc.right - thumbRc.left);
        const int trackW = MaxValue(1, scrollRc.right - scrollRc.left - 4);
        const int thumbRange = MaxValue(1, trackW - thumbW);
        const int targetX = ClampValue(sx - (scrollRc.left + 2) - thumbOffsetX, 0, thumbRange);
        g_app.previewScrollX = ClampValue(static_cast<int>(std::lround(static_cast<double>(preview.maxScrollX) * static_cast<double>(targetX) / static_cast<double>(thumbRange))), 0, preview.maxScrollX);
        InvalidateTexturePreviewOnly(hwnd);
    }

    void BeginSmoothPreviewScrollDrag(HWND hwnd, int sx)
    {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const PreviewMetrics preview = GetPreviewMetrics(rc);
        const RECT scrollRc = GetPreviewScrollBarRect(rc);
        const RECT thumbRc = GetPreviewScrollThumbRect(scrollRc, preview);
        const int thumbW = MaxValue(1, thumbRc.right - thumbRc.left);
        const int trackW = MaxValue(1, scrollRc.right - scrollRc.left - 4);
        g_app.previewScrollDragStartMouseX = sx;
        g_app.previewScrollDragStartScrollX = ClampValue(g_app.previewScrollX, 0, preview.maxScrollX);
        g_app.previewScrollDragThumbRange = MaxValue(1, trackW - thumbW);
        g_app.previewScrollDragMaxScrollX = MaxValue(0, preview.maxScrollX);
    }

    void UpdatePreviewScrollFromSmoothDrag(HWND hwnd, int sx)
    {
        if (g_app.previewScrollDragMaxScrollX <= 0 || g_app.previewScrollDragThumbRange <= 0) return;
        const int dx = sx - g_app.previewScrollDragStartMouseX;
        const double scrollPerPixel = static_cast<double>(g_app.previewScrollDragMaxScrollX) / static_cast<double>(g_app.previewScrollDragThumbRange);
        const int newScroll = ClampValue(g_app.previewScrollDragStartScrollX + static_cast<int>(std::lround(static_cast<double>(dx) * scrollPerPixel)),
                                         0, g_app.previewScrollDragMaxScrollX);
        if (newScroll != g_app.previewScrollX)
        {
            g_app.previewScrollX = newScroll;
            InvalidateTexturePreviewOnly(hwnd);
        }
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

    std::string TrimTrailingSlashes(std::string path)
    {
        while (path.size() > 1 && (path.back() == '/' || path.back() == '\\'))
        {
            path.pop_back();
        }
        return path;
    }

    bool DirectoryExistsLocal(const std::string& path)
    {
        if (path.empty()) return false;
        const DWORD attributes = GetFileAttributesA(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
    }

    std::string BaseNameNoSlash(const std::string& path)
    {
        std::string trimmed = TrimTrailingSlashes(path);
        const size_t slash = trimmed.find_last_of("\\/");
        return slash == std::string::npos ? trimmed : trimmed.substr(slash + 1);
    }

    bool FileExistsLocal(const std::string& path)
    {
        if (path.empty()) return false;
        const DWORD attrs = GetFileAttributesA(path.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    std::string NormalizePathForProfileCompare(std::string path)
    {
        std::replace(path.begin(), path.end(), '\\', '/');
        return LowerAscii(path);
    }

    bool PathContainsFolderPair(const std::string& normalizedPath, const char* gameFolder, const char* mapFolder)
    {
        if (!gameFolder || !mapFolder) return false;
        const std::string needle = std::string("/") + LowerAscii(gameFolder) + "/" + LowerAscii(mapFolder) + "/";
        return normalizedPath.find(needle) != std::string::npos;
    }

    GameProfile MakeGameProfile(GameProfileKind kind, const std::string& rootPath)
    {
        GameProfile profile;
        profile.kind = kind;
        profile.rootPath = TrimTrailingSlashes(rootPath);
        switch (kind)
        {
        case GameProfileKind::Gloom:
            profile.title = L"Gloom";
            profile.mapFolder = "maps";
            profile.objectFolder = "objs";
            profile.imageFolder = "pics";
            profile.soundFolder = "sfxs";
            profile.scriptPath = "misc/script";
            break;
        case GameProfileKind::GloomDeluxe:
            profile.title = L"Gloom Deluxe";
            profile.mapFolder = "maps";
            profile.objectFolder = "objs";
            profile.imageFolder = "pics";
            profile.soundFolder = "sfxs";
            profile.scriptPath = "misc/script";
            break;
        case GameProfileKind::Gloom3:
            profile.title = L"Gloom 3";
            profile.mapFolder = "maps";
            profile.objectFolder = "objs";
            profile.imageFolder = "pics";
            profile.soundFolder = "sfxs";
            profile.scriptPath = "misc/script";
            break;
        case GameProfileKind::ZombieMassacre:
            profile.title = L"Zombie Massacre";
            profile.mapFolder = "lvls";
            profile.objectFolder = "char";
            profile.imageFolder = "pixs";
            profile.soundFolder = "musi";
            profile.scriptPath = "stuf/stages";
            break;
        case GameProfileKind::Unknown:
        default:
            profile.title = L"Unknown Game";
            break;
        }
        return profile;
    }

    GameProfile DetectGameProfileForMapPath(const std::string& path)
    {
        if (path.empty())
        {
            return MakeGameProfile(GameProfileKind::Unknown, std::string());
        }

        const std::string mapDir = DirName(path);
        const std::string projectDir = DirName(mapDir);
        const std::string normalized = NormalizePathForProfileCompare(path);
        const std::string mapDirName = LowerAscii(BaseNameNoSlash(mapDir));
        const bool mapLivesInKnownMapFolder = mapDirName == "maps" || mapDirName == "lvls";
        const std::string gameRoot = mapLivesInKnownMapFolder ? projectDir : mapDir;
        const std::string gameRootName = LowerAscii(BaseNameNoSlash(gameRoot));

        if (PathContainsFolderPair(normalized, "massacre", "lvls") ||
            mapDirName == "lvls" ||
            gameRootName == "massacre" ||
            DirectoryExistsLocal(JoinPath(gameRoot, "char")) ||
            FileExistsLocal(JoinPath(gameRoot, "stuf/stages")))
        {
            return MakeGameProfile(GameProfileKind::ZombieMassacre, gameRoot);
        }

        if (PathContainsFolderPair(normalized, "gloom3", "maps") ||
            gameRootName == "gloom3" ||
            DirectoryExistsLocal(JoinPath(gameRoot, "fonts")))
        {
            return MakeGameProfile(GameProfileKind::Gloom3, gameRoot);
        }

        if (PathContainsFolderPair(normalized, "deluxe", "maps") ||
            gameRootName == "deluxe" ||
            FileExistsLocal(JoinPath(gameRoot, "pics/blackmagic")))
        {
            return MakeGameProfile(GameProfileKind::GloomDeluxe, gameRoot);
        }

        if (PathContainsFolderPair(normalized, "gloom", "maps") ||
            gameRootName == "gloom" ||
            FileExistsLocal(JoinPath(gameRoot, "misc/script")))
        {
            return MakeGameProfile(GameProfileKind::Gloom, gameRoot);
        }

        return MakeGameProfile(GameProfileKind::Unknown, gameRoot);
    }

    void RefreshGameProfile()
    {
        const GameProfile previous = g_app.gameProfile;
        g_app.gameProfile = DetectGameProfileForMapPath(g_app.document.sourcePath);
        if (previous.kind != g_app.gameProfile.kind || previous.rootPath != g_app.gameProfile.rootPath)
        {
            g_app.objectPreviewCache.clear();
        }
        if (g_app.statusBar)
        {
            InvalidateRect(g_app.statusBar, nullptr, TRUE);
        }
    }

    std::string GetConfiguredTextureFolder()
    {
        const std::string configured = TrimTrailingSlashes(g_app.textureDataPath);
        if (configured.empty()) return std::string();
        if (LowerAscii(BaseNameNoSlash(configured)) == "txts")
        {
            return configured;
        }
        return JoinPath(configured, "txts");
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
        case InsertMode::EventTrigger: return L"Draw Event Zone";
        case InsertMode::ObjectSpawn: return L"Place Object";
        case InsertMode::PlayerStart: return L"Set Player Start";
        case InsertMode::LevelEnd: return L"Set Level End";
        case InsertMode::LinkEventToZone: return L"Link Event > Wall/Door";
        case InsertMode::LinkEventToSwitchTexture: return L"Link Event > Switch/Trigger";
        case InsertMode::LinkEventToEnemyObject: return L"Link Event > Enemy/Objects";
        case InsertMode::DeleteLinkEventToEnemyObject: return L"Delete Link Event";
        case InsertMode::LinkEventToRotateClockwise: return L"Link Event > Rotate CW";
        case InsertMode::LinkEventToRotateCounterClockwise: return L"Link Event > Rotate CCW";
        case InsertMode::LinkEventToMoveWallGroup: return L"Link Event > Move Wallblock";
        case InsertMode::SetTeleportTarget: return L"Set Teleport Target";
        default: return L"Select";
        }
    }

    void InvalidateInfoPanelPreservingWalkPreview(HWND hwnd)
    {
        if (!hwnd) return;

        // v37: The 3D Walk Preview must be visible immediately and reflect map
        // changes right away.  Older anti-flicker builds validated the preview
        // rectangle out of normal inspector repaints, which could leave it blank
        // until a texture/action forced a full redraw.
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    void InvalidateEditorViews()
    {
        if (g_app.canvas) InvalidateRect(g_app.canvas, nullptr, FALSE);
        if (g_app.infoPanel) InvalidateRect(g_app.infoPanel, nullptr, FALSE);
    }

    void InvalidateEditorViewsIncludingWalkPreview()
    {
        if (g_app.canvas) InvalidateRect(g_app.canvas, nullptr, FALSE);
        if (g_app.infoPanel) InvalidateRect(g_app.infoPanel, nullptr, FALSE);
    }

    bool HasMapContentForValidation()
    {
        if (!g_app.document.zones.empty())
        {
            return true;
        }
        for (const auto& script : g_app.document.events)
        {
            if (script.hasUnsupportedRaw || !script.commands.empty())
            {
                return true;
            }
        }
        return false;
    }

    void UpdateModeButtons()
    {
        if (!g_app.btnAddWall) return;
        SetWindowTextW(g_app.btnAddWall, g_app.insertMode == InsertMode::Wall ? L"Drawing Wall..." : L"Draw Wall");
        SetWindowTextW(g_app.btnAddMonster, g_app.insertMode == InsertMode::MonsterZone ? L"Drawing Monster Zone..." : L"Draw Monster Zone");
        SetWindowTextW(g_app.btnAddTrigger, g_app.insertMode == InsertMode::EventTrigger ? L"Drawing Event Zone..." : L"Draw Event Zone");
        if (g_app.btnPlaceEnemy) SetWindowTextW(g_app.btnPlaceEnemy, g_app.insertMode == InsertMode::ObjectSpawn && MonsterTypeCategory(g_app.placeObjectType) == L"Enemy / actor" ? L"Placing Enemy..." : L"Place Enemy");
        if (g_app.btnPlaceWeapon) SetWindowTextW(g_app.btnPlaceWeapon, g_app.insertMode == InsertMode::ObjectSpawn && IsWeaponObjectType(g_app.placeObjectType) ? L"Placing Weapon..." : L"Place Weapon");
        if (g_app.btnPlacePickup) SetWindowTextW(g_app.btnPlacePickup, g_app.insertMode == InsertMode::ObjectSpawn && MonsterTypeCategory(g_app.placeObjectType) == L"Pickup / item" && !IsWeaponObjectType(g_app.placeObjectType) ? L"Placing Pickup..." : L"Place Pickup");
        if (g_app.btnPlayerStart) SetWindowTextW(g_app.btnPlayerStart, g_app.insertMode == InsertMode::PlayerStart ? L"Click Player Start..." : L"Set Player Start");
        if (g_app.btnLevelEnd) SetWindowTextW(g_app.btnLevelEnd, g_app.insertMode == InsertMode::LevelEnd ? L"Drawing Level End..." : L"Set Level End");
        if (g_app.btnLinkEvent) SetWindowTextW(g_app.btnLinkEvent, g_app.insertMode == InsertMode::LinkEventToZone ? L"Click Wall/Door..." : L"Link Event > Wall/Door");
        if (g_app.btnLinkSwitchTexture) SetWindowTextW(g_app.btnLinkSwitchTexture, g_app.insertMode == InsertMode::LinkEventToSwitchTexture ? L"Click OFF Switch..." : L"Link Event > Switch/Trigger");
        if (g_app.btnLinkEnemyObjects) SetWindowTextW(g_app.btnLinkEnemyObjects, g_app.insertMode == InsertMode::LinkEventToEnemyObject ? L"Click Enemy/Object..." : L"Link Event > Enemy/Objects");
        if (g_app.btnLinkRotateCW) SetWindowTextW(g_app.btnLinkRotateCW, g_app.insertMode == InsertMode::LinkEventToRotateClockwise ? L"Click Rotate Target..." : L"Link Event > Rotate CW");
        if (g_app.btnLinkRotateCCW) SetWindowTextW(g_app.btnLinkRotateCCW, g_app.insertMode == InsertMode::LinkEventToRotateCounterClockwise ? L"Click Rotate Target..." : L"Link Event > Rotate CCW");
        if (g_app.btnLinkMoveWallGroup) SetWindowTextW(g_app.btnLinkMoveWallGroup,
            g_app.insertMode == InsertMode::LinkEventToMoveWallGroup
                ? (g_app.pendingMoveWallGroupTargetPlacementActive ? L"Place Move Target..." : L"Click Move Block...")
                : L"Link Event > Move Wallblock");
        if (g_app.btnDeleteLinkEvent)
        {
            SetWindowTextW(g_app.btnDeleteLinkEvent, L"Delete Link Event");
            EnableWindow(g_app.btnDeleteLinkEvent, CanDeleteSelectedEventLink() ? TRUE : FALSE);
        }
        if (g_app.btnSetTeleportTarget) SetWindowTextW(g_app.btnSetTeleportTarget,
            g_app.insertMode == InsertMode::SetTeleportTarget
                ? (g_app.teleportTargetAwaitDirection ? L"Enter to Fix Target..." : L"Click Teleport Target...")
                : L"Set Teleport Target");
        if (g_app.btnFlipDoorDirection) SetWindowTextW(g_app.btnFlipDoorDirection, IsZoneControlledByOpenDoor(g_app.selectedZone) ? L"Flip Door Direction" : L"Flip Line Direction");
        if (g_app.btnDelete) SetWindowTextW(g_app.btnDelete, IsSelectedTeleportTargetValid() ? L"Delete Teleport" : (IsSelectedMonsterSpawnValid() ? L"Delete Object" : L"Delete Zone"));
        if (g_app.btnValidateMap)
        {
            EnableWindow(g_app.btnValidateMap, HasMapContentForValidation() ? TRUE : FALSE);
        }
    }

    void ClearInsertMode()
    {
        g_app.insertMode = InsertMode::None;
        g_app.isDrawing = false;
        g_app.drawWallAngleLock = false;
        g_app.linkEventTriggerZone = -1;
        g_app.linkEventIndex = -1;
        ResetPendingMoveWallGroup();
        ResetPendingTeleportTarget();
        UpdateModeButtons();
        RefreshPreviewImage();
        if (g_app.mainWindow) RefreshStatus();
        InvalidateEditorViews();
    }

    void CancelCurrentTool()
    {
        bool changed = false;
        if (g_app.isDrawing)
        {
            g_app.isDrawing = false;
            if (GetCapture() == g_app.canvas) ReleaseCapture();
            changed = true;
        }
        if (g_app.isPanning)
        {
            g_app.isPanning = false;
            if (GetCapture() == g_app.canvas) ReleaseCapture();
            changed = true;
        }
        if (g_app.pendingMoveWallGroupTargetPlacementActive)
        {
            if (GetCapture() == g_app.canvas) ReleaseCapture();
            changed = true;
        }
        if (g_app.insertMode != InsertMode::None)
        {
            g_app.insertMode = InsertMode::None;
            changed = true;
        }
        g_app.drawWallAngleLock = false;
        g_app.drawWallLengthSnapLock = false;
        g_app.linkEventTriggerZone = -1;
        g_app.linkEventIndex = -1;
        ResetPendingMoveWallGroup();
        ResetPendingTeleportTarget();
        if (changed)
        {
            UpdateModeButtons();
            RefreshPreviewImage();
            RefreshStatus();
            InvalidateEditorViews();
        }
    }

    void SetInsertMode(InsertMode mode)
    {
        g_app.insertMode = (g_app.insertMode == mode) ? InsertMode::None : mode;
        if (g_app.insertMode != InsertMode::None)
        {
            ClearSelectedMonsterSpawn();
            g_app.selectedZone = -1;
        }
        g_app.isDrawing = false;
        g_app.isPanning = false;
        g_app.drawWallAngleLock = false;
        if (g_app.insertMode != InsertMode::LinkEventToZone)
        {
            g_app.linkEventTriggerZone = -1;
            g_app.linkEventIndex = -1;
        }
        ResetPendingTeleportTarget();
        UpdateModeButtons();
        RefreshPreviewImage();
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

        const std::string configuredTxtsDir = GetConfiguredTextureFolder();
        addCandidate(JoinPath(configuredTxtsDir, textureName));
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
        LoadGloomTexturePalette(raw, static_cast<size_t>(paletteOffset), palette);

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

    bool LoadFlatPreviewImage(const std::string& textureName, TexturePreviewImage& outImage)
    {
        outImage = {};
        outImage.name = textureName;
        if (textureName.empty())
        {
            outImage.error = "No floor/ceiling texture name assigned.";
            return false;
        }

        std::string resolvedPath;
        if (!ResolveTexturePath(textureName, resolvedPath))
        {
            outImage.error = "Floor/ceiling texture file not found in txts/.";
            return false;
        }

        std::string error;
        std::vector<uint8_t> raw;
        if (!LoadMaybeCrmLocal(resolvedPath, raw, error))
        {
            outImage.error = error;
            return false;
        }

        constexpr size_t kFlatSize = 128u * 128u;
        if (raw.size() < kFlatSize + 2u)
        {
            outImage.error = "Floor/ceiling texture is too small.";
            return false;
        }

        std::array<std::array<uint8_t, 3>, 256> palette{};
        const size_t paletteBytes = raw.size() - kFlatSize;
        const size_t paletteEntries = MinValue<size_t>(256u, paletteBytes / 2u);
        if (paletteEntries == 0)
        {
            outImage.error = "Floor/ceiling texture palette is missing.";
            return false;
        }

        for (size_t p = 0; p < paletteEntries; ++p)
        {
            const size_t off = kFlatSize + p * 2u;
            const uint16_t colval = ReadBE16Local(raw.data() + off);
            DecodeGloomPaletteWord(colval, palette[p][0], palette[p][1], palette[p][2]);
        }

        // Original Gloom floor/roof files are 128x128 texels followed by
        // packed 12-bit Amiga RGB palette words. The texel block is stored
        // column-major: data[x][y] = raw[y + x * 128].
        outImage.width = 128;
        outImage.height = 128;
        outImage.loadedPath = resolvedPath;
        outImage.pixels.assign(static_cast<size_t>(outImage.width) * static_cast<size_t>(outImage.height), 0xFF000000u);

        for (int y = 0; y < outImage.height; ++y)
        {
            for (int x = 0; x < outImage.width; ++x)
            {
                const uint8_t idx = raw[static_cast<size_t>(y) + static_cast<size_t>(x) * 128u];
                const auto& rgb = palette[MinValue<size_t>(idx, paletteEntries - 1u)];
                outImage.pixels[static_cast<size_t>(y) * static_cast<size_t>(outImage.width) + static_cast<size_t>(x)] =
                    0xFF000000u | (static_cast<uint32_t>(rgb[0]) << 16) | (static_cast<uint32_t>(rgb[1]) << 8) | static_cast<uint32_t>(rgb[2]);
            }
        }

        return true;
    }

    bool LoadSurfaceTexturePreviewImage(const std::string& textureName, TexturePreviewImage& outImage)
    {
        if (LoadFlatPreviewImage(textureName, outImage))
        {
            return true;
        }

        TexturePreviewImage wallFallback;
        if (LoadTexturePreviewImage(textureName, wallFallback))
        {
            outImage = wallFallback;
            return true;
        }

        return false;
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
        g_app.previewWheelRemainder = 0;
        g_app.previewTextureIndex = 0;
        g_app.previewTextureSlot = 0;
        g_app.previewTextureStrip = 0;
        g_app.previewAutoScrollPending = false;
        g_app.objectPreviewImage = {};

        SyncActiveWallTextureFromSelectedWall();
        const int activeSwitchTriggerZone = GetActiveSwitchTextureTriggerZone();
        if (activeSwitchTriggerZone >= 0)
        {
            SyncActiveSwitchTextureFromTriggerZone(activeSwitchTriggerZone);
        }

        if (IsSelectedMonsterSpawnValid())
        {
            if (const mapfmt::EventCommand* cmd = GetSelectedMonsterSpawnCommand())
            {
                LoadObjectPreviewImage(cmd->params[0], g_app.objectPreviewImage);
            }
            if (g_app.infoPanel)
            {
                UpdateInfoPanelScrollBar(g_app.infoPanel);
            }
            return;
        }

        if (g_app.insertMode == InsertMode::ObjectSpawn)
        {
            LoadObjectPreviewImage(g_app.placeObjectType, g_app.objectPreviewImage);
            if (g_app.infoPanel)
            {
                UpdateInfoPanelScrollBar(g_app.infoPanel);
            }
            return;
        }

        if (IsWallTexturePickerActive())
        {
            const int slot = ClampValue(g_app.activeWallTextureSlot, 0, static_cast<int>(mapfmt::MapDocument::kTextureSlotCount) - 1);
            const int strip = ClampValue(g_app.activeWallTextureStrip, 0, 19);
            g_app.activeWallTextureSlot = slot;
            g_app.activeWallTextureStrip = strip;
            g_app.previewTextureSlot = slot;
            g_app.previewTextureStrip = strip;
            g_app.previewTextureIndex = GetActiveWallTextureIndex();
            LoadTexturePreviewImage(g_app.document.textureNames[slot], g_app.previewImage);
            g_app.previewAutoScrollPending = true;
        }
        else if (g_app.selectedZone >= 0 && g_app.selectedZone < static_cast<int>(g_app.document.zones.size()))
        {
            const auto& zone = g_app.document.zones[g_app.selectedZone];
            if (IsLevelEndZone(zone) || IsEventTriggerLineZone(zone))
            {
                if (g_app.infoPanel)
                {
                    UpdateInfoPanelScrollBar(g_app.infoPanel);
                }
                return;
            }

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
        else if (IsSurfaceTexturePickerActive())
        {
            RefreshSurfaceTextureChoices();
            const int textureChoice = GetActiveSurfaceTextureChoice();
            const int strip = GetActiveSurfaceTextureIndex();
            g_app.previewTextureIndex = strip;
            g_app.previewTextureSlot = textureChoice;
            g_app.previewTextureStrip = strip;
            LoadSurfaceTexturePreviewImage(GetActiveSurfaceTextureName(), g_app.previewImage);
            if (g_app.previewImage.width > 0)
            {
                const int maxSurfaceStrip = MaxValue(0, (g_app.previewImage.width - 1) / 64);
                SetActiveSurfaceTextureIndex(ClampValue(GetActiveSurfaceTextureIndex(), 0, maxSurfaceStrip));
                g_app.previewTextureStrip = GetActiveSurfaceTextureIndex();
                g_app.previewTextureIndex = g_app.previewTextureStrip;
            }
            g_app.previewAutoScrollPending = true;
        }

        if (g_app.infoPanel)
        {
            AutoScrollPreviewToSelectedStrip();
            UpdateInfoPanelScrollBar(g_app.infoPanel);
        }
    }

    void RefreshWallTexturePickerPreviewFromActive()
    {
        if (!IsWallTexturePickerActive())
        {
            RefreshPreviewImage();
            return;
        }

        g_app.previewImage = {};
        g_app.previewScrollX = 0;
        g_app.previewWheelRemainder = 0;
        g_app.previewAutoScrollPending = false;

        const int slot = ClampValue(g_app.activeWallTextureSlot, 0, static_cast<int>(mapfmt::MapDocument::kTextureSlotCount) - 1);
        const int strip = ClampValue(g_app.activeWallTextureStrip, 0, 19);
        g_app.activeWallTextureSlot = slot;
        g_app.activeWallTextureStrip = strip;
        g_app.previewTextureSlot = slot;
        g_app.previewTextureStrip = strip;
        g_app.previewTextureIndex = GetActiveWallTextureIndex();
        LoadTexturePreviewImage(g_app.document.textureNames[slot], g_app.previewImage);
        g_app.previewAutoScrollPending = true;

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

    std::wstring GetFileNameForMenu(const std::string& path)
    {
        std::wstring wide = Utf8ToWide(path);
        const size_t slash = wide.find_last_of(L"\\/");
        std::wstring name = (slash == std::wstring::npos) ? wide : wide.substr(slash + 1);
        for (wchar_t& ch : name)
        {
            if (ch == L'&') ch = L'+';
        }
        return name.empty() ? L"(missing)" : name;
    }

    void LoadRecentFiles()
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\ZGloomEditor", 0, KEY_READ, &key) != ERROR_SUCCESS)
        {
            return;
        }

        for (int i = 0; i < kRecentFileCount; ++i)
        {
            std::wstringstream valueName;
            valueName << L"Recent" << i;
            wchar_t buffer[MAX_PATH * 4]{};
            DWORD type = REG_SZ;
            DWORD bytes = sizeof(buffer);
            if (RegQueryValueExW(key, valueName.str().c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &bytes) == ERROR_SUCCESS && type == REG_SZ)
            {
                g_app.recentFiles[i] = WideToUtf8(buffer);
            }
        }
        RegCloseKey(key);
    }

    void SaveRecentFiles()
    {
        HKEY key = nullptr;
        DWORD disposition = 0;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\ZGloomEditor", 0, nullptr, 0, KEY_WRITE, nullptr, &key, &disposition) != ERROR_SUCCESS)
        {
            return;
        }

        for (int i = 0; i < kRecentFileCount; ++i)
        {
            std::wstringstream valueName;
            valueName << L"Recent" << i;
            const std::wstring wide = Utf8ToWide(g_app.recentFiles[i]);
            RegSetValueExW(key, valueName.str().c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(wide.c_str()), static_cast<DWORD>((wide.size() + 1) * sizeof(wchar_t)));
        }
        RegCloseKey(key);
    }

    void LoadEditorSettings()
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\ZGloomEditor", 0, KEY_READ, &key) != ERROR_SUCCESS)
        {
            return;
        }

        wchar_t buffer[MAX_PATH * 4]{};
        DWORD type = REG_SZ;
        DWORD bytes = sizeof(buffer);
        if (RegQueryValueExW(key, L"TextureDataPath", nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &bytes) == ERROR_SUCCESS && type == REG_SZ)
        {
            g_app.textureDataPath = WideToUtf8(buffer);
        }
        RegCloseKey(key);
    }

    void SaveEditorSettings()
    {
        HKEY key = nullptr;
        DWORD disposition = 0;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\ZGloomEditor", 0, nullptr, 0, KEY_WRITE, nullptr, &key, &disposition) != ERROR_SUCCESS)
        {
            return;
        }

        const std::wstring wide = Utf8ToWide(g_app.textureDataPath);
        RegSetValueExW(key, L"TextureDataPath", 0, REG_SZ, reinterpret_cast<const BYTE*>(wide.c_str()), static_cast<DWORD>((wide.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }

    OwnerDrawMenuItem* MakeOwnerDrawMenuItem(const std::wstring& text, UINT_PTR id = 0, bool topLevel = false, bool separator = false)
    {
        auto item = std::make_unique<OwnerDrawMenuItem>();
        item->text = text;
        item->id = id;
        item->topLevel = topLevel;
        item->separator = separator;
        OwnerDrawMenuItem* raw = item.get();
        g_app.menuItems.push_back(std::move(item));
        return raw;
    }

    void AppendOwnerDrawMenuItem(HMENU menu, UINT_PTR id, const std::wstring& text, bool enabled = true)
    {
        UINT flags = MF_OWNERDRAW;
        if (!enabled)
        {
            flags |= MF_GRAYED;
        }
        AppendMenuW(menu, flags, id, reinterpret_cast<LPCWSTR>(MakeOwnerDrawMenuItem(text, id, false)));
    }

    void AppendOwnerDrawPopup(HMENU menuBar, HMENU popup, const std::wstring& text)
    {
        AppendMenuW(menuBar, MF_POPUP | MF_OWNERDRAW, reinterpret_cast<UINT_PTR>(popup), reinterpret_cast<LPCWSTR>(MakeOwnerDrawMenuItem(text, reinterpret_cast<UINT_PTR>(popup), true)));
    }

    void AppendOwnerDrawSeparator(HMENU menu)
    {
        AppendMenuW(menu, MF_OWNERDRAW | MF_SEPARATOR, 0, reinterpret_cast<LPCWSTR>(MakeOwnerDrawMenuItem(std::wstring(), 0, false, true)));
    }

    void ModifyOwnerDrawMenuItem(HMENU menu, UINT id, const std::wstring& text, bool enabled = true)
    {
        UINT flags = MF_BYCOMMAND | MF_OWNERDRAW;
        if (!enabled)
        {
            flags |= MF_GRAYED;
        }
        ModifyMenuW(menu, id, flags, id, reinterpret_cast<LPCWSTR>(MakeOwnerDrawMenuItem(text, id, false)));
    }

    void UpdateRecentFilesMenu()
    {
        if (!g_app.fileMenu) return;
        for (int i = 0; i < kRecentFileCount; ++i)
        {
            const UINT id = kRecentFileBaseId + static_cast<UINT>(i);
            if (g_app.recentFiles[i].empty())
            {
                std::wstringstream label;
                label << L"&" << (i + 1) << L" (empty)";
                ModifyOwnerDrawMenuItem(g_app.fileMenu, id, label.str(), false);
            }
            else
            {
                std::wstringstream label;
                label << L"&" << (i + 1) << L" " << GetFileNameForMenu(g_app.recentFiles[i]);
                ModifyOwnerDrawMenuItem(g_app.fileMenu, id, label.str(), true);
            }
        }
    }

    std::array<std::wstring, kRecentCampaignGameRootCount> LoadRecentCampaignGameRootsForMenu()
    {
        std::array<std::wstring, kRecentCampaignGameRootCount> roots{};
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\ZGloomEditor", 0, KEY_READ, &key) != ERROR_SUCCESS)
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

    std::wstring GetCampaignGameRootNameForMenu(const std::wstring& path)
    {
        std::wstring name = path;
        const size_t slash = name.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
            name = name.substr(slash + 1);
        for (wchar_t& ch : name)
        {
            if (ch == L'&') ch = L'+';
        }
        return name.empty() ? L"(missing)" : name;
    }

    void UpdateRecentCampaignGameRootsMenu()
    {
        if (!g_app.campaignMenu)
            return;

        const auto roots = LoadRecentCampaignGameRootsForMenu();
        for (int i = 0; i < kRecentCampaignGameRootCount; ++i)
        {
            const UINT id = kRecentCampaignGameRootBaseId + static_cast<UINT>(i);
            if (roots[static_cast<size_t>(i)].empty())
            {
                std::wstringstream label;
                label << L"&" << (i + 1) << L" (empty)";
                ModifyOwnerDrawMenuItem(g_app.campaignMenu, id, label.str(), false);
            }
            else
            {
                std::wstringstream label;
                label << L"&" << (i + 1) << L" " << GetCampaignGameRootNameForMenu(roots[static_cast<size_t>(i)]);
                ModifyOwnerDrawMenuItem(g_app.campaignMenu, id, label.str(), true);
            }
        }
    }

    void AddRecentFile(const std::string& path)
    {
        if (path.empty()) return;
        std::array<std::string, kRecentFileCount> next{};
        next[0] = path;
        int out = 1;
        for (const auto& item : g_app.recentFiles)
        {
            if (item.empty() || item == path) continue;
            if (out >= kRecentFileCount) break;
            next[out++] = item;
        }
        g_app.recentFiles = next;
        SaveRecentFiles();
        UpdateRecentFilesMenu();
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
        case mapfmt::ZoneType::MonsterZone: return L"Monster/Trigger Line";
        case mapfmt::ZoneType::EventTrigger: return L"Event Trigger";
        default: return L"Unknown";
        }
    }

    std::wstring ZoneRoleToText(const mapfmt::Zone& zone)
    {
        if (IsLevelEndZone(zone)) return L"Level End";
        if (IsEventTriggerLineZone(zone)) return L"Event Trigger";
        return ZoneTypeToText(zone.ztype);
    }

    std::wstring ZoneToListText(const mapfmt::Zone& zone, int index)
    {
        std::wstringstream ss;
        ss << L"[" << index << L"] " << ZoneRoleToText(zone)
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
               << L"  Type: " << ZoneRoleToText(zone)
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
        else if (g_app.insertMode == InsertMode::ObjectSpawn)
        {
            ss << L"  Click to place " << ObjectPlacementLabel(g_app.placeObjectType) << L" as a map-start object";
        }
        else if (g_app.insertMode == InsertMode::PlayerStart)
        {
            ss << L"  Click on the canvas to set Player 1 start";
        }
        else if (g_app.insertMode == InsertMode::LevelEnd)
        {
            ss << L"  Drag a crossing line to set the level end";
        }
        else if (g_app.insertMode == InsertMode::LinkEventToZone)
        {
            ss << L"  Click the wall/door zone that this trigger should open/move";
        }
        else if (g_app.insertMode == InsertMode::LinkEventToSwitchTexture)
        {
            ss << L"  Click the OFF switch/wall panel; ON uses the next texture frame";
        }
        else if (g_app.insertMode == InsertMode::LinkEventToEnemyObject)
        {
            ss << L"  Click an existing enemy/object/ammo marker to move it into this trigger event";
        }
        else if (g_app.insertMode == InsertMode::LinkEventToRotateClockwise ||
                 g_app.insertMode == InsertMode::LinkEventToRotateCounterClockwise)
        {
            ss << L"  Click the wall/poly group that should rotate";
        }
        else if (g_app.insertMode == InsertMode::LinkEventToMoveWallGroup)
        {
            if (g_app.pendingMoveWallGroupTargetPlacementActive)
            {
                ss << L"  Move the copied block on the 8/8 grid, then click to place its target guide";
            }
            else
            {
                ss << L"  Click one segment of the source wall block that should move";
            }
        }
        else if (g_app.insertMode == InsertMode::SetTeleportTarget)
        {
            if (g_app.teleportTargetAwaitDirection)
            {
                ss << L"  Left/Right rotates teleport facing; click away or press Enter to fix";
            }
            else
            {
                ss << L"  Click the destination point for this trigger teleport";
            }
        }
        else if (g_app.insertMode != InsertMode::None)
        {
            ss << L"  Click and drag on the canvas to draw";
        }

        if (g_app.showEventGraphOverlay)
        {
            ss << L"  Event Links Overlay: ON";
        }

        const std::wstring text = ss.str();
        g_app.statusText = text;
        RedrawWindow(g_app.statusBar, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE);
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
        if (g_app.selectedMonsterSpawn.IsSet() && !IsSelectedMonsterSpawnValid())
        {
            ClearSelectedMonsterSpawn();
        }
        if (g_app.selectedTeleportTarget.IsSet() && !IsSelectedTeleportTargetValid())
        {
            ClearSelectedTeleportTarget();
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

    void PushUndoSnapshot()
    {
        EditorSnapshot snap;
        snap.document = g_app.document;
        snap.selectedZone = g_app.selectedZone;
        if (g_app.undoStack.size() >= kMaxUndoSteps)
        {
            g_app.undoStack.erase(g_app.undoStack.begin());
        }
        g_app.undoStack.push_back(std::move(snap));
    }

    void ClearUndoHistory()
    {
        g_app.undoStack.clear();
    }

    void UndoLastChange()
    {
        if (g_app.undoStack.empty())
        {
            MessageBeep(MB_ICONINFORMATION);
            return;
        }
        EditorSnapshot snap = std::move(g_app.undoStack.back());
        g_app.undoStack.pop_back();
        g_app.document = std::move(snap.document);
        g_app.selectedZone = snap.selectedZone;
        ClearSelectedMonsterSpawn();
        ClearSelectedTeleportTarget();
        if (g_app.selectedZone >= static_cast<int>(g_app.document.zones.size()))
        {
            g_app.selectedZone = static_cast<int>(g_app.document.zones.size()) - 1;
        }
        g_app.insertMode = InsertMode::None;
        g_app.isDrawing = false;
        g_app.isPanning = false;
        g_app.drawWallAngleLock = false;
        g_app.document.dirty = true;
        UpdateModeButtons();
        RefreshZoneList();
        UpdateCanvasScrollBars(g_app.canvas);
        UpdateTitle();
    }

    bool IsEditorShortcutMessage(const MSG& msg)
    {
        if (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN)
        {
            return false;
        }
        const bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (ctrlDown && msg.wParam == 'Z')
        {
            UndoLastChange();
            return true;
        }
        if (ctrlDown && msg.wParam == 'O')
        {
            OpenDocument();
            return true;
        }
        if (ctrlDown && msg.wParam == 'S')
        {
            SaveDocument(false);
            return true;
        }
        if (!ctrlDown && msg.wParam == VK_LEFT && g_app.insertMode == InsertMode::SetTeleportTarget && g_app.teleportTargetAwaitDirection)
        {
            return RotatePendingTeleportTargetByDegrees(-5);
        }
        if (!ctrlDown && msg.wParam == VK_RIGHT && g_app.insertMode == InsertMode::SetTeleportTarget && g_app.teleportTargetAwaitDirection)
        {
            return RotatePendingTeleportTargetByDegrees(5);
        }
        if (!ctrlDown && msg.wParam == VK_RETURN && g_app.insertMode == InsertMode::SetTeleportTarget && g_app.teleportTargetAwaitDirection)
        {
            return CommitPendingTeleportTarget();
        }
        if (!ctrlDown && msg.wParam == VK_LEFT && IsSelectedTeleportTargetValid())
        {
            return RotateSelectedTeleportTargetByDegrees(-5);
        }
        if (!ctrlDown && msg.wParam == VK_RIGHT && IsSelectedTeleportTargetValid())
        {
            return RotateSelectedTeleportTargetByDegrees(5);
        }
        if (!ctrlDown && msg.wParam == VK_LEFT && IsSelectedMonsterSpawnValid())
        {
            return RotateSelectedMonsterSpawnByDegrees(-5);
        }
        if (!ctrlDown && msg.wParam == VK_RIGHT && IsSelectedMonsterSpawnValid())
        {
            return RotateSelectedMonsterSpawnByDegrees(5);
        }
        if (msg.wParam == VK_ESCAPE)
        {
            if (g_app.insertMode != InsertMode::None || g_app.isDrawing || g_app.isPanning)
            {
                CancelCurrentTool();
                return true;
            }
        }
        if (msg.wParam == VK_DELETE)
        {
            DeleteSelectedItem();
            return true;
        }
        return false;
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

        if (!ConfirmSaveWithValidationAndMaybeFixes(path))
        {
            return false;
        }

        const std::vector<uint8_t> moveWallGuideMask = BuildMoveWallGroupGuideMaskForDocument(g_app.document);
        for (int zoneIndex = 0; zoneIndex < static_cast<int>(g_app.document.zones.size()); ++zoneIndex)
        {
            auto& zone = g_app.document.zones[static_cast<size_t>(zoneIndex)];
            if (IsMoveWallGroupGuideZoneIndex(moveWallGuideMask, zoneIndex))
            {
                // Keep hidden Move-Wallblock target guides neutral while saving.
                // The normal wall texture-band update derives sc from length and
                // would otherwise undo Apply Safe Fixes by restoring sc=2/4/etc.
                MakeMoveWallGroupGuideAiNeutral(zone);
                continue;
            }
            UpdateWallTextureBandCountFromLength(zone);
        }

        std::string error;
        if (!g_app.document.SaveToFile(path, error))
        {
            MessageBoxW(g_app.mainWindow, Utf8ToWide(error).c_str(), L"Save Failed", MB_OK | MB_ICONERROR);
            return false;
        }


        g_app.document.sourcePath = path;
        g_app.document.dirty = false;
        RefreshGameProfile();
        AddRecentFile(path);
        UpdateTitle();
        RefreshStatus();
        return true;
    }

    void NewDocument()
    {
        if (!ConfirmDiscardChanges()) return;
        g_app.document.NewBlank();
        RefreshGameProfile();
        g_app.objectPreviewCache.clear();
        ClearUndoHistory();
        g_app.selectedZone = -1;
        ClearSelectedMonsterSpawn();
        ClearSelectedTeleportTarget();
        ClearPendingSwitchTextureChoice();
        g_app.previewTextureBand = 0;
        g_app.isDrawing = false;
        g_app.isPanning = false;
        g_app.drawWallAngleLock = false;
        g_app.walkPreviewInitialized = false;
        ClearInsertMode();
        FitViewToDocument();
        RefreshZoneList();
        InvalidateWalkPreview();
        UpdateTitle();
    }

    void CloseDocument()
    {
        // Same visible result as the program start: one clean, unsaved blank map.
        NewDocument();
    }

    bool OpenDocumentFromPath(const std::string& path)
    {
        if (path.empty()) return false;
        if (!ConfirmDiscardChanges()) return false;

        std::string error;
        if (!g_app.document.LoadFromFile(path, error))
        {
            MessageBoxW(g_app.mainWindow, Utf8ToWide(error).c_str(), L"Open Failed", MB_OK | MB_ICONERROR);
            return false;
        }

        RefreshGameProfile();
        g_app.objectPreviewCache.clear();
        ClearUndoHistory();
        g_app.selectedZone = g_app.document.zones.empty() ? -1 : 0;
        ClearSelectedMonsterSpawn();
        ClearSelectedTeleportTarget();
        ClearPendingSwitchTextureChoice();
        g_app.previewTextureBand = 0;
        g_app.isDrawing = false;
        g_app.isPanning = false;
        g_app.drawWallAngleLock = false;
        g_app.walkPreviewInitialized = false;
        ClearInsertMode();
        FitViewToDocument();
        RefreshZoneList();
        InvalidateWalkPreview();
        UpdateTitle();
        AddRecentFile(path);
        return true;
    }

    void OpenDocument()
    {
        const std::string path = GetOpenOrSavePath(g_app.mainWindow, false, L"Open Map");
        OpenDocumentFromPath(path);
    }

    void OpenRecentDocument(int recentIndex)
    {
        if (recentIndex < 0 || recentIndex >= kRecentFileCount) return;
        OpenDocumentFromPath(g_app.recentFiles[recentIndex]);
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

    void CenterDialogOnOwner(HWND dlg, HWND owner)
    {
        if (!dlg) return;

        RECT dlgRc{};
        GetWindowRect(dlg, &dlgRc);
        const int dlgW = dlgRc.right - dlgRc.left;
        const int dlgH = dlgRc.bottom - dlgRc.top;

        RECT baseRc{};
        if (owner && IsWindow(owner))
        {
            GetWindowRect(owner, &baseRc);
        }
        else
        {
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &baseRc, 0);
        }

        int x = baseRc.left + ((baseRc.right - baseRc.left) - dlgW) / 2;
        int y = baseRc.top + ((baseRc.bottom - baseRc.top) - dlgH) / 2;

        HMONITOR mon = MonitorFromRect(&baseRc, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(mon, &mi))
        {
            x = ClampValue(x, mi.rcWork.left, MaxValue(mi.rcWork.left, mi.rcWork.right - dlgW));
            y = ClampValue(y, mi.rcWork.top, MaxValue(mi.rcWork.top, mi.rcWork.bottom - dlgH));
        }

        SetWindowPos(dlg, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
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
            CenterDialogOnOwner(dlg, g_app.mainWindow);
            ApplyEditorDarkModeToWindowTree(dlg);
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
        case WM_DRAWITEM:
            return DrawDarkOwnerDrawControl(reinterpret_cast<DRAWITEMSTRUCT*>(lParam)) ? TRUE : FALSE;

        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            return HandleDarkCtlColor(msg, wParam);

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
            CenterDialogOnOwner(dlg, g_app.mainWindow);
            ApplyEditorDarkModeToWindowTree(dlg);

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
        case WM_DRAWITEM:
            return DrawDarkOwnerDrawControl(reinterpret_cast<DRAWITEMSTRUCT*>(lParam)) ? TRUE : FALSE;

        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            return HandleDarkCtlColor(msg, wParam);

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

    struct EditorSettingsDialogState
    {
        std::string textureDataPath;
        bool accepted = false;
    };

    bool BrowseForFolder(HWND owner, std::wstring& outPath)
    {
        OleInitialize(nullptr);
        wchar_t displayName[MAX_PATH]{};
        BROWSEINFOW bi{};
        bi.hwndOwner = owner;
        bi.pszDisplayName = displayName;
        bi.lpszTitle = L"Choose the Gloom data folder containing txts, or choose the txts folder itself.";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
        PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
        if (!pidl)
        {
            OleUninitialize();
            return false;
        }

        wchar_t path[MAX_PATH]{};
        const BOOL ok = SHGetPathFromIDListW(pidl, path);
        CoTaskMemFree(pidl);
        OleUninitialize();
        if (!ok || !path[0]) return false;
        outPath = path;
        return true;
    }

    void UpdateEditorSettingsStatus(HWND dlg, const EditorSettingsDialogState* state)
    {
        if (!state) return;
        const std::string configured = TrimTrailingSlashes(state->textureDataPath);
        std::string message;
        if (configured.empty())
        {
            message = "No folder set. New maps can only use textures found next to the map or editor.";
        }
        else
        {
            std::string txts = configured;
            if (LowerAscii(BaseNameNoSlash(txts)) != "txts")
            {
                txts = JoinPath(txts, "txts");
            }
            message = DirectoryExistsLocal(txts) ? ("OK: " + txts) : ("Warning: txts not found at " + txts);
        }
        SetWindowTextUtf8(GetDlgItem(dlg, IDC_SETTINGS_STATUS), message);
    }

    INT_PTR CALLBACK EditorSettingsDialogProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<EditorSettingsDialogState*>(GetWindowLongPtrW(dlg, DWLP_USER));
        if (msg == WM_INITDIALOG)
        {
            state = reinterpret_cast<EditorSettingsDialogState*>(lParam);
            SetWindowLongPtrW(dlg, DWLP_USER, lParam);
            CenterDialogOnOwner(dlg, g_app.mainWindow);
            HICON smallIcon = static_cast<HICON>(LoadImageW(g_app.instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR | LR_SHARED));
            HICON bigIcon = static_cast<HICON>(LoadImageW(g_app.instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR | LR_SHARED));
            if (!smallIcon) smallIcon = LoadIconW(g_app.instance, MAKEINTRESOURCEW(IDI_APP_ICON));
            if (!bigIcon) bigIcon = LoadIconW(g_app.instance, MAKEINTRESOURCEW(IDI_APP_ICON));
            SendMessageW(dlg, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
            SendMessageW(dlg, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon));
            ApplyEditorDarkModeToWindowTree(dlg);
            SetWindowTextUtf8(GetDlgItem(dlg, IDC_SETTINGS_TEXTURE_ROOT), state->textureDataPath);
            UpdateEditorSettingsStatus(dlg, state);
            return TRUE;
        }

        if (!state) return FALSE;

        switch (msg)
        {
        case WM_DRAWITEM:
            return DrawDarkOwnerDrawControl(reinterpret_cast<DRAWITEMSTRUCT*>(lParam)) ? TRUE : FALSE;

        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            return HandleDarkCtlColor(msg, wParam);

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IDC_SETTINGS_BROWSE:
            {
                std::wstring selected;
                if (BrowseForFolder(dlg, selected))
                {
                    state->textureDataPath = WideToUtf8(selected);
                    SetWindowTextUtf8(GetDlgItem(dlg, IDC_SETTINGS_TEXTURE_ROOT), state->textureDataPath);
                    UpdateEditorSettingsStatus(dlg, state);
                }
                return TRUE;
            }
            case IDC_SETTINGS_CLEAR:
                state->textureDataPath.clear();
                SetWindowTextW(GetDlgItem(dlg, IDC_SETTINGS_TEXTURE_ROOT), L"");
                UpdateEditorSettingsStatus(dlg, state);
                return TRUE;
            case IDC_SETTINGS_TEXTURE_ROOT:
                if (HIWORD(wParam) == EN_CHANGE)
                {
                    state->textureDataPath = WideToUtf8(GetWindowTextString(GetDlgItem(dlg, IDC_SETTINGS_TEXTURE_ROOT)));
                    UpdateEditorSettingsStatus(dlg, state);
                }
                return TRUE;
            case IDOK:
                state->textureDataPath = TrimTrailingSlashes(WideToUtf8(GetWindowTextString(GetDlgItem(dlg, IDC_SETTINGS_TEXTURE_ROOT))));
                state->accepted = true;
                EndDialog(dlg, IDOK);
                return TRUE;
            case IDCANCEL:
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
        return FALSE;
    }

    void ShowEditorSettingsDialog()
    {
        EditorSettingsDialogState state;
        state.textureDataPath = g_app.textureDataPath;
        if (DialogBoxParamW(g_app.instance, MAKEINTRESOURCEW(IDD_EDITORSETTINGS), g_app.mainWindow, EditorSettingsDialogProc, reinterpret_cast<LPARAM>(&state)) == IDOK && state.accepted)
        {
            g_app.textureDataPath = state.textureDataPath;
            SaveEditorSettings();
            g_app.objectPreviewCache.clear();
            RefreshPreviewImage();
            RefreshStatus();
            InvalidateEditorViews();
        }
    }

    struct ManualTextureAnimationRange
    {
        int firstStrip = 0;
        int frames = 0;
        uint16_t delay = 4;
        uint16_t current = 0;
    };

    struct TextureDialogState
    {
        std::array<std::string, mapfmt::MapDocument::kTextureSlotCount> names;
        std::vector<std::string> foundTextureNames;
        TexturePreviewImage selectedPreview;
        std::vector<std::string> checkedOrder;
        std::unordered_map<std::string, std::vector<ManualTextureAnimationRange>> manualAnimationsByTexture;
        std::string pendingManualAnimationTexture;
        std::array<bool, 20> pendingManualAnimationStrips{};
        std::string animationStatusMessage;
        int selectedListIndex = -1;
        int previewScrollX = 0;
        int previewMaxScrollX = 0;
        int previewWheelRemainder = 0;
        bool previewScrollDragging = false;
        bool previewScrollHover = false;
        int previewScrollDragOffsetX = 0;
        bool populating = false;
        bool textureSelectionTouched = false;
        bool accepted = false;
    };

    std::string NormalizeTextureNameForSlotCompare(std::string value)
    {
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r' || value.front() == '\n'))
        {
            value.erase(value.begin());
        }
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r' || value.back() == '\n'))
        {
            value.pop_back();
        }
        std::replace(value.begin(), value.end(), '\\', '/');
        const size_t slash = value.find_last_of('/');
        if (slash != std::string::npos)
        {
            value.erase(0, slash + 1);
        }
        const size_t dot = value.find_last_of('.');
        if (dot != std::string::npos)
        {
            value.erase(dot);
        }
        return LowerAscii(value);
    }

    bool TextureNameEqualsNoCase(const std::string& a, const std::string& b)
    {
        return NormalizeTextureNameForSlotCompare(a) == NormalizeTextureNameForSlotCompare(b);
    }

    bool TextureNameListContainsNoCase(const std::vector<std::string>& names, const std::string& value)
    {
        if (value.empty()) return false;
        return std::find_if(names.begin(), names.end(), [&](const std::string& existing)
        {
            return TextureNameEqualsNoCase(existing, value);
        }) != names.end();
    }

    bool TextureMapSlotsContainNoCase(const TextureDialogState* state, const std::string& value)
    {
        if (!state || value.empty()) return false;
        for (const auto& assigned : state->names)
        {
            if (!assigned.empty() && TextureNameEqualsNoCase(assigned, value)) return true;
        }
        return false;
    }

    std::string TextureDialogAnimationKey(const std::string& name)
    {
        return LowerAscii(name);
    }

    std::string GetTextureDialogSelectedTextureName(const TextureDialogState* state)
    {
        if (!state || state->selectedListIndex < 0 || state->selectedListIndex >= static_cast<int>(state->foundTextureNames.size()))
        {
            return std::string();
        }
        return state->foundTextureNames[state->selectedListIndex];
    }

    std::vector<ManualTextureAnimationRange>* GetManualAnimationRanges(TextureDialogState* state, const std::string& textureName)
    {
        if (!state || textureName.empty()) return nullptr;
        return &state->manualAnimationsByTexture[TextureDialogAnimationKey(textureName)];
    }

    const std::vector<ManualTextureAnimationRange>* FindManualAnimationRanges(const TextureDialogState* state, const std::string& textureName)
    {
        if (!state || textureName.empty()) return nullptr;
        const auto it = state->manualAnimationsByTexture.find(TextureDialogAnimationKey(textureName));
        return it == state->manualAnimationsByTexture.end() ? nullptr : &it->second;
    }

    bool ManualAnimationContainsStrip(const ManualTextureAnimationRange& range, int strip)
    {
        return range.frames > 0 && strip >= range.firstStrip && strip < range.firstStrip + range.frames;
    }

    bool TextureDialogPendingAppliesToTexture(const TextureDialogState* state, const std::string& textureName)
    {
        return state && !textureName.empty() && TextureNameEqualsNoCase(state->pendingManualAnimationTexture, textureName);
    }

    bool TextureDialogPendingStripSelected(const TextureDialogState* state, const std::string& textureName, int strip)
    {
        return TextureDialogPendingAppliesToTexture(state, textureName) && strip >= 0 && strip < 20 && state->pendingManualAnimationStrips[strip];
    }

    std::vector<int> CollectTextureDialogPendingStrips(const TextureDialogState* state)
    {
        std::vector<int> strips;
        if (!state) return strips;
        for (int i = 0; i < 20; ++i)
        {
            if (state->pendingManualAnimationStrips[i]) strips.push_back(i);
        }
        return strips;
    }

    bool TextureDialogStripsFormContiguousAnimation(const std::vector<int>& strips, int& outFirstStrip, int& outFrames)
    {
        outFirstStrip = -1;
        outFrames = 0;
        if (strips.size() < 2 || strips.size() > 4) return false;

        std::vector<int> sorted = strips;
        std::sort(sorted.begin(), sorted.end());
        sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
        if (sorted.size() != strips.size()) return false;

        for (size_t i = 1; i < sorted.size(); ++i)
        {
            if (sorted[i] != sorted[i - 1] + 1) return false;
        }

        outFirstStrip = sorted.front();
        outFrames = static_cast<int>(sorted.size());
        return true;
    }

    std::vector<ManualTextureAnimationRange> SplitManualAnimationRangeIntoGroups(int firstStrip, int frames, uint16_t delay = 4, uint16_t current = 0)
    {
        std::vector<ManualTextureAnimationRange> result;
        firstStrip = ClampValue(firstStrip, 0, 19);
        frames = ClampValue(frames, 0, 20 - firstStrip);

        int offset = 0;
        while (frames - offset >= 2)
        {
            const int remaining = frames - offset;
            int chunk = 0;
            if (remaining <= 4)
            {
                chunk = remaining;
            }
            else if ((remaining % 4) == 1)
            {
                // Avoid a trailing one-frame 'animation': 5 -> 3+2, 9 -> 3+4+2.
                chunk = 3;
            }
            else
            {
                chunk = 4;
            }

            ManualTextureAnimationRange range{};
            range.firstStrip = firstStrip + offset;
            range.frames = chunk;
            range.delay = delay > 0 ? delay : 4;
            range.current = current;
            result.push_back(range);
            offset += chunk;
        }
        return result;
    }

    std::string FormatStripSelectionList(const std::vector<int>& strips)
    {
        if (strips.empty()) return std::string();
        std::vector<int> sorted = strips;
        std::sort(sorted.begin(), sorted.end());
        sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

        std::ostringstream ss;
        for (size_t i = 0; i < sorted.size(); ++i)
        {
            if (i > 0) ss << ", ";
            ss << (sorted[i] + 1);
        }
        return ss.str();
    }

    void SetTextureDialogPendingStrips(TextureDialogState* state, const std::string& textureName, const std::vector<int>& strips)
    {
        if (!state) return;
        state->pendingManualAnimationTexture = textureName;
        state->pendingManualAnimationStrips.fill(false);
        for (int strip : strips)
        {
            if (strip >= 0 && strip < 20) state->pendingManualAnimationStrips[strip] = true;
        }
    }

    const ManualTextureAnimationRange* FindManualAnimationRangeAtStrip(const TextureDialogState* state, const std::string& textureName, int strip)
    {
        if (const auto* ranges = FindManualAnimationRanges(state, textureName))
        {
            for (const auto& range : *ranges)
            {
                if (ManualAnimationContainsStrip(range, strip)) return &range;
            }
        }
        return nullptr;
    }

    void AddOrReplaceManualAnimationRange(TextureDialogState* state, const std::string& textureName, int firstStrip, int frames)
    {
        auto* ranges = GetManualAnimationRanges(state, textureName);
        if (!ranges) return;

        firstStrip = ClampValue(firstStrip, 0, 19);
        frames = ClampValue(frames, 2, 4);
        if (firstStrip + frames > 20)
        {
            firstStrip = MaxValue(0, 20 - frames);
        }

        ranges->erase(std::remove_if(ranges->begin(), ranges->end(), [&](const ManualTextureAnimationRange& existing)
        {
            const int a0 = existing.firstStrip;
            const int a1 = existing.firstStrip + existing.frames - 1;
            const int b0 = firstStrip;
            const int b1 = firstStrip + frames - 1;
            return !(a1 < b0 || b1 < a0);
        }), ranges->end());

        ManualTextureAnimationRange range{};
        range.firstStrip = firstStrip;
        range.frames = frames;
        range.delay = 4;
        range.current = 0;
        ranges->push_back(range);
        std::sort(ranges->begin(), ranges->end(), [](const ManualTextureAnimationRange& a, const ManualTextureAnimationRange& b)
        {
            return a.firstStrip < b.firstStrip;
        });
    }

    bool RemoveManualAnimationRangeAtStrip(TextureDialogState* state, const std::string& textureName, int strip)
    {
        auto* ranges = GetManualAnimationRanges(state, textureName);
        if (!ranges) return false;
        const size_t oldSize = ranges->size();
        ranges->erase(std::remove_if(ranges->begin(), ranges->end(), [&](const ManualTextureAnimationRange& range)
        {
            return ManualAnimationContainsStrip(range, strip);
        }), ranges->end());
        return oldSize != ranges->size();
    }

    void CancelTextureDialogPendingAnimation(TextureDialogState* state)
    {
        if (!state) return;
        state->pendingManualAnimationTexture.clear();
        state->pendingManualAnimationStrips.fill(false);
    }

    bool TextureDialogTextureIsAssigned(const TextureDialogState* state, const std::string& textureName)
    {
        return state ? TextureNameListContainsNoCase(state->checkedOrder, textureName) : false;
    }

    void ImportTextureDialogManualAnimationsFromDocument(TextureDialogState& state)
    {
        state.manualAnimationsByTexture.clear();
        for (const auto& anim : g_app.document.animations)
        {
            if (anim.frames < 2 || anim.first >= 160 || anim.first + anim.frames > 160) continue;
            const int slot = ClampValue(static_cast<int>(anim.first / 20), 0, static_cast<int>(mapfmt::MapDocument::kTextureSlotCount) - 1);
            const int firstStrip = ClampValue(static_cast<int>(anim.first % 20), 0, 19);
            if (firstStrip + static_cast<int>(anim.frames) > 20) continue;
            const std::string textureName = state.names[slot];
            if (textureName.empty()) continue;

            auto& ranges = state.manualAnimationsByTexture[TextureDialogAnimationKey(textureName)];
            for (const auto& range : SplitManualAnimationRangeIntoGroups(firstStrip, static_cast<int>(anim.frames), anim.delay, anim.current))
            {
                const bool exists = std::any_of(ranges.begin(), ranges.end(), [&](const ManualTextureAnimationRange& existing)
                {
                    return existing.firstStrip == range.firstStrip && existing.frames == range.frames;
                });
                if (!exists) ranges.push_back(range);
            }
        }
    }

    void ApplyTextureDialogManualAnimationsToDocument(const TextureDialogState& state)
    {
        g_app.document.animations.clear();
        for (int slot = 0; slot < static_cast<int>(mapfmt::MapDocument::kTextureSlotCount); ++slot)
        {
            const std::string textureName = g_app.document.textureNames[slot];
            if (textureName.empty()) continue;
            const auto it = state.manualAnimationsByTexture.find(TextureDialogAnimationKey(textureName));
            if (it == state.manualAnimationsByTexture.end()) continue;

            for (const auto& range : it->second)
            {
                if (range.frames < 2 || range.firstStrip < 0 || range.firstStrip + range.frames > 20) continue;
                for (const auto& group : SplitManualAnimationRangeIntoGroups(range.firstStrip, range.frames, range.delay, range.current))
                {
                    mapfmt::AnimationEntry entry{};
                    entry.frames = static_cast<uint16_t>(group.frames);
                    entry.first = static_cast<uint16_t>(slot * 20 + group.firstStrip);
                    entry.delay = group.delay > 0 ? group.delay : 4;
                    entry.current = group.current;
                    g_app.document.animations.push_back(entry);
                }
            }
        }
        RebuildAnimationBlockFromDocumentAnimations();
    }

    void EnsureAssignedTexturesInFoundList(TextureDialogState& state)
    {
        for (const auto& assigned : state.names)
        {
            if (!assigned.empty() && !TextureNameListContainsNoCase(state.foundTextureNames, assigned))
            {
                state.foundTextureNames.push_back(assigned);
            }
        }
    }

    int CountCheckedTextureListItems(HWND list)
    {
        if (!list) return 0;
        int checked = 0;
        const int count = ListView_GetItemCount(list);
        for (int i = 0; i < count; ++i)
        {
            if (ListView_GetCheckState(list, i)) ++checked;
        }
        return checked;
    }

    int TextureDialogAssignedSlotForName(const TextureDialogState* state, const std::string& value)
    {
        if (!state || value.empty()) return -1;
        for (int i = 0; i < static_cast<int>(mapfmt::MapDocument::kTextureSlotCount); ++i)
        {
            if (!state->names[i].empty() && TextureNameEqualsNoCase(state->names[i], value))
            {
                return i;
            }
        }
        return -1;
    }

    void RemoveTextureDialogCheckedOrder(TextureDialogState* state, const std::string& value)
    {
        if (!state || value.empty()) return;
        state->checkedOrder.erase(std::remove_if(state->checkedOrder.begin(), state->checkedOrder.end(), [&](const std::string& existing)
        {
            return TextureNameEqualsNoCase(existing, value);
        }), state->checkedOrder.end());
    }

    void EnsureTextureDialogCheckedOrder(TextureDialogState* state, const std::string& value)
    {
        if (!state || value.empty()) return;
        if (!TextureNameListContainsNoCase(state->checkedOrder, value))
        {
            state->checkedOrder.push_back(value);
        }
    }

    std::vector<std::string> BuildTextureDialogCheckedOrder(HWND list, TextureDialogState* state)
    {
        std::vector<std::string> ordered;
        if (!list || !state) return ordered;

        auto isCurrentlyChecked = [&](const std::string& name) -> bool
        {
            const int count = ListView_GetItemCount(list);
            for (int i = 0; i < count && i < static_cast<int>(state->foundTextureNames.size()); ++i)
            {
                if (TextureNameEqualsNoCase(state->foundTextureNames[i], name))
                {
                    return ListView_GetCheckState(list, i) ? true : false;
                }
            }
            return false;
        };

        for (const auto& name : state->checkedOrder)
        {
            if (!name.empty() && isCurrentlyChecked(name) && !TextureNameListContainsNoCase(ordered, name))
            {
                ordered.push_back(name);
            }
        }

        const int count = ListView_GetItemCount(list);
        for (int i = 0; i < count && i < static_cast<int>(state->foundTextureNames.size()); ++i)
        {
            if (ListView_GetCheckState(list, i) && !TextureNameListContainsNoCase(ordered, state->foundTextureNames[i]))
            {
                ordered.push_back(state->foundTextureNames[i]);
            }
        }
        return ordered;
    }

    void SyncTextureDialogAssignedSlotsFromList(HWND dlg, TextureDialogState* state)
    {
        HWND list = GetDlgItem(dlg, IDC_TEX_FOUND_LIST);
        if (!list || !state) return;

        std::vector<std::string> checked = BuildTextureDialogCheckedOrder(list, state);

        for (int slot = 0; slot < static_cast<int>(mapfmt::MapDocument::kTextureSlotCount); ++slot)
        {
            if (!state->names[slot].empty() && !TextureNameListContainsNoCase(checked, state->names[slot]))
            {
                state->names[slot].clear();
            }
        }

        for (const auto& name : checked)
        {
            if (name.empty() || TextureMapSlotsContainNoCase(state, name)) continue;
            for (int slot = 0; slot < static_cast<int>(mapfmt::MapDocument::kTextureSlotCount); ++slot)
            {
                if (state->names[slot].empty())
                {
                    state->names[slot] = name;
                    break;
                }
            }
        }

        state->checkedOrder = checked;
    }

    void UpdateTextureDialogBankColumn(HWND dlg, TextureDialogState* state)
    {
        HWND list = GetDlgItem(dlg, IDC_TEX_FOUND_LIST);
        if (!list || !state) return;

        // Display only: do not derive/clear slot assignments here. The list view may
        // briefly report stale checkbox states during init/refresh, and mutating the
        // T0-T7 model from this paint/update path can wipe existing map slots.
        const int count = ListView_GetItemCount(list);
        for (int i = 0; i < count && i < static_cast<int>(state->foundTextureNames.size()); ++i)
        {
            std::wstring bank;
            const int slot = TextureDialogAssignedSlotForName(state, state->foundTextureNames[i]);
            if (slot >= 0)
            {
                std::wstringstream ss;
                ss << L"T" << slot;
                bank = ss.str();
            }
            ListView_SetItemText(list, i, 1, const_cast<LPWSTR>(bank.c_str()));
        }
    }

    void RefreshTextureFoundStatus(HWND dlg, const TextureDialogState* state)
    {
        HWND list = GetDlgItem(dlg, IDC_TEX_FOUND_LIST);
        const int checked = CountCheckedTextureListItems(list);
        std::string message;
        if (!state || state->foundTextureNames.empty())
        {
            message = "No wall textures found. Check File > Editor Settings and point it to the game data/txts folder.";
        }
        else
        {
            std::ostringstream ss;
            ss << state->foundTextureNames.size() << " texture files found. Tick up to 8 files for this map. "
               << checked << "/8 currently assigned. ";
            if (!state->animationStatusMessage.empty())
            {
                ss << state->animationStatusMessage;
            }
            else
            {
                ss << "Preview animation: tick each frame below. Long neighbouring runs are saved as groups of max. 4 frames. Right-click a marked range to remove it.";
            }
            message = ss.str();
        }
        SetWindowTextUtf8(GetDlgItem(dlg, IDC_TEX_FOUND_STATUS), message);
    }

    int TextureDialogPreviewViewportWidth(HWND dlg)
    {
        HWND preview = GetDlgItem(dlg, IDC_TEX_PREVIEW);
        if (!preview) return 0;
        RECT rc{};
        GetClientRect(preview, &rc);
        return MaxValue(0, (rc.right - rc.left) - 4);
    }

    int TextureDialogPreviewScaledWidth(HWND dlg, const TexturePreviewImage& image)
    {
        HWND preview = GetDlgItem(dlg, IDC_TEX_PREVIEW);
        if (!preview || image.width <= 0 || image.height <= 0) return 0;
        RECT rc{};
        GetClientRect(preview, &rc);
        const int viewportH = MaxValue(1, (rc.bottom - rc.top) - 4);
        const double scale = MaxValue(1.0, static_cast<double>(viewportH) / static_cast<double>(image.height));
        return MaxValue(1, static_cast<int>(std::lround(static_cast<double>(image.width) * scale)));
    }

    void UpdateTextureDialogPreviewScrollbar(HWND dlg, TextureDialogState* state)
    {
        HWND scroll = GetDlgItem(dlg, IDC_TEX_PREVIEW_SCROLL);
        if (!scroll || !state) return;

        const int viewportW = TextureDialogPreviewViewportWidth(dlg);
        const int scaledW = TextureDialogPreviewScaledWidth(dlg, state->selectedPreview);
        state->previewMaxScrollX = MaxValue(0, scaledW - MaxValue(1, viewportW));
        state->previewScrollX = ClampValue(state->previewScrollX, 0, state->previewMaxScrollX);

        EnableWindow(scroll, state->previewMaxScrollX > 0 ? TRUE : FALSE);
        InvalidateRect(scroll, nullptr, TRUE);
    }

    RECT TextureDialogPreviewScrollThumbRect(HWND scroll, const TextureDialogState* state)
    {
        RECT rc{};
        GetClientRect(scroll, &rc);
        if (!state || state->previewMaxScrollX <= 0)
        {
            return RECT{ rc.left, rc.top, rc.left, rc.top };
        }

        HWND dlg = GetParent(scroll);
        const int viewportW = MaxValue(1, TextureDialogPreviewViewportWidth(dlg));
        const int scaledW = MaxValue(viewportW, TextureDialogPreviewScaledWidth(dlg, state->selectedPreview));
        const int trackW = MaxValue(1, rc.right - rc.left - 4);
        const int thumbW = ClampValue(static_cast<int>(std::lround(static_cast<double>(trackW) * static_cast<double>(viewportW) / MaxValue(1.0, static_cast<double>(scaledW)))), 24, trackW);
        const int thumbRange = MaxValue(0, trackW - thumbW);
        const int thumbX = rc.left + 2 + (state->previewMaxScrollX > 0 ? static_cast<int>(std::lround(static_cast<double>(thumbRange) * static_cast<double>(state->previewScrollX) / static_cast<double>(state->previewMaxScrollX))) : 0);
        return RECT{ thumbX, rc.top + 2, thumbX + thumbW, rc.bottom - 2 };
    }

    void SetTextureDialogPreviewScrollFromX(HWND scroll, TextureDialogState* state, int sx, int thumbOffsetX)
    {
        if (!scroll || !state || state->previewMaxScrollX <= 0) return;

        RECT rc{};
        GetClientRect(scroll, &rc);
        const RECT thumbRc = TextureDialogPreviewScrollThumbRect(scroll, state);
        const int thumbW = MaxValue(1, thumbRc.right - thumbRc.left);
        const int trackW = MaxValue(1, rc.right - rc.left - 4);
        const int thumbRange = MaxValue(1, trackW - thumbW);
        const int targetX = ClampValue(sx - (rc.left + 2) - thumbOffsetX, 0, thumbRange);
        state->previewScrollX = ClampValue(static_cast<int>(std::lround(static_cast<double>(state->previewMaxScrollX) * static_cast<double>(targetX) / static_cast<double>(thumbRange))), 0, state->previewMaxScrollX);

        HWND dlg = GetParent(scroll);
        InvalidateRect(GetDlgItem(dlg, IDC_TEX_PREVIEW), nullptr, TRUE);
        InvalidateRect(scroll, nullptr, TRUE);
    }

    LRESULT CALLBACK TextureDialogPreviewScrollProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR refData)
    {
        UNREFERENCED_PARAMETER(wParam);
        UNREFERENCED_PARAMETER(subclassId);
        auto* state = reinterpret_cast<TextureDialogState*>(refData);
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
            FillRect(hdc, &rc, DarkWindowBrush());
            if (state && state->previewMaxScrollX > 0)
            {
                const RECT thumbRc = TextureDialogPreviewScrollThumbRect(hwnd, state);
                DrawSlimHorizontalScrollBar(hdc, rc, thumbRc, state->previewScrollHover, state->previewScrollDragging);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN:
            if (state && state->previewMaxScrollX > 0)
            {
                const int sx = GET_X_LPARAM(lParam);
                const int sy = GET_Y_LPARAM(lParam);
                RECT rc{};
                GetClientRect(hwnd, &rc);
                if (sx >= rc.left && sx < rc.right && sy >= rc.top && sy < rc.bottom)
                {
                    const RECT thumbRc = TextureDialogPreviewScrollThumbRect(hwnd, state);
                    if (sx >= thumbRc.left && sx < thumbRc.right)
                    {
                        state->previewScrollDragOffsetX = sx - thumbRc.left;
                    }
                    else
                    {
                        state->previewScrollDragOffsetX = MaxValue(1, thumbRc.right - thumbRc.left) / 2;
                        SetTextureDialogPreviewScrollFromX(hwnd, state, sx, state->previewScrollDragOffsetX);
                    }
                    state->previewScrollDragging = true;
                    state->previewScrollHover = true;
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                }
            }
            break;

        case WM_MOUSEMOVE:
            if (state)
            {
                if (!state->previewScrollHover)
                {
                    TRACKMOUSEEVENT tme{};
                    tme.cbSize = sizeof(tme);
                    tme.dwFlags = TME_LEAVE;
                    tme.hwndTrack = hwnd;
                    TrackMouseEvent(&tme);
                    state->previewScrollHover = true;
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
                if (state->previewScrollDragging)
                {
                    SetTextureDialogPreviewScrollFromX(hwnd, state, GET_X_LPARAM(lParam), state->previewScrollDragOffsetX);
                    return 0;
                }
            }
            break;

        case WM_LBUTTONUP:
            if (state && state->previewScrollDragging)
            {
                state->previewScrollDragging = false;
                if (GetCapture() == hwnd) ReleaseCapture();
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
            break;

        case WM_MOUSELEAVE:
            if (state && !state->previewScrollDragging)
            {
                state->previewScrollHover = false;
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            break;

        case WM_CAPTURECHANGED:
            if (state && reinterpret_cast<HWND>(lParam) != hwnd)
            {
                state->previewScrollDragging = false;
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            break;

        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, TextureDialogPreviewScrollProc, subclassId);
            break;
        }
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

    bool TextureDialogPreviewStripFromPoint(HWND preview, TextureDialogState* state, int sx, int sy, int& outStrip)
    {
        outStrip = -1;
        if (!preview || !state || state->selectedPreview.width <= 0 || state->selectedPreview.height <= 0) return false;

        RECT rc{};
        GetClientRect(preview, &rc);
        RECT inner = rc;
        InflateRect(&inner, -2, -2);
        if (sx < inner.left || sx >= inner.right || sy < inner.top || sy >= inner.bottom) return false;

        const int viewportW = MaxValue(1, inner.right - inner.left);
        const int viewportH = MaxValue(1, inner.bottom - inner.top);
        const double scale = MaxValue(1.0, static_cast<double>(viewportH) / static_cast<double>(state->selectedPreview.height));
        const int scaledW = MaxValue(1, static_cast<int>(std::lround(static_cast<double>(state->selectedPreview.width) * scale)));
        const int scrollX = ClampValue(state->previewScrollX, 0, MaxValue(0, scaledW - viewportW));
        const int drawX = inner.left - scrollX;
        const int imageX = static_cast<int>(std::floor(static_cast<double>(sx - drawX) / scale));
        if (imageX < 0 || imageX >= state->selectedPreview.width) return false;

        const int stripCount = ClampValue(state->selectedPreview.width / 64, 0, 20);
        if (stripCount <= 0) return false;
        outStrip = ClampValue(imageX / 64, 0, stripCount - 1);
        return true;
    }

    void DrawTextureDialogAnimationOverlay(HDC hdc, const TextureDialogState* state, const RECT& inner, int drawX, int drawY, double scale, int scaledH)
    {
        UNREFERENCED_PARAMETER(drawY);
        UNREFERENCED_PARAMETER(scaledH);
        if (!state || state->selectedPreview.width <= 0) return;
        const std::string textureName = GetTextureDialogSelectedTextureName(state);
        if (textureName.empty()) return;

        const int stripCount = ClampValue(state->selectedPreview.width / 64, 0, 20);
        if (stripCount <= 0) return;

        HPEN dividerPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
        HPEN animPen = CreatePen(PS_SOLID, 2, RGB(255, 220, 120));
        HPEN pendingPen = CreatePen(PS_SOLID, 2, RGB(105, 170, 255));
        HBRUSH animBrush = CreateSolidBrush(RGB(70, 58, 30));
        HBRUSH pendingBrush = CreateSolidBrush(RGB(35, 58, 85));
        HBRUSH emptyBrush = CreateSolidBrush(RGB(28, 28, 32));
        HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(hdc, emptyBrush));
        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(hdc, dividerPen));

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT)));

        const auto* ranges = FindManualAnimationRanges(state, textureName);
        const bool pendingForTexture = TextureDialogPendingAppliesToTexture(state, textureName);

        for (int strip = 0; strip < stripCount; ++strip)
        {
            const int left = drawX + static_cast<int>(std::lround(static_cast<double>(strip * 64) * scale));
            const int right = drawX + static_cast<int>(std::lround(static_cast<double>((strip + 1) * 64) * scale));
            if (right < inner.left || left > inner.right) continue;

            SelectObject(hdc, dividerPen);
            MoveToEx(hdc, left, inner.top, nullptr);
            LineTo(hdc, left, inner.bottom);

            bool inManualRange = false;
            bool isRangeEdge = false;
            if (ranges)
            {
                for (const auto& range : *ranges)
                {
                    if (ManualAnimationContainsStrip(range, strip))
                    {
                        inManualRange = true;
                        isRangeEdge = strip == range.firstStrip || strip == (range.firstStrip + range.frames - 1);
                        break;
                    }
                }
            }
            const bool isPendingStrip = pendingForTexture && TextureDialogPendingStripSelected(state, textureName, strip);

            RECT box{ MaxValue(inner.left + 3, left + 4), inner.bottom - 18, MinValue(inner.right - 3, left + 18), inner.bottom - 4 };
            if (box.right <= box.left + 4) continue;

            SelectObject(hdc, isPendingStrip ? pendingBrush : (inManualRange ? animBrush : emptyBrush));
            SelectObject(hdc, isPendingStrip ? pendingPen : (inManualRange ? animPen : dividerPen));
            Rectangle(hdc, box.left, box.top, box.right, box.bottom);

            if (isPendingStrip || inManualRange || isRangeEdge)
            {
                MoveToEx(hdc, box.left + 3, box.top + 7, nullptr);
                LineTo(hdc, box.left + 6, box.bottom - 3);
                LineTo(hdc, box.right - 3, box.top + 3);
            }

            RECT numRc{ left + 22, inner.bottom - 18, MinValue(right - 2, inner.right), inner.bottom - 3 };
            if (numRc.right > numRc.left + 10)
            {
                std::wstring label = std::to_wstring(strip + 1);
                SetTextColor(hdc, inManualRange || isPendingStrip ? RGB(255, 245, 190) : RGB(210, 210, 218));
                DrawTextW(hdc, label.c_str(), -1, &numRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
        }

        SelectObject(hdc, oldFont);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(dividerPen);
        DeleteObject(animPen);
        DeleteObject(pendingPen);
        DeleteObject(animBrush);
        DeleteObject(pendingBrush);
        DeleteObject(emptyBrush);
    }

    bool RemoveTextureDialogManualRangeByIndex(TextureDialogState* state, const std::string& textureName, size_t rangeIndex, ManualTextureAnimationRange* removed = nullptr)
    {
        auto* ranges = GetManualAnimationRanges(state, textureName);
        if (!ranges || rangeIndex >= ranges->size()) return false;
        if (removed) *removed = (*ranges)[rangeIndex];
        ranges->erase(ranges->begin() + static_cast<std::ptrdiff_t>(rangeIndex));
        return true;
    }

    bool TryExtendTextureDialogExistingRange(TextureDialogState* state, const std::string& textureName, int strip, std::string& outMessage)
    {
        auto* ranges = GetManualAnimationRanges(state, textureName);
        if (!ranges) return false;

        struct ExtensionCandidate
        {
            size_t rangeIndex = 0;
            int first = 0;
            int frames = 0;
        };

        std::vector<ExtensionCandidate> candidates;
        for (size_t i = 0; i < ranges->size(); ++i)
        {
            const auto& range = (*ranges)[i];
            const int first = range.firstStrip;
            const int last = range.firstStrip + range.frames - 1;
            if (strip != first - 1 && strip != last + 1)
            {
                continue;
            }

            std::vector<int> candidateStrips{ strip };
            for (int s = first; s <= last; ++s) candidateStrips.push_back(s);

            int candidateFirst = -1;
            int candidateFrames = 0;
            if (TextureDialogStripsFormContiguousAnimation(candidateStrips, candidateFirst, candidateFrames))
            {
                candidates.push_back(ExtensionCandidate{ i, candidateFirst, candidateFrames });
            }
        }

        if (candidates.empty())
        {
            // Adjacent to a full 4-frame range? Start a new manual group instead
            // of trying to create an invalid 5+ frame animation. This lets a run
            // like 1..8 become 1..4 plus 5..8 while clicking left-to-right.
            return false;
        }

        std::sort(candidates.begin(), candidates.end(), [](const ExtensionCandidate& a, const ExtensionCandidate& b)
        {
            if (a.frames != b.frames) return a.frames < b.frames;
            return a.first < b.first;
        });

        const ExtensionCandidate chosen = candidates.front();
        RemoveTextureDialogManualRangeByIndex(state, textureName, chosen.rangeIndex);
        AddOrReplaceManualAnimationRange(state, textureName, chosen.first, chosen.frames);
        CancelTextureDialogPendingAnimation(state);

        std::ostringstream ss;
        ss << "Animation set: ticked sections " << (chosen.first + 1) << "-" << (chosen.first + chosen.frames) << ".";
        if (!TextureDialogTextureIsAssigned(state, textureName))
        {
            ss << " Tick this texture above so it is saved in the map.";
        }
        outMessage = ss.str();
        return true;
    }

    void HandleTextureDialogPreviewLeftClick(HWND preview, TextureDialogState* state, int sx, int sy)
    {
        if (!preview || !state) return;
        int strip = -1;
        if (!TextureDialogPreviewStripFromPoint(preview, state, sx, sy, strip)) return;
        const std::string textureName = GetTextureDialogSelectedTextureName(state);
        if (textureName.empty()) return;

        auto* ranges = GetManualAnimationRanges(state, textureName);
        if (ranges)
        {
            for (size_t i = 0; i < ranges->size(); ++i)
            {
                const ManualTextureAnimationRange range = (*ranges)[i];
                if (!ManualAnimationContainsStrip(range, strip)) continue;

                std::vector<int> remaining;
                for (int s = range.firstStrip; s < range.firstStrip + range.frames; ++s)
                {
                    if (s != strip) remaining.push_back(s);
                }
                RemoveTextureDialogManualRangeByIndex(state, textureName, i);
                CancelTextureDialogPendingAnimation(state);

                int first = -1;
                int frames = 0;
                if (remaining.empty())
                {
                    state->animationStatusMessage = "Animation range removed.";
                }
                else if (TextureDialogStripsFormContiguousAnimation(remaining, first, frames))
                {
                    AddOrReplaceManualAnimationRange(state, textureName, first, frames);
                    std::ostringstream ss;
                    ss << "Animation updated: ticked sections " << (first + 1) << "-" << (first + frames) << ".";
                    state->animationStatusMessage = ss.str();
                }
                else
                {
                    SetTextureDialogPendingStrips(state, textureName, remaining);
                    std::ostringstream ss;
                    ss << "Ticked sections " << FormatStripSelectionList(remaining)
                       << ". Tick every neighbouring animation frame, or right-click to clear.";
                    state->animationStatusMessage = ss.str();
                }

                HWND dlg = GetParent(preview);
                RefreshTextureFoundStatus(dlg, state);
                InvalidateRect(preview, nullptr, TRUE);
                return;
            }
        }

        std::string extendMessage;
        if (TryExtendTextureDialogExistingRange(state, textureName, strip, extendMessage))
        {
            state->animationStatusMessage = extendMessage;
            HWND dlg = GetParent(preview);
            RefreshTextureFoundStatus(dlg, state);
            InvalidateRect(preview, nullptr, TRUE);
            return;
        }

        if (!TextureDialogPendingAppliesToTexture(state, textureName))
        {
            SetTextureDialogPendingStrips(state, textureName, {});
        }

        state->pendingManualAnimationStrips[strip] = !state->pendingManualAnimationStrips[strip];
        std::vector<int> selected = CollectTextureDialogPendingStrips(state);
        if (selected.empty())
        {
            CancelTextureDialogPendingAnimation(state);
            state->animationStatusMessage = "Animation selection cleared.";
        }
        else if (selected.size() > 4)
        {
            state->pendingManualAnimationStrips[strip] = false;
            selected = CollectTextureDialogPendingStrips(state);
            std::ostringstream ss;
            ss << "Each animation group can use at most 4 sections. Start the next group after this one. Ticked: " << FormatStripSelectionList(selected) << ".";
            state->animationStatusMessage = ss.str();
        }
        else
        {
            int first = -1;
            int frames = 0;
            if (TextureDialogStripsFormContiguousAnimation(selected, first, frames))
            {
                AddOrReplaceManualAnimationRange(state, textureName, first, frames);
                CancelTextureDialogPendingAnimation(state);
                std::ostringstream ss;
                ss << "Animation set: ticked sections " << (first + 1) << "-" << (first + frames) << ".";
                if (!TextureDialogTextureIsAssigned(state, textureName))
                {
                    ss << " Tick this texture above so it is saved in the map.";
                }
                state->animationStatusMessage = ss.str();
            }
            else
            {
                std::ostringstream ss;
                ss << "Ticked sections " << FormatStripSelectionList(selected)
                   << ". Tick every neighbouring animation frame; each saved group uses 2-4 sections.";
                state->animationStatusMessage = ss.str();
            }
        }

        HWND dlg = GetParent(preview);
        RefreshTextureFoundStatus(dlg, state);
        InvalidateRect(preview, nullptr, TRUE);
    }

    void HandleTextureDialogPreviewRightClick(HWND preview, TextureDialogState* state, int sx, int sy)
    {
        if (!preview || !state) return;
        int strip = -1;
        if (!TextureDialogPreviewStripFromPoint(preview, state, sx, sy, strip)) return;
        const std::string textureName = GetTextureDialogSelectedTextureName(state);
        if (textureName.empty()) return;

        if (RemoveManualAnimationRangeAtStrip(state, textureName, strip))
        {
            state->animationStatusMessage = "Animation range removed from this texture.";
        }
        else
        {
            state->animationStatusMessage = "Animation selection cancelled.";
        }
        CancelTextureDialogPendingAnimation(state);

        HWND dlg = GetParent(preview);
        RefreshTextureFoundStatus(dlg, state);
        InvalidateRect(preview, nullptr, TRUE);
    }

    bool HandleTextureDialogPreviewMouseWheel(HWND preview, TextureDialogState* state, int wheelDelta);

    LRESULT CALLBACK TextureDialogPreviewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR refData)
    {
        UNREFERENCED_PARAMETER(wParam);
        UNREFERENCED_PARAMETER(subclassId);
        auto* state = reinterpret_cast<TextureDialogState*>(refData);
        switch (msg)
        {
        case WM_LBUTTONDOWN:
            HandleTextureDialogPreviewLeftClick(hwnd, state, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_RBUTTONDOWN:
            HandleTextureDialogPreviewRightClick(hwnd, state, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MOUSEWHEEL:
            if (HandleTextureDialogPreviewMouseWheel(hwnd, state, GET_WHEEL_DELTA_WPARAM(wParam)))
            {
                return 0;
            }
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, TextureDialogPreviewProc, subclassId);
            break;
        }
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

    void SelectTextureDialogListItem(HWND dlg, TextureDialogState* state, int index)
    {
        if (!state) return;
        state->selectedListIndex = index;
        CancelTextureDialogPendingAnimation(state);
        state->selectedPreview = {};
        state->previewScrollX = 0;
        if (index >= 0 && index < static_cast<int>(state->foundTextureNames.size()))
        {
            LoadTexturePreviewImage(state->foundTextureNames[index], state->selectedPreview);
        }
        UpdateTextureDialogPreviewScrollbar(dlg, state);
        InvalidateRect(GetDlgItem(dlg, IDC_TEX_PREVIEW), nullptr, TRUE);
    }

    void DrawTextureDialogPreview(DRAWITEMSTRUCT* draw, TextureDialogState* state)
    {
        if (!draw) return;
        HDC hdc = draw->hDC;
        RECT rc = draw->rcItem;
        FillRect(hdc, &rc, DarkFieldBrush());

        HPEN borderPen = CreatePen(PS_SOLID, 1, kDarkBorder);
        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(hdc, borderPen));
        HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
        Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
        if (oldBrush) SelectObject(hdc, oldBrush);
        if (oldPen) SelectObject(hdc, oldPen);
        DeleteObject(borderPen);

        RECT inner = rc;
        InflateRect(&inner, -2, -2);
        if (!state || state->selectedListIndex < 0)
        {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, kDarkMutedText);
            DrawTextW(hdc, L"Select a texture in the list to preview it.", -1, &inner, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            return;
        }

        const TexturePreviewImage& image = state->selectedPreview;
        if (image.pixels.empty() || image.width <= 0 || image.height <= 0)
        {
            std::wstring message = image.error.empty() ? L"Preview could not be loaded." : Utf8ToWide(image.error);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, kDarkMutedText);
            DrawTextW(hdc, message.c_str(), -1, &inner, DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS);
            return;
        }

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = image.width;
        bmi.bmiHeader.biHeight = -image.height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        const int viewportW = MaxValue(1, inner.right - inner.left);
        const int viewportH = MaxValue(1, inner.bottom - inner.top);
        const double scale = MaxValue(1.0, static_cast<double>(viewportH) / static_cast<double>(image.height));
        const int scaledW = MaxValue(1, static_cast<int>(std::lround(static_cast<double>(image.width) * scale)));
        const int scaledH = MaxValue(1, static_cast<int>(std::lround(static_cast<double>(image.height) * scale)));
        const int scrollX = ClampValue(state->previewScrollX, 0, MaxValue(0, scaledW - viewportW));
        const int drawX = inner.left - scrollX;
        const int drawY = inner.top + (viewportH - scaledH) / 2;

        SaveDC(hdc);
        IntersectClipRect(hdc, inner.left, inner.top, inner.right, inner.bottom);
        StretchDIBits(hdc, drawX, drawY, scaledW, scaledH, 0, 0,
            image.width, image.height, image.pixels.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
        DrawTextureDialogAnimationOverlay(hdc, state, inner, drawX, drawY, scale, scaledH);
        RestoreDC(hdc, -1);
    }

    void PopulateTextureFoundList(HWND dlg, TextureDialogState* state)
    {
        HWND list = GetDlgItem(dlg, IDC_TEX_FOUND_LIST);
        if (!list || !state) return;

        state->populating = true;
        state->checkedOrder.clear();
        for (const auto& assigned : state->names)
        {
            if (!assigned.empty())
            {
                EnsureTextureDialogCheckedOrder(state, assigned);
            }
        }

        ListView_DeleteAllItems(list);
        while (ListView_DeleteColumn(list, 0)) {}

        ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        ListView_SetBkColor(list, kDarkFieldBg);
        ListView_SetTextBkColor(list, kDarkFieldBg);
        ListView_SetTextColor(list, kDarkListText);

        RECT rc{};
        GetClientRect(list, &rc);
        const int verticalReserve = GetSystemMetrics(SM_CXVSCROLL) + 8;
        const int totalWidth = MaxValue(160, rc.right - rc.left - verticalReserve);
        const int bankWidth = 48;
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<LPWSTR>(L"Texture file");
        col.cx = MaxValue(120, totalWidth - bankWidth);
        ListView_InsertColumn(list, 0, &col);
        col.pszText = const_cast<LPWSTR>(L"Bank");
        col.cx = bankWidth;
        ListView_InsertColumn(list, 1, &col);

        int initialSelection = -1;
        for (int i = 0; i < static_cast<int>(state->foundTextureNames.size()); ++i)
        {
            const std::wstring wide = Utf8ToWide(state->foundTextureNames[i]);
            const BOOL checked = TextureMapSlotsContainNoCase(state, state->foundTextureNames[i]) ? TRUE : FALSE;

            LVITEMW item{};
            item.mask = LVIF_TEXT | LVIF_STATE;
            item.iItem = i;
            item.pszText = const_cast<LPWSTR>(wide.c_str());
            item.stateMask = LVIS_STATEIMAGEMASK;
            item.state = INDEXTOSTATEIMAGEMASK(checked ? 2 : 1);
            ListView_InsertItem(list, &item);
            // Set the checkbox explicitly after insertion.  Some common-controls
            // builds ignore the initial LVIS_STATEIMAGEMASK when LVS_EX_CHECKBOXES
            // was just applied, which made loaded map slots appear unchecked.
            ListView_SetCheckState(list, i, checked);

            if (initialSelection < 0 && checked)
            {
                initialSelection = i;
            }
        }

        if (initialSelection < 0 && !state->foundTextureNames.empty())
        {
            initialSelection = 0;
        }
        if (initialSelection >= 0)
        {
            ListView_SetItemState(list, initialSelection, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }
        state->populating = false;
        UpdateTextureDialogBankColumn(dlg, state);
        SelectTextureDialogListItem(dlg, state, initialSelection);
        RefreshTextureFoundStatus(dlg, state);
    }

    std::vector<std::string> BuildTextureDialogAssignedNamesFromSlots(const TextureDialogState* state)
    {
        std::vector<std::string> assigned;
        if (!state) return assigned;
        for (const auto& name : state->names)
        {
            if (!name.empty()) assigned.push_back(name);
        }
        return assigned;
    }

    bool TextureDialogNameVectorsMatch(const std::vector<std::string>& a, const std::vector<std::string>& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (!TextureNameEqualsNoCase(a[i], b[i])) return false;
        }
        return true;
    }

    bool ReadCheckedTextureNames(HWND dlg, TextureDialogState* state, std::vector<std::string>& checked)
    {
        checked.clear();
        HWND list = GetDlgItem(dlg, IDC_TEX_FOUND_LIST);
        if (!list || !state) return true;

        const std::vector<std::string> visibleChecked = BuildTextureDialogCheckedOrder(list, state);
        const std::vector<std::string> assignedFromSlots = BuildTextureDialogAssignedNamesFromSlots(state);

        if (!state->textureSelectionTouched)
        {
            // No checkbox edit: keep the exact T0-T7 model.  This lets the user
            // open the dialog only to edit animation ranges without the list-view
            // state ever becoming authoritative.
            checked = assignedFromSlots;
            return true;
        }

        checked = visibleChecked;
        if (checked.size() > mapfmt::MapDocument::kTextureSlotCount)
        {
            MessageBoxW(dlg, L"A Gloom map has 8 texture slots. Please tick at most 8 textures.", L"Too many textures selected", MB_OK | MB_ICONWARNING);
            return false;
        }

        state->checkedOrder = checked;
        SyncTextureDialogAssignedSlotsFromList(dlg, state);
        return true;
    }

    void HandleTextureDialogPreviewScroll(HWND dlg, TextureDialogState* state, HWND scroll, UINT request)
    {
        if (!state || !scroll) return;
        SCROLLINFO si{};
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        GetScrollInfo(scroll, SB_CTL, &si);

        int pos = state->previewScrollX;
        switch (request)
        {
        case SB_LINELEFT:  pos -= 24; break;
        case SB_LINERIGHT: pos += 24; break;
        case SB_PAGELEFT:  pos -= static_cast<int>(si.nPage); break;
        case SB_PAGERIGHT: pos += static_cast<int>(si.nPage); break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK: pos = si.nTrackPos; break;
        case SB_LEFT:      pos = 0; break;
        case SB_RIGHT:     pos = state->previewMaxScrollX; break;
        default: return;
        }

        state->previewScrollX = ClampValue(pos, 0, state->previewMaxScrollX);
        si.fMask = SIF_POS;
        si.nPos = state->previewScrollX;
        SetScrollInfo(scroll, SB_CTL, &si, TRUE);
        InvalidateRect(GetDlgItem(dlg, IDC_TEX_PREVIEW), nullptr, TRUE);
    }

    bool HandleTextureDialogPreviewMouseWheel(HWND preview, TextureDialogState* state, int wheelDelta)
    {
        if (!preview || !state || state->previewMaxScrollX <= 0 || wheelDelta == 0) return false;
        HWND dlg = GetParent(preview);
        if (!dlg) return false;

        state->previewWheelRemainder += wheelDelta;
        constexpr int kPreviewWheelPixelsPerNotch = 32;
        const int pixelDelta = (state->previewWheelRemainder * kPreviewWheelPixelsPerNotch) / WHEEL_DELTA;
        if (pixelDelta == 0) return true;

        state->previewWheelRemainder -= (pixelDelta * WHEEL_DELTA) / kPreviewWheelPixelsPerNotch;
        state->previewScrollX = ClampValue(state->previewScrollX - pixelDelta, 0, state->previewMaxScrollX);
        if (HWND scroll = GetDlgItem(dlg, IDC_TEX_PREVIEW_SCROLL))
        {
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_POS;
            si.nPos = state->previewScrollX;
            SetScrollInfo(scroll, SB_CTL, &si, TRUE);
            InvalidateRect(scroll, nullptr, TRUE);
        }
        InvalidateRect(preview, nullptr, TRUE);
        return true;
    }

    INT_PTR CALLBACK TextureDialogProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<TextureDialogState*>(GetWindowLongPtrW(dlg, DWLP_USER));
        if (msg == WM_INITDIALOG)
        {
            state = reinterpret_cast<TextureDialogState*>(lParam);
            SetWindowLongPtrW(dlg, DWLP_USER, lParam);
            CenterDialogOnOwner(dlg, g_app.mainWindow);
            ApplyEditorDarkModeToWindowTree(dlg);
            if (HWND scroll = GetDlgItem(dlg, IDC_TEX_PREVIEW_SCROLL))
            {
                SetWindowSubclass(scroll, TextureDialogPreviewScrollProc, 1, reinterpret_cast<DWORD_PTR>(state));
            }
            if (HWND preview = GetDlgItem(dlg, IDC_TEX_PREVIEW))
            {
                SetWindowSubclass(preview, TextureDialogPreviewProc, 1, reinterpret_cast<DWORD_PTR>(state));
            }
            PopulateTextureFoundList(dlg, state);
            return TRUE;
        }

        if (!state) return FALSE;

        switch (msg)
        {
        case WM_DRAWITEM:
        {
            auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (draw && draw->CtlID == IDC_TEX_PREVIEW)
            {
                DrawTextureDialogPreview(draw, state);
                return TRUE;
            }
            return DrawDarkOwnerDrawControl(draw) ? TRUE : FALSE;
        }

        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORSCROLLBAR:
            return HandleDarkCtlColor(msg, wParam);

        case WM_HSCROLL:
            if (reinterpret_cast<HWND>(lParam) == GetDlgItem(dlg, IDC_TEX_PREVIEW_SCROLL))
            {
                HandleTextureDialogPreviewScroll(dlg, state, reinterpret_cast<HWND>(lParam), LOWORD(wParam));
                return TRUE;
            }
            break;

        case WM_NOTIFY:
        {
            auto* nm = reinterpret_cast<NMHDR*>(lParam);
            if (nm && nm->idFrom == IDC_TEX_FOUND_LIST && nm->code == LVN_ITEMCHANGED)
            {
                auto* changed = reinterpret_cast<NMLISTVIEW*>(lParam);
                if (changed && (changed->uChanged & LVIF_STATE) && (changed->uNewState & LVIS_SELECTED) && changed->iItem >= 0)
                {
                    SelectTextureDialogListItem(dlg, state, changed->iItem);
                }

                if (!state->populating && changed && changed->iItem >= 0 && changed->iItem < static_cast<int>(state->foundTextureNames.size()) &&
                    (changed->uChanged & LVIF_STATE) && ((changed->uOldState ^ changed->uNewState) & LVIS_STATEIMAGEMASK))
                {
                    const std::string textureName = state->foundTextureNames[changed->iItem];
                    const bool nowChecked = (changed->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(2);
                    state->textureSelectionTouched = true;
                    if (nowChecked)
                    {
                        EnsureTextureDialogCheckedOrder(state, textureName);
                    }
                    else
                    {
                        RemoveTextureDialogCheckedOrder(state, textureName);
                    }
                    SyncTextureDialogAssignedSlotsFromList(dlg, state);
                }

                const int checkedCount = CountCheckedTextureListItems(nm->hwndFrom);
                if (checkedCount > static_cast<int>(mapfmt::MapDocument::kTextureSlotCount))
                {
                    if (changed && changed->iItem >= 0)
                    {
                        ListView_SetCheckState(nm->hwndFrom, changed->iItem, FALSE);
                        if (changed->iItem < static_cast<int>(state->foundTextureNames.size()))
                        {
                            RemoveTextureDialogCheckedOrder(state, state->foundTextureNames[changed->iItem]);
                        }
                    }
                    MessageBoxW(dlg, L"A Gloom map has 8 texture slots. Please tick at most 8 textures.", L"Too many textures selected", MB_OK | MB_ICONWARNING);
                }
                UpdateTextureDialogBankColumn(dlg, state);
                RefreshTextureFoundStatus(dlg, state);
            }
            else if (nm && nm->idFrom == IDC_TEX_FOUND_LIST && nm->code == NM_CLICK)
            {
                // LVN_ITEMCHANGED is the primary path, but remember explicit
                // checkbox clicks as a belt-and-braces guard so OK never ignores
                // a visible user tick because a notification was swallowed.
                HWND list = nm->hwndFrom;
                DWORD pos = GetMessagePos();
                POINT pt{ GET_X_LPARAM(pos), GET_Y_LPARAM(pos) };
                ScreenToClient(list, &pt);
                LVHITTESTINFO hit{};
                hit.pt = pt;
                const int hitIndex = ListView_HitTest(list, &hit);
                if (hitIndex >= 0 && (hit.flags & LVHT_ONITEMSTATEICON))
                {
                    state->textureSelectionTouched = true;
                }
            }
            break;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IDOK:
            {
                std::vector<std::string> checked;
                if (!ReadCheckedTextureNames(dlg, state, checked))
                {
                    return TRUE;
                }

                UNREFERENCED_PARAMETER(checked);
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
        state.foundTextureNames = BuildTextureSlotNameList();
        ImportTextureDialogManualAnimationsFromDocument(state);
        EnsureAssignedTexturesInFoundList(state);
        if (DialogBoxParamW(g_app.instance, MAKEINTRESOURCEW(IDD_MAPSETTINGS), g_app.mainWindow, TextureDialogProc, reinterpret_cast<LPARAM>(&state)) == IDOK && state.accepted)
        {
            PushUndoSnapshot();
            g_app.document.textureNames = state.names;
            ApplyTextureDialogManualAnimationsToDocument(state);
            for (int i = 0; i < static_cast<int>(mapfmt::MapDocument::kTextureSlotCount); ++i)
            {
                if (!g_app.document.textureNames[i].empty())
                {
                    g_app.activeWallTextureSlot = i;
                    g_app.previewTextureSlot = i;
                    break;
                }
            }
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
        const auto& script = state->document->events[state->slotIndex];
        const auto& commands = script.commands;
        for (const auto& cmd : commands)
        {
            const auto text = Utf8ToWide(cmd.ToDisplayString());
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        }
        if (script.hasUnsupportedRaw)
        {
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"High-level editing disabled: original MAPED bytes will be preserved."));
        }
    }

    INT_PTR CALLBACK EventDialogProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<EventDialogState*>(GetWindowLongPtrW(dlg, DWLP_USER));
        if (msg == WM_INITDIALOG)
        {
            state = reinterpret_cast<EventDialogState*>(lParam);
            SetWindowLongPtrW(dlg, DWLP_USER, lParam);
            CenterDialogOnOwner(dlg, g_app.mainWindow);
            ApplyEditorDarkModeToWindowTree(dlg);
            for (int i = 0; i < mapfmt::MapDocument::kEventCount; ++i)
            {
                const std::wstring label = EventSlotComboLabel(i);
                SendDlgItemMessageW(dlg, IDC_EVENT_SLOT, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            }
            SendDlgItemMessageW(dlg, IDC_EVENT_SLOT, CB_SETCURSEL, state->slotIndex, 0);
            PopulateEventCommandList(dlg, state);
            const BOOL editable = state->document && !state->document->events[state->slotIndex].hasUnsupportedRaw;
            EnableWindow(GetDlgItem(dlg, IDC_EVENT_ADD), editable);
            EnableWindow(GetDlgItem(dlg, IDC_EVENT_EDIT), editable);
            EnableWindow(GetDlgItem(dlg, IDC_EVENT_DELETE), editable);
            EnableWindow(GetDlgItem(dlg, IDC_EVENT_UP), editable);
            EnableWindow(GetDlgItem(dlg, IDC_EVENT_DOWN), editable);
            return TRUE;
        }

        if (!state) return FALSE;

        switch (msg)
        {
        case WM_DRAWITEM:
            return DrawDarkOwnerDrawControl(reinterpret_cast<DRAWITEMSTRUCT*>(lParam)) ? TRUE : FALSE;

        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            return HandleDarkCtlColor(msg, wParam);

        case WM_COMMAND:
        {
            HWND list = GetDlgItem(dlg, IDC_EVENT_COMMANDS);
            auto& script = state->document->events[state->slotIndex];
            auto& commands = script.commands;
            const bool eventEditable = !script.hasUnsupportedRaw;
            switch (LOWORD(wParam))
            {
            case IDC_EVENT_SLOT:
                if (HIWORD(wParam) == CBN_SELCHANGE)
                {
                    state->slotIndex = static_cast<int>(SendDlgItemMessageW(dlg, IDC_EVENT_SLOT, CB_GETCURSEL, 0, 0));
                    PopulateEventCommandList(dlg, state);
                    const BOOL editable = state->document && !state->document->events[state->slotIndex].hasUnsupportedRaw;
                    EnableWindow(GetDlgItem(dlg, IDC_EVENT_ADD), editable);
                    EnableWindow(GetDlgItem(dlg, IDC_EVENT_EDIT), editable);
                    EnableWindow(GetDlgItem(dlg, IDC_EVENT_DELETE), editable);
                    EnableWindow(GetDlgItem(dlg, IDC_EVENT_UP), editable);
                    EnableWindow(GetDlgItem(dlg, IDC_EVENT_DOWN), editable);
                }
                break;
            case IDC_EVENT_ADD:
            {
                if (!eventEditable) break;
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
                if (!eventEditable) break;
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
                if (!eventEditable) break;
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
                if (!eventEditable) break;
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
                if (!eventEditable) break;
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
        EditorSnapshot before;
        before.document = g_app.document;
        before.selectedZone = g_app.selectedZone;
        state.document = &g_app.document;
        state.slotIndex = 0;
        if (g_app.selectedZone >= 0 && g_app.selectedZone < static_cast<int>(g_app.document.zones.size()))
        {
            const int triggerSlot = EventSlotFromZoneEventValue(g_app.document.zones[g_app.selectedZone].ev);
            if (triggerSlot >= 0)
            {
                state.slotIndex = triggerSlot;
            }
        }
        else if (g_app.selectedMonsterSpawn.IsSet())
        {
            state.slotIndex = ClampValue(g_app.selectedMonsterSpawn.eventIndex, 0, static_cast<int>(mapfmt::MapDocument::kEventCount) - 1);
        }
        if (DialogBoxParamW(g_app.instance, MAKEINTRESOURCEW(IDD_EVENT), g_app.mainWindow, EventDialogProc, reinterpret_cast<LPARAM>(&state)) >= 0 && state.changed)
        {
            if (g_app.undoStack.size() >= kMaxUndoSteps)
            {
                g_app.undoStack.erase(g_app.undoStack.begin());
            }
            g_app.undoStack.push_back(std::move(before));
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
        if (zoneType == static_cast<int>(mapfmt::ZoneType::Wall))
        {
            InitializeNewWallTextureSequence(zone);
            mapfmt::RecalculateWallMetadata(zone);
            UpdateWallTextureBandCountFromLength(zone);
        }
        if (ShowZoneDialog(zone, L"Add Zone"))
        {
            PushUndoSnapshot();
            g_app.document.zones.push_back(zone);
            ClearSelectedMonsterSpawn();
            g_app.selectedZone = static_cast<int>(g_app.document.zones.size()) - 1;
            if (zone.ztype == static_cast<int16_t>(mapfmt::ZoneType::Wall))
            {
                EnsureBackfaceForWallAtIndex(g_app.selectedZone);
            }
            MarkDirty();
            RefreshZoneList();
        }
    }

    void EditSelectedZone()
    {
        ClearInsertMode();
        if (g_app.selectedZone < 0 || g_app.selectedZone >= static_cast<int>(g_app.document.zones.size()))
        {
            MessageBoxW(g_app.mainWindow, L"Select a zone first, then choose Edit Zone.", L"ZGloom Editor", MB_OK | MB_ICONINFORMATION);
            return;
        }
        auto zone = g_app.document.zones[g_app.selectedZone];
        const int previousBackface = FindReverseWallPairIndex(g_app.selectedZone);
        if (ShowZoneDialog(zone, L"Edit Zone"))
        {
            PushUndoSnapshot();
            g_app.document.zones[g_app.selectedZone] = zone;
            WeldWallCornersAroundZone(g_app.selectedZone);
            if (g_app.document.zones[g_app.selectedZone].ztype == static_cast<int16_t>(mapfmt::ZoneType::Wall))
            {
                if (previousBackface >= 0 && previousBackface < static_cast<int>(g_app.document.zones.size()) && previousBackface != g_app.selectedZone)
                {
                    SyncBackfaceWallFromFront(g_app.selectedZone, previousBackface);
                }
                else
                {
                    EnsureBackfaceForWallAtIndex(g_app.selectedZone);
                }
            }
            MarkDirty();
            RefreshZoneList();
        }
    }

    bool DeleteSelectedMonsterSpawn()
    {
        if (!IsSelectedMonsterSpawnValid())
        {
            return false;
        }

        const int eventIndex = g_app.selectedMonsterSpawn.eventIndex;
        const int commandIndex = g_app.selectedMonsterSpawn.commandIndex;
        std::wstringstream question;
        question << L"Delete selected object from Event " << (eventIndex + 1)
                 << L", command " << (commandIndex + 1) << L"?";
        if (MessageBoxW(g_app.mainWindow, question.str().c_str(), L"ZGloom Editor", MB_YESNO | MB_ICONQUESTION) != IDYES)
        {
            return true;
        }

        PushUndoSnapshot();
        auto& commands = g_app.document.events[eventIndex].commands;
        if (commandIndex >= 0 && commandIndex < static_cast<int>(commands.size()))
        {
            commands.erase(commands.begin() + commandIndex);
        }
        ClearSelectedMonsterSpawn();
        g_app.selectedZone = -1;
        RefreshPreviewImage();
        MarkDirty();
        RefreshZoneList();
        RefreshStatus();
        InvalidateEditorViews();
        return true;
    }

    bool DeleteSelectedTeleportTarget()
    {
        if (!IsSelectedTeleportTargetValid())
        {
            return false;
        }

        const int eventIndex = g_app.selectedTeleportTarget.eventIndex;
        const int commandIndex = g_app.selectedTeleportTarget.commandIndex;
        std::wstringstream question;
        question << L"Delete selected teleport target from Event " << (eventIndex + 1)
                 << L"? The trigger line itself stays on the map.";
        if (MessageBoxW(g_app.mainWindow, question.str().c_str(), L"ZGloom Editor", MB_YESNO | MB_ICONQUESTION) != IDYES)
        {
            return true;
        }

        PushUndoSnapshot();
        auto& commands = g_app.document.events[eventIndex].commands;
        if (commandIndex >= 0 && commandIndex < static_cast<int>(commands.size()))
        {
            commands.erase(commands.begin() + commandIndex);
        }
        ClearSelectedTeleportTarget();
        ClearSelectedMonsterSpawn();
        g_app.selectedZone = -1;
        RefreshPreviewImage();
        MarkDirty();
        RefreshZoneList();
        RefreshStatus();
        InvalidateEditorViews();
        return true;
    }

    void DeleteSelectedItem()
    {
        if (DeleteSelectedTeleportTarget())
        {
            return;
        }
        if (DeleteSelectedMonsterSpawn())
        {
            return;
        }
        DeleteSelectedZone();
    }

    void DeleteSelectedZone()
    {
        ClearInsertMode();
        if (g_app.selectedZone < 0 || g_app.selectedZone >= static_cast<int>(g_app.document.zones.size()))
        {
            MessageBoxW(g_app.mainWindow, L"Select a zone first, then choose Delete Zone.", L"ZGloom Editor", MB_OK | MB_ICONINFORMATION);
            return;
        }
        const int backfaceIndex = FindReverseWallPairIndex(g_app.selectedZone);
        const bool deleteBackface = backfaceIndex >= 0;
        const wchar_t* question = deleteBackface
            ? L"Delete the selected wall and its invisible back side?"
            : L"Delete the selected zone?";
        if (MessageBoxW(g_app.mainWindow, question, L"ZGloom Editor", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
        PushUndoSnapshot();
        if (deleteBackface)
        {
            const int first = MinValue(g_app.selectedZone, backfaceIndex);
            const int second = MaxValue(g_app.selectedZone, backfaceIndex);
            // Event commands store zone indices. Remove links to the deleted
            // wall/backface pair before erasing; otherwise OpenDoor arrows and
            // commands silently slide to the next neighbouring zone.
            RemoveEventReferencesForDeletedZoneIndex(second);
            RemoveEventReferencesForDeletedZoneIndex(first);
            g_app.document.zones.erase(g_app.document.zones.begin() + second);
            g_app.document.zones.erase(g_app.document.zones.begin() + first);
            g_app.selectedZone = MinValue(first, static_cast<int>(g_app.document.zones.size()) - 1);
        }
        else
        {
            const int deletedZone = g_app.selectedZone;
            RemoveEventReferencesForDeletedZoneIndex(deletedZone);
            g_app.document.zones.erase(g_app.document.zones.begin() + deletedZone);
            if (g_app.selectedZone >= static_cast<int>(g_app.document.zones.size())) g_app.selectedZone = static_cast<int>(g_app.document.zones.size()) - 1;
        }
        MarkDirty();
        RefreshPreviewImage();
        RefreshZoneList();
        RefreshStatus();
        InvalidateEditorViews();
    }

    void MoveZoneUp()
    {
        if (g_app.selectedZone > 0)
        {
            PushUndoSnapshot();
            const int oldIndex = g_app.selectedZone;
            const int newIndex = g_app.selectedZone - 1;
            std::swap(g_app.document.zones[oldIndex], g_app.document.zones[newIndex]);
            SwapEventZoneReferences(oldIndex, newIndex);
            --g_app.selectedZone;
            MarkDirty();
            RefreshZoneList();
            RefreshStatus();
            InvalidateEditorViews();
        }
    }

    void MoveZoneDown()
    {
        if (g_app.selectedZone >= 0 && g_app.selectedZone + 1 < static_cast<int>(g_app.document.zones.size()))
        {
            PushUndoSnapshot();
            const int oldIndex = g_app.selectedZone;
            const int newIndex = g_app.selectedZone + 1;
            std::swap(g_app.document.zones[oldIndex], g_app.document.zones[newIndex]);
            SwapEventZoneReferences(oldIndex, newIndex);
            ++g_app.selectedZone;
            MarkDirty();
            RefreshZoneList();
            RefreshStatus();
            InvalidateEditorViews();
        }
    }

    std::wstring FormatYesNo(bool value)
    {
        return value ? L"yes" : L"no";
    }

    std::wstring FormatObjectTypeList(const std::vector<int>& values)
    {
        if (values.empty()) return L"none";
        std::wstringstream ss;
        for (size_t i = 0; i < values.size(); ++i)
        {
            if (i) ss << L", ";
            ss << values[i];
        }
        return ss.str();
    }

    void AddSortedUniqueInt(std::vector<int>& values, int value)
    {
        if (std::find(values.begin(), values.end(), value) == values.end())
        {
            values.push_back(value);
            std::sort(values.begin(), values.end());
        }
    }

    int CountValidationTagOccurrences(const std::wstring& text, const wchar_t* tag)
    {
        if (!tag || !*tag) return 0;
        int count = 0;
        size_t pos = 0;
        const size_t tagLen = std::wcslen(tag);
        while ((pos = text.find(tag, pos)) != std::wstring::npos)
        {
            ++count;
            pos += tagLen;
        }
        return count;
    }

    std::wstring FormatLimitedWideList(const std::vector<std::wstring>& values, size_t limit = 8)
    {
        if (values.empty()) return L"none";
        std::wstringstream ss;
        const size_t shown = MinValue(values.size(), limit);
        for (size_t i = 0; i < shown; ++i)
        {
            if (i) ss << L"; ";
            ss << values[i];
        }
        if (values.size() > shown)
        {
            ss << L"; ... +" << (values.size() - shown) << L" more";
        }
        return ss.str();
    }

    bool WorldPointOutsideBoundsWithMargin(int x, int z, const mapfmt::Bounds& bounds, int margin)
    {
        if (!bounds.valid) return false;
        return x < bounds.minX - margin || x > bounds.maxX + margin ||
               z < bounds.minZ - margin || z > bounds.maxZ + margin;
    }

    void AddValidationIssueLine(std::vector<std::wstring>& issues, const std::wstring& line)
    {
        if (!line.empty())
        {
            issues.push_back(line);
        }
    }

    MapValidationReportSections BuildMapValidationReportSections()
    {
        RefreshGameProfile();

        int wallCount = 0;
        int triggerCount = 0;
        int monsterLineCount = 0;
        int levelEndCount = 0;
        int unusualScCount = 0;
        int eventOneTriggerCount = 0;
        int textureOutOfCorpusCount = 0;
        std::vector<int> usedTextures;
        std::vector<int> invalidTriggerEventZones;

        for (size_t i = 0; i < g_app.document.zones.size(); ++i)
        {
            const auto& zone = g_app.document.zones[i];
            if (zone.ztype == static_cast<int16_t>(mapfmt::ZoneType::Wall))
            {
                ++wallCount;
                if (zone.sc < -8 || zone.sc > 16) ++unusualScCount;
            }
            else if (IsLevelEndZone(zone))
            {
                ++levelEndCount;
            }
            else if (IsEventTriggerLineZone(zone))
            {
                ++triggerCount;
                if (zone.ev == 1) ++eventOneTriggerCount;
            }
            else if (zone.ztype == static_cast<int16_t>(mapfmt::ZoneType::MonsterZone))
            {
                ++monsterLineCount;
            }

            if (IsLinearZoneType(zone.ztype) && zone.ztype != static_cast<int16_t>(mapfmt::ZoneType::Wall) &&
                zone.ev != 0 && !IsLevelEndZone(zone) && EventSlotFromZoneEventValue(zone.ev) < 0)
            {
                AddSortedUniqueInt(invalidTriggerEventZones, static_cast<int>(i));
            }

            for (uint8_t texture : zone.textures)
            {
                const int textureId = static_cast<int>(texture);
                AddSortedUniqueInt(usedTextures, textureId);
                if (textureId > 79) ++textureOutOfCorpusCount;
            }
        }

        int addObjectCount = 0;
        int openDoorCount = 0;
        int teleportCount = 0;
        int loadObjectsCount = 0;
        int changeTextureCount = 0;
        int rotatePolyCount = 0;
        int moveWallBlockCount = 0;
        int unsupportedEventCount = 0;
        bool hasP1Start = false;
        bool hasP2Start = false;
        std::vector<int> objectTypes;
        std::vector<int> triggerlessEvents;
        std::vector<int> emptyTriggeredEvents;
        std::vector<int> nonNeutralMoveGuides;
        std::vector<std::wstring> invalidZoneReferences;
        std::vector<std::wstring> objectOutsideMapIssues;
        const std::vector<uint8_t> moveWallGuideMask = BuildMoveWallGroupGuideMaskForDocument(g_app.document);
        const mapfmt::Bounds validationBounds = g_app.document.ComputeBounds();

        for (int eventIndex = 0; eventIndex < static_cast<int>(g_app.document.events.size()); ++eventIndex)
        {
            const auto& script = g_app.document.events[eventIndex];
            if (script.hasUnsupportedRaw)
            {
                ++unsupportedEventCount;
                continue;
            }

            const bool hasCommands = !script.commands.empty();
            const bool hasTrigger = EventHasActiveTrigger(eventIndex);
            if (eventIndex > kInitialEventIndex && hasCommands && !hasTrigger)
            {
                triggerlessEvents.push_back(eventIndex + 1);
            }
            if (eventIndex > kInitialEventIndex && hasTrigger && !hasCommands)
            {
                emptyTriggeredEvents.push_back(eventIndex + 1);
            }

            for (const auto& command : script.commands)
            {
                switch (command.type)
                {
                case mapfmt::CommandType::AddMonster:
                    ++addObjectCount;
                    AddSortedUniqueInt(objectTypes, static_cast<int>(command.params[0]));
                    if (eventIndex == kInitialEventIndex && command.params[0] == kPlayer1ObjectType) hasP1Start = true;
                    if (eventIndex == kInitialEventIndex && command.params[0] == kPlayer2ObjectType) hasP2Start = true;
                    if (WorldPointOutsideBoundsWithMargin(command.params[1], command.params[3], validationBounds, kGridStep))
                    {
                        std::wstringstream issue;
                        issue << L"Event " << (eventIndex + 1) << L" command " << (&command - script.commands.data() + 1)
                              << L" object type " << command.params[0]
                              << L" at X " << command.params[1] << L" Z " << command.params[3];
                        AddValidationIssueLine(objectOutsideMapIssues, issue.str());
                    }
                    break;
                case mapfmt::CommandType::OpenDoor:
                    ++openDoorCount;
                    if (command.params[0] < 0 || command.params[0] >= static_cast<int>(g_app.document.zones.size()))
                    {
                        std::wstringstream issue;
                        issue << L"Event " << (eventIndex + 1) << L" OpenDoor targets missing Z" << command.params[0];
                        AddValidationIssueLine(invalidZoneReferences, issue.str());
                    }
                    break;
                case mapfmt::CommandType::Teleport:
                    ++teleportCount;
                    break;
                case mapfmt::CommandType::LoadObjects:
                    ++loadObjectsCount;
                    break;
                case mapfmt::CommandType::ChangeTexture:
                    ++changeTextureCount;
                    if (command.params[0] < 0 || command.params[0] >= static_cast<int>(g_app.document.zones.size()))
                    {
                        std::wstringstream issue;
                        issue << L"Event " << (eventIndex + 1) << L" ChangeTexture targets missing Z" << command.params[0];
                        AddValidationIssueLine(invalidZoneReferences, issue.str());
                    }
                    break;
                case mapfmt::CommandType::RotatePoly:
                    ++rotatePolyCount;
                    {
                        const int first = static_cast<int>(command.params[0]);
                        const int rawCount = static_cast<int>(command.params[1]);
                        const int count = MaxValue(1, rawCount);
                        if (first < 0 || rawCount <= 0 || first + count > static_cast<int>(g_app.document.zones.size()))
                        {
                            std::wstringstream issue;
                            issue << L"Event " << (eventIndex + 1) << L" RotatePoly range Z" << first << L" count " << rawCount << L" is outside the map zone list";
                            AddValidationIssueLine(invalidZoneReferences, issue.str());
                        }
                    }
                    if (IsMoveWallGroupRotateCommand(command))
                    {
                        ++moveWallBlockCount;
                        const int first = static_cast<int>(command.params[0]);
                        const int count = MaxValue(1, static_cast<int>(command.params[1]));
                        const int guideFirst = first + count;
                        if (guideFirst < 0 || guideFirst + count > static_cast<int>(g_app.document.zones.size()))
                        {
                            std::wstringstream issue;
                            issue << L"Event " << (eventIndex + 1) << L" Move-Wallblock guide range Z" << guideFirst << L" count " << count << L" is missing or incomplete";
                            AddValidationIssueLine(invalidZoneReferences, issue.str());
                        }
                        for (int i = 0; i < count; ++i)
                        {
                            const int guideIndex = guideFirst + i;
                            if (!IsMoveWallGroupGuideZoneIndex(moveWallGuideMask, guideIndex))
                            {
                                continue;
                            }
                            const auto& guide = g_app.document.zones[static_cast<size_t>(guideIndex)];
                            const bool neutral = guide.a == 0 && guide.b == 0 && guide.na == 0 && guide.nb == 0 &&
                                guide.ln == 0 && guide.sc == 0 && guide.ev == 0 &&
                                std::all_of(guide.textures.begin(), guide.textures.end(), [](uint8_t t) { return t == 0; });
                            if (!neutral)
                            {
                                AddSortedUniqueInt(nonNeutralMoveGuides, guideIndex);
                            }
                        }
                    }
                    break;
                default:
                    break;
                }
            }
        }

        MapValidationReportSections sections;

        {
            std::wstringstream ss;
            ss << (g_app.gameProfile.IsKnown() ? L"[OK] " : L"[INFO] ")
               << L"Game Profile: " << g_app.gameProfile.title << L"\r\n";
            if (!g_app.gameProfile.rootPath.empty())
            {
                ss << L"Game Root: " << Utf8ToWide(g_app.gameProfile.rootPath) << L"\r\n";
                const std::string mapFolderPath = JoinPath(g_app.gameProfile.rootPath, g_app.gameProfile.mapFolder);
                const std::string objectFolderPath = JoinPath(g_app.gameProfile.rootPath, g_app.gameProfile.objectFolder);
                const std::string imageFolderPath = JoinPath(g_app.gameProfile.rootPath, g_app.gameProfile.imageFolder);
                const std::string soundFolderPath = JoinPath(g_app.gameProfile.rootPath, g_app.gameProfile.soundFolder);
                const std::string scriptFullPath = JoinPath(g_app.gameProfile.rootPath, g_app.gameProfile.scriptPath);
                ss << (DirectoryExistsLocal(mapFolderPath) ? L"[OK] " : L"[INFO] ") << L"Map folder: " << Utf8ToWide(g_app.gameProfile.mapFolder) << L"\r\n";
                ss << (DirectoryExistsLocal(objectFolderPath) ? L"[OK] " : L"[INFO] ") << L"Object folder: " << Utf8ToWide(g_app.gameProfile.objectFolder) << L"\r\n";
                ss << (DirectoryExistsLocal(imageFolderPath) ? L"[OK] " : L"[INFO] ") << L"Image folder: " << Utf8ToWide(g_app.gameProfile.imageFolder) << L"\r\n";
                ss << (DirectoryExistsLocal(soundFolderPath) ? L"[OK] " : L"[INFO] ") << L"Sound/music folder: " << Utf8ToWide(g_app.gameProfile.soundFolder) << L"\r\n";
                ss << (FileExistsLocal(scriptFullPath) ? L"[OK] " : L"[INFO] ") << L"Campaign script: " << Utf8ToWide(g_app.gameProfile.scriptPath) << L"\r\n";
            }
            else
            {
                ss << L"[INFO] Game Root: not detected from current map path\r\n";
            }
            sections.profile = ss.str();
        }

        {
            std::wstringstream ss;
            ss << L"Zones: " << g_app.document.zones.size() << L" total, " << wallCount << L" walls, "
               << triggerCount << L" event triggers, " << monsterLineCount << L" object/marker lines, "
               << levelEndCount << L" level exits\r\n";
            ss << L"Player 1 start: " << FormatYesNo(hasP1Start) << L"\r\n";
            ss << L"Player 2 start: " << FormatYesNo(hasP2Start) << L"\r\n";
            ss << L"Exit line: " << FormatYesNo(levelEndCount > 0) << L"\r\n";
            ss << L"Commands: " << addObjectCount << L" AddMonster/Object, "
               << openDoorCount << L" OpenDoor, " << changeTextureCount << L" ChangeTexture, "
               << teleportCount << L" Teleport, " << loadObjectsCount << L" LoadObjects, "
               << rotatePolyCount << L" RotatePoly\r\n";
            ss << L"Move Wallblock commands: " << moveWallBlockCount << L"\r\n";
            ss << L"Object types used: " << FormatObjectTypeList(objectTypes) << L"\r\n";
            if (!usedTextures.empty())
            {
                ss << L"Texture IDs used: " << usedTextures.size() << L" unique, min " << usedTextures.front()
                   << L", max " << usedTextures.back() << L"\r\n";
            }
            else
            {
                ss << L"Texture IDs used: none\r\n";
            }
            ss << L"Unsupported/raw events: " << unsupportedEventCount << L"\r\n";
            sections.intelligence = ss.str();
        }

        {
            std::wstringstream ss;
            ss << (hasP1Start ? L"[OK] " : L"[WARN] ") << L"Player 1 start object in Event 1\r\n";
            ss << (levelEndCount > 0 ? L"[OK] " : L"[WARN] ") << L"Level exit line present\r\n";
            ss << (loadObjectsCount > 0 ? L"[OK] " : L"[INFO] ") << L"LoadObjects command count: " << loadObjectsCount << L"\r\n";
            if (eventOneTriggerCount > 0)
            {
                ss << (g_app.gameProfile.kind == GameProfileKind::ZombieMassacre ? L"[INFO] " : L"[WARN] ")
                   << L"Event 1 is used by " << eventOneTriggerCount << L" trigger line(s). Original Zombie Massacre maps may do this; classic Gloom maps usually keep Event 1 for map-start/object loading.\r\n";
            }
            else
            {
                ss << L"[OK] Event 1 is not used as a normal trigger source\r\n";
            }
            if (!triggerlessEvents.empty())
            {
                ss << L"[WARN] Event slot(s) with commands but no trigger: " << FormatObjectTypeList(triggerlessEvents) << L"\r\n";
            }
            else
            {
                ss << L"[OK] No triggered event slot with commands is orphaned\r\n";
            }
            if (!emptyTriggeredEvents.empty())
            {
                ss << L"[INFO] Trigger line(s) point to empty event slot(s): " << FormatObjectTypeList(emptyTriggeredEvents) << L"\r\n";
            }
            else
            {
                ss << L"[OK] No trigger line points to an empty event slot\r\n";
            }
            if (!invalidTriggerEventZones.empty())
            {
                ss << L"[WARN] Trigger/helper line(s) use invalid Event IDs: " << FormatObjectTypeList(invalidTriggerEventZones) << L"\r\n";
            }
            else
            {
                ss << L"[OK] Trigger line Event IDs are within the valid 1-24 range\r\n";
            }
            if (!invalidZoneReferences.empty())
            {
                ss << L"[WARN] Event command(s) reference missing or incomplete zone ranges: " << FormatLimitedWideList(invalidZoneReferences) << L"\r\n";
            }
            else
            {
                ss << L"[OK] Event command zone references point into the map zone list\r\n";
            }
            if (!objectOutsideMapIssues.empty())
            {
                ss << L"[WARN] Object/player spawn(s) are outside the current map bounds: " << FormatLimitedWideList(objectOutsideMapIssues) << L"\r\n";
            }
            else
            {
                ss << L"[OK] Object/player spawn positions are within the current map bounds\r\n";
            }
            if (textureOutOfCorpusCount > 0)
            {
                ss << L"[INFO] " << textureOutOfCorpusCount << L" texture slot reference(s) use IDs outside the shipped 0-79 corpus range. Keep this for custom texture sets.\r\n";
            }
            if (unusualScCount > 0)
            {
                ss << L"[INFO] " << unusualScCount << L" wall(s) use unusual texture scale values outside the common corpus range.\r\n";
            }
            if (!nonNeutralMoveGuides.empty())
            {
                ss << L"[WARN] Move-Wallblock target guide zone(s) are not AI/collision-neutral: " << FormatObjectTypeList(nonNeutralMoveGuides) << L"\r\n";
                sections.safeNeutralGuideZones = nonNeutralMoveGuides;
            }
            else if (moveWallBlockCount > 0)
            {
                ss << L"[OK] Move-Wallblock guide zones look AI/collision-neutral\r\n";
            }
            sections.checks = ss.str();
        }

        {
            std::wstringstream ss;
            bool wroteSuggestion = false;
            auto addSuggestion = [&](const wchar_t* severity, const std::wstring& text)
            {
                ss << severity << L" " << text << L"\r\n";
                wroteSuggestion = true;
            };

            if (!hasP1Start)
            {
                addSuggestion(L"[WARN]", L"Add Player 1 start with the left toolbar button 'Set Player Start'. The start should be written into Event 1.");
            }
            if (levelEndCount <= 0)
            {
                addSuggestion(L"[WARN]", L"Add a level exit line with 'Set Level End'. Without it, the map may not be finishable.");
            }
            if (loadObjectsCount <= 0)
            {
                addSuggestion(L"[INFO]", L"No LoadObjects command found. Original maps often load object/sprite resources from Event 1; check Events if enemies/items should appear.");
            }
            if (eventOneTriggerCount > 0)
            {
                if (g_app.gameProfile.kind == GameProfileKind::ZombieMassacre)
                {
                    addSuggestion(L"[INFO]", L"Event 1 trigger usage can be valid for Zombie Massacre. Keep it if the map was imported from that game.");
                }
                else
                {
                    addSuggestion(L"[WARN]", L"Classic Gloom maps usually reserve Event 1 for init/player/object loading. Move normal trigger logic to Event 2 or higher if this was accidental.");
                }
            }
            if (!triggerlessEvents.empty())
            {
                std::wstringstream line;
                line << L"Event slot(s) with commands but no trigger: " << FormatObjectTypeList(triggerlessEvents) << L". Add trigger lines or intentionally move one-shot setup commands to Event 1.";
                addSuggestion(L"[WARN]", line.str());
            }
            if (!emptyTriggeredEvents.empty())
            {
                std::wstringstream line;
                line << L"Trigger line(s) point to empty event slot(s): " << FormatObjectTypeList(emptyTriggeredEvents) << L". Link a target action or delete/change those trigger assignments.";
                addSuggestion(L"[INFO]", line.str());
            }
            if (!invalidTriggerEventZones.empty())
            {
                std::wstringstream line;
                line << L"Trigger/helper line(s) with invalid Event IDs: " << FormatObjectTypeList(invalidTriggerEventZones) << L". Edit those zones and use Event 1-24, or 0 for an inert helper line.";
                addSuggestion(L"[WARN]", line.str());
            }
            if (!invalidZoneReferences.empty())
            {
                std::wstringstream line;
                line << L"Event commands reference missing zones/ranges: " << FormatLimitedWideList(invalidZoneReferences, 5) << L". Re-link the affected event actions or remove the broken commands in Map > Events.";
                addSuggestion(L"[WARN]", line.str());
            }
            if (!objectOutsideMapIssues.empty())
            {
                std::wstringstream line;
                line << L"Object/player spawn positions outside the current map bounds: " << FormatLimitedWideList(objectOutsideMapIssues, 5) << L". Move them with drag-and-drop or reset Player Start.";
                addSuggestion(L"[WARN]", line.str());
            }
            if (textureOutOfCorpusCount > 0)
            {
                std::wstringstream line;
                line << textureOutOfCorpusCount << L" texture reference(s) are outside the shipped 0-79 corpus range. This is fine for custom texture sets; otherwise reassign visible wall bands.";
                addSuggestion(L"[INFO]", line.str());
            }
            if (unusualScCount > 0)
            {
                std::wstringstream line;
                line << unusualScCount << L" wall(s) use unusual texture scale values. Check them visually; keep them if they intentionally create narrow-face texture mapping.";
                addSuggestion(L"[INFO]", line.str());
            }
            if (!nonNeutralMoveGuides.empty())
            {
                std::wstringstream line;
                line << L"Move-Wallblock guide zone(s) " << FormatObjectTypeList(nonNeutralMoveGuides) << L" can be repaired safely: 'Apply Safe Fixes' will make them invisible and AI/collision-neutral.";
                addSuggestion(L"[WARN]", line.str());
            }

            if (!wroteSuggestion)
            {
                ss << L"[OK] No actionable improvement suggestions found.\r\n";
            }
            sections.suggestions = ss.str();
        }

        {
            std::wstringstream ss;
            const auto issues = g_app.document.Validate();
            if (issues.empty())
            {
                ss << L"[OK] MapFormat validation returned no technical issues.\r\n";
            }
            else
            {
                for (const auto& item : issues)
                {
                    if (item == "No issues found." || item == "No issues found")
                    {
                        continue;
                    }
                    const bool autoRecalculatedOnSave = item.find("will be recalculated on save") != std::string::npos;
                    ss << (autoRecalculatedOnSave ? L"[INFO] " : L"[WARN] ") << Utf8ToWide(item) << L"\r\n";
                }
            }
            sections.technical = ss.str();
        }

        sections.okCount = CountValidationTagOccurrences(sections.profile, L"[OK]") +
            CountValidationTagOccurrences(sections.checks, L"[OK]") +
            CountValidationTagOccurrences(sections.technical, L"[OK]");
        sections.infoCount = CountValidationTagOccurrences(sections.profile, L"[INFO]") +
            CountValidationTagOccurrences(sections.checks, L"[INFO]") +
            CountValidationTagOccurrences(sections.technical, L"[INFO]");
        sections.warnCount = CountValidationTagOccurrences(sections.profile, L"[WARN]") +
            CountValidationTagOccurrences(sections.checks, L"[WARN]") +
            CountValidationTagOccurrences(sections.technical, L"[WARN]");
        sections.saveWarnCount = CountValidationTagOccurrences(sections.checks, L"[WARN]") +
            CountValidationTagOccurrences(sections.technical, L"[WARN]");

        std::wstringstream full;
        full << L"Game Profile\r\n" << sections.profile << L"\r\n";
        full << L"Map Intelligence\r\n" << sections.intelligence << L"\r\n";
        full << L"Corpus Checks / Warnings\r\n" << sections.checks << L"\r\n";
        full << L"Suggested Fixes\r\n" << sections.suggestions << L"\r\n";
        full << L"Technical Validation\r\n" << sections.technical;
        sections.full = full.str();
        return sections;
    }

    std::wstring BuildMapValidationReport()
    {
        return BuildMapValidationReportSections().full;
    }

    bool CopyTextToClipboard(HWND owner, const std::wstring& text)
    {
        if (!OpenClipboard(owner)) return false;
        EmptyClipboard();

        const SIZE_T bytes = (text.size() + 1u) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!memory)
        {
            CloseClipboard();
            return false;
        }

        void* target = GlobalLock(memory);
        if (!target)
        {
            GlobalFree(memory);
            CloseClipboard();
            return false;
        }
        std::memcpy(target, text.c_str(), bytes);
        GlobalUnlock(memory);

        if (!SetClipboardData(CF_UNICODETEXT, memory))
        {
            GlobalFree(memory);
            CloseClipboard();
            return false;
        }

        CloseClipboard();
        return true;
    }

    constexpr int kValidationCopyAllControlId = 0x7F01;
    constexpr int kValidationApplySafeFixesControlId = 0x7F02;

    struct ValidationDialogState
    {
        MapValidationReportSections sections;
        HFONT titleFont = nullptr;
        HFONT sectionFont = nullptr;
        HFONT monoFont = nullptr;
        HWND profileEdit = nullptr;
        HWND intelligenceEdit = nullptr;
        HWND checksEdit = nullptr;
        HWND suggestionsEdit = nullptr;
        HWND technicalEdit = nullptr;
        HWND repairButton = nullptr;
        HWND copyButton = nullptr;
        HWND closeButton = nullptr;
    };

    bool EnsureRichEditLoaded()
    {
        static bool attempted = false;
        static HMODULE module = nullptr;
        if (!attempted)
        {
            attempted = true;
            module = LoadLibraryW(L"Msftedit.dll");
        }
        return module != nullptr;
    }

    bool IsRichEditWindow(HWND hwnd)
    {
        wchar_t className[80]{};
        if (!hwnd || !GetClassNameW(hwnd, className, static_cast<int>(sizeof(className) / sizeof(className[0]))))
        {
            return false;
        }
        return wcsstr(className, L"RichEdit") != nullptr || wcscmp(className, MSFTEDIT_CLASS) == 0;
    }

    struct ValidationStatusTagStyle
    {
        const wchar_t* tag;
        COLORREF color;
    };

    void ApplyValidationTagColor(HWND edit, const wchar_t* tag, COLORREF color)
    {
        if (!edit || !tag || !*tag)
        {
            return;
        }

        CHARRANGE searchRange{0, -1};
        FINDTEXTEXW find{};
        find.lpstrText = const_cast<wchar_t*>(tag);

        while (true)
        {
            find.chrg = searchRange;
            const LRESULT found = SendMessageW(edit, EM_FINDTEXTEXW, FR_DOWN, reinterpret_cast<LPARAM>(&find));
            if (found < 0)
            {
                break;
            }

            SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&find.chrgText));
            CHARFORMAT2W format{};
            format.cbSize = sizeof(format);
            format.dwMask = CFM_COLOR;
            format.crTextColor = color;
            SendMessageW(edit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));

            if (find.chrgText.cpMax <= searchRange.cpMin)
            {
                break;
            }
            searchRange.cpMin = find.chrgText.cpMax;
            searchRange.cpMax = -1;
        }
    }

    void ApplyValidationRichTextColors(HWND edit, const std::wstring& /*text*/)
    {
        if (!IsRichEditWindow(edit))
        {
            return;
        }

        SendMessageW(edit, WM_SETREDRAW, FALSE, 0);
        SendMessageW(edit, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(kDarkFieldBg));

        CHARRANGE allRange{0, -1};
        SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&allRange));
        CHARFORMAT2W baseFormat{};
        baseFormat.cbSize = sizeof(baseFormat);
        baseFormat.dwMask = CFM_COLOR;
        baseFormat.crTextColor = kDarkListText;
        SendMessageW(edit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&baseFormat));

        const ValidationStatusTagStyle tagStyles[] =
        {
            { L"[OK]", kValidationOkText },
            { L"[INFO]", kValidationInfoText },
            { L"[WARN]", kValidationWarnText },
        };

        for (const ValidationStatusTagStyle& style : tagStyles)
        {
            ApplyValidationTagColor(edit, style.tag, style.color);
        }

        CHARRANGE caretRange{0, 0};
        SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&caretRange));
        SendMessageW(edit, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(edit, nullptr, TRUE);
    }

    void SetValidationBlockText(HWND edit, const std::wstring& text)
    {
        if (!edit) return;
        SetWindowTextW(edit, text.c_str());
        ApplyValidationRichTextColors(edit, text);
    }

    HWND CreateValidationBlock(HWND parent, const std::wstring& text, HFONT font)
    {
        const wchar_t* className = EnsureRichEditLoaded() ? MSFTEDIT_CLASS : L"EDIT";
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, className, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_NOHIDESEL | WS_VSCROLL,
            0, 0, 10, 10, parent, nullptr, g_app.instance, nullptr);
        if (edit)
        {
            SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT))), TRUE);
            SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
            SetValidationBlockText(edit, text);
        }
        return edit;
    }

    void SetValidationChildFont(HWND child, HFONT font)
    {
        if (child)
        {
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT))), TRUE);
        }
    }

    void UpdateValidationRepairButton(ValidationDialogState* state)
    {
        if (!state || !state->repairButton) return;
        EnableWindow(state->repairButton, state->sections.HasSafeRepairs() ? TRUE : FALSE);
        SetWindowTextW(state->repairButton, state->sections.HasSafeRepairs() ? L"Apply Safe Fixes" : L"No Safe Fixes");
    }

    bool ApplySafeValidationRepairsNoPrompt(const MapValidationReportSections& sections, bool pushUndoSnapshot)
    {
        if (!sections.HasSafeRepairs())
        {
            return false;
        }

        if (pushUndoSnapshot)
        {
            PushUndoSnapshot();
        }

        int fixedCount = 0;
        for (int zoneIndex : sections.safeNeutralGuideZones)
        {
            if (zoneIndex >= 0 && zoneIndex < static_cast<int>(g_app.document.zones.size()))
            {
                MakeMoveWallGroupGuideAiNeutral(g_app.document.zones[zoneIndex]);
                ++fixedCount;
            }
        }

        if (fixedCount <= 0)
        {
            return false;
        }

        MarkDirty();
        RefreshPreviewImage();
        RefreshZoneList();
        RefreshStatus();
        UpdateModeButtons();
        InvalidateEditorViews();
        return true;
    }

    std::vector<std::wstring> CollectSaveValidationWarningLines(const MapValidationReportSections& sections)
    {
        std::vector<std::wstring> warnings;
        auto collect = [&](const std::wstring& block)
        {
            std::wstringstream input(block);
            std::wstring line;
            while (std::getline(input, line))
            {
                while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n')) line.pop_back();
                if (line.find(L"[WARN]") != std::wstring::npos &&
                    line.find(L"No issues found") == std::wstring::npos)
                {
                    warnings.push_back(line);
                }
            }
        };
        collect(sections.checks);
        collect(sections.technical);
        return warnings;
    }

    std::wstring BuildSaveValidationWarningSummary(const MapValidationReportSections& sections)
    {
        const std::vector<std::wstring> warnings = CollectSaveValidationWarningLines(sections);
        if (warnings.empty())
        {
            return L"No save-blocking validation issues found.";
        }

        std::wstringstream ss;
        const size_t shown = MinValue<size_t>(warnings.size(), 7u);
        for (size_t i = 0; i < shown; ++i)
        {
            ss << L"- " << warnings[i] << L"\n";
        }
        if (warnings.size() > shown)
        {
            ss << L"- ... +" << (warnings.size() - shown) << L" more warning(s)\n";
        }
        if (sections.HasSafeRepairs())
        {
            ss << L"\nSafe fixes available: Move-Wallblock target guides will be made invisible and AI/collision-neutral.";
        }
        else
        {
            ss << L"\nNo clearly safe automatic fixes are available. Please review the Validate Map window.";
        }
        return ss.str();
    }

    enum class SaveValidationAction
    {
        Cancel,
        SaveAnyway,
        SaveWithSafeFixes,
    };

    constexpr int kSaveValidationSaveFixesControlId = 0x7F11;
    constexpr int kSaveValidationSaveAnywayControlId = 0x7F12;
    constexpr int kSaveValidationCancelControlId = 0x7F13;

    struct SaveValidationDialogState
    {
        const MapValidationReportSections* sections = nullptr;
        std::wstring bodyText;
        SaveValidationAction action = SaveValidationAction::Cancel;
        HFONT titleFont = nullptr;
        HFONT monoFont = nullptr;
        HWND bodyEdit = nullptr;
        HWND fixButton = nullptr;
        HWND saveAnywayButton = nullptr;
        HWND cancelButton = nullptr;
    };

    void SetSaveValidationDialogActionAndClose(HWND hwnd, SaveValidationDialogState* state, SaveValidationAction action)
    {
        if (state)
        {
            state->action = action;
        }
        DestroyWindow(hwnd);
    }

    LRESULT CALLBACK SaveValidationWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<SaveValidationDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (msg)
        {
        case WM_NCCREATE:
            {
                auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
                return TRUE;
            }

        case WM_CREATE:
            {
                state = reinterpret_cast<SaveValidationDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                if (!state || !state->sections)
                {
                    return -1;
                }

                HFONT defaultFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                {
                    LOGFONTW titleLf{};
                    if (defaultFont)
                    {
                        GetObjectW(defaultFont, sizeof(titleLf), &titleLf);
                    }
                    titleLf.lfHeight = -18;
                    titleLf.lfWeight = FW_SEMIBOLD;
                    wcscpy_s(titleLf.lfFaceName, L"Segoe UI");
                    state->titleFont = CreateFontIndirectW(&titleLf);
                }
                state->monoFont = CreateFontW(
                    -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                    FIXED_PITCH | FF_MODERN, L"Consolas");

                HWND icon = CreateWindowW(L"STATIC", nullptr,
                    WS_CHILD | WS_VISIBLE | SS_ICON,
                    22, 22, 42, 42, hwnd, nullptr, g_app.instance, nullptr);
                if (icon)
                {
                    HICON appIcon = LoadIconW(nullptr, IDI_WARNING);
                    SendMessageW(icon, STM_SETICON, reinterpret_cast<WPARAM>(appIcon), 0);
                }

                HWND title = CreateWindowW(L"STATIC", L"Validate before Save",
                    WS_CHILD | WS_VISIBLE,
                    82, 20, 500, 26, hwnd, nullptr, g_app.instance, nullptr);
                HWND subtitle = CreateWindowW(L"STATIC", L"The map has warnings. Please choose how it should be saved.",
                    WS_CHILD | WS_VISIBLE,
                    82, 50, 560, 20, hwnd, nullptr, g_app.instance, nullptr);

                CreateWindowW(L"STATIC", nullptr,
                    WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                    22, 84, 626, 2, hwnd, nullptr, g_app.instance, nullptr);

                state->bodyEdit = CreateValidationBlock(hwnd, state->bodyText, state->monoFont ? state->monoFont : defaultFont);
                MoveWindow(state->bodyEdit, 22, 102, 626, 190, TRUE);

                CreateWindowW(L"STATIC", nullptr,
                    WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                    22, 310, 626, 2, hwnd, nullptr, g_app.instance, nullptr);

                const bool hasSafeRepairs = state->sections->HasSafeRepairs();
                state->fixButton = CreateWindowW(L"BUTTON", hasSafeRepairs ? L"Save with Fixes" : L"No Safe Fixes",
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    22, 328, 202, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSaveValidationSaveFixesControlId)), g_app.instance, nullptr);
                state->saveAnywayButton = CreateWindowW(L"BUTTON", L"Save Anyway",
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    238, 328, 176, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSaveValidationSaveAnywayControlId)), g_app.instance, nullptr);
                state->cancelButton = CreateWindowW(L"BUTTON", L"Cancel",
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    526, 328, 122, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSaveValidationCancelControlId)), g_app.instance, nullptr);

                SetValidationChildFont(title, state->titleFont ? state->titleFont : defaultFont);
                SetValidationChildFont(subtitle, defaultFont);
                SetValidationChildFont(state->fixButton, defaultFont);
                SetValidationChildFont(state->saveAnywayButton, defaultFont);
                SetValidationChildFont(state->cancelButton, defaultFont);

                EnableWindow(state->fixButton, hasSafeRepairs ? TRUE : FALSE);
                ApplyEditorDarkModeToWindowTree(hwnd);
                SetFocus(hasSafeRepairs ? state->fixButton : state->saveAnywayButton);
                return 0;
            }

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case kSaveValidationSaveFixesControlId:
                if (state && state->sections && state->sections->HasSafeRepairs())
                {
                    SetSaveValidationDialogActionAndClose(hwnd, state, SaveValidationAction::SaveWithSafeFixes);
                }
                return 0;
            case kSaveValidationSaveAnywayControlId:
                SetSaveValidationDialogActionAndClose(hwnd, state, SaveValidationAction::SaveAnyway);
                return 0;
            case kSaveValidationCancelControlId:
            case IDCANCEL:
            case IDOK:
                SetSaveValidationDialogActionAndClose(hwnd, state, SaveValidationAction::Cancel);
                return 0;
            }
            break;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE)
            {
                SetSaveValidationDialogActionAndClose(hwnd, state, SaveValidationAction::Cancel);
                return 0;
            }
            break;

        case WM_DRAWITEM:
            return DrawDarkOwnerDrawControl(reinterpret_cast<DRAWITEMSTRUCT*>(lParam)) ? TRUE : 0;

        case WM_CTLCOLORSTATIC:
            if (state && reinterpret_cast<HWND>(lParam) == state->bodyEdit)
            {
                HDC dc = reinterpret_cast<HDC>(wParam);
                SetBkMode(dc, OPAQUE);
                SetBkColor(dc, kDarkFieldBg);
                SetTextColor(dc, kDarkListText);
                return reinterpret_cast<LRESULT>(DarkFieldBrush());
            }
            return HandleDarkCtlColor(msg, wParam);

        case WM_CTLCOLORDLG:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            return HandleDarkCtlColor(msg, wParam);

        case WM_CLOSE:
            SetSaveValidationDialogActionAndClose(hwnd, state, SaveValidationAction::Cancel);
            return 0;

        case WM_DESTROY:
            if (state)
            {
                if (state->titleFont)
                {
                    DeleteObject(state->titleFont);
                    state->titleFont = nullptr;
                }
                if (state->monoFont)
                {
                    DeleteObject(state->monoFont);
                    state->monoFont = nullptr;
                }
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    SaveValidationAction AskSaveValidationAction(const MapValidationReportSections& sections)
    {
        SaveValidationDialogState state{};
        state.sections = &sections;
        state.bodyText = L"Validate Map reports " + std::to_wstring(sections.saveWarnCount) +
            L" save warning(s).\r\n\r\n" + BuildSaveValidationWarningSummary(sections);

        const wchar_t* className = L"ZGloomEditorSaveValidationWindow";
        static bool registered = false;
        if (!registered)
        {
            WNDCLASSW wc{};
            wc.lpfnWndProc = SaveValidationWndProc;
            wc.hInstance = g_app.instance;
            wc.hIcon = LoadIconW(g_app.instance, MAKEINTRESOURCEW(IDI_APP_ICON));
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = DarkWindowBrush();
            wc.lpszClassName = className;
            RegisterClassW(&wc);
            registered = true;
        }

        RECT ownerRect{};
        GetWindowRect(g_app.mainWindow, &ownerRect);
        const int width = 690;
        const int height = 410;
        const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
        const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;

        HWND dialog = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
            className,
            L"Validate before Save",
            WS_CAPTION | WS_SYSMENU | WS_POPUP,
            x, y, width, height,
            g_app.mainWindow, nullptr, g_app.instance, &state);
        if (!dialog)
        {
            std::wstringstream fallback;
            fallback << L"Validate Map reports " << sections.saveWarnCount << L" save warning(s).\n\n"
                     << BuildSaveValidationWarningSummary(sections) << L"\n\n";
            if (sections.HasSafeRepairs())
            {
                fallback << L"Yes = Save with Fixes\n"
                         << L"No = Save Anyway\n"
                         << L"Cancel = Do Not Save";
                const int result = MessageBoxW(g_app.mainWindow, fallback.str().c_str(), L"Validate before Save", MB_YESNOCANCEL | MB_ICONWARNING);
                if (result == IDYES) return SaveValidationAction::SaveWithSafeFixes;
                if (result == IDNO) return SaveValidationAction::SaveAnyway;
                return SaveValidationAction::Cancel;
            }

            fallback << L"Yes = Save Anyway\n"
                     << L"No = Do Not Save";
            return MessageBoxW(g_app.mainWindow, fallback.str().c_str(), L"Validate before Save", MB_YESNO | MB_ICONWARNING) == IDYES
                ? SaveValidationAction::SaveAnyway
                : SaveValidationAction::Cancel;
        }

        ApplyEditorDarkModeToWindowTree(dialog);
        EnableWindow(g_app.mainWindow, FALSE);
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

        EnableWindow(g_app.mainWindow, TRUE);
        SetForegroundWindow(g_app.mainWindow);
        return state.action;
    }


    bool ConfirmSaveWithValidationAndMaybeFixes(const std::string& /*path*/)
    {
        if (!HasMapContentForValidation())
        {
            return true;
        }

        MapValidationReportSections sections = BuildMapValidationReportSections();
        if (!sections.HasSaveWarnings())
        {
            return true;
        }

        const SaveValidationAction action = AskSaveValidationAction(sections);
        if (action == SaveValidationAction::Cancel)
        {
            return false;
        }
        if (action == SaveValidationAction::SaveWithSafeFixes)
        {
            ApplySafeValidationRepairsNoPrompt(sections, true);
        }
        return true;
    }

    bool ApplySafeValidationRepairs(HWND owner, ValidationDialogState* state)
    {
        if (!state || !state->sections.HasSafeRepairs())
        {
            return false;
        }

        std::wstringstream question;
        question << L"Apply safe repairs now?\n\n"
                 << L"This will make these Move-Wallblock target guide zones invisible and AI/collision-neutral:\n"
                 << FormatObjectTypeList(state->sections.safeNeutralGuideZones)
                 << L"\n\nOther warnings will only stay as suggestions because they need a design decision.";
        if (MessageBoxW(owner, question.str().c_str(), L"Validate Map", MB_YESNO | MB_ICONQUESTION) != IDYES)
        {
            return false;
        }

        return ApplySafeValidationRepairsNoPrompt(state->sections, true);
    }

    void RefreshValidationDialogBlocks(ValidationDialogState* state)
    {
        if (!state) return;
        SetValidationBlockText(state->profileEdit, state->sections.profile);
        SetValidationBlockText(state->intelligenceEdit, state->sections.intelligence);
        SetValidationBlockText(state->checksEdit, state->sections.checks);
        SetValidationBlockText(state->suggestionsEdit, state->sections.suggestions);
        SetValidationBlockText(state->technicalEdit, state->sections.technical);
        UpdateValidationRepairButton(state);
    }

    LRESULT CALLBACK ValidationWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<ValidationDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (msg)
        {
        case WM_NCCREATE:
            {
                auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
                return TRUE;
            }

        case WM_CREATE:
            {
                state = reinterpret_cast<ValidationDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                if (!state) return -1;

                EnableEditorDarkModeForWindow(hwnd);
                HFONT defaultFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

                LOGFONTW lf{};
                if (defaultFont && GetObjectW(defaultFont, sizeof(lf), &lf) == sizeof(lf))
                {
                    LOGFONTW titleLf = lf;
                    titleLf.lfWeight = FW_SEMIBOLD;
                    titleLf.lfHeight = lf.lfHeight != 0 ? lf.lfHeight - 3 : -16;
                    state->titleFont = CreateFontIndirectW(&titleLf);

                    LOGFONTW sectionLf = lf;
                    sectionLf.lfWeight = FW_SEMIBOLD;
                    sectionLf.lfHeight = lf.lfHeight != 0 ? lf.lfHeight - 2 : -15;
                    state->sectionFont = CreateFontIndirectW(&sectionLf);

                    LOGFONTW monoLf = lf;
                    monoLf.lfWeight = FW_NORMAL;
                    wcscpy_s(monoLf.lfFaceName, L"Consolas");
                    state->monoFont = CreateFontIndirectW(&monoLf);
                }

                HWND icon = CreateWindowW(L"STATIC", nullptr,
                    WS_CHILD | WS_VISIBLE | SS_ICON,
                    22, 18, 48, 48, hwnd, nullptr, g_app.instance, nullptr);
                HICON appIcon = static_cast<HICON>(LoadImageW(g_app.instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR));
                if (!appIcon)
                {
                    appIcon = LoadIconW(g_app.instance, MAKEINTRESOURCEW(IDI_APP_ICON));
                }
                SendMessageW(icon, STM_SETICON, reinterpret_cast<WPARAM>(appIcon), 0);

                HWND title = CreateWindowW(L"STATIC", L"Validate Map",
                    WS_CHILD | WS_VISIBLE,
                    86, 20, 700, 24, hwnd, nullptr, g_app.instance, nullptr);
                HWND subtitle = CreateWindowW(L"STATIC", L"Map intelligence, corpus checks and technical validation",
                    WS_CHILD | WS_VISIBLE,
                    86, 48, 700, 20, hwnd, nullptr, g_app.instance, nullptr);

                CreateWindowW(L"STATIC", nullptr,
                    WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                    22, 84, 830, 2, hwnd, nullptr, g_app.instance, nullptr);

                HWND profileHeading = CreateWindowW(L"STATIC", L"Game Profile",
                    WS_CHILD | WS_VISIBLE,
                    22, 102, 830, 22, hwnd, nullptr, g_app.instance, nullptr);
                state->profileEdit = CreateValidationBlock(hwnd, state->sections.profile, state->monoFont ? state->monoFont : defaultFont);

                HWND intelligenceHeading = CreateWindowW(L"STATIC", L"Map Intelligence",
                    WS_CHILD | WS_VISIBLE,
                    22, 194, 830, 22, hwnd, nullptr, g_app.instance, nullptr);
                state->intelligenceEdit = CreateValidationBlock(hwnd, state->sections.intelligence, state->monoFont ? state->monoFont : defaultFont);

                HWND checksHeading = CreateWindowW(L"STATIC", L"Corpus Checks / Warnings",
                    WS_CHILD | WS_VISIBLE,
                    22, 342, 830, 22, hwnd, nullptr, g_app.instance, nullptr);
                state->checksEdit = CreateValidationBlock(hwnd, state->sections.checks, state->monoFont ? state->monoFont : defaultFont);

                HWND suggestionsHeading = CreateWindowW(L"STATIC", L"Suggested Fixes",
                    WS_CHILD | WS_VISIBLE,
                    22, 496, 830, 22, hwnd, nullptr, g_app.instance, nullptr);
                state->suggestionsEdit = CreateValidationBlock(hwnd, state->sections.suggestions, state->monoFont ? state->monoFont : defaultFont);

                HWND techHeading = CreateWindowW(L"STATIC", L"Technical Validation",
                    WS_CHILD | WS_VISIBLE,
                    22, 626, 830, 22, hwnd, nullptr, g_app.instance, nullptr);
                state->technicalEdit = CreateValidationBlock(hwnd, state->sections.technical, state->monoFont ? state->monoFont : defaultFont);

                CreateWindowW(L"STATIC", nullptr,
                    WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                    22, 722, 830, 2, hwnd, nullptr, g_app.instance, nullptr);

                state->repairButton = CreateWindowW(L"BUTTON", state->sections.HasSafeRepairs() ? L"Apply Safe Fixes" : L"No Safe Fixes",
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    446, 740, 150, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kValidationApplySafeFixesControlId)), g_app.instance, nullptr);
                state->copyButton = CreateWindowW(L"BUTTON", L"Copy All",
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    610, 740, 104, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kValidationCopyAllControlId)), g_app.instance, nullptr);
                state->closeButton = CreateWindowW(L"BUTTON", L"OK",
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    728, 740, 124, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), g_app.instance, nullptr);

                SetValidationChildFont(title, state->titleFont ? state->titleFont : defaultFont);
                SetValidationChildFont(subtitle, defaultFont);
                SetValidationChildFont(profileHeading, state->sectionFont ? state->sectionFont : defaultFont);
                SetValidationChildFont(intelligenceHeading, state->sectionFont ? state->sectionFont : defaultFont);
                SetValidationChildFont(checksHeading, state->sectionFont ? state->sectionFont : defaultFont);
                SetValidationChildFont(suggestionsHeading, state->sectionFont ? state->sectionFont : defaultFont);
                SetValidationChildFont(techHeading, state->sectionFont ? state->sectionFont : defaultFont);
                SetValidationChildFont(state->repairButton, defaultFont);
                SetValidationChildFont(state->copyButton, defaultFont);
                SetValidationChildFont(state->closeButton, defaultFont);

                MoveWindow(state->profileEdit, 22, 128, 830, 52, TRUE);
                MoveWindow(state->intelligenceEdit, 22, 220, 830, 108, TRUE);
                MoveWindow(state->checksEdit, 22, 368, 830, 114, TRUE);
                MoveWindow(state->suggestionsEdit, 22, 522, 830, 90, TRUE);
                MoveWindow(state->technicalEdit, 22, 652, 830, 58, TRUE);

                ApplyEditorDarkModeToWindowTree(hwnd);
                UpdateValidationRepairButton(state);
                SetFocus(state->closeButton);
                return 0;
            }

        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wParam) == kValidationApplySafeFixesControlId)
            {
                if (state && ApplySafeValidationRepairs(hwnd, state))
                {
                    state->sections = BuildMapValidationReportSections();
                    RefreshValidationDialogBlocks(state);
                    SetWindowTextW(state->repairButton, L"Fixed");
                    UpdateValidationRepairButton(state);
                }
                return 0;
            }
            if (LOWORD(wParam) == kValidationCopyAllControlId)
            {
                if (state && CopyTextToClipboard(hwnd, state->sections.full))
                {
                    SetWindowTextW(state->copyButton, L"Copied");
                }
                else
                {
                    MessageBoxW(hwnd, L"Could not copy the validation report to the clipboard.", L"Validate Map", MB_OK | MB_ICONWARNING);
                }
                return 0;
            }
            break;

        case WM_DRAWITEM:
            return DrawDarkOwnerDrawControl(reinterpret_cast<DRAWITEMSTRUCT*>(lParam)) ? TRUE : 0;

        case WM_CTLCOLORSTATIC:
            if (state)
            {
                HWND child = reinterpret_cast<HWND>(lParam);
                if (child == state->profileEdit || child == state->intelligenceEdit ||
                    child == state->checksEdit || child == state->suggestionsEdit || child == state->technicalEdit)
                {
                    HDC dc = reinterpret_cast<HDC>(wParam);
                    SetBkMode(dc, OPAQUE);
                    SetBkColor(dc, kDarkFieldBg);
                    SetTextColor(dc, kDarkListText);
                    return reinterpret_cast<LRESULT>(DarkFieldBrush());
                }
            }
            return HandleDarkCtlColor(msg, wParam);

        case WM_CTLCOLORDLG:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            return HandleDarkCtlColor(msg, wParam);

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (state)
            {
                if (state->titleFont) DeleteObject(state->titleFont);
                if (state->sectionFont) DeleteObject(state->sectionFont);
                if (state->monoFont) DeleteObject(state->monoFont);
                delete state;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void ShowValidationReport()
    {
        auto* state = new ValidationDialogState();
        state->sections = BuildMapValidationReportSections();

        const wchar_t* className = L"ZGloomEditorValidationWindow";
        static bool registered = false;
        if (!registered)
        {
            WNDCLASSW wc{};
            wc.lpfnWndProc = ValidationWndProc;
            wc.hInstance = g_app.instance;
            wc.hIcon = LoadIconW(g_app.instance, MAKEINTRESOURCEW(IDI_APP_ICON));
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = DarkWindowBrush();
            wc.lpszClassName = className;
            RegisterClassW(&wc);
            registered = true;
        }

        RECT ownerRect{};
        GetWindowRect(g_app.mainWindow, &ownerRect);
        const int width = 890;
        const int height = 820;
        const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
        const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;

        HWND dialog = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
            className,
            L"Validate Map",
            WS_CAPTION | WS_SYSMENU | WS_POPUP,
            x, y, width, height,
            g_app.mainWindow, nullptr, g_app.instance, state);
        if (!dialog)
        {
            const std::wstring fallback = state->sections.full;
            delete state;
            MessageBoxW(g_app.mainWindow, fallback.c_str(), L"Validate Map", MB_OK | MB_ICONINFORMATION);
            return;
        }

        ApplyEditorDarkModeToWindowTree(dialog);
        EnableWindow(g_app.mainWindow, FALSE);
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

        EnableWindow(g_app.mainWindow, TRUE);
        SetForegroundWindow(g_app.mainWindow);
    }

    void ApplyGreyMenuBackground(HMENU menu)
    {
        if (!menu)
        {
            return;
        }

        MENUINFO mi{};
        mi.cbSize = sizeof(mi);
        mi.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
        mi.hbrBack = DarkPanelBrush();
        SetMenuInfo(menu, &mi);
    }

    std::wstring MenuTextWithoutShortcut(const std::wstring& text)
    {
        const size_t tab = text.find(L'\t');
        return tab == std::wstring::npos ? text : text.substr(0, tab);
    }

    std::wstring MenuShortcutText(const std::wstring& text)
    {
        const size_t tab = text.find(L'\t');
        return tab == std::wstring::npos ? std::wstring() : text.substr(tab + 1);
    }

    void MeasureOwnerDrawMenuItem(MEASUREITEMSTRUCT* measure)
    {
        if (!measure || measure->CtlType != ODT_MENU || !measure->itemData)
        {
            return;
        }

        const auto* item = reinterpret_cast<const OwnerDrawMenuItem*>(measure->itemData);
        if (item->separator)
        {
            measure->itemWidth = 1;
            measure->itemHeight = 10;
            return;
        }

        HDC hdc = GetDC(g_app.mainWindow ? g_app.mainWindow : nullptr);
        HFONT oldFont = nullptr;
        if (hdc)
        {
            oldFont = reinterpret_cast<HFONT>(SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT)));
        }

        RECT rcLeft{0, 0, 0, 0};
        std::wstring left = MenuTextWithoutShortcut(item->text);
        DrawTextW(hdc, left.c_str(), -1, &rcLeft, DT_CALCRECT | DT_SINGLELINE);

        RECT rcRight{0, 0, 0, 0};
        std::wstring right = MenuShortcutText(item->text);
        if (!right.empty())
        {
            DrawTextW(hdc, right.c_str(), -1, &rcRight, DT_CALCRECT | DT_SINGLELINE);
        }

        if (hdc)
        {
            if (oldFont)
            {
                SelectObject(hdc, oldFont);
            }
            ReleaseDC(g_app.mainWindow ? g_app.mainWindow : nullptr, hdc);
        }

        const int textWidth = (rcLeft.right - rcLeft.left) + (right.empty() ? 0 : 42 + (rcRight.right - rcRight.left));
        if (item->topLevel)
        {
            measure->itemWidth = MaxValue(48U, static_cast<UINT>(textWidth + 22));
            measure->itemHeight = static_cast<UINT>(MaxValue(22, GetSystemMetrics(SM_CYMENU)));
        }
        else
        {
            measure->itemWidth = static_cast<UINT>(MaxValue(230, textWidth + 92));
            measure->itemHeight = static_cast<UINT>(MaxValue(26, GetSystemMetrics(SM_CYMENUSIZE) + 4));
        }
    }

    // Font Awesome Free 6.7.2 Classic Solid icon masks.
    // Source SVG icons: https://fontawesome.com/license/free
    // Icons are embedded here as scaled 16x16 alpha masks only; no font files are included.
    static const unsigned char kFa_file[256] = {
          0,   0,  92, 255, 255, 255, 255, 255, 255,   0, 160,   0,   0,   0,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255,   0, 255, 160,   0,   0,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255,   0, 255, 255, 170,   0,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255,   0, 255, 255, 255, 170,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255,  73,   0,   0,   0,   0,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,   0,
          0,   0,  91, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  90,   0,   0
    };
    static const unsigned char kFa_folder_open[256] = {
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,  50,  63,  63,  63,  38,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        185, 255, 255, 255, 255, 255, 156,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        255, 255, 255, 255, 255, 255, 255, 221, 150, 148, 148, 140,  47,   0,   0,   0,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  33,   0,   0,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 102,   0,   0,
        255, 255, 255, 115,  63,  63,  63,  63,  63,  63,  63,  63,  63,  21,   0,   0,
        255, 255,  76, 200, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 220,
        255, 207, 101, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255,  46, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  87,
        152, 155, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 215,   0,
         45, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  41,   0,
        206, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 162,   0,   0,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  12,   0,   0,
         31,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  25,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
    };
    static const unsigned char kFa_xmark[256] = {
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0, 245, 253,  27,   0,   0,   0,   0,  27, 255, 243,   0,   0,   0,
          0,   0,   0, 255, 255, 255,  27,   0,   0,  27, 255, 255, 253,   0,   0,   0,
          0,   0,   0,  26, 255, 255, 255,  31,  26, 255, 255, 255,  30,   0,   0,   0,
          0,   0,   0,   0,  27, 255, 255, 255, 255, 255, 255,  27,   0,   0,   0,   0,
          0,   0,   0,   0,   0,  26, 255, 255, 255, 255,  30,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,  26, 255, 255, 255, 255,  31,   0,   0,   0,   0,   0,
          0,   0,   0,   0,  27, 255, 255, 255, 255, 255, 255,  27,   0,   0,   0,   0,
          0,   0,   0,  26, 255, 255, 255,  30,  26, 255, 255, 255,  31,   0,   0,   0,
          0,   0,   0, 255, 255, 255,  27,   0,   0,  27, 255, 255, 253,   0,   0,   0,
          0,   0,   0, 243, 253,  27,   0,   0,   0,   0,  27, 253, 242,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
    };
    static const unsigned char kFa_floppy_disk[256] = {
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0, 102, 255, 255, 255, 255, 255, 255, 255, 255, 255, 238,  27,   0,   0,   0,
          0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  27,   0,   0,
          0, 255, 255,  65,   0,   0,   0,   0,   0,   0,  67, 255, 255, 255,  27,   0,
          0, 255, 255,   0,   0,   0,   0,   0,   0,   0,   0, 255, 255, 255, 237,   0,
          0, 255, 255,   0,   0,   0,   0,   0,   0,   0,   0, 255, 255, 255, 255,   0,
          0, 255, 255,  67,   0,   0,   0,   0,   0,   0,  71, 255, 255, 255, 255,   0,
          0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,
          0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,
          0, 255, 255, 255, 255, 255, 216,  25,  26, 218, 255, 255, 255, 255, 255,   0,
          0, 255, 255, 255, 255, 255,  25,   0,   0,  28, 255, 255, 255, 255, 255,   0,
          0, 255, 255, 255, 255, 255,  26,   0,   0,  30, 255, 255, 255, 255, 255,   0,
          0, 255, 255, 255, 255, 255, 218,  27,  28, 221, 255, 255, 255, 255, 255,   0,
          0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,
          0, 100, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  97,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
    };
    static const unsigned char kFa_file_export[256] = {
          0,  18,  42,  42,  42,  42,  10,   1,   0,   0,   0,   0,   0,   0,   0,   0,
        152, 255, 255, 255, 255, 255,  71, 186,   7,   0,   0,   0,   0,   0,   0,   0,
        255, 255, 255, 255, 255, 255,  71, 255, 222,   7,   0,   0,   0,   0,   0,   0,
        255, 255, 255, 255, 255, 255,  71, 255, 255, 227,  10,   0,   0,   0,   0,   0,
        255, 255, 255, 255, 255, 255,  91, 132, 148, 148,  67,   0,   0,   0,   0,   0,
        255, 255, 255, 255, 255, 255, 255, 212, 212, 212, 142,   0,   0,   0,   0,   0,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 212,   0,  25,  48,   0,   0,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 212,   0, 136, 255,  82,   0,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 198,  42,  46, 240, 255,  82,
        255, 255, 255, 255, 255, 140,   0,   0,   0,   0, 106, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 191,  63,  63, 227, 255, 112,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 212,   0, 123, 255, 112,   0,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 212,   0,  41,  78,   0,   0,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 198,   0,   0,   0,   0,   0,
        151, 255, 255, 255, 255, 255, 255, 255, 255, 255,  66,   0,   0,   0,   0,   0,
          0,  18,  42,  42,  42,  42,  42,  42,  41,   6,   0,   0,   0,   0,   0,   0
    };
    static const unsigned char kFa_gear[256] = {
          0,   0,   0,   0,   0,   0, 231, 255, 255, 232,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,  23, 255, 255, 255, 255,  28,   0,   0,   0,   0,   0,
          0,  11,  92,  15,  10, 170, 255, 255, 255, 255, 171,  10,  16,  92,  11,   0,
          0, 193, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 195,   0,
         62, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  63,
        152, 255, 255, 255, 255, 255, 255, 181, 182, 255, 255, 255, 255, 255, 255, 148,
         11, 220, 255, 255, 255, 255,  35,   0,   0,  36, 255, 255, 255, 255, 220,  11,
          0,   3, 255, 255, 255, 180,   0,   0,   0,   0, 182, 255, 255, 255,   3,   0,
          0,   3, 255, 255, 255, 180,   0,   0,   0,   0, 183, 255, 255, 255,   3,   0,
         10, 218, 255, 255, 255, 255,  36,   0,   0,  37, 255, 255, 255, 255, 220,  11,
        152, 255, 255, 255, 255, 255, 255, 183, 185, 255, 255, 255, 255, 255, 255, 148,
         62, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  58,
          0, 193, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 188,   0,
          0,  10,  92,  15,  10, 171, 255, 255, 255, 255, 166,   8,  15,  91,  10,   0,
          0,   0,   0,   0,   0,  27, 255, 255, 255, 255,  23,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0, 231, 255, 255, 225,   0,   0,   0,   0,   0,   0
    };
    static const unsigned char kFa_right_from_bracket[256] = {
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          7, 171, 255, 255, 255, 245,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        170, 255, 255, 255, 255, 243,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        255, 255,  72,   0,   0,   0,   0,   0,   0,   0, 237, 255,  40,   0,   0,   0,
        255, 255,   0,   0,   0,   0,   0,   0,   0,   0, 255, 255, 255,  40,   0,   0,
        255, 255,   0,   0,   0,   0,   0,   0,   0,   0, 255, 255, 255, 255,  42,   0,
        255, 255,   0,   0,   0, 246, 255, 255, 255, 255, 255, 255, 255, 255, 255,  40,
        255, 255,   0,   0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255,   0,   0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255,   0,   0,   0, 245, 255, 255, 255, 255, 255, 255, 255, 255, 255,  40,
        255, 255,   0,   0,   0,   0,   0,   0,   0,   0, 255, 255, 255, 255,  42,   0,
        255, 255,   0,   0,   0,   0,   0,   0,   0,   0, 255, 255, 255,  40,   0,   0,
        255, 255,  73,   0,   0,   0,   0,   0,   0,   0, 235, 255,  40,   0,   0,   0,
        170, 255, 255, 255, 255, 245,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          7, 170, 255, 255, 255, 243,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
    };
    static const unsigned char kFa_rotate_left[256] = {
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
         40, 117,   0,   0,  27, 166, 255, 255, 255, 255, 165,  27,   0,   0,   0,   0,
        156, 255, 178, 105, 255, 255, 255, 255, 255, 255, 255, 255, 103,   0,   0,   0,
        160, 255, 255, 255, 255, 221,  78,  12,  12,  80, 222, 255, 255, 102,   0,   0,
        160, 255, 255, 255, 245,   0,   0,   0,   0,   0,   0, 152, 255, 255,  25,   0,
        160, 255, 255, 255, 255, 173,   0,   0,   0,   0,   0,   0, 223, 255, 162,   0,
        111, 255, 255, 255, 255, 255,   0,   0,   0,   0,   0,   0,  82, 255, 255,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  15, 255, 255,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  16, 255, 255,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  82, 255, 255,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 225, 255, 161,   0,
          0,   0,  15, 211, 132,   0,   0,   0,   0,   0,   0, 153, 255, 255,  25,   0,
          0,   0,  48, 255, 255, 223,  81,  13,  15,  82, 225, 255, 255, 101,   0,   0,
          0,   0,   0, 102, 255, 255, 255, 255, 255, 255, 255, 255, 101,   0,   0,   0,
          0,   0,   0,   0,  25, 162, 255, 255, 255, 255, 161,  25,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
    };
    static const unsigned char kFa_rotate_right[256] = {
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,  27, 165, 255, 255, 255, 255, 163,  26,   0,   0, 117,  40,
          0,   0,   0, 103, 255, 255, 255, 255, 255, 255, 255, 255, 102, 178, 255, 156,
          0,   0, 103, 255, 255, 220,  78,  12,  12,  80, 221, 255, 255, 255, 255, 160,
          0,  27, 255, 255, 151,   0,   0,   0,   0,   0,   0, 245, 255, 255, 255, 160,
          0, 165, 255, 222,   0,   0,   0,   0,   0,   0, 173, 255, 255, 255, 255, 160,
          0, 255, 255,  80,   0,   0,   0,   0,   0,   0, 255, 255, 255, 255, 255, 110,
          0, 255, 255,  13,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0, 255, 255,  15,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0, 255, 255,  80,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0, 163, 255, 223,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,  26, 255, 255, 153,   0,   0,   0,   0,   0,   0, 133, 211,  15,   0,   0,
          0,   0, 102, 255, 255, 223,  81,  13,  15,  82, 225, 255, 255,  47,   0,   0,
          0,   0,   0, 102, 255, 255, 255, 255, 255, 255, 255, 255, 101,   0,   0,   0,
          0,   0,   0,   0,  25, 162, 255, 255, 255, 255, 161,  25,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
    };
    static const unsigned char kFa_pen_to_square[256] = {
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 147, 248, 147,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 132, 255, 255, 255, 147,
          7, 171, 255, 255, 255, 255, 245,   0,   0,   0, 132,  27, 240, 255, 255, 250,
        170, 255, 255, 255, 255, 255, 243,   0,   0, 178, 255, 240,  27, 240, 255, 145,
        255, 255,  72,   0,   0,   0,   0,   0, 178, 255, 255, 255, 240,  27, 131,   0,
        255, 255,   0,   0,   0,   0,   0, 177, 255, 255, 255, 255, 255, 132,   0,   0,
        255, 255,   0,   0,   0,   0, 168, 255, 255, 255, 255, 255, 178,   0,   0,   0,
        255, 255,   0,   0,   0, 166, 255, 255, 255, 255, 255, 178,   0,   0,   0,   0,
        255, 255,   0,   0,  40, 255, 255, 255, 255, 255, 177,   0,   0,   0,   0,   0,
        255, 255,   0,   0, 145, 255, 255, 255, 255, 177,   0,   0, 246, 245,   0,   0,
        255, 255,   0,   0, 251, 255, 255, 255, 166,   0,   0,   0, 255, 255,   0,   0,
        255, 255,   0,   0, 255, 251, 145,  40,   0,   0,   0,   0, 255, 255,   0,   0,
        255, 255,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 255, 255,   0,   0,
        255, 255,  73,   0,   0,   0,   0,   0,   0,   0,   0,  75, 255, 255,   0,   0,
        170, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 168,   0,   0,
          7, 170, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 170,   7,   0,   0
    };
    static const unsigned char kFa_trash_can[256] = {
          0,   0,   0,   0,   0, 210, 255, 255, 255, 255, 210,   0,   0,   0,   0,   0,
          0, 246, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 245,   0,
          0, 245, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 243,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,   0,
          0,   0, 255, 255, 182, 182, 255, 182, 182, 255, 182, 182, 255, 255,   0,   0,
          0,   0, 255, 255, 160, 160, 255, 160, 160, 255, 160, 160, 255, 255,   0,   0,
          0,   0, 255, 255, 160, 160, 255, 160, 160, 255, 160, 160, 255, 255,   0,   0,
          0,   0, 255, 255, 160, 160, 255, 160, 160, 255, 160, 160, 255, 255,   0,   0,
          0,   0, 255, 255, 160, 160, 255, 160, 160, 255, 160, 160, 255, 255,   0,   0,
          0,   0, 255, 255, 160, 160, 255, 160, 160, 255, 160, 160, 255, 255,   0,   0,
          0,   0, 255, 255, 160, 160, 255, 160, 160, 255, 160, 160, 255, 255,   0,   0,
          0,   0, 255, 255, 182, 182, 255, 182, 182, 255, 182, 182, 255, 255,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,   0,
          0,   0,  91, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  90,   0,   0
    };
    static const unsigned char kFa_link[256] = {
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,  18, 166, 255, 252, 151,   6,   0,
          0,   0,   0,   0,   0,   0,  27,  16,   0, 138, 255, 255, 255, 255, 221,   0,
          0,   0,   0,   0,  73, 255, 255, 255, 225,  41,  48,   0,   0, 172, 255,  90,
          0,   0,   0,  98, 255, 255, 165, 188, 255, 255,   7,   0,   0,  33, 255, 157,
          0,   0,  98, 255, 255,  37, 225, 138, 131, 255,  95,   0,   0,  82, 255, 128,
          0,  95, 255, 255,  35,  90, 255, 133,  51, 255, 137,   0,  36, 255, 255,  28,
         31, 255, 255,  35,   0, 140, 255,  48, 135, 255,  86,  36, 255, 255,  95,   0,
        131, 255,  80,   0,   0,  96, 255, 131, 138, 225,  37, 255, 255,  98,   0,   0,
        158, 255,  33,   0,   0,   8, 255, 255, 190, 166, 255, 255,  98,   0,   0,   0,
         91, 255, 172,   0,   0,  50,  41, 225, 255, 255, 255,  73,   0,   0,   0,   0,
          0, 221, 255, 255, 255, 255, 137,   0,  15,  26,   0,   0,   0,   0,   0,   0,
          0,   6, 148, 251, 252, 165,  17,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
    };
    static const unsigned char kFa_toggle_on[256] = {
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,  15,  85, 106, 106, 106, 106, 106, 106,  83,  13,   0,   0,   0,
          0,   6, 166, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 161,   5,   0,
          1, 215, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 211,   1,
        120, 255, 255, 255, 255, 255, 255, 255, 255, 195, 110, 152, 255, 255, 255, 115,
        250, 255, 255, 255, 255, 255, 255, 255, 152,   0,   0,   0,  57, 255, 255, 245,
        255, 255, 255, 255, 255, 255, 255, 255,  18,   0,   0,   0,   0, 235, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255,  20,   0,   0,   0,   0, 236, 255, 255,
        248, 255, 255, 255, 255, 255, 255, 255, 156,   0,   0,   0,  60, 255, 255, 242,
        117, 255, 255, 255, 255, 255, 255, 255, 255, 200, 112, 158, 255, 255, 255, 111,
          1, 212, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 207,   1,
          0,   5, 160, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 156,   3,   0,
          0,   0,   0,  13,  81, 106, 106, 106, 106, 106, 106,  80,  12,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
    };
    static const unsigned char kFa_skull[256] = {
          0,   0,   0,   1,  95, 208, 255, 255, 255, 255, 206,  93,   1,   0,   0,   0,
          0,   0,  48, 251, 255, 255, 255, 255, 255, 255, 255, 255, 247,  45,   0,   0,
          0,  62, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  58,   0,
         16, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  13,
        151, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 147,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 216,  25,  26, 218, 255, 255, 216,  25,  26, 218, 255, 255, 255,
        255, 255, 255,  25,   0,   0,  28, 255, 255,  25,   0,   0,  28, 255, 255, 255,
        255, 255, 255,  26,   0,   0,  30, 255, 255,  26,   0,   0,  30, 255, 255, 255,
        148, 255, 255, 218,  27,  28, 221, 255, 255, 218,  27,  28, 221, 255, 255, 145,
         15, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  13,
          0,  58, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  56,   0,
          0,   0,  45, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  45,   0,   0,
          0,   0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,   0,   0,
          0,   0,   0, 255, 255, 255,  32, 255, 255,  32, 255, 255, 255,   0,   0,   0,
          0,   0,   0, 172, 255, 255,   0, 255, 255,   0, 255, 255, 170,   0,   0,   0
    };
    static const unsigned char kFa_arrows_move[256] = {
          0,   0,   0,   0,   0,   0,  27, 255, 253,  27,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,  26, 255, 255, 255, 255,  31,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0, 255, 255, 255, 255, 255, 253,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0, 243, 253, 255, 255, 253, 242,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0, 255, 255,   0,   0,   0,   0,   0,   0,   0,
          0,  27, 255, 243,   0,   0,   0, 255, 255,   0,   0,   0, 245, 253,  27,   0,
         27, 255, 255, 253,   0,   0,   0, 255, 255,   0,   0,   0, 255, 255, 255,  27,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 253,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 253,
         27, 255, 255, 253,   0,   0,   0, 255, 255,   0,   0,   0, 255, 255, 255,  27,
          0,  27, 253, 242,   0,   0,   0, 255, 255,   0,   0,   0, 243, 253,  27,   0,
          0,   0,   0,   0,   0,   0,   0, 255, 255,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0, 245, 253, 255, 255, 255, 243,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0, 255, 255, 255, 255, 255, 253,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,  26, 255, 255, 255, 255,  30,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,  27, 253, 253,  27,   0,   0,   0,   0,   0,   0
    };
    static const unsigned char kFa_location_crosshairs[256] = {
          0,   0,   0,   0,   0,   0,   0, 253, 251,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0, 255, 255,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,  11, 145, 255, 255, 255, 255, 142,  10,   0,   0,   0,   0,
          0,   0,   0,  38, 255, 255, 255, 255, 255, 255, 255, 255,  36,   0,   0,   0,
          0,   0,  11, 255, 255, 255,  93,  11,  11,  95, 255, 255, 255,  10,   0,   0,
          0,   0, 145, 255, 255,  17,  26, 137, 136,  25,  18, 255, 255, 140,   0,   0,
          0,   0, 255, 255,  93,  27, 255, 255, 255, 255,  25,  97, 255, 253,   0,   0,
        253, 255, 255, 255,  11, 138, 255, 255, 255, 255, 136,  15, 255, 255, 255, 251,
        251, 255, 255, 255,  12, 138, 255, 255, 255, 255, 135,  15, 255, 255, 255, 247,
          0,   0, 255, 255,  95,  25, 255, 255, 255, 255,  23, 100, 255, 252,   0,   0,
          0,   0, 142, 255, 255,  18,  25, 135, 133,  23,  20, 255, 255, 138,   0,   0,
          0,   0,  10, 255, 255, 255,  97,  13,  15, 100, 255, 255, 255,   8,   0,   0,
          0,   0,   0,  36, 255, 255, 255, 255, 255, 255, 255, 255,  33,   0,   0,   0,
          0,   0,   0,   0,  10, 140, 253, 255, 255, 252, 138,   8,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0, 255, 255,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0, 251, 247,   0,   0,   0,   0,   0,   0,   0
    };
    static const unsigned char kFa_check[256] = {
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  27, 255, 243,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  27, 255, 255, 253,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  26, 255, 255, 255,  30,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,  26, 255, 255, 255,  30,   0,   0,
          0, 245, 253,  27,   0,   0,   0,   0,  27, 255, 255, 255,  27,   0,   0,   0,
          0, 255, 255, 255,  27,   0,   0,  27, 255, 255, 255,  27,   0,   0,   0,   0,
          0,  26, 255, 255, 255,  31,  26, 255, 255, 255,  30,   0,   0,   0,   0,   0,
          0,   0,  27, 255, 255, 255, 255, 255, 255,  27,   0,   0,   0,   0,   0,   0,
          0,   0,   0,  26, 255, 255, 255, 255,  30,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,  27, 253, 253,  27,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
    };
    static const unsigned char kFa_table_cells[256] = {
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
         92, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  91,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255,   0,   0,  80, 255, 160,   0,   0, 238, 255,  80,   0,   0, 255, 255,
        255, 255,   0,   0,  80, 255, 160,   0,   0, 238, 255,  80,   0,   0, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255,   0,   0,  80, 255, 160,   0,   0, 238, 255,  80,   0,   0, 255, 255,
        255, 255,   0,   0,  80, 255, 160,   0,   0, 238, 255,  80,   0,   0, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255,   0,   0,  80, 255, 160,   0,   0, 238, 255,  80,   0,   0, 255, 255,
        255, 255,   0,   0,  80, 255, 160,   0,   0, 238, 255,  80,   0,   0, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
         91, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  90,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
    };
    static const unsigned char kFa_diagram_project[256] = {
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
         11,  62,  63,  63,  31,   0,   0,   0,   0,   0,   0,  32,  63,  63,  62,  11,
        251, 255, 255, 255, 255,  46,   0,   0,   0,   0,  48, 255, 255, 255, 255, 247,
        255, 255, 255, 255, 255, 205, 148, 148, 148, 148, 205, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 177, 106, 106, 106, 106, 176, 255, 255, 255, 255, 255,
        225, 255, 255, 255, 255, 238,   3,   0,   0,   0,  37, 255, 255, 255, 255, 221,
          0,  36,  42,  42, 223, 255, 162,   0,   0,   0,   0,   8,  42,  42,  36,   0,
          0,   0,   0,   0,  23, 255, 255, 103,  42,  42,  18,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,  71, 255, 255, 255, 255, 255,  88,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0, 247, 255, 255, 255, 255, 175,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0, 247, 255, 255, 255, 255, 176,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0, 247, 255, 255, 255, 255, 176,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0, 178, 255, 255, 255, 255, 106,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   1,  57,  63,  63,  45,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
    };
    static const unsigned char kFa_list_check[256] = {
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0, 168, 255,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        201, 101, 138, 255, 183,   0,   0, 253, 255, 255, 255, 255, 255, 255, 255, 251,
        240, 255, 255, 216,   3,   0,   0, 251, 255, 255, 255, 255, 255, 255, 255, 247,
         15, 238, 235,  10,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0, 167, 255,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        200, 100, 138, 255, 183,   0,   0, 253, 255, 255, 255, 255, 255, 255, 255, 251,
        240, 255, 255, 216,   3,   0,   0, 251, 255, 255, 255, 255, 255, 255, 255, 247,
         15, 238, 235,  10,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
         50, 147,  48,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        255, 255, 255,   0,   0, 253, 255, 255, 255, 255, 255, 255, 255, 255, 255, 251,
        255, 255, 255,   0,   0, 251, 255, 255, 255, 255, 255, 255, 255, 255, 255, 247,
         47, 145,  46,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
    };
    static const unsigned char kFa_folder_tree[256] = {
          7,   1,   0,   0,   0,   0,   0,   0,   8,  42,  36,   0,   0,   0,   0,   0,
        255, 201,   0,   0,   0,   0,   0,   0, 255, 255, 255, 223,  75,  63,  63,  26,
        255, 247,   0,   0,   0,   0,   0,   0, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 148, 148, 148, 148, 148,  16, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255,  35, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 106, 106, 106, 106, 106,  11, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 247,   0,   0,   0,   0,   0,   0, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 247,   0,   0,   0,   0,   0,   0,  10,  42,  42,  42,  42,  42,  42,  10,
        255, 247,   0,   0,   0,   0,   0,   0,   8,  42,  36,   0,   0,   0,   0,   0,
        255, 247,   0,   0,   0,   0,   0,   0, 255, 255, 255, 225,  75,  63,  63,  26,
        255, 247,   0,   0,   0,   0,   0,   0, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 148, 148, 148, 148, 148,  16, 255, 255, 255, 255, 255, 255, 255, 255,
        210, 255, 255, 255, 255, 255, 255,  35, 255, 255, 255, 255, 255, 255, 255, 255,
          3,  85, 106, 106, 106, 106, 106,  11, 255, 255, 255, 255, 255, 255, 255, 255,
          0,   0,   0,   0,   0,   0,   0,   0, 255, 255, 255, 255, 255, 255, 255, 255,
          0,   0,   0,   0,   0,   0,   0,   0,  10,  42,  42,  42,  42,  42,  42,  10
    };
    static const unsigned char kFa_file_code[256] = {
          0,   0,  92, 255, 255, 255, 255, 255, 255,   0, 160,   0,   0,   0,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255,   0, 255, 160,   0,   0,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255,   0, 255, 255, 170,   0,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255,   0, 255, 255, 255, 170,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255,  73,   0,   0,   0,   0,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,   0,
          0,   0, 255, 255, 255,  76,  35, 255, 255,  33,  75, 255, 255, 255,   0,   0,
          0,   0, 255, 255,  86,   5, 213, 255, 255, 215,   6,  86, 255, 255,   0,   0,
          0,   0, 255, 255,  86,   5, 213, 255, 255, 215,   6,  87, 255, 255,   0,   0,
          0,   0, 255, 255, 255,  76,  32, 255, 255,  33,  76, 255, 255, 255,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,   0,
          0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   0,   0,
          0,   0,  91, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  90,   0,   0
    };
    static const unsigned char kFa_images[256] = {
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,  12,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  53,   0,
          0,   0,  22, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 191,
        112,  21, 101, 255, 255, 255, 208, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 105, 106, 255, 255, 121,   2, 255, 255, 255, 166, 255, 255, 255, 255, 255,
        255, 106, 106, 255, 255, 255, 246, 255, 255, 121,   0, 157, 255, 255, 255, 255,
        255, 106, 106, 255, 255, 255, 245,  98, 211,   0,   0,   6, 255, 255, 255, 255,
        255, 106, 106, 255, 255, 255,  30,   0,   1,   0,   0,   0,  58, 255, 255, 255,
        255, 106, 106, 255, 255,  68,   0,   0,   0,   0,   0,   0,   0, 160, 255, 255,
        255, 106, 105, 255, 255,  83,  63,  63,  63,  63,  63,  63,  63, 157, 255, 255,
        255, 110,  52, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 190,   0,  80, 170, 170, 170, 170, 170, 170, 170, 170, 170, 170, 158,  32,
        118, 255, 160,  48,  42,  42,  42,  42,  42,  42,  42,  42,  38,   0,   0,   0,
          0, 137, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  82,   0,   0,
          0,   0,  11,  60,  63,  63,  63,  63,  63,  63,  63,  63,  62,   3,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
    };
    static const unsigned char kFa_layer_group[256] = {
          0,   0,   0,   0,   0,   0,   0,   8,   8,   0,   0,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,  40, 182, 255, 255, 182,  40,   0,   0,   0,   0,   0,
          0,   0,   0,  57, 202, 255, 255, 255, 255, 255, 255, 223,  67,   0,   0,   0,
          0,  80, 231, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 231,  80,   0,
         13, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  12,
          0,  51, 196, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 196,  51,   0,
          0,   0,  47,  43, 172, 255, 255, 255, 255, 255, 255, 172,  42,  46,   0,   0,
          6, 221, 255, 255, 153,  40, 147, 255, 255, 147,  40, 153, 255, 255, 221,   6,
          6, 221, 255, 255, 255, 255, 178,  72,  72, 177, 255, 255, 255, 255, 221,   6,
          0,   0,  56, 191, 255, 255, 255, 255, 255, 255, 255, 255, 212,  65,   0,   0,
          0,  51, 187, 125,  45, 177, 255, 255, 255, 255, 177,  45, 125, 187,  51,   0,
         13, 255, 255, 255, 255, 148,  40, 136, 136,  40, 148, 255, 255, 255, 255,  12,
          0,  80, 231, 255, 255, 255, 255, 213, 213, 255, 255, 255, 255, 231,  80,   0,
          0,   0,   0,  57, 202, 255, 255, 255, 255, 255, 255, 223,  67,   0,   0,   0,
          0,   0,   0,   0,   0,  40, 182, 255, 255, 182,  40,   0,   0,   0,   0,   0,
          0,   0,   0,   0,   0,   0,   0,   7,   7,   0,   0,   0,   0,   0,   0,   0
    };
    static const unsigned char kFa_folder_plus[256] = {
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
         92, 255, 255, 255, 255, 255, 255,  90,   0,   0,   0,   0,   0,   0,   0,   0,
        255, 255, 255, 255, 255, 255, 255, 255,  70,   0,   0,   0,   0,   0,   0,   0,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  91,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 247, 248, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255,  83,  83, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255,  80,  80, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 247,  87,  85,  21,  21,  85,  87, 247, 255, 255, 255, 255,
        255, 255, 255, 255, 247,  87,  85,  21,  21,  85,  87, 248, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255,  80,  80, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255,  83,  83, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 248, 248, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
         91, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  90,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
    };
    static const unsigned char kFa_clock_rotate_left[256] = {
          0,   0,   0,   0,  55, 180, 255, 255, 255, 255, 180,  55,   0,   0,   0,   0,
        252, 153,   7, 186, 255, 255, 255, 255, 255, 255, 255, 255, 185,   7,   0,   0,
        255, 255, 255, 255, 255, 181,  68,  13,  13,  68, 182, 255, 255, 236,   7,   0,
        255, 255, 255, 255,  58,   0,   0,   0,   0,   0,   0,  60, 255, 255, 183,   0,
        255, 255, 255, 255, 155,   0,   0, 187, 185,   0,   0,   0,  60, 255, 255,  53,
        255, 255, 255, 255, 250,   0,   0, 238, 237,   0,   0,   0,   0, 183, 255, 177,
          0,   0,   0,   0,   0,   0,   0, 238, 237,   0,   0,   0,   0,  70, 255, 255,
          0,   0,   0,   0,   0,   0,   0, 238, 252,   0,   0,   0,   0,  16, 255, 255,
          0,   0,   0,   0,   0,   0,   0, 152, 255, 176,   0,   0,   0,  16, 255, 255,
          0,   0,   0,   0,   0,   0,   0,   0, 178, 255, 170,   0,   0,  70, 255, 255,
          0,   0,   0,   0,   0,   0,   0,   0,   0, 171, 255,   0,   0, 183, 255, 177,
          0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  60, 255, 255,  52,
          0,   0,   0,  37,  37,   0,   0,   0,   0,   0,   0,  60, 255, 255, 181,   0,
          0,   0,   0, 255, 255, 196,  76,  23,  15,  71, 183, 255, 255, 233,   6,   0,
          0,   0,   0, 177, 255, 255, 255, 255, 255, 255, 255, 255, 181,   6,   0,   0,
          0,   0,   0,   0,  57, 185, 255, 255, 255, 255, 176,  52,   0,   0,   0,   0
    };
    static const unsigned char kFa_circle_info[256] = {
          0,   0,   0,   0,  63, 192, 255, 255, 255, 255, 190,  62,   0,   0,   0,   0,
          0,   0,  12, 198, 255, 255, 255, 255, 255, 255, 255, 255, 193,  11,   0,   0,
          0,  12, 245, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 241,  11,   0,
          0, 197, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 191,   0,
         63, 255, 255, 255, 255, 255, 255,  65,  68, 255, 255, 255, 255, 255, 255,  60,
        192, 255, 255, 255, 255, 255, 255,  68,  72, 255, 255, 255, 255, 255, 255, 187,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255,  37,   0,  38, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 196,  75,   0, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 160,   0, 255, 255, 255, 255, 255, 255, 255,
        190, 255, 255, 255, 255, 255, 200,  85,   0, 201, 255, 255, 255, 255, 255, 185,
         62, 255, 255, 255, 255, 255,  38,   0,   0,  42, 255, 255, 255, 255, 255,  57,
          0, 192, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 187,   0,
          0,  11, 241, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 237,  10,   0,
          0,   0,  11, 192, 255, 255, 255, 255, 255, 255, 255, 255, 187,  10,   0,   0,
          0,   0,   0,   0,  60, 187, 255, 255, 255, 255, 185,  57,   0,   0,   0,   0
    };
    static const unsigned char kFa_file_circle_plus[256] = {
          0,  26,  42,  42,  42,  42,  10,   1,   0,   0,   0,   0,   0,   0,   0,   0,
        163, 255, 255, 255, 255, 255,  71, 186,   7,   0,   0,   0,   0,   0,   0,   0,
        255, 255, 255, 255, 255, 255,  71, 255, 222,   7,   0,   0,   0,   0,   0,   0,
        255, 255, 255, 255, 255, 255,  71, 255, 255, 227,  10,   0,   0,   0,   0,   0,
        255, 255, 255, 255, 255, 255,  81, 132, 148, 148,  67,   0,   0,   0,   0,   0,
        255, 255, 255, 255, 255, 255, 255, 212, 212, 212, 142,   0,   0,   0,   0,   0,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 108,   0,   0,   0,   0,   0,
        255, 255, 255, 255, 255, 255, 255, 255, 216,  45, 190, 255, 255, 188,  31,   0,
        255, 255, 255, 255, 255, 255, 255, 255,  48, 255, 255, 255, 255, 255, 255,  35,
        255, 255, 255, 255, 255, 255, 255, 122, 210, 255, 255, 181, 182, 255, 255, 206,
        255, 255, 255, 255, 255, 255, 255,  48, 255, 255, 213, 117, 117, 213, 255, 255,
        255, 255, 255, 255, 255, 255, 255,  42, 255, 255, 148,  82,  82, 148, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 101, 237, 255, 255, 177, 177, 255, 255, 232,
        255, 255, 255, 255, 255, 255, 255, 236,  70, 255, 255, 255, 255, 255, 255,  65,
        160, 255, 255, 255, 255, 255, 255, 255, 158,  75, 255, 255, 255, 253,  72,   0,
          0,  23,  42,  42,  42,  42,  42,  42,  42,   2,   0,  21,  20,   0,   0,   0
    };

    void DrawMenuCommandIcon(HDC hdc, RECT iconRc, UINT_PTR commandId, bool disabled, COLORREF backgroundColor)
    {
        const UINT id = static_cast<UINT>(commandId);
        const COLORREF normal = disabled ? RGB(112, 112, 118) : RGB(226, 230, 236);
        const COLORREF blue = disabled ? RGB(105, 105, 112) : RGB(96, 170, 230);
        const COLORREF green = disabled ? RGB(105, 105, 112) : RGB(98, 200, 132);
        const COLORREF yellow = disabled ? RGB(105, 105, 112) : RGB(235, 184, 82);
        const COLORREF red = disabled ? RGB(105, 105, 112) : RGB(230, 105, 105);
        const COLORREF purple = disabled ? RGB(105, 105, 112) : RGB(198, 142, 232);

        const unsigned char* icon = nullptr;
        COLORREF color = normal;

        switch (id)
        {
        case IDM_FILE_NEW: icon = kFa_file_circle_plus; color = green; break;
        case IDM_FILE_OPEN: icon = kFa_folder_open; color = yellow; break;
        case IDM_FILE_CLOSE: icon = kFa_xmark; color = red; break;
        case IDM_FILE_SAVE: icon = kFa_floppy_disk; color = blue; break;
        case IDM_FILE_SAVE_AS: icon = kFa_floppy_disk; color = green; break;
        case IDM_FILE_EXPORT_SVG: icon = kFa_file_export; color = green; break;
        case IDM_TOOLS_SETTINGS: icon = kFa_gear; color = blue; break;
        case IDM_FILE_EXIT: icon = kFa_right_from_bracket; color = red; break;

        case IDM_EDIT_UNDO: icon = kFa_rotate_left; color = blue; break;
        case IDM_EDIT_ZONE: icon = kFa_pen_to_square; color = yellow; break;
        case IDM_EDIT_DELETE: icon = kFa_trash_can; color = red; break;
        case IDM_EDIT_LINK_EVENT: icon = kFa_link; color = green; break;
        case IDM_EDIT_LINK_SWITCH_TEXTURE: icon = kFa_toggle_on; color = yellow; break;
        case kCmdLinkEnemyObjects: icon = kFa_skull; color = purple; break;
        case IDM_EDIT_LINK_ROTATE_CW: icon = kFa_rotate_right; color = blue; break;
        case IDM_EDIT_LINK_ROTATE_CCW: icon = kFa_rotate_left; color = blue; break;
        case kCmdLinkMoveWallGroup: icon = kFa_arrows_move; color = green; break;
        case IDM_EDIT_SET_TELEPORT_TARGET: icon = kFa_location_crosshairs; color = purple; break;
        case IDM_TOOLS_VALIDATE: icon = kFa_check; color = green; break;

        case IDM_MAP_TEXTURES: icon = kFa_table_cells; color = yellow; break;
        case kCmdViewEventGraphOverlay: icon = kFa_diagram_project; color = green; break;
        case IDM_MAP_EVENTS: icon = kFa_list_check; color = blue; break;

        case IDM_CAMPAIGN_OPEN_GAME_ROOT: icon = kFa_folder_tree; color = yellow; break;
        case IDM_CAMPAIGN_SCRIPT: icon = kFa_file_code; color = blue; break;
        case IDM_CAMPAIGN_INTERMISSION_SCREENS: icon = kFa_images; color = purple; break;
        case IDM_CAMPAIGN_TITLE_MUSIC: icon = kFa_layer_group; color = yellow; break; // Campaign > Textures
        case IDM_CAMPAIGN_EXPORT_GAME_PACKAGE: icon = kFa_folder_plus; color = green; break; // Campaign > New Campaign

        case IDM_HELP_ABOUT: icon = kFa_circle_info; color = blue; break;
        default:
            if (id >= IDM_FILE_RECENT_FIRST && id <= IDM_FILE_RECENT_LAST)
            {
                icon = kFa_clock_rotate_left;
                color = blue;
            }
            else if (id >= kRecentCampaignGameRootBaseId && id < kRecentCampaignGameRootBaseId + kRecentCampaignGameRootCount)
            {
                icon = kFa_folder_open;
                color = yellow;
            }
            else
            {
                icon = kFa_file;
                color = normal;
            }
            break;
        }

        if (!icon)
        {
            return;
        }

        auto blend = [](BYTE bg, BYTE fg, BYTE a) -> BYTE
        {
            return static_cast<BYTE>((static_cast<int>(fg) * a + static_cast<int>(bg) * (255 - a)) / 255);
        };

        const BYTE bgR = GetRValue(backgroundColor);
        const BYTE bgG = GetGValue(backgroundColor);
        const BYTE bgB = GetBValue(backgroundColor);
        const BYTE fgR = GetRValue(color);
        const BYTE fgG = GetGValue(color);
        const BYTE fgB = GetBValue(color);

        uint32_t dibPixels[16 * 16]{};
        for (int i = 0; i < 16 * 16; ++i)
        {
            const BYTE a = icon[i];
            const BYTE rr = blend(bgR, fgR, a);
            const BYTE gg = blend(bgG, fgG, a);
            const BYTE bb = blend(bgB, fgB, a);
            dibPixels[i] = (static_cast<uint32_t>(bb) << 16) | (static_cast<uint32_t>(gg) << 8) | rr;
        }

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = 16;
        bmi.bmiHeader.biHeight = -16;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        const int dstW = iconRc.right - iconRc.left;
        const int dstH = iconRc.bottom - iconRc.top;
        const int drawW = MinValue(dstW, 16);
        const int drawH = MinValue(dstH, 16);
        const int x0 = iconRc.left + (dstW - drawW) / 2;
        const int y0 = iconRc.top + (dstH - drawH) / 2;

        StretchDIBits(
            hdc,
            x0,
            y0,
            drawW,
            drawH,
            0,
            0,
            16,
            16,
            dibPixels,
            &bmi,
            DIB_RGB_COLORS,
            SRCCOPY);
    }

    void DrawOwnerDrawMenuItem(DRAWITEMSTRUCT* draw)
    {
        if (!draw || draw->CtlType != ODT_MENU || !draw->itemData)
        {
            return;
        }

        const auto* item = reinterpret_cast<const OwnerDrawMenuItem*>(draw->itemData);
        HDC hdc = draw->hDC;
        RECT rc = draw->rcItem;
        if (item->separator)
        {
            HBRUSH sepBrush = CreateSolidBrush(RGB(37, 37, 38));
            FillRect(hdc, &rc, sepBrush);
            DeleteObject(sepBrush);
            const int y = rc.top + ((rc.bottom - rc.top) / 2);
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(62, 62, 66));
            HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(hdc, pen));
            MoveToEx(hdc, rc.left + 38, y, nullptr);
            LineTo(hdc, rc.right - 8, y);
            if (oldPen)
            {
                SelectObject(hdc, oldPen);
            }
            DeleteObject(pen);
            return;
        }
        const bool selected = (draw->itemState & ODS_SELECTED) != 0;
        const bool disabled = (draw->itemState & ODS_GRAYED) != 0;

        const COLORREF normalBg = item->topLevel ? kDarkWindowBg : RGB(37, 37, 38);
        const COLORREF selectedBg = item->topLevel ? RGB(52, 52, 60) : RGB(63, 63, 70);
        const COLORREF normalText = disabled ? RGB(118, 118, 128) : RGB(241, 241, 241);
        const COLORREF selectedText = disabled ? RGB(118, 118, 128) : RGB(255, 255, 255);

        HBRUSH bgBrush = CreateSolidBrush(selected ? selectedBg : normalBg);
        FillRect(hdc, &rc, bgBrush);
        DeleteObject(bgBrush);

        HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT)));
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, selected ? selectedText : normalText);

        std::wstring left = MenuTextWithoutShortcut(item->text);
        std::wstring right = MenuShortcutText(item->text);

        RECT textRc = rc;
        if (item->topLevel)
        {
            InflateRect(&textRc, -8, 0);
            DrawTextW(hdc, left.c_str(), -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else
        {
            RECT gutterRc = rc;
            gutterRc.right = gutterRc.left + 34;
            HBRUSH gutterBrush = CreateSolidBrush(selected ? RGB(51, 51, 55) : RGB(45, 45, 48));
            FillRect(hdc, &gutterRc, gutterBrush);
            DeleteObject(gutterBrush);

            RECT iconRc{ rc.left + 8, rc.top + 5, rc.left + 24, rc.bottom - 5 };
            DrawMenuCommandIcon(hdc, iconRc, item->id, disabled, selected ? RGB(51, 51, 55) : RGB(45, 45, 48));
            if (item->id == kCmdViewEventGraphOverlay && g_app.showEventGraphOverlay)
            {
                HPEN checkPen = CreatePen(PS_SOLID, 2, RGB(92, 214, 132));
                HPEN oldCheckPen = reinterpret_cast<HPEN>(SelectObject(hdc, checkPen));
                MoveToEx(hdc, iconRc.left + 1, iconRc.bottom - 5, nullptr);
                LineTo(hdc, iconRc.left + 6, iconRc.bottom - 1);
                LineTo(hdc, iconRc.right - 1, iconRc.top + 2);
                if (oldCheckPen) SelectObject(hdc, oldCheckPen);
                DeleteObject(checkPen);
            }

            textRc.left += 46;
            textRc.right -= 14;
            RECT leftRc = textRc;
            if (!right.empty())
            {
                leftRc.right -= 84;
            }
            DrawTextW(hdc, left.c_str(), -1, &leftRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            if (!right.empty())
            {
                RECT rightRc = textRc;
                DrawTextW(hdc, right.c_str(), -1, &rightRc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            }
        }

        if (item->topLevel && left == L"&About")
        {
            RECT wndRc{};
            GetWindowRect(g_app.mainWindow, &wndRc);
            RECT tailRc = rc;
            tailRc.left = rc.right;
            tailRc.right = MaxValue(rc.right, wndRc.right - wndRc.left);
            if (tailRc.right > tailRc.left)
            {
                FillRect(hdc, &tailRc, DarkWindowBrush());
                HPEN pen = CreatePen(PS_SOLID, 1, kDarkBorder);
                HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(hdc, pen));
                MoveToEx(hdc, tailRc.left, tailRc.bottom - 1, nullptr);
                LineTo(hdc, tailRc.right, tailRc.bottom - 1);
                if (oldPen)
                {
                    SelectObject(hdc, oldPen);
                }
                DeleteObject(pen);
            }
        }

        if (oldFont)
        {
            SelectObject(hdc, oldFont);
        }
    }

    HMENU BuildMainMenu()
    {
        g_app.menuItems.clear();
        HMENU menuBar = CreateMenu();
        HMENU fileMenu = CreatePopupMenu();
        HMENU editMenu = CreatePopupMenu();
        HMENU viewMenu = CreatePopupMenu();
        HMENU campaignMenu = CreatePopupMenu();
        HMENU aboutMenu = CreatePopupMenu();

        AppendOwnerDrawMenuItem(fileMenu, IDM_FILE_NEW, L"&New");
        AppendOwnerDrawMenuItem(fileMenu, IDM_FILE_OPEN, L"&Open...\tCtrl+O");
        AppendOwnerDrawMenuItem(fileMenu, IDM_FILE_CLOSE, L"&Close");
        AppendOwnerDrawSeparator(fileMenu);
        for (int i = 0; i < kRecentFileCount; ++i)
        {
            std::wstringstream label;
            label << L"&" << (i + 1) << L" (empty)";
            AppendOwnerDrawMenuItem(fileMenu, kRecentFileBaseId + static_cast<UINT>(i), label.str(), false);
        }
        AppendOwnerDrawSeparator(fileMenu);
        AppendOwnerDrawMenuItem(fileMenu, IDM_FILE_SAVE, L"&Save\tCtrl+S");
        AppendOwnerDrawMenuItem(fileMenu, IDM_FILE_SAVE_AS, L"Save &As...");
        AppendOwnerDrawMenuItem(fileMenu, IDM_FILE_EXPORT_SVG, L"Export &SVG Overview...");
        AppendOwnerDrawSeparator(fileMenu);
        AppendOwnerDrawMenuItem(fileMenu, IDM_TOOLS_SETTINGS, L"Editor &Settings");
        AppendOwnerDrawSeparator(fileMenu);
        AppendOwnerDrawMenuItem(fileMenu, IDM_FILE_EXIT, L"E&xit");

        AppendOwnerDrawMenuItem(editMenu, IDM_EDIT_UNDO, L"&Undo\tCtrl+Z");
        AppendOwnerDrawSeparator(editMenu);
        AppendOwnerDrawMenuItem(editMenu, IDM_EDIT_ZONE, L"&Edit Selected Zone...");
        AppendOwnerDrawMenuItem(editMenu, IDM_EDIT_DELETE, L"&Delete Selection\tDel");
        AppendOwnerDrawMenuItem(editMenu, IDM_EDIT_LINK_EVENT, L"Link Event -> &Wall/Door");
        AppendOwnerDrawMenuItem(editMenu, IDM_EDIT_LINK_SWITCH_TEXTURE, L"Link Event -> &Switch/Trigger");
        AppendOwnerDrawMenuItem(editMenu, kCmdLinkEnemyObjects, L"Link Event -> &Enemy/Objects");
        AppendOwnerDrawMenuItem(editMenu, IDM_EDIT_LINK_ROTATE_CW, L"Link Event -> Rotate &CW");
        AppendOwnerDrawMenuItem(editMenu, IDM_EDIT_LINK_ROTATE_CCW, L"Link Event -> Rotate &CCW");
        AppendOwnerDrawMenuItem(editMenu, kCmdLinkMoveWallGroup, L"Link Event -> Move Wall&block");
        AppendOwnerDrawMenuItem(editMenu, IDM_EDIT_SET_TELEPORT_TARGET, L"Set &Teleport Target");
        AppendOwnerDrawSeparator(editMenu);
        AppendOwnerDrawMenuItem(editMenu, IDM_TOOLS_VALIDATE, L"&Validate Map");

        AppendOwnerDrawMenuItem(viewMenu, IDM_MAP_TEXTURES, L"Texture &Slots");
        AppendOwnerDrawMenuItem(viewMenu, kCmdViewEventGraphOverlay, L"Toggle Event &Links Overlay");
        AppendOwnerDrawMenuItem(viewMenu, IDM_MAP_EVENTS, L"Show &Events");

        AppendOwnerDrawMenuItem(campaignMenu, IDM_CAMPAIGN_OPEN_GAME_ROOT, L"Open Game Root");
        AppendOwnerDrawSeparator(campaignMenu);
        for (int i = 0; i < kRecentCampaignGameRootCount; ++i)
        {
            std::wstringstream label;
            label << L"&" << (i + 1) << L" (empty)";
            AppendOwnerDrawMenuItem(campaignMenu, kRecentCampaignGameRootBaseId + static_cast<UINT>(i), label.str(), false);
        }
        AppendOwnerDrawSeparator(campaignMenu);
        AppendOwnerDrawMenuItem(campaignMenu, IDM_CAMPAIGN_SCRIPT, L"Campaign Script");
        AppendOwnerDrawMenuItem(campaignMenu, IDM_CAMPAIGN_INTERMISSION_SCREENS, L"Title and Intermission Screens");
        AppendOwnerDrawMenuItem(campaignMenu, IDM_CAMPAIGN_TITLE_MUSIC, L"Textures");
        AppendOwnerDrawSeparator(campaignMenu);
        AppendOwnerDrawMenuItem(campaignMenu, IDM_CAMPAIGN_EXPORT_GAME_PACKAGE, L"New Campaign");

        AppendOwnerDrawMenuItem(aboutMenu, IDM_HELP_ABOUT, L"&Information");

        g_app.fileMenu = fileMenu;
        g_app.campaignMenu = campaignMenu;
        UpdateRecentFilesMenu();
        UpdateRecentCampaignGameRootsMenu();
        ApplyDarkMenuHints(fileMenu);
        ApplyDarkMenuHints(editMenu);
        ApplyDarkMenuHints(viewMenu);
        ApplyDarkMenuHints(campaignMenu);
        ApplyDarkMenuHints(aboutMenu);

        // Keep the top-level menu bar native so item widths stay Windows-like.
        // Only the drop-down menus are owner-drawn in the Visual-Studio-like dark style.
        AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), L"&File");
        AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(editMenu), L"&Edit");
        AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(viewMenu), L"&View");
        AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(campaignMenu), L"&Campaign");
        AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(aboutMenu), L"&About");
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

        const int padding = 10;
        const int buttonHeight = 30;
        const int contentTop = 0;
        const int contentWidth = MaxValue(1, rc.right - rc.left);
        const int contentHeight = MaxValue(1, rc.bottom - statusHeight - contentTop);

        const int leftWidth = ClampValue(220, 190, MaxValue(190, contentWidth / 4));
        const int rightAreaX = leftWidth + padding;
        const int rightAreaWidth = MaxValue(1, contentWidth - rightAreaX - padding);
        const int actualInfoWidth = ClampValue(320, 240, MaxValue(240, rightAreaWidth / 2));
        const int canvasWidth = MaxValue(160, rightAreaWidth - actualInfoWidth - padding);
        const int panelHeight = MaxValue(120, contentHeight - (padding * 2));
        const int groupHeaderHeight = 18;
        const int groupHeaderGap = 6;
        const int groupGap = 14;

        auto hideControl = [](HWND hwndControl)
        {
            if (hwndControl) ShowWindow(hwndControl, SW_HIDE);
        };

        hideControl(g_app.zoneList);
        hideControl(g_app.btnEdit);
        hideControl(g_app.btnDelete);
        hideControl(g_app.btnUp);
        hideControl(g_app.btnDown);
        hideControl(g_app.btnFlipDoorDirection);
        hideControl(g_app.btnAddMonster);

        hideControl(g_app.btnTextures);
        hideControl(g_app.btnEvents);
        hideControl(g_app.btnAddWall);
        hideControl(g_app.btnAddTrigger);
        hideControl(g_app.btnLinkEvent);
        hideControl(g_app.btnLinkSwitchTexture);
        hideControl(g_app.btnLinkEnemyObjects);
        hideControl(g_app.btnLinkRotateCW);
        hideControl(g_app.btnLinkRotateCCW);
        hideControl(g_app.btnLinkMoveWallGroup);
        hideControl(g_app.btnDeleteLinkEvent);
        hideControl(g_app.btnPlaceEnemy);
        hideControl(g_app.btnPlacePickup);
        hideControl(g_app.btnPlaceWeapon);
        hideControl(g_app.btnPlayerStart);
        hideControl(g_app.btnSetTeleportTarget);
        hideControl(g_app.btnLevelEnd);
        hideControl(g_app.btnValidateMap);
        hideControl(g_app.btnToolbarScrollUp);
        hideControl(g_app.btnToolbarScrollDown);
        hideControl(g_app.lineValidateTop);
        hideControl(g_app.lineZonesLeft);
        hideControl(g_app.labelZones);
        hideControl(g_app.lineZonesRight);
        hideControl(g_app.lineObjectsLeft);
        hideControl(g_app.labelObjects);
        hideControl(g_app.lineObjectsRight);
        hideControl(g_app.lineEventLinksLeft);
        hideControl(g_app.labelEventLinks);
        hideControl(g_app.lineEventLinksRight);
        hideControl(g_app.lineOrderLeft);
        hideControl(g_app.labelOrder);
        hideControl(g_app.lineOrderRight);
        hideControl(g_app.lineMapMarkersLeft);
        hideControl(g_app.labelMapMarkers);
        hideControl(g_app.lineMapMarkersRight);

        const int buttonWidth = MaxValue(120, leftWidth - (padding * 2));
        // v45: center the left toolbar controls in the full left rail, including
        // the visual gap before the canvas.  Previously they sat flush at the
        // left padding, which made the whole button column look slightly off.
        const int leftRailWidth = rightAreaX;
        const int x = MaxValue(padding, (leftRailWidth - buttonWidth) / 2);

        auto measureToolbarHeight = [&]() -> int
        {
            int h = 0;
            auto addGroup = [&]() { h += groupHeaderHeight + groupHeaderGap; };
            auto addButton = [&]() { h += buttonHeight + 7; };
            auto addGap = [&]() { h += groupGap - 4; };

            addGroup();
            addButton();
            addButton();
            addGap();

            addGroup();
            addButton();
            addButton();
            addGap();

            addGroup();
            addButton();
            addButton();
            addButton();
            addButton();
            addButton();
            addButton();
            addButton();
            addGap();

            addGroup();
            addButton();
            addButton();
            addButton();
            addGap();

            addGroup();
            addButton();
            addButton();
            addButton();

            addGap();
            h += 13; // separator and small breathing room before Validate Map
            addButton();
            return MaxValue(0, h - 7);
        };

        const int toolbarTotalHeight = measureToolbarHeight();
        const int normalAvailableHeight = MaxValue(1, contentHeight - padding * 2);
        const bool useToolbarScroll = toolbarTotalHeight > normalAvailableHeight;
        g_app.toolbarScrollMode = useToolbarScroll;

        if (!useToolbarScroll)
        {
            g_app.toolbarScrollY = 0;
            g_app.toolbarScrollMaxY = 0;

            int y = contentTop + padding;
            auto moveGroupHeader = [&](HWND leftLine, HWND label, HWND rightLine)
            {
                if (leftLine) ShowWindow(leftLine, SW_HIDE);
                if (rightLine) ShowWindow(rightLine, SW_HIDE);
                MoveWindow(label, x, y, buttonWidth, groupHeaderHeight, TRUE);
                ShowWindow(label, SW_SHOW);
                y += groupHeaderHeight + groupHeaderGap;
            };
            auto moveButton = [&](HWND button)
            {
                MoveWindow(button, x, y, buttonWidth, buttonHeight, TRUE);
                ShowWindow(button, SW_SHOW);
                y += buttonHeight + 7;
            };

            moveGroupHeader(g_app.lineZonesLeft, g_app.labelZones, g_app.lineZonesRight);
            moveButton(g_app.btnTextures);
            moveButton(g_app.btnEvents);
            y += groupGap - 4;

            moveGroupHeader(g_app.lineObjectsLeft, g_app.labelObjects, g_app.lineObjectsRight);
            moveButton(g_app.btnAddWall);
            moveButton(g_app.btnAddTrigger);
            y += groupGap - 4;

            moveGroupHeader(g_app.lineEventLinksLeft, g_app.labelEventLinks, g_app.lineEventLinksRight);
            moveButton(g_app.btnLinkEvent);
            moveButton(g_app.btnLinkSwitchTexture);
            moveButton(g_app.btnLinkEnemyObjects);
            moveButton(g_app.btnLinkRotateCW);
            moveButton(g_app.btnLinkRotateCCW);
            moveButton(g_app.btnLinkMoveWallGroup);
            moveButton(g_app.btnDeleteLinkEvent);
            y += groupGap - 4;

            moveGroupHeader(g_app.lineOrderLeft, g_app.labelOrder, g_app.lineOrderRight);
            moveButton(g_app.btnPlaceEnemy);
            moveButton(g_app.btnPlacePickup);
            moveButton(g_app.btnPlaceWeapon);
            y += groupGap - 4;

            moveGroupHeader(g_app.lineMapMarkersLeft, g_app.labelMapMarkers, g_app.lineMapMarkersRight);
            moveButton(g_app.btnPlayerStart);
            moveButton(g_app.btnSetTeleportTarget);
            moveButton(g_app.btnLevelEnd);

            const int validateY = contentTop + contentHeight - padding - buttonHeight;
            if (validateY > y + 8)
            {
                if (g_app.lineValidateTop)
                {
                    MoveWindow(g_app.lineValidateTop, x, validateY - 13, buttonWidth, 1, TRUE);
                    ShowWindow(g_app.lineValidateTop, SW_SHOW);
                }
                MoveWindow(g_app.btnValidateMap, x, validateY, buttonWidth, buttonHeight, TRUE);
                ShowWindow(g_app.btnValidateMap, SW_SHOW);
            }
            else
            {
                y += groupGap - 4;
                moveButton(g_app.btnValidateMap);
            }
        }
        else
        {
            const int scrollButtonHeight = 24;
            const int scrollButtonGap = 6;
            const int scrollTop = contentTop + padding;
            const int scrollBottom = contentTop + contentHeight - padding;
            const int visibleTop = scrollTop + scrollButtonHeight + scrollButtonGap;
            const int visibleBottom = MaxValue(visibleTop + 1, scrollBottom - scrollButtonHeight - scrollButtonGap);
            const int visibleHeight = MaxValue(1, visibleBottom - visibleTop);
            g_app.toolbarScrollMaxY = MaxValue(0, toolbarTotalHeight - visibleHeight);
            g_app.toolbarScrollY = ClampValue(g_app.toolbarScrollY, 0, g_app.toolbarScrollMaxY);

            MoveWindow(g_app.btnToolbarScrollUp, x, scrollTop, buttonWidth, scrollButtonHeight, TRUE);
            MoveWindow(g_app.btnToolbarScrollDown, x, scrollBottom - scrollButtonHeight, buttonWidth, scrollButtonHeight, TRUE);
            ShowWindow(g_app.btnToolbarScrollUp, SW_SHOW);
            ShowWindow(g_app.btnToolbarScrollDown, SW_SHOW);
            EnableWindow(g_app.btnToolbarScrollUp, g_app.toolbarScrollY > 0 ? TRUE : FALSE);
            EnableWindow(g_app.btnToolbarScrollDown, g_app.toolbarScrollY < g_app.toolbarScrollMaxY ? TRUE : FALSE);

            int y = visibleTop - g_app.toolbarScrollY;
            auto showIfFullyVisible = [&](HWND control, int itemY, int itemH)
            {
                if (!control) return;
                MoveWindow(control, x, itemY, buttonWidth, itemH, TRUE);
                const bool visible = itemY >= visibleTop && (itemY + itemH) <= visibleBottom;
                ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
            };
            auto moveGroupHeader = [&](HWND leftLine, HWND label, HWND rightLine)
            {
                if (leftLine) ShowWindow(leftLine, SW_HIDE);
                if (rightLine) ShowWindow(rightLine, SW_HIDE);
                showIfFullyVisible(label, y, groupHeaderHeight);
                y += groupHeaderHeight + groupHeaderGap;
            };
            auto moveButton = [&](HWND button)
            {
                showIfFullyVisible(button, y, buttonHeight);
                y += buttonHeight + 7;
            };
            auto moveSeparator = [&]()
            {
                if (g_app.lineValidateTop)
                {
                    MoveWindow(g_app.lineValidateTop, x, y + 6, buttonWidth, 1, TRUE);
                    const bool visible = (y + 6) >= visibleTop && (y + 7) <= visibleBottom;
                    ShowWindow(g_app.lineValidateTop, visible ? SW_SHOW : SW_HIDE);
                }
                y += 13;
            };

            moveGroupHeader(g_app.lineZonesLeft, g_app.labelZones, g_app.lineZonesRight);
            moveButton(g_app.btnTextures);
            moveButton(g_app.btnEvents);
            y += groupGap - 4;

            moveGroupHeader(g_app.lineObjectsLeft, g_app.labelObjects, g_app.lineObjectsRight);
            moveButton(g_app.btnAddWall);
            moveButton(g_app.btnAddTrigger);
            y += groupGap - 4;

            moveGroupHeader(g_app.lineEventLinksLeft, g_app.labelEventLinks, g_app.lineEventLinksRight);
            moveButton(g_app.btnLinkEvent);
            moveButton(g_app.btnLinkSwitchTexture);
            moveButton(g_app.btnLinkEnemyObjects);
            moveButton(g_app.btnLinkRotateCW);
            moveButton(g_app.btnLinkRotateCCW);
            moveButton(g_app.btnLinkMoveWallGroup);
            moveButton(g_app.btnDeleteLinkEvent);
            y += groupGap - 4;

            moveGroupHeader(g_app.lineOrderLeft, g_app.labelOrder, g_app.lineOrderRight);
            moveButton(g_app.btnPlaceEnemy);
            moveButton(g_app.btnPlacePickup);
            moveButton(g_app.btnPlaceWeapon);
            y += groupGap - 4;

            moveGroupHeader(g_app.lineMapMarkersLeft, g_app.labelMapMarkers, g_app.lineMapMarkersRight);
            moveButton(g_app.btnPlayerStart);
            moveButton(g_app.btnSetTeleportTarget);
            moveButton(g_app.btnLevelEnd);

            y += groupGap - 4;
            moveSeparator();
            moveButton(g_app.btnValidateMap);
        }

        MoveWindow(g_app.canvas, rightAreaX, contentTop + padding, canvasWidth, panelHeight, TRUE);
        MoveWindow(g_app.infoPanel, rightAreaX + canvasWidth + padding, contentTop + padding, MaxValue(180, actualInfoWidth - padding), panelHeight, TRUE);
        UpdateCanvasScrollBars(g_app.canvas);
        UpdateInfoPanelScrollBar(g_app.infoPanel);
        InvalidateRect(hwnd, nullptr, TRUE);
        InvalidateEditorViews();
        if (g_app.statusBar)
        {
            RedrawWindow(g_app.statusBar, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
        }
    }

    void UpdateCanvasScrollBars(HWND hwnd)
    {
        if (!hwnd) return;
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const mapfmt::Bounds rawBounds = g_app.document.ComputeBounds();
        ClampViewCenterToDocument(rawBounds);
        const CanvasMetrics metrics = GetCanvasMetrics(rc, rawBounds);
        const int baseWidth = MaxValue(1, metrics.baseBounds.maxX - metrics.baseBounds.minX);
        const int baseHeight = MaxValue(1, metrics.baseBounds.maxZ - metrics.baseBounds.minZ);
        const int pageX = ClampValue(static_cast<int>(std::lround(metrics.visibleWorldWidth)), 1, baseWidth + 1);
        const int pageY = ClampValue(static_cast<int>(std::lround(metrics.visibleWorldHeight)), 1, baseHeight + 1);
        const int leftOffset = ClampValue(static_cast<int>(std::lround(g_app.viewCenterX - metrics.visibleWorldWidth * 0.5 - metrics.baseBounds.minX)), 0, MaxValue(0, baseWidth - pageX + 1));
        const int topOffset = ClampValue(static_cast<int>(std::lround(metrics.baseBounds.maxZ - (g_app.viewCenterZ + metrics.visibleWorldHeight * 0.5))), 0, MaxValue(0, baseHeight - pageY + 1));

        SCROLLINFO si{};
        si.cbSize = sizeof(SCROLLINFO);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0;
        si.nMax = baseWidth;
        si.nPage = static_cast<UINT>(pageX);
        si.nPos = leftOffset;
        SetScrollInfo(hwnd, SB_HORZ, &si, TRUE);

        si.nMax = baseHeight;
        si.nPage = static_cast<UINT>(pageY);
        si.nPos = topOffset;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
    }

    void ScrollCanvasBy(HWND hwnd, int bar, int request, int trackPos)
    {
        if (!hwnd) return;
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const mapfmt::Bounds rawBounds = g_app.document.ComputeBounds();
        CanvasMetrics metrics = GetCanvasMetrics(rc, rawBounds);
        const int baseWidth = MaxValue(1, metrics.baseBounds.maxX - metrics.baseBounds.minX);
        const int baseHeight = MaxValue(1, metrics.baseBounds.maxZ - metrics.baseBounds.minZ);
        const int pageX = ClampValue(static_cast<int>(std::lround(metrics.visibleWorldWidth)), 1, baseWidth + 1);
        const int pageY = ClampValue(static_cast<int>(std::lround(metrics.visibleWorldHeight)), 1, baseHeight + 1);
        const int maxX = MaxValue(0, baseWidth - pageX + 1);
        const int maxY = MaxValue(0, baseHeight - pageY + 1);

        const bool horizontal = (bar == SB_HORZ);
        int pos = horizontal
            ? ClampValue(static_cast<int>(std::lround(g_app.viewCenterX - metrics.visibleWorldWidth * 0.5 - metrics.baseBounds.minX)), 0, maxX)
            : ClampValue(static_cast<int>(std::lround(metrics.baseBounds.maxZ - (g_app.viewCenterZ + metrics.visibleWorldHeight * 0.5))), 0, maxY);
        const int page = horizontal ? pageX : pageY;
        const int line = MaxValue(32, kGridStep / 2);

        // Do not use a switch here: Win32 defines horizontal and vertical scroll
        // command aliases with identical numeric values (SB_LINELEFT == SB_LINEUP,
        // SB_LEFT == SB_TOP, etc.). Some compilers report duplicate case labels when
        // both families appear in nearby edits, so keep this as explicit comparisons.
        const int cmd = request;
        if (cmd == SB_LINELEFT || cmd == SB_LINEUP)
        {
            pos -= line;
        }
        else if (cmd == SB_LINERIGHT || cmd == SB_LINEDOWN)
        {
            pos += line;
        }
        else if (cmd == SB_PAGELEFT || cmd == SB_PAGEUP)
        {
            pos -= page;
        }
        else if (cmd == SB_PAGERIGHT || cmd == SB_PAGEDOWN)
        {
            pos += page;
        }
        else if (cmd == SB_THUMBPOSITION || cmd == SB_THUMBTRACK)
        {
            pos = trackPos;
        }
        else if (cmd == SB_LEFT || cmd == SB_TOP)
        {
            pos = 0;
        }
        else if (cmd == SB_RIGHT || cmd == SB_BOTTOM)
        {
            pos = horizontal ? maxX : maxY;
        }

        if (horizontal)
        {
            pos = ClampValue(pos, 0, maxX);
            g_app.viewCenterX = static_cast<double>(metrics.baseBounds.minX) + static_cast<double>(pos) + metrics.visibleWorldWidth * 0.5;
        }
        else
        {
            pos = ClampValue(pos, 0, maxY);
            g_app.viewCenterZ = static_cast<double>(metrics.baseBounds.maxZ) - static_cast<double>(pos) - metrics.visibleWorldHeight * 0.5;
        }

        g_app.viewInitialized = true;
        ClampViewCenterToDocument(rawBounds);
        UpdateCanvasScrollBars(hwnd);
        InvalidateRect(hwnd, nullptr, TRUE);
        RefreshStatus();
    }

    POINT WorldToScreen(const RECT& rc, const mapfmt::Bounds& bounds, int x, int z)
    {
        const CanvasMetrics metrics = GetCanvasMetrics(rc, bounds);
        POINT pt{};
        pt.x = static_cast<LONG>(std::lround(metrics.centerPixelX + (static_cast<double>(x) - metrics.centerWorldX) * metrics.scale));
        pt.y = static_cast<LONG>(std::lround(metrics.centerPixelY + (metrics.centerWorldZ - static_cast<double>(z)) * metrics.scale));
        return pt;
    }

    POINT ScreenToWorldPrecise(const RECT& rc, const mapfmt::Bounds& bounds, int sx, int sy)
    {
        const CanvasMetrics metrics = GetCanvasMetrics(rc, bounds);
        POINT pt{};
        pt.x = static_cast<LONG>(std::lround(metrics.centerWorldX + (static_cast<double>(sx) - metrics.centerPixelX) / metrics.scale));
        pt.y = static_cast<LONG>(std::lround(metrics.centerWorldZ - (static_cast<double>(sy) - metrics.centerPixelY) / metrics.scale));
        return pt;
    }

    POINT ScreenToWorld(const RECT& rc, const mapfmt::Bounds& bounds, int sx, int sy)
    {
        return ScreenToWorldPrecise(rc, bounds, sx, sy);
    }

    struct WallEndpointMatch
    {
        int zoneIndex = -1;
        int endpointIndex = -1; // 0 = x1/z1, 1 = x2/z2
        double distanceSq = 0.0;
        POINT point{};
    };

    bool FindNearestWallEndpoint(POINT worldPoint, int excludeZone, double radiusWorld, WallEndpointMatch& outMatch)
    {
        double bestDistSq = radiusWorld * radiusWorld;
        bool found = false;
        const int canonicalExclude = GetCanonicalWallIndex(excludeZone);

        for (int zoneIndex = 0; zoneIndex < static_cast<int>(g_app.document.zones.size()); ++zoneIndex)
        {
            if (zoneIndex == excludeZone || GetCanonicalWallIndex(zoneIndex) == canonicalExclude)
            {
                continue;
            }
            if (IsVisualBackfaceWallIndex(zoneIndex))
            {
                continue;
            }

            const auto& zone = g_app.document.zones[zoneIndex];
            if (zone.ztype != static_cast<int>(mapfmt::ZoneType::Wall))
            {
                continue;
            }

            const POINT endpoints[2] = {
                { static_cast<LONG>(zone.x1), static_cast<LONG>(zone.z1) },
                { static_cast<LONG>(zone.x2), static_cast<LONG>(zone.z2) }
            };

            for (int endpointIndex = 0; endpointIndex < 2; ++endpointIndex)
            {
                const POINT& endpoint = endpoints[endpointIndex];
                const double dx = static_cast<double>(worldPoint.x) - static_cast<double>(endpoint.x);
                const double dz = static_cast<double>(worldPoint.y) - static_cast<double>(endpoint.y);
                const double distSq = dx * dx + dz * dz;
                if (distSq <= bestDistSq)
                {
                    bestDistSq = distSq;
                    outMatch.zoneIndex = zoneIndex;
                    outMatch.endpointIndex = endpointIndex;
                    outMatch.distanceSq = distSq;
                    outMatch.point = endpoint;
                    found = true;
                }
            }
        }

        return found;
    }

    int SnapWorldCoordinateToFineGrid(int value)
    {
        const double step = static_cast<double>(kGridSnapStep);
        return static_cast<int>(std::lround(static_cast<double>(value) / step) * step);
    }

    POINT SnapWorldPointToFineGrid(POINT point)
    {
        point.x = SnapWorldCoordinateToFineGrid(point.x);
        point.y = SnapWorldCoordinateToFineGrid(point.y);
        return point;
    }

    bool FindNearbyFineGridPoint(POINT point, double radiusWorld, POINT& outPoint)
    {
        const POINT gridPoint = SnapWorldPointToFineGrid(point);
        const double dx = static_cast<double>(point.x) - static_cast<double>(gridPoint.x);
        const double dz = static_cast<double>(point.y) - static_cast<double>(gridPoint.y);
        if ((dx * dx + dz * dz) > (radiusWorld * radiusWorld))
        {
            return false;
        }

        outPoint = gridPoint;
        return true;
    }

    POINT SnapWorldPointToNearbyFineGrid(POINT point)
    {
        POINT gridPoint{};
        return FindNearbyFineGridPoint(point, kGridPointSnapWorldRadius, gridPoint) ? gridPoint : point;
    }

    POINT ApplyWallEndpointSnap(const RECT& rc, POINT worldPoint)
    {
        UNREFERENCED_PARAMETER(rc);
        // Drawing/edit snapping is deliberately grid-point based: the cursor always
        // lands on the nearest 1/8 tile point and never on a farther wall endpoint.
        return SnapWorldPointToFineGrid(worldPoint);
    }

    POINT PrepareDrawStartPoint(const RECT& rc, POINT rawPoint, bool allowWallEndpointSnap)
    {
        UNREFERENCED_PARAMETER(rc);
        UNREFERENCED_PARAMETER(allowWallEndpointSnap);
        // Wall, event and monster-zone line drawing all start on the nearest
        // 1/8 tile point.  This avoids the old endpoint search pulling a new
        // line onto an unrelated nearby wall segment.
        return SnapWorldPointToFineGrid(rawPoint);
    }

    POINT ConstrainWallEndpoint45(POINT start, POINT current)
    {
        const int dx = current.x - start.x;
        const int dz = current.y - start.y;
        const int adx = std::abs(dx);
        const int adz = std::abs(dz);
        if (adx < 1 && adz < 1)
        {
            return current;
        }

        POINT out = current;
        // Choose the nearest of horizontal, vertical, or 45-degree diagonal.
        if (adx > adz * 2)
        {
            out.y = start.y;
        }
        else if (adz > adx * 2)
        {
            out.x = start.x;
        }
        else
        {
            const int len = MaxValue(adx, adz);
            out.x = start.x + ((dx < 0) ? -len : len);
            out.y = start.y + ((dz < 0) ? -len : len);
        }
        return out;
    }

    bool SnapWallLengthToTextureElement(POINT start, POINT current, POINT& out)
    {
        UNREFERENCED_PARAMETER(start);
        out = current;
        // Disabled intentionally: drawing snaps must be point-based only.  The old
        // texture-length snap forced short diagonals to 256 world units (previously 8 fine-grid
        // points), which made short diagonal wall segments jump past the intended
        // endpoint when starting a branch.
        return false;
    }

    bool PointsEqualWithinWorldUnits(POINT a, POINT b, int tolerance)
    {
        return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance;
    }

    POINT PrepareWallDrawEndpoint(const RECT& rc, POINT rawPoint)
    {
        // Always use the mathematically nearest 1/8 tile point.  Do not snap to
        // arbitrary existing endpoints, because that can move the cursor beyond
        // the intended point or onto a different line inside the same tile.
        POINT point = SnapWorldPointToFineGrid(rawPoint);

        const bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (shiftDown)
        {
            g_app.drawWallAngleLock = true;
        }

        if (g_app.drawWallAngleLock)
        {
            point = ConstrainWallEndpoint45(g_app.drawStartWorld, point);
            point = SnapWorldPointToFineGrid(point);
        }

        g_app.drawWallLengthSnapLock = false;
        UNREFERENCED_PARAMETER(rc);
        return point;
    }

    POINT PrepareLineZoneDrawEndpoint(POINT rawPoint)
    {
        // Event trigger lines and monster-zone lines follow the same exact
        // point grid as walls: nearest 1/8 tile point, no endpoint magnet.
        POINT point = SnapWorldPointToFineGrid(rawPoint);
        const bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (shiftDown)
        {
            g_app.drawWallAngleLock = true;
        }
        if (g_app.drawWallAngleLock)
        {
            point = ConstrainWallEndpoint45(g_app.drawStartWorld, point);
            point = SnapWorldPointToFineGrid(point);
        }
        return point;
    }


    bool SnapPointToNearestWallEndpoint(POINT& point, int excludeZone)
    {
        WallEndpointMatch match{};
        if (!FindNearestWallEndpoint(point, excludeZone, kCornerWeldWorldRadius, match))
        {
            return false;
        }

        point = match.point;
        return true;
    }

    void SetWallEndpoint(mapfmt::Zone& zone, int endpointIndex, POINT point)
    {
        if (endpointIndex == 0)
        {
            zone.x1 = ClampWorldToInt16(point.x);
            zone.z1 = ClampWorldToInt16(point.y);
        }
        else
        {
            zone.x2 = ClampWorldToInt16(point.x);
            zone.z2 = ClampWorldToInt16(point.y);
        }
    }

    POINT GetWallEndpoint(const mapfmt::Zone& zone, int endpointIndex)
    {
        if (endpointIndex == 0)
        {
            return POINT{ static_cast<LONG>(zone.x1), static_cast<LONG>(zone.z1) };
        }
        return POINT{ static_cast<LONG>(zone.x2), static_cast<LONG>(zone.z2) };
    }

    void RecalculateWallZoneAfterEndpointMove(int zoneIndex)
    {
        if (zoneIndex < 0 || zoneIndex >= static_cast<int>(g_app.document.zones.size()))
        {
            return;
        }

        auto& zone = g_app.document.zones[zoneIndex];
        if (zone.ztype != static_cast<int>(mapfmt::ZoneType::Wall))
        {
            return;
        }

        // Do not auto-extend a collapsed/same-point wall to a full 256-unit cell.
        // That made point-snapped drawing previews suddenly become 8 fine-grid points long.
        // RecalculateWallMetadata() keeps the internal metadata safe with a minimal 1-unit fallback.
        mapfmt::RecalculateWallMetadata(zone);
        UpdateWallTextureBandCountFromLength(zone);
    }

    void WeldWallEndpointClusterToPoint(POINT anchor)
    {
        const double radiusSq = kCornerClusterWeldWorldRadius * kCornerClusterWeldWorldRadius;
        std::vector<int> changedZones;

        for (int zoneIndex = 0; zoneIndex < static_cast<int>(g_app.document.zones.size()); ++zoneIndex)
        {
            auto& zone = g_app.document.zones[zoneIndex];
            if (zone.ztype != static_cast<int>(mapfmt::ZoneType::Wall))
            {
                continue;
            }

            for (int endpointIndex = 0; endpointIndex < 2; ++endpointIndex)
            {
                const POINT endpoint = GetWallEndpoint(zone, endpointIndex);
                const double dx = static_cast<double>(endpoint.x) - static_cast<double>(anchor.x);
                const double dz = static_cast<double>(endpoint.y) - static_cast<double>(anchor.y);
                if ((dx * dx + dz * dz) <= radiusSq)
                {
                    SetWallEndpoint(zone, endpointIndex, anchor);
                    changedZones.push_back(zoneIndex);
                }
            }
        }

        std::sort(changedZones.begin(), changedZones.end());
        changedZones.erase(std::unique(changedZones.begin(), changedZones.end()), changedZones.end());
        for (int zoneIndex : changedZones)
        {
            RecalculateWallZoneAfterEndpointMove(zoneIndex);
        }
    }

    void ReverseWallDirectionPreservingTexture(mapfmt::Zone& zone)
    {
        std::swap(zone.x1, zone.x2);
        std::swap(zone.z1, zone.z2);

        const int activeBands = GetZonePreviewSlotCount(zone);
        for (int i = 0; i < activeBands / 2; ++i)
        {
            std::swap(zone.textures[i], zone.textures[activeBands - 1 - i]);
        }

        mapfmt::RecalculateWallMetadata(zone);
    }


    bool IsWallZone(const mapfmt::Zone& zone)
    {
        return zone.ztype == static_cast<int16_t>(mapfmt::ZoneType::Wall);
    }

    bool SamePointWithin(int ax, int az, int bx, int bz, int tolerance)
    {
        return std::abs(ax - bx) <= tolerance && std::abs(az - bz) <= tolerance;
    }

    bool AreReverseWallPair(const mapfmt::Zone& a, const mapfmt::Zone& b)
    {
        if (!IsWallZone(a) || !IsWallZone(b))
        {
            return false;
        }

        constexpr int kPairTolerance = 2;
        return SamePointWithin(a.x1, a.z1, b.x2, b.z2, kPairTolerance) &&
               SamePointWithin(a.x2, a.z2, b.x1, b.z1, kPairTolerance);
    }

    bool IsVisualBackfaceWallIndex(int zoneIndex)
    {
        if (zoneIndex < 0 || zoneIndex >= static_cast<int>(g_app.document.zones.size()))
        {
            return false;
        }
        if (!IsWallZone(g_app.document.zones[zoneIndex]))
        {
            return false;
        }

        // Editor-created two-sided walls are stored as a front wall followed by an
        // exact reversed visual backface. Snapping/orientation must ignore the
        // later backface; otherwise new corners can weld to the duplicate reverse
        // endpoint and inherit the wrong normal direction. That is a common cause
        // of "sticky" seams while walking along a wall chain.
        for (int i = 0; i < zoneIndex; ++i)
        {
            if (AreReverseWallPair(g_app.document.zones[i], g_app.document.zones[zoneIndex]))
            {
                return true;
            }
        }
        return false;
    }

    int GetCanonicalWallIndex(int zoneIndex)
    {
        if (zoneIndex < 0 || zoneIndex >= static_cast<int>(g_app.document.zones.size()))
        {
            return zoneIndex;
        }
        if (!IsWallZone(g_app.document.zones[zoneIndex]))
        {
            return zoneIndex;
        }

        for (int i = 0; i < zoneIndex; ++i)
        {
            if (AreReverseWallPair(g_app.document.zones[i], g_app.document.zones[zoneIndex]))
            {
                return i;
            }
        }
        return zoneIndex;
    }

    void NormalizeSelectedWallToCanonical()
    {
        const int canonical = GetCanonicalWallIndex(g_app.selectedZone);
        if (canonical >= 0 && canonical < static_cast<int>(g_app.document.zones.size()))
        {
            g_app.selectedZone = canonical;
        }
    }

    bool PointsCloseForDirection(POINT a, POINT b)
    {
        return std::abs(a.x - b.x) <= 2 && std::abs(a.y - b.y) <= 2;
    }

    bool WallsShareEndpoint(const mapfmt::Zone& a, const mapfmt::Zone& b)
    {
        const POINT a1{ static_cast<LONG>(a.x1), static_cast<LONG>(a.z1) };
        const POINT a2{ static_cast<LONG>(a.x2), static_cast<LONG>(a.z2) };
        const POINT b1{ static_cast<LONG>(b.x1), static_cast<LONG>(b.z1) };
        const POINT b2{ static_cast<LONG>(b.x2), static_cast<LONG>(b.z2) };
        return PointsCloseForDirection(a1, b1) || PointsCloseForDirection(a1, b2) ||
               PointsCloseForDirection(a2, b1) || PointsCloseForDirection(a2, b2);
    }

    bool AreNearlyCollinearForCollision(const mapfmt::Zone& a, const mapfmt::Zone& b)
    {
        const double ax = static_cast<double>(a.x2) - static_cast<double>(a.x1);
        const double az = static_cast<double>(a.z2) - static_cast<double>(a.z1);
        const double bx = static_cast<double>(b.x2) - static_cast<double>(b.x1);
        const double bz = static_cast<double>(b.z2) - static_cast<double>(b.z1);
        const double alen = std::hypot(ax, az);
        const double blen = std::hypot(bx, bz);
        if (alen < 1.0 || blen < 1.0)
        {
            return false;
        }

        const double cross = std::abs(ax * bz - az * bx);
        return cross <= (alen * blen * 0.035);
    }

    void NormalizeCollinearConnectedWallsAroundZone(int zoneIndex)
    {
        zoneIndex = GetCanonicalWallIndex(zoneIndex);
        if (zoneIndex < 0 || zoneIndex >= static_cast<int>(g_app.document.zones.size()))
        {
            return;
        }
        auto& zone = g_app.document.zones[zoneIndex];
        if (!IsWallZone(zone))
        {
            return;
        }

        for (int otherIndex = 0; otherIndex < static_cast<int>(g_app.document.zones.size()); ++otherIndex)
        {
            if (otherIndex == zoneIndex || IsVisualBackfaceWallIndex(otherIndex))
            {
                continue;
            }
            const auto& other = g_app.document.zones[otherIndex];
            if (!IsWallZone(other) || !WallsShareEndpoint(zone, other) || !AreNearlyCollinearForCollision(zone, other))
            {
                continue;
            }

            const int ax = static_cast<int>(zone.x2) - static_cast<int>(zone.x1);
            const int az = static_cast<int>(zone.z2) - static_cast<int>(zone.z1);
            const int bx = static_cast<int>(other.x2) - static_cast<int>(other.x1);
            const int bz = static_cast<int>(other.z2) - static_cast<int>(other.z1);
            const int dot = ax * bx + az * bz;

            // Adjacent pieces of the same straight wall must use the same segment
            // direction. If one segment is reversed, its collision normal points the
            // opposite way. Gloom's movement resolver then catches the player at the
            // seam even though the visible endpoints look closed.
            if (dot < 0)
            {
                ReverseWallDirectionPreservingTexture(zone);
                break;
            }
        }

        mapfmt::RecalculateWallMetadata(zone);
        UpdateWallTextureBandCountFromLength(zone);
        const int backface = FindReverseWallPairIndex(zoneIndex);
        if (backface >= 0)
        {
            SyncBackfaceWallFromFront(zoneIndex, backface);
        }
    }

    void CleanupWallsForGameplaySave()
    {
        // Normalize front/back wall pairs and same-line wall chains before writing
        // the map. This is intentionally conservative: it does not alter non-wall
        // zones and it ignores the visual backfaces while making collision-facing
        // decisions.
        for (int i = 0; i < static_cast<int>(g_app.document.zones.size()); ++i)
        {
            if (IsVisualBackfaceWallIndex(i))
            {
                continue;
            }
            NormalizeCollinearConnectedWallsAroundZone(i);
            if (i >= 0 && i < static_cast<int>(g_app.document.zones.size()) && IsWallZone(g_app.document.zones[i]))
            {
                mapfmt::RecalculateWallMetadata(g_app.document.zones[i]);
                UpdateWallTextureBandCountFromLength(g_app.document.zones[i]);
                EnsureBackfaceForWallAtIndex(i);
            }
        }
    }

    mapfmt::Zone MakeBackfaceWallFromFront(const mapfmt::Zone& front)
    {
        mapfmt::Zone back = front;
        ReverseWallDirectionPreservingTexture(back);
        return back;
    }

    int FindReverseWallPairIndex(int zoneIndex)
    {
        if (zoneIndex < 0 || zoneIndex >= static_cast<int>(g_app.document.zones.size()))
        {
            return -1;
        }

        const auto& zone = g_app.document.zones[zoneIndex];
        if (!IsWallZone(zone))
        {
            return -1;
        }

        for (int i = 0; i < static_cast<int>(g_app.document.zones.size()); ++i)
        {
            if (i == zoneIndex)
            {
                continue;
            }
            if (AreReverseWallPair(zone, g_app.document.zones[i]))
            {
                return i;
            }
        }

        return -1;
    }

    void SyncBackfaceWallFromFront(int frontIndex, int backIndex)
    {
        if (frontIndex < 0 || frontIndex >= static_cast<int>(g_app.document.zones.size()) ||
            backIndex < 0 || backIndex >= static_cast<int>(g_app.document.zones.size()) ||
            frontIndex == backIndex)
        {
            return;
        }

        const mapfmt::Zone front = g_app.document.zones[frontIndex];
        if (!IsWallZone(front))
        {
            return;
        }

        g_app.document.zones[backIndex] = MakeBackfaceWallFromFront(front);
    }

    void EnsureBackfaceForWallAtIndex(int zoneIndex)
    {
        if (zoneIndex < 0 || zoneIndex >= static_cast<int>(g_app.document.zones.size()))
        {
            return;
        }

        const auto& zone = g_app.document.zones[zoneIndex];
        if (!IsWallZone(zone))
        {
            return;
        }

        const int existingBackface = FindReverseWallPairIndex(zoneIndex);
        if (existingBackface >= 0)
        {
            SyncBackfaceWallFromFront(zoneIndex, existingBackface);
            return;
        }

        g_app.document.zones.push_back(MakeBackfaceWallFromFront(zone));
    }

    void OrientWallForConnectedEndpoints(int zoneIndex)
    {
        if (zoneIndex < 0 || zoneIndex >= static_cast<int>(g_app.document.zones.size()))
        {
            return;
        }

        auto& zone = g_app.document.zones[zoneIndex];
        if (zone.ztype != static_cast<int>(mapfmt::ZoneType::Wall))
        {
            return;
        }

        const POINT p1{ static_cast<LONG>(zone.x1), static_cast<LONG>(zone.z1) };
        const POINT p2{ static_cast<LONG>(zone.x2), static_cast<LONG>(zone.z2) };
        WallEndpointMatch match1{};
        WallEndpointMatch match2{};
        const bool has1 = FindNearestWallEndpoint(p1, zoneIndex, kEndpointExactWorldRadius, match1);
        const bool has2 = FindNearestWallEndpoint(p2, zoneIndex, kEndpointExactWorldRadius, match2);
        if (!has1 && !has2)
        {
            return;
        }

        // Gloom wall rendering is direction-sensitive. When a new wall is welded
        // onto an existing endpoint, keep the segment direction continuous:
        // existing end (x2/z2) -> new start, or new end -> existing start (x1/z1).
        // Without this, connected walls can be solid but only textured from one side.
        int keepScore = 0;
        int reverseScore = 0;
        if (has1)
        {
            keepScore += (match1.endpointIndex == 1) ? 1 : 0;
            reverseScore += (match1.endpointIndex == 0) ? 1 : 0;
        }
        if (has2)
        {
            keepScore += (match2.endpointIndex == 0) ? 1 : 0;
            reverseScore += (match2.endpointIndex == 1) ? 1 : 0;
        }

        if (reverseScore > keepScore)
        {
            ReverseWallDirectionPreservingTexture(zone);
        }
    }

    void WeldWallCornersAroundZone(int zoneIndex)
    {
        if (zoneIndex < 0 || zoneIndex >= static_cast<int>(g_app.document.zones.size()))
        {
            return;
        }
        auto& zone = g_app.document.zones[zoneIndex];
        if (zone.ztype != static_cast<int>(mapfmt::ZoneType::Wall))
        {
            return;
        }

        POINT p1{ static_cast<LONG>(zone.x1), static_cast<LONG>(zone.z1) };
        POINT p2{ static_cast<LONG>(zone.x2), static_cast<LONG>(zone.z2) };
        const bool snapped1 = SnapPointToNearestWallEndpoint(p1, zoneIndex);
        const bool snapped2 = SnapPointToNearestWallEndpoint(p2, zoneIndex);
        if (snapped1 || snapped2)
        {
            zone.x1 = ClampWorldToInt16(p1.x);
            zone.z1 = ClampWorldToInt16(p1.y);
            zone.x2 = ClampWorldToInt16(p2.x);
            zone.z2 = ClampWorldToInt16(p2.y);
            // Keep same-point snaps point-sized. Do not turn them into a full 256-unit wall.
            mapfmt::RecalculateWallMetadata(zone);
            UpdateWallTextureBandCountFromLength(zone);
        }

        // After a line has snapped to a nearby corner, force only truly identical
        // front/back endpoints onto the exact same coordinate.  Older builds used
        // a large weld radius here; with 4 snap points per tile that collapsed
        // neighbouring half-cell points into unrelated wall corners.
        p1 = POINT{ static_cast<LONG>(zone.x1), static_cast<LONG>(zone.z1) };
        p2 = POINT{ static_cast<LONG>(zone.x2), static_cast<LONG>(zone.z2) };
        WeldWallEndpointClusterToPoint(p1);
        WeldWallEndpointClusterToPoint(p2);

        OrientWallForConnectedEndpoints(zoneIndex);
    }

    int SnapWorldCoordinate(int value)
    {
        return SnapWorldCoordinateToFineGrid(value);
    }

    int16_t ClampWorldToInt16(int value)
    {
        return static_cast<int16_t>(ClampValue(value, -32768, 32767));
    }

    bool IsTinyDrawSegment(POINT a, POINT b)
    {
        return std::hypot(static_cast<double>(a.x - b.x), static_cast<double>(a.y - b.y)) < 4.0;
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
            mode == InsertMode::MonsterZone || mode == InsertMode::LevelEnd ? static_cast<int>(mapfmt::ZoneType::MonsterZone) :
            static_cast<int>(mapfmt::ZoneType::EventTrigger));
        if (mode == InsertMode::EventTrigger)
        {
            // New trigger lines start deliberately unassigned.  A Link Event tool
            // assigns the first free non-initial event slot when the trigger is
            // actually linked to a target.
            zone.ev = 0;
        }
        else if (mode == InsertMode::MonsterZone)
        {
            zone.ev = 0;
        }
        else if (mode == InsertMode::LevelEnd)
        {
            zone.ev = kLevelEndEventValue;
        }

        if (IsLineInsertMode(mode))
        {
            // Walls, event triggers and monster-zone markers are all line segments in
            // the original map format. A trigger fires when the player crosses/touches
            // the segment; it is not a filled rectangle.
            zone.x1 = ClampWorldToInt16(a.x);
            zone.z1 = ClampWorldToInt16(a.y);
            zone.x2 = ClampWorldToInt16(b.x);
            zone.z2 = ClampWorldToInt16(b.y);
            // Keep same-point snaps point-sized. Do not turn them into a full 256-unit wall.
            mapfmt::RecalculateWallMetadata(zone);
            if (mode == InsertMode::Wall)
            {
                InitializeNewWallTextureSequence(zone);
                UpdateWallTextureBandCountFromLength(zone);
                // Keep the live Draw Wall mapping authoritative for both the
                // 2D rubber-band line and the 3D Walk Preview. Classification
                // from zone.sc is ambiguous for freshly-built preview zones
                // (especially 2/8 Clip), so re-apply the active picker mode last.
                ApplyWallTextureModeToZone(zone, g_app.activeWallTextureMode);
            }
        }
        else
        {
            zone.x1 = ClampWorldToInt16(a.x);
            zone.z1 = ClampWorldToInt16(a.y);
            zone.x2 = ClampWorldToInt16(b.x);
            zone.z2 = ClampWorldToInt16(b.y);
            mapfmt::RecalculateWallMetadata(zone);
        }

        return zone;
    }

    void CommitDrawnZone()
    {
        if (!g_app.isDrawing || g_app.insertMode == InsertMode::None) return;
        if (IsTinyDrawSegment(g_app.drawStartWorld, g_app.drawCurrentWorld))
        {
            g_app.isDrawing = false;
            g_app.drawWallAngleLock = false;
            g_app.drawWallLengthSnapLock = false;
            RefreshStatus();
            InvalidateEditorViews();
            return;
        }
        if (g_app.insertMode == InsertMode::LevelEnd)
        {
            g_app.isDrawing = false;
            g_app.drawWallAngleLock = false;
            g_app.drawWallLengthSnapLock = false;
            SetLevelEndFromDrawPoints(g_app.drawStartWorld, g_app.drawCurrentWorld);
            return;
        }

        const bool skipAutoWeld = (g_app.insertMode == InsertMode::Wall && g_app.drawWallAngleLock);
        const auto zone = BuildZoneFromDrawPoints(g_app.insertMode, g_app.drawStartWorld, g_app.drawCurrentWorld);
        PushUndoSnapshot();
        g_app.document.zones.push_back(zone);
        g_app.selectedZone = static_cast<int>(g_app.document.zones.size()) - 1;
        if (g_app.insertMode == InsertMode::Wall)
        {
            if (!skipAutoWeld)
            {
                WeldWallCornersAroundZone(g_app.selectedZone);
            }
            else
            {
                OrientWallForConnectedEndpoints(g_app.selectedZone);
            }
            NormalizeCollinearConnectedWallsAroundZone(g_app.selectedZone);
            EnsureBackfaceForWallAtIndex(g_app.selectedZone);
            if (g_app.selectedZone >= 0 && g_app.selectedZone < static_cast<int>(g_app.document.zones.size()))
            {
                const auto& finalZone = g_app.document.zones[g_app.selectedZone];
                if (finalZone.ztype == static_cast<int16_t>(mapfmt::ZoneType::Wall))
                {
                    WeldWallEndpointClusterToPoint(POINT{ static_cast<LONG>(finalZone.x1), static_cast<LONG>(finalZone.z1) });
                    WeldWallEndpointClusterToPoint(POINT{ static_cast<LONG>(finalZone.x2), static_cast<LONG>(finalZone.z2) });
                    const int backfaceIndex = FindReverseWallPairIndex(g_app.selectedZone);
                    if (backfaceIndex >= 0)
                    {
                        SyncBackfaceWallFromFront(g_app.selectedZone, backfaceIndex);
                    }
                }
            }
        }
        g_app.previewTextureBand = 0;
        g_app.isDrawing = false;
        g_app.drawWallAngleLock = false;
        g_app.drawWallLengthSnapLock = false;
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
            if (IsVisualBackfaceWallIndex(i))
            {
                continue;
            }
            const auto& zone = g_app.document.zones[i];
            POINT p1 = WorldToScreen(rc, bounds, zone.x1, zone.z1);
            POINT p2 = WorldToScreen(rc, bounds, zone.x2, zone.z2);
            double distance = 1e30;

            if (IsLinearZoneType(zone.ztype))
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
                const int cx = ClampValue(sx, static_cast<int>(zr.left), static_cast<int>(zr.right));
                const int cy = ClampValue(sy, static_cast<int>(zr.top), static_cast<int>(zr.bottom));
                distance = std::hypot(static_cast<double>(sx - cx), static_cast<double>(sy - cy));
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

    void DrawWalkPreviewCameraOverlay(HDC hdc, const RECT& rc, const mapfmt::Bounds& bounds)
    {
        if (!g_app.walkPreviewInitialized) return;

        const POINT pos = WorldToScreen(rc, bounds,
            static_cast<int>(std::lround(g_app.walkPreviewX)),
            static_cast<int>(std::lround(g_app.walkPreviewZ)));

        double fx = 0.0, fz = 1.0, rx = 1.0, rz = 0.0;
        WalkPreviewDirectionVectors(g_app.walkPreviewDir, fx, fz, rx, rz);
        const POINT tip = WorldToScreen(rc, bounds,
            static_cast<int>(std::lround(g_app.walkPreviewX + fx * 96.0)),
            static_cast<int>(std::lround(g_app.walkPreviewZ + fz * 96.0)));

        HBRUSH bodyBrush = CreateSolidBrush(RGB(94, 174, 255));
        HPEN outlinePen = CreatePen(PS_SOLID, 2, RGB(255, 245, 170));
        HGDIOBJ oldBrush = SelectObject(hdc, bodyBrush);
        HGDIOBJ oldPen = SelectObject(hdc, outlinePen);
        Ellipse(hdc, pos.x - 6, pos.y - 6, pos.x + 6, pos.y + 6);
        MoveToEx(hdc, pos.x, pos.y, nullptr);
        LineTo(hdc, tip.x, tip.y);

        // Small arrow head.  Use screen-space direction, so it stays readable at all zoom levels.
        const double dx = static_cast<double>(tip.x - pos.x);
        const double dy = static_cast<double>(tip.y - pos.y);
        const double len = MaxValue(1.0, std::sqrt(dx * dx + dy * dy));
        const double ux = dx / len;
        const double uy = dy / len;
        const double px = -uy;
        const double py = ux;
        const int wing = 8;
        POINT head[3] = {
            { tip.x, tip.y },
            { static_cast<LONG>(std::lround(tip.x - ux * wing + px * (wing * 0.55))), static_cast<LONG>(std::lround(tip.y - uy * wing + py * (wing * 0.55))) },
            { static_cast<LONG>(std::lround(tip.x - ux * wing - px * (wing * 0.55))), static_cast<LONG>(std::lround(tip.y - uy * wing - py * (wing * 0.55))) }
        };
        Polygon(hdc, head, 3);

        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(outlinePen);
        DeleteObject(bodyBrush);

        DrawZoneOverlayLabel(hdc, L"3D", pos.x + 8, pos.y - 8, RGB(190, 225, 255));
    }

    void DrawTeleportMarker(HDC hdc, POINT center, int rot, bool selected, const std::wstring& label)
    {
        const double angle = (static_cast<double>(rot & 255) * 6.28318530717958647692) / 256.0;
        const double sx = std::sin(angle);
        const double sy = -std::cos(angle);
        const int arrowLen = selected ? 42 : 32;
        const POINT tip{
            center.x + static_cast<LONG>(std::lround(sx * static_cast<double>(arrowLen))),
            center.y + static_cast<LONG>(std::lround(sy * static_cast<double>(arrowLen)))
        };

        HBRUSH fill = CreateSolidBrush(selected ? RGB(165, 105, 255) : RGB(130, 95, 230));
        HPEN pen = CreatePen(PS_SOLID, selected ? 4 : 2, selected ? RGB(255, 255, 255) : RGB(235, 210, 255));
        HGDIOBJ oldBrush = SelectObject(hdc, fill);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        const int radius = selected ? 10 : 7;
        Ellipse(hdc, center.x - radius, center.y - radius, center.x + radius + 1, center.y + radius + 1);
        MoveToEx(hdc, center.x, center.y, nullptr);
        LineTo(hdc, tip.x, tip.y);

        const double sideX = std::cos(angle);
        const double sideY = std::sin(angle);
        POINT head[3] = {
            tip,
            POINT{ tip.x - static_cast<LONG>(std::lround(sx * 11.0 + sideX * 6.0)),
                   tip.y - static_cast<LONG>(std::lround(sy * 11.0 - sideY * 6.0)) },
            POINT{ tip.x - static_cast<LONG>(std::lround(sx * 11.0 - sideX * 6.0)),
                   tip.y - static_cast<LONG>(std::lround(sy * 11.0 + sideY * 6.0)) }
        };
        Polygon(hdc, head, 3);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(pen);
        DeleteObject(fill);

        DrawZoneOverlayLabel(hdc, label, center.x + 10, center.y - 28, selected ? RGB(255, 255, 255) : RGB(235, 210, 255));
    }

    void DrawTeleportDirectionSelectionOverlay(HDC hdc, const RECT& rc, const mapfmt::Bounds& bounds)
    {
        if (g_app.insertMode != InsertMode::SetTeleportTarget || !g_app.teleportTargetAwaitDirection)
        {
            return;
        }

        const POINT center = WorldToScreen(rc, bounds, g_app.teleportTargetWorld.x, g_app.teleportTargetWorld.y);
        std::wstringstream label;
        label << L"Teleport dir " << MonsterRotationDegrees(g_app.teleportTargetRotation) << L"°";
        DrawTeleportMarker(hdc, center, g_app.teleportTargetRotation, true, label.str());
    }

    void DrawTeleportTargetOverlays(HDC hdc, const RECT& rc, const mapfmt::Bounds& bounds)
    {
        int markerIndex = 0;
        for (int eventIndex = 0; eventIndex < static_cast<int>(g_app.document.events.size()); ++eventIndex)
        {
            const auto& script = g_app.document.events[eventIndex];
            for (int commandIndex = 0; commandIndex < static_cast<int>(script.commands.size()); ++commandIndex)
            {
                const auto& command = script.commands[commandIndex];
                if (command.type != mapfmt::CommandType::Teleport)
                {
                    continue;
                }

                const bool selected = g_app.selectedTeleportTarget.eventIndex == eventIndex &&
                    g_app.selectedTeleportTarget.commandIndex == commandIndex;
                const POINT tp = WorldToScreen(rc, bounds, command.params[0], command.params[2]);
                std::wstringstream label;
                label << L"Teleport E" << (eventIndex + 1);
                DrawTeleportMarker(hdc, tp, command.params[3] & 255, selected, label.str());
                ++markerIndex;
            }
        }
    }

    std::wstring GetCanvasHelpText()
    {
        if (g_app.isDrawing)
        {
            if (g_app.insertMode == InsertMode::Wall)
            {
                return L"Release the mouse to create the wall. Hold Shift for straight/45° lines.";
            }
            if (g_app.insertMode == InsertMode::LevelEnd)
            {
                return L"Release the mouse to place the level-end crossing line.";
            }
            return L"Release the mouse to create this line. Esc cancels the current tool.";
        }

        switch (g_app.insertMode)
        {
        case InsertMode::Wall:
            return L"Select a wall- or door-texture on the right and start drawing walls";
        case InsertMode::MonsterZone:
            return L"Draw Monster Zone: drag a helper/logic line. Real enemies are placed with Place Enemy.";
        case InsertMode::EventTrigger:
            return L"Draw Event Zone: drag a crossing line, then link its Event to a wall, door, teleport or even enemies and objects.";
        case InsertMode::ObjectSpawn:
            return L"Place Object: click the map to add it as a map-start object. Use Link Event > Enemy/Objects only when it should be trigger-spawned.";
        case InsertMode::PlayerStart:
            return L"Player Start: click the map to place P1. Right-click anywhere to set the 3D preview camera.";
        case InsertMode::LevelEnd:
            return L"Level End: drag the crossing line where the level should finish.";
        case InsertMode::LinkEventToZone:
            return L"Link Event > Wall/Door: click the wall or door this trigger should move/open.";
        case InsertMode::LinkEventToSwitchTexture:
            return L"Link Event > Switch/Trigger: click the OFF switch/wall panel. Static panels use OFF+1; animated panels use the first texture after the OFF animation.";
        case InsertMode::LinkEventToEnemyObject:
            return L"Link Event > Enemy/Objects: click an existing enemy, object, pickup or ammo marker to trigger it from this event.";
        case InsertMode::DeleteLinkEventToEnemyObject:
            return L"Delete Link Event: click an existing enemy, object, pickup or ammo marker to remove it from this trigger event.";
        case InsertMode::LinkEventToRotateClockwise:
            return L"Link Event > Rotate CW: click the first wall of the rotating group. Connected walls are detected automatically.";
        case InsertMode::LinkEventToRotateCounterClockwise:
            return L"Link Event > Rotate CCW: click the first wall of the rotating group. Connected walls are detected automatically.";
        case InsertMode::LinkEventToMoveWallGroup:
            return g_app.pendingMoveWallGroupSourceZone >= 0
                ? L"Move Wallblock: move the copied block with the mouse, then click the target position."
                : L"Move Wallblock: click one wall segment of the block that should move.";
        case InsertMode::SetTeleportTarget:
            if (g_app.teleportTargetAwaitDirection)
            {
                return L"Teleport Target: Left/Right rotates the view direction. Click away or press Enter to fix it.";
            }
            return L"Teleport Target: click the destination point. Then rotate the view with Left/Right.";
        case InsertMode::None:
        default:
            break;
        }

        if (IsSelectedTeleportTargetValid())
        {
            return L"Selected Teleport: use Left/Right to rotate the arrival view. Delete removes the target link; the trigger stays.";
        }
        if (IsSelectedMonsterSpawnValid())
        {
            return L"Selected Object: use Left/Right to rotate it. Delete removes only this Event command.";
        }
        if (g_app.selectedZone >= 0 && g_app.selectedZone < static_cast<int>(g_app.document.zones.size()))
        {
            const auto& zone = g_app.document.zones[g_app.selectedZone];
            if (zone.ztype == static_cast<int16_t>(mapfmt::ZoneType::Wall))
            {
                return L"Selected Wall: click a segment to choose its texture band; use texture and mapping controls on the right.";
            }
            if (IsLevelEndZone(zone))
            {
                return L"Selected Level End: move it with Set Level End, or Delete to remove the exit line.";
            }
            if (IsEventTriggerLineZone(zone))
            {
                return L"Selected Trigger: use Link-options to connect to walls/doors, switches, enemies/objects or rotating geometry.";
            }
            if (zone.ztype == static_cast<int16_t>(mapfmt::ZoneType::MonsterZone))
            {
                return L"Selected Monster Zone: this is a helper/logic line. Place real enemies with Place Enemy.";
            }
            return L"Selected Zone: edit exact values with Edit, or Delete to remove it.";
        }

        if (g_app.document.zones.empty() && CountMonsterSpawns() == 0 && CountEventCommandsOfType(mapfmt::CommandType::Teleport) == 0)
        {
            return L"Open a map or use Draw Wall to start. This area now shows short context help.";
        }
        return L"Select a wall, trigger, object or teleport target, or choose a drawing/link tool on the left.";
    }

    void DrawCanvasTextOverlayBox(HDC hdc, const RECT& rc, const std::wstring& text, bool bottomLeft)
    {
        if (text.empty() || rc.right - rc.left < 120 || rc.bottom - rc.top < 80)
        {
            return;
        }

        constexpr int margin = 18;
        constexpr int padX = 10;
        constexpr int padY = 8;

        const int left = rc.left + margin;
        const int maxRight = rc.right - margin;
        if (maxRight <= left + 80)
        {
            return;
        }

        SIZE oneLine{};
        GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &oneLine);

        // Prefer one line and only wrap when the canvas is too narrow.
        int wantedRight = MinValue(maxRight, left + padX + oneLine.cx + padX);
        if (wantedRight < left + 120)
        {
            wantedRight = maxRight;
        }

        RECT measure{ left + padX, 0, wantedRight - padX, rc.bottom };
        DrawTextW(hdc, text.c_str(), -1, &measure, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_CALCRECT);

        TEXTMETRICW tm{};
        GetTextMetricsW(hdc, &tm);
        const int lineHeight = MaxValue(16, tm.tmHeight + tm.tmExternalLeading);
        const int maxTextHeight = lineHeight * 2 + 2;
        const int textHeight = MinValue(maxTextHeight, MaxValue(lineHeight, measure.bottom - measure.top));
        const int boxHeight = textHeight + padY * 2;
        const int top = bottomLeft
            ? MaxValue(rc.top + margin, rc.bottom - margin - boxHeight)
            : rc.top + margin;
        RECT box{ left, top, wantedRight, MinValue(rc.bottom - margin, top + boxHeight) };

        HBRUSH bg = CreateSolidBrush(RGB(22, 22, 26));
        FillRect(hdc, &box, bg);
        DeleteObject(bg);

        HPEN border = CreatePen(PS_SOLID, 1, RGB(58, 58, 66));
        HGDIOBJ oldPen = SelectObject(hdc, border);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(hdc, box.left, box.top, box.right, box.bottom);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(border);

        RECT textRc{ box.left + padX, box.top + padY, box.right - padX, box.bottom - padY };
        SetTextColor(hdc, kDarkMutedText);
        DrawTextW(hdc, text.c_str(), -1, &textRc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
    }

    void DrawCanvasHelpOverlay(HDC hdc, const RECT& rc)
    {
        DrawCanvasTextOverlayBox(hdc, rc, GetCanvasHelpText(), false);
    }

    void DrawCanvasQuickHelpOverlay(HDC hdc, const RECT& rc)
    {
        DrawCanvasTextOverlayBox(hdc, rc,
            L"ESC ends current task | SPACE + Left Mouse drags the map | Right Mouse places 3D viewpoint on map",
            true);
    }

    void PaintCanvas(HWND hwnd)
    {
        PAINTSTRUCT ps{};
        HDC paintDc = BeginPaint(hwnd, &ps);

        RECT rc{};
        GetClientRect(hwnd, &rc);

        const int bufferWidth = MaxValue(1, rc.right - rc.left);
        const int bufferHeight = MaxValue(1, rc.bottom - rc.top);
        HDC bufferDc = CreateCompatibleDC(paintDc);
        HBITMAP bufferBitmap = bufferDc ? CreateCompatibleBitmap(paintDc, bufferWidth, bufferHeight) : nullptr;
        HGDIOBJ oldBufferBitmap = (bufferDc && bufferBitmap) ? SelectObject(bufferDc, bufferBitmap) : nullptr;
        HDC hdc = (bufferDc && bufferBitmap) ? bufferDc : paintDc;

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

        // Draw subtle 1/8-tile helper dots behind the main 256-unit grid.
        // v10 drew every dot with SetPixelV. That is expensive while panning/zooming,
        // especially when the view contains many 8x8 grid cells.  v11 keeps exact
        // 1/8 snap precision, but throttles only the *visual* dots when zoomed out
        // and uses a cheap solid brush fill instead of per-pixel SetPixelV calls.
        const double finePointSpacingPx = metrics.scale * static_cast<double>(kGridSnapStep);
        if (finePointSpacingPx >= 5.0)
        {
            const int fineStartX = FloorToFineGrid(metrics.viewBounds.minX);
            const int fineEndX = CeilToFineGrid(metrics.viewBounds.maxX);
            const int fineStartZ = FloorToFineGrid(metrics.viewBounds.minZ);
            const int fineEndZ = CeilToFineGrid(metrics.viewBounds.maxZ);

            const int columns = MaxValue(1, ((fineEndX - fineStartX) / kGridSnapStep) + 1);
            const int rows = MaxValue(1, ((fineEndZ - fineStartZ) / kGridSnapStep) + 1);
            const int maxFineDotsPerPaint = 9000;
            int visualStep = 1;
            while ((columns / visualStep) * (rows / visualStep) > maxFineDotsPerPaint)
            {
                visualStep *= 2;
            }

            const int worldStep = kGridSnapStep * visualStep;
            const int drawSize = finePointSpacingPx >= 10.0 ? 2 : 1;
            HBRUSH dotBrush = CreateSolidBrush(RGB(74, 74, 82));

            for (int fx = FloorToFineGrid(metrics.viewBounds.minX); fx <= fineEndX; fx += worldStep)
            {
                for (int fz = FloorToFineGrid(metrics.viewBounds.minZ); fz <= fineEndZ; fz += worldStep)
                {
                    const POINT p = WorldToScreen(rc, bounds, fx, fz);
                    if (p.x < rc.left || p.x >= rc.right || p.y < rc.top || p.y >= rc.bottom)
                    {
                        continue;
                    }

                    RECT dotRc{ p.x, p.y, p.x + drawSize, p.y + drawSize };
                    FillRect(hdc, &dotRc, dotBrush);
                }
            }

            DeleteObject(dotBrush);
        }

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
                const bool moveGuideWall = (zone.a == 0 && zone.b == 0);
                HPEN pen = CreatePen(moveGuideWall ? PS_DOT : PS_SOLID, selected ? 4 : 2,
                    selected ? RGB(255, 220, 64) : (moveGuideWall ? RGB(115, 135, 160) : RGB(96, 170, 255)));
                HPEN prev = static_cast<HPEN>(SelectObject(hdc, pen));
                MoveToEx(hdc, p1.x, p1.y, nullptr);
                LineTo(hdc, p2.x, p2.y);

                SelectObject(hdc, prev);
                DeleteObject(pen);

                if (selected)
                {
                    const int segmentCount = GetZonePreviewSlotCount(zone);
                    const int activeBand = GetActiveTextureBandForZone(zone);
                    for (int band = 0; band < segmentCount; ++band)
                    {
                        const double t0 = static_cast<double>(band) / static_cast<double>(segmentCount);
                        const double t1 = static_cast<double>(band + 1) / static_cast<double>(segmentCount);
                        POINT s0{
                            static_cast<LONG>(std::lround(static_cast<double>(p1.x) + (static_cast<double>(p2.x - p1.x) * t0))),
                            static_cast<LONG>(std::lround(static_cast<double>(p1.y) + (static_cast<double>(p2.y - p1.y) * t0)))
                        };
                        POINT s1{
                            static_cast<LONG>(std::lround(static_cast<double>(p1.x) + (static_cast<double>(p2.x - p1.x) * t1))),
                            static_cast<LONG>(std::lround(static_cast<double>(p1.y) + (static_cast<double>(p2.y - p1.y) * t1)))
                        };
                        HPEN segPen = CreatePen(PS_SOLID, band == activeBand ? 7 : 3,
                            band == activeBand ? RGB(255, 140, 32) : RGB(150, 130, 76));
                        HPEN oldSegPen = static_cast<HPEN>(SelectObject(hdc, segPen));
                        MoveToEx(hdc, s0.x, s0.y, nullptr);
                        LineTo(hdc, s1.x, s1.y);
                        SelectObject(hdc, oldSegPen);
                        DeleteObject(segPen);

                        if (band > 0)
                        {
                            HPEN tickPen = CreatePen(PS_SOLID, 1, RGB(255, 235, 150));
                            HPEN oldTickPen = static_cast<HPEN>(SelectObject(hdc, tickPen));
                            Ellipse(hdc, s0.x - 3, s0.y - 3, s0.x + 3, s0.y + 3);
                            SelectObject(hdc, oldTickPen);
                            DeleteObject(tickPen);
                        }
                    }
                }
            }
            else
            {
                const bool eventTrigger = IsEventTriggerLineZone(zone);
                const bool monsterZone = zone.ztype == static_cast<int>(mapfmt::ZoneType::MonsterZone) && !eventTrigger;
                const bool levelEnd = IsLevelEndZone(zone);
                COLORREF edgeColor = selected ? RGB(255, 220, 64)
                    : (levelEnd ? RGB(255, 176, 48) : (eventTrigger ? RGB(255, 98, 98) : (monsterZone ? RGB(76, 230, 116) : RGB(255, 98, 98))));
                HPEN pen = CreatePen(selected ? PS_SOLID : PS_DASH, selected ? 4 : 3, edgeColor);
                HPEN oldPen2 = static_cast<HPEN>(SelectObject(hdc, pen));
                MoveToEx(hdc, p1.x, p1.y, nullptr);
                LineTo(hdc, p2.x, p2.y);

                // Endpoint ticks make trigger/monster lines easy to distinguish from walls
                // without implying that they are filled rectangular areas.
                const int tick = selected ? 7 : 5;
                MoveToEx(hdc, p1.x - tick, p1.y - tick, nullptr);
                LineTo(hdc, p1.x + tick, p1.y + tick);
                MoveToEx(hdc, p1.x - tick, p1.y + tick, nullptr);
                LineTo(hdc, p1.x + tick, p1.y - tick);
                MoveToEx(hdc, p2.x - tick, p2.y - tick, nullptr);
                LineTo(hdc, p2.x + tick, p2.y + tick);
                MoveToEx(hdc, p2.x - tick, p2.y + tick, nullptr);
                LineTo(hdc, p2.x + tick, p2.y - tick);

                SelectObject(hdc, oldPen2);
                DeleteObject(pen);
            }

            std::wstringstream tag;
            bool skipZoneLabel = false;
            bool highlightZoneLabel = selected;

            if (zone.ztype == static_cast<int>(mapfmt::ZoneType::Wall))
            {
                if (zone.a == 0 && zone.b == 0 && !selected)
                {
                    skipZoneLabel = true;
                }
                const int reversePair = FindReverseWallPairIndex(i);
                if (reversePair >= 0)
                {
                    if (reversePair < i)
                    {
                        // Reverse/front wall pairs share the same screen position.
                        // Draw both Z numbers once, stacked, instead of overprinting
                        // unreadable labels on top of each other.
                        skipZoneLabel = true;
                    }
                    else
                    {
                        const auto& pairedZone = g_app.document.zones[reversePair];
                        highlightZoneLabel = selected || (reversePair == g_app.selectedZone);
                        tag << L"Z" << i;
                        if (zone.ev) tag << L" E" << zone.ev;
                        tag << L"\nZ" << reversePair;
                        if (pairedZone.ev) tag << L" E" << pairedZone.ev;
                    }
                }
            }

            if (!skipZoneLabel && tag.str().empty())
            {
                if (IsLevelEndZone(zone))
                {
                    tag << L"Level End Z" << i << L" E" << zone.ev;
                }
                else if (IsEventTriggerLineZone(zone))
                {
                    tag << L"Trigger E" << zone.ev << L" Z" << i;
                }
                else if (zone.ztype == static_cast<int>(mapfmt::ZoneType::MonsterZone))
                {
                    tag << L"Monster Zone Z" << i;
                    if (zone.ev) tag << L" E" << zone.ev;
                }
                else if (zone.ztype == static_cast<int>(mapfmt::ZoneType::EventTrigger))
                {
                    tag << L"Trigger E" << zone.ev << L" Z" << i;
                }
                else
                {
                    tag << L"Z" << i << L" E" << zone.ev;
                }
            }

            if (!skipZoneLabel)
            {
                const std::wstring tagText = tag.str();
                if (!tagText.empty())
                {
                    DrawZoneOverlayLabel(hdc, tagText, MinValue(p1.x, p2.x) + 4, MinValue(p1.y, p2.y) + 4,
                        highlightZoneLabel ? RGB(255, 235, 150) : RGB(220, 220, 220));
                }
            }
        }

        DrawAllMoveWallGroupEventPaths(hdc, rc, bounds);
        DrawEventGraphOverlay(hdc, rc, bounds);
        DrawEventLogicOverlay(hdc, rc, bounds);
        DrawTeleportTargetOverlays(hdc, rc, bounds);
        DrawEventMonsterSpawnOverlays(hdc, rc, bounds);
        DrawTeleportDirectionSelectionOverlay(hdc, rc, bounds);
        DrawWalkPreviewCameraOverlay(hdc, rc, bounds);

        if (g_app.isDrawing && g_app.insertMode != InsertMode::None)
        {
            const auto previewZone = BuildZoneFromDrawPoints(g_app.insertMode, g_app.drawStartWorld, g_app.drawCurrentWorld);
            POINT p1 = WorldToScreen(rc, bounds, previewZone.x1, previewZone.z1);
            POINT p2 = WorldToScreen(rc, bounds, previewZone.x2, previewZone.z2);
            HPEN pen = CreatePen(PS_DOT, 1, RGB(255, 255, 255));
            HGDIOBJ oldPreviewPen = SelectObject(hdc, pen);
            HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
            if (IsLinearZoneType(previewZone.ztype))
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

        DrawMoveWallGroupPlacementPreview(hdc, rc, bounds);

        DrawCanvasHelpOverlay(hdc, rc);
        DrawCanvasQuickHelpOverlay(hdc, rc);

        SelectObject(hdc, oldFont);
        DeleteObject(font);

        if (bufferDc && bufferBitmap)
        {
            BitBlt(paintDc, rc.left, rc.top, bufferWidth, bufferHeight, bufferDc, 0, 0, SRCCOPY);
        }
        if (oldBufferBitmap) SelectObject(bufferDc, oldBufferBitmap);
        if (bufferBitmap) DeleteObject(bufferBitmap);
        if (bufferDc) DeleteDC(bufferDc);
        EndPaint(hwnd, &ps);
    }

    void DrawSelectedMonsterObjectPreview(HDC hdc, const RECT& rc, const mapfmt::EventCommand& command)
    {
        HBRUSH bg = CreateSolidBrush(RGB(38, 38, 46));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        HPEN border = CreatePen(PS_SOLID, 1, RGB(92, 92, 104));
        HGDIOBJ oldPen = SelectObject(hdc, border);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(border);

        const int monsterType = command.params[0];
        if (g_app.objectPreviewImage.objectType != monsterType)
        {
            LoadObjectPreviewImage(monsterType, g_app.objectPreviewImage);
        }

        const int rot = command.params[4] & 255;
        const int centerX = (rc.left + rc.right) / 2;
        const int centerY = rc.top + MaxValue(34, (rc.bottom - rc.top) / 2 - 6);

        if (!g_app.objectPreviewImage.pixels.empty() && g_app.objectPreviewImage.width > 0 && g_app.objectPreviewImage.height > 0)
        {
            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = g_app.objectPreviewImage.width;
            bmi.bmiHeader.biHeight = -g_app.objectPreviewImage.height;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            const int labelReserve = 30;
            const int availW = MaxValue(1, rc.right - rc.left - 16);
            const int availH = MaxValue(1, rc.bottom - rc.top - labelReserve - 10);
            const double scale = MinValue(static_cast<double>(availW) / static_cast<double>(g_app.objectPreviewImage.width),
                static_cast<double>(availH) / static_cast<double>(g_app.objectPreviewImage.height));
            const int drawW = MaxValue(1, static_cast<int>(std::lround(static_cast<double>(g_app.objectPreviewImage.width) * scale)));
            const int drawH = MaxValue(1, static_cast<int>(std::lround(static_cast<double>(g_app.objectPreviewImage.height) * scale)));
            const int drawX = rc.left + (rc.right - rc.left - drawW) / 2;
            const int drawY = rc.top + 6 + MaxValue(0, (availH - drawH) / 2);

            StretchDIBits(hdc, drawX, drawY, drawW, drawH, 0, 0,
                g_app.objectPreviewImage.width, g_app.objectPreviewImage.height,
                g_app.objectPreviewImage.pixels.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
        }
        else
        {
            const COLORREF objectColor = MonsterTypeColor(monsterType);
            const int bodyW = MaxValue(18, MinValue(46, (rc.right - rc.left) / 5));
            const int bodyH = MaxValue(28, MinValue(64, (rc.bottom - rc.top) / 2));

            HBRUSH body = CreateSolidBrush(objectColor);
            HPEN outline = CreatePen(PS_SOLID, 2, RGB(255, 220, 130));
            HGDIOBJ oldBody = SelectObject(hdc, body);
            HGDIOBJ oldOutline = SelectObject(hdc, outline);

            if (monsterType == 0 || monsterType == 1)
            {
                RoundRect(hdc, centerX - bodyW, centerY - bodyH / 2, centerX + bodyW, centerY + bodyH / 2, 18, 18);
            }
            else if ((monsterType >= 2 && monsterType <= 7) || monsterType == 9 || (monsterType >= 16 && monsterType <= 20))
            {
                POINT diamond[4] = {
                    { centerX, centerY - bodyH / 2 },
                    { centerX + bodyW, centerY },
                    { centerX, centerY + bodyH / 2 },
                    { centerX - bodyW, centerY }
                };
                Polygon(hdc, diamond, 4);
            }
            else
            {
                POINT monsterShape[5] = {
                    { centerX, centerY - bodyH / 2 },
                    { centerX + bodyW, centerY - bodyH / 6 },
                    { centerX + bodyW / 2, centerY + bodyH / 2 },
                    { centerX - bodyW / 2, centerY + bodyH / 2 },
                    { centerX - bodyW, centerY - bodyH / 6 }
                };
                Polygon(hdc, monsterShape, 5);
            }

            SelectObject(hdc, oldOutline);
            SelectObject(hdc, oldBody);
            DeleteObject(outline);
            DeleteObject(body);
        }

        const double angle = (static_cast<double>(rot) / 256.0) * 6.28318530717958647692;
        HPEN arrowPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
        HGDIOBJ oldArrowPen = SelectObject(hdc, arrowPen);
        MoveToEx(hdc, centerX, centerY, nullptr);
        LineTo(hdc,
            centerX + static_cast<int>(std::lround(std::sin(angle) * 38.0)),
            centerY - static_cast<int>(std::lround(std::cos(angle) * 38.0)));
        SelectObject(hdc, oldArrowPen);
        DeleteObject(arrowPen);

        RECT textRc{ rc.left + 8, rc.bottom - 34, rc.right - 8, rc.bottom - 8 };
        std::wstring label = MonsterTypeName(monsterType);
        SetTextColor(hdc, RGB(225, 225, 230));
        DrawTextW(hdc, label.c_str(), -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }


    constexpr double kWalkPreviewPi = 3.14159265358979323846;
    constexpr double kWalkPreviewFov = kWalkPreviewPi * 0.42; // ~75.6 degrees; wider than the old 60 degree tunnel view.
    constexpr double kWalkPreviewWallHeightWorld = 256.0; // one grid/wall element should project as a square.

    bool ShouldShowWalkPreview(const RECT& panelRc)
    {
        const int leftPad = 12;
        const int rightPad = 12;
        const int controlHeight = 58;
        const int top = GetPreviewScrollBarRect(panelRc).bottom + 14;
        const int availableWidth = panelRc.right - leftPad - rightPad;
        const int availableHeight = panelRc.bottom - top - controlHeight - 14;
        if (availableWidth < 120 || availableHeight < 120)
        {
            return false;
        }

        const int size = MinValue(availableWidth, availableHeight);
        // When the square 3D preview would shrink to two thirds or less of
        // the inspector/overview width, hide it instead of leaving a tiny box
        // floating below the texture overview.
        if (size * 3 <= availableWidth * 2)
        {
            return false;
        }
        return true;
    }

    RECT GetWalkPreviewRect(const RECT& panelRc)
    {
        const int leftPad = 12;
        const int rightPad = 12;
        const int controlHeight = 58;
        const int top = GetPreviewScrollBarRect(panelRc).bottom + 14;
        if (!ShouldShowWalkPreview(panelRc))
        {
            return RECT{ leftPad, top, leftPad, top };
        }

        const int availableWidth = MaxValue(96, panelRc.right - leftPad - rightPad);
        const int availableHeight = MaxValue(96, panelRc.bottom - top - controlHeight - 14);
        const int size = MaxValue(96, MinValue(availableWidth, availableHeight));
        return RECT{ leftPad, top, leftPad + size, top + size };
    }

    RECT GetWalkPreviewControlsRect(const RECT& panelRc)
    {
        const RECT view = GetWalkPreviewRect(panelRc);
        if (view.right <= view.left || view.bottom <= view.top)
        {
            return RECT{ view.left, view.bottom, view.left, view.bottom };
        }
        return RECT{ view.left, view.bottom + 8, view.right, view.bottom + 58 };
    }

    int NormalizeWalkPreviewDir(int dir)
    {
        return ((dir % kWalkPreviewDirectionCount) + kWalkPreviewDirectionCount) % kWalkPreviewDirectionCount;
    }

    int WalkPreviewDirFromScreenDrag(int dx, int dy)
    {
        if (dx == 0 && dy == 0)
        {
            return NormalizeWalkPreviewDir(g_app.walkPreviewDir);
        }

        // Screen y grows downward.  atan2(dx, -dy) gives a clockwise angle from
        // screen-up: up=0, up/right=1, right=2, ... left=6.
        double angle = std::atan2(static_cast<double>(dx), static_cast<double>(-dy));
        if (angle < 0.0)
        {
            angle += kWalkPreviewPi * 2.0;
        }
        return NormalizeWalkPreviewDir(static_cast<int>(std::lround(angle / (kWalkPreviewPi * 0.25))));
    }

    void WalkPreviewDirectionVectors(int dir, double& fx, double& fz, double& rx, double& rz)
    {
        const double heading = static_cast<double>(NormalizeWalkPreviewDir(dir)) * (kWalkPreviewPi * 0.25);
        fx = std::sin(heading);
        fz = std::cos(heading);
        rx = std::cos(heading);
        rz = -std::sin(heading);
    }

    double WalkPreviewGridCenter(double value)
    {
        // Movement in the mini preview is grid-cell based.  Gloom's walls sit on
        // 256-unit grid lines, so the camera should stand on the cell center
        // line at +128, otherwise one 256-unit step feels offset inside a cell.
        return std::floor((value - (static_cast<double>(kGridStep) * 0.5)) / static_cast<double>(kGridStep) + 0.5)
            * static_cast<double>(kGridStep) + (static_cast<double>(kGridStep) * 0.5);
    }

    void CenterWalkPreviewCameraOnGridCell()
    {
        g_app.walkPreviewX = WalkPreviewGridCenter(g_app.walkPreviewX);
        g_app.walkPreviewZ = WalkPreviewGridCenter(g_app.walkPreviewZ);
    }

    void EnsureWalkPreviewCamera()
    {
        if (g_app.walkPreviewInitialized) return;

        const mapfmt::EventCommand* playerStart = FindPlayerStartCommand(kPlayer1ObjectType);
        if (playerStart)
        {
            g_app.walkPreviewX = static_cast<double>(playerStart->params[1]);
            g_app.walkPreviewZ = static_cast<double>(playerStart->params[3]);
            const int rot = playerStart->params[4] & 255;
            g_app.walkPreviewDir = ClampValue(static_cast<int>(std::lround(static_cast<double>(rot) / static_cast<double>(kWalkPreviewRotationUnitsPerStep))), 0, kWalkPreviewDirectionCount - 1);
        }
        else
        {
            const mapfmt::Bounds bounds = g_app.document.ComputeBounds();
            g_app.walkPreviewX = (static_cast<double>(bounds.minX) + static_cast<double>(bounds.maxX)) * 0.5;
            g_app.walkPreviewZ = (static_cast<double>(bounds.minZ) + static_cast<double>(bounds.maxZ)) * 0.5;
            g_app.walkPreviewDir = 0;
        }
        CenterWalkPreviewCameraOnGridCell();
        g_app.walkPreviewInitialized = true;
    }

    double Cross2(double ax, double az, double bx, double bz)
    {
        return ax * bz - az * bx;
    }

    bool RaySegmentIntersection(double ox, double oz, double dx, double dz,
        const mapfmt::Zone& zone, double& outT)
    {
        const double ax = static_cast<double>(zone.x1);
        const double az = static_cast<double>(zone.z1);
        const double sx = static_cast<double>(zone.x2 - zone.x1);
        const double sz = static_cast<double>(zone.z2 - zone.z1);
        const double denom = Cross2(dx, dz, sx, sz);
        if (std::abs(denom) < 0.00001) return false;
        const double qx = ax - ox;
        const double qz = az - oz;
        const double t = Cross2(qx, qz, sx, sz) / denom;
        const double u = Cross2(qx, qz, dx, dz) / denom;
        if (t <= 1.0 || u < 0.0 || u > 1.0) return false;
        outT = t;
        return true;
    }

    double DistancePointToSegment(double px, double pz, const mapfmt::Zone& zone)
    {
        const double ax = static_cast<double>(zone.x1);
        const double az = static_cast<double>(zone.z1);
        const double bx = static_cast<double>(zone.x2);
        const double bz = static_cast<double>(zone.z2);
        const double vx = bx - ax;
        const double vz = bz - az;
        const double lenSq = vx * vx + vz * vz;
        if (lenSq < 0.0001)
        {
            const double dx = px - ax;
            const double dz = pz - az;
            return std::sqrt(dx * dx + dz * dz);
        }
        const double t = ClampValue(((px - ax) * vx + (pz - az) * vz) / lenSq, 0.0, 1.0);
        const double cx = ax + vx * t;
        const double cz = az + vz * t;
        const double dx = px - cx;
        const double dz = pz - cz;
        return std::sqrt(dx * dx + dz * dz);
    }

    bool IsWalkPreviewRenderableWallIndex(int zoneIndex)
    {
        if (zoneIndex < 0 || zoneIndex >= static_cast<int>(g_app.document.zones.size()))
        {
            return false;
        }

        const auto& zone = g_app.document.zones[zoneIndex];
        if (!IsWallZone(zone))
        {
            return false;
        }

        // Editor-created two-sided walls contain a reverse visual backface.
        // Rendering/colliding both halves makes the 3D preview look like there
        // is a second wall beside the real wall.  Keep the canonical/front wall
        // only; the actual game data still keeps both sides where needed.
        return !IsVisualBackfaceWallIndex(zoneIndex);
    }

    bool LooksLikeWalkPreviewMoveWallGuideZone(const mapfmt::Zone& zone)
    {
        // Move-Wallblock target guides are real map zones for the event engine,
        // but they are intentionally invisible/neutral in-game.  Show them as
        // editor-only ghost geometry in the 3D preview and never use them as
        // movement blockers.
        return zone.a == 0 && zone.b == 0 && zone.na == 0 && zone.nb == 0 &&
               zone.ln == 0 && zone.sc == 0 && zone.ev == 0 &&
               std::all_of(zone.textures.begin(), zone.textures.end(), [](uint8_t t) { return t == 0; });
    }

    bool IsWalkPreviewBlockingWallIndex(int zoneIndex)
    {
        if (!IsWalkPreviewRenderableWallIndex(zoneIndex))
        {
            return false;
        }

        const auto& zone = g_app.document.zones[static_cast<size_t>(zoneIndex)];
        if (LooksLikeWalkPreviewMoveWallGuideZone(zone))
        {
            return false;
        }

        // OpenDoor-driven walls/doors are drawn in the preview but do not block
        // movement, so the mini walk view behaves like an already-open test pass.
        return !IsZoneControlledByOpenDoor(zoneIndex);
    }

    bool IsWalkPreviewPointBlocked(double x, double z, double radius = 38.0)
    {
        for (int zoneIndex = 0; zoneIndex < static_cast<int>(g_app.document.zones.size()); ++zoneIndex)
        {
            if (!IsWalkPreviewBlockingWallIndex(zoneIndex)) continue;
            if (DistancePointToSegment(x, z, g_app.document.zones[zoneIndex]) <= radius)
            {
                return true;
            }
        }
        return false;
    }

    const TexturePreviewImage* GetWalkPreviewWallTexture(const std::string& textureName)
    {
        static std::unordered_map<std::string, TexturePreviewImage> cache;
        const std::string cacheKey = TrimTrailingSlashes(g_app.textureDataPath) + "|" + textureName;
        auto it = cache.find(cacheKey);
        if (it != cache.end())
        {
            return it->second.pixels.empty() ? nullptr : &it->second;
        }
        TexturePreviewImage image;
        LoadTexturePreviewImage(textureName, image);
        auto inserted = cache.emplace(cacheKey, std::move(image));
        return inserted.first->second.pixels.empty() ? nullptr : &inserted.first->second;
    }

    bool TryGetWalkPreviewSwitchOnTextureForZone(int zoneIndex, int& outTextureIndex)
    {
        outTextureIndex = -1;
        if (zoneIndex < 0 || zoneIndex >= static_cast<int>(g_app.document.zones.size())) return false;

        std::vector<int> eventIndexes;
        auto addEvent = [&](int eventIndex)
        {
            if (eventIndex < 0 || eventIndex >= static_cast<int>(g_app.document.events.size())) return;
            if (std::find(eventIndexes.begin(), eventIndexes.end(), eventIndex) == eventIndexes.end())
            {
                eventIndexes.push_back(eventIndex);
            }
        };

        auto addTriggerEvent = [&](int triggerZoneIndex)
        {
            if (!IsSwitchTextureSourceZoneIndex(triggerZoneIndex)) return;
            addEvent(EventSlotFromZoneEventValue(g_app.document.zones[triggerZoneIndex].ev));
        };

        // Selecting/using a trigger previews the activated switch texture in the
        // mini 3D view.  Selecting a switch target wall previews its controlling
        // ChangeTexture result as well, which makes the OFF->ON state visible
        // right after Link Event > Switch/Trigger.
        addTriggerEvent(g_app.selectedZone);
        addTriggerEvent(g_app.linkEventTriggerZone);
        if (g_app.insertMode == InsertMode::LinkEventToSwitchTexture)
        {
            addEvent(g_app.linkEventIndex);
        }

        if (g_app.selectedZone == zoneIndex)
        {
            for (int eventIndex = 0; eventIndex < static_cast<int>(g_app.document.events.size()); ++eventIndex)
            {
                const auto& commands = g_app.document.events[eventIndex].commands;
                for (const auto& command : commands)
                {
                    if (command.type != mapfmt::CommandType::ChangeTexture) continue;
                    const int rawTarget = static_cast<int>(command.params[0]);
                    const int displayTarget = DisplayZoneIndexForEventTarget(rawTarget);
                    if (rawTarget == zoneIndex || displayTarget == zoneIndex)
                    {
                        addEvent(eventIndex);
                        break;
                    }
                }
            }
        }

        for (int eventIndex : eventIndexes)
        {
            const auto& commands = g_app.document.events[eventIndex].commands;
            for (const auto& command : commands)
            {
                if (command.type != mapfmt::CommandType::ChangeTexture) continue;
                const int rawTarget = static_cast<int>(command.params[0]);
                const int displayTarget = DisplayZoneIndexForEventTarget(rawTarget);
                if (rawTarget == zoneIndex || displayTarget == zoneIndex)
                {
                    int textureIndex = ClampValue(static_cast<int>(command.params[1]), 0, 159);
                    if (textureIndex < 0)
                    {
                        textureIndex = GetSwitchAutoOnTextureIndexForTargetZone(zoneIndex);
                    }
                    outTextureIndex = ClampValue(textureIndex, 0, 159);
                    return true;
                }
            }
        }
        return false;
    }

    COLORREF SampleWalkPreviewWallColor(int zoneIndex, const mapfmt::Zone& zone, double hitU, double verticalV, bool hitDoor, double distance)
    {
        const int activeBands = ClampValue(GetZonePreviewSlotCount(zone), 1, 8);
        const WallTextureMode mode = GetWallTextureModeForZone(zone);
        double textureU = 0.0;
        int band = 0;

        if (mode == WallTextureMode::Repeat1To1 || mode == WallTextureMode::Clip2Of8)
        {
            const double u = ClampValue(hitU, 0.0, 0.9999);
            if (zone.sc < 0)
            {
                // Match the original game path for negative sc: one texture strip is
                // selected, but only the first 1/(-sc*2) part of it is sampled.
                const int divisor = MaxValue(1, std::abs(static_cast<int>(zone.sc)) * 2);
                textureU = u / static_cast<double>(divisor);
                band = 0;
            }
            else
            {
                const int repeatCount = GetPositiveWallTextureRepeatCount(zone);
                textureU = u * static_cast<double>(repeatCount);
                band = ClampValue(static_cast<int>(std::floor(textureU)), 0, activeBands - 1);
            }
        }
        else
        {
            // Stretch mode maps one active 64px strip over the full wall length.
            textureU = ClampValue(hitU, 0.0, 0.9999) * static_cast<double>(activeBands);
            band = ClampValue(static_cast<int>(std::floor(textureU)), 0, activeBands - 1);
        }

        int textureIndex = ClampValue(static_cast<int>(zone.textures[band]), 0, 159);
        int switchOnTexture = -1;
        if (TryGetWalkPreviewSwitchOnTextureForZone(zoneIndex, switchOnTexture))
        {
            textureIndex = ClampValue(switchOnTexture, 0, 159);
            textureU = ClampValue(hitU, 0.0, 0.9999);
        }
        const int slot = ClampValue(textureIndex / 20, 0, static_cast<int>(mapfmt::MapDocument::kTextureSlotCount) - 1);
        const int strip = ClampValue(textureIndex % 20, 0, 19);
        const std::string& textureName = g_app.document.textureNames[slot];
        const TexturePreviewImage* image = GetWalkPreviewWallTexture(textureName);
        if (!image || image->width <= 0 || image->height <= 0)
        {
            const int shade = ClampValue(210 - static_cast<int>(distance / 18.0), 55, 210);
            return hitDoor ? RGB(ClampValue(shade + 30, 0, 255), ClampValue(shade - 20, 0, 255), 70) : RGB(shade, shade, shade + 8);
        }

        const double localU = ClampValue(textureU - std::floor(textureU), 0.0, 0.9999);
        const int sx = ClampValue(strip * 64 + static_cast<int>(std::floor(localU * 64.0)), 0, image->width - 1);
        const int sy = ClampValue(static_cast<int>(std::floor(ClampValue(verticalV, 0.0, 0.9999) * static_cast<double>(image->height))), 0, image->height - 1);
        uint32_t px = image->pixels[static_cast<size_t>(sy) * static_cast<size_t>(image->width) + static_cast<size_t>(sx)];
        (void)hitDoor;
        px = ApplyGloomRuntimeDim(px, distance);
        return RGB(static_cast<int>((px >> 16) & 0xFFu),
            static_cast<int>((px >> 8) & 0xFFu),
            static_cast<int>(px & 0xFFu));
    }

    bool SegmentIntersectsWall(double x1, double z1, double x2, double z2)
    {
        mapfmt::Zone test{};
        test.x1 = static_cast<int32_t>(std::lround(x1));
        test.z1 = static_cast<int32_t>(std::lround(z1));
        test.x2 = static_cast<int32_t>(std::lround(x2));
        test.z2 = static_cast<int32_t>(std::lround(z2));
        const double dx = x2 - x1;
        const double dz = z2 - z1;
        const double len = std::sqrt(dx * dx + dz * dz);
        if (len < 0.001) return false;
        const double ndx = dx / len;
        const double ndz = dz / len;
        for (int zoneIndex = 0; zoneIndex < static_cast<int>(g_app.document.zones.size()); ++zoneIndex)
        {
            if (!IsWalkPreviewBlockingWallIndex(zoneIndex)) continue;
            const auto& zone = g_app.document.zones[zoneIndex];
            double t = 0.0;
            if (RaySegmentIntersection(x1, z1, ndx, ndz, zone, t) && t < len)
            {
                return true;
            }
        }
        return false;
    }

    void InvalidateWalkPreview()
    {
        if (g_app.infoPanel)
        {
            RECT panelRc{};
            GetClientRect(g_app.infoPanel, &panelRc);
            const RECT viewRc = GetWalkPreviewRect(panelRc);
            const RECT controlsRc = GetWalkPreviewControlsRect(panelRc);
            RECT dirty{};
            UnionRect(&dirty, &viewRc, &controlsRc);
            InflateRect(&dirty, 4, 4);
            InvalidateRect(g_app.infoPanel, &dirty, FALSE);
        }
        if (g_app.canvas)
        {
            InvalidateRect(g_app.canvas, nullptr, FALSE);
        }
    }
    void InvalidateTexturePreviewOnly(HWND hwnd)
    {
        if (!hwnd) return;
        if (IsSelectedEventTriggerOrLevelEndZone()) return;
        RECT panelRc{};
        GetClientRect(hwnd, &panelRc);
        const PreviewMetrics preview = GetPreviewMetrics(panelRc);
        const RECT scrollRc = GetPreviewScrollBarRect(panelRc);
        const RECT slotRc = GetTextureSlotBarRect(panelRc);
        RECT dirty{};
        UnionRect(&dirty, &preview.outer, &scrollRc);
        UnionRect(&dirty, &dirty, &slotRc);
        if (IsSurfaceTexturePickerActive())
        {
            RECT surfaceRc{ slotRc.left, slotRc.top - 64, slotRc.right, slotRc.bottom };
            UnionRect(&dirty, &dirty, &surfaceRc);
        }
        InflateRect(&dirty, 4, 4);
        InvalidateRect(hwnd, &dirty, FALSE);
    }


    void MoveWalkPreview(double forwardAmount, double strafeAmount)
    {
        EnsureWalkPreviewCamera();
        double fx = 0.0, fz = 1.0, rx = 1.0, rz = 0.0;
        WalkPreviewDirectionVectors(g_app.walkPreviewDir, fx, fz, rx, rz);
        const double nx = WalkPreviewGridCenter(g_app.walkPreviewX + fx * forwardAmount + rx * strafeAmount);
        const double nz = WalkPreviewGridCenter(g_app.walkPreviewZ + fz * forwardAmount + rz * strafeAmount);
        g_app.walkPreviewX = nx;
        g_app.walkPreviewZ = nz;
        InvalidateWalkPreview();
    }

    COLORREF WalkPreviewObjectColor(int type)
    {
        if (type == kPlayer1ObjectType || type == kPlayer2ObjectType) return RGB(115, 220, 135);
        if (IsWeaponObjectType(type)) return RGB(100, 185, 255);
        const std::wstring category = MonsterTypeCategory(type);
        if (category == L"Pickup / item") return RGB(110, 220, 210);
        return RGB(230, 100, 130);
    }

    void DrawWalkPreviewControls(HDC hdc, const RECT& panelRc)
    {
        if (!ShouldShowWalkPreview(panelRc)) return;
        const RECT controls = GetWalkPreviewControlsRect(panelRc);
        const int gap = 5;
        const int rowH = 22;
        const int colW = MaxValue(42, (controls.right - controls.left - gap * 2) / 3);
        auto buttonRect = [&](int col, int row) -> RECT
        {
            return RECT{ controls.left + col * (colW + gap), controls.top + row * (rowH + 5),
                controls.left + col * (colW + gap) + colW, controls.top + row * (rowH + 5) + rowH };
        };
        auto drawButton = [&](RECT rc, const wchar_t* label)
        {
            HBRUSH brush = CreateSolidBrush(RGB(45, 45, 52));
            FillRect(hdc, &rc, brush);
            DeleteObject(brush);
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(92, 92, 104));
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
            Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
            SetTextColor(hdc, RGB(218, 218, 224));
            DrawTextW(hdc, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        };
        drawButton(buttonRect(0, 0), L"Turn -45");
        drawButton(buttonRect(1, 0), L"Forward");
        drawButton(buttonRect(2, 0), L"Turn +45");
        drawButton(buttonRect(0, 1), L"Strafe L");
        drawButton(buttonRect(1, 1), L"Back");
        drawButton(buttonRect(2, 1), L"Strafe R");
    }

    uint32_t WalkPreviewDibPixel(COLORREF color)
    {
        return (static_cast<uint32_t>(GetRValue(color)) << 16) |
               (static_cast<uint32_t>(GetGValue(color)) << 8) |
               static_cast<uint32_t>(GetBValue(color));
    }

    uint32_t BlendWalkPreviewDibPixel(uint32_t basePixel, COLORREF overlay, double alpha)
    {
        alpha = ClampValue(alpha, 0.0, 1.0);
        const double inv = 1.0 - alpha;
        const int br = static_cast<int>((basePixel >> 16) & 0xFFu);
        const int bg = static_cast<int>((basePixel >> 8) & 0xFFu);
        const int bb = static_cast<int>(basePixel & 0xFFu);
        const int r = ClampValue(static_cast<int>(std::lround(static_cast<double>(br) * inv + static_cast<double>(GetRValue(overlay)) * alpha)), 0, 255);
        const int g = ClampValue(static_cast<int>(std::lround(static_cast<double>(bg) * inv + static_cast<double>(GetGValue(overlay)) * alpha)), 0, 255);
        const int b = ClampValue(static_cast<int>(std::lround(static_cast<double>(bb) * inv + static_cast<double>(GetBValue(overlay)) * alpha)), 0, 255);
        return WalkPreviewDibPixel(RGB(r, g, b));
    }

    COLORREF SampleWalkPreviewMoveGuideColor(double verticalV, double distance)
    {
        const double centerShade = 1.0 - std::abs(ClampValue(verticalV, 0.0, 1.0) - 0.5) * 0.55;
        const int base = ClampValue(180 - static_cast<int>(distance / 24.0), 82, 188);
        const int shade = ClampValue(static_cast<int>(std::lround(static_cast<double>(base) * centerShade)), 68, 196);
        return RGB(shade, shade, ClampValue(shade + 10, 0, 255));
    }

    bool IsWalkPreviewTransparentSpritePixel(uint32_t pixel)
    {
        // Object preview sprites decode transparent palette index 0 to the
        // editor-preview background color.  Also accept alpha 0 for future/
        // generated preview images.
        return ((pixel >> 24) == 0u) || ((pixel & 0x00FFFFFFu) == 0x0026262Eu);
    }

    uint32_t ShadeWalkPreviewSpritePixel(uint32_t pixel, double depth)
    {
        return ApplyGloomRuntimeDim(pixel, depth);
    }

    bool TryBuildActiveDrawWallPreviewZone(mapfmt::Zone& outZone)
    {
        if (!g_app.isDrawing || g_app.insertMode != InsertMode::Wall)
        {
            return false;
        }
        if (IsTinyDrawSegment(g_app.drawStartWorld, g_app.drawCurrentWorld))
        {
            return false;
        }

        outZone = BuildZoneFromDrawPoints(InsertMode::Wall, g_app.drawStartWorld, g_app.drawCurrentWorld);
        return IsWallZone(outZone);
    }

    void DrawWalkPreview3D(HDC hdc, const RECT& viewRc)
    {
        EnsureWalkPreviewCamera();
        if (viewRc.right <= viewRc.left || viewRc.bottom <= viewRc.top) return;

        const int w = MaxValue(1, viewRc.right - viewRc.left);
        const int h = MaxValue(1, viewRc.bottom - viewRc.top);
        const uint32_t blackPixel = WalkPreviewDibPixel(RGB(0, 0, 0));
        std::vector<uint32_t> pixels(static_cast<size_t>(w) * static_cast<size_t>(h), blackPixel);

        const double heading = static_cast<double>(NormalizeWalkPreviewDir(g_app.walkPreviewDir)) * (kWalkPreviewPi * 0.25);
        const double fov = kWalkPreviewFov;
        const double halfFovTan = std::tan(fov * 0.5);
        const double projectionScale = static_cast<double>(w) / (2.0 * halfFovTan);
        const double forwardX = std::sin(heading);
        const double forwardZ = std::cos(heading);
        const double rightX = std::cos(heading);
        const double rightZ = -std::sin(heading);
        const int columns = w;
        const double maxDistance = 4096.0;
        std::vector<double> depth(columns, maxDistance);
        const std::vector<uint8_t> moveWallGuideMask = BuildMoveWallGroupGuideMaskForDocument(g_app.document);
        mapfmt::Zone activeDrawWall{};
        const bool hasActiveDrawWall = TryBuildActiveDrawWallPreviewZone(activeDrawWall);

        auto setPixel = [&](int x, int y, COLORREF color)
        {
            if (x < 0 || x >= w || y < 0 || y >= h) return;
            pixels[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = WalkPreviewDibPixel(color);
        };

        for (int c = 0; c < columns; ++c)
        {
            const double u = (static_cast<double>(c) + 0.5) / static_cast<double>(columns);
            const double cameraX = (u * 2.0) - 1.0;
            // Use a camera-plane ray instead of evenly-spaced angles.  This is
            // the standard raycaster projection and keeps nearby wall edges
            // visually straight instead of bending toward the screen edges.
            const double dx = forwardX + rightX * cameraX * halfFovTan;
            const double dz = forwardZ + rightZ * cameraX * halfFovTan;
            struct WallHitCandidate
            {
                double t = 4096.0;
                double u = 0.0;
                int zoneIndex = -1;
                bool hitDoor = false;
                bool activeDrawWall = false;
            };

            WallHitCandidate opaqueHit;
            WallHitCandidate guideHit;

            auto considerWallHit = [&](const mapfmt::Zone& zone, int zoneIndex, bool activeDrawWall)
            {
                double t = 0.0;
                if (!RaySegmentIntersection(g_app.walkPreviewX, g_app.walkPreviewZ, dx, dz, zone, t) || t >= maxDistance)
                {
                    return;
                }

                const bool isMoveGuide = !activeDrawWall && zoneIndex >= 0 &&
                    (IsMoveWallGroupGuideZoneIndex(moveWallGuideMask, zoneIndex) || LooksLikeWalkPreviewMoveWallGuideZone(zone));

                WallHitCandidate& target = isMoveGuide ? guideHit : opaqueHit;
                if (t >= target.t)
                {
                    return;
                }

                const double hx = g_app.walkPreviewX + dx * t;
                const double hz = g_app.walkPreviewZ + dz * t;
                const double wx = static_cast<double>(zone.x2 - zone.x1);
                const double wz = static_cast<double>(zone.z2 - zone.z1);
                const double lenSq = MaxValue(1.0, wx * wx + wz * wz);

                target.t = t;
                target.u = ClampValue(((hx - static_cast<double>(zone.x1)) * wx + (hz - static_cast<double>(zone.z1)) * wz) / lenSq, 0.0, 1.0);
                target.zoneIndex = zoneIndex;
                target.hitDoor = !activeDrawWall && zoneIndex >= 0 && IsZoneControlledByOpenDoor(zoneIndex);
                target.activeDrawWall = activeDrawWall;
            };

            for (int zi = 0; zi < static_cast<int>(g_app.document.zones.size()); ++zi)
            {
                if (!IsWalkPreviewRenderableWallIndex(zi)) continue;
                considerWallHit(g_app.document.zones[zi], zi, false);
            }
            if (hasActiveDrawWall)
            {
                considerWallHit(activeDrawWall, -1, true);
            }

            auto drawWallHit = [&](const WallHitCandidate& hit, bool asMoveGuide)
            {
                if (hit.t >= maxDistance || (hit.zoneIndex < 0 && !hit.activeDrawWall))
                {
                    return;
                }

                const auto& hitZone = hit.activeDrawWall ? activeDrawWall : g_app.document.zones[hit.zoneIndex];
                const int sampleZoneIndex = hit.activeDrawWall ? -1 : hit.zoneIndex;
                const double corrected = hit.t;
                const int wallH = ClampValue(static_cast<int>(std::lround((kWalkPreviewWallHeightWorld * projectionScale) / MaxValue(48.0, corrected))), 8, h * 4);
                const int y1 = h / 2 - wallH / 2;
                const int y2 = y1 + wallH;
                const int drawTop = MaxValue(0, y1);
                const int drawBottom = MinValue(h, y2);

                for (int y = drawTop; y < drawBottom; ++y)
                {
                    const double v = static_cast<double>(y - y1) / MaxValue(1.0, static_cast<double>(wallH));
                    if (asMoveGuide)
                    {
                        const size_t pixelIndex = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(c);
                        const COLORREF ghost = SampleWalkPreviewMoveGuideColor(v, hit.t);
                        pixels[pixelIndex] = BlendWalkPreviewDibPixel(pixels[pixelIndex], ghost, 0.50);
                    }
                    else
                    {
                        setPixel(c, y, SampleWalkPreviewWallColor(sampleZoneIndex, hitZone, hit.u, v, hit.hitDoor, hit.t));
                    }
                }
            };

            // Draw the real/opaque wall first, then overlay the move-wall target
            // guide at exactly 50% opacity if it is in front.  This keeps the
            // destination marker visible without hiding what is behind it.
            depth[c] = opaqueHit.t;
            drawWallHit(opaqueHit, false);
            if (guideHit.t < maxDistance && guideHit.t <= opaqueHit.t)
            {
                drawWallHit(guideHit, true);
            }
        }

        struct SpriteMarker
        {
            double depth = 0.0;
            double side = 0.0;
            int type = 0;
        };
        std::vector<SpriteMarker> sprites;
        for (const auto& event : g_app.document.events)
        {
            for (const auto& command : event.commands)
            {
                if (command.type != mapfmt::CommandType::AddMonster) continue;
                const double relX = static_cast<double>(command.params[1]) - g_app.walkPreviewX;
                const double relZ = static_cast<double>(command.params[3]) - g_app.walkPreviewZ;
                const double forward = relX * forwardX + relZ * forwardZ;
                if (forward < 48.0 || forward > maxDistance) continue;
                const double side = relX * rightX + relZ * rightZ;
                const double halfWidthAtDepth = forward * halfFovTan;
                if (std::abs(side) > halfWidthAtDepth + 160.0) continue;
                sprites.push_back(SpriteMarker{ forward, side, command.params[0] });
            }
        }
        std::sort(sprites.begin(), sprites.end(), [](const SpriteMarker& a, const SpriteMarker& b) { return a.depth > b.depth; });
        for (const auto& sprite : sprites)
        {
            const double sxNorm = 0.5 + sprite.side / (2.0 * sprite.depth * halfFovTan);
            const int centerX = static_cast<int>(std::lround(sxNorm * static_cast<double>(w)));
            if (centerX < -w || centerX > w * 2) continue;

            ObjectPreviewImage spriteImage;
            const bool hasSpriteImage = LoadObjectPreviewImage(sprite.type, spriteImage) &&
                spriteImage.width > 0 && spriteImage.height > 0 && !spriteImage.pixels.empty();

            if (hasSpriteImage)
            {
                const int floorY = h / 2 + ClampValue(static_cast<int>(std::lround((kWalkPreviewWallHeightWorld * projectionScale) /
                    (2.0 * MaxValue(72.0, sprite.depth)))), 0, h * 2);
                const bool isEnemySprite = (MonsterTypeCategory(sprite.type) == L"Enemy / actor");
                const double spriteWorldHeight = isEnemySprite ? 150.0 : 75.0;
                const int spriteH = ClampValue(static_cast<int>(std::lround((spriteWorldHeight * projectionScale) / MaxValue(72.0, sprite.depth))), 4, h);
                const int spriteW = ClampValue(static_cast<int>(std::lround(static_cast<double>(spriteH) *
                    (static_cast<double>(spriteImage.width) / MaxValue(1.0, static_cast<double>(spriteImage.height))))), 3, w);
                const int bottom = floorY;
                const int top = bottom - spriteH;
                const int left = centerX - spriteW / 2;

                for (int dy = 0; dy < spriteH; ++dy)
                {
                    const int py = top + dy;
                    if (py < 0 || py >= h) continue;
                    const int sy = ClampValue((dy * spriteImage.height) / MaxValue(1, spriteH), 0, spriteImage.height - 1);
                    for (int dxp = 0; dxp < spriteW; ++dxp)
                    {
                        const int pxScreen = left + dxp;
                        if (pxScreen < 0 || pxScreen >= w) continue;
                        if (sprite.depth + 8.0 >= depth[pxScreen]) continue;
                        const int sx = ClampValue((dxp * spriteImage.width) / MaxValue(1, spriteW), 0, spriteImage.width - 1);
                        const uint32_t sourcePixel = spriteImage.pixels[static_cast<size_t>(sy) * static_cast<size_t>(spriteImage.width) + static_cast<size_t>(sx)];
                        if (IsWalkPreviewTransparentSpritePixel(sourcePixel)) continue;
                        pixels[static_cast<size_t>(py) * static_cast<size_t>(w) + static_cast<size_t>(pxScreen)] =
                            ShadeWalkPreviewSpritePixel(sourcePixel, sprite.depth);
                    }
                }
            }
            else
            {
                const int column = ClampValue(centerX, 0, columns - 1);
                if (sprite.depth + 8.0 >= depth[column]) continue;
                const bool isEnemySprite = (MonsterTypeCategory(sprite.type) == L"Enemy / actor");
                const int maxMarkerSize = isEnemySprite ? 32 : 16;
                const double markerScale = isEnemySprite ? 9000.0 : 4500.0;
                const int size = ClampValue(static_cast<int>(std::lround(markerScale / MaxValue(96.0, sprite.depth))), 4, maxMarkerSize);
                const int floorY = h / 2 + ClampValue(static_cast<int>(std::lround((kWalkPreviewWallHeightWorld * projectionScale) /
                    (2.0 * MaxValue(72.0, sprite.depth)))), 0, h * 2);
                const int cy = floorY;
                const COLORREF fallbackColor = WalkPreviewObjectColor(sprite.type);
                const uint32_t fallbackPixel = WalkPreviewDibPixel(fallbackColor);
                for (int yy = cy - size; yy < cy; ++yy)
                {
                    if (yy < 0 || yy >= h) continue;
                    for (int xx = centerX - size / 2; xx < centerX + size / 2; ++xx)
                    {
                        if (xx < 0 || xx >= w) continue;
                        const double ex = (static_cast<double>(xx - centerX) / MaxValue(1.0, static_cast<double>(size) * 0.5));
                        const double ey = (static_cast<double>(yy - (cy - size / 2)) / MaxValue(1.0, static_cast<double>(size) * 0.5));
                        if (ex * ex + ey * ey <= 1.0)
                        {
                            pixels[static_cast<size_t>(yy) * static_cast<size_t>(w) + static_cast<size_t>(xx)] = fallbackPixel;
                        }
                    }
                }
            }
        }

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        SetDIBitsToDevice(hdc, viewRc.left, viewRc.top, w, h, 0, 0, 0, h, pixels.data(), &bmi, DIB_RGB_COLORS);

        HPEN framePen = CreatePen(PS_SOLID, 1, RGB(80, 80, 92));
        HGDIOBJ oldPen = SelectObject(hdc, framePen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(hdc, viewRc.left, viewRc.top, viewRc.right, viewRc.bottom);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(framePen);

        RECT labelRc{ viewRc.left + 6, viewRc.top + 5, viewRc.right - 6, viewRc.top + 24 };
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(220, 220, 226));
        DrawTextW(hdc, L"3D Walk Preview", -1, &labelRc, DT_LEFT | DT_TOP | DT_SINGLELINE);
    }

    bool HandleWalkPreviewClick(HWND hwnd, int sx, int sy)
    {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        if (!ShouldShowWalkPreview(rc)) return false;
        const RECT controls = GetWalkPreviewControlsRect(rc);
        const int gap = 5;
        const int rowH = 22;
        const int colW = MaxValue(42, (controls.right - controls.left - gap * 2) / 3);
        auto buttonRect = [&](int col, int row) -> RECT
        {
            return RECT{ controls.left + col * (colW + gap), controls.top + row * (rowH + 5),
                controls.left + col * (colW + gap) + colW, controls.top + row * (rowH + 5) + rowH };
        };
        auto contains = [&](const RECT& b) { return sx >= b.left && sx < b.right && sy >= b.top && sy < b.bottom; };
        constexpr double step = 256.0;
        if (contains(buttonRect(0, 0)))
        {
            g_app.walkPreviewDir = NormalizeWalkPreviewDir(g_app.walkPreviewDir - 1);
        }
        else if (contains(buttonRect(2, 0)))
        {
            g_app.walkPreviewDir = NormalizeWalkPreviewDir(g_app.walkPreviewDir + 1);
        }
        else if (contains(buttonRect(1, 0)))
        {
            MoveWalkPreview(step, 0.0);
            return true;
        }
        else if (contains(buttonRect(1, 1)))
        {
            MoveWalkPreview(-step, 0.0);
            return true;
        }
        else if (contains(buttonRect(0, 1)))
        {
            MoveWalkPreview(0.0, -step);
            return true;
        }
        else if (contains(buttonRect(2, 1)))
        {
            MoveWalkPreview(0.0, step);
            return true;
        }
        else
        {
            return false;
        }
        InvalidateWalkPreview();
        return true;
    }

    void UpdateInfoPanelScrollBar(HWND hwnd)
    {
        AutoScrollPreviewToSelectedStrip();
        if (!hwnd) return;
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const PreviewMetrics metrics = GetPreviewMetrics(rc);

        g_app.previewScrollX = ClampValue(g_app.previewScrollX, 0, metrics.maxScrollX);
    }

    void PaintInfoPanel(HWND hwnd)
    {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);

        // v45: use the same background as the main UI so the inspector no
        // longer appears as a separate darker panel.
        FillRect(hdc, &rc, DarkWindowBrush());
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
        DrawTextW(hdc, L"INSPECTOR", -1, &titleRc, DT_LEFT | DT_TOP | DT_SINGLELINE);

        const PreviewMetrics preview = GetPreviewMetrics(rc);
        const bool texturePickerActive = IsTexturePickerActive();

        SelectObject(hdc, modeFont);
        RECT modeRc{ 12, 46, rc.right - 12, preview.outer.top - 12 };
        std::wstring modeText = L"Mode: " + InsertModeToText(g_app.insertMode);
        SetTextColor(hdc, RGB(232, 232, 232));
        DrawTextW(hdc, modeText.c_str(), -1, &modeRc, DT_LEFT | DT_TOP | DT_SINGLELINE);

        SelectObject(hdc, textFont);
        int y = 76;
        std::wstring detailText;
        std::wstring noteText;
        bool anchorNoteToPreviewBox = false;
        bool anchorNoteCloseToDetails = false;

        if (IsWallTexturePickerActive())
        {
            std::wstringstream details;
            const int slot = ClampValue(g_app.activeWallTextureSlot, 0, static_cast<int>(mapfmt::MapDocument::kTextureSlotCount) - 1);
            const int strip = ClampValue(g_app.activeWallTextureStrip, 0, 19);
            const bool switchTextureContext = IsSwitchTexturePickerContext() && !IsWallTextureMappingPickerActive();
            const bool editingSelectedWall = !switchTextureContext && (g_app.insertMode != InsertMode::Wall && IsSelectedWall());

            if (switchTextureContext)
            {
                details << L"Switch ON texture preview: T" << slot << L"  Section: " << (strip + 1) << L"/20\r\n";

                int triggerZoneIndex = -1;
                int eventIndex = -1;
                if (g_app.linkEventTriggerZone >= 0 &&
                    g_app.linkEventTriggerZone < static_cast<int>(g_app.document.zones.size()) &&
                    IsEventTriggerLineZone(g_app.document.zones[g_app.linkEventTriggerZone]))
                {
                    triggerZoneIndex = g_app.linkEventTriggerZone;
                    eventIndex = EventSlotFromZoneEventValue(g_app.document.zones[triggerZoneIndex].ev);
                }
                else if (g_app.selectedZone >= 0 && g_app.selectedZone < static_cast<int>(g_app.document.zones.size()))
                {
                    const auto& zone = g_app.document.zones[g_app.selectedZone];
                    if (IsEventTriggerLineZone(zone))
                    {
                        triggerZoneIndex = g_app.selectedZone;
                        eventIndex = EventSlotFromZoneEventValue(zone.ev);
                    }
                }

                if (triggerZoneIndex >= 0 && triggerZoneIndex < static_cast<int>(g_app.document.zones.size()))
                {
                    const auto& zone = g_app.document.zones[triggerZoneIndex];
                    details << L"Trigger: Z" << triggerZoneIndex << L" -> Event " << zone.ev << L"\r\n";
                }

                SwitchTextureCommandInfo existingSwitch{};
                if (triggerZoneIndex >= 0 && TryGetSwitchTextureCommandForTriggerZone(triggerZoneIndex, existingSwitch))
                {
                    details << L"Existing switch: Z" << existingSwitch.rawTargetZoneIndex
                            << L" -> " << FormatTextureIndexShort(existingSwitch.newTextureIndex) << L"\r\n";
                }
                else
                {
                    int existingTarget = -1;
                    int existingTexture = -1;
                    if (TryGetFirstSwitchTextureCommandForEvent(eventIndex, existingTarget, existingTexture))
                    {
                        details << L"Existing switch: Z" << existingTarget << L" -> " << FormatTextureIndexShort(existingTexture) << L"\r\n";
                    }
                }

                details << L"Manual texture ID: " << GetActiveWallAssignedTextureIndex() << L"\r\n";
                const std::wstring animInfo = FormatAnimationInfoForTexture(GetActiveWallTextureIndex());
                if (!animInfo.empty())
                {
                    details << animInfo << L"\r\n";
                }
                details << L"File: " << Utf8ToWide(g_app.document.textureNames[slot]);
                detailText = details.str();
                noteText = (g_app.insertMode == InsertMode::LinkEventToSwitchTexture)
                    ? L"Click the OFF switch/wall panel. Static panels write OFF+1; animated panels write the first texture after the OFF animation."
                    : L"Switch texture is derived from the clicked OFF wall/panel; no trigger-side texture assignment is needed.";
            }
            else
            {
                details << (editingSelectedWall ? L"Selected wall texture: T" : L"Draw Wall texture: T")
                        << slot << L"  Section: " << (strip + 1) << L"/20\r\n";
                if (editingSelectedWall)
                {
                    const auto& zone = g_app.document.zones[g_app.selectedZone];
                    details << L"Wall: Z" << g_app.selectedZone << L"\r\n";
                    details << L"Wall band: " << (GetActiveTextureBandForZone(zone) + 1)
                            << L"/" << GetZonePreviewSlotCount(zone) << L"\r\n";
                    details << L"Mapping: " << WallTextureModeLabel(GetWallTextureModeForZone(zone)) << L"\r\n";
                }
                details << L"Texture ID: " << GetActiveWallAssignedTextureIndex() << L"\r\n";
                details << L"Mapping: " << WallTextureModeLabel(g_app.activeWallTextureMode) << L"\r\n";
                const std::wstring animInfo = FormatAnimationInfoForTexture(GetActiveWallTextureIndex());
                if (!animInfo.empty())
                {
                    details << animInfo << L"\r\n";
                }
                details << L"File: " << Utf8ToWide(g_app.document.textureNames[slot]);
                detailText = details.str();
                noteText = editingSelectedWall
                    ? L"Click a wall segment to choose its band. Click T0-T7 and a 64px preview section to assign it. Ctrl+Z restores the previous texture and mapping."
                    : L"Select a texture before drawing. New walls use the selected mapping; Stretch squeezes one strip, 1:1 Clip/Repeat maps by world length.";
            }
        }
        else if (IsSelectedMonsterSpawnValid())
        {
            const mapfmt::EventCommand* command = GetSelectedMonsterSpawnCommand();
            std::wstringstream details;
            const int marker = g_app.selectedMonsterSpawn.markerIndex;
            const int eventNumber = g_app.selectedMonsterSpawn.eventIndex + 1;
            const int commandNumber = g_app.selectedMonsterSpawn.commandIndex + 1;
            const int monsterType = command ? command->params[0] : -1;
            details << L"Selected object: M" << marker << L"\r\n";
            details << L"Kind: " << MonsterTypeCategory(monsterType) << L"\r\n";
            details << L"Type: " << monsterType << L" - " << MonsterTypeName(monsterType) << L"\r\n";
            details << L"Source: Event " << eventNumber << L", command " << commandNumber << L"\r\n";
            if (command)
            {
                details << L"Position: X " << command->params[1]
                        << L"  Y " << command->params[2]
                        << L"  Z " << command->params[3] << L"\r\n";
                details << L"Rotation: " << (command->params[4] & 255)
                        << L" / 255 (~" << MonsterRotationDegrees(command->params[4]) << L" deg)\r\n";
            }
            if (!g_app.objectPreviewImage.loadedPath.empty())
            {
                details << L"Sprite: " << FormatPreviewSourcePath(g_app.objectPreviewImage.loadedPath)
                        << L"  frame " << (g_app.objectPreviewImage.frameIndex + 1)
                        << L"/" << MaxValue(1, g_app.objectPreviewImage.frameCount) << L"\r\n";
            }
            else if (!g_app.objectPreviewImage.error.empty())
            {
                details << L"Sprite: " << Utf8ToWide(g_app.objectPreviewImage.error) << L"\r\n";
            }
            details << L"\r\nThis marker is an Add Monster event command, not a map zone.";
            detailText = details.str();
            noteText = L"Drag this marker in the map to move it. Use Left/Right to rotate by 5 deg. Edit exact values in Map > Events. The preview below uses the original object sprite when objs/ or char/ files are available.";
            anchorNoteToPreviewBox = true;
        }
        else if (g_app.insertMode == InsertMode::ObjectSpawn)
        {
            std::wstringstream details;
            details << L"Placing map-start object\r\n";
            details << L"Kind: " << MonsterTypeCategory(g_app.placeObjectType) << L"\r\n";
            details << L"Type: " << g_app.placeObjectType << L" - " << MonsterTypeName(g_app.placeObjectType) << L"\r\n";
            if (!g_app.objectPreviewImage.loadedPath.empty())
            {
                details << L"Sprite: " << FormatPreviewSourcePath(g_app.objectPreviewImage.loadedPath)
                        << L"  frame " << (g_app.objectPreviewImage.frameIndex + 1)
                        << L"/" << MaxValue(1, g_app.objectPreviewImage.frameCount) << L"\r\n";
            }
            detailText = details.str();
            noteText = L"Click the map to create a startup Add Monster command. It will not be tied to a trigger unless you explicitly use Link Event > Enemy/Objects. Rotation defaults to 0 and can be edited in Map > Events.";
            anchorNoteToPreviewBox = true;
        }
        else if (g_app.selectedZone >= 0 && g_app.selectedZone < static_cast<int>(g_app.document.zones.size()))
        {
            const auto& zone = g_app.document.zones[g_app.selectedZone];
            const int activeSlotCount = GetZonePreviewSlotCount(zone);
            const int band = ClampValue(g_app.previewTextureBand, 0, activeSlotCount - 1);
            std::wstringstream details;
            details << L"Selected: Z" << g_app.selectedZone << L"\r\n";
            details << L"Type: " << ZoneRoleToText(zone) << L"\r\n";
            details << L"Coords: (" << zone.x1 << L", " << zone.z1 << L") -> (" << zone.x2 << L", " << zone.z2 << L")\r\n";
            details << L"Event: " << zone.ev;
            if (zone.ev >= 1 && zone.ev <= mapfmt::MapDocument::kEventCount)
            {
                details << L" (" << EventSlotRoleText(zone.ev - 1) << L")";
            }
            details << L"\r\n";
            const bool selectedEventTrigger = IsEventTriggerLineZone(zone);
            const bool selectedLevelEnd = IsLevelEndZone(zone);
            if (selectedLevelEnd)
            {
                details << L"Function: level exit / level complete trigger (ev=24).\r\n";
            }
            else if (selectedEventTrigger)
            {
                details << L"Function: Player crossing or touching this line triggers event " << zone.ev << L".\r\n";
            }
            else if (zone.ztype == static_cast<int16_t>(mapfmt::ZoneType::MonsterZone))
            {
                details << L"Function: map monster/logic line with no valid event assigned.\r\n";
                details << L"Actual enemies are Add Monster event commands and are shown as M markers.\r\n";
            }

            const std::wstring logicInfo = FormatEventLogicSummaryForZone(g_app.selectedZone);
            if (selectedEventTrigger || selectedLevelEnd)
            {
                if (!logicInfo.empty())
                {
                    details << L"\r\n" << logicInfo;
                }
            }
            else
            {
                details << L"Preview band: " << (band + 1) << L"/" << activeSlotCount << L"\r\n";
                details << L"Texture index: " << g_app.previewTextureIndex
                        << L"  Slot file: " << g_app.previewTextureSlot
                        << L"  Strip: " << (g_app.previewTextureStrip + 1) << L"/20";
                if (!logicInfo.empty())
                {
                    details << L"\r\n" << logicInfo;
                }
                const std::wstring animInfo = FormatAnimationInfoForTexture(g_app.previewTextureIndex);
                if (!animInfo.empty())
                {
                    details << L"\r\n" << animInfo;
                }
            }
            const std::wstring validationInfo = FormatSelectedZoneValidationSummary(g_app.selectedZone);
            if (!validationInfo.empty())
            {
                details << L"\r\nValidation:\r\n" << validationInfo;
            }
            detailText = details.str();
            if (IsLevelEndZone(zone))
            {
                noteText = L"Use Set Level End to move this line. Level End is stored as the special ev=24 crossing line observed in original maps.";
                anchorNoteCloseToDetails = true;
            }
            else if (IsEventTriggerLineZone(zone))
            {
                noteText = L"Trigger lines are red/orange crossing segments. Use Link Event > Wall/Door for moving/opening geometry, Link Event > Switch/Trigger for switch panels, Link Event > Enemy/Objects for spawned actors/items, or Delete Link Event to remove a link.";
                anchorNoteCloseToDetails = true;
            }
            else if (zone.ztype == static_cast<int16_t>(mapfmt::ZoneType::MonsterZone))
            {
                noteText = L"Type-2 lines without valid Event ID are shown as monster/logic helper lines. Purple M markers are real Add Monster spawns from event scripts.";
            }
            else
            {
                noteText = L"Note: wall texture files contain 20 strips side by side. Stretch squeezes one strip onto the wall; 1:1 Clip/Repeat maps one full strip per 256 world units and clips/repeats it.";
            }
        }
        else
        {
            std::wstringstream details;
            details << L"No zone selected.\r\n";
            if (const mapfmt::EventCommand* p1 = FindPlayerStartCommandConst(kPlayer1ObjectType))
            {
                details << L"P1 Start: X " << p1->params[1] << L"  Z " << p1->params[3] << L"\r\n";
            }
            else
            {
                details << L"P1 Start: not set\r\n";
            }
            const int exitZone = FindLevelEndZoneIndex();
            if (exitZone >= 0)
            {
                const auto& exit = g_app.document.zones[exitZone];
                details << L"Level End: Z" << exitZone << L" (" << exit.x1 << L"," << exit.z1 << L") -> (" << exit.x2 << L"," << exit.z2 << L")\r\n";
            }
            else
            {
                details << L"Level End: not set\r\n";
            }
            int triggerLineCount = 0;
            int controlledZoneCount = 0;
            for (int i = 0; i < static_cast<int>(g_app.document.zones.size()); ++i)
            {
                if (IsEventTriggerLineZone(g_app.document.zones[i])) ++triggerLineCount;
                if (!GetEventsControllingZone(i).empty()) ++controlledZoneCount;
            }
            details << L"\r\nMap overlays: Walls " << CountZonesOfType(mapfmt::ZoneType::Wall)
                    << L", Trigger Lines " << triggerLineCount
                    << L", Controlled Door/Wall Zones " << controlledZoneCount
                    << L", M Spawns " << CountMonsterSpawns() << L".";
            detailText = details.str();
            noteText = L"Floor and ceiling textures are not edited or saved by this map editor, because the original map file has no confirmed game-compatible field for them.";
        }

        // Do not let trailing CR/LF from formatted detail blocks reserve an
        // additional empty line before short muted notes. Event-trigger and
        // level-end hints should sit close to their Logic/Function text.
        while (!detailText.empty() && (detailText.back() == L'\r' || detailText.back() == L'\n'))
        {
            detailText.pop_back();
        }

        const RECT slotBarForLayout = texturePickerActive ? GetTextureSlotBarRect(rc) : RECT{ 0, 0, 0, 0 };
        const int pickerTop = texturePickerActive
            ? (IsWallTextureMappingPickerActive() ? GetWallTextureModeRowRect(rc).top
               : (IsSurfaceTexturePickerActive() ? (slotBarForLayout.top - 60) : slotBarForLayout.top))
            : preview.outer.top;
        const int fixedPickerHelpHeight = texturePickerActive ? 46 : 0;
        const int fixedPreviewHelpHeight = anchorNoteToPreviewBox ? 52 : 0;
        const int attachedHelpHeight = texturePickerActive ? fixedPickerHelpHeight : fixedPreviewHelpHeight;
        const int attachedHelpTargetTop = texturePickerActive ? pickerTop : preview.outer.top;
        const int textBottomLimit = attachedHelpHeight > 0
            ? (attachedHelpTargetTop - attachedHelpHeight - 8)
            : (preview.outer.top - 34);
        RECT detailRc{ 12, y, rc.right - 12, MaxValue(y + 48, textBottomLimit) };
        SetTextColor(hdc, RGB(220, 220, 220));
        DrawTextW(hdc, detailText.c_str(), -1, &detailRc, DT_LEFT | DT_TOP | DT_WORDBREAK);

        if (!noteText.empty())
        {
            if (texturePickerActive || anchorNoteToPreviewBox)
            {
                // Keep short hints visually attached to the block they explain:
                // wall hints to the texture selector, object/player-start hints
                // to the sprite preview box. This prevents floating helper text
                // from sticking to the upper inspector paragraph.
                const int targetTop = texturePickerActive ? pickerTop : preview.outer.top;
                const int helpHeight = texturePickerActive ? fixedPickerHelpHeight : 52;
                RECT noteMeasureRc{ 12, 0, rc.right - 12, helpHeight };
                DrawTextW(hdc, noteText.c_str(), -1, &noteMeasureRc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_CALCRECT);
                const int noteHeight = ClampValue(noteMeasureRc.bottom - noteMeasureRc.top, 16, helpHeight);
                RECT noteRc{ 12, MaxValue(y + 48, targetTop - 6 - noteHeight), rc.right - 12, targetTop - 6 };
                if (noteRc.bottom > noteRc.top)
                {
                    SetTextColor(hdc, kDarkMutedText);
                    DrawTextW(hdc, noteText.c_str(), -1, &noteRc, DT_LEFT | DT_TOP | DT_WORDBREAK);
                }
            }
            else
            {
                RECT detailMeasureRc = detailRc;
                DrawTextW(hdc, detailText.c_str(), -1, &detailMeasureRc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_CALCRECT);
                const int noteGap = anchorNoteCloseToDetails ? 16 : 18;
                const int noteTop = detailMeasureRc.bottom + noteGap;
                const int noteBottomLimit = anchorNoteCloseToDetails
                    ? MinValue(preview.outer.top - 10, noteTop + 52)
                    : (preview.outer.top - 14);
                if (noteTop < noteBottomLimit)
                {
                    RECT noteRc{ 12, noteTop, rc.right - 12, noteBottomLimit };
                    SetTextColor(hdc, kDarkMutedText);
                    DrawTextW(hdc, noteText.c_str(), -1, &noteRc, DT_LEFT | DT_TOP | DT_WORDBREAK);
                }
            }
        }

        if (texturePickerActive)
        {
            const RECT slotBar = slotBarForLayout;
            SelectObject(hdc, textFont);

            auto drawDarkButton = [&](RECT buttonRc, const std::wstring& label, bool active, bool enabled = true, COLORREF textOverride = CLR_INVALID)
            {
                HBRUSH brush = CreateSolidBrush(active ? RGB(64, 74, 104) : (enabled ? RGB(44, 44, 52) : RGB(34, 34, 40)));
                FillRect(hdc, &buttonRc, brush);
                DeleteObject(brush);
                HPEN pen = CreatePen(PS_SOLID, 1, active ? RGB(255, 220, 120) : (enabled ? RGB(92, 92, 104) : RGB(58, 58, 66)));
                HGDIOBJ oldPen = SelectObject(hdc, pen);
                HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
                Rectangle(hdc, buttonRc.left, buttonRc.top, buttonRc.right, buttonRc.bottom);
                SelectObject(hdc, oldBrush);
                SelectObject(hdc, oldPen);
                DeleteObject(pen);
                const COLORREF textColor = (textOverride != CLR_INVALID)
                    ? textOverride
                    : (active ? RGB(255, 235, 160) : (enabled ? RGB(210, 210, 218) : RGB(118, 118, 128)));
                SetTextColor(hdc, textColor);
                DrawTextW(hdc, label.c_str(), -1, &buttonRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            };

            if (IsWallTextureMappingPickerActive())
            {
                const RECT modeRow = GetWallTextureModeRowRect(rc);
                const int buttonGap = 6;
                const int totalW = MaxValue(1, modeRow.right - modeRow.left - (buttonGap * 2));
                const int buttonW = MaxValue(1, totalW / 3);
                const RECT stretchRc{ modeRow.left, modeRow.top, modeRow.left + buttonW, modeRow.bottom };
                const RECT repeatRc{ stretchRc.right + buttonGap, modeRow.top, stretchRc.right + buttonGap + buttonW, modeRow.bottom };
                const RECT clipRc{ repeatRc.right + buttonGap, modeRow.top, modeRow.right, modeRow.bottom };
                drawDarkButton(stretchRc, L"Stretch", g_app.activeWallTextureMode == WallTextureMode::Stretch);
                drawDarkButton(repeatRc, L"1:1", g_app.activeWallTextureMode == WallTextureMode::Repeat1To1);
                drawDarkButton(clipRc, L"2/8 Clip", g_app.activeWallTextureMode == WallTextureMode::Clip2Of8);
            }

            if (IsSurfaceTexturePickerActive())
            {
                const RECT surfaceRow{ slotBar.left, slotBar.top - 60, slotBar.right, slotBar.top - 36 };
                const RECT floorRc{ surfaceRow.left, surfaceRow.top,
                                    surfaceRow.left + (surfaceRow.right - surfaceRow.left) / 2 - 3, surfaceRow.bottom };
                const RECT ceilRc{ floorRc.right + 6, surfaceRow.top, surfaceRow.right, surfaceRow.bottom };
                drawDarkButton(floorRc, L"Floor", g_app.activeSurfaceTextureTarget == SurfaceTextureTarget::Floor);
                drawDarkButton(ceilRc, L"Ceiling", g_app.activeSurfaceTextureTarget == SurfaceTextureTarget::Ceiling);

                const RECT prevRc{ slotBar.left, slotBar.top,
                                   slotBar.left + (slotBar.right - slotBar.left) / 2 - 3, slotBar.bottom };
                const RECT nextRc{ prevRc.right + 6, slotBar.top, slotBar.right, slotBar.bottom };
                const int activeChoice = GetActiveSurfaceTextureChoice();
                const int activeCount = static_cast<int>(ActiveSurfaceTextureList().size());
                drawDarkButton(prevRc, L"< Previous", false, activeChoice > 0);
                drawDarkButton(nextRc, L"Next >", false, activeChoice + 1 < activeCount);
            }
            else
            {
                const int slotWidth = MaxValue(1, (slotBar.right - slotBar.left) / static_cast<int>(mapfmt::MapDocument::kTextureSlotCount));
                const int activeSlot = g_app.activeWallTextureSlot;
                for (int i = 0; i < static_cast<int>(mapfmt::MapDocument::kTextureSlotCount); ++i)
                {
                    RECT buttonRc{ slotBar.left + i * slotWidth, slotBar.top,
                                   (i == static_cast<int>(mapfmt::MapDocument::kTextureSlotCount) - 1) ? slotBar.right : slotBar.left + (i + 1) * slotWidth - 2,
                                   slotBar.bottom };
                    const bool active = (i == activeSlot);
                    const bool hasTexture = !g_app.document.textureNames[i].empty();
                    std::wstringstream label;
                    label << L"T" << i;
                    drawDarkButton(buttonRc, label.str(), active, true, hasTexture ? RGB(255, 255, 255) : kDarkMutedText);
                }
            }
        }

        const bool hideZoneTextureOverview = IsSelectedEventTriggerOrLevelEndZone();
        if (!hideZoneTextureOverview)
        {
            HGDIOBJ oldPreviewBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
            Rectangle(hdc, preview.outer.left, preview.outer.top, preview.outer.right, preview.outer.bottom);
            SelectObject(hdc, oldPreviewBrush);

            if (IsSelectedMonsterSpawnValid() && GetSelectedMonsterSpawnCommand())
            {
                DrawSelectedMonsterObjectPreview(hdc, preview.inner, *GetSelectedMonsterSpawnCommand());
            }
            else if (g_app.insertMode == InsertMode::ObjectSpawn)
            {
                mapfmt::EventCommand previewCommand;
                previewCommand.type = mapfmt::CommandType::AddMonster;
                previewCommand.params[0] = static_cast<int16_t>(g_app.placeObjectType);
                DrawSelectedMonsterObjectPreview(hdc, preview.inner, previewCommand);
            }
            else if (!g_app.previewImage.pixels.empty() && g_app.previewImage.width > 0 && g_app.previewImage.height > 0)
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
            const int activeTextureIndex = ClampValue(g_app.previewTextureSlot * 20 + g_app.previewTextureStrip, 0, 159);
            mapfmt::AnimationEntry activeAnim{};
            const bool hasActiveAnim = !IsSurfaceTexturePickerActive() && ResolveAnimationForTextureIndex(activeTextureIndex, activeAnim);
            int stripSrcX = 0;
            int stripSrcWidth = g_app.previewImage.width;
            if (!IsSurfaceTexturePickerActive())
            {
                stripSrcX = ClampValue(g_app.previewTextureStrip, 0, 19) * 64;
                stripSrcWidth = 64;
                if (hasActiveAnim && (activeAnim.first / 20) == g_app.previewTextureSlot)
                {
                    stripSrcX = ClampValue(static_cast<int>(activeAnim.first % 20), 0, 19) * 64;
                    stripSrcWidth = ClampValue(static_cast<int>(activeAnim.frames), 1, 20) * 64;
                }
                stripSrcWidth = MaxValue(1, MinValue(stripSrcWidth, g_app.previewImage.width - stripSrcX));
            }
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
            if (hasActiveAnim && activeAnim.frames > 1)
            {
                for (uint16_t frame = 1; frame < activeAnim.frames; ++frame)
                {
                    const int divX = stripDrawX + static_cast<int>(std::lround(static_cast<double>(frame * 64) * previewScaleX));
                    MoveToEx(hdc, divX, preview.inner.top, nullptr);
                    LineTo(hdc, divX, preview.inner.bottom);
                }
            }
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

            const RECT scrollRc = GetPreviewScrollBarRect(rc);
            if (preview.maxScrollX > 0)
            {
                const RECT thumbRc = GetPreviewScrollThumbRect(scrollRc, preview);
                DrawSlimHorizontalScrollBar(hdc, scrollRc, thumbRc, g_app.previewScrollHover, g_app.previewScrollDragging);
            }
        }

        if (ShouldShowWalkPreview(rc))
        {
            const RECT walkPreviewRc = GetWalkPreviewRect(rc);
            DrawWalkPreview3D(hdc, walkPreviewRc);
            DrawWalkPreviewControls(hdc, rc);
        }

        SelectObject(hdc, oldFont);
        DeleteObject(titleFont);
        DeleteObject(modeFont);
        DeleteObject(textFont);
        EndPaint(hwnd, &ps);
    }

    bool BeginPreviewScrollBarDrag(HWND hwnd, int sx, int sy)
    {
        if (IsSelectedEventTriggerOrLevelEndZone()) return false;
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const PreviewMetrics preview = GetPreviewMetrics(rc);
        if (preview.maxScrollX <= 0) return false;
        const RECT scrollRc = GetPreviewScrollBarRect(rc);
        if (sx < scrollRc.left || sx >= scrollRc.right || sy < scrollRc.top || sy >= scrollRc.bottom) return false;

        const RECT thumbRc = GetPreviewScrollThumbRect(scrollRc, preview);
        if (sx >= thumbRc.left && sx < thumbRc.right && sy >= thumbRc.top && sy < thumbRc.bottom)
        {
            g_app.previewScrollDragOffsetX = sx - thumbRc.left;
        }
        else
        {
            g_app.previewScrollDragOffsetX = MaxValue(1, thumbRc.right - thumbRc.left) / 2;
            SetPreviewScrollFromThumbX(hwnd, sx, g_app.previewScrollDragOffsetX);
        }

        BeginSmoothPreviewScrollDrag(hwnd, sx);
        g_app.previewScrollDragging = true;
        SetCapture(hwnd);
        return true;
    }


    RECT GetWallTextureModeRowRect(const RECT& panelRc)
    {
        const RECT slotBar = GetTextureSlotBarRect(panelRc);
        return RECT{ slotBar.left, slotBar.top - 32, slotBar.right, slotBar.top - 8 };
    }

    bool HandleWallTextureModeClick(HWND hwnd, int sx, int sy)
    {
        if (!IsWallTextureMappingPickerActive()) return false;

        RECT rc{};
        GetClientRect(hwnd, &rc);
        const RECT modeRow = GetWallTextureModeRowRect(rc);
        if (sy < modeRow.top || sy >= modeRow.bottom || sx < modeRow.left || sx >= modeRow.right)
        {
            return false;
        }

        const int buttonGap = 6;
        const int totalW = MaxValue(1, modeRow.right - modeRow.left - (buttonGap * 2));
        const int buttonW = MaxValue(1, totalW / 3);
        const RECT stretchRc{ modeRow.left, modeRow.top, modeRow.left + buttonW, modeRow.bottom };
        const RECT repeatRc{ stretchRc.right + buttonGap, modeRow.top, stretchRc.right + buttonGap + buttonW, modeRow.bottom };
        const RECT clipRc{ repeatRc.right + buttonGap, modeRow.top, modeRow.right, modeRow.bottom };

        WallTextureMode newMode = g_app.activeWallTextureMode;
        if (sx >= stretchRc.left && sx < stretchRc.right)
        {
            newMode = WallTextureMode::Stretch;
        }
        else if (sx >= repeatRc.left && sx < repeatRc.right)
        {
            newMode = WallTextureMode::Repeat1To1;
        }
        else if (sx >= clipRc.left && sx < clipRc.right)
        {
            newMode = WallTextureMode::Clip2Of8;
        }
        else
        {
            return true;
        }

        if (newMode == g_app.activeWallTextureMode)
        {
            return true;
        }

        g_app.activeWallTextureMode = newMode;
        if (IsSelectedWall() && !(g_app.insertMode == InsertMode::Wall && g_app.isDrawing))
        {
            NormalizeSelectedWallToCanonical();
            PushUndoSnapshot();
            auto& zone = g_app.document.zones[g_app.selectedZone];
            ApplyWallTextureModeToZone(zone, newMode);
            NormalizeCollinearConnectedWallsAroundZone(g_app.selectedZone);
            EnsureBackfaceForWallAtIndex(g_app.selectedZone);
            MarkDirty();
            RefreshZoneList();
        }

        RefreshWallTexturePickerPreviewFromActive();
        InvalidateEditorViewsIncludingWalkPreview();
        RefreshStatus();
        return true;
    }

    bool HandleWallTexturePickerClick(HWND hwnd, int sx, int sy)
    {
        if (!IsWallTexturePickerActive()) return false;
        if (HandleWallTextureModeClick(hwnd, sx, sy)) return true;

        auto applyToSelectedWallIfNeeded = [&]()
        {
            if (!IsSelectedWall() || (g_app.insertMode == InsertMode::Wall && g_app.isDrawing))
            {
                return false;
            }

            NormalizeSelectedWallToCanonical();
            PushUndoSnapshot();
            auto& zone = g_app.document.zones[g_app.selectedZone];
            ApplyActiveWallTextureToZoneBand(zone, GetActiveTextureBandForZone(zone));
            NormalizeCollinearConnectedWallsAroundZone(g_app.selectedZone);
            EnsureBackfaceForWallAtIndex(g_app.selectedZone);
            MarkDirty();
            RefreshZoneList();
            // Keep the right-hand picker on the actually clicked strip after
            // assigning an animated texture. A normal RefreshPreviewImage() would
            // resync from the wall's stored first animation frame and collapse
            // the picker back to frame one.
            RefreshWallTexturePickerPreviewFromActive();
            InvalidateEditorViewsIncludingWalkPreview();
            return true;
        };

        auto refreshPickerOnly = [&]()
        {
            // In switch-link mode the picker is an input control.  Persist an
            // existing ChangeTexture command immediately, but do not call
            // RefreshPreviewImage() because that would re-sync the old command
            // from the selected trigger and visually undo the user's T0-T7/strip click.
            if (IsSwitchTexturePickerContext())
            {
                PersistActiveSwitchTextureChoiceForCurrentContext();
                RefreshWallTexturePickerPreviewFromActive();
            }
            else if (g_app.insertMode == InsertMode::Wall)
            {
                // While Draw Wall is active, keep the live picker state instead
                // of re-syncing from the previously selected wall. New walls use
                // the currently visible texture/strip and mapping mode.
                RefreshWallTexturePickerPreviewFromActive();
                InvalidateEditorViewsIncludingWalkPreview();
            }
            else
            {
                RefreshPreviewImage();
            }
            InvalidateRect(hwnd, nullptr, TRUE);
            RefreshStatus();
        };

        RECT rc{};
        GetClientRect(hwnd, &rc);
        const RECT slotBar = GetTextureSlotBarRect(rc);
        if (sx >= slotBar.left && sx < slotBar.right && sy >= slotBar.top && sy < slotBar.bottom)
        {
            const int slotCount = static_cast<int>(mapfmt::MapDocument::kTextureSlotCount);
            const int slot = ClampValue(((sx - slotBar.left) * slotCount) / MaxValue(1, slotBar.right - slotBar.left), 0, slotCount - 1);
            if (slot != g_app.activeWallTextureSlot)
            {
                g_app.activeWallTextureSlot = slot;
                g_app.activeWallTextureStrip = NormalizeStripToAnimationStart(g_app.activeWallTextureSlot, g_app.activeWallTextureStrip);
                if (!applyToSelectedWallIfNeeded())
                {
                    refreshPickerOnly();
                }
            }
            return true;
        }

        const PreviewMetrics preview = GetPreviewMetrics(rc);
        if (sx >= preview.inner.left && sx < preview.inner.right && sy >= preview.inner.top && sy < preview.inner.bottom &&
            g_app.previewImage.width > 0 && preview.scaledWidth > 0)
        {
            const int scrollX = ClampValue(g_app.previewScrollX, 0, preview.maxScrollX);
            const int drawX = preview.inner.left - scrollX;
            const double scale = static_cast<double>(preview.scaledWidth) / static_cast<double>(MaxValue(1, g_app.previewImage.width));
            const int srcX = static_cast<int>(std::floor(static_cast<double>(sx - drawX) / MaxValue(0.0001, scale)));
            const int clickedStrip = ClampValue(srcX / 64, 0, 19);
            // Keep the actually clicked strip as UI state. Animated ranges are
            // still saved by GetActiveWallAssignedTextureIndex() as their first
            // frame, but preserving the click target keeps the preview/picker
            // usable: clicking another strip inside or past an animation range
            // no longer collapses immediately back to frame one before the next
            // click can be processed.
            const int strip = clickedStrip;
            if (strip != g_app.activeWallTextureStrip)
            {
                g_app.activeWallTextureStrip = strip;
                if (!applyToSelectedWallIfNeeded())
                {
                    refreshPickerOnly();
                }
            }
            return true;
        }

        return false;
    }

    bool HandleSurfaceTexturePickerClick(HWND hwnd, int sx, int sy)
    {
        if (!IsSurfaceTexturePickerActive()) return false;

        RECT rc{};
        GetClientRect(hwnd, &rc);
        const RECT slotBar = GetTextureSlotBarRect(rc);

        const RECT surfaceRow{ slotBar.left, slotBar.top - 60, slotBar.right, slotBar.top - 36 };
        const RECT floorRc{ surfaceRow.left, surfaceRow.top,
                            surfaceRow.left + (surfaceRow.right - surfaceRow.left) / 2 - 3, surfaceRow.bottom };
        const RECT ceilRc{ floorRc.right + 6, surfaceRow.top, surfaceRow.right, surfaceRow.bottom };
        const RECT prevRc{ slotBar.left, slotBar.top,
                           slotBar.left + (slotBar.right - slotBar.left) / 2 - 3, slotBar.bottom };
        const RECT nextRc{ prevRc.right + 6, slotBar.top, slotBar.right, slotBar.bottom };

        auto refreshSurfacePreview = [&]()
        {
            RefreshPreviewImage();
            InvalidateRect(hwnd, nullptr, TRUE);
            RefreshStatus();
        };

        if (sx >= floorRc.left && sx < floorRc.right && sy >= floorRc.top && sy < floorRc.bottom)
        {
            g_app.activeSurfaceTextureTarget = SurfaceTextureTarget::Floor;
            refreshSurfacePreview();
            return true;
        }
        if (sx >= ceilRc.left && sx < ceilRc.right && sy >= ceilRc.top && sy < ceilRc.bottom)
        {
            g_app.activeSurfaceTextureTarget = SurfaceTextureTarget::Ceiling;
            refreshSurfacePreview();
            return true;
        }

        if (sx >= prevRc.left && sx < prevRc.right && sy >= prevRc.top && sy < prevRc.bottom)
        {
            if (StepActiveSurfaceTextureChoice(-1))
            {
                MarkDirty();
                refreshSurfacePreview();
            }
            return true;
        }
        if (sx >= nextRc.left && sx < nextRc.right && sy >= nextRc.top && sy < nextRc.bottom)
        {
            if (StepActiveSurfaceTextureChoice(1))
            {
                MarkDirty();
                refreshSurfacePreview();
            }
            return true;
        }

        const PreviewMetrics preview = GetPreviewMetrics(rc);
        if (sx >= preview.inner.left && sx < preview.inner.right && sy >= preview.inner.top && sy < preview.inner.bottom &&
            g_app.previewImage.width > 0 && preview.scaledWidth > 0)
        {
            // Floor/roof files are full 128x128 flat textures, not 64-column
            // wall strips. The preview click is intentionally non-destructive.
            return true;
        }

        return false;
    }

    LRESULT CALLBACK InfoPanelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_ERASEBKGND:
            return 1;

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
        case WM_LBUTTONDOWN:
            if (BeginPreviewScrollBarDrag(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)) ||
                HandleWalkPreviewClick(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)) ||
                HandleWallTexturePickerClick(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)))
            {
                return 0;
            }
            break;

        case WM_MOUSEMOVE:
        {
            if (g_app.previewScrollDragging)
            {
                UpdatePreviewScrollFromSmoothDrag(hwnd, GET_X_LPARAM(lParam));
                return 0;
            }

            RECT rc{};
            GetClientRect(hwnd, &rc);
            const PreviewMetrics preview = GetPreviewMetrics(rc);
            const RECT scrollRc = GetPreviewScrollBarRect(rc);
            const bool canHoverTextureScroll = !IsSelectedEventTriggerOrLevelEndZone() && preview.maxScrollX > 0;
            const bool overScroll = canHoverTextureScroll &&
                                    GET_X_LPARAM(lParam) >= scrollRc.left && GET_X_LPARAM(lParam) < scrollRc.right &&
                                    GET_Y_LPARAM(lParam) >= scrollRc.top && GET_Y_LPARAM(lParam) < scrollRc.bottom;
            if (overScroll != g_app.previewScrollHover)
            {
                g_app.previewScrollHover = overScroll;
                if (overScroll)
                {
                    TRACKMOUSEEVENT tme{};
                    tme.cbSize = sizeof(tme);
                    tme.dwFlags = TME_LEAVE;
                    tme.hwndTrack = hwnd;
                    TrackMouseEvent(&tme);
                }
                InvalidateTexturePreviewOnly(hwnd);
            }
            break;
        }
        case WM_MOUSELEAVE:
            if (g_app.previewScrollHover && !g_app.previewScrollDragging)
            {
                g_app.previewScrollHover = false;
                InvalidateTexturePreviewOnly(hwnd);
            }
            return 0;
        case WM_LBUTTONUP:
            if (g_app.previewScrollDragging)
            {
                g_app.previewScrollDragging = false;
                if (GetCapture() == hwnd) ReleaseCapture();
                return 0;
            }
            break;
        case WM_CAPTURECHANGED:
            if (reinterpret_cast<HWND>(lParam) != hwnd)
            {
                g_app.previewScrollDragging = false;
            }
            break;
        case WM_MOUSEWHEEL:
        {
            if (IsSelectedEventTriggerOrLevelEndZone()) return 0;
            RECT rc{};
            GetClientRect(hwnd, &rc);
            const PreviewMetrics preview = GetPreviewMetrics(rc);
            if (preview.maxScrollX <= 0) return 0;

            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (delta == 0) return 0;
            g_app.previewWheelRemainder += delta;

            constexpr int kPreviewWheelPixelsPerNotch = 32;
            const int pixelDelta = (g_app.previewWheelRemainder * kPreviewWheelPixelsPerNotch) / WHEEL_DELTA;
            if (pixelDelta != 0)
            {
                g_app.previewWheelRemainder -= (pixelDelta * WHEEL_DELTA) / kPreviewWheelPixelsPerNotch;
                g_app.previewScrollX = ClampValue(g_app.previewScrollX - pixelDelta, 0, preview.maxScrollX);
                UpdateInfoPanelScrollBar(hwnd);
                InvalidateTexturePreviewOnly(hwnd);
            }
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
        case WM_ERASEBKGND:
            return 1;

        case WM_SIZE:
            ClampViewCenterToDocument(g_app.document.ComputeBounds());
            UpdateCanvasScrollBars(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE)
            {
                CancelCurrentTool();
                return 0;
            }
            if (g_app.insertMode == InsertMode::SetTeleportTarget && g_app.teleportTargetAwaitDirection)
            {
                if (wParam == VK_RETURN)
                {
                    CommitPendingTeleportTarget();
                    return 0;
                }
                if (wParam == VK_LEFT)
                {
                    RotatePendingTeleportTargetByDegrees(-5);
                    return 0;
                }
                if (wParam == VK_RIGHT)
                {
                    RotatePendingTeleportTargetByDegrees(5);
                    return 0;
                }
            }
            break;

        case WM_HSCROLL:
        {
            SCROLLINFO si{};
            si.cbSize = sizeof(SCROLLINFO);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_HORZ, &si);
            ScrollCanvasBy(hwnd, SB_HORZ, LOWORD(wParam), si.nTrackPos);
            return 0;
        }

        case WM_VSCROLL:
        {
            SCROLLINFO si{};
            si.cbSize = sizeof(SCROLLINFO);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);
            ScrollCanvasBy(hwnd, SB_VERT, LOWORD(wParam), si.nTrackPos);
            return 0;
        }

        case WM_LBUTTONDOWN:
        {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            const bool spaceDown = (GetKeyState(VK_SPACE) & 0x8000) != 0;
            if (spaceDown)
            {
                SetFocus(hwnd);
                SetCapture(hwnd);
                g_app.isPanning = true;
                g_app.isDrawing = false;
                g_app.drawWallAngleLock = false;
                g_app.drawWallLengthSnapLock = false;
                g_app.panStartClient = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                g_app.panStartCenterX = g_app.viewCenterX;
                g_app.panStartCenterZ = g_app.viewCenterZ;
                RefreshStatus();
                return 0;
            }

            if (g_app.insertMode == InsertMode::LinkEventToEnemyObject)
            {
                SetFocus(hwnd);
                const MonsterSpawnSelection targetSpawn = HitTestMonsterSpawn(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), rc);
                FinishLinkEventToEnemyObject(targetSpawn);
                return 0;
            }

            if (g_app.insertMode == InsertMode::DeleteLinkEventToEnemyObject)
            {
                SetFocus(hwnd);
                const MonsterSpawnSelection targetSpawn = HitTestMonsterSpawn(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), rc);
                FinishDeleteLinkEventToEnemyObject(targetSpawn);
                return 0;
            }

            if (g_app.insertMode == InsertMode::LinkEventToZone)
            {
                SetFocus(hwnd);
                const int targetZone = HitTestZone(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), rc);
                FinishLinkEventToZone(targetZone);
                return 0;
            }

            if (g_app.insertMode == InsertMode::LinkEventToSwitchTexture)
            {
                SetFocus(hwnd);
                const int targetZone = HitTestZone(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), rc);
                FinishLinkEventToSwitchTexture(targetZone);
                return 0;
            }

            if (g_app.insertMode == InsertMode::LinkEventToRotateClockwise ||
                g_app.insertMode == InsertMode::LinkEventToRotateCounterClockwise)
            {
                SetFocus(hwnd);
                const int targetZone = HitTestZone(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), rc);
                FinishLinkEventToRotate(targetZone);
                return 0;
            }

            if (g_app.insertMode == InsertMode::LinkEventToMoveWallGroup)
            {
                SetFocus(hwnd);
                const POINT world = ScreenToWorld(rc, g_app.document.ComputeBounds(), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                if (g_app.pendingMoveWallGroupTargetPlacementActive)
                {
                    CommitMoveWallGroupTargetPlacement(world);
                    return 0;
                }

                const int targetZone = HitTestZone(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), rc);
                if (BeginMoveWallGroupTargetPlacement(targetZone, world))
                {
                    SetCapture(hwnd);
                }
                return 0;
            }

            if (g_app.insertMode == InsertMode::SetTeleportTarget)
            {
                SetFocus(hwnd);
                POINT world = ScreenToWorld(rc, g_app.document.ComputeBounds(), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                FinishSetTeleportTarget(world);
                return 0;
            }

            if (g_app.insertMode == InsertMode::PlayerStart)
            {
                SetFocus(hwnd);
                POINT world = ScreenToWorld(rc, g_app.document.ComputeBounds(), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                SetPlayerStartAtWorld(world);
                return 0;
            }

            if (g_app.insertMode == InsertMode::ObjectSpawn)
            {
                SetFocus(hwnd);
                POINT world = ScreenToWorld(rc, g_app.document.ComputeBounds(), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                PlaceObjectAtWorld(world);
                return 0;
            }

            if (g_app.insertMode != InsertMode::None)
            {
                SetFocus(hwnd);
                SetCapture(hwnd);
                g_app.isDrawing = true;
                g_app.isPanning = false;
                g_app.drawWallAngleLock = (IsLineInsertMode(g_app.insertMode) && (GetKeyState(VK_SHIFT) & 0x8000) != 0);
                g_app.drawWallLengthSnapLock = false;
                const POINT rawStartWorld = ScreenToWorld(rc, g_app.document.ComputeBounds(), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                g_app.drawStartWorld = PrepareDrawStartPoint(rc, rawStartWorld, g_app.insertMode == InsertMode::Wall && !g_app.drawWallAngleLock);
                g_app.drawCurrentWorld = g_app.drawStartWorld;
                RefreshStatus();
                InvalidateEditorViews();
                return 0;
            }

            const TeleportSelection teleport = HitTestTeleportTarget(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), rc);
            if (teleport.IsSet())
            {
                g_app.selectedTeleportTarget = teleport;
                ClearSelectedMonsterSpawn();
                g_app.selectedZone = -1;
                g_app.previewTextureBand = 0;
                RefreshZoneList();
                RefreshPreviewImage();
                UpdateModeButtons();
                RefreshStatus();
                InvalidateEditorViews();
                return 0;
            }

            const MonsterSpawnSelection spawn = HitTestMonsterSpawn(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), rc);
            if (spawn.IsSet())
            {
                g_app.selectedMonsterSpawn = spawn;
                ClearSelectedTeleportTarget();
                g_app.selectedZone = -1;
                g_app.previewTextureBand = 0;
                BeginMonsterSpawnDrag(spawn, POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }, rc);
                SetFocus(hwnd);
                SetCapture(hwnd);
                RefreshZoneList();
                RefreshPreviewImage();
                UpdateModeButtons();
                RefreshStatus();
                InvalidateEditorViews();
                return 0;
            }

            const int zone = HitTestZone(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), rc);
            ClearSelectedMonsterSpawn();
            ClearSelectedTeleportTarget();
            g_app.selectedZone = zone;
            NormalizeSelectedWallToCanonical();
            if (g_app.selectedZone >= 0)
            {
                if (IsEventTriggerLineZone(g_app.document.zones[g_app.selectedZone]))
                {
                    SyncActiveSwitchTextureFromTriggerZone(g_app.selectedZone);
                }
                UpdatePreviewTextureBandFromPoint(rc, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
            RefreshZoneList();
            RefreshPreviewImage();
            UpdateModeButtons();
            RefreshStatus();
            InvalidateEditorViews();
            return 0;
        }
        case WM_RBUTTONDOWN:
        {
            SetFocus(hwnd);
            SetCapture(hwnd);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            const POINT world = ScreenToWorld(rc, g_app.document.ComputeBounds(), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            g_app.walkPreviewX = static_cast<double>(world.x);
            g_app.walkPreviewZ = static_cast<double>(world.y);
            // Right click places the preview camera. While the button stays
            // down, mouse gestures choose the nearest 45-degree direction directly:
            // up=0, up/right=45°, right=90°, ... left=270°.
            g_app.walkPreviewDir = NormalizeWalkPreviewDir(g_app.walkPreviewDir);
            g_app.walkPreviewInitialized = true;
            g_app.walkPreviewRightDrag = true;
            g_app.walkPreviewRightDragStartClient = POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            g_app.walkPreviewRightDragLastClient = g_app.walkPreviewRightDragStartClient;
            g_app.walkPreviewRightDragBaseDir = g_app.walkPreviewDir;
            InvalidateWalkPreview();
            RefreshStatus();
            return 0;
        }

        case WM_MOUSEMOVE:
            if (g_app.walkPreviewRightDrag)
            {
                const POINT current{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                const int dx = current.x - g_app.walkPreviewRightDragStartClient.x;
                const int dy = current.y - g_app.walkPreviewRightDragStartClient.y;
                const int absDx = std::abs(dx);
                const int absDy = std::abs(dy);

                if (MaxValue(absDx, absDy) >= kWalkPreviewDragDirectionPixels)
                {
                    const int newDir = WalkPreviewDirFromScreenDrag(dx, dy);
                    g_app.walkPreviewRightDragLastClient = current;
                    if (newDir != g_app.walkPreviewDir)
                    {
                        g_app.walkPreviewDir = newDir;
                        InvalidateWalkPreview();
                        RefreshStatus();
                    }
                }
                return 0;
            }
            if (g_app.pendingMoveWallGroupTargetPlacementActive)
            {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                const POINT world = ScreenToWorld(rc, g_app.document.ComputeBounds(), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                UpdateMoveWallGroupTargetPlacement(world);
                return 0;
            }
            if (g_app.monsterSpawnDragging)
            {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                UpdateMonsterSpawnDrag(POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }, rc);
                return 0;
            }
            if (g_app.isPanning)
            {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                const CanvasMetrics metrics = GetCanvasMetrics(rc, g_app.document.ComputeBounds());
                const int dx = GET_X_LPARAM(lParam) - g_app.panStartClient.x;
                const int dy = GET_Y_LPARAM(lParam) - g_app.panStartClient.y;
                g_app.viewCenterX = g_app.panStartCenterX - static_cast<double>(dx) / metrics.scale;
                g_app.viewCenterZ = g_app.panStartCenterZ + static_cast<double>(dy) / metrics.scale;
                g_app.viewInitialized = true;
                ClampViewCenterToDocument(g_app.document.ComputeBounds());
                UpdateCanvasScrollBars(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (g_app.isDrawing)
            {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                g_app.drawCurrentWorld = ScreenToWorld(rc, g_app.document.ComputeBounds(), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                if (g_app.insertMode == InsertMode::Wall)
                {
                    g_app.drawCurrentWorld = PrepareWallDrawEndpoint(rc, g_app.drawCurrentWorld);
                }
                else if (IsLineInsertMode(g_app.insertMode))
                {
                    g_app.drawCurrentWorld = PrepareLineZoneDrawEndpoint(g_app.drawCurrentWorld);
                }
                if (g_app.insertMode == InsertMode::Wall)
                {
                    InvalidateEditorViewsIncludingWalkPreview();
                }
                else
                {
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            break;
        case WM_RBUTTONUP:
            if (g_app.walkPreviewRightDrag)
            {
                g_app.walkPreviewRightDrag = false;
                if (GetCapture() == hwnd) ReleaseCapture();
                RefreshStatus();
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (g_app.monsterSpawnDragging)
            {
                FinishMonsterSpawnDrag();
                if (GetCapture() == hwnd) ReleaseCapture();
                return 0;
            }
            if (g_app.isPanning)
            {
                g_app.isPanning = false;
                if (GetCapture() == hwnd) ReleaseCapture();
                RefreshStatus();
                return 0;
            }
            if (g_app.isDrawing)
            {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                g_app.drawCurrentWorld = ScreenToWorld(rc, g_app.document.ComputeBounds(), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                if (g_app.insertMode == InsertMode::Wall)
                {
                    g_app.drawCurrentWorld = PrepareWallDrawEndpoint(rc, g_app.drawCurrentWorld);
                }
                else if (IsLineInsertMode(g_app.insertMode))
                {
                    g_app.drawCurrentWorld = PrepareLineZoneDrawEndpoint(g_app.drawCurrentWorld);
                }
                CommitDrawnZone();
                if (GetCapture() == hwnd) ReleaseCapture();
                UpdateCanvasScrollBars(hwnd);
                return 0;
            }
            break;
        case WM_CAPTURECHANGED:
            if (reinterpret_cast<HWND>(lParam) != hwnd)
            {
                if (g_app.pendingMoveWallGroupTargetPlacementActive)
                {
                    ResetPendingMoveWallGroup();
                    UpdateModeButtons();
                    RefreshStatus();
                    InvalidateEditorViews();
                }
                if (g_app.isDrawing)
                {
                    g_app.isDrawing = false;
                    g_app.drawWallAngleLock = false;
                    g_app.drawWallLengthSnapLock = false;
                    RefreshStatus();
                    InvalidateEditorViews();
                }
                if (g_app.isPanning)
                {
                    g_app.isPanning = false;
                    RefreshStatus();
                }
                if (g_app.monsterSpawnDragging)
                {
                    FinishMonsterSpawnDrag();
                }
                if (g_app.walkPreviewRightDrag)
                {
                    g_app.walkPreviewRightDrag = false;
                    RefreshStatus();
                }
            }
            break;
        case WM_LBUTTONDBLCLK:
            if (g_app.insertMode == InsertMode::None && !((GetKeyState(VK_SPACE) & 0x8000) != 0))
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
            const CanvasMetrics beforeMetrics = GetCanvasMetrics(rc, rawBounds);
            const double beforeWorldX = beforeMetrics.centerWorldX + (static_cast<double>(clientPt.x) - beforeMetrics.centerPixelX) / beforeMetrics.scale;
            const double beforeWorldZ = beforeMetrics.centerWorldZ - (static_cast<double>(clientPt.y) - beforeMetrics.centerPixelY) / beforeMetrics.scale;

            const short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (delta > 0)
            {
                g_app.canvasZoom = MinValue(kCanvasZoomMax, g_app.canvasZoom * 1.2);
            }
            else if (delta < 0)
            {
                g_app.canvasZoom = MaxValue(kCanvasZoomMin, g_app.canvasZoom / 1.2);
            }

            const CanvasMetrics afterMetrics = GetCanvasMetrics(rc, rawBounds);
            const double afterWorldX = afterMetrics.centerWorldX + (static_cast<double>(clientPt.x) - afterMetrics.centerPixelX) / afterMetrics.scale;
            const double afterWorldZ = afterMetrics.centerWorldZ - (static_cast<double>(clientPt.y) - afterMetrics.centerPixelY) / afterMetrics.scale;
            g_app.viewCenterX += beforeWorldX - afterWorldX;
            g_app.viewCenterZ += beforeWorldZ - afterWorldZ;
            g_app.viewInitialized = true;
            ClampViewCenterToDocument(rawBounds);
            UpdateCanvasScrollBars(hwnd);
            RefreshStatus();
            InvalidateRect(hwnd, nullptr, FALSE);
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
        g_app.zoneList = CreateWindowExW(0, L"LISTBOX", nullptr,
            WS_CHILD | LBS_NOTIFY | WS_VSCROLL,
            0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(IDC_ZONE_LIST), g_app.instance, nullptr);
        SendMessageW(g_app.zoneList, WM_SETFONT, reinterpret_cast<WPARAM>(NormalEditorListFont()), TRUE);

        g_app.canvas = CreateWindowExW(0, kCanvasClass, nullptr,
            WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL | WS_CLIPSIBLINGS,
            0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(IDC_CANVAS), g_app.instance, nullptr);

        g_app.infoPanel = CreateWindowExW(0, kInfoPanelClass, nullptr,
            WS_CHILD | WS_VISIBLE,
            0, 0, 100, 100, hwnd, nullptr, g_app.instance, nullptr);

        g_app.btnAddWall = CreateWindowW(L"BUTTON", L"Draw Wall", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_ADD_WALL), g_app.instance, nullptr);
        g_app.btnAddMonster = CreateWindowW(L"BUTTON", L"Draw Monster Zone", WS_CHILD | BS_OWNERDRAW,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_ADD_MONSTER), g_app.instance, nullptr);
        g_app.btnAddTrigger = CreateWindowW(L"BUTTON", L"Draw Event Zone", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_ADD_TRIGGER), g_app.instance, nullptr);
        g_app.btnPlaceEnemy = CreateWindowW(L"BUTTON", L"Place Enemy", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_PLACE_ENEMY), g_app.instance, nullptr);
        g_app.btnPlaceWeapon = CreateWindowW(L"BUTTON", L"Place Weapon", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_PLACE_WEAPON), g_app.instance, nullptr);
        g_app.btnPlacePickup = CreateWindowW(L"BUTTON", L"Place Pickup", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_PLACE_PICKUP), g_app.instance, nullptr);
        g_app.btnEdit = CreateWindowW(L"BUTTON", L"Edit Zone", WS_CHILD | BS_OWNERDRAW,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_EDIT_ZONE), g_app.instance, nullptr);
        g_app.btnDelete = CreateWindowW(L"BUTTON", L"Delete Zone", WS_CHILD | BS_OWNERDRAW,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_DELETE_ZONE), g_app.instance, nullptr);
        g_app.btnUp = CreateWindowW(L"BUTTON", L"Move Up", WS_CHILD | BS_OWNERDRAW,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_MOVE_UP), g_app.instance, nullptr);
        g_app.btnDown = CreateWindowW(L"BUTTON", L"Move Down", WS_CHILD | BS_OWNERDRAW,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_MOVE_DOWN), g_app.instance, nullptr);
        g_app.btnEvents = CreateWindowW(L"BUTTON", L"Toggle Event Links Overlay", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_EVENTS), g_app.instance, nullptr);
        g_app.btnTextures = CreateWindowW(L"BUTTON", L"Texture Slots", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_TEXTURES), g_app.instance, nullptr);
        g_app.btnPlayerStart = CreateWindowW(L"BUTTON", L"Set Player Start", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_PLAYER_START), g_app.instance, nullptr);
        g_app.btnLevelEnd = CreateWindowW(L"BUTTON", L"Set Level End", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_LEVEL_END), g_app.instance, nullptr);
        g_app.btnLinkEvent = CreateWindowW(L"BUTTON", L"Link Event > Wall/Door", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_LINK_EVENT), g_app.instance, nullptr);
        g_app.btnLinkSwitchTexture = CreateWindowW(L"BUTTON", L"Link Event > Switch/Trigger", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_LINK_SWITCH_TEXTURE), g_app.instance, nullptr);
        g_app.btnLinkEnemyObjects = CreateWindowW(L"BUTTON", L"Link Event > Enemy/Objects", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCmdLinkEnemyObjects)), g_app.instance, nullptr);
        g_app.btnLinkRotateCW = CreateWindowW(L"BUTTON", L"Link Event > Rotate CW", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_LINK_ROTATE_CW), g_app.instance, nullptr);
        g_app.btnLinkRotateCCW = CreateWindowW(L"BUTTON", L"Link Event > Rotate CCW", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_LINK_ROTATE_CCW), g_app.instance, nullptr);
        g_app.btnLinkMoveWallGroup = CreateWindowW(L"BUTTON", L"Link Event > Move Wallblock", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCmdLinkMoveWallGroup)), g_app.instance, nullptr);
        g_app.btnDeleteLinkEvent = CreateWindowW(L"BUTTON", L"Delete Link Event", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_DELETE_LINK_EVENT), g_app.instance, nullptr);
        g_app.btnSetTeleportTarget = CreateWindowW(L"BUTTON", L"Set Teleport Target", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_SET_TELEPORT_TARGET), g_app.instance, nullptr);
        g_app.btnFlipDoorDirection = CreateWindowW(L"BUTTON", L"Flip Door Direction", WS_CHILD | BS_OWNERDRAW,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_FLIP_DOOR_DIRECTION), g_app.instance, nullptr);
        g_app.btnValidateMap = CreateWindowW(L"BUTTON", L"Validate Map", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_BTN_VALIDATE_MAP), g_app.instance, nullptr);
        g_app.btnToolbarScrollUp = CreateWindowW(L"BUTTON", L"", WS_CHILD | BS_OWNERDRAW,
            0, 0, 100, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCmdToolbarScrollUp)), g_app.instance, nullptr);
        g_app.btnToolbarScrollDown = CreateWindowW(L"BUTTON", L"", WS_CHILD | BS_OWNERDRAW,
            0, 0, 100, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCmdToolbarScrollDown)), g_app.instance, nullptr);
        g_app.lineValidateTop = CreateWindowW(L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            0, 0, 10, 2, hwnd, nullptr, g_app.instance, nullptr);

        auto createLine = [hwnd]() -> HWND {
            return CreateWindowW(L"STATIC", nullptr, WS_CHILD,
                0, 0, 10, 2, hwnd, nullptr, g_app.instance, nullptr);
        };
        auto createLabel = [hwnd](const wchar_t* text) -> HWND {
            return CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_CENTER,
                0, 0, 80, 18, hwnd, nullptr, g_app.instance, nullptr);
        };
        g_app.lineZonesLeft = createLine();
        g_app.labelZones = createLabel(L"GLOBAL");
        g_app.lineZonesRight = createLine();
        g_app.lineObjectsLeft = createLine();
        g_app.labelObjects = createLabel(L"ZONES");
        g_app.lineObjectsRight = createLine();
        g_app.lineEventLinksLeft = createLine();
        g_app.labelEventLinks = createLabel(L"EVENTLINKS");
        g_app.lineEventLinksRight = createLine();
        g_app.lineOrderLeft = createLine();
        g_app.labelOrder = createLabel(L"OBJECTS");
        g_app.lineOrderRight = createLine();
        g_app.lineMapMarkersLeft = createLine();
        g_app.labelMapMarkers = createLabel(L"MAP MARKERS");
        g_app.lineMapMarkersRight = createLine();

        g_app.statusBar = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr,
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
            0, 0, 0, 0, hwnd, nullptr, g_app.instance, nullptr);
        if (g_app.statusBar)
        {
            g_app.statusBarOldProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_app.statusBar, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(DarkStatusBarProc)));
        }

        ApplyEditorDarkModeToWindowTree(hwnd);
        UpdateModeButtons();
    }


    constexpr int kAboutLinkControlId = 0x7E01;

    struct AboutDialogFonts
    {
        HFONT titleFont = nullptr;
        HFONT linkFont = nullptr;
    };

    LRESULT CALLBACK AboutWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_CREATE:
            {
                EnableEditorDarkModeForWindow(hwnd);
                HFONT defaultFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

                auto* fonts = new AboutDialogFonts();
                LOGFONTW lf{};
                if (defaultFont && GetObjectW(defaultFont, sizeof(lf), &lf) == sizeof(lf))
                {
                    LOGFONTW titleLf = lf;
                    titleLf.lfWeight = FW_SEMIBOLD;
                    titleLf.lfHeight = lf.lfHeight != 0 ? lf.lfHeight - 2 : -15;
                    fonts->titleFont = CreateFontIndirectW(&titleLf);

                    LOGFONTW linkLf = lf;
                    linkLf.lfUnderline = TRUE;
                    fonts->linkFont = CreateFontIndirectW(&linkLf);
                }
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(fonts));

                HWND icon = CreateWindowW(L"STATIC", nullptr,
                    WS_CHILD | WS_VISIBLE | SS_ICON,
                    22, 18, 48, 48, hwnd, nullptr, g_app.instance, nullptr);
                HICON appIcon = static_cast<HICON>(LoadImageW(g_app.instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR));
                if (!appIcon)
                {
                    appIcon = LoadIconW(g_app.instance, MAKEINTRESOURCEW(IDI_APP_ICON));
                }
                SendMessageW(icon, STM_SETICON, reinterpret_cast<WPARAM>(appIcon), 0);

                HWND title = CreateWindowW(L"STATIC", L"ZGLOOM Level Editor",
                    WS_CHILD | WS_VISIBLE,
                    86, 20, 420, 24, hwnd, nullptr, g_app.instance, nullptr);
                HWND subtitle = CreateWindowW(L"STATIC", L"Standalone Win32 Editor for Gloom maps",
                    WS_CHILD | WS_VISIBLE,
                    86, 46, 420, 20, hwnd, nullptr, g_app.instance, nullptr);

                HWND sep1 = CreateWindowW(L"STATIC", nullptr,
                    WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                    22, 84, 494, 2, hwnd, nullptr, g_app.instance, nullptr);

                HWND body = CreateWindowW(L"STATIC",
                    L"This editor allows you to edit and create level maps for Gloom, Gloom Deluxe, and related games. "
                    L"The level editor was built to the best of our knowledge and belief, with a focus on user-friendliness and intuitive operation.",
                    WS_CHILD | WS_VISIBLE,
                    22, 104, 494, 48, hwnd, nullptr, g_app.instance, nullptr);

                HWND sep2 = CreateWindowW(L"STATIC", nullptr,
                    WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                    22, 158, 494, 2, hwnd, nullptr, g_app.instance, nullptr);

                HWND credit = CreateWindowW(L"STATIC", L"Design, UI and coding by A. St\u00FCrmer / @andiweli",
                    WS_CHILD | WS_VISIBLE,
                    22, 178, 494, 20, hwnd, nullptr, g_app.instance, nullptr);
                HWND projects = CreateWindowW(L"STATIC", L"More projects on",
                    WS_CHILD | WS_VISIBLE,
                    22, 202, 92, 20, hwnd, nullptr, g_app.instance, nullptr);
                HWND link = CreateWindowW(L"STATIC", L"andiweli.github.io",
                    WS_CHILD | WS_VISIBLE | SS_NOTIFY,
                    116, 202, 180, 20, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAboutLinkControlId)), g_app.instance, nullptr);
                HWND ok = CreateWindowW(L"BUTTON", L"OK",
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    434, 220, 82, 26, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), g_app.instance, nullptr);

                SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(fonts->titleFont ? fonts->titleFont : defaultFont), TRUE);
                SendMessageW(subtitle, WM_SETFONT, reinterpret_cast<WPARAM>(defaultFont), TRUE);
                SendMessageW(body, WM_SETFONT, reinterpret_cast<WPARAM>(defaultFont), TRUE);
                SendMessageW(credit, WM_SETFONT, reinterpret_cast<WPARAM>(defaultFont), TRUE);
                SendMessageW(projects, WM_SETFONT, reinterpret_cast<WPARAM>(defaultFont), TRUE);
                SendMessageW(link, WM_SETFONT, reinterpret_cast<WPARAM>(fonts->linkFont ? fonts->linkFont : defaultFont), TRUE);
                SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(defaultFont), TRUE);

                // Position the project link after exactly one rendered space. Fixed
                // control widths left a visibly too-wide gap on some Windows font metrics.
                HDC aboutDc = GetDC(hwnd);
                if (aboutDc)
                {
                    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(aboutDc, defaultFont));
                    SIZE prefixSize{};
                    SIZE spaceSize{};
                    GetTextExtentPoint32W(aboutDc, L"More projects on", 16, &prefixSize);
                    GetTextExtentPoint32W(aboutDc, L" ", 1, &spaceSize);
                    if (oldFont) SelectObject(aboutDc, oldFont);
                    ReleaseDC(hwnd, aboutDc);

                    const int projectX = 22;
                    const int projectY = 202;
                    MoveWindow(projects, projectX, projectY, MaxValue(1, prefixSize.cx), 20, TRUE);
                    MoveWindow(link, projectX + prefixSize.cx + MaxValue(1, spaceSize.cx), projectY, 180, 20, TRUE);
                }

                SetFocus(ok);
                return 0;
            }

        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wParam) == kAboutLinkControlId)
            {
                ShellExecuteW(hwnd, L"open", L"https://andiweli.github.io", nullptr, nullptr, SW_SHOWNORMAL);
                return 0;
            }
            break;

        case WM_DRAWITEM:
            return DrawDarkOwnerDrawControl(reinterpret_cast<DRAWITEMSTRUCT*>(lParam)) ? TRUE : 0;

        case WM_CTLCOLORSTATIC:
            {
                HDC dc = reinterpret_cast<HDC>(wParam);
                HWND child = reinterpret_cast<HWND>(lParam);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, GetDlgCtrlID(child) == kAboutLinkControlId ? kDarkLinkText : kDarkText);
                return reinterpret_cast<LRESULT>(DarkWindowBrush());
            }
        case WM_CTLCOLORBTN:
            return HandleDarkCtlColor(msg, wParam);

        case WM_SETCURSOR:
            if (reinterpret_cast<HWND>(wParam) == GetDlgItem(hwnd, kAboutLinkControlId))
            {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            {
                auto* fonts = reinterpret_cast<AboutDialogFonts*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                if (fonts)
                {
                    if (fonts->titleFont) DeleteObject(fonts->titleFont);
                    if (fonts->linkFont) DeleteObject(fonts->linkFont);
                    delete fonts;
                    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                }
                return 0;
            }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void ShowAboutDialog(HWND owner)
    {
        const wchar_t* className = L"ZGloomEditorAboutWindow";
        static bool registered = false;
        if (!registered)
        {
            WNDCLASSW wc{};
            wc.lpfnWndProc = AboutWndProc;
            wc.hInstance = g_app.instance;
            wc.hIcon = LoadIconW(g_app.instance, MAKEINTRESOURCEW(IDI_APP_ICON));
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = DarkWindowBrush();
            wc.lpszClassName = className;
            RegisterClassW(&wc);
            registered = true;
        }

        RECT ownerRect{};
        GetWindowRect(owner, &ownerRect);
        const int width = 550;
        const int height = 308;
        const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
        const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;

        HWND dialog = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
            className,
            L"Information",
            WS_CAPTION | WS_SYSMENU | WS_POPUP,
            x, y, width, height,
            owner, nullptr, g_app.instance, nullptr);
        if (!dialog)
        {
            MessageBoxW(owner,
                L"ZGLOOM Level Editor\nStandalone Win32 Editor for Gloom maps\n\nThis editor allows you to edit and create level maps for Gloom, Gloom Deluxe, and related games. The level editor was built to the best of our knowledge and belief, with a focus on user-friendliness and intuitive operation.\n\nDesign, UI and coding by A. St\u00FCrmer / @andiweli\nMore projects on https://andiweli.github.io",
                L"Information", MB_OK | MB_ICONINFORMATION);
            return;
        }

        ApplyEditorDarkModeToWindowTree(dialog);
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

    LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_CREATE:
            EnableEditorDarkModeForWindow(hwnd);
            SetMenu(hwnd, BuildMainMenu());
            ApplyDarkMenuHints(GetMenu(hwnd));
            DrawMenuBar(hwnd);
            CreateChildWindows(hwnd);
            ApplyEditorDarkModeToWindowTree(hwnd);
            LayoutChildren(hwnd);
            FitViewToDocument();
            return 0;

        case WM_SIZE:
            LayoutChildren(hwnd);
            return 0;

        case WM_ERASEBKGND:
        {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, DarkWindowBrush());
            return 1;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORSCROLLBAR:
            return HandleDarkCtlColor(msg, wParam);

        case WM_MEASUREITEM:
        {
            auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (measure && measure->CtlType == ODT_MENU)
            {
                MeasureOwnerDrawMenuItem(measure);
                return TRUE;
            }
            break;
        }

        case WM_DRAWITEM:
        {
            auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (draw && draw->CtlType == ODT_MENU)
            {
                DrawOwnerDrawMenuItem(draw);
                return TRUE;
            }
            if (DrawDarkOwnerDrawControl(draw))
            {
                return TRUE;
            }
            break;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) >= IDM_FILE_RECENT_FIRST && LOWORD(wParam) <= IDM_FILE_RECENT_LAST)
            {
                OpenRecentDocument(static_cast<int>(LOWORD(wParam) - IDM_FILE_RECENT_FIRST));
                return 0;
            }
            if (CampaignEditor::HandleCommand(hwnd, LOWORD(wParam)))
            {
                UpdateRecentCampaignGameRootsMenu();
                return 0;
            }
            switch (LOWORD(wParam))
            {
            case IDC_ZONE_LIST:
                if (HIWORD(wParam) == LBN_SELCHANGE)
                {
                    ClearSelectedMonsterSpawn();
                    g_app.selectedZone = static_cast<int>(SendMessageW(g_app.zoneList, LB_GETCURSEL, 0, 0));
                    NormalizeSelectedWallToCanonical();
                    g_app.previewTextureBand = 0;
                    RefreshPreviewImage();
                    UpdateModeButtons();
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
            case IDM_FILE_CLOSE: CloseDocument(); return 0;
            case IDM_FILE_SAVE: SaveDocument(false); return 0;
            case IDM_FILE_SAVE_AS: SaveDocument(true); return 0;
            case IDM_FILE_EXPORT_SVG: ExportSvg(); return 0;
            case IDM_FILE_EXIT: PostMessageW(hwnd, WM_CLOSE, 0, 0); return 0;
            case IDM_EDIT_UNDO: UndoLastChange(); return 0;
            case IDM_EDIT_ZONE:
            case IDC_BTN_EDIT_ZONE: EditSelectedZone(); return 0;
            case IDM_EDIT_LINK_EVENT:
            case IDC_BTN_LINK_EVENT: StartLinkEventToZoneTool(); return 0;
            case IDM_EDIT_LINK_SWITCH_TEXTURE:
            case IDC_BTN_LINK_SWITCH_TEXTURE: StartLinkEventToSwitchTextureTool(); return 0;
            case kCmdLinkEnemyObjects: StartLinkEventToEnemyObjectTool(); return 0;
            case IDM_EDIT_LINK_ROTATE_CW:
            case IDC_BTN_LINK_ROTATE_CW: StartLinkEventToRotateTool(true); return 0;
            case IDM_EDIT_LINK_ROTATE_CCW:
            case IDC_BTN_LINK_ROTATE_CCW: StartLinkEventToRotateTool(false); return 0;
            case kCmdLinkMoveWallGroup: StartLinkEventToMoveWallGroupTool(); return 0;
            case IDC_BTN_DELETE_LINK_EVENT: StartDeleteLinkEventTool(); return 0;
            case IDM_EDIT_SET_TELEPORT_TARGET:
            case IDC_BTN_SET_TELEPORT_TARGET: StartSetTeleportTargetTool(); return 0;
            case IDM_EDIT_DELETE:
            case IDC_BTN_DELETE_ZONE: DeleteSelectedItem(); return 0;
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
            case IDM_INSERT_PLAYER_START:
            case IDC_BTN_PLAYER_START: SetInsertMode(InsertMode::PlayerStart); return 0;
            case IDM_INSERT_LEVEL_END:
            case IDC_BTN_LEVEL_END: SetInsertMode(InsertMode::LevelEnd); return 0;
            case IDC_BTN_PLACE_ENEMY: ShowObjectPlacementMenu(ObjectPlacementGroup::Enemy, g_app.btnPlaceEnemy); return 0;
            case IDC_BTN_PLACE_WEAPON: ShowObjectPlacementMenu(ObjectPlacementGroup::Weapon, g_app.btnPlaceWeapon); return 0;
            case IDC_BTN_PLACE_PICKUP: ShowObjectPlacementMenu(ObjectPlacementGroup::Pickup, g_app.btnPlacePickup); return 0;
            case IDM_MAP_EVENTS: ShowEventEditor(); return 0;
            case IDM_MAP_TEXTURES:
            case IDC_BTN_TEXTURES: ShowTextureDialog(); return 0;
            case IDM_TOOLS_VALIDATE:
            case IDC_BTN_VALIDATE_MAP:
                if (!HasMapContentForValidation())
                {
                    MessageBoxW(hwnd, L"There is nothing on the map yet. Add at least one zone or object before validating.", L"Validate Map", MB_OK | MB_ICONINFORMATION);
                    return 0;
                }
                ShowValidationReport();
                return 0;
            case kCmdToolbarScrollUp:
                if (g_app.toolbarScrollMode)
                {
                    g_app.toolbarScrollY = ClampValue(g_app.toolbarScrollY - 48, 0, g_app.toolbarScrollMaxY);
                    LayoutChildren(hwnd);
                }
                return 0;
            case kCmdToolbarScrollDown:
                if (g_app.toolbarScrollMode)
                {
                    g_app.toolbarScrollY = ClampValue(g_app.toolbarScrollY + 48, 0, g_app.toolbarScrollMaxY);
                    LayoutChildren(hwnd);
                }
                return 0;
            case kCmdViewEventGraphOverlay:
            case IDC_BTN_EVENTS:
                g_app.showEventGraphOverlay = !g_app.showEventGraphOverlay;
                RefreshStatus();
                InvalidateEditorViews();
                return 0;
            case IDM_TOOLS_SETTINGS: ShowEditorSettingsDialog(); return 0;
            case IDM_HELP_ABOUT:
                ShowAboutDialog(hwnd);
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
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icc);
    EnableEditorDarkModeForApp();

    g_app.instance = instance;
    LoadEditorSettings();
    g_app.document.NewBlank();
    RefreshGameProfile();
    g_app.walkPreviewInitialized = false;
    LoadRecentFiles();

    WNDCLASSEXW mainClass{};
    mainClass.cbSize = sizeof(mainClass);
    mainClass.hInstance = instance;
    mainClass.lpszClassName = kMainWindowClass;
    mainClass.lpfnWndProc = MainWndProc;
    mainClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    mainClass.hIcon = LoadIconW(g_app.instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    mainClass.hIconSm = static_cast<HICON>(LoadImageW(g_app.instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    mainClass.hbrBackground = DarkWindowBrush();
    RegisterClassExW(&mainClass);

    WNDCLASSEXW canvasClass{};
    canvasClass.cbSize = sizeof(canvasClass);
    canvasClass.hInstance = instance;
    canvasClass.lpszClassName = kCanvasClass;
    canvasClass.lpfnWndProc = CanvasProc;
    canvasClass.hCursor = LoadCursor(nullptr, IDC_CROSS);
    canvasClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    canvasClass.style = CS_DBLCLKS;
    RegisterClassExW(&canvasClass);

    WNDCLASSEXW infoClass{};
    infoClass.cbSize = sizeof(infoClass);
    infoClass.hInstance = instance;
    infoClass.lpszClassName = kInfoPanelClass;
    infoClass.lpfnWndProc = InfoPanelProc;
    infoClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    infoClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    RegisterClassExW(&infoClass);

    g_app.mainWindow = CreateWindowExW(
        0,
        kMainWindowClass,
        L"ZGloom Editor",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 820,
        nullptr, nullptr, instance, nullptr);

    if (!g_app.mainWindow)
    {
        return 0;
    }

    ApplyEditorDarkModeToWindowTree(g_app.mainWindow);
    UpdateTitle();
    UNREFERENCED_PARAMETER(showCmd);

    HICON appIconLarge = LoadIconW(g_app.instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    HICON appIconSmall = static_cast<HICON>(LoadImageW(g_app.instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    if (appIconLarge) SendMessageW(g_app.mainWindow, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(appIconLarge));
    if (appIconSmall) SendMessageW(g_app.mainWindow, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(appIconSmall));

    ShowWindow(g_app.mainWindow, SW_SHOWMAXIMIZED);
    UpdateWindow(g_app.mainWindow);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (IsEditorShortcutMessage(msg))
        {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
