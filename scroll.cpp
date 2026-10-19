#include <windows.h>
#include <shellapi.h>

#define WM_TRAYICON     (WM_USER + 1)
#define ID_TRAY_TOGGLE  1001
#define ID_TRAY_EXIT    1002
#define ID_SCROLL_TIMER 1

// ---------- Настройки ----------
// Сколько "щелчков" колёсика за один шаг (1 щелчок = WHEEL_DELTA = 120)
const int SCROLL_CLICKS = 30;

// Задержка перед началом непрерывной прокрутки при удержании (мс)
const UINT HOLD_DELAY_MS = 300;

// Интервал между щелчками при удержании (мс). Меньше = быстрее.
const UINT REPEAT_INTERVAL_MS = 40;
// --------------------------------

HHOOK g_hook = nullptr;
HWND g_hwnd = nullptr;
NOTIFYICONDATAW g_nid = {};
UINT g_wmTaskbarCreated = 0;

bool g_enabled = true;
bool g_rshiftDown = false;
bool g_rctrlDown = false;
bool g_timerRunning = false;
ULONGLONG g_nextUpTick = 0;
ULONGLONG g_nextDownTick = 0;

void ScrollWheel(int delta)
{
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = static_cast<DWORD>(delta);
    SendInput(1, &input, sizeof(INPUT));
}

void StartScrollTimer()
{
    if (!g_timerRunning)
    {
        SetTimer(g_hwnd, ID_SCROLL_TIMER, 10, nullptr);
        g_timerRunning = true;
    }
}

void StopScrollTimer()
{
    if (g_timerRunning)
    {
        KillTimer(g_hwnd, ID_SCROLL_TIMER);
        g_timerRunning = false;
    }
}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION)
    {
        const KBDLLHOOKSTRUCT* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        const bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        const bool isUp   = (wParam == WM_KEYUP   || wParam == WM_SYSKEYUP);

        if (kb->vkCode == VK_NUMPAD1)
        {
            if (isDown && !g_rshiftDown)
            {
                g_rshiftDown = true;
                if (g_enabled)
                {
                    ScrollWheel(WHEEL_DELTA * SCROLL_CLICKS);   // вверх
                    g_nextUpTick = GetTickCount64() + HOLD_DELAY_MS;
                    StartScrollTimer();
                }
            }
            else if (isUp)
                g_rshiftDown = false;
        }
        else if (kb->vkCode == VK_NUMPAD0)
        {
            if (isDown && !g_rctrlDown)
            {
                g_rctrlDown = true;
                if (g_enabled)
                {
                    ScrollWheel(-WHEEL_DELTA * SCROLL_CLICKS);  // вниз
                    g_nextDownTick = GetTickCount64() + HOLD_DELAY_MS;
                    StartScrollTimer();
                }
            }
            else if (isUp)
                g_rctrlDown = false;
        }
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

void OnScrollTimer()
{
    if (!g_enabled || (!g_rshiftDown && !g_rctrlDown))
    {
        StopScrollTimer();
        return;
    }

    const ULONGLONG now = GetTickCount64();

    if (g_rshiftDown && now >= g_nextUpTick)
    {
        ScrollWheel(WHEEL_DELTA * SCROLL_CLICKS);
        g_nextUpTick = now + REPEAT_INTERVAL_MS;
    }
    if (g_rctrlDown && now >= g_nextDownTick)
    {
        ScrollWheel(-WHEEL_DELTA * SCROLL_CLICKS);
        g_nextDownTick = now + REPEAT_INTERVAL_MS;
    }
}

void UpdateTrayTip()
{
    g_nid.uFlags = NIF_TIP;
    lstrcpyW(g_nid.szTip, g_enabled ? L"Scroll Keys: включено"
                                    : L"Scroll Keys: выключено");
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void AddTrayIcon()
{
    g_nid = {};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    lstrcpyW(g_nid.szTip, g_enabled ? L"Scroll Keys: включено"
                                    : L"Scroll Keys: выключено");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void ShowTrayMenu()
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (g_enabled ? MF_CHECKED : 0),
                ID_TRAY_TOGGLE, L"Включено");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Выход");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, nullptr);
    PostMessage(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == g_wmTaskbarCreated)
    {
        AddTrayIcon();
        return 0;
    }

    switch (msg)
    {
    case WM_TIMER:
        if (wParam == ID_SCROLL_TIMER)
            OnScrollTimer();
        return 0;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU)
            ShowTrayMenu();
        else if (lParam == WM_LBUTTONDBLCLK)
        {
            g_enabled = !g_enabled;
            UpdateTrayTip();
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TRAY_TOGGLE)
        {
            g_enabled = !g_enabled;
            UpdateTrayTip();
        }
        else if (LOWORD(wParam) == ID_TRAY_EXIT)
        {
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_DESTROY:
        StopScrollTimer();
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"ScrollKeysTrayMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
        return 0;

    g_wmTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"ScrollKeysTrayClass";
    RegisterClassW(&wc);

    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"ScrollKeys", 0,
                             0, 0, 0, 0, nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd)
        return 1;

    AddTrayIcon();

    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, hInst, 0);
    if (!g_hook)
    {
        MessageBoxW(nullptr, L"Не удалось установить хук клавиатуры.",
                    L"Scroll Keys", MB_ICONERROR);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        return 1;
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(g_hook);
    if (mutex) CloseHandle(mutex);
    return 0;
}