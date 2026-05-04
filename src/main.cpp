#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <wincodec.h>
#include <dwmapi.h>
#include <urlmon.h>
#include <commctrl.h>
#include "resource.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
enum class ActionType { None, Open, Command, Settings, Keys };

struct Action {
    ActionType type = ActionType::None;
    std::wstring target;
    std::wstring args;
};

struct ButtonConfig {
    std::wstring title;
    std::wstring imagePath;
    std::wstring text;
    Action action;
};

struct AppConfig {
    int rows = 3;
    int cols = 5;
    int buttonSize = 96;
    int gap = 10;
    int windowX = CW_USEDEFAULT;
    int windowY = CW_USEDEFAULT;
    bool alwaysOnTop = true;
    bool showTrayIcon = false;
    int keyboardLayout = 106;
    std::vector<int> pageOrder;
    std::map<int, std::wstring> pageNames;
    std::map<int, std::vector<ButtonConfig>> pages;
};
struct CachedIcon {
    HBITMAP bitmap = nullptr;
    HICON icon = nullptr;
    void Clear() {
        if (bitmap) { DeleteObject(bitmap); bitmap = nullptr; }
        if (icon) { DestroyIcon(icon); icon = nullptr; }
    }
};

struct AppState {
    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
    HFONT uiFont = nullptr;
    HFONT keyFont = nullptr;
    AppConfig config;
    int currentPage = 0;
    std::wstring configPath;
    HWND tooltipHwnd = nullptr;
    int lastHoveredButton = -1;
    std::vector<CachedIcon> currentIcons;
    bool altTabActive = false;
};

static AppState g;

static void EnableDpiAwareness() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    auto setDpiAwarenessContext = reinterpret_cast<BOOL(WINAPI*)(HANDLE)>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setDpiAwarenessContext) setDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

static void InitUiFont() {
    LOGFONTW lf{};
    HDC hdc = GetDC(nullptr);
    lf.lfHeight = -MulDiv(10, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(nullptr, hdc);
    lf.lfWeight = FW_NORMAL;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    g.uiFont = CreateFontIndirectW(&lf);

    HDC keyHdc = GetDC(nullptr);
    lf.lfHeight = -MulDiv(9, GetDeviceCaps(keyHdc, LOGPIXELSY), 72);
    ReleaseDC(nullptr, keyHdc);
    g.keyFont = CreateFontIndirectW(&lf);
}

static constexpr int IDM_PAGE_PREV = 1001;
static constexpr int IDM_PAGE_NEXT = 1002;
static constexpr int IDM_SETTINGS = 1003;
static constexpr int IDM_ALWAYS_ON_TOP = 1004;
static constexpr int IDM_EXIT = 1005;
static constexpr int IDM_PAGE_SETTINGS = 1006;
static constexpr int IDM_RESTORE = 1007;
static constexpr int IDM_DELETE_PAGE = 1008;
static constexpr int IDM_ADD_PAGE = 1009;
static constexpr int IDM_EDIT_BASE = 2000;
static constexpr int IDM_CLEAR_BASE = 2400;
static constexpr UINT WM_TRAYICON = WM_APP + 1;

static constexpr int HEADER_HEIGHT = 40;
static constexpr int HEADER_BUTTON_SIZE = 30;
static constexpr int HEADER_BUTTON_GAP = 8;
static constexpr BYTE WINDOW_OPACITY = 232;
static constexpr COLORREF DIALOG_BG = RGB(30, 34, 40);
static constexpr COLORREF DIALOG_TEXT = RGB(238, 242, 247);
static constexpr COLORREF DIALOG_FIELD_BG = RGB(42, 48, 57);

static HBRUSH DialogBrush() {
    static HBRUSH brush = CreateSolidBrush(DIALOG_BG);
    return brush;
}

static HBRUSH DialogFieldBrush() {
    static HBRUSH brush = CreateSolidBrush(DIALOG_FIELD_BG);
    return brush;
}

static constexpr int IDC_TITLE = 3001;
static constexpr int IDC_TEXT = 3002;
static constexpr int IDC_IMAGE = 3003;
static constexpr int IDC_BROWSE = 3004;
static constexpr int IDC_ACTION = 3005;
static constexpr int IDC_TARGET = 3006;
static constexpr int IDC_ARGS = 3007;
static constexpr int IDC_TARGET_BROWSE = 3008;
static constexpr int IDC_URL_IMPORT = 3009;
static constexpr int IDC_TARGET_LABEL = 3010;
static constexpr int IDC_ARGS_LABEL = 3011;
static constexpr int IDC_ROWS = 3101;
static constexpr int IDC_COLS = 3102;
static constexpr int IDC_BUTTON_SIZE = 3103;
static constexpr int IDC_GAP = 3104;
static constexpr int IDC_TOPMOST = 3105;
static constexpr int IDC_PAGE_NAME = 3106;
static constexpr int IDC_TRAY_ICON = 3107;
static constexpr int IDC_DELETE_PAGE = 3108;
static constexpr int IDC_CLEAR_BUTTON = 3109;
static constexpr int IDC_RUN_ON_STARTUP = 3110;
static constexpr int IDC_ADD_PAGE = 3111;
static constexpr int IDC_PAGE_LIST = 3112;
static constexpr int IDC_PAGE_MOVE_UP = 3113;
static constexpr int IDC_PAGE_MOVE_DOWN = 3114;
static constexpr int IDC_KEYBOARD_LAYOUT = 3115;
static constexpr int IDC_KEY_MODE = 3201;
static constexpr int IDC_KEY_SPEC = 3202;
static constexpr int IDC_KEY_CTRL = 3203;
static constexpr int IDC_KEY_ALT = 3204;
static constexpr int IDC_KEY_SHIFT = 3205;
static constexpr int IDC_KEY_WIN = 3206;
static constexpr int IDC_KEY_CLEAR = 3207;
static constexpr int IDC_KEY_LAYOUT = 3208;
static constexpr int IDC_KEY_ALTTAB_NEXT = 3209;
static constexpr int IDC_KEY_ALTTAB_PREV = 3210;
static constexpr int IDC_KEY_BUTTON_BASE = 6000;
static constexpr UINT_PTR IDT_ALT_TAB_RELEASE = 4101;

static std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(len, L'\0');
    if (len > 0) MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), len);
    return result;
}

static std::wstring Trim(const std::wstring& s) {
    const wchar_t* ws = L" \t\r\n";
    const size_t start = s.find_first_not_of(ws);
    if (start == std::wstring::npos) return L"";
    const size_t end = s.find_last_not_of(ws);
    return s.substr(start, end - start + 1);
}

static std::vector<int> ParsePageOrder(const std::wstring& value) {
    std::vector<int> order;
    std::wstringstream ss(value);
    std::wstring token;
    while (std::getline(ss, token, L',')) {
        int page = _wtoi(Trim(token).c_str());
        if (page >= 0 && std::find(order.begin(), order.end(), page) == order.end()) {
            order.push_back(page);
        }
    }
    return order;
}

static std::wstring JoinPageOrder(const std::vector<int>& order) {
    std::wstring value;
    for (int page : order) {
        if (!value.empty()) value += L",";
        value += std::to_wstring(page);
    }
    return value;
}

static std::wstring ActionToString(ActionType type) {
    switch (type) {
    case ActionType::Open: return L"Open";
    case ActionType::Command: return L"Command";
    case ActionType::Settings: return L"Settings";
    case ActionType::Keys: return L"Keys";
    default: return L"None";
    }
}

static ActionType ActionFromString(const std::wstring& value) {
    if (_wcsicmp(value.c_str(), L"Open") == 0) return ActionType::Open;
    if (_wcsicmp(value.c_str(), L"Command") == 0) return ActionType::Command;
    if (_wcsicmp(value.c_str(), L"Settings") == 0) return ActionType::Settings;
    if (_wcsicmp(value.c_str(), L"Keys") == 0) return ActionType::Keys;
    return ActionType::None;
}

static std::wstring GetConfigPath() {
    wchar_t buffer[MAX_PATH]{};
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    std::wstring base = len ? std::wstring(buffer, len) : L".";
    std::wstring dir = base + L"\\LauncherWidget";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\config.ini";
}

static std::wstring GetEnvPath(const wchar_t* name) {
    wchar_t buffer[MAX_PATH]{};
    DWORD len = GetEnvironmentVariableW(name, buffer, MAX_PATH);
    return len ? std::wstring(buffer, len) : L"";
}

static std::vector<ButtonConfig>& CurrentButtons() {
    auto& buttons = g.config.pages[g.currentPage];
    const int count = g.config.rows * g.config.cols;
    if (static_cast<int>(buttons.size()) < count) buttons.resize(count);
    return buttons;
}

static std::wstring DefaultPageName(int page) {
    return L"Page " + std::to_wstring(page + 1);
}

static std::wstring PageName(int page) {
    auto it = g.config.pageNames.find(page);
    if (it != g.config.pageNames.end()) {
        std::wstring name = Trim(it->second);
        if (!name.empty()) return name;
    }
    return DefaultPageName(page);
}

static std::wstring FileNameFromPath(std::wstring path, bool removeExtension) {
    while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) {
        path.pop_back();
    }
    if (path.empty()) return L"";

    wchar_t name[MAX_PATH]{};
    wcscpy_s(name, PathFindFileNameW(path.c_str()));
    if (removeExtension) PathRemoveExtensionW(name);
    return name;
}

static void EnsureDefaults() {
    auto& buttons = g.config.pages[0];
    if (!buttons.empty()) return;
    buttons.resize(g.config.rows * g.config.cols);
    buttons[0].title = L"Explorer";
    buttons[0].text = L"EX";
    buttons[0].action.type = ActionType::Open;
    buttons[0].action.target = L"explorer.exe";
    buttons[1].title = L"Settings";
    buttons[1].text = L"SET";
    buttons[1].action.type = ActionType::Settings;
    buttons[1].action.target = L"ms-settings:";
}

static void NormalizePageOrder() {
    std::vector<int> actualPages;
    for (const auto& pagePair : g.config.pages) {
        if (pagePair.first >= 0) actualPages.push_back(pagePair.first);
    }
    if (actualPages.empty()) actualPages.push_back(0);
    std::sort(actualPages.begin(), actualPages.end());
    actualPages.erase(std::unique(actualPages.begin(), actualPages.end()), actualPages.end());

    std::vector<int> order;
    for (int page : g.config.pageOrder) {
        if (std::binary_search(actualPages.begin(), actualPages.end(), page) &&
            std::find(order.begin(), order.end(), page) == order.end()) {
            order.push_back(page);
        }
    }
    for (int page : actualPages) {
        if (std::find(order.begin(), order.end(), page) == order.end()) {
            order.push_back(page);
        }
    }
    g.config.pageOrder = order;
}

static void LoadConfig() {
    g.configPath = GetConfigPath();
    wchar_t sections[32768]{};
    GetPrivateProfileSectionNamesW(sections, 32768, g.configPath.c_str());

    g.config.rows = GetPrivateProfileIntW(L"Window", L"Rows", 3, g.configPath.c_str());
    g.config.cols = GetPrivateProfileIntW(L"Window", L"Cols", 5, g.configPath.c_str());
    g.config.buttonSize = GetPrivateProfileIntW(L"Window", L"ButtonSize", 96, g.configPath.c_str());
    g.config.gap = GetPrivateProfileIntW(L"Window", L"Gap", 10, g.configPath.c_str());
    g.config.windowX = GetPrivateProfileIntW(L"Window", L"X", CW_USEDEFAULT, g.configPath.c_str());
    g.config.windowY = GetPrivateProfileIntW(L"Window", L"Y", CW_USEDEFAULT, g.configPath.c_str());
    g.config.alwaysOnTop = GetPrivateProfileIntW(L"Window", L"AlwaysOnTop", 1, g.configPath.c_str()) != 0;
    g.config.showTrayIcon = GetPrivateProfileIntW(L"Window", L"ShowTrayIcon", 0, g.configPath.c_str()) != 0;
    g.config.keyboardLayout = GetPrivateProfileIntW(L"Window", L"KeyboardLayout", 106, g.configPath.c_str()) == 101 ? 101 : 106;
    wchar_t pageOrder[4096]{};
    GetPrivateProfileStringW(L"Window", L"PageOrder", L"", pageOrder, 4096, g.configPath.c_str());
    g.config.pageOrder = ParsePageOrder(pageOrder);
    int uiVersion = GetPrivateProfileIntW(L"Window", L"UiVersion", 0, g.configPath.c_str());
    if (uiVersion < 2) {
        if (g.config.buttonSize <= 78) g.config.buttonSize = 104;
        if (g.config.gap <= 8) g.config.gap = 12;
    }

    for (wchar_t* p = sections; *p; p += wcslen(p) + 1) {
        int page = -1;
        int index = -1;
        wchar_t value[2048]{};
        if (swscanf_s(p, L"Page:%d", &page) == 1 && page >= 0) {
            GetPrivateProfileStringW(p, L"Name", L"", value, 2048, g.configPath.c_str());
            g.config.pageNames[page] = value;
            continue;
        }
        if (swscanf_s(p, L"Button:%d:%d", &page, &index) != 2 || page < 0 || index < 0) continue;
        auto& buttons = g.config.pages[page];
        if (static_cast<int>(buttons.size()) <= index) buttons.resize(index + 1);
        GetPrivateProfileStringW(p, L"Title", L"", value, 2048, g.configPath.c_str());
        buttons[index].title = value;
        GetPrivateProfileStringW(p, L"Image", L"", value, 2048, g.configPath.c_str());
        buttons[index].imagePath = value;
        GetPrivateProfileStringW(p, L"Text", L"", value, 2048, g.configPath.c_str());
        buttons[index].text = value;
        GetPrivateProfileStringW(p, L"ActionType", L"None", value, 2048, g.configPath.c_str());
        buttons[index].action.type = ActionFromString(value);
        GetPrivateProfileStringW(p, L"Target", L"", value, 2048, g.configPath.c_str());
        buttons[index].action.target = value;
        GetPrivateProfileStringW(p, L"Args", L"", value, 2048, g.configPath.c_str());
        buttons[index].action.args = value;
    }
    EnsureDefaults();
    NormalizePageOrder();
}

static void SaveConfig() {
    NormalizePageOrder();
    WritePrivateProfileStringW(L"Window", L"Rows", std::to_wstring(g.config.rows).c_str(), g.configPath.c_str());
    WritePrivateProfileStringW(L"Window", L"Cols", std::to_wstring(g.config.cols).c_str(), g.configPath.c_str());
    WritePrivateProfileStringW(L"Window", L"ButtonSize", std::to_wstring(g.config.buttonSize).c_str(), g.configPath.c_str());
    WritePrivateProfileStringW(L"Window", L"Gap", std::to_wstring(g.config.gap).c_str(), g.configPath.c_str());
    WritePrivateProfileStringW(L"Window", L"X", std::to_wstring(g.config.windowX).c_str(), g.configPath.c_str());
    WritePrivateProfileStringW(L"Window", L"Y", std::to_wstring(g.config.windowY).c_str(), g.configPath.c_str());
    WritePrivateProfileStringW(L"Window", L"AlwaysOnTop", g.config.alwaysOnTop ? L"1" : L"0", g.configPath.c_str());
    WritePrivateProfileStringW(L"Window", L"ShowTrayIcon", g.config.showTrayIcon ? L"1" : L"0", g.configPath.c_str());
    WritePrivateProfileStringW(L"Window", L"KeyboardLayout", std::to_wstring(g.config.keyboardLayout).c_str(), g.configPath.c_str());
    WritePrivateProfileStringW(L"Window", L"UiVersion", L"2", g.configPath.c_str());
    WritePrivateProfileStringW(L"Window", L"PageOrder", JoinPageOrder(g.config.pageOrder).c_str(), g.configPath.c_str());

    for (const auto& pageName : g.config.pageNames) {
        std::wstring section = L"Page:" + std::to_wstring(pageName.first);
        WritePrivateProfileStringW(section.c_str(), L"Name", pageName.second.c_str(), g.configPath.c_str());
    }

    for (const auto& pagePair : g.config.pages) {
        for (size_t i = 0; i < pagePair.second.size(); ++i) {
            const ButtonConfig& b = pagePair.second[i];
            std::wstring section = L"Button:" + std::to_wstring(pagePair.first) + L":" + std::to_wstring(i);
            WritePrivateProfileStringW(section.c_str(), L"Title", b.title.c_str(), g.configPath.c_str());
            WritePrivateProfileStringW(section.c_str(), L"Image", b.imagePath.c_str(), g.configPath.c_str());
            WritePrivateProfileStringW(section.c_str(), L"Text", b.text.c_str(), g.configPath.c_str());
            WritePrivateProfileStringW(section.c_str(), L"ActionType", ActionToString(b.action.type).c_str(), g.configPath.c_str());
            WritePrivateProfileStringW(section.c_str(), L"Target", b.action.target.c_str(), g.configPath.c_str());
            WritePrivateProfileStringW(section.c_str(), L"Args", b.action.args.c_str(), g.configPath.c_str());
        }
    }
}

static RECT ButtonRect(int index) {
    int row = index / g.config.cols;
    int col = index % g.config.cols;
    int s = g.config.buttonSize;
    int gap = g.config.gap;
    return RECT{
        gap + col * (s + gap),
        HEADER_HEIGHT + gap + row * (s + gap),
        gap + col * (s + gap) + s,
        HEADER_HEIGHT + gap + row * (s + gap) + s
    };
}

static RECT HeaderButtonRect(int slotFromRight) {
    RECT client{};
    GetClientRect(g.hwnd, &client);
    const int right = client.right - HEADER_BUTTON_GAP - slotFromRight * (HEADER_BUTTON_SIZE + HEADER_BUTTON_GAP);
    return RECT{ right - HEADER_BUTTON_SIZE, 4, right, 4 + HEADER_BUTTON_SIZE };
}

static RECT HeaderPageButtonRect(bool next) {
    RECT client{};
    GetClientRect(g.hwnd, &client);
    const LONG center = (client.right - client.left) / 2;
    const LONG titleHalfWidth = std::min<LONG>(120, std::max<LONG>(54, (client.right - 240) / 2));
    const int x = next ? center + titleHalfWidth + HEADER_BUTTON_GAP :
        center - titleHalfWidth - HEADER_BUTTON_GAP - HEADER_BUTTON_SIZE;
    return RECT{ x, 4, x + HEADER_BUTTON_SIZE, 4 + HEADER_BUTTON_SIZE };
}

static RECT HeaderTitleRect() {
    RECT client{};
    GetClientRect(g.hwnd, &client);
    RECT prevRect = HeaderPageButtonRect(false);
    RECT nextRect = HeaderPageButtonRect(true);
    return RECT{ prevRect.right + HEADER_BUTTON_GAP, 0, nextRect.left - HEADER_BUTTON_GAP, HEADER_HEIGHT };
}

static std::vector<int> ExistingPages() {
    NormalizePageOrder();
    return g.config.pageOrder;
}

static int NextPageIndex() {
    std::vector<int> pages = ExistingPages();
    int maxPage = -1;
    for (int page : pages) maxPage = std::max(maxPage, page);
    for (const auto& pagePair : g.config.pages) maxPage = std::max(maxPage, pagePair.first);
    return maxPage + 1;
}

static int AddPageAfter(int afterPage) {
    int newPage = NextPageIndex();
    auto& buttons = g.config.pages[newPage];
    buttons.clear();
    buttons.resize(g.config.rows * g.config.cols);
    g.config.pageNames.erase(newPage);
    NormalizePageOrder();
    g.config.pageOrder.erase(std::remove(g.config.pageOrder.begin(), g.config.pageOrder.end(), newPage), g.config.pageOrder.end());
    auto it = std::find(g.config.pageOrder.begin(), g.config.pageOrder.end(), afterPage);
    if (it == g.config.pageOrder.end()) {
        g.config.pageOrder.push_back(newPage);
    } else {
        g.config.pageOrder.insert(it + 1, newPage);
    }
    g.currentPage = newPage;
    return newPage;
}

static int AddPage() {
    return AddPageAfter(g.currentPage);
}

static void DeletePageSectionsFromConfigFile(int page) {
    if (g.configPath.empty()) return;

    wchar_t sections[32768]{};
    GetPrivateProfileSectionNamesW(sections, 32768, g.configPath.c_str());
    const std::wstring pageSection = L"Page:" + std::to_wstring(page);
    const std::wstring buttonPrefix = L"Button:" + std::to_wstring(page) + L":";

    for (wchar_t* p = sections; *p; p += wcslen(p) + 1) {
        std::wstring section = p;
        if (section == pageSection || section.rfind(buttonPrefix, 0) == 0) {
            WritePrivateProfileStringW(section.c_str(), nullptr, nullptr, g.configPath.c_str());
        }
    }
}

static bool DeletePage(int page) {
    std::vector<int> pages = ExistingPages();
    if (pages.size() <= 1 || std::find(pages.begin(), pages.end(), page) == pages.end()) return false;

    auto it = std::find(pages.begin(), pages.end(), page);
    int nextPage = 0;
    if (it + 1 != pages.end()) {
        nextPage = *(it + 1);
    } else {
        nextPage = pages.front() == page && pages.size() > 1 ? pages[1] : pages.front();
    }

    g.config.pages.erase(page);
    g.config.pageNames.erase(page);
    g.config.pageOrder.erase(std::remove(g.config.pageOrder.begin(), g.config.pageOrder.end(), page), g.config.pageOrder.end());
    DeletePageSectionsFromConfigFile(page);
    g.currentPage = nextPage;
    CurrentButtons();
    return true;
}

static bool ConfirmAndDeleteCurrentPage(HWND owner) {
    if (ExistingPages().size() <= 1) {
        MessageBoxW(owner, L"The last page cannot be deleted.", L"Delete page", MB_OK | MB_ICONINFORMATION);
        return false;
    }
    std::wstring message = L"Delete \"" + PageName(g.currentPage) + L"\" and all buttons on this page?";
    if (MessageBoxW(owner, message.c_str(), L"Delete page", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
        return false;
    }
    return DeletePage(g.currentPage);
}

static bool ConfirmAndClearButton(HWND owner, int index) {
    auto& buttons = CurrentButtons();
    if (index < 0 || index >= static_cast<int>(buttons.size())) return false;
    if (MessageBoxW(owner, L"Clear this button assignment and display settings?", L"Clear button", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
        return false;
    }
    buttons[index] = ButtonConfig{};
    return true;
}

static void RefreshCurrentIcons();

static void MovePage(bool next) {
    std::vector<int> pages = ExistingPages();
    if (pages.empty()) return;
    auto it = std::find(pages.begin(), pages.end(), g.currentPage);
    if (it == pages.end()) {
        g.currentPage = pages.front();
    } else if (next) {
        ++it;
        g.currentPage = it == pages.end() ? pages.front() : *it;
    } else {
        g.currentPage = it == pages.begin() ? pages.back() : *(it - 1);
    }
    CurrentButtons();
    RefreshCurrentIcons();
    InvalidateRect(g.hwnd, nullptr, TRUE);
}

static int HitButton(POINT pt) {
    const int count = g.config.rows * g.config.cols;
    for (int i = 0; i < count; ++i) {
        RECT r = ButtonRect(i);
        if (PtInRect(&r, pt)) return i;
    }
    return -1;
}

static void UpdateTooltipRects() {
    if (!g.tooltipHwnd) return;
    for (int i = 0; i < 256; ++i) {
        TOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.hwnd = g.hwnd;
        ti.uId = i;
        SendMessageW(g.tooltipHwnd, TTM_DELTOOLW, 0, (LPARAM)&ti);
    }
    int count = g.config.rows * g.config.cols;
    for (int i = 0; i < count; ++i) {
        TOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_SUBCLASS;
        ti.hwnd = g.hwnd;
        ti.uId = i;
        ti.rect = ButtonRect(i);
        ti.lpszText = LPSTR_TEXTCALLBACKW;
        SendMessageW(g.tooltipHwnd, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    }
}

static void ResizeWindowToGrid() {
    if (!g.hwnd) return;
    const int width = g.config.gap + g.config.cols * (g.config.buttonSize + g.config.gap);
    const int height = HEADER_HEIGHT + g.config.gap + g.config.rows * (g.config.buttonSize + g.config.gap);
    SetWindowPos(g.hwnd, g.config.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0,
        width, height, SWP_NOMOVE | SWP_NOACTIVATE);
    BOOL value = DWMWCP_ROUND;
    DwmSetWindowAttribute(g.hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &value, sizeof(value));
    SetLayeredWindowAttributes(g.hwnd, 0, WINDOW_OPACITY, LWA_ALPHA);
    UpdateTooltipRects();
}

static void RememberWindowPosition() {
    if (!g.hwnd || IsIconic(g.hwnd)) return;
    RECT window{};
    if (GetWindowRect(g.hwnd, &window)) {
        g.config.windowX = window.left;
        g.config.windowY = window.top;
    }
}

static void DrawCenteredText(HDC hdc, RECT rc, const std::wstring& text, int points, bool bold) {
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(points, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    lf.lfWeight = bold ? FW_SEMIBOLD : FW_NORMAL;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    HFONT font = CreateFontIndirectW(&lf);
    HFONT old = static_cast<HFONT>(SelectObject(hdc, font));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(245, 247, 250));
    DrawTextW(hdc, text.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, old);
    DeleteObject(font);
}

static bool IsTextTruncated(HDC hdc, const std::wstring& text, RECT rc, int points, bool bold) {
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(points, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    lf.lfWeight = bold ? FW_SEMIBOLD : FW_NORMAL;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    HFONT font = CreateFontIndirectW(&lf);
    HFONT old = static_cast<HFONT>(SelectObject(hdc, font));
    RECT calcRect = { 0, 0, 0, 0 };
    DrawTextW(hdc, text.c_str(), -1, &calcRect, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, old);
    DeleteObject(font);
    return (calcRect.right - calcRect.left) > (rc.right - rc.left);
}

static bool IsWebUrl(const std::wstring& value);
static std::wstring HostFromUrl(const std::wstring& url);

static IWICImagingFactory* GetWicFactory() {
    static IWICImagingFactory* factory = nullptr;
    if (!factory) {
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    }
    return factory;
}

static HBITMAP LoadBitmapWithWic(const std::wstring& path, int side) {
    IWICImagingFactory* wic = GetWicFactory();
    if (!wic) return nullptr;

    IWICBitmapDecoder* decoder = nullptr;
    if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder))) return nullptr;

    IWICBitmapFrameDecode* frame = nullptr;
    HRESULT hr = decoder->GetFrame(0, &frame);
    decoder->Release();
    if (FAILED(hr)) return nullptr;

    IWICFormatConverter* converter = nullptr;
    hr = wic->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeCustom);
    }
    frame->Release();
    if (FAILED(hr)) {
        if (converter) converter->Release();
        return nullptr;
    }

    IWICBitmapScaler* scaler = nullptr;
    hr = wic->CreateBitmapScaler(&scaler);
    if (SUCCEEDED(hr)) {
        hr = scaler->Initialize(converter, side, side, WICBitmapInterpolationModeFant);
    }
    converter->Release();
    if (FAILED(hr)) {
        if (scaler) scaler->Release();
        return nullptr;
    }

    BITMAPINFO bminfo{};
    bminfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bminfo.bmiHeader.biWidth = side;
    bminfo.bmiHeader.biHeight = -side;
    bminfo.bmiHeader.biPlanes = 1;
    bminfo.bmiHeader.biBitCount = 32;
    bminfo.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HDC hdc = GetDC(nullptr);
    HBITMAP hBitmap = CreateDIBSection(hdc, &bminfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
    ReleaseDC(nullptr, hdc);

    if (hBitmap && pixels) {
        scaler->CopyPixels(nullptr, side * 4, side * side * 4, static_cast<BYTE*>(pixels));
    } else {
        if (hBitmap) DeleteObject(hBitmap);
        hBitmap = nullptr;
    }
    scaler->Release();

    return hBitmap;
}

static void RefreshCurrentIcons() {
    auto& buttons = CurrentButtons();
    g.currentIcons.resize(buttons.size());
    for (size_t i = 0; i < buttons.size(); ++i) {
        g.currentIcons[i].Clear();
        const ButtonConfig& b = buttons[i];
        
        RECT r = ButtonRect(static_cast<int>(i));
        int side = std::min(r.right - r.left, r.bottom - r.top) - 28;
        if (side < 24) side = 24;

        if (b.action.type == ActionType::Settings || b.action.type == ActionType::Keys) continue;

        bool loaded = false;
        if (!b.imagePath.empty() && PathFileExistsW(b.imagePath.c_str()) && _wcsicmp(PathFindExtensionW(b.imagePath.c_str()), L".svg") != 0) {
            g.currentIcons[i].bitmap = LoadBitmapWithWic(b.imagePath, side);
            loaded = g.currentIcons[i].bitmap != nullptr;
        }
        
        if (!loaded && IsWebUrl(b.action.target)) {
            std::wstring host = HostFromUrl(b.action.target);
            if (!host.empty()) {
                std::wstring faviconUrl = L"https://" + host + L"/favicon.ico";
                wchar_t cachePath[MAX_PATH]{};
                if (SUCCEEDED(URLDownloadToCacheFileW(nullptr, faviconUrl.c_str(), cachePath, MAX_PATH, 0, nullptr))) {
                    g.currentIcons[i].bitmap = LoadBitmapWithWic(cachePath, side);
                    loaded = g.currentIcons[i].bitmap != nullptr;
                }
            }
        }
        
        if (!loaded && (b.action.type == ActionType::Open || b.action.type == ActionType::Command) && !b.action.target.empty()) {
            SHFILEINFOW info{};
            DWORD flags = SHGFI_ICON | SHGFI_LARGEICON;
            if (b.action.target.rfind(L"shell:", 0) == 0) {
                PIDLIST_ABSOLUTE pidl = nullptr;
                if (SUCCEEDED(SHParseDisplayName(b.action.target.c_str(), nullptr, &pidl, 0, nullptr))) {
                    if (SHGetFileInfoW(reinterpret_cast<LPCWSTR>(pidl), 0, &info, sizeof(info), flags | SHGFI_PIDL)) {
                        g.currentIcons[i].icon = info.hIcon;
                        loaded = true;
                    }
                    CoTaskMemFree(pidl);
                }
            } else {
                if (!PathFileExistsW(b.action.target.c_str())) flags |= SHGFI_USEFILEATTRIBUTES;
                if (SHGetFileInfoW(b.action.target.c_str(), FILE_ATTRIBUTE_NORMAL, &info, sizeof(info), flags)) {
                    g.currentIcons[i].icon = info.hIcon;
                    loaded = true;
                }
            }
        }
    }
}

static bool DrawSystemKeyIcon(HDC hdc, const Action& action, RECT rc) {
    if (action.type != ActionType::Keys) return false;
    const bool volumeUp = action.target == L"VOLUME_UP";
    const bool volumeDown = action.target == L"VOLUME_DOWN";
    const bool volumeUpFast = action.target == L"VOLUME_UP_FAST";
    const bool volumeDownFast = action.target == L"VOLUME_DOWN_FAST";
    const bool volumeMute = action.target == L"VOLUME_MUTE";
    const bool screenshot = action.target == L"PRINTSCREEN";
    const bool playPause = action.target == L"MEDIA_PLAY_PAUSE";
    const bool nextTrack = action.target == L"MEDIA_NEXT_TRACK";
    const bool previousTrack = action.target == L"MEDIA_PREV_TRACK";
    const bool stopMedia = action.target == L"MEDIA_STOP";
    if (!volumeUp && !volumeDown && !volumeUpFast && !volumeDownFast && !volumeMute && !screenshot && !playPause && !nextTrack && !previousTrack && !stopMedia) return false;

    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    const int side = std::min(width, height) - 36;
    if (side < 30) return false;

    const int left = rc.left + (width - side) / 2;
    const int top = rc.top + 12;
    const int midY = top + side / 2;
    const int color = RGB(238, 242, 247);

    HPEN pen = CreatePen(PS_SOLID, 3, color);
    HBRUSH brush = CreateSolidBrush(color);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);

    SelectObject(hdc, GetStockObject(NULL_BRUSH));

    if (volumeUp || volumeDown || volumeUpFast || volumeDownFast || volumeMute) {
        SelectObject(hdc, brush);
        RECT box{ left + side / 10, midY - side / 7, left + side / 3, midY + side / 7 };
        Rectangle(hdc, box.left, box.top, box.right, box.bottom);

        POINT speaker[] = {
            { left + side / 3, midY - side / 6 },
            { left + side / 2, top + side / 4 },
            { left + side / 2, top + side * 3 / 4 },
            { left + side / 3, midY + side / 6 }
        };
        Polygon(hdc, speaker, 4);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));

        if (volumeMute) {
            MoveToEx(hdc, left + side * 2 / 3, midY - side / 6, nullptr);
            LineTo(hdc, left + side * 5 / 6, midY + side / 6);
            MoveToEx(hdc, left + side * 5 / 6, midY - side / 6, nullptr);
            LineTo(hdc, left + side * 2 / 3, midY + side / 6);
        } else {
            const int markY = midY;
            const int markHalf = std::max(5, side / 10);
            const int firstX = left + side * 13 / 20;
            const int secondX = left + side * 9 / 10;
            const int count = volumeUpFast || volumeDownFast ? 2 : 1;
            for (int i = 0; i < count; ++i) {
                const int markX = count == 1 ? left + side * 3 / 4 : (i == 0 ? firstX : secondX);
                MoveToEx(hdc, markX - markHalf, markY, nullptr);
                LineTo(hdc, markX + markHalf, markY);
                if (volumeUp || volumeUpFast) {
                    MoveToEx(hdc, markX, markY - markHalf, nullptr);
                    LineTo(hdc, markX, markY + markHalf);
                }
            }
        }
    } else if (screenshot) {
        RoundRect(hdc, left + side / 8, top + side / 4, left + side * 7 / 8, top + side * 3 / 4, 6, 6);
        Rectangle(hdc, left + side / 3, top + side / 6, left + side * 2 / 3, top + side / 4);
        Ellipse(hdc, left + side * 3 / 8, top + side * 3 / 8, left + side * 5 / 8, top + side * 5 / 8);
    } else if (playPause) {
        SelectObject(hdc, brush);
        POINT play[] = {
            { left + side / 5, top + side / 4 },
            { left + side / 5, top + side * 3 / 4 },
            { left + side / 2, midY }
        };
        Polygon(hdc, play, 3);
        Rectangle(hdc, left + side * 3 / 5, top + side / 4, left + side * 7 / 10, top + side * 3 / 4);
        Rectangle(hdc, left + side * 4 / 5, top + side / 4, left + side * 9 / 10, top + side * 3 / 4);
    } else if (nextTrack || previousTrack) {
        SelectObject(hdc, brush);
        const int dir = nextTrack ? 1 : -1;
        const int baseX = left + side / 2;
        POINT tri1[] = {
            { baseX - dir * side / 3, top + side / 4 },
            { baseX - dir * side / 3, top + side * 3 / 4 },
            { baseX, midY }
        };
        POINT tri2[] = {
            { baseX, top + side / 4 },
            { baseX, top + side * 3 / 4 },
            { baseX + dir * side / 3, midY }
        };
        Polygon(hdc, tri1, 3);
        Polygon(hdc, tri2, 3);
        if (nextTrack) {
            Rectangle(hdc, left + side * 5 / 6, top + side / 4, left + side * 11 / 12, top + side * 3 / 4);
        } else {
            Rectangle(hdc, left + side / 12, top + side / 4, left + side / 6, top + side * 3 / 4);
        }
    } else if (stopMedia) {
        SelectObject(hdc, brush);
        Rectangle(hdc, left + side / 3, top + side / 3, left + side * 2 / 3, top + side * 2 / 3);
    }

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
    return true;
}

static std::wstring CompactKeySpec(std::wstring spec) {
    spec = Trim(spec);
    if (spec == L"ALT_TAB_NEXT") return L"Alt+Tab";
    if (spec == L"ALT_TAB_PREV") return L"Alt+Shift+Tab";
    if (spec.rfind(L"SEQ:", 0) == 0) return L"SEQ";

    std::wstring compact;
    std::wstringstream ss(spec);
    std::wstring token;
    while (std::getline(ss, token, L'+')) {
        token = Trim(token);
        if (token == L"CONTROL") token = L"CTRL";
        if (token == L"WINDOWS") token = L"WIN";
        if (token == L"CAPS_LOCK") token = L"CAPS";
        if (token == L"NUM_LOCK") token = L"NUM";
        if (token == L"SCROLL_LOCK") token = L"SCR";
        if (token == L"BACKSPACE") token = L"BACK";
        if (token == L"PAGEUP") token = L"PGUP";
        if (token == L"PAGEDOWN") token = L"PGDN";
        if (token == L"OEM_102") token = L"\\";
        if (!compact.empty()) compact += L"+";
        compact += token;
    }
    return compact.empty() ? L"KEY" : compact;
}

static std::wstring KeyBadgeFromSpec(const std::wstring& spec) {
    std::wstring compact = CompactKeySpec(spec);
    if (compact == L"SEQ") return L"SEQ";
    if (compact == L"Alt+Tab") return L"TAB";
    if (compact == L"Alt+Shift+Tab") return L"TAB";

    size_t pos = compact.find_last_of(L'+');
    std::wstring badge = pos == std::wstring::npos ? compact : compact.substr(pos + 1);
    if (badge == L"SPACE") return L"SPC";
    if (badge == L"ENTER") return L"ENT";
    if (badge == L"ESCAPE") return L"ESC";
    if (badge == L"BACK") return L"BK";
    if (badge == L"DELETE") return L"DEL";
    if (badge == L"PRINTSCREEN") return L"PRT";
    if (badge.size() > 5) badge = badge.substr(0, 5);
    return badge.empty() ? L"KEY" : badge;
}

static std::vector<std::wstring> SplitKeySpecParts(const std::wstring& spec) {
    std::vector<std::wstring> parts;
    std::wstring trimmed = Trim(spec);
    wchar_t delimiter = trimmed.rfind(L"SEQ:", 0) == 0 ? L',' : L'+';
    if (delimiter == L',') trimmed = trimmed.substr(4);

    std::wstringstream ss(trimmed);
    std::wstring token;
    while (std::getline(ss, token, delimiter)) {
        token = CompactKeySpec(token);
        if (!token.empty()) parts.push_back(token);
    }
    return parts;
}

static std::wstring KeyCapPrimaryText(const std::wstring& spec) {
    std::wstring trimmed = Trim(spec);
    if (trimmed.rfind(L"SEQ:", 0) == 0) {
        std::vector<std::wstring> parts = SplitKeySpecParts(trimmed);
        if (parts.empty()) return L"SEQ";
        std::wstring first = KeyBadgeFromSpec(parts.front());
        std::wstring last = parts.size() > 1 ? KeyBadgeFromSpec(parts.back()) : L"";
        std::wstring label = last.empty() ? first : first + L">" + last;
        return label.size() > 9 ? L"SEQ" : label;
    }
    return KeyBadgeFromSpec(trimmed);
}

static std::wstring KeyCapSecondaryText(const std::wstring& spec) {
    std::wstring trimmed = Trim(spec);
    if (trimmed.rfind(L"SEQ:", 0) == 0) {
        std::vector<std::wstring> parts = SplitKeySpecParts(trimmed);
        return parts.size() <= 1 ? L"SEQ" : L"SEQ x" + std::to_wstring(parts.size());
    }

    std::vector<std::wstring> parts = SplitKeySpecParts(trimmed);
    if (parts.size() <= 1) return L"";
    std::wstring mods;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        std::wstring mod = parts[i];
        if (mod == L"CONTROL") mod = L"CTRL";
        if (mod.size() > 5) mod = mod.substr(0, 5);
        if (!mods.empty()) mods += L"+";
        mods += mod;
    }
    return mods;
}

static bool DrawKeySpecIcon(HDC hdc, const Action& action, RECT rc) {
    if (action.type != ActionType::Keys || action.target.empty()) return false;

    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    const int keyWidth = std::max(46, std::min(width - 26, 96));
    const int keyHeight = std::max(32, std::min(height - 44, 58));
    RECT keyRect{
        rc.left + (width - keyWidth) / 2,
        rc.top + 16,
        rc.left + (width + keyWidth) / 2,
        rc.top + 16 + keyHeight
    };

    HBRUSH fill = CreateSolidBrush(RGB(238, 242, 247));
    HPEN edge = CreatePen(PS_SOLID, 2, RGB(170, 178, 192));
    HGDIOBJ oldBrush = SelectObject(hdc, fill);
    HGDIOBJ oldPen = SelectObject(hdc, edge);
    RoundRect(hdc, keyRect.left, keyRect.top, keyRect.right, keyRect.bottom, 8, 8);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(fill);
    DeleteObject(edge);

    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(10, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    lf.lfWeight = FW_SEMIBOLD;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    HFONT font = CreateFontIndirectW(&lf);
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, font));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(18, 22, 28));
    std::wstring primary = KeyCapPrimaryText(action.target);
    std::wstring secondary = KeyCapSecondaryText(action.target);
    if (!secondary.empty()) {
        RECT topText = keyRect;
        topText.bottom = keyRect.top + keyHeight / 2;
        RECT bottomText = keyRect;
        bottomText.top = keyRect.top + keyHeight / 2 - 2;
        DrawTextW(hdc, secondary.c_str(), -1, &topText, DT_CENTER | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS);
        DrawTextW(hdc, primary.c_str(), -1, &bottomText, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else {
        DrawTextW(hdc, primary.c_str(), -1, &keyRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    SelectObject(hdc, oldFont);
    DeleteObject(font);
    return true;
}

static void DrawHeaderControl(HDC hdc, RECT rc) {
    HBRUSH brush = CreateSolidBrush(RGB(37, 43, 52));
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(104, 116, 136));
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

static void DrawSettingsIcon(HDC hdc, RECT rc) {
    const float cx = (rc.left + rc.right) / 2.0f;
    const float cy = (rc.top + rc.bottom) / 2.0f;
    const float size = static_cast<float>(std::min(rc.right - rc.left, rc.bottom - rc.top));
    const float bodyRadius = size * 0.23f;
    const float tipRadius = size * 0.32f;
    const float holeRadius = size * 0.10f;

    const int numTeeth = 8;
    POINT pts[32];
    for (int i = 0; i < numTeeth; ++i) {
        double aCenter = i * 45.0;
        double a1 = (aCenter - 12.0) * 3.14159265358979323846 / 180.0;
        double a2 = (aCenter - 7.0) * 3.14159265358979323846 / 180.0;
        double a3 = (aCenter + 7.0) * 3.14159265358979323846 / 180.0;
        double a4 = (aCenter + 12.0) * 3.14159265358979323846 / 180.0;

        pts[i * 4 + 0] = { static_cast<LONG>(cx + bodyRadius * cos(a1)), static_cast<LONG>(cy + bodyRadius * sin(a1)) };
        pts[i * 4 + 1] = { static_cast<LONG>(cx + tipRadius * cos(a2)), static_cast<LONG>(cy + tipRadius * sin(a2)) };
        pts[i * 4 + 2] = { static_cast<LONG>(cx + tipRadius * cos(a3)), static_cast<LONG>(cy + tipRadius * sin(a3)) };
        pts[i * 4 + 3] = { static_cast<LONG>(cx + bodyRadius * cos(a4)), static_cast<LONG>(cy + bodyRadius * sin(a4)) };
    }

    int oldMode = SetPolyFillMode(hdc, ALTERNATE);
    HBRUSH gearBrush = CreateSolidBrush(RGB(245, 247, 250));
    HPEN nullPen = static_cast<HPEN>(GetStockObject(NULL_PEN));
    HGDIOBJ oldBrush = SelectObject(hdc, gearBrush);
    HGDIOBJ oldPen = SelectObject(hdc, nullPen);

    BeginPath(hdc);
    Polygon(hdc, pts, 32);
    int hr = static_cast<int>(holeRadius);
    Ellipse(hdc, static_cast<int>(cx) - hr, static_cast<int>(cy) - hr, static_cast<int>(cx) + hr, static_cast<int>(cy) + hr);
    EndPath(hdc);

    HBRUSH bgBrush = CreateSolidBrush(RGB(37, 43, 52));
    SelectObject(hdc, gearBrush);
    SetBkMode(hdc, TRANSPARENT);
    FillPath(hdc);

    // Draw the center hole with the background color
    SelectObject(hdc, bgBrush);
    Ellipse(hdc, static_cast<int>(cx) - hr, static_cast<int>(cy) - hr, static_cast<int>(cx) + hr, static_cast<int>(cy) + hr);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    SetPolyFillMode(hdc, oldMode);
    DeleteObject(gearBrush);
    DeleteObject(bgBrush);
}

static void Paint(HDC hdc) {
    RECT client{};
    GetClientRect(g.hwnd, &client);
    HBRUSH bg = CreateSolidBrush(RGB(22, 25, 30));
    HPEN border = CreatePen(PS_SOLID, 1, RGB(82, 91, 108));
    HGDIOBJ oldBg = SelectObject(hdc, bg);
    HGDIOBJ oldBorder = SelectObject(hdc, border);
    RoundRect(hdc, client.left, client.top, client.right, client.bottom, 18, 18);
    SelectObject(hdc, oldBg);
    SelectObject(hdc, oldBorder);
    DeleteObject(bg);
    DeleteObject(border);

    RECT prevRect = HeaderPageButtonRect(false);
    RECT nextRect = HeaderPageButtonRect(true);
    RECT pageTitleRect = HeaderTitleRect();
    RECT settingsRect = HeaderButtonRect(2);
    RECT minimizeRect = HeaderButtonRect(1);
    RECT closeRect = HeaderButtonRect(0);
    DrawCenteredText(hdc, pageTitleRect, PageName(g.currentPage), 9, true);

    DrawHeaderControl(hdc, settingsRect);
    DrawHeaderControl(hdc, minimizeRect);
    DrawHeaderControl(hdc, closeRect);
    DrawCenteredText(hdc, prevRect, L"<", 12, true);
    DrawCenteredText(hdc, nextRect, L">", 12, true);
    DrawSettingsIcon(hdc, settingsRect);
    DrawCenteredText(hdc, minimizeRect, L"_", 11, true);
    DrawCenteredText(hdc, closeRect, L"X", 10, true);

    auto& buttons = CurrentButtons();
    const int count = g.config.rows * g.config.cols;
    for (int i = 0; i < count; ++i) {
        RECT r = ButtonRect(i);
        HBRUSH brush = CreateSolidBrush(RGB(38, 43, 52));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(92, 101, 118));
        HGDIOBJ oldBrush = SelectObject(hdc, brush);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        RoundRect(hdc, r.left, r.top, r.right, r.bottom, 10, 10);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);

        const ButtonConfig& b = buttons[i];
        RECT iconRect = r;
        iconRect.bottom -= 20;
        bool drew = false;

        if (g.currentIcons.size() > static_cast<size_t>(i)) {
            const CachedIcon& cached = g.currentIcons[i];
            if (cached.bitmap) {
                HDC memDc = CreateCompatibleDC(hdc);
                HGDIOBJ oldBitmap = SelectObject(memDc, cached.bitmap);
                BLENDFUNCTION blend{};
                blend.BlendOp = AC_SRC_OVER;
                blend.SourceConstantAlpha = 255;
                blend.AlphaFormat = AC_SRC_ALPHA;
                
                int side = std::min(r.right - r.left, r.bottom - r.top) - 28;
                if (side < 24) side = 24;
                int x = r.left + ((r.right - r.left) - side) / 2;
                int y = r.top + 10;
                
                AlphaBlend(hdc, x, y, side, side, memDc, 0, 0, side, side, blend);
                SelectObject(memDc, oldBitmap);
                DeleteDC(memDc);
                drew = true;
            } else if (cached.icon) {
                int side = std::min(r.right - r.left, r.bottom - r.top) - 32;
                if (side < 24) side = 24;
                int x = r.left + ((r.right - r.left) - side) / 2;
                int y = r.top + 12;
                DrawIconEx(hdc, x, y, cached.icon, side, side, 0, nullptr, DI_NORMAL);
                drew = true;
            }
        }

        if (!drew && b.action.type == ActionType::Keys) {
            drew = DrawSystemKeyIcon(hdc, b.action, r);
            if (!drew) drew = DrawKeySpecIcon(hdc, b.action, r);
        }
        if (!drew && b.action.type == ActionType::Settings) {
            RECT settingsIconRect = r;
            settingsIconRect.bottom -= 20;
            DrawSettingsIcon(hdc, settingsIconRect);
            drew = true;
        }
        if (!drew && !b.text.empty()) DrawCenteredText(hdc, iconRect, b.text, 24, false);

        RECT titleRect = r;
        titleRect.top = r.bottom - 24;
        titleRect.left += 6;
        titleRect.right -= 6;
        DrawCenteredText(hdc, titleRect, b.title.empty() ? L"Empty" : b.title, 9, false);
    }
}

static WORD VkFromToken(std::wstring token) {
    std::transform(token.begin(), token.end(), token.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(towupper(c));
    });
    if (token == L"CTRL" || token == L"CONTROL") return VK_CONTROL;
    if (token == L"ALT") return VK_MENU;
    if (token == L"SHIFT") return VK_SHIFT;
    if (token == L"WIN" || token == L"WINDOWS") return VK_LWIN;
    if (token == L"VOLUMEUP" || token == L"VOLUME_UP") return VK_VOLUME_UP;
    if (token == L"VOLUMEDOWN" || token == L"VOLUME_DOWN") return VK_VOLUME_DOWN;
    if (token == L"MUTE" || token == L"VOLUMEMUTE" || token == L"VOLUME_MUTE") return VK_VOLUME_MUTE;
    if (token == L"SCREENSHOT" || token == L"PRINTSCREEN" || token == L"PRTSC") return VK_SNAPSHOT;
    if (token == L"PLAYPAUSE" || token == L"PLAY_PAUSE" || token == L"MEDIA_PLAY_PAUSE") return VK_MEDIA_PLAY_PAUSE;
    if (token == L"NEXTTRACK" || token == L"NEXT_TRACK" || token == L"MEDIA_NEXT_TRACK") return VK_MEDIA_NEXT_TRACK;
    if (token == L"PREVTRACK" || token == L"PREVIOUS_TRACK" || token == L"MEDIA_PREV_TRACK") return VK_MEDIA_PREV_TRACK;
    if (token == L"MEDIASTOP" || token == L"MEDIA_STOP") return VK_MEDIA_STOP;
    if (token == L"ENTER" || token == L"RETURN") return VK_RETURN;
    if (token == L"ESC" || token == L"ESCAPE") return VK_ESCAPE;
    if (token == L"TAB") return VK_TAB;
    if (token == L"SPACE") return VK_SPACE;
    if (token == L"BACKSPACE" || token == L"BKSP") return VK_BACK;
    if (token == L"CAPSLOCK" || token == L"CAPS_LOCK") return VK_CAPITAL;
    if (token == L"NUMLOCK" || token == L"NUM_LOCK") return VK_NUMLOCK;
    if (token == L"SCROLLLOCK" || token == L"SCROLL_LOCK") return VK_SCROLL;
    if (token == L"PAUSE" || token == L"BREAK") return VK_PAUSE;
    if (token == L"MENU" || token == L"APPS") return VK_APPS;
    if (token == L"NUM0") return VK_NUMPAD0;
    if (token == L"NUM1") return VK_NUMPAD1;
    if (token == L"NUM2") return VK_NUMPAD2;
    if (token == L"NUM3") return VK_NUMPAD3;
    if (token == L"NUM4") return VK_NUMPAD4;
    if (token == L"NUM5") return VK_NUMPAD5;
    if (token == L"NUM6") return VK_NUMPAD6;
    if (token == L"NUM7") return VK_NUMPAD7;
    if (token == L"NUM8") return VK_NUMPAD8;
    if (token == L"NUM9") return VK_NUMPAD9;
    if (token == L"NUMADD") return VK_ADD;
    if (token == L"NUMSUB") return VK_SUBTRACT;
    if (token == L"NUMMUL") return VK_MULTIPLY;
    if (token == L"NUMDIV") return VK_DIVIDE;
    if (token == L"NUMDECIMAL") return VK_DECIMAL;
    if (token == L"DELETE" || token == L"DEL") return VK_DELETE;
    if (token == L"INSERT" || token == L"INS") return VK_INSERT;
    if (token == L"HOME") return VK_HOME;
    if (token == L"END") return VK_END;
    if (token == L"PAGEUP" || token == L"PGUP") return VK_PRIOR;
    if (token == L"PAGEDOWN" || token == L"PGDN") return VK_NEXT;
    if (token == L"UP") return VK_UP;
    if (token == L"DOWN") return VK_DOWN;
    if (token == L"LEFT") return VK_LEFT;
    if (token == L"RIGHT") return VK_RIGHT;
    if (token == L"OEM_PLUS") return VK_OEM_PLUS;
    if (token == L"OEM_MINUS") return VK_OEM_MINUS;
    if (token == L"OEM_COMMA") return VK_OEM_COMMA;
    if (token == L"OEM_PERIOD") return VK_OEM_PERIOD;
    if (token == L"OEM_1") return VK_OEM_1;
    if (token == L"OEM_2") return VK_OEM_2;
    if (token == L"OEM_3") return VK_OEM_3;
    if (token == L"OEM_4") return VK_OEM_4;
    if (token == L"OEM_5") return VK_OEM_5;
    if (token == L"OEM_6") return VK_OEM_6;
    if (token == L"OEM_7") return VK_OEM_7;
    if (token == L"OEM_102") return VK_OEM_102;
    if (token.size() == 1) return VkKeyScanW(token[0]) & 0xff;
    if (token.size() > 1 && token[0] == L'F') {
        int n = _wtoi(token.c_str() + 1);
        if (n >= 1 && n <= 24) return static_cast<WORD>(VK_F1 + n - 1);
    }
    return 0;
}

static void SendKeyChord(const std::wstring& spec) {
    std::vector<WORD> keys;
    std::wstringstream ss(spec);
    std::wstring token;
    while (std::getline(ss, token, L'+')) {
        WORD vk = VkFromToken(Trim(token));
        if (vk) keys.push_back(vk);
    }
    std::vector<INPUT> inputs;
    for (WORD vk : keys) {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = vk;
        inputs.push_back(in);
    }
    for (auto it = keys.rbegin(); it != keys.rend(); ++it) {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = *it;
        in.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.push_back(in);
    }
    if (!inputs.empty()) SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

static void SendVirtualKey(WORD vk, bool keyUp = false) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    if (keyUp) in.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(INPUT));
}

static void ReleaseAltTabHold() {
    if (!g.altTabActive) return;
    KillTimer(g.hwnd, IDT_ALT_TAB_RELEASE);
    SendVirtualKey(VK_MENU, true);
    g.altTabActive = false;
}

static void SendAltTabSwitch(bool reverse) {
    if (!g.altTabActive) {
        SendVirtualKey(VK_MENU);
        g.altTabActive = true;
    }
    if (reverse) SendVirtualKey(VK_SHIFT);
    SendVirtualKey(VK_TAB);
    SendVirtualKey(VK_TAB, true);
    if (reverse) SendVirtualKey(VK_SHIFT, true);
    if (g.hwnd) SetTimer(g.hwnd, IDT_ALT_TAB_RELEASE, 1200, nullptr);
}

static void SendKeySpec(const std::wstring& spec) {
    std::wstring trimmed = Trim(spec);
    if (trimmed == L"ALT_TAB_NEXT" || trimmed == L"ALT_TAB_PREV") {
        SendAltTabSwitch(trimmed == L"ALT_TAB_PREV");
        return;
    }
    if (trimmed == L"VOLUME_UP_FAST" || trimmed == L"VOLUME_DOWN_FAST") {
        const std::wstring chord = trimmed == L"VOLUME_UP_FAST" ? L"VOLUME_UP" : L"VOLUME_DOWN";
        for (int i = 0; i < 5; ++i) {
            SendKeyChord(chord);
            Sleep(20);
        }
        return;
    }
    if (trimmed.rfind(L"SEQ:", 0) != 0) {
        SendKeyChord(trimmed);
        return;
    }

    std::wstringstream ss(trimmed.substr(4));
    std::wstring chord;
    while (std::getline(ss, chord, L',')) {
        chord = Trim(chord);
        if (!chord.empty()) {
            SendKeyChord(chord);
            Sleep(40);
        }
    }
}

static void ExecuteAction(const Action& action) {
    switch (action.type) {
    case ActionType::Open:
    case ActionType::Settings:
        ShellExecuteW(g.hwnd, L"open", action.target.c_str(), action.args.empty() ? nullptr : action.args.c_str(), nullptr, SW_SHOWNORMAL);
        break;
    case ActionType::Command: {
        std::wstring cmd = L"\"" + action.target + L"\"";
        if (!action.args.empty()) cmd += L" " + action.args;
        STARTUPINFOW si{ sizeof(si) };
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        break;
    }
    case ActionType::Keys:
        SendKeySpec(action.target);
        break;
    default:
        break;
    }
}

static std::wstring BrowseForImage(HWND owner) {
    wchar_t file[MAX_PATH]{};
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner = owner;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.ico;*.svg\0All files\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    return GetOpenFileNameW(&ofn) ? std::wstring(file) : L"";
}

static std::wstring BrowseForTarget(HWND owner, const wchar_t* filter) {
    wchar_t file[MAX_PATH]{};
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner = owner;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = filter;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    return GetOpenFileNameW(&ofn) ? std::wstring(file) : L"";
}

static std::wstring BrowseForFolder(HWND owner) {
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpszTitle = L"Select folder";
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return L"";
    wchar_t path[MAX_PATH]{};
    SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    return path;
}

static std::wstring GetWindowTextString(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    std::wstring text(len + 1, L'\0');
    if (len > 0) GetWindowTextW(hwnd, text.data(), len + 1);
    text.resize(len);
    return text;
}

static void SetControlFont(HWND hwnd) {
    if (g.uiFont && hwnd) SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(g.uiFont), TRUE);
}

static HMENU ControlId(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

static HWND AddLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h, int id = 0) {
    HWND label = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h,
        parent, id ? ControlId(id) : nullptr, g.instance, nullptr);
    SetControlFont(label);
    return label;
}

static HWND AddEdit(HWND parent, int id, const std::wstring& value, int x, int y, int w, int h) {
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", value.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        x, y, w, h, parent, ControlId(id), g.instance, nullptr);
    SetControlFont(edit);
    return edit;
}

static HWND AddButton(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h, DWORD style = 0) {
    HWND button = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
        x, y, w, h, parent, ControlId(id), g.instance, nullptr);
    SetControlFont(button);
    return button;
}

static HWND AddCombo(HWND parent, int id, int x, int y, int w, int h) {
    HWND combo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
        x, y, w, h, parent, ControlId(id), g.instance, nullptr);
    SetControlFont(combo);
    SendMessageW(combo, CB_SETMINVISIBLE, 12, 0);
    return combo;
}

static LRESULT DialogControlColor(WPARAM wp) {
    HDC hdc = reinterpret_cast<HDC>(wp);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, DIALOG_TEXT);
    return reinterpret_cast<LRESULT>(DialogBrush());
}

static LRESULT DialogFieldColor(WPARAM wp) {
    HDC hdc = reinterpret_cast<HDC>(wp);
    SetBkMode(hdc, OPAQUE);
    SetBkColor(hdc, DIALOG_FIELD_BG);
    SetTextColor(hdc, DIALOG_TEXT);
    return reinterpret_cast<LRESULT>(DialogFieldBrush());
}

static void ApplyDialogDarkMode(HWND hwnd) {
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    using DwmSetWindowAttributeProc = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    auto setAttr = reinterpret_cast<DwmSetWindowAttributeProc>(GetProcAddress(dwm, "DwmSetWindowAttribute"));
    if (setAttr) {
        BOOL enabled = TRUE;
        setAttr(hwnd, 20, &enabled, sizeof(enabled));
        setAttr(hwnd, 19, &enabled, sizeof(enabled));
    }
    FreeLibrary(dwm);
}

static void PlaceDialogNearApp(HWND dialog) {
    if (!dialog || !g.hwnd) return;
    RECT app{};
    RECT dlg{};
    GetWindowRect(g.hwnd, &app);
    GetWindowRect(dialog, &dlg);

    const int width = dlg.right - dlg.left;
    const int height = dlg.bottom - dlg.top;
    HMONITOR monitor = MonitorFromWindow(g.hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{ sizeof(info) };
    GetMonitorInfoW(monitor, &info);
    RECT work = info.rcWork;

    int x = app.right + 12;
    if (x + width > work.right) x = app.left - width - 12;
    if (x < work.left) x = app.left + ((app.right - app.left) - width) / 2;

    int y = app.top;
    if (y + height > work.bottom) y = work.bottom - height;
    if (y < work.top) y = work.top;

    SetWindowPos(dialog, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void UpdateTrayIcon(bool add) {
    if (!g.hwnd) return;

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g.hwnd;
    nid.uID = 1;

    if (!add) {
        Shell_NotifyIconW(NIM_DELETE, &nid);
        return;
    }

    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIconW(g.instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wcscpy_s(nid.szTip, L"Launcher Widget");
    Shell_NotifyIconW(NIM_ADD, &nid);
}

static void ApplyTrayIconSetting() {
    UpdateTrayIcon(g.config.showTrayIcon);
}

static void RestoreMainWindow() {
    ShowWindow(g.hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(g.hwnd);
}

static void MinimizeMainWindow() {
    if (g.config.showTrayIcon) {
        ShowWindow(g.hwnd, SW_HIDE);
    } else {
        ShowWindow(g.hwnd, SW_MINIMIZE);
    }
}

static void ShowTrayMenu() {
    POINT pt{};
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_RESTORE, L"Show");
    AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"Settings");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");
    SetForegroundWindow(g.hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g.hwnd, nullptr);
    DestroyMenu(menu);
}

struct FavoriteLink {
    std::wstring title;
    std::wstring url;
};

struct AppCandidate {
    std::wstring title;
    std::wstring target;
    std::wstring args;
    std::wstring iconPath;
};

static bool IsWebUrl(const std::wstring& value) {
    return value.rfind(L"http://", 0) == 0 || value.rfind(L"https://", 0) == 0;
}

static std::wstring HostFromUrl(const std::wstring& url) {
    size_t start = url.find(L"://");
    start = start == std::wstring::npos ? 0 : start + 3;
    size_t end = url.find_first_of(L"/?#", start);
    return url.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
}

static std::wstring UrlDecodeAscii(const std::string& value) {
    std::string decoded;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            char c = value[++i];
            switch (c) {
            case '"': decoded.push_back('"'); break;
            case '\\': decoded.push_back('\\'); break;
            case '/': decoded.push_back('/'); break;
            case 'b': decoded.push_back('\b'); break;
            case 'f': decoded.push_back('\f'); break;
            case 'n': decoded.push_back('\n'); break;
            case 'r': decoded.push_back('\r'); break;
            case 't': decoded.push_back('\t'); break;
            default: decoded.push_back(c); break;
            }
        } else {
            decoded.push_back(value[i]);
        }
    }
    return Utf8ToWide(decoded);
}

static std::string ReadTextFileUtf8(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static std::wstring ReadUrlShortcut(const std::wstring& path) {
    std::wifstream in(path);
    if (!in) return L"";
    std::wstring line;
    while (std::getline(in, line)) {
        if (line.rfind(L"URL=", 0) == 0) return Trim(line.substr(4));
    }
    return L"";
}

static void CollectUrlShortcuts(const std::wstring& dir, std::vector<FavoriteLink>& links) {
    std::wstring pattern = dir + L"\\*";
    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring path = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CollectUrlShortcuts(path, links);
        } else if (_wcsicmp(PathFindExtensionW(path.c_str()), L".url") == 0) {
            std::wstring url = ReadUrlShortcut(path);
            if (!url.empty()) {
                PathRemoveExtensionW(fd.cFileName);
                links.push_back({ fd.cFileName, url });
            }
        }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

static void CollectChromiumBookmarks(const std::wstring& path, std::vector<FavoriteLink>& links) {
    std::string json = ReadTextFileUtf8(path);
    size_t pos = 0;
    while ((pos = json.find("\"url\"", pos)) != std::string::npos) {
        size_t colon = json.find(':', pos);
        size_t first = json.find('"', colon + 1);
        size_t last = json.find('"', first + 1);
        if (colon == std::string::npos || first == std::string::npos || last == std::string::npos) break;
        std::wstring url = UrlDecodeAscii(json.substr(first + 1, last - first - 1));

        std::wstring title = L"Bookmark";
        size_t namePos = json.rfind("\"name\"", pos);
        if (namePos != std::string::npos) {
            size_t nameColon = json.find(':', namePos);
            size_t nameFirst = json.find('"', nameColon + 1);
            size_t nameLast = json.find('"', nameFirst + 1);
            if (nameFirst != std::string::npos && nameLast != std::string::npos && nameFirst < pos) {
                title = UrlDecodeAscii(json.substr(nameFirst + 1, nameLast - nameFirst - 1));
            }
        }
        if (!url.empty() && IsWebUrl(url)) {
            links.push_back({ title, url });
        }
        pos = last + 1;
        if (links.size() >= 80) break;
    }
}

static bool ResolveShortcutTarget(const std::wstring& shortcutPath, std::wstring& target, std::wstring& args, std::wstring& iconPath) {
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return false;
    IPersistFile* file = nullptr;
    bool success = false;
    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
        if (SUCCEEDED(file->Load(shortcutPath.c_str(), STGM_READ))) {
            wchar_t path[MAX_PATH]{};
            if (SUCCEEDED(link->GetPath(path, MAX_PATH, nullptr, SLGP_UNCPRIORITY)) && path[0]) {
                target = path;
                success = true;
            }
            wchar_t argsBuf[INFOTIPSIZE]{};
            if (SUCCEEDED(link->GetArguments(argsBuf, INFOTIPSIZE)) && argsBuf[0]) {
                args = argsBuf;
            }
            wchar_t iconBuf[MAX_PATH]{};
            int iconIndex = 0;
            if (SUCCEEDED(link->GetIconLocation(iconBuf, MAX_PATH, &iconIndex)) && iconBuf[0]) {
                iconPath = iconBuf;
            }
        }
        file->Release();
    }
    link->Release();
    return success;
}

static void CollectStartMenuShortcuts(const std::wstring& dir, std::vector<AppCandidate>& apps) {
    std::wstring pattern = dir + L"\\*";
    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring path = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CollectStartMenuShortcuts(path, apps);
        } else if (_wcsicmp(PathFindExtensionW(path.c_str()), L".lnk") == 0) {
            std::wstring target, args, iconPath;
            if (ResolveShortcutTarget(path, target, args, iconPath)) {
                if (_wcsicmp(PathFindExtensionW(target.c_str()), L".exe") == 0) {
                    wchar_t title[MAX_PATH]{};
                    wcscpy_s(title, fd.cFileName);
                    PathRemoveExtensionW(title);
                    apps.push_back({ title, target, args, iconPath });
                }
            }
        }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

static void CollectKnownFolderApps(REFKNOWNFOLDERID folderId, std::vector<AppCandidate>& apps) {
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(folderId, 0, nullptr, &path))) {
        CollectStartMenuShortcuts(std::wstring(path) + L"\\Programs", apps);
        CoTaskMemFree(path);
    }
}

static void CollectUWPApps(std::vector<AppCandidate>& apps) {
    IShellFolder* desktop = nullptr;
    if (FAILED(SHGetDesktopFolder(&desktop))) return;

    PIDLIST_ABSOLUTE appsPidl = nullptr;
    if (SUCCEEDED(desktop->ParseDisplayName(NULL, NULL, const_cast<LPWSTR>(L"shell:AppsFolder"), NULL, &appsPidl, NULL))) {
        IShellFolder* appsFolder = nullptr;
        if (SUCCEEDED(desktop->BindToObject(appsPidl, NULL, IID_PPV_ARGS(&appsFolder)))) {
            IEnumIDList* enumIdList = nullptr;
            if (SUCCEEDED(appsFolder->EnumObjects(NULL, SHCONTF_NONFOLDERS, &enumIdList))) {
                PITEMID_CHILD childPidl = nullptr;
                while (enumIdList->Next(1, &childPidl, NULL) == S_OK) {
                    STRRET strName;
                    std::wstring title;
                    if (SUCCEEDED(appsFolder->GetDisplayNameOf(childPidl, SHGDN_NORMAL, &strName))) {
                        wchar_t* nameStr = nullptr;
                        StrRetToStrW(&strName, childPidl, &nameStr);
                        if (nameStr) {
                            title = nameStr;
                            CoTaskMemFree(nameStr);
                        }
                    }
                    std::wstring target;
                    if (SUCCEEDED(appsFolder->GetDisplayNameOf(childPidl, SHGDN_FORPARSING, &strName))) {
                        wchar_t* parseStr = nullptr;
                        StrRetToStrW(&strName, childPidl, &parseStr);
                        if (parseStr) {
                            target = L"shell:AppsFolder\\" + std::wstring(parseStr);
                            CoTaskMemFree(parseStr);
                        }
                    }
                    if (!title.empty() && !target.empty()) {
                        apps.push_back({ title, target, L"", L"" });
                    }
                    CoTaskMemFree(childPidl);
                }
                enumIdList->Release();
            }
            appsFolder->Release();
        }
        CoTaskMemFree(appsPidl);
    }
    desktop->Release();
}

static std::vector<AppCandidate> LoadStartMenuApps() {
    std::vector<AppCandidate> apps;
    CollectKnownFolderApps(FOLDERID_StartMenu, apps);
    CollectKnownFolderApps(FOLDERID_CommonStartMenu, apps);
    CollectUWPApps(apps);
    std::sort(apps.begin(), apps.end(), [](const AppCandidate& a, const AppCandidate& b) {
        return _wcsicmp(a.title.c_str(), b.title.c_str()) < 0;
    });
    for (size_t i = 0; i < apps.size(); ) {
        size_t j = i + 1;
        while (j < apps.size() && _wcsicmp(apps[i].title.c_str(), apps[j].title.c_str()) == 0) {
            j++;
        }
        if (j - i > 1) {
            for (size_t k = i; k < j; ++k) {
                if (apps[k].target.find(L"Brave") != std::wstring::npos) apps[k].title += L" (Brave)";
                else if (apps[k].target.find(L"Edge") != std::wstring::npos) apps[k].title += L" (Edge)";
                else if (apps[k].target.find(L"Chrome") != std::wstring::npos) apps[k].title += L" (Chrome)";
                else if (apps[k].target.find(L"shell:AppsFolder") == 0) {
                    if (apps[k].target.find(L"!App") != std::wstring::npos) {
                        apps[k].title += L" (Edge App)";
                    } else {
                        apps[k].title += L" (UWP)";
                    }
                }
            }
        }
        i = j;
    }
    apps.erase(std::unique(apps.begin(), apps.end(), [](const AppCandidate& a, const AppCandidate& b) {
        return _wcsicmp(a.target.c_str(), b.target.c_str()) == 0 && wcscmp(a.args.c_str(), b.args.c_str()) == 0;
    }), apps.end());
    return apps;
}

static std::vector<FavoriteLink> LoadBrowserFavorites() {
    std::vector<FavoriteLink> links;
    std::wstring profile = GetEnvPath(L"USERPROFILE");
    if (!profile.empty()) CollectUrlShortcuts(profile + L"\\Favorites", links);

    std::wstring local = GetEnvPath(L"LOCALAPPDATA");
    if (!local.empty()) {
        CollectChromiumBookmarks(local + L"\\Microsoft\\Edge\\User Data\\Default\\Bookmarks", links);
        CollectChromiumBookmarks(local + L"\\Google\\Chrome\\User Data\\Default\\Bookmarks", links);
    }
    return links;
}

struct ButtonEditorContext {
    ButtonConfig original;
    ButtonConfig* target = nullptr;
    bool accepted = false;
    bool cleared = false;
};

static std::wstring ComboText(HWND combo) {
    wchar_t text[128]{};
    int sel = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (sel >= 0) SendMessageW(combo, CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(text));
    return text;
}

static bool IsSystemKeyKind(const std::wstring& kind) {
    return kind == L"Screenshot" || kind == L"スクリーンショット";
}

static bool IsSequenceKeySpec(const std::wstring& spec) {
    return Trim(spec).rfind(L"SEQ:", 0) == 0;
}

static std::wstring SystemKeyTarget(const std::wstring& kind) {
    if (kind == L"Screenshot" || kind == L"スクリーンショット") return L"PRINTSCREEN";
    return L"";
}

static std::wstring ActionKindForButton(const ButtonConfig& button) {
    const Action& action = button.action;
    if (action.type == ActionType::None) return L"None";
    if (action.type == ActionType::Settings) return L"Windows Settings";
    if (action.type == ActionType::Command) return L"Command";
    if (action.type == ActionType::Keys) {
        if (action.target == L"VOLUME_UP") return L"メディアコントロール";
        if (action.target == L"VOLUME_DOWN") return L"メディアコントロール";
        if (action.target == L"VOLUME_UP_FAST") return L"メディアコントロール";
        if (action.target == L"VOLUME_DOWN_FAST") return L"メディアコントロール";
        if (action.target == L"VOLUME_MUTE") return L"メディアコントロール";
        if (action.target == L"PRINTSCREEN") return L"スクリーンショット";
        if (action.target == L"MEDIA_PLAY_PAUSE") return L"メディアコントロール";
        if (action.target == L"MEDIA_NEXT_TRACK") return L"メディアコントロール";
        if (action.target == L"MEDIA_PREV_TRACK") return L"メディアコントロール";
        if (action.target == L"MEDIA_STOP") return L"メディアコントロール";
        return L"Keys";
    }
    if (action.target.rfind(L"http://", 0) == 0 || action.target.rfind(L"https://", 0) == 0) return L"URL";
    if (PathIsDirectoryW(action.target.c_str())) return L"Folder";
    if (_wcsicmp(PathFindExtensionW(action.target.c_str()), L".exe") == 0) return L"App (.exe)";
    return L"File";
}

static void SetVisible(HWND hwnd, bool visible) {
    ShowWindow(hwnd, visible ? SW_SHOW : SW_HIDE);
}

static void UpdateButtonEditorFields(HWND hwnd) {
    std::wstring kind = ComboText(GetDlgItem(hwnd, IDC_ACTION));
    HWND targetLabel = GetDlgItem(hwnd, IDC_TARGET_LABEL);
    HWND argsLabel = GetDlgItem(hwnd, IDC_ARGS_LABEL);
    HWND target = GetDlgItem(hwnd, IDC_TARGET);
    HWND args = GetDlgItem(hwnd, IDC_ARGS);
    HWND browse = GetDlgItem(hwnd, IDC_TARGET_BROWSE);
    HWND import = GetDlgItem(hwnd, IDC_URL_IMPORT);

    bool needsTarget = kind != L"None" && !IsSystemKeyKind(kind);
    bool needsArgs = kind == L"App (.exe)" || kind == L"File" || kind == L"Command";
    bool canBrowse = kind == L"App (.exe)" || kind == L"File" || kind == L"Folder" || kind == L"Keys";
    bool canImport = kind == L"URL" || kind == L"App (.exe)" || kind == L"Windows Settings" || kind == L"メディアコントロール";

    SetWindowTextW(targetLabel, kind == L"URL" ? L"URL" :
        kind == L"Keys" ? L"Key chord" :
        kind == L"Windows Settings" ? L"Settings" :
        kind == L"Command" ? L"Command" :
        kind == L"Folder" ? L"Folder" :
        kind == L"メディアコントロール" ? L"Command" :
        kind == L"File" ? L"File" : L"Target");
    SetWindowTextW(argsLabel, kind == L"Command" ? L"Arguments" : L"Options");
    SetWindowTextW(browse, kind == L"Folder" ? L"フォルダー" :
        kind == L"Keys" ? L"選択" : L"選択");
    SetWindowTextW(import, kind == L"App (.exe)" ? L"Start menu" :
        (kind == L"Windows Settings" || kind == L"メディアコントロール") ? L"Choose" : L"Favorites");

    if (kind == L"App (.exe)") {
        MoveWindow(target, 220, 118, 500, 34, TRUE);
        MoveWindow(browse, 740, 118, 86, 34, TRUE);
        MoveWindow(import, 842, 118, 116, 34, TRUE);
    } else if (kind == L"URL") {
        MoveWindow(target, 220, 118, 588, 34, TRUE);
        MoveWindow(import, 842, 118, 116, 34, TRUE);
    } else if (kind == L"Windows Settings" || kind == L"メディアコントロール") {
        MoveWindow(target, 220, 118, 588, 34, TRUE);
        MoveWindow(import, 842, 118, 116, 34, TRUE);
    } else if (kind == L"File" || kind == L"Folder") {
        MoveWindow(target, 220, 118, 588, 34, TRUE);
        MoveWindow(browse, 842, 118, 116, 34, TRUE);
    } else if (kind == L"Keys") {
        MoveWindow(target, 220, 118, 588, 34, TRUE);
        MoveWindow(browse, 842, 118, 116, 34, TRUE);
    } else {
        MoveWindow(target, 220, 118, 700, 34, TRUE);
    }

    SetVisible(targetLabel, needsTarget);
    SetVisible(target, needsTarget);
    SetVisible(argsLabel, needsArgs);
    SetVisible(args, needsArgs);
    SetVisible(browse, canBrowse);
    SetVisible(import, canImport);
}

static std::wstring TextBadgeFromTitle(const std::wstring& title, const wchar_t* fallback) {
    std::wstring text;
    for (wchar_t c : title) {
        if (iswalpha(c) || iswdigit(c)) text.push_back(static_cast<wchar_t>(towupper(c)));
        if (text.size() == 3) break;
    }
    return text.empty() ? fallback : text;
}

static void FillDisplayDefaults(HWND hwnd, const std::wstring& kind);

static void ComputeDisplayDefaults(HWND hwnd, const std::wstring& kind, std::wstring& title, std::wstring& text) {
    HWND targetCtrl = GetDlgItem(hwnd, IDC_TARGET);
    std::wstring target = GetWindowTextString(targetCtrl);
    title.clear();
    text.clear();

    if (IsSystemKeyKind(kind)) {
        title = kind;
        text = (kind == L"Screenshot" || kind == L"スクリーンショット") ? L"SS" : L"";
    } else if (kind == L"メディアコントロール") {
        if (target.empty()) return;
        struct TmpMediaPreset {
            const wchar_t* title;
            const wchar_t* target;
            const wchar_t* badge;
        };
        static const TmpMediaPreset tmpPresets[] = {
            { L"音量アップ", L"VOLUME_UP", L"VOL+" },
            { L"音量ダウン", L"VOLUME_DOWN", L"VOL-" },
            { L"音量アップ++", L"VOLUME_UP_FAST", L"VOL++" },
            { L"音量ダウン--", L"VOLUME_DOWN_FAST", L"VOL--" },
            { L"ミュート", L"VOLUME_MUTE", L"MUTE" },
            { L"再生/一時停止", L"MEDIA_PLAY_PAUSE", L"PLAY" },
            { L"次のトラック", L"MEDIA_NEXT_TRACK", L"NEXT" },
            { L"前のトラック", L"MEDIA_PREV_TRACK", L"PREV" },
            { L"停止", L"MEDIA_STOP", L"STOP" }
        };
        for (const auto& preset : tmpPresets) {
            if (target == preset.target) {
                title = preset.title;
                text = preset.badge;
                return;
            }
        }
        title = L"メディアコントロール";
        text = L"MEDIA";
    } else if (kind == L"URL") {
        if (target.empty()) return;
        std::wstring host = HostFromUrl(target);
        title = host.empty() ? target : host;
        text = L"URL";
    } else if (kind == L"Windows Settings") {
        if (target.empty()) return;
        title = L"Windows Settings";
        text = L"SET";
    } else if (kind == L"App (.exe)" || kind == L"File" || kind == L"Folder") {
        if (target.empty()) return;
        title = FileNameFromPath(target, kind != L"Folder");
        text = TextBadgeFromTitle(title, kind == L"App (.exe)" ? L"APP" : L"FILE");
    } else if (kind == L"Keys") {
        if (target.empty()) return;
        title = CompactKeySpec(target);
        text = KeyBadgeFromSpec(target);
    }
}

static void ApplyDisplayDefaults(HWND hwnd, const std::wstring& kind, bool force) {
    std::wstring title;
    std::wstring text;
    ComputeDisplayDefaults(hwnd, kind, title, text);
    if (title.empty() && text.empty()) return;

    HWND titleCtrl = GetDlgItem(hwnd, IDC_TITLE);
    HWND textCtrl = GetDlgItem(hwnd, IDC_TEXT);
    if (force || GetWindowTextLengthW(titleCtrl) == 0) SetWindowTextW(titleCtrl, title.c_str());
    if (force || GetWindowTextLengthW(textCtrl) == 0) SetWindowTextW(textCtrl, text.c_str());
    if (force) SetWindowTextW(GetDlgItem(hwnd, IDC_IMAGE), L"");
}

static void ImportFavoriteUrl(HWND hwnd) {
    std::vector<FavoriteLink> links = LoadBrowserFavorites();
    if (links.empty()) {
        MessageBoxW(hwnd, L"No browser favorites were found.", L"Import favorites", MB_OK | MB_ICONINFORMATION);
        return;
    }

    HMENU menu = CreatePopupMenu();
    const int maxItems = std::min<int>(static_cast<int>(links.size()), 40);
    for (int i = 0; i < maxItems; ++i) {
        std::wstring label = links[i].title.empty() ? links[i].url : links[i].title;
        if (label.size() > 72) label = label.substr(0, 69) + L"...";
        AppendMenuW(menu, MF_STRING, 5000 + i, label.c_str());
    }
    RECT rc{};
    GetWindowRect(GetDlgItem(hwnd, IDC_URL_IMPORT), &rc);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, rc.left, rc.bottom, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd >= 5000 && cmd < 5000 + maxItems) {
        const FavoriteLink& link = links[cmd - 5000];
        SetWindowTextW(GetDlgItem(hwnd, IDC_TARGET), link.url.c_str());
        std::wstring title = link.title.empty() ? HostFromUrl(link.url) : link.title;
        if (title.empty()) title = link.url;
        SetWindowTextW(GetDlgItem(hwnd, IDC_TITLE), title.c_str());
        SetWindowTextW(GetDlgItem(hwnd, IDC_TEXT), L"URL");
        SetWindowTextW(GetDlgItem(hwnd, IDC_IMAGE), L"");
    }
}

struct StartMenuSelectorContext {
    std::vector<AppCandidate> apps;
    int selectedIndex = -1;
    bool accepted = false;
};

static constexpr int IDC_STARTMENU_LIST = 4001;

static void RunOwnedModal(HWND dialog);

static LRESULT CALLBACK StartMenuProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    StartMenuSelectorContext* ctx = reinterpret_cast<StartMenuSelectorContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        ctx = reinterpret_cast<StartMenuSelectorContext*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
        
        HWND list = CreateWindowExW(0, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER | LBS_NOTIFY | LBS_HASSTRINGS | LBS_WANTKEYBOARDINPUT,
            20, 20, 440, 360, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STARTMENU_LIST)), g.instance, nullptr);
        
        SendMessageW(list, WM_SETFONT, reinterpret_cast<WPARAM>(g.uiFont), FALSE);
        
        for (const auto& app : ctx->apps) {
            std::wstring label = app.title.empty() ? app.target : app.title;
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        
        AddButton(hwnd, IDOK, L"OK", 250, 400, 100, 34, BS_DEFPUSHBUTTON);
        AddButton(hwnd, IDCANCEL, L"Cancel", 360, 400, 100, 34);
        
        SetFocus(list);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_STARTMENU_LIST) {
            if (HIWORD(wp) == LBN_DBLCLK) {
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
            }
            return 0;
        }
        if (LOWORD(wp) == IDOK && ctx) {
            HWND list = GetDlgItem(hwnd, IDC_STARTMENU_LIST);
            int sel = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
            if (sel >= 0 && sel < static_cast<int>(ctx->apps.size())) {
                ctx->selectedIndex = sel;
                ctx->accepted = true;
                DestroyWindow(hwnd);
            }
            return 0;
        }
        if (LOWORD(wp) == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        return DialogControlColor(wp);
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        return DialogFieldColor(wp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void ImportStartMenuApp(HWND hwnd) {
    std::vector<AppCandidate> apps = LoadStartMenuApps();
    if (apps.empty()) {
        MessageBoxW(hwnd, L"No Start menu applications were found.", L"Start menu", MB_OK | MB_ICONINFORMATION);
        return;
    }

    StartMenuSelectorContext ctx{};
    ctx.apps = std::move(apps);

    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = StartMenuProc;
        wc.hInstance = g.instance;
        wc.lpszClassName = L"LauncherStartMenuSelector";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = DialogBrush();
        RegisterClassW(&wc);
        registered = true;
    }

    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, L"LauncherStartMenuSelector", L"Start menu",
        WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 500, 500,
        hwnd, nullptr, g.instance, &ctx);
    
    RunOwnedModal(dialog);

    if (ctx.accepted && ctx.selectedIndex >= 0 && ctx.selectedIndex < static_cast<int>(ctx.apps.size())) {
        const AppCandidate& app = ctx.apps[ctx.selectedIndex];
        SetWindowTextW(GetDlgItem(hwnd, IDC_TARGET), app.target.c_str());
        SetWindowTextW(GetDlgItem(hwnd, IDC_ARGS), app.args.c_str());
        if (!app.iconPath.empty() && _wcsicmp(PathFindExtensionW(app.iconPath.c_str()), L".exe") != 0 && _wcsicmp(PathFindExtensionW(app.iconPath.c_str()), L".dll") != 0) {
            SetWindowTextW(GetDlgItem(hwnd, IDC_IMAGE), app.iconPath.c_str());
        } else {
            SetWindowTextW(GetDlgItem(hwnd, IDC_IMAGE), L"");
        }
        std::wstring title = app.title.empty() ? FileNameFromPath(app.target, true) : app.title;
        SetWindowTextW(GetDlgItem(hwnd, IDC_TITLE), title.c_str());
        std::wstring text = TextBadgeFromTitle(title, L"APP");
        SetWindowTextW(GetDlgItem(hwnd, IDC_TEXT), text.c_str());
    }
}

struct SettingsPreset {
    const wchar_t* title;
    const wchar_t* uri;
    const wchar_t* badge;
};

static const SettingsPreset kSettingsPresets[] = {
    { L"Home", L"ms-settings:", L"SET" },
    { L"Display", L"ms-settings:display", L"DIS" },
    { L"Sound", L"ms-settings:sound", L"SND" },
    { L"Bluetooth & devices", L"ms-settings:bluetooth", L"BT" },
    { L"Network & internet", L"ms-settings:network", L"NET" },
    { L"Wi-Fi", L"ms-settings:network-wifi", L"WIFI" },
    { L"Apps", L"ms-settings:appsfeatures", L"APP" },
    { L"Default apps", L"ms-settings:defaultapps", L"DEF" },
    { L"Accounts", L"ms-settings:yourinfo", L"ACC" },
    { L"Time & language", L"ms-settings:dateandtime", L"TIME" },
    { L"Personalization", L"ms-settings:personalization", L"PRS" },
    { L"Windows Update", L"ms-settings:windowsupdate", L"UPD" },
    { L"Privacy & security", L"ms-settings:privacy", L"SEC" },
    { L"Storage", L"ms-settings:storagesense", L"STO" },
    { L"Power & battery", L"ms-settings:powersleep", L"PWR" }
};

static void ChooseWindowsSetting(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    const int count = static_cast<int>(sizeof(kSettingsPresets) / sizeof(kSettingsPresets[0]));
    for (int i = 0; i < count; ++i) {
        AppendMenuW(menu, MF_STRING, 7000 + i, kSettingsPresets[i].title);
    }
    RECT rc{};
    GetWindowRect(GetDlgItem(hwnd, IDC_URL_IMPORT), &rc);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, rc.left, rc.bottom, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd >= 7000 && cmd < 7000 + count) {
        const SettingsPreset& preset = kSettingsPresets[cmd - 7000];
        SetWindowTextW(GetDlgItem(hwnd, IDC_TARGET), preset.uri);
        SetWindowTextW(GetDlgItem(hwnd, IDC_TITLE), preset.title);
        SetWindowTextW(GetDlgItem(hwnd, IDC_TEXT), preset.badge);
        SetWindowTextW(GetDlgItem(hwnd, IDC_IMAGE), L"");
    }
}

struct MediaPreset {
    const wchar_t* title;
    const wchar_t* target;
    const wchar_t* badge;
};

static const MediaPreset kMediaPresets[] = {
    { L"音量アップ", L"VOLUME_UP", L"VOL+" },
    { L"音量ダウン", L"VOLUME_DOWN", L"VOL-" },
    { L"音量アップ++", L"VOLUME_UP_FAST", L"VOL++" },
    { L"音量ダウン--", L"VOLUME_DOWN_FAST", L"VOL--" },
    { L"ミュート", L"VOLUME_MUTE", L"MUTE" },
    { L"再生/一時停止", L"MEDIA_PLAY_PAUSE", L"PLAY" },
    { L"次のトラック", L"MEDIA_NEXT_TRACK", L"NEXT" },
    { L"前のトラック", L"MEDIA_PREV_TRACK", L"PREV" },
    { L"停止", L"MEDIA_STOP", L"STOP" }
};

static void ChooseMediaControl(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    const int count = static_cast<int>(sizeof(kMediaPresets) / sizeof(kMediaPresets[0]));
    for (int i = 0; i < count; ++i) {
        AppendMenuW(menu, MF_STRING, 8000 + i, kMediaPresets[i].title);
    }
    RECT rc{};
    GetWindowRect(GetDlgItem(hwnd, IDC_URL_IMPORT), &rc);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, rc.left, rc.bottom, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd >= 8000 && cmd < 8000 + count) {
        const MediaPreset& preset = kMediaPresets[cmd - 8000];
        SetWindowTextW(GetDlgItem(hwnd, IDC_TARGET), preset.target);
        SetWindowTextW(GetDlgItem(hwnd, IDC_TITLE), preset.title);
        SetWindowTextW(GetDlgItem(hwnd, IDC_TEXT), preset.badge);
        SetWindowTextW(GetDlgItem(hwnd, IDC_IMAGE), L"");
    }
}

static void FillDisplayDefaults(HWND hwnd, const std::wstring& kind) {
    ApplyDisplayDefaults(hwnd, kind, false);
}

struct KeyChoice {
    const wchar_t* label101;
    const wchar_t* label106;
    const wchar_t* token;
    int col;
    int row;
    int units;
};

static const KeyChoice kKeyChoices[] = {
    { L"Esc", L"Esc", L"ESC", 0, 0, 1 }, { L"F1", L"F1", L"F1", 2, 0, 1 }, { L"F2", L"F2", L"F2", 3, 0, 1 }, { L"F3", L"F3", L"F3", 4, 0, 1 },
    { L"F4", L"F4", L"F4", 5, 0, 1 }, { L"F5", L"F5", L"F5", 7, 0, 1 }, { L"F6", L"F6", L"F6", 8, 0, 1 }, { L"F7", L"F7", L"F7", 9, 0, 1 },
    { L"F8", L"F8", L"F8", 10, 0, 1 }, { L"F9", L"F9", L"F9", 12, 0, 1 }, { L"F10", L"F10", L"F10", 13, 0, 1 }, { L"F11", L"F11", L"F11", 14, 0, 1 },
    { L"F12", L"F12", L"F12", 15, 0, 1 }, { L"Prt", L"Prt", L"PRINTSCREEN", 17, 0, 1 }, { L"Scr", L"Scr", L"SCROLL_LOCK", 18, 0, 1 }, { L"Pau", L"Pau", L"PAUSE", 19, 0, 1 },

    { L"`", L"半/全", L"OEM_3", 0, 1, 1 }, { L"1", L"1", L"1", 1, 1, 1 }, { L"2", L"2", L"2", 2, 1, 1 }, { L"3", L"3", L"3", 3, 1, 1 },
    { L"4", L"4", L"4", 4, 1, 1 }, { L"5", L"5", L"5", 5, 1, 1 }, { L"6", L"6", L"6", 6, 1, 1 }, { L"7", L"7", L"7", 7, 1, 1 },
    { L"8", L"8", L"8", 8, 1, 1 }, { L"9", L"9", L"9", 9, 1, 1 }, { L"0", L"0", L"0", 10, 1, 1 }, { L"-", L"-", L"OEM_MINUS", 11, 1, 1 },
    { L"=", L"^", L"OEM_PLUS", 12, 1, 1 }, { L"Back", L"Back", L"BACKSPACE", 13, 1, 2 },
    { L"Ins", L"Ins", L"INSERT", 17, 1, 1 }, { L"Hm", L"Hm", L"HOME", 18, 1, 1 }, { L"PU", L"PU", L"PAGEUP", 19, 1, 1 },
    { L"Num", L"Num", L"NUM_LOCK", 21, 1, 1 }, { L"/", L"/", L"NUMDIV", 22, 1, 1 }, { L"*", L"*", L"NUMMUL", 23, 1, 1 }, { L"-", L"-", L"NUMSUB", 24, 1, 1 },

    { L"Tab", L"Tab", L"TAB", 0, 2, 2 }, { L"Q", L"Q", L"Q", 2, 2, 1 }, { L"W", L"W", L"W", 3, 2, 1 }, { L"E", L"E", L"E", 4, 2, 1 },
    { L"R", L"R", L"R", 5, 2, 1 }, { L"T", L"T", L"T", 6, 2, 1 }, { L"Y", L"Y", L"Y", 7, 2, 1 }, { L"U", L"U", L"U", 8, 2, 1 },
    { L"I", L"I", L"I", 9, 2, 1 }, { L"O", L"O", L"O", 10, 2, 1 }, { L"P", L"P", L"P", 11, 2, 1 }, { L"[", L"@", L"OEM_4", 12, 2, 1 },
    { L"]", L"[", L"OEM_6", 13, 2, 1 }, { L"\\", L"]", L"OEM_5", 14, 2, 1 },
    { L"Del", L"Del", L"DELETE", 17, 2, 1 }, { L"End", L"End", L"END", 18, 2, 1 }, { L"PD", L"PD", L"PAGEDOWN", 19, 2, 1 },
    { L"7", L"7", L"NUM7", 21, 2, 1 }, { L"8", L"8", L"NUM8", 22, 2, 1 }, { L"9", L"9", L"NUM9", 23, 2, 1 }, { L"+", L"+", L"NUMADD", 24, 2, 1 },

    { L"Caps", L"Caps", L"CAPS_LOCK", 0, 3, 2 }, { L"A", L"A", L"A", 2, 3, 1 }, { L"S", L"S", L"S", 3, 3, 1 }, { L"D", L"D", L"D", 4, 3, 1 },
    { L"F", L"F", L"F", 5, 3, 1 }, { L"G", L"G", L"G", 6, 3, 1 }, { L"H", L"H", L"H", 7, 3, 1 }, { L"J", L"J", L"J", 8, 3, 1 },
    { L"K", L"K", L"K", 9, 3, 1 }, { L"L", L"L", L"L", 10, 3, 1 }, { L";", L";", L"OEM_1", 11, 3, 1 }, { L"'", L":", L"OEM_7", 12, 3, 1 },
    { L"Enter", L"Enter", L"ENTER", 13, 3, 2 },
    { L"4", L"4", L"NUM4", 21, 3, 1 }, { L"5", L"5", L"NUM5", 22, 3, 1 }, { L"6", L"6", L"NUM6", 23, 3, 1 },

    { L"Shift", L"Shift", L"SHIFT", 0, 4, 2 }, { L"Z", L"Z", L"Z", 2, 4, 1 }, { L"X", L"X", L"X", 3, 4, 1 }, { L"C", L"C", L"C", 4, 4, 1 },
    { L"V", L"V", L"V", 5, 4, 1 }, { L"B", L"B", L"B", 6, 4, 1 }, { L"N", L"N", L"N", 7, 4, 1 }, { L"M", L"M", L"M", 8, 4, 1 },
    { L",", L",", L"OEM_COMMA", 9, 4, 1 }, { L".", L".", L"OEM_PERIOD", 10, 4, 1 }, { L"/", L"/", L"OEM_2", 11, 4, 1 },
    { L"Intl", L"\\", L"OEM_102", 12, 4, 1 }, { L"Shift", L"Shift", L"SHIFT", 13, 4, 2 },
    { L"Up", L"Up", L"UP", 18, 4, 1 }, { L"1", L"1", L"NUM1", 21, 4, 1 }, { L"2", L"2", L"NUM2", 22, 4, 1 }, { L"3", L"3", L"NUM3", 23, 4, 1 },

    { L"Ctrl", L"Ctrl", L"CTRL", 0, 5, 2 }, { L"Win", L"Win", L"WIN", 2, 5, 1 }, { L"Alt", L"Alt", L"ALT", 3, 5, 2 },
    { L"Space", L"Space", L"SPACE", 5, 5, 6 }, { L"Alt", L"Alt", L"ALT", 11, 5, 2 }, { L"Menu", L"Menu", L"MENU", 13, 5, 1 }, { L"Ctrl", L"Ctrl", L"CTRL", 14, 5, 1 },
    { L"Lt", L"Lt", L"LEFT", 17, 5, 1 }, { L"Dn", L"Dn", L"DOWN", 18, 5, 1 }, { L"Rt", L"Rt", L"RIGHT", 19, 5, 1 },
    { L"0", L"0", L"NUM0", 21, 5, 2 }, { L".", L".", L"NUMDECIMAL", 23, 5, 1 }
};

struct KeyPickerContext {
    std::wstring spec;
    int keyboardLayout = 106;
    bool accepted = false;
};

static const wchar_t* KeyPickerLabel(const KeyChoice& key, int keyboardLayout) {
    return keyboardLayout == 101 ? key.label101 : key.label106;
}

static void RefreshKeyPickerLayout(HWND hwnd, int keyboardLayout) {
    for (int i = 0; i < static_cast<int>(sizeof(kKeyChoices) / sizeof(kKeyChoices[0])); ++i) {
        SetWindowTextW(GetDlgItem(hwnd, IDC_KEY_BUTTON_BASE + i), KeyPickerLabel(kKeyChoices[i], keyboardLayout));
    }
}

static std::wstring KeyPickerChord(HWND hwnd, const wchar_t* token) {
    std::wstring chord;
    if (SendMessageW(GetDlgItem(hwnd, IDC_KEY_CTRL), BM_GETCHECK, 0, 0) == BST_CHECKED) chord += L"CTRL+";
    if (SendMessageW(GetDlgItem(hwnd, IDC_KEY_ALT), BM_GETCHECK, 0, 0) == BST_CHECKED) chord += L"ALT+";
    if (SendMessageW(GetDlgItem(hwnd, IDC_KEY_SHIFT), BM_GETCHECK, 0, 0) == BST_CHECKED) chord += L"SHIFT+";
    if (SendMessageW(GetDlgItem(hwnd, IDC_KEY_WIN), BM_GETCHECK, 0, 0) == BST_CHECKED) chord += L"WIN+";
    chord += token;
    return chord;
}

static bool KeyPickerSequenceMode(HWND hwnd) {
    return ComboText(GetDlgItem(hwnd, IDC_KEY_MODE)) == L"順次押下";
}

static void KeyPickerAddSpec(HWND hwnd, const std::wstring& chord) {
    HWND edit = GetDlgItem(hwnd, IDC_KEY_SPEC);
    std::wstring current = Trim(GetWindowTextString(edit));
    if (!KeyPickerSequenceMode(hwnd)) {
        SetWindowTextW(edit, chord.c_str());
        return;
    }
    if (current.rfind(L"SEQ:", 0) != 0) current = L"SEQ:";
    if (current.size() > 4) current += L",";
    current += chord;
    SetWindowTextW(edit, current.c_str());
}

static void AddCheckWithReadableLabel(HWND hwnd, int id, const wchar_t* text, int x, int y, int labelWidth) {
    AddButton(hwnd, id, L"", x, y + 2, 22, 22, BS_AUTOCHECKBOX);
    AddLabel(hwnd, text, x + 28, y, labelWidth, 28);
}

static LRESULT CALLBACK KeyPickerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    KeyPickerContext* ctx = reinterpret_cast<KeyPickerContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        ctx = reinterpret_cast<KeyPickerContext*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
        AddLabel(hwnd, L"配列", 24, 22, 42, 24);
        HWND layout = AddCombo(hwnd, IDC_KEY_LAYOUT, 70, 18, 112, 160);
        SendMessageW(layout, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"106"));
        SendMessageW(layout, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"101"));
        SendMessageW(layout, CB_SETCURSEL, ctx->keyboardLayout == 101 ? 1 : 0, 0);
        AddLabel(hwnd, L"入力方式", 204, 22, 80, 24);
        HWND mode = AddCombo(hwnd, IDC_KEY_MODE, 284, 18, 160, 160);
        SendMessageW(mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"同時押下"));
        SendMessageW(mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"順次押下"));
        SendMessageW(mode, CB_SETCURSEL, IsSequenceKeySpec(ctx->spec) ? 1 : 0, 0);
        AddCheckWithReadableLabel(hwnd, IDC_KEY_CTRL, L"Ctrl", 466, 18, 48);
        AddCheckWithReadableLabel(hwnd, IDC_KEY_ALT, L"Alt", 546, 18, 42);
        AddCheckWithReadableLabel(hwnd, IDC_KEY_SHIFT, L"Shift", 626, 18, 58);
        AddCheckWithReadableLabel(hwnd, IDC_KEY_WIN, L"Win", 732, 18, 42);
        AddEdit(hwnd, IDC_KEY_SPEC, ctx->spec, 24, 62, 760, 34);
        AddButton(hwnd, IDC_KEY_CLEAR, L"クリア", 800, 62, 92, 34);
        AddButton(hwnd, IDC_KEY_ALTTAB_NEXT, L"Alt+Tab切替", 908, 62, 132, 34);
        AddButton(hwnd, IDC_KEY_ALTTAB_PREV, L"Alt+Shift+Tab切替", 1052, 62, 172, 34);

        for (int i = 0; i < static_cast<int>(sizeof(kKeyChoices) / sizeof(kKeyChoices[0])); ++i) {
            const int unit = 46;
            const int h = 34;
            int x = 24 + kKeyChoices[i].col * unit;
            int y = 116 + kKeyChoices[i].row * 42;
            int w = kKeyChoices[i].units * unit - 4;
            HWND keyButton = AddButton(hwnd, IDC_KEY_BUTTON_BASE + i, KeyPickerLabel(kKeyChoices[i], ctx->keyboardLayout), x, y, w, h);
            if (g.keyFont) SendMessageW(keyButton, WM_SETFONT, reinterpret_cast<WPARAM>(g.keyFont), TRUE);
        }
        AddButton(hwnd, IDOK, L"OK", 1030, 590, 96, 38, BS_DEFPUSHBUTTON);
        AddButton(hwnd, IDCANCEL, L"キャンセル", 1138, 590, 116, 38);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id >= IDC_KEY_BUTTON_BASE && id < IDC_KEY_BUTTON_BASE + static_cast<int>(sizeof(kKeyChoices) / sizeof(kKeyChoices[0]))) {
            const KeyChoice& key = kKeyChoices[id - IDC_KEY_BUTTON_BASE];
            KeyPickerAddSpec(hwnd, KeyPickerChord(hwnd, key.token));
            return 0;
        }
        if (id == IDC_KEY_CLEAR) {
            SetWindowTextW(GetDlgItem(hwnd, IDC_KEY_SPEC), L"");
            return 0;
        }
        if (id == IDC_KEY_ALTTAB_NEXT || id == IDC_KEY_ALTTAB_PREV) {
            SetWindowTextW(GetDlgItem(hwnd, IDC_KEY_SPEC), id == IDC_KEY_ALTTAB_NEXT ? L"ALT_TAB_NEXT" : L"ALT_TAB_PREV");
            return 0;
        }
        if (id == IDC_KEY_LAYOUT && HIWORD(wp) == CBN_SELCHANGE && ctx) {
            ctx->keyboardLayout = ComboText(GetDlgItem(hwnd, IDC_KEY_LAYOUT)) == L"101" ? 101 : 106;
            RefreshKeyPickerLayout(hwnd, ctx->keyboardLayout);
            return 0;
        }
        if (id == IDOK && ctx) {
            ctx->spec = GetWindowTextString(GetDlgItem(hwnd, IDC_KEY_SPEC));
            ctx->keyboardLayout = ComboText(GetDlgItem(hwnd, IDC_KEY_LAYOUT)) == L"101" ? 101 : 106;
            ctx->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        return DialogControlColor(wp);
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        return DialogFieldColor(wp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static std::wstring ChooseKeySpec(HWND owner, const std::wstring& current) {
    KeyPickerContext ctx{};
    ctx.spec = current;
    ctx.keyboardLayout = g.config.keyboardLayout;
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = KeyPickerProc;
        wc.hInstance = g.instance;
        wc.lpszClassName = L"LauncherKeyPicker";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = DialogBrush();
        RegisterClassW(&wc);
        registered = true;
    }
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, L"LauncherKeyPicker", L"キー選択",
        WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 690,
        owner, nullptr, g.instance, &ctx);
    RunOwnedModal(dialog);
    if (ctx.accepted) {
        g.config.keyboardLayout = ctx.keyboardLayout;
        SaveConfig();
    }
    return ctx.accepted ? ctx.spec : L"";
}

static LRESULT CALLBACK ButtonEditorProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ButtonEditorContext* ctx = reinterpret_cast<ButtonEditorContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        ctx = reinterpret_cast<ButtonEditorContext*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
        AddLabel(hwnd, L"Action", 32, 28, 180, 28);
        AddLabel(hwnd, L"Type", 56, 74, 140, 28);
        HWND combo = AddCombo(hwnd, IDC_ACTION, 220, 70, 700, 340);
        for (const wchar_t* item : { L"URL", L"File", L"App (.exe)", L"Folder", L"Windows Settings", L"メディアコントロール", L"スクリーンショット", L"Command", L"Keys", L"None" }) {
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
        }
        std::wstring kind = ActionKindForButton(ctx->original);
        SendMessageW(combo, CB_SELECTSTRING, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(kind.c_str()));

        AddLabel(hwnd, L"Target", 56, 122, 140, 28, IDC_TARGET_LABEL);
        AddEdit(hwnd, IDC_TARGET, ctx->original.action.target, 220, 118, 500, 34);
        AddButton(hwnd, IDC_TARGET_BROWSE, L"Select", 740, 118, 86, 34);
        AddButton(hwnd, IDC_URL_IMPORT, L"Start menu", 842, 118, 116, 34);
        AddLabel(hwnd, L"Options", 56, 170, 140, 28, IDC_ARGS_LABEL);
        AddEdit(hwnd, IDC_ARGS, ctx->original.action.args, 220, 166, 700, 34);

        AddLabel(hwnd, L"Display", 32, 254, 180, 28);
        AddLabel(hwnd, L"Title", 56, 300, 140, 28);
        AddEdit(hwnd, IDC_TITLE, ctx->original.title, 220, 296, 700, 34);
        AddLabel(hwnd, L"Text", 56, 348, 140, 28);
        AddEdit(hwnd, IDC_TEXT, ctx->original.text, 220, 344, 210, 34);
        AddLabel(hwnd, L"Image", 56, 396, 140, 28);
        AddEdit(hwnd, IDC_IMAGE, ctx->original.imagePath, 220, 392, 588, 34);
        AddButton(hwnd, IDC_BROWSE, L"Browse", 842, 392, 116, 34);

        AddButton(hwnd, IDC_CLEAR_BUTTON, L"Clear", 56, 522, 120, 38);
        AddButton(hwnd, IDOK, L"OK", 754, 522, 96, 38, BS_DEFPUSHBUTTON);
        AddButton(hwnd, IDCANCEL, L"Cancel", 862, 522, 96, 38);
        UpdateButtonEditorFields(hwnd);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_ACTION && HIWORD(wp) == CBN_SELCHANGE) {
            UpdateButtonEditorFields(hwnd);
            std::wstring kind = ComboText(GetDlgItem(hwnd, IDC_ACTION));
            if (IsSystemKeyKind(kind)) ApplyDisplayDefaults(hwnd, kind, true);
            return 0;
        }
        if (LOWORD(wp) == IDC_BROWSE) {
            std::wstring image = BrowseForImage(hwnd);
            if (!image.empty()) SetWindowTextW(GetDlgItem(hwnd, IDC_IMAGE), image.c_str());
            return 0;
        }
        if (LOWORD(wp) == IDC_TARGET_BROWSE) {
            std::wstring kind = ComboText(GetDlgItem(hwnd, IDC_ACTION));
            std::wstring target;
            if (kind == L"App (.exe)") target = BrowseForTarget(hwnd, L"Applications\0*.exe\0All files\0*.*\0");
            else if (kind == L"File") target = BrowseForTarget(hwnd, L"All files\0*.*\0");
            else if (kind == L"Folder") target = BrowseForFolder(hwnd);
            else if (kind == L"Keys") target = ChooseKeySpec(hwnd, GetWindowTextString(GetDlgItem(hwnd, IDC_TARGET)));
            if (!target.empty()) {
                SetWindowTextW(GetDlgItem(hwnd, IDC_TARGET), target.c_str());
                ApplyDisplayDefaults(hwnd, kind, true);
            }
            return 0;
        }
        if (LOWORD(wp) == IDC_URL_IMPORT) {
            std::wstring kind = ComboText(GetDlgItem(hwnd, IDC_ACTION));
            if (kind == L"App (.exe)") ImportStartMenuApp(hwnd);
            else if (kind == L"Windows Settings") ChooseWindowsSetting(hwnd);
            else if (kind == L"メディアコントロール") ChooseMediaControl(hwnd);
            else ImportFavoriteUrl(hwnd);
            return 0;
        }
        if (LOWORD(wp) == IDC_CLEAR_BUTTON && ctx) {
            if (MessageBoxW(hwnd, L"Clear this button assignment and display settings?", L"Clear button", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
                ctx->cleared = true;
                ctx->accepted = true;
                DestroyWindow(hwnd);
            }
            return 0;
        }
        if (LOWORD(wp) == IDOK && ctx) {
            std::wstring kind = ComboText(GetDlgItem(hwnd, IDC_ACTION));
            FillDisplayDefaults(hwnd, kind);
            ctx->target->title = GetWindowTextString(GetDlgItem(hwnd, IDC_TITLE));
            ctx->target->text = GetWindowTextString(GetDlgItem(hwnd, IDC_TEXT));
            ctx->target->imagePath = GetWindowTextString(GetDlgItem(hwnd, IDC_IMAGE));
            if (kind == L"None") ctx->target->action.type = ActionType::None;
            else if (kind == L"Windows Settings") ctx->target->action.type = ActionType::Settings;
            else if (kind == L"Command") ctx->target->action.type = ActionType::Command;
            else if (kind == L"Keys" || IsSystemKeyKind(kind) || kind == L"メディアコントロール") ctx->target->action.type = ActionType::Keys;
            else ctx->target->action.type = ActionType::Open;
            ctx->target->action.target = IsSystemKeyKind(kind) ? SystemKeyTarget(kind) : GetWindowTextString(GetDlgItem(hwnd, IDC_TARGET));
            ctx->target->action.args = GetWindowTextString(GetDlgItem(hwnd, IDC_ARGS));
            ctx->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wp) == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        return DialogControlColor(wp);
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        return DialogFieldColor(wp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void RunOwnedModal(HWND dialog) {
    EnableWindow(g.hwnd, FALSE);
    PlaceDialogNearApp(dialog);
    ApplyDialogDarkMode(dialog);
    ShowWindow(dialog, SW_SHOW);
    MSG msg{};
    while (IsWindow(dialog) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(g.hwnd, TRUE);
    SetForegroundWindow(g.hwnd);
}

static void EditButton(int index) {
    auto& buttons = CurrentButtons();
    ButtonEditorContext ctx{};
    ctx.original = buttons[index];
    ctx.target = &buttons[index];
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = ButtonEditorProc;
        wc.hInstance = g.instance;
        wc.lpszClassName = L"LauncherButtonEditor";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = DialogBrush();
        RegisterClassW(&wc);
        registered = true;
    }
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, L"LauncherButtonEditor", L"Edit Button",
        WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 1010, 650,
        g.hwnd, nullptr, g.instance, &ctx);
    RunOwnedModal(dialog);
    if (ctx.accepted) {
        if (ctx.cleared) {
            buttons[index] = ButtonConfig{};
        }
        SaveConfig();
        RefreshCurrentIcons();
        InvalidateRect(g.hwnd, nullptr, TRUE);
    }
}

struct SettingsEditorContext {
    AppConfig original;
    bool accepted = false;
};

static constexpr wchar_t REG_RUN_KEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static constexpr wchar_t REG_APP_NAME[] = L"AntigravityLauncher";

static bool IsRunOnStartupEnabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t path[MAX_PATH];
        DWORD pathSize = sizeof(path);
        DWORD type;
        if (RegQueryValueExW(hKey, REG_APP_NAME, 0, &type, reinterpret_cast<LPBYTE>(path), &pathSize) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return true;
        }
        RegCloseKey(hKey);
    }
    return false;
}

static void SetRunOnStartup(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_KEY, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            wchar_t path[MAX_PATH];
            GetModuleFileNameW(nullptr, path, MAX_PATH);
            std::wstring command = std::wstring(L"\"") + path + L"\"";
            RegSetValueExW(hKey, REG_APP_NAME, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.length() + 1) * sizeof(wchar_t)));
        } else {
            RegDeleteValueW(hKey, REG_APP_NAME);
        }
        RegCloseKey(hKey);
    }
}

static LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    SettingsEditorContext* ctx = reinterpret_cast<SettingsEditorContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        ctx = reinterpret_cast<SettingsEditorContext*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
        AddLabel(hwnd, L"Layout", 32, 28, 180, 28);
        AddLabel(hwnd, L"Rows", 56, 78, 150, 28);
        AddEdit(hwnd, IDC_ROWS, std::to_wstring(ctx->original.rows), 230, 74, 160, 34);
        AddLabel(hwnd, L"Columns", 56, 126, 150, 28);
        AddEdit(hwnd, IDC_COLS, std::to_wstring(ctx->original.cols), 230, 122, 160, 34);
        AddLabel(hwnd, L"Button size", 56, 174, 150, 28);
        AddEdit(hwnd, IDC_BUTTON_SIZE, std::to_wstring(ctx->original.buttonSize), 230, 170, 160, 34);
        AddLabel(hwnd, L"Gap", 56, 222, 150, 28);
        AddEdit(hwnd, IDC_GAP, std::to_wstring(ctx->original.gap), 230, 218, 160, 34);
        AddCheckWithReadableLabel(hwnd, IDC_TOPMOST, L"Always on top", 230, 278, 220);
        SendMessageW(GetDlgItem(hwnd, IDC_TOPMOST), BM_SETCHECK, ctx->original.alwaysOnTop ? BST_CHECKED : BST_UNCHECKED, 0);
        AddCheckWithReadableLabel(hwnd, IDC_TRAY_ICON, L"Show tray icon", 230, 322, 220);
        SendMessageW(GetDlgItem(hwnd, IDC_TRAY_ICON), BM_SETCHECK, ctx->original.showTrayIcon ? BST_CHECKED : BST_UNCHECKED, 0);
        AddCheckWithReadableLabel(hwnd, IDC_RUN_ON_STARTUP, L"Run on Windows startup", 230, 366, 260);
        SendMessageW(GetDlgItem(hwnd, IDC_RUN_ON_STARTUP), BM_SETCHECK, IsRunOnStartupEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
        AddButton(hwnd, IDOK, L"OK", 344, 424, 96, 38, BS_DEFPUSHBUTTON);
        AddButton(hwnd, IDCANCEL, L"Cancel", 456, 424, 104, 38);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK && ctx) {
            g.config.rows = std::max(1, std::min(10, _wtoi(GetWindowTextString(GetDlgItem(hwnd, IDC_ROWS)).c_str())));
            g.config.cols = std::max(1, std::min(12, _wtoi(GetWindowTextString(GetDlgItem(hwnd, IDC_COLS)).c_str())));
            g.config.buttonSize = std::max(64, std::min(220, _wtoi(GetWindowTextString(GetDlgItem(hwnd, IDC_BUTTON_SIZE)).c_str())));
            g.config.gap = std::max(4, std::min(32, _wtoi(GetWindowTextString(GetDlgItem(hwnd, IDC_GAP)).c_str())));
            g.config.alwaysOnTop = SendMessageW(GetDlgItem(hwnd, IDC_TOPMOST), BM_GETCHECK, 0, 0) == BST_CHECKED;
            g.config.showTrayIcon = SendMessageW(GetDlgItem(hwnd, IDC_TRAY_ICON), BM_GETCHECK, 0, 0) == BST_CHECKED;
            SetRunOnStartup(SendMessageW(GetDlgItem(hwnd, IDC_RUN_ON_STARTUP), BM_GETCHECK, 0, 0) == BST_CHECKED);
            ctx->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wp) == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        return DialogControlColor(wp);
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        return DialogFieldColor(wp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void ShowSettingsDialog() {
    SettingsEditorContext ctx{};
    ctx.original = g.config;
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = SettingsProc;
        wc.hInstance = g.instance;
        wc.lpszClassName = L"LauncherSettingsEditor";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = DialogBrush();
        RegisterClassW(&wc);
        registered = true;
    }
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, L"LauncherSettingsEditor", L"Settings",
        WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 600, 526,
        g.hwnd, nullptr, g.instance, &ctx);
    RunOwnedModal(dialog);
    if (ctx.accepted) {
        CurrentButtons();
        SaveConfig();
        ResizeWindowToGrid();
        ApplyTrayIconSetting();
        RefreshCurrentIcons();
        InvalidateRect(g.hwnd, nullptr, TRUE);
    }
}

struct PageSettingsContext {
    std::vector<int> pages;
    std::map<int, std::wstring> pageNames;
    int selectedPage = 0;
    bool accepted = false;
};

static std::wstring PageNameFromMap(int page, const std::map<int, std::wstring>& pageNames) {
    auto it = pageNames.find(page);
    if (it != pageNames.end()) {
        std::wstring name = Trim(it->second);
        if (!name.empty()) return name;
    }
    return DefaultPageName(page);
}

static int SelectedPageListIndex(HWND hwnd) {
    HWND list = GetDlgItem(hwnd, IDC_PAGE_LIST);
    return static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
}

static void SaveSelectedPageName(HWND hwnd, PageSettingsContext* ctx) {
    if (!ctx || ctx->selectedPage < 0) return;
    ctx->pageNames[ctx->selectedPage] = GetWindowTextString(GetDlgItem(hwnd, IDC_PAGE_NAME));
}

static void SetSelectedPageNameEdit(HWND hwnd, PageSettingsContext* ctx) {
    if (!ctx || ctx->selectedPage < 0) return;
    SetWindowTextW(GetDlgItem(hwnd, IDC_PAGE_NAME), PageNameFromMap(ctx->selectedPage, ctx->pageNames).c_str());
}

static void FillPageList(HWND hwnd, PageSettingsContext* ctx) {
    if (!ctx) return;
    HWND list = GetDlgItem(hwnd, IDC_PAGE_LIST);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    int selectedIndex = 0;
    for (int i = 0; i < static_cast<int>(ctx->pages.size()); ++i) {
        int page = ctx->pages[i];
        std::wstring label = std::to_wstring(i + 1) + L". " + PageNameFromMap(page, ctx->pageNames);
        int item = static_cast<int>(SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())));
        SendMessageW(list, LB_SETITEMDATA, item, static_cast<LPARAM>(page));
        if (page == ctx->selectedPage) selectedIndex = item;
    }
    if (!ctx->pages.empty()) {
        SendMessageW(list, LB_SETCURSEL, selectedIndex, 0);
        ctx->selectedPage = ctx->pages[std::min<int>(selectedIndex, static_cast<int>(ctx->pages.size()) - 1)];
        SetSelectedPageNameEdit(hwnd, ctx);
    }
}

static void SelectPageInSettings(HWND hwnd, PageSettingsContext* ctx, int page) {
    if (!ctx || ctx->pages.empty()) return;
    ctx->selectedPage = page;
    FillPageList(hwnd, ctx);
}

static void ApplyPageSettings(PageSettingsContext* ctx) {
    if (!ctx || ctx->pages.empty()) return;

    std::vector<int> previousPages = ExistingPages();
    for (int page : previousPages) {
        if (std::find(ctx->pages.begin(), ctx->pages.end(), page) == ctx->pages.end()) {
            g.config.pages.erase(page);
            g.config.pageNames.erase(page);
            DeletePageSectionsFromConfigFile(page);
        }
    }
    for (int page : ctx->pages) {
        auto& buttons = g.config.pages[page];
        if (buttons.empty()) buttons.resize(g.config.rows * g.config.cols);
    }

    g.config.pageNames.clear();
    for (int page : ctx->pages) {
        auto it = ctx->pageNames.find(page);
        if (it != ctx->pageNames.end()) {
            g.config.pageNames[page] = it->second;
        }
    }
    g.config.pageOrder = ctx->pages;
    NormalizePageOrder();

    if (std::find(g.config.pageOrder.begin(), g.config.pageOrder.end(), g.currentPage) == g.config.pageOrder.end()) {
        auto it = std::find(g.config.pageOrder.begin(), g.config.pageOrder.end(), ctx->selectedPage);
        g.currentPage = it == g.config.pageOrder.end() ? g.config.pageOrder.front() : ctx->selectedPage;
    }
    CurrentButtons();
}

static LRESULT CALLBACK PageSettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PageSettingsContext* ctx = reinterpret_cast<PageSettingsContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        ctx = reinterpret_cast<PageSettingsContext*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
        AddLabel(hwnd, L"Pages", 32, 24, 180, 28);
        HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS,
            56, 62, 260, 268, hwnd, ControlId(IDC_PAGE_LIST), g.instance, nullptr);
        SetControlFont(list);

        AddButton(hwnd, IDC_ADD_PAGE, L"Add page", 340, 62, 130, 36);
        AddButton(hwnd, IDC_DELETE_PAGE, L"Delete page", 484, 62, 130, 36);
        AddButton(hwnd, IDC_PAGE_MOVE_UP, L"Move up", 340, 112, 130, 36);
        AddButton(hwnd, IDC_PAGE_MOVE_DOWN, L"Move down", 484, 112, 130, 36);

        AddLabel(hwnd, L"Name", 340, 184, 130, 28);
        AddEdit(hwnd, IDC_PAGE_NAME, L"", 340, 222, 274, 34);
        AddButton(hwnd, IDOK, L"OK", 406, 330, 96, 38, BS_DEFPUSHBUTTON);
        AddButton(hwnd, IDCANCEL, L"Cancel", 514, 330, 100, 38);
        FillPageList(hwnd, ctx);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_PAGE_LIST && HIWORD(wp) == LBN_SELCHANGE && ctx) {
            SaveSelectedPageName(hwnd, ctx);
            int sel = SelectedPageListIndex(hwnd);
            if (sel >= 0) {
                ctx->selectedPage = static_cast<int>(SendMessageW(GetDlgItem(hwnd, IDC_PAGE_LIST), LB_GETITEMDATA, sel, 0));
                SetSelectedPageNameEdit(hwnd, ctx);
            }
            return 0;
        }
        if (LOWORD(wp) == IDOK && ctx) {
            SaveSelectedPageName(hwnd, ctx);
            ApplyPageSettings(ctx);
            ctx->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wp) == IDC_DELETE_PAGE && ctx) {
            if (ctx->pages.size() <= 1) {
                MessageBoxW(hwnd, L"The last page cannot be deleted.", L"Delete page", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            SaveSelectedPageName(hwnd, ctx);
            std::wstring message = L"Delete \"" + PageNameFromMap(ctx->selectedPage, ctx->pageNames) + L"\" and all buttons on this page?";
            if (MessageBoxW(hwnd, message.c_str(), L"Delete page", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
                auto it = std::find(ctx->pages.begin(), ctx->pages.end(), ctx->selectedPage);
                if (it != ctx->pages.end()) {
                    size_t index = static_cast<size_t>(std::distance(ctx->pages.begin(), it));
                    ctx->pages.erase(it);
                    ctx->pageNames.erase(ctx->selectedPage);
                    if (index >= ctx->pages.size()) index = ctx->pages.size() - 1;
                    SelectPageInSettings(hwnd, ctx, ctx->pages[index]);
                }
            }
            return 0;
        }
        if (LOWORD(wp) == IDC_ADD_PAGE && ctx) {
            SaveSelectedPageName(hwnd, ctx);
            int newPage = NextPageIndex();
            for (int page : ctx->pages) newPage = std::max(newPage, page + 1);
            auto it = std::find(ctx->pages.begin(), ctx->pages.end(), ctx->selectedPage);
            if (it == ctx->pages.end()) {
                ctx->pages.push_back(newPage);
            } else {
                ctx->pages.insert(it + 1, newPage);
            }
            SelectPageInSettings(hwnd, ctx, newPage);
            return 0;
        }
        if ((LOWORD(wp) == IDC_PAGE_MOVE_UP || LOWORD(wp) == IDC_PAGE_MOVE_DOWN) && ctx) {
            SaveSelectedPageName(hwnd, ctx);
            auto it = std::find(ctx->pages.begin(), ctx->pages.end(), ctx->selectedPage);
            if (it != ctx->pages.end()) {
                if (LOWORD(wp) == IDC_PAGE_MOVE_UP && it != ctx->pages.begin()) {
                    std::iter_swap(it, it - 1);
                } else if (LOWORD(wp) == IDC_PAGE_MOVE_DOWN && it + 1 != ctx->pages.end()) {
                    std::iter_swap(it, it + 1);
                }
                SelectPageInSettings(hwnd, ctx, ctx->selectedPage);
            }
            return 0;
        }
        if (LOWORD(wp) == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        return DialogControlColor(wp);
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        return DialogFieldColor(wp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void ShowPageSettingsDialog() {
    PageSettingsContext ctx{};
    ctx.pages = ExistingPages();
    ctx.pageNames = g.config.pageNames;
    ctx.selectedPage = std::find(ctx.pages.begin(), ctx.pages.end(), g.currentPage) == ctx.pages.end() ? ctx.pages.front() : g.currentPage;
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = PageSettingsProc;
        wc.hInstance = g.instance;
        wc.lpszClassName = L"LauncherPageSettingsEditor";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = DialogBrush();
        RegisterClassW(&wc);
        registered = true;
    }
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, L"LauncherPageSettingsEditor", L"Page Settings",
        WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 660, 430,
        g.hwnd, nullptr, g.instance, &ctx);
    RunOwnedModal(dialog);
    if (ctx.accepted) {
        SaveConfig();
        RefreshCurrentIcons();
        InvalidateRect(g.hwnd, nullptr, TRUE);
    }
}

static void ShowContextMenu(POINT pt, int buttonIndex, bool pageTitle) {
    HMENU menu = CreatePopupMenu();
    if (buttonIndex >= 0) {
        AppendMenuW(menu, MF_STRING, IDM_EDIT_BASE + buttonIndex, L"Edit button");
        AppendMenuW(menu, MF_STRING, IDM_CLEAR_BASE + buttonIndex, L"Clear button");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    if (pageTitle) {
        AppendMenuW(menu, MF_STRING, IDM_PAGE_SETTINGS, L"Page settings");
        AppendMenuW(menu, MF_STRING, IDM_ADD_PAGE, L"Add page");
        AppendMenuW(menu, MF_STRING, IDM_DELETE_PAGE, L"Delete page");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(menu, MF_STRING, IDM_PAGE_PREV, L"Page -");
    AppendMenuW(menu, MF_STRING, IDM_PAGE_NEXT, L"Page +");
    if (!pageTitle) AppendMenuW(menu, MF_STRING, IDM_PAGE_SETTINGS, L"Page settings");
    AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"Settings");
    AppendMenuW(menu, MF_STRING | (g.config.alwaysOnTop ? MF_CHECKED : MF_UNCHECKED), IDM_ALWAYS_ON_TOP, L"Always on top");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g.hwnd, nullptr);
    DestroyMenu(menu);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g.hwnd = hwnd;
        INITCOMMONCONTROLSEX icex{};
        icex.dwSize = sizeof(icex);
        icex.dwICC = ICC_WIN95_CLASSES;
        InitCommonControlsEx(&icex);

        g.tooltipHwnd = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd, nullptr, g.instance, nullptr);
        
        ResizeWindowToGrid();
        ApplyTrayIconSetting();
        return 0;
    }
    case WM_MOVE:
        RememberWindowPosition();
        return DefWindowProcW(hwnd, msg, wp, lp);
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_TIMER:
        if (wp == IDT_ALT_TAB_RELEASE) {
            ReleaseAltTabHold();
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        Paint(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        RECT prevRect = HeaderPageButtonRect(false);
        RECT nextRect = HeaderPageButtonRect(true);
        RECT settingsRect = HeaderButtonRect(2);
        RECT minimizeRect = HeaderButtonRect(1);
        RECT closeRect = HeaderButtonRect(0);
        if (PtInRect(&prevRect, pt)) {
            MovePage(false);
            return 0;
        }
        if (PtInRect(&nextRect, pt)) {
            MovePage(true);
            return 0;
        }
        if (PtInRect(&settingsRect, pt)) {
            ShowSettingsDialog();
            return 0;
        }
        if (PtInRect(&minimizeRect, pt)) {
            MinimizeMainWindow();
            return 0;
        }
        if (PtInRect(&closeRect, pt)) {
            DestroyWindow(hwnd);
            return 0;
        }
        int index = HitButton(pt);
        if (index >= 0) {
            auto& buttons = CurrentButtons();
            ExecuteAction(buttons[index].action);
        } else if (pt.y < HEADER_HEIGHT) {
            ReleaseCapture();
            SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        } else {
            ClientToScreen(hwnd, &pt);
            ShowContextMenu(pt, -1, false);
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int index = HitButton(pt);
        RECT titleRect = HeaderTitleRect();
        bool pageTitle = index < 0 && PtInRect(&titleRect, pt);
        ClientToScreen(hwnd, &pt);
        ShowContextMenu(pt, index, pageTitle);
        return 0;
    }
    case WM_NOTIFY: {
        auto* hdr = reinterpret_cast<NMHDR*>(lp);
        if (hdr->hwndFrom == g.tooltipHwnd && hdr->code == TTN_GETDISPINFOW) {
            auto* di = reinterpret_cast<NMTTDISPINFOW*>(lp);
            int index = static_cast<int>(di->hdr.idFrom);
            auto& buttons = CurrentButtons();
            if (index >= 0 && index < static_cast<int>(buttons.size())) {
                std::wstring title = buttons[index].title.empty() ? L"Empty" : buttons[index].title;
                RECT r = ButtonRect(index);
                RECT titleRect = r;
                titleRect.top = r.bottom - 24;
                titleRect.left += 6;
                titleRect.right -= 6;
                HDC hdc = GetDC(hwnd);
                bool truncated = IsTextTruncated(hdc, title, titleRect, 9, false);
                ReleaseDC(hwnd, hdc);
                if (truncated) {
                    static std::wstring s_tooltipCache;
                    s_tooltipCache = title;
                    di->lpszText = const_cast<wchar_t*>(s_tooltipCache.c_str());
                } else {
                    di->lpszText = const_cast<wchar_t*>(L"");
                }
            }
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED && g.config.showTrayIcon) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    case WM_TRAYICON:
        if (lp == WM_LBUTTONDBLCLK) {
            RestoreMainWindow();
            return 0;
        }
        if (lp == WM_RBUTTONUP) {
            ShowTrayMenu();
            return 0;
        }
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id >= IDM_EDIT_BASE && id < IDM_EDIT_BASE + 256) EditButton(id - IDM_EDIT_BASE);
        if (id >= IDM_CLEAR_BASE && id < IDM_CLEAR_BASE + 256) {
            if (ConfirmAndClearButton(hwnd, id - IDM_CLEAR_BASE)) {
                SaveConfig();
                RefreshCurrentIcons();
                InvalidateRect(hwnd, nullptr, TRUE);
            }
        }
        else if (id == IDM_RESTORE) RestoreMainWindow();
        else if (id == IDM_PAGE_PREV) MovePage(false);
        else if (id == IDM_PAGE_NEXT) MovePage(true);
        else if (id == IDM_PAGE_SETTINGS) ShowPageSettingsDialog();
        else if (id == IDM_ADD_PAGE) {
            AddPage();
            SaveConfig();
            RefreshCurrentIcons();
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        else if (id == IDM_DELETE_PAGE) {
            if (ConfirmAndDeleteCurrentPage(hwnd)) {
                SaveConfig();
                RefreshCurrentIcons();
                InvalidateRect(hwnd, nullptr, TRUE);
            }
        }
        else if (id == IDM_SETTINGS) ShowSettingsDialog();
        else if (id == IDM_ALWAYS_ON_TOP) {
            g.config.alwaysOnTop = !g.config.alwaysOnTop;
            SaveConfig();
            ResizeWindowToGrid();
        } else if (id == IDM_EXIT) DestroyWindow(hwnd);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        ReleaseAltTabHold();
        RememberWindowPosition();
        UpdateTrayIcon(false);
        SaveConfig();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    EnableDpiAwareness();
    HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g.instance = instance;
    InitUiFont();
    LoadConfig();

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"LauncherWidgetWindow";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    RegisterClassW(&wc);

    g.hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE | (g.config.alwaysOnTop ? WS_EX_TOPMOST : 0),
        wc.lpszClassName,
        L"Launcher Widget",
        WS_POPUP,
        g.config.windowX, g.config.windowY, 420, 280,
        nullptr, nullptr, instance, nullptr);

    if (!g.hwnd) return 1;
    ResizeWindowToGrid();
    RefreshCurrentIcons();
    ShowWindow(g.hwnd, show);
    UpdateWindow(g.hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    for (auto& ic : g.currentIcons) ic.Clear();
    g.currentIcons.clear();
    if (g.keyFont) DeleteObject(g.keyFont);
    if (g.uiFont) DeleteObject(g.uiFont);
    if (SUCCEEDED(comInit)) CoUninitialize();
    return static_cast<int>(msg.wParam);
}
