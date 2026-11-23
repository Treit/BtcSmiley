#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define WM_TRAY (WM_APP + 1)
#define WM_QUOTE (WM_APP + 2)
#define ID_BEAR 1
#define ID_BULL 2
#define ID_EXIT 3

typedef struct {
    double price;
    double change;
    BOOL valid;
} Quote;

static NOTIFYICONDATAW tray;
static HICON faces[3];
static Quote current, pending;
static BOOL bull, busy, stale;
static UINT taskbarCreated;

static double PriceField(const char *json, const char *key)
{
    const char *field = strstr(json, key);
    char *end;
    double value;

    if (!field) {
        return 0;
    }

    field += strlen(key);
    field += strspn(field, " \t\r\n");

    if (*field++ != ':') {
        return 0;
    }

    field += strspn(field, " \t\r\n");

    if (*field++ != '"' || *field < '0' || *field > '9') {
        return 0;
    }

    value = strtod(field, &end);
    return *end == '"' && isfinite(value) && value > 0 ? value : 0;
}

static Quote ParseQuote(const char *json)
{
    Quote quote = {0};
    double open = PriceField(json, "\"open\"");
    double last = PriceField(json, "\"last\"");

    if (open > 0 && last > 0) {
        quote.price = last;
        quote.change = (last / open - 1) * 100;
        quote.valid = isfinite(quote.change);
    }

    return quote;
}

static Quote FetchQuote(void)
{
    Quote quote = {0};
    HINTERNET connection = NULL, request = NULL;
    HINTERNET session = WinHttpOpen(L"BtcSmiley/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, NULL, NULL, 0);
    char json[4096];
    DWORD status, size = sizeof(status), used = 0, count;

    if (!session) {
        return quote;
    }

    if (!WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000)) {
        goto done;
    }

    connection = WinHttpConnect(session, L"api.exchange.coinbase.com",
        INTERNET_DEFAULT_HTTPS_PORT, 0);

    if (!connection) {
        goto done;
    }

    request = WinHttpOpenRequest(connection, L"GET",
        L"/products/BTC-USD/stats", NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);

    if (!request ||
        !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, NULL) ||
        !WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE |
            WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &size, NULL) ||
        status != 200) {
        goto done;
    }

    for (;;) {
        if (used == sizeof(json) - 1 ||
            !WinHttpReadData(request, json + used,
                (DWORD)sizeof(json) - 1 - used, &count)) {
            goto done;
        }

        if (!count) {
            break;
        }

        used += count;
    }

    json[used] = '\0';
    quote = ParseQuote(json);

done:
    if (request) {
        WinHttpCloseHandle(request);
    }

    if (connection) {
        WinHttpCloseHandle(connection);
    }

    WinHttpCloseHandle(session);
    return quote;
}

static int Mood(double change, BOOL bullish)
{
    int direction = change > 0.1 ? 1 : change < -0.1 ? -1 : 0;
    return 1 + (bullish ? direction : -direction);
}

static HICON DrawFace(int mood)
{
    int size = GetSystemMetrics(SM_CXSMICON);
    HDC screen = GetDC(NULL);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP color = CreateCompatibleBitmap(screen, size, size);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, NULL);
    HGDIOBJ old = SelectObject(dc, mask);
    HBRUSH yellow = CreateSolidBrush(RGB(255, 220, 0));
    POINT mouth[5] = {{4, 10}, {6, 12}, {8, 13}, {10, 12}, {12, 10}};
    ICONINFO info = {0};
    HICON icon;
    int i;

    PatBlt(dc, 0, 0, size, size, WHITENESS);
    SetMapMode(dc, MM_ANISOTROPIC);
    SetWindowExtEx(dc, 16, 16, NULL);
    SetViewportExtEx(dc, size, size, NULL);
    SelectObject(dc, GetStockObject(BLACK_BRUSH));
    Ellipse(dc, 1, 1, 15, 15);
    SelectObject(dc, color);
    PatBlt(dc, 0, 0, 16, 16, BLACKNESS);
    SelectObject(dc, yellow);
    Ellipse(dc, 1, 1, 15, 15);
    SelectObject(dc, GetStockObject(BLACK_BRUSH));
    Ellipse(dc, 4, 5, 6, 7);
    Ellipse(dc, 10, 5, 12, 7);

    for (i = 0; i < 5; ++i) {
        if (mood == 0) {
            mouth[i].y = 22 - mouth[i].y;
        } else if (mood == 1) {
            mouth[i].y = 11;
        } else {
            mouth[i].y -= (i > 0 && i < 4) ? 2 : 1;
        }
    }

    Polyline(dc, mouth, 5);
    info.fIcon = TRUE;
    info.hbmColor = color;
    info.hbmMask = mask;
    icon = CreateIconIndirect(&info);
    SelectObject(dc, old);
    DeleteObject(yellow);
    DeleteObject(color);
    DeleteObject(mask);
    DeleteDC(dc);
    ReleaseDC(NULL, screen);
    return icon;
}

static void UpdateTray(DWORD operation)
{
    tray.hIcon = faces[current.valid ? Mood(current.change, bull) : 1];

    if (current.valid) {
        swprintf(tray.szTip, ARRAYSIZE(tray.szTip),
            L"BTC $%.2f | 24h %+.2f%% | %ls%ls",
            current.price, current.change, bull ? L"Bull" : L"Bear",
            stale ? L" | stale: update failed" : L"");
    } else {
        swprintf(tray.szTip, ARRAYSIZE(tray.szTip),
            L"BtcSmiley | %ls | %ls", bull ? L"Bull" : L"Bear",
            stale ? L"Price unavailable; retrying" : L"Fetching BTC/USD...");
    }

    Shell_NotifyIconW(operation, &tray);
}

static DWORD WINAPI FetchThread(void *window)
{
    pending = FetchQuote();
    PostMessageW((HWND)window, WM_QUOTE, 0, 0);
    return 0;
}

static void Refresh(HWND window)
{
    HANDLE thread;

    if (busy) {
        return;
    }

    busy = TRUE;
    thread = CreateThread(NULL, 0, FetchThread, window, 0, NULL);

    if (thread) {
        CloseHandle(thread);
    } else {
        busy = FALSE;
        stale = TRUE;
        UpdateTray(NIM_MODIFY);
    }
}

static LRESULT CALLBACK WindowProc(HWND window, UINT message,
    WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_TIMER:
        Refresh(window);
        return 0;
    case WM_QUOTE:
        busy = FALSE;
        stale = !pending.valid;

        if (pending.valid) {
            current = pending;
        }

        UpdateTray(NIM_MODIFY);
        return 0;
    case WM_TRAY:
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            POINT point;
            HMENU menu = CreatePopupMenu();
            UINT command;
            AppendMenuW(menu, MF_STRING | (bull ? 0 : MF_CHECKED), ID_BEAR, L"Bear");
            AppendMenuW(menu, MF_STRING | (bull ? MF_CHECKED : 0), ID_BULL, L"Bull");
            AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(menu, MF_STRING, ID_EXIT, L"Exit");
            GetCursorPos(&point);
            SetForegroundWindow(window);
            command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                point.x, point.y, 0, window, NULL);
            DestroyMenu(menu);
            PostMessageW(window, WM_NULL, 0, 0);

            if (command == ID_EXIT) {
                DestroyWindow(window);
            } else if (command == ID_BEAR || command == ID_BULL) {
                bull = command == ID_BULL;
                UpdateTray(NIM_MODIFY);
            }
        }

        return 0;
    case WM_DESTROY:
        KillTimer(window, 1);
        Shell_NotifyIconW(NIM_DELETE, &tray);
        PostQuitMessage(0);
        return 0;
    }

    if (message == taskbarCreated) {
        UpdateTray(NIM_ADD);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
    WNDCLASSW type = {0};
    HWND window;
    MSG message;
    int i;
    (void)previous;
    (void)command;
    (void)show;
    type.lpfnWndProc = WindowProc;
    type.hInstance = instance;
    type.lpszClassName = L"BtcSmiley";
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    if (!RegisterClassW(&type)) {
        return 1;
    }

    window = CreateWindowW(type.lpszClassName, L"BtcSmiley", WS_OVERLAPPED,
        0, 0, 0, 0, NULL, NULL, instance, NULL);

    if (!window) {
        return 1;
    }

    for (i = 0; i < 3; ++i) {
        faces[i] = DrawFace(i);
    }

    tray.cbSize = sizeof(tray);
    tray.hWnd = window;
    tray.uID = 1;
    tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray.uCallbackMessage = WM_TRAY;
    UpdateTray(NIM_ADD);
    SetTimer(window, 1, 60000, NULL);
    Refresh(window);

    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    for (i = 0; i < 3; ++i) {
        DestroyIcon(faces[i]);
    }

    return 0;
}
