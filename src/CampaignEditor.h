#pragma once

#include <windows.h>

namespace CampaignEditor
{
    // Phase 1 campaign/game editor entry points.
    // Menu entries are created by EditorApp.cpp so they match the existing dark menu style.
    bool HandleCommand(HWND owner, unsigned int commandId);
}
