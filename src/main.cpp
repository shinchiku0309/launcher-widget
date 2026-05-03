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

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")

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
static constexpr int IDM_EDIT_BASE = 2000;

static constexpr int HEADER_HEIGHT = 40;
static constexpr int HEADER_BUTTON_SIZE = 30;
static constexpr int HEADER_BUTTON_GAP = 8;

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
        if (swscanf_s(p, L"Button:%d:%d", &page, &index) != 2 || page < 0 || index < 0) continue;
        auto& buttons = g.config.pages[page];
        if (static_cast<int>(buttons.size()) <= index) buttons.resize(index + 1);
        wchar_t value[2048]{};
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
    RECT rc{ 0, 0,
        g.config.gap + g.config.cols * (g.config.buttonSize + g.config.gap),
        HEADER_HEIGHT + g.config.gap + g.config.rows * (g.config.buttonSize + g.config.gap) };
    AdjustWindowRectEx(&rc, WS_POPUP | WS_THICKFRAME, FALSE, WS_EX_TOOLWINDOW);
    SetWindowPos(g.hwnd, g.config.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0,
        rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOACTIVATE);
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

static bool DrawCustomImage(HDC hdc, const std::wstring& path, RECT rc) {
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
    HBRUSH bg = CreateSolidBrush(RGB(18, 20, 24));
    FillRect(hdc, &client, bg);
    DeleteObject(bg);

    RECT header{ 0, 0, client.right, HEADER_HEIGHT };
    HBRUSH headerBrush = CreateSolidBrush(RGB(30, 34, 40));
    FillRect(hdc, &header, headerBrush);
    DeleteObject(headerBrush);

    RECT headerTitleRect{ 10, 0, client.right - 72, HEADER_HEIGHT };
    DrawCenteredText(hdc, headerTitleRect, L"Launcher", 9, true);

    RECT settingsRect = HeaderButtonRect(1);
    RECT closeRect = HeaderButtonRect(0);
    HBRUSH controlBrush = CreateSolidBrush(RGB(48, 54, 64));
    HPEN controlPen = CreatePen(PS_SOLID, 1, RGB(88, 96, 110));
    HGDIOBJ oldControlBrush = SelectObject(hdc, controlBrush);
    HGDIOBJ oldControlPen = SelectObject(hdc, controlPen);
    RoundRect(hdc, settingsRect.left, settingsRect.top, settingsRect.right, settingsRect.bottom, 6, 6);
    RoundRect(hdc, closeRect.left, closeRect.top, closeRect.right, closeRect.bottom, 6, 6);
    SelectObject(hdc, oldControlBrush);
    SelectObject(hdc, oldControlPen);
    DeleteObject(controlBrush);
    DeleteObject(controlPen);
    DrawCenteredText(hdc, settingsRect, L"SET", 7, true);
    DrawCenteredText(hdc, closeRect, L"X", 10, true);

    auto& buttons = CurrentButtons();
    const int count = g.config.rows * g.config.cols;
    for (int i = 0; i < count; ++i) {
        RECT r = ButtonRect(i);
        HBRUSH brush = CreateSolidBrush(RGB(42, 46, 54));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(74, 80, 92));
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
    HWND combo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        x, y, w, h, parent, ControlId(id), g.instance, nullptr);
    SetControlFont(combo);
    return combo;
}

struct FavoriteLink {
    std::wstring title;
    std::wstring url;
};

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
        if (!url.empty() && (url.rfind(L"http://", 0) == 0 || url.rfind(L"https://", 0) == 0)) {
            links.push_back({ title, url });
        }
        pos = last + 1;
        if (links.size() >= 80) break;
    }
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

static std::wstring ActionKindForButton(const ButtonConfig& button) {
    const Action& action = button.action;
    if (action.type == ActionType::None) return L"None";
    if (action.type == ActionType::Settings) return L"Windows Settings";
    if (action.type == ActionType::Command) return L"Command";
    if (action.type == ActionType::Keys) return L"Keys";
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

    bool needsTarget = kind != L"None";
    bool needsArgs = kind == L"App (.exe)" || kind == L"File" || kind == L"Command";
    bool canBrowse = kind == L"App (.exe)" || kind == L"File" || kind == L"Folder";
    bool canImport = kind == L"URL";

    SetWindowTextW(targetLabel, kind == L"URL" ? L"URL" :
        kind == L"Keys" ? L"Key chord" :
        kind == L"Windows Settings" ? L"Settings URI" :
        kind == L"Command" ? L"Command" :
        kind == L"Folder" ? L"Folder" :
        kind == L"File" ? L"File" : L"Target");
    SetWindowTextW(argsLabel, kind == L"Command" ? L"Arguments" : L"Options");

    SetVisible(targetLabel, needsTarget);
    SetVisible(target, needsTarget);
    SetVisible(argsLabel, needsArgs);
    SetVisible(args, needsArgs);
    SetVisible(browse, canBrowse);
    SetVisible(import, canImport);
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
        if (GetWindowTextLengthW(GetDlgItem(hwnd, IDC_TITLE)) == 0) SetWindowTextW(GetDlgItem(hwnd, IDC_TITLE), link.title.c_str());
        if (GetWindowTextLengthW(GetDlgItem(hwnd, IDC_TEXT)) == 0) SetWindowTextW(GetDlgItem(hwnd, IDC_TEXT), L"URL");
    }
}

static LRESULT CALLBACK ButtonEditorProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ButtonEditorContext* ctx = reinterpret_cast<ButtonEditorContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        ctx = reinterpret_cast<ButtonEditorContext*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
        AddLabel(hwnd, L"Display", 24, 22, 120, 24);
        AddLabel(hwnd, L"Title", 40, 58, 100, 24);
        AddEdit(hwnd, IDC_TITLE, ctx->original.title, 150, 56, 390, 28);
        AddLabel(hwnd, L"Text", 40, 96, 100, 24);
        AddEdit(hwnd, IDC_TEXT, ctx->original.text, 150, 94, 130, 28);
        AddLabel(hwnd, L"Image", 40, 134, 100, 24);
        AddEdit(hwnd, IDC_IMAGE, ctx->original.imagePath, 150, 132, 300, 28);
        AddButton(hwnd, IDC_BROWSE, L"Browse", 462, 132, 78, 28);

        AddLabel(hwnd, L"Action", 24, 184, 120, 24);
        AddLabel(hwnd, L"Type", 40, 220, 100, 24);
        HWND combo = AddCombo(hwnd, IDC_ACTION, 150, 218, 390, 220);
        for (const wchar_t* item : { L"None", L"App (.exe)", L"URL", L"File", L"Folder", L"Windows Settings", L"Command", L"Keys" }) {
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
        }
        std::wstring kind = ActionKindForButton(ctx->original);
        SendMessageW(combo, CB_SELECTSTRING, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(kind.c_str()));

        AddLabel(hwnd, L"Target", 40, 260, 100, 24, IDC_TARGET_LABEL);
        AddEdit(hwnd, IDC_TARGET, ctx->original.action.target, 150, 258, 300, 28);
        AddButton(hwnd, IDC_TARGET_BROWSE, L"Select", 462, 258, 78, 28);
        AddButton(hwnd, IDC_URL_IMPORT, L"Favorites", 462, 258, 90, 28);
        AddLabel(hwnd, L"Options", 40, 298, 100, 24, IDC_ARGS_LABEL);
        AddEdit(hwnd, IDC_ARGS, ctx->original.action.args, 150, 296, 390, 28);

        AddButton(hwnd, IDOK, L"OK", 370, 360, 80, 32, BS_DEFPUSHBUTTON);
        AddButton(hwnd, IDCANCEL, L"Cancel", 460, 360, 80, 32);
        UpdateButtonEditorFields(hwnd);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_ACTION && HIWORD(wp) == CBN_SELCHANGE) {
            UpdateButtonEditorFields(hwnd);
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
                if (GetWindowTextLengthW(GetDlgItem(hwnd, IDC_TITLE)) == 0) {
                    wchar_t name[MAX_PATH]{};
                    wcscpy_s(name, PathFindFileNameW(target.c_str()));
                    PathRemoveExtensionW(name);
                    SetWindowTextW(GetDlgItem(hwnd, IDC_TITLE), name);
                }
            }
            return 0;
        }
        if (LOWORD(wp) == IDC_URL_IMPORT) {
            ImportFavoriteUrl(hwnd);
            return 0;
        }
        if (LOWORD(wp) == IDOK && ctx) {
            ctx->target->title = GetWindowTextString(GetDlgItem(hwnd, IDC_TITLE));
            ctx->target->text = GetWindowTextString(GetDlgItem(hwnd, IDC_TEXT));
            ctx->target->imagePath = GetWindowTextString(GetDlgItem(hwnd, IDC_IMAGE));
            std::wstring kind = ComboText(GetDlgItem(hwnd, IDC_ACTION));
            if (kind == L"None") ctx->target->action.type = ActionType::None;
            else if (kind == L"Windows Settings") ctx->target->action.type = ActionType::Settings;
            else if (kind == L"Command") ctx->target->action.type = ActionType::Command;
            else if (kind == L"Keys") ctx->target->action.type = ActionType::Keys;
            else ctx->target->action.type = ActionType::Open;
            ctx->target->action.target = GetWindowTextString(GetDlgItem(hwnd, IDC_TARGET));
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
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void RunOwnedModal(HWND dialog) {
    EnableWindow(g.hwnd, FALSE);
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
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        registered = true;
    }
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, L"LauncherButtonEditor", L"Edit Button",
        WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 590, 450,
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
        AddLabel(hwnd, L"Layout", 24, 22, 120, 24);
        AddLabel(hwnd, L"Rows", 40, 62, 130, 24);
        AddEdit(hwnd, IDC_ROWS, std::to_wstring(ctx->original.rows), 190, 60, 120, 28);
        AddLabel(hwnd, L"Columns", 40, 102, 130, 24);
        AddEdit(hwnd, IDC_COLS, std::to_wstring(ctx->original.cols), 190, 100, 120, 28);
        AddLabel(hwnd, L"Button size", 40, 142, 130, 24);
        AddEdit(hwnd, IDC_BUTTON_SIZE, std::to_wstring(ctx->original.buttonSize), 190, 140, 120, 28);
        AddLabel(hwnd, L"Gap", 40, 182, 130, 24);
        AddEdit(hwnd, IDC_GAP, std::to_wstring(ctx->original.gap), 190, 180, 120, 28);
        AddButton(hwnd, IDC_TOPMOST, L"Always on top", 190, 226, 180, 30, BS_AUTOCHECKBOX);
        SendMessageW(GetDlgItem(hwnd, IDC_TOPMOST), BM_SETCHECK, ctx->original.alwaysOnTop ? BST_CHECKED : BST_UNCHECKED, 0);
        AddButton(hwnd, IDOK, L"OK", 220, 294, 80, 32, BS_DEFPUSHBUTTON);
        AddButton(hwnd, IDCANCEL, L"Cancel", 310, 294, 90, 32);
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
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        registered = true;
    }
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, L"LauncherSettingsEditor", L"Settings",
        WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 450, 380,
        g.hwnd, nullptr, g.instance, &ctx);
    RunOwnedModal(dialog);
    if (ctx.accepted) {
        CurrentButtons();
        SaveConfig();
        ResizeWindowToGrid();
        InvalidateRect(g.hwnd, nullptr, TRUE);
    }
}

static void ShowContextMenu(POINT pt, int buttonIndex) {
    HMENU menu = CreatePopupMenu();
    if (buttonIndex >= 0) AppendMenuW(menu, MF_STRING, IDM_EDIT_BASE + buttonIndex, L"Edit button");
    AppendMenuW(menu, MF_STRING, IDM_PAGE_PREV, L"Page -");
    AppendMenuW(menu, MF_STRING, IDM_PAGE_NEXT, L"Page +");
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
        RECT settingsRect = HeaderButtonRect(1);
        RECT closeRect = HeaderButtonRect(0);
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
        WS_EX_TOOLWINDOW | (g.config.alwaysOnTop ? WS_EX_TOPMOST : 0),
        wc.lpszClassName,
        L"Launcher Widget",
        WS_POPUP | WS_THICKFRAME,
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
    return static_cast<int>(msg.wParam);
}
