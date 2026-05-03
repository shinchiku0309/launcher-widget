#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <urlmon.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "urlmon.lib")

using namespace Gdiplus;

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
    bool alwaysOnTop = true;
    std::map<int, std::wstring> pageNames;
    std::map<int, std::vector<ButtonConfig>> pages;
};

struct AppState {
    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
    HFONT uiFont = nullptr;
    ULONG_PTR gdiplusToken = 0;
    AppConfig config;
    int currentPage = 0;
    std::wstring configPath;
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
}

static constexpr int IDM_PAGE_PREV = 1001;
static constexpr int IDM_PAGE_NEXT = 1002;
static constexpr int IDM_SETTINGS = 1003;
static constexpr int IDM_ALWAYS_ON_TOP = 1004;
static constexpr int IDM_EXIT = 1005;
static constexpr int IDM_PAGE_SETTINGS = 1006;
static constexpr int IDM_EDIT_BASE = 2000;

static constexpr int HEADER_HEIGHT = 40;
static constexpr int HEADER_BUTTON_SIZE = 30;
static constexpr int HEADER_BUTTON_GAP = 8;
static constexpr BYTE WINDOW_OPACITY = 232;
static constexpr COLORREF DIALOG_BG = RGB(250, 251, 253);

static HBRUSH DialogBrush() {
    static HBRUSH brush = CreateSolidBrush(DIALOG_BG);
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

static void LoadConfig() {
    g.configPath = GetConfigPath();
    wchar_t sections[32768]{};
    GetPrivateProfileSectionNamesW(sections, 32768, g.configPath.c_str());

    g.config.rows = GetPrivateProfileIntW(L"Window", L"Rows", 3, g.configPath.c_str());
    g.config.cols = GetPrivateProfileIntW(L"Window", L"Cols", 5, g.configPath.c_str());
    g.config.buttonSize = GetPrivateProfileIntW(L"Window", L"ButtonSize", 96, g.configPath.c_str());
    g.config.gap = GetPrivateProfileIntW(L"Window", L"Gap", 10, g.configPath.c_str());
    g.config.alwaysOnTop = GetPrivateProfileIntW(L"Window", L"AlwaysOnTop", 1, g.configPath.c_str()) != 0;
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
}

static void SaveConfig() {
    WritePrivateProfileStringW(L"Window", L"Rows", std::to_wstring(g.config.rows).c_str(), g.configPath.c_str());
    WritePrivateProfileStringW(L"Window", L"Cols", std::to_wstring(g.config.cols).c_str(), g.configPath.c_str());
    WritePrivateProfileStringW(L"Window", L"ButtonSize", std::to_wstring(g.config.buttonSize).c_str(), g.configPath.c_str());
    WritePrivateProfileStringW(L"Window", L"Gap", std::to_wstring(g.config.gap).c_str(), g.configPath.c_str());
    WritePrivateProfileStringW(L"Window", L"AlwaysOnTop", g.config.alwaysOnTop ? L"1" : L"0", g.configPath.c_str());
    WritePrivateProfileStringW(L"Window", L"UiVersion", L"2", g.configPath.c_str());

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

static int HitButton(POINT pt) {
    const int count = g.config.rows * g.config.cols;
    for (int i = 0; i < count; ++i) {
        RECT r = ButtonRect(i);
        if (PtInRect(&r, pt)) return i;
    }
    return -1;
}

static void ResizeWindowToGrid() {
    if (!g.hwnd) return;
    const int width = g.config.gap + g.config.cols * (g.config.buttonSize + g.config.gap);
    const int height = HEADER_HEIGHT + g.config.gap + g.config.rows * (g.config.buttonSize + g.config.gap);
    SetWindowPos(g.hwnd, g.config.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0,
        width, height, SWP_NOMOVE | SWP_NOACTIVATE);
    SetWindowRgn(g.hwnd, CreateRoundRectRgn(0, 0, width + 1, height + 1, 18, 18), TRUE);
    SetLayeredWindowAttributes(g.hwnd, 0, WINDOW_OPACITY, LWA_ALPHA);
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

static bool IsWebUrl(const std::wstring& value);
static std::wstring HostFromUrl(const std::wstring& url);

static bool DrawCustomImage(HDC hdc, const std::wstring& path, RECT rc) {
    if (path.rfind(L"favicon:", 0) == 0) return false;
    if (path.empty() || !PathFileExistsW(path.c_str())) return false;
    const wchar_t* ext = PathFindExtensionW(path.c_str());
    if (_wcsicmp(ext, L".svg") == 0) return false;
    Graphics graphics(hdc);
    Image image(path.c_str());
    if (image.GetLastStatus() != Ok) return false;
    int side = std::min(rc.right - rc.left, rc.bottom - rc.top) - 28;
    if (side <= 8) return false;
    Rect dest(rc.left + ((rc.right - rc.left) - side) / 2, rc.top + 10, side, side);
    graphics.DrawImage(&image, dest);
    return true;
}

static bool DrawUrlFavicon(HDC hdc, const std::wstring& url, RECT rc) {
    if (!IsWebUrl(url)) return false;
    std::wstring host = HostFromUrl(url);
    if (host.empty()) return false;
    std::wstring faviconUrl = L"https://" + host + L"/favicon.ico";
    wchar_t cachePath[MAX_PATH]{};
    if (FAILED(URLDownloadToCacheFileW(nullptr, faviconUrl.c_str(), cachePath, MAX_PATH, 0, nullptr))) return false;
    return DrawCustomImage(hdc, cachePath, rc);
}

static bool DrawShellIcon(HDC hdc, const std::wstring& target, RECT rc) {
    if (target.empty()) return false;
    SHFILEINFOW info{};
    DWORD flags = SHGFI_ICON | SHGFI_LARGEICON;
    if (!PathFileExistsW(target.c_str())) flags |= SHGFI_USEFILEATTRIBUTES;
    if (!SHGetFileInfoW(target.c_str(), FILE_ATTRIBUTE_NORMAL, &info, sizeof(info), flags)) return false;
    int side = std::min(rc.right - rc.left, rc.bottom - rc.top) - 32;
    if (side < 24) side = 24;
    DrawIconEx(hdc, rc.left + ((rc.right - rc.left) - side) / 2, rc.top + 12, info.hIcon, side, side, 0, nullptr, DI_NORMAL);
    DestroyIcon(info.hIcon);
    return true;
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

    RECT prevRect = HeaderButtonRect(3);
    RECT nextRect = HeaderButtonRect(2);
    RECT settingsRect = HeaderButtonRect(1);
    RECT closeRect = HeaderButtonRect(0);
    RECT headerTitleRect{ 10, 0, std::max<LONG>(10, prevRect.left - HEADER_BUTTON_GAP), HEADER_HEIGHT };
    DrawCenteredText(hdc, headerTitleRect, PageName(g.currentPage), 9, true);

    HBRUSH controlBrush = CreateSolidBrush(RGB(37, 43, 52));
    HPEN controlPen = CreatePen(PS_SOLID, 1, RGB(104, 116, 136));
    HGDIOBJ oldControlBrush = SelectObject(hdc, controlBrush);
    HGDIOBJ oldControlPen = SelectObject(hdc, controlPen);
    RoundRect(hdc, prevRect.left, prevRect.top, prevRect.right, prevRect.bottom, 6, 6);
    RoundRect(hdc, nextRect.left, nextRect.top, nextRect.right, nextRect.bottom, 6, 6);
    RoundRect(hdc, settingsRect.left, settingsRect.top, settingsRect.right, settingsRect.bottom, 6, 6);
    RoundRect(hdc, closeRect.left, closeRect.top, closeRect.right, closeRect.bottom, 6, 6);
    SelectObject(hdc, oldControlBrush);
    SelectObject(hdc, oldControlPen);
    DeleteObject(controlBrush);
    DeleteObject(controlPen);
    DrawCenteredText(hdc, prevRect, L"<", 10, true);
    DrawCenteredText(hdc, nextRect, L">", 10, true);
    DrawCenteredText(hdc, settingsRect, L"SET", 7, true);
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
        bool drew = DrawCustomImage(hdc, b.imagePath, r);
        if (!drew && IsWebUrl(b.action.target)) {
            drew = DrawUrlFavicon(hdc, b.action.target, r);
        }
        if (!drew && (b.action.type == ActionType::Open || b.action.type == ActionType::Command)) {
            drew = DrawShellIcon(hdc, b.action.target, r);
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
        SendKeyChord(action.target);
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
    SetTextColor(hdc, RGB(22, 25, 30));
    return reinterpret_cast<LRESULT>(DialogBrush());
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

struct FavoriteLink {
    std::wstring title;
    std::wstring url;
};

struct AppCandidate {
    std::wstring title;
    std::wstring target;
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

static std::wstring ResolveShortcutTarget(const std::wstring& shortcutPath) {
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return L"";
    IPersistFile* file = nullptr;
    std::wstring target;
    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
        if (SUCCEEDED(file->Load(shortcutPath.c_str(), STGM_READ))) {
            wchar_t path[MAX_PATH]{};
            if (SUCCEEDED(link->GetPath(path, MAX_PATH, nullptr, SLGP_UNCPRIORITY)) && path[0]) {
                target = path;
            }
        }
        file->Release();
    }
    link->Release();
    return target;
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
            std::wstring target = ResolveShortcutTarget(path);
            if (!target.empty() && _wcsicmp(PathFindExtensionW(target.c_str()), L".exe") == 0) {
                wchar_t title[MAX_PATH]{};
                wcscpy_s(title, fd.cFileName);
                PathRemoveExtensionW(title);
                apps.push_back({ title, target });
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

static std::vector<AppCandidate> LoadStartMenuApps() {
    std::vector<AppCandidate> apps;
    CollectKnownFolderApps(FOLDERID_StartMenu, apps);
    CollectKnownFolderApps(FOLDERID_CommonStartMenu, apps);
    std::sort(apps.begin(), apps.end(), [](const AppCandidate& a, const AppCandidate& b) {
        return _wcsicmp(a.title.c_str(), b.title.c_str()) < 0;
    });
    apps.erase(std::unique(apps.begin(), apps.end(), [](const AppCandidate& a, const AppCandidate& b) {
        return _wcsicmp(a.target.c_str(), b.target.c_str()) == 0;
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
};

static std::wstring ComboText(HWND combo) {
    wchar_t text[128]{};
    int sel = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (sel >= 0) SendMessageW(combo, CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(text));
    return text;
}

static bool IsSystemKeyKind(const std::wstring& kind) {
    return kind == L"Volume Up" || kind == L"Volume Down" || kind == L"Mute" || kind == L"Screenshot" ||
        kind == L"Play/Pause" || kind == L"Next Track" || kind == L"Previous Track" || kind == L"Stop Media";
}

static std::wstring SystemKeyTarget(const std::wstring& kind) {
    if (kind == L"Volume Up") return L"VOLUME_UP";
    if (kind == L"Volume Down") return L"VOLUME_DOWN";
    if (kind == L"Mute") return L"VOLUME_MUTE";
    if (kind == L"Screenshot") return L"PRINTSCREEN";
    if (kind == L"Play/Pause") return L"MEDIA_PLAY_PAUSE";
    if (kind == L"Next Track") return L"MEDIA_NEXT_TRACK";
    if (kind == L"Previous Track") return L"MEDIA_PREV_TRACK";
    if (kind == L"Stop Media") return L"MEDIA_STOP";
    return L"";
}

static std::wstring ActionKindForButton(const ButtonConfig& button) {
    const Action& action = button.action;
    if (action.type == ActionType::None) return L"None";
    if (action.type == ActionType::Settings) return L"Windows Settings";
    if (action.type == ActionType::Command) return L"Command";
    if (action.type == ActionType::Keys) {
        if (action.target == L"VOLUME_UP") return L"Volume Up";
        if (action.target == L"VOLUME_DOWN") return L"Volume Down";
        if (action.target == L"VOLUME_MUTE") return L"Mute";
        if (action.target == L"PRINTSCREEN") return L"Screenshot";
        if (action.target == L"MEDIA_PLAY_PAUSE") return L"Play/Pause";
        if (action.target == L"MEDIA_NEXT_TRACK") return L"Next Track";
        if (action.target == L"MEDIA_PREV_TRACK") return L"Previous Track";
        if (action.target == L"MEDIA_STOP") return L"Stop Media";
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
    bool canBrowse = kind == L"App (.exe)" || kind == L"File" || kind == L"Folder";
    bool canImport = kind == L"URL" || kind == L"App (.exe)" || kind == L"Windows Settings";

    SetWindowTextW(targetLabel, kind == L"URL" ? L"URL" :
        kind == L"Keys" ? L"Key chord" :
        kind == L"Windows Settings" ? L"Settings" :
        kind == L"Command" ? L"Command" :
        kind == L"Folder" ? L"Folder" :
        kind == L"File" ? L"File" : L"Target");
    SetWindowTextW(argsLabel, kind == L"Command" ? L"Arguments" : L"Options");
    SetWindowTextW(browse, kind == L"Folder" ? L"Folder" : L"Select");
    SetWindowTextW(import, kind == L"App (.exe)" ? L"Start menu" :
        kind == L"Windows Settings" ? L"Choose" : L"Favorites");

    if (kind == L"App (.exe)") {
        MoveWindow(target, 170, 110, 470, 30, TRUE);
        MoveWindow(browse, 658, 110, 82, 30, TRUE);
        MoveWindow(import, 752, 110, 112, 30, TRUE);
    } else if (kind == L"URL") {
        MoveWindow(target, 170, 110, 532, 30, TRUE);
        MoveWindow(import, 752, 110, 112, 30, TRUE);
    } else if (kind == L"Windows Settings") {
        MoveWindow(target, 170, 110, 532, 30, TRUE);
        MoveWindow(import, 752, 110, 112, 30, TRUE);
    } else if (kind == L"File" || kind == L"Folder") {
        MoveWindow(target, 170, 110, 532, 30, TRUE);
        MoveWindow(browse, 752, 110, 112, 30, TRUE);
    } else {
        MoveWindow(target, 170, 110, 660, 30, TRUE);
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
        if (GetWindowTextLengthW(GetDlgItem(hwnd, IDC_TITLE)) == 0) SetWindowTextW(GetDlgItem(hwnd, IDC_TITLE), link.title.c_str());
        if (GetWindowTextLengthW(GetDlgItem(hwnd, IDC_TEXT)) == 0) SetWindowTextW(GetDlgItem(hwnd, IDC_TEXT), L"URL");
        FillDisplayDefaults(hwnd, L"URL");
    }
}

static void ImportStartMenuApp(HWND hwnd) {
    std::vector<AppCandidate> apps = LoadStartMenuApps();
    if (apps.empty()) {
        MessageBoxW(hwnd, L"No Start menu applications were found.", L"Start menu", MB_OK | MB_ICONINFORMATION);
        return;
    }

    HMENU menu = CreatePopupMenu();
    const int maxItems = std::min<int>(static_cast<int>(apps.size()), 80);
    for (int i = 0; i < maxItems; ++i) {
        std::wstring label = apps[i].title.empty() ? apps[i].target : apps[i].title;
        if (label.size() > 72) label = label.substr(0, 69) + L"...";
        AppendMenuW(menu, MF_STRING, 6000 + i, label.c_str());
    }
    RECT rc{};
    GetWindowRect(GetDlgItem(hwnd, IDC_URL_IMPORT), &rc);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, rc.left, rc.bottom, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd >= 6000 && cmd < 6000 + maxItems) {
        const AppCandidate& app = apps[cmd - 6000];
        SetWindowTextW(GetDlgItem(hwnd, IDC_TARGET), app.target.c_str());
        if (GetWindowTextLengthW(GetDlgItem(hwnd, IDC_TITLE)) == 0) SetWindowTextW(GetDlgItem(hwnd, IDC_TITLE), app.title.c_str());
        if (GetWindowTextLengthW(GetDlgItem(hwnd, IDC_TEXT)) == 0) {
            std::wstring text = TextBadgeFromTitle(app.title, L"APP");
            SetWindowTextW(GetDlgItem(hwnd, IDC_TEXT), text.c_str());
        }
        FillDisplayDefaults(hwnd, L"App (.exe)");
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
        if (GetWindowTextLengthW(GetDlgItem(hwnd, IDC_TITLE)) == 0) SetWindowTextW(GetDlgItem(hwnd, IDC_TITLE), preset.title);
        if (GetWindowTextLengthW(GetDlgItem(hwnd, IDC_TEXT)) == 0) SetWindowTextW(GetDlgItem(hwnd, IDC_TEXT), preset.badge);
    }
}

static void FillDisplayDefaults(HWND hwnd, const std::wstring& kind) {
    HWND titleCtrl = GetDlgItem(hwnd, IDC_TITLE);
    HWND textCtrl = GetDlgItem(hwnd, IDC_TEXT);
    HWND targetCtrl = GetDlgItem(hwnd, IDC_TARGET);
    std::wstring target = GetWindowTextString(targetCtrl);

    if (IsSystemKeyKind(kind)) {
        if (GetWindowTextLengthW(titleCtrl) == 0) SetWindowTextW(titleCtrl, kind.c_str());
        if (GetWindowTextLengthW(textCtrl) == 0) {
            std::wstring badge = kind == L"Volume Up" ? L"VOL+" :
                kind == L"Volume Down" ? L"VOL-" :
                kind == L"Mute" ? L"MUTE" :
                kind == L"Screenshot" ? L"SS" :
                kind == L"Play/Pause" ? L"PLAY" :
                kind == L"Next Track" ? L"NEXT" :
                kind == L"Previous Track" ? L"PREV" : L"STOP";
            SetWindowTextW(textCtrl, badge.c_str());
        }
    } else if (kind == L"URL") {
        if (target.empty()) return;
        std::wstring host = HostFromUrl(target);
        if (GetWindowTextLengthW(titleCtrl) == 0) SetWindowTextW(titleCtrl, host.empty() ? target.c_str() : host.c_str());
        if (GetWindowTextLengthW(textCtrl) == 0) SetWindowTextW(textCtrl, L"URL");
    } else if (kind == L"Windows Settings") {
        if (target.empty()) return;
        if (GetWindowTextLengthW(titleCtrl) == 0) SetWindowTextW(titleCtrl, L"Windows Settings");
        if (GetWindowTextLengthW(textCtrl) == 0) SetWindowTextW(textCtrl, L"SET");
    } else if (kind == L"App (.exe)" || kind == L"File" || kind == L"Folder") {
        if (target.empty()) return;
        wchar_t name[MAX_PATH]{};
        wcscpy_s(name, PathFindFileNameW(target.c_str()));
        if (kind != L"Folder") PathRemoveExtensionW(name);
        if (GetWindowTextLengthW(titleCtrl) == 0) SetWindowTextW(titleCtrl, name);
        if (GetWindowTextLengthW(textCtrl) == 0) {
            std::wstring badge = TextBadgeFromTitle(name, kind == L"App (.exe)" ? L"APP" : L"FILE");
            SetWindowTextW(textCtrl, badge.c_str());
        }
    }
}

static LRESULT CALLBACK ButtonEditorProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ButtonEditorContext* ctx = reinterpret_cast<ButtonEditorContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        ctx = reinterpret_cast<ButtonEditorContext*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
        AddLabel(hwnd, L"Action", 28, 24, 140, 24);
        AddLabel(hwnd, L"Type", 44, 64, 110, 24);
        HWND combo = AddCombo(hwnd, IDC_ACTION, 170, 62, 660, 320);
        for (const wchar_t* item : { L"URL", L"File", L"App (.exe)", L"Folder", L"Windows Settings", L"Volume Up", L"Volume Down", L"Mute", L"Play/Pause", L"Next Track", L"Previous Track", L"Stop Media", L"Screenshot", L"Command", L"Keys", L"None" }) {
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
        }
        std::wstring kind = ActionKindForButton(ctx->original);
        SendMessageW(combo, CB_SELECTSTRING, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(kind.c_str()));

        AddLabel(hwnd, L"Target", 44, 112, 110, 24, IDC_TARGET_LABEL);
        AddEdit(hwnd, IDC_TARGET, ctx->original.action.target, 170, 110, 470, 30);
        AddButton(hwnd, IDC_TARGET_BROWSE, L"Select", 658, 110, 82, 30);
        AddButton(hwnd, IDC_URL_IMPORT, L"Start menu", 752, 110, 112, 30);
        AddLabel(hwnd, L"Options", 44, 154, 110, 24, IDC_ARGS_LABEL);
        AddEdit(hwnd, IDC_ARGS, ctx->original.action.args, 170, 152, 660, 30);

        AddLabel(hwnd, L"Display", 28, 228, 140, 24);
        AddLabel(hwnd, L"Title", 44, 268, 110, 24);
        AddEdit(hwnd, IDC_TITLE, ctx->original.title, 170, 266, 660, 30);
        AddLabel(hwnd, L"Text", 44, 310, 110, 24);
        AddEdit(hwnd, IDC_TEXT, ctx->original.text, 170, 308, 180, 30);
        AddLabel(hwnd, L"Image", 44, 352, 110, 24);
        AddEdit(hwnd, IDC_IMAGE, ctx->original.imagePath, 170, 350, 532, 30);
        AddButton(hwnd, IDC_BROWSE, L"Browse", 752, 350, 112, 30);

        AddButton(hwnd, IDOK, L"OK", 672, 456, 90, 34, BS_DEFPUSHBUTTON);
        AddButton(hwnd, IDCANCEL, L"Cancel", 774, 456, 90, 34);
        UpdateButtonEditorFields(hwnd);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_ACTION && HIWORD(wp) == CBN_SELCHANGE) {
            UpdateButtonEditorFields(hwnd);
            std::wstring kind = ComboText(GetDlgItem(hwnd, IDC_ACTION));
            if (IsSystemKeyKind(kind)) FillDisplayDefaults(hwnd, kind);
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
            if (!target.empty()) {
                SetWindowTextW(GetDlgItem(hwnd, IDC_TARGET), target.c_str());
                FillDisplayDefaults(hwnd, kind);
            }
            return 0;
        }
        if (LOWORD(wp) == IDC_URL_IMPORT) {
            std::wstring kind = ComboText(GetDlgItem(hwnd, IDC_ACTION));
            if (kind == L"App (.exe)") ImportStartMenuApp(hwnd);
            else if (kind == L"Windows Settings") ChooseWindowsSetting(hwnd);
            else ImportFavoriteUrl(hwnd);
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
            else if (kind == L"Keys" || IsSystemKeyKind(kind)) ctx->target->action.type = ActionType::Keys;
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
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void RunOwnedModal(HWND dialog) {
    EnableWindow(g.hwnd, FALSE);
    PlaceDialogNearApp(dialog);
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
        WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 910, 550,
        g.hwnd, nullptr, g.instance, &ctx);
    RunOwnedModal(dialog);
    if (ctx.accepted) {
        SaveConfig();
        InvalidateRect(g.hwnd, nullptr, TRUE);
    }
}

struct SettingsEditorContext {
    AppConfig original;
    bool accepted = false;
};

static LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    SettingsEditorContext* ctx = reinterpret_cast<SettingsEditorContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        ctx = reinterpret_cast<SettingsEditorContext*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
        AddLabel(hwnd, L"Layout", 28, 24, 140, 24);
        AddLabel(hwnd, L"Rows", 44, 70, 130, 24);
        AddEdit(hwnd, IDC_ROWS, std::to_wstring(ctx->original.rows), 190, 68, 140, 30);
        AddLabel(hwnd, L"Columns", 44, 112, 130, 24);
        AddEdit(hwnd, IDC_COLS, std::to_wstring(ctx->original.cols), 190, 110, 140, 30);
        AddLabel(hwnd, L"Button size", 44, 154, 130, 24);
        AddEdit(hwnd, IDC_BUTTON_SIZE, std::to_wstring(ctx->original.buttonSize), 190, 152, 140, 30);
        AddLabel(hwnd, L"Gap", 44, 196, 130, 24);
        AddEdit(hwnd, IDC_GAP, std::to_wstring(ctx->original.gap), 190, 194, 140, 30);
        AddButton(hwnd, IDC_TOPMOST, L"Always on top", 190, 246, 190, 32, BS_AUTOCHECKBOX);
        SendMessageW(GetDlgItem(hwnd, IDC_TOPMOST), BM_SETCHECK, ctx->original.alwaysOnTop ? BST_CHECKED : BST_UNCHECKED, 0);
        AddButton(hwnd, IDOK, L"OK", 270, 316, 88, 34, BS_DEFPUSHBUTTON);
        AddButton(hwnd, IDCANCEL, L"Cancel", 370, 316, 92, 34);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK && ctx) {
            g.config.rows = std::max(1, std::min(10, _wtoi(GetWindowTextString(GetDlgItem(hwnd, IDC_ROWS)).c_str())));
            g.config.cols = std::max(1, std::min(12, _wtoi(GetWindowTextString(GetDlgItem(hwnd, IDC_COLS)).c_str())));
            g.config.buttonSize = std::max(64, std::min(220, _wtoi(GetWindowTextString(GetDlgItem(hwnd, IDC_BUTTON_SIZE)).c_str())));
            g.config.gap = std::max(4, std::min(32, _wtoi(GetWindowTextString(GetDlgItem(hwnd, IDC_GAP)).c_str())));
            g.config.alwaysOnTop = SendMessageW(GetDlgItem(hwnd, IDC_TOPMOST), BM_GETCHECK, 0, 0) == BST_CHECKED;
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
        WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 500, 400,
        g.hwnd, nullptr, g.instance, &ctx);
    RunOwnedModal(dialog);
    if (ctx.accepted) {
        CurrentButtons();
        SaveConfig();
        ResizeWindowToGrid();
        InvalidateRect(g.hwnd, nullptr, TRUE);
    }
}

struct PageSettingsContext {
    int pageIndex = 0;
    bool accepted = false;
};

static LRESULT CALLBACK PageSettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PageSettingsContext* ctx = reinterpret_cast<PageSettingsContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        ctx = reinterpret_cast<PageSettingsContext*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
        AddLabel(hwnd, L"Page", 28, 24, 140, 24);
        AddLabel(hwnd, L"Name", 44, 70, 130, 24);
        AddEdit(hwnd, IDC_PAGE_NAME, PageName(ctx->pageIndex), 170, 68, 260, 30);
        AddButton(hwnd, IDOK, L"OK", 240, 136, 88, 34, BS_DEFPUSHBUTTON);
        AddButton(hwnd, IDCANCEL, L"Cancel", 338, 136, 92, 34);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK && ctx) {
            g.config.pageNames[ctx->pageIndex] = GetWindowTextString(GetDlgItem(hwnd, IDC_PAGE_NAME));
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
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void ShowPageSettingsDialog() {
    PageSettingsContext ctx{};
    ctx.pageIndex = g.currentPage;
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
        WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 480, 230,
        g.hwnd, nullptr, g.instance, &ctx);
    RunOwnedModal(dialog);
    if (ctx.accepted) {
        SaveConfig();
        InvalidateRect(g.hwnd, nullptr, TRUE);
    }
}

static void ShowContextMenu(POINT pt, int buttonIndex) {
    HMENU menu = CreatePopupMenu();
    if (buttonIndex >= 0) AppendMenuW(menu, MF_STRING, IDM_EDIT_BASE + buttonIndex, L"Edit button");
    AppendMenuW(menu, MF_STRING, IDM_PAGE_PREV, L"Page -");
    AppendMenuW(menu, MF_STRING, IDM_PAGE_NEXT, L"Page +");
    AppendMenuW(menu, MF_STRING, IDM_PAGE_SETTINGS, L"Page settings");
    AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"Settings");
    AppendMenuW(menu, MF_STRING | (g.config.alwaysOnTop ? MF_CHECKED : MF_UNCHECKED), IDM_ALWAYS_ON_TOP, L"Always on top");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g.hwnd, nullptr);
    DestroyMenu(menu);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g.hwnd = hwnd;
        ResizeWindowToGrid();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        Paint(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        RECT prevRect = HeaderButtonRect(3);
        RECT nextRect = HeaderButtonRect(2);
        RECT settingsRect = HeaderButtonRect(1);
        RECT closeRect = HeaderButtonRect(0);
        if (PtInRect(&prevRect, pt)) {
            if (g.currentPage > 0) --g.currentPage;
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        if (PtInRect(&nextRect, pt)) {
            ++g.currentPage;
            CurrentButtons();
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        if (PtInRect(&settingsRect, pt)) {
            ShowSettingsDialog();
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
            ShowContextMenu(pt, -1);
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int index = HitButton(pt);
        ClientToScreen(hwnd, &pt);
        ShowContextMenu(pt, index);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id >= IDM_EDIT_BASE && id < IDM_EDIT_BASE + 256) EditButton(id - IDM_EDIT_BASE);
        else if (id == IDM_PAGE_PREV && g.currentPage > 0) { --g.currentPage; InvalidateRect(hwnd, nullptr, TRUE); }
        else if (id == IDM_PAGE_NEXT) { ++g.currentPage; CurrentButtons(); InvalidateRect(hwnd, nullptr, TRUE); }
        else if (id == IDM_PAGE_SETTINGS) ShowPageSettingsDialog();
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
    GdiplusStartupInput gdiplusInput;
    GdiplusStartup(&g.gdiplusToken, &gdiplusInput, nullptr);
    LoadConfig();

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"LauncherWidgetWindow";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    g.hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_LAYERED | (g.config.alwaysOnTop ? WS_EX_TOPMOST : 0),
        wc.lpszClassName,
        L"Launcher Widget",
        WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 280,
        nullptr, nullptr, instance, nullptr);

    if (!g.hwnd) return 1;
    ShowWindow(g.hwnd, show);
    UpdateWindow(g.hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g.uiFont) DeleteObject(g.uiFont);
    GdiplusShutdown(g.gdiplusToken);
    if (SUCCEEDED(comInit)) CoUninitialize();
    return static_cast<int>(msg.wParam);
}
