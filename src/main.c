#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <stdbool.h>
#include <stdio.h>

#define APP_NAME L"DrawCursor"
#define MAIN_CLASS_NAME L"DrawCursor.MainWindow"
#define OVERLAY_CLASS_NAME L"DrawCursor.CursorOverlay"

#define WM_TRAYICON (WM_APP + 1)
#define TIMER_CURSOR 1
#define TIMER_RENDER 2
#define TIMER_INTERVAL_MS 50
#define RENDER_INTERVAL_MS 4
#define MOTION_IDLE_MS 20
#define CURSOR_CANVAS_SIZE 384
#define CURSOR_CANVAS_MARGIN 32

#define IDM_ENABLE 1001
#define IDM_DISABLE 1002
#define IDM_EXIT 1003

#define TRAY_ICON_ID 1
#define TRANSPARENT_COLOR RGB(255, 0, 255)

typedef HANDLE DPI_AWARENESS_CONTEXT;
typedef BOOL(WINAPI *SetProcessDpiAwarenessContextFn)(DPI_AWARENESS_CONTEXT);
typedef BOOL(WINAPI *SetProcessDpiAwareFn)(void);
typedef UINT(WINAPI *TimePeriodFn)(UINT);

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

typedef struct CursorMetrics {
    HCURSOR handle;
    int width;
    int height;
    int hotspot_x;
    int hotspot_y;
} CursorMetrics;

typedef struct DurationStats {
    ULONGLONG count;
    ULONGLONG total_us;
    ULONGLONG max_us;
} DurationStats;

typedef struct ProfileStats {
    ULONGLONG input_events;
    ULONGLONG render_requests;
    ULONGLONG render_timer_ticks;
    ULONGLONG state_timer_ticks;
    ULONGLONG render_attempts;
    ULONGLONG render_success;
    ULONGLONG render_fail;
    ULONGLONG render_noop_same_pos;
    ULONGLONG render_force;
    ULONGLONG canvas_recenter;
    ULONGLONG canvas_recreated;
    ULONGLONG cursor_changed;
    ULONGLONG cursor_hidden;
    ULONGLONG timer_started;
    ULONGLONG timer_stopped;
    ULONGLONG overlay_shown;
    DurationStats get_cursor_pos;
    DurationStats fill_canvas;
    DurationStats draw_icon;
    DurationStats get_screen_dc;
    DurationStats update_layered;
    DurationStats render_total;
} ProfileStats;

static HINSTANCE g_instance;
static HWND g_main_hwnd;
static HWND g_overlay_hwnd;
static NOTIFYICONDATAW g_tray;
static bool g_redraw_enabled = true;
static bool g_overlay_visible = false;
static bool g_cursor_showing = false;
static bool g_render_timer_active = false;
static DWORD g_last_input_tick = 0;
static CursorMetrics g_cursor;
static HMODULE g_winmm;
static TimePeriodFn g_time_begin_period;
static TimePeriodFn g_time_end_period;
static bool g_timer_precision_active = false;
static HDC g_canvas_dc;
static HBITMAP g_canvas_bitmap;
static HGDIOBJ g_canvas_old_bitmap;
static HDC g_cursor_dc;
static HBITMAP g_cursor_bitmap;
static HGDIOBJ g_cursor_old_bitmap;
static int g_cursor_bitmap_w = 0;
static int g_cursor_bitmap_h = 0;
static int g_canvas_x = 0;
static int g_canvas_y = 0;
static int g_canvas_w = 0;
static int g_canvas_h = 0;
static POINT g_last_rendered_cursor_pos = {0, 0};
static bool g_have_rendered_cursor = false;
static HANDLE g_profile_file = INVALID_HANDLE_VALUE;
static LARGE_INTEGER g_qpc_frequency;
static LARGE_INTEGER g_profile_start_time;
static LARGE_INTEGER g_profile_last_flush_time;
static ProfileStats g_profile;

static void ReleaseCursorBitmapResources(void);

static void SetLatencyPriority(void)
{
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
}

static void BeginTimerPrecision(void)
{
    if (g_timer_precision_active) {
        return;
    }

    if (!g_winmm) {
        g_winmm = LoadLibraryW(L"winmm.dll");
        if (g_winmm) {
            union {
                FARPROC proc;
                TimePeriodFn fn;
            } time_api;

            time_api.proc = GetProcAddress(g_winmm, "timeBeginPeriod");
            g_time_begin_period = time_api.fn;
            time_api.proc = GetProcAddress(g_winmm, "timeEndPeriod");
            g_time_end_period = time_api.fn;
        }
    }

    if (g_time_begin_period && g_time_begin_period(1) == 0) {
        g_timer_precision_active = true;
    }
}

static void EndTimerPrecision(void)
{
    if (g_timer_precision_active && g_time_end_period) {
        g_time_end_period(1);
    }

    g_timer_precision_active = false;

    if (g_winmm) {
        FreeLibrary(g_winmm);
        g_winmm = NULL;
        g_time_begin_period = NULL;
        g_time_end_period = NULL;
    }
}

static LARGE_INTEGER ProfileNow(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now;
}

static ULONGLONG ProfileElapsedUs(LARGE_INTEGER start, LARGE_INTEGER end)
{
    if (g_qpc_frequency.QuadPart <= 0) {
        return 0;
    }

    return (ULONGLONG)(((end.QuadPart - start.QuadPart) * 1000000LL) / g_qpc_frequency.QuadPart);
}

static void ProfileAddDuration(DurationStats *stats, LARGE_INTEGER start, LARGE_INTEGER end)
{
    ULONGLONG elapsed_us = ProfileElapsedUs(start, end);
    ++stats->count;
    stats->total_us += elapsed_us;
    if (elapsed_us > stats->max_us) {
        stats->max_us = elapsed_us;
    }
}

static ULONGLONG ProfileAvgUs(DurationStats stats)
{
    return stats.count ? stats.total_us / stats.count : 0;
}

static void ProfileWrite(const char *text)
{
    if (g_profile_file == INVALID_HANDLE_VALUE || !text) {
        return;
    }

    DWORD bytes_written = 0;
    WriteFile(g_profile_file, text, (DWORD)lstrlenA(text), &bytes_written, NULL);
}

static void ProfileBuildLogPath(WCHAR *path, DWORD path_count)
{
    DWORD len = GetModuleFileNameW(NULL, path, path_count);
    if (len == 0 || len >= path_count) {
        lstrcpynW(path, L"drawcursor-profile.csv", path_count);
        return;
    }

    for (DWORD i = len; i > 0; --i) {
        if (path[i - 1] == L'\\' || path[i - 1] == L'/') {
            path[i] = L'\0';
            break;
        }
    }

    WCHAR log_dir[MAX_PATH];
    lstrcpynW(log_dir, path, ARRAYSIZE(log_dir));
    lstrcatW(log_dir, L"logs");
    CreateDirectoryW(log_dir, NULL);

    lstrcatW(path, L"logs\\drawcursor-profile.csv");
}

static void ProfileFlush(bool force)
{
    if (g_profile_file == INVALID_HANDLE_VALUE) {
        return;
    }

    LARGE_INTEGER now = ProfileNow();
    ULONGLONG interval_us = ProfileElapsedUs(g_profile_last_flush_time, now);
    if (!force && interval_us < 1000000ULL) {
        return;
    }

    ULONGLONG since_start_ms = ProfileElapsedUs(g_profile_start_time, now) / 1000ULL;
    char line[2048];
    int len = snprintf(
        line,
        sizeof(line),
        "%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,"
        "%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u\n",
        since_start_ms,
        interval_us / 1000ULL,
        g_profile.input_events,
        g_profile.render_requests,
        g_profile.render_timer_ticks,
        g_profile.state_timer_ticks,
        g_profile.render_attempts,
        g_profile.render_success,
        g_profile.render_fail,
        g_profile.render_noop_same_pos,
        g_profile.render_force,
        g_profile.canvas_recenter,
        g_profile.canvas_recreated,
        g_profile.cursor_changed,
        g_profile.cursor_hidden,
        g_profile.timer_started,
        g_profile.timer_stopped,
        g_profile.overlay_shown,
        ProfileAvgUs(g_profile.get_cursor_pos),
        g_profile.get_cursor_pos.max_us,
        ProfileAvgUs(g_profile.fill_canvas),
        g_profile.fill_canvas.max_us,
        ProfileAvgUs(g_profile.draw_icon),
        g_profile.draw_icon.max_us,
        ProfileAvgUs(g_profile.get_screen_dc),
        g_profile.get_screen_dc.max_us,
        ProfileAvgUs(g_profile.update_layered),
        g_profile.update_layered.max_us,
        ProfileAvgUs(g_profile.render_total),
        g_profile.render_total.max_us,
        g_profile.update_layered.count,
        g_profile.render_total.count);

    if (len > 0) {
        ProfileWrite(line);
        FlushFileBuffers(g_profile_file);
    }

    ZeroMemory(&g_profile, sizeof(g_profile));
    g_profile_last_flush_time = now;
}

static void ProfileInit(void)
{
    QueryPerformanceFrequency(&g_qpc_frequency);
    g_profile_start_time = ProfileNow();
    g_profile_last_flush_time = g_profile_start_time;

    WCHAR path[MAX_PATH];
    ProfileBuildLogPath(path, ARRAYSIZE(path));
    g_profile_file = CreateFileW(
        path,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (g_profile_file == INVALID_HANDLE_VALUE) {
        return;
    }

    ProfileWrite(
        "since_start_ms,interval_ms,input_events,render_requests,render_timer_ticks,state_timer_ticks,"
        "render_attempts,render_success,render_fail,render_noop_same_pos,render_force,canvas_recenter,"
        "canvas_recreated,cursor_changed,cursor_hidden,timer_started,timer_stopped,overlay_shown,"
        "getcursor_avg_us,getcursor_max_us,fill_avg_us,fill_max_us,drawicon_avg_us,drawicon_max_us,"
        "getdc_avg_us,getdc_max_us,update_layered_avg_us,update_layered_max_us,render_total_avg_us,"
        "render_total_max_us,update_layered_calls,render_total_calls\n");
}

static void ProfileClose(void)
{
    ProfileFlush(true);
    if (g_profile_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_profile_file);
        g_profile_file = INVALID_HANDLE_VALUE;
    }
}

static void SetBestDpiAwareness(void)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        union {
            FARPROC proc;
            SetProcessDpiAwarenessContextFn set_context;
            SetProcessDpiAwareFn set_dpi_aware;
        } dpi;

        dpi.proc = GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (dpi.set_context && dpi.set_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            return;
        }

        dpi.proc = GetProcAddress(user32, "SetProcessDPIAware");
        if (dpi.set_dpi_aware) {
            dpi.set_dpi_aware();
        }
    }
}

static void DeleteIconInfoBitmaps(ICONINFO *icon_info)
{
    if (icon_info->hbmColor) {
        DeleteObject(icon_info->hbmColor);
    }
    if (icon_info->hbmMask) {
        DeleteObject(icon_info->hbmMask);
    }
}

static void UpdateCursorMetrics(HCURSOR cursor)
{
    ReleaseCursorBitmapResources();

    g_cursor.handle = cursor;
    g_cursor.width = GetSystemMetrics(SM_CXCURSOR);
    g_cursor.height = GetSystemMetrics(SM_CYCURSOR);
    g_cursor.hotspot_x = 0;
    g_cursor.hotspot_y = 0;

    ICONINFO icon_info;
    ZeroMemory(&icon_info, sizeof(icon_info));

    if (!cursor || !GetIconInfo(cursor, &icon_info)) {
        return;
    }

    g_cursor.hotspot_x = (int)icon_info.xHotspot;
    g_cursor.hotspot_y = (int)icon_info.yHotspot;

    BITMAP bitmap;
    ZeroMemory(&bitmap, sizeof(bitmap));

    if (icon_info.hbmColor && GetObjectW(icon_info.hbmColor, sizeof(bitmap), &bitmap) == sizeof(bitmap)) {
        g_cursor.width = bitmap.bmWidth;
        g_cursor.height = bitmap.bmHeight;
    } else if (icon_info.hbmMask && GetObjectW(icon_info.hbmMask, sizeof(bitmap), &bitmap) == sizeof(bitmap)) {
        g_cursor.width = bitmap.bmWidth;
        g_cursor.height = bitmap.bmHeight / 2;
    }

    if (g_cursor.width <= 0) {
        g_cursor.width = GetSystemMetrics(SM_CXCURSOR);
    }
    if (g_cursor.height <= 0) {
        g_cursor.height = GetSystemMetrics(SM_CYCURSOR);
    }

    DeleteIconInfoBitmaps(&icon_info);
}

static void SetOverlayVisible(bool visible)
{
    if (!g_overlay_hwnd || g_overlay_visible == visible) {
        return;
    }

    ShowWindow(g_overlay_hwnd, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    g_overlay_visible = visible;
}

static int MaxInt(int a, int b)
{
    return a > b ? a : b;
}

static void ReleaseCanvasResources(void)
{
    if (g_canvas_dc && g_canvas_old_bitmap) {
        SelectObject(g_canvas_dc, g_canvas_old_bitmap);
    }

    if (g_canvas_bitmap) {
        DeleteObject(g_canvas_bitmap);
        g_canvas_bitmap = NULL;
    }

    if (g_canvas_dc) {
        DeleteDC(g_canvas_dc);
        g_canvas_dc = NULL;
    }

    g_canvas_old_bitmap = NULL;
    g_canvas_w = 0;
    g_canvas_h = 0;
    g_have_rendered_cursor = false;
}

static void ReleaseCursorBitmapResources(void)
{
    if (g_cursor_dc && g_cursor_old_bitmap) {
        SelectObject(g_cursor_dc, g_cursor_old_bitmap);
    }

    if (g_cursor_bitmap) {
        DeleteObject(g_cursor_bitmap);
        g_cursor_bitmap = NULL;
    }

    if (g_cursor_dc) {
        DeleteDC(g_cursor_dc);
        g_cursor_dc = NULL;
    }

    g_cursor_old_bitmap = NULL;
    g_cursor_bitmap_w = 0;
    g_cursor_bitmap_h = 0;
}

static bool EnsureCursorBitmapResources(void)
{
    if (!g_cursor.handle || g_cursor.width <= 0 || g_cursor.height <= 0) {
        return false;
    }

    if (g_cursor_dc &&
        g_cursor_bitmap &&
        g_cursor_bitmap_w == g_cursor.width &&
        g_cursor_bitmap_h == g_cursor.height) {
        return true;
    }

    ReleaseCursorBitmapResources();

    g_cursor_dc = CreateCompatibleDC(NULL);
    if (!g_cursor_dc) {
        return false;
    }

    BITMAPINFO bitmap_info;
    ZeroMemory(&bitmap_info, sizeof(bitmap_info));
    bitmap_info.bmiHeader.biSize = sizeof(bitmap_info.bmiHeader);
    bitmap_info.bmiHeader.biWidth = g_cursor.width;
    bitmap_info.bmiHeader.biHeight = -g_cursor.height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    g_cursor_bitmap = CreateDIBSection(
        g_cursor_dc,
        &bitmap_info,
        DIB_RGB_COLORS,
        NULL,
        NULL,
        0);

    if (!g_cursor_bitmap) {
        ReleaseCursorBitmapResources();
        return false;
    }

    g_cursor_old_bitmap = SelectObject(g_cursor_dc, g_cursor_bitmap);
    if (!g_cursor_old_bitmap) {
        ReleaseCursorBitmapResources();
        return false;
    }

    RECT rect = {0, 0, g_cursor.width, g_cursor.height};
    HBRUSH brush = CreateSolidBrush(TRANSPARENT_COLOR);
    FillRect(g_cursor_dc, &rect, brush);
    DeleteObject(brush);

    DrawIconEx(
        g_cursor_dc,
        0,
        0,
        g_cursor.handle,
        g_cursor.width,
        g_cursor.height,
        0,
        NULL,
        DI_NORMAL);

    g_cursor_bitmap_w = g_cursor.width;
    g_cursor_bitmap_h = g_cursor.height;
    return true;
}

static bool EnsureCanvasResources(int width, int height)
{
    if (g_canvas_dc && g_canvas_bitmap && g_canvas_w == width && g_canvas_h == height) {
        return true;
    }

    ReleaseCanvasResources();

    g_canvas_dc = CreateCompatibleDC(NULL);
    if (!g_canvas_dc) {
        return false;
    }

    BITMAPINFO bitmap_info;
    ZeroMemory(&bitmap_info, sizeof(bitmap_info));
    bitmap_info.bmiHeader.biSize = sizeof(bitmap_info.bmiHeader);
    bitmap_info.bmiHeader.biWidth = width;
    bitmap_info.bmiHeader.biHeight = -height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    g_canvas_bitmap = CreateDIBSection(
        g_canvas_dc,
        &bitmap_info,
        DIB_RGB_COLORS,
        NULL,
        NULL,
        0);

    if (!g_canvas_bitmap) {
        ReleaseCanvasResources();
        return false;
    }

    g_canvas_old_bitmap = SelectObject(g_canvas_dc, g_canvas_bitmap);
    if (!g_canvas_old_bitmap) {
        ReleaseCanvasResources();
        return false;
    }

    g_canvas_w = width;
    g_canvas_h = height;
    ++g_profile.canvas_recreated;
    return true;
}

static int CursorCanvasSize(void)
{
    int min_size = MaxInt(g_cursor.width, g_cursor.height) + CURSOR_CANVAS_MARGIN * 2;
    return MaxInt(CURSOR_CANVAS_SIZE, min_size);
}

static bool CursorFitsCanvas(POINT cursor_pos)
{
    if (!g_overlay_visible || g_canvas_w <= 0 || g_canvas_h <= 0) {
        return false;
    }

    int image_left = cursor_pos.x - g_cursor.hotspot_x;
    int image_top = cursor_pos.y - g_cursor.hotspot_y;
    int image_right = image_left + g_cursor.width;
    int image_bottom = image_top + g_cursor.height;

    return
        image_left >= g_canvas_x + CURSOR_CANVAS_MARGIN &&
        image_top >= g_canvas_y + CURSOR_CANVAS_MARGIN &&
        image_right <= g_canvas_x + g_canvas_w - CURSOR_CANVAS_MARGIN &&
        image_bottom <= g_canvas_y + g_canvas_h - CURSOR_CANVAS_MARGIN;
}

static bool RenderCursorCanvas(POINT cursor_pos, bool force_update)
{
    LARGE_INTEGER render_start = ProfileNow();
    ++g_profile.render_attempts;
    if (force_update) {
        ++g_profile.render_force;
    }

    if (!g_redraw_enabled || !g_overlay_hwnd || !g_cursor_showing || !g_cursor.handle) {
        ++g_profile.render_fail;
        ProfileAddDuration(&g_profile.render_total, render_start, ProfileNow());
        return false;
    }

    int canvas_size = CursorCanvasSize();
    if (!EnsureCanvasResources(canvas_size, canvas_size) || !EnsureCursorBitmapResources()) {
        ++g_profile.render_fail;
        ProfileAddDuration(&g_profile.render_total, render_start, ProfileNow());
        return false;
    }

    bool recenter_canvas =
        force_update ||
        g_canvas_w != canvas_size ||
        g_canvas_h != canvas_size ||
        !CursorFitsCanvas(cursor_pos);

    if (recenter_canvas) {
        ++g_profile.canvas_recenter;
        g_canvas_x = cursor_pos.x - canvas_size / 2;
        g_canvas_y = cursor_pos.y - canvas_size / 2;
    }

    if (!force_update &&
        !recenter_canvas &&
        g_have_rendered_cursor &&
        cursor_pos.x == g_last_rendered_cursor_pos.x &&
        cursor_pos.y == g_last_rendered_cursor_pos.y) {
        ++g_profile.render_noop_same_pos;
        ProfileAddDuration(&g_profile.render_total, render_start, ProfileNow());
        return true;
    }

    LARGE_INTEGER step_start = ProfileNow();
    RECT rect = {0, 0, g_canvas_w, g_canvas_h};
    HBRUSH brush = CreateSolidBrush(TRANSPARENT_COLOR);
    FillRect(g_canvas_dc, &rect, brush);
    DeleteObject(brush);
    ProfileAddDuration(&g_profile.fill_canvas, step_start, ProfileNow());

    int image_x = cursor_pos.x - g_cursor.hotspot_x - g_canvas_x;
    int image_y = cursor_pos.y - g_cursor.hotspot_y - g_canvas_y;

    step_start = ProfileNow();
    BitBlt(
        g_canvas_dc,
        image_x,
        image_y,
        g_cursor.width,
        g_cursor.height,
        g_cursor_dc,
        0,
        0,
        SRCCOPY);
    ProfileAddDuration(&g_profile.draw_icon, step_start, ProfileNow());

    step_start = ProfileNow();
    HDC screen_dc = GetDC(NULL);
    ProfileAddDuration(&g_profile.get_screen_dc, step_start, ProfileNow());
    if (!screen_dc) {
        ++g_profile.render_fail;
        ProfileAddDuration(&g_profile.render_total, render_start, ProfileNow());
        return false;
    }

    POINT dst = {g_canvas_x, g_canvas_y};
    POINT src = {0, 0};
    SIZE size = {g_canvas_w, g_canvas_h};
    step_start = ProfileNow();
    BOOL ok = UpdateLayeredWindow(
        g_overlay_hwnd,
        screen_dc,
        &dst,
        &size,
        g_canvas_dc,
        &src,
        TRANSPARENT_COLOR,
        NULL,
        ULW_COLORKEY);
    ProfileAddDuration(&g_profile.update_layered, step_start, ProfileNow());

    ReleaseDC(NULL, screen_dc);

    if (!ok) {
        ++g_profile.render_fail;
        ProfileAddDuration(&g_profile.render_total, render_start, ProfileNow());
        return false;
    }

    if (!g_overlay_visible) {
        SetWindowPos(
            g_overlay_hwnd,
            HWND_TOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOREDRAW);
        ShowWindow(g_overlay_hwnd, SW_SHOWNOACTIVATE);
        g_overlay_visible = true;
        ++g_profile.overlay_shown;
    }

    g_last_rendered_cursor_pos = cursor_pos;
    g_have_rendered_cursor = true;
    ++g_profile.render_success;
    ProfileAddDuration(&g_profile.render_total, render_start, ProfileNow());
    return true;
}

static void RenderCursorAtCurrentPosition(bool force_update)
{
    LARGE_INTEGER step_start = ProfileNow();
    POINT cursor_pos;
    BOOL got_cursor = GetCursorPos(&cursor_pos);
    ProfileAddDuration(&g_profile.get_cursor_pos, step_start, ProfileNow());

    if (got_cursor) {
        RenderCursorCanvas(cursor_pos, force_update);
    }
}

static void StopRenderTimer(void)
{
    if (g_render_timer_active && g_main_hwnd) {
        KillTimer(g_main_hwnd, TIMER_RENDER);
        ++g_profile.timer_stopped;
    }

    g_render_timer_active = false;
}

static void StartRenderTimer(void)
{
    if (!g_render_timer_active && g_main_hwnd) {
        SetTimer(g_main_hwnd, TIMER_RENDER, RENDER_INTERVAL_MS, NULL);
        g_render_timer_active = true;
        ++g_profile.timer_started;
    }
}

static void RequestCursorRender(void)
{
    if (!g_main_hwnd) {
        return;
    }

    ++g_profile.render_requests;
    g_last_input_tick = GetTickCount();

    if (!g_render_timer_active) {
        RenderCursorAtCurrentPosition(false);
        StartRenderTimer();
    }
}

static void SyncCursorState(void)
{
    if (!g_redraw_enabled || !g_overlay_hwnd) {
        g_cursor_showing = false;
        g_have_rendered_cursor = false;
        StopRenderTimer();
        SetOverlayVisible(false);
        return;
    }

    CURSORINFO cursor_info;
    cursor_info.cbSize = sizeof(cursor_info);

    if (!GetCursorInfo(&cursor_info) ||
        !(cursor_info.flags & CURSOR_SHOWING) ||
        cursor_info.hCursor == NULL) {
        g_cursor_showing = false;
        g_have_rendered_cursor = false;
        StopRenderTimer();
        ++g_profile.cursor_hidden;
        SetOverlayVisible(false);
        return;
    }

    g_cursor_showing = true;

    bool cursor_changed = cursor_info.hCursor != g_cursor.handle;
    if (cursor_changed) {
        ++g_profile.cursor_changed;
        UpdateCursorMetrics(cursor_info.hCursor);
    }

    if (!RenderCursorCanvas(cursor_info.ptScreenPos, cursor_changed || !g_overlay_visible)) {
        g_cursor_showing = false;
        SetOverlayVisible(false);
    }
}

static bool RegisterRawMouseInput(HWND hwnd)
{
    RAWINPUTDEVICE mouse;
    ZeroMemory(&mouse, sizeof(mouse));
    mouse.usUsagePage = 0x01;
    mouse.usUsage = 0x02;
    mouse.dwFlags = RIDEV_INPUTSINK;
    mouse.hwndTarget = hwnd;

    return RegisterRawInputDevices(&mouse, 1, sizeof(mouse)) != FALSE;
}

static void UnregisterRawMouseInput(void)
{
    RAWINPUTDEVICE mouse;
    ZeroMemory(&mouse, sizeof(mouse));
    mouse.usUsagePage = 0x01;
    mouse.usUsage = 0x02;
    mouse.dwFlags = RIDEV_REMOVE;
    mouse.hwndTarget = NULL;

    RegisterRawInputDevices(&mouse, 1, sizeof(mouse));
}

static void UpdateTrayIconTip(void)
{
    if (!g_main_hwnd) {
        return;
    }

    ZeroMemory(&g_tray, sizeof(g_tray));
    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = g_main_hwnd;
    g_tray.uID = TRAY_ICON_ID;
    g_tray.uFlags = NIF_TIP;
    lstrcpynW(
        g_tray.szTip,
        g_redraw_enabled ? L"DrawCursor - 重绘已打开" : L"DrawCursor - 重绘已关闭",
        ARRAYSIZE(g_tray.szTip));

    Shell_NotifyIconW(NIM_MODIFY, &g_tray);
}

static bool AddTrayIcon(HWND hwnd)
{
    ZeroMemory(&g_tray, sizeof(g_tray));
    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = hwnd;
    g_tray.uID = TRAY_ICON_ID;
    g_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_tray.uCallbackMessage = WM_TRAYICON;
    g_tray.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    lstrcpynW(g_tray.szTip, L"DrawCursor - 重绘已打开", ARRAYSIZE(g_tray.szTip));

    return Shell_NotifyIconW(NIM_ADD, &g_tray) != FALSE;
}

static void RemoveTrayIcon(void)
{
    if (!g_main_hwnd) {
        return;
    }

    ZeroMemory(&g_tray, sizeof(g_tray));
    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = g_main_hwnd;
    g_tray.uID = TRAY_ICON_ID;
    Shell_NotifyIconW(NIM_DELETE, &g_tray);
}

static void ShowTrayMenu(HWND hwnd)
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }

    AppendMenuW(menu, MF_STRING, IDM_ENABLE, g_redraw_enabled ? L"打开重绘  \x2022" : L"打开重绘");
    AppendMenuW(menu, MF_STRING, IDM_DISABLE, g_redraw_enabled ? L"关闭重绘" : L"关闭重绘  \x2022");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"关闭 DrawCursor");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
    PostMessageW(hwnd, WM_NULL, 0, 0);

    DestroyMenu(menu);
}

static void SetRedrawEnabled(bool enabled)
{
    if (g_redraw_enabled == enabled) {
        return;
    }

    g_redraw_enabled = enabled;
    if (!enabled) {
        g_cursor_showing = false;
        g_have_rendered_cursor = false;
        StopRenderTimer();
        SetOverlayVisible(false);
    } else {
        SyncCursorState();
    }
    UpdateTrayIconTip();
}

static LRESULT CALLBACK OverlayWindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    (void)wparam;
    (void)lparam;

    switch (message) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATEANDEAT;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }

    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static bool CreateOverlayWindow(void)
{
    g_overlay_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        OVERLAY_CLASS_NAME,
        APP_NAME,
        WS_POPUP,
        0,
        0,
        GetSystemMetrics(SM_CXCURSOR),
        GetSystemMetrics(SM_CYCURSOR),
        NULL,
        NULL,
        g_instance,
        NULL);

    return g_overlay_hwnd != NULL;
}

static LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_CREATE:
        g_main_hwnd = hwnd;
        if (!CreateOverlayWindow() ||
            !AddTrayIcon(hwnd) ||
            !RegisterRawMouseInput(hwnd)) {
            DestroyWindow(hwnd);
            return -1;
        }
        SetTimer(hwnd, TIMER_CURSOR, TIMER_INTERVAL_MS, NULL);
        SyncCursorState();
        return 0;

    case WM_INPUT:
        ++g_profile.input_events;
        if (g_redraw_enabled) {
            RequestCursorRender();
        }
        ProfileFlush(false);
        return DefWindowProcW(hwnd, message, wparam, lparam);

    case WM_TIMER:
        if (wparam == TIMER_CURSOR) {
            ++g_profile.state_timer_ticks;
            SyncCursorState();
            ProfileFlush(false);
            return 0;
        }
        if (wparam == TIMER_RENDER) {
            ++g_profile.render_timer_ticks;
            if (g_redraw_enabled && GetTickCount() - g_last_input_tick <= MOTION_IDLE_MS) {
                RenderCursorAtCurrentPosition(false);
            } else {
                RenderCursorAtCurrentPosition(false);
                StopRenderTimer();
            }
            ProfileFlush(false);
            return 0;
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDM_ENABLE:
            SetRedrawEnabled(true);
            return 0;
        case IDM_DISABLE:
            SetRedrawEnabled(false);
            return 0;
        case IDM_EXIT:
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_TRAYICON:
        if (LOWORD(lparam) == WM_RBUTTONUP || LOWORD(lparam) == WM_CONTEXTMENU) {
            ShowTrayMenu(hwnd);
            return 0;
        }
        break;

    case WM_DESTROY:
        UnregisterRawMouseInput();
        StopRenderTimer();
        KillTimer(hwnd, TIMER_CURSOR);
        RemoveTrayIcon();
        if (g_overlay_hwnd) {
            DestroyWindow(g_overlay_hwnd);
            g_overlay_hwnd = NULL;
        }
        ReleaseCanvasResources();
        ReleaseCursorBitmapResources();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static bool RegisterWindowClasses(void)
{
    WNDCLASSEXW main_class;
    ZeroMemory(&main_class, sizeof(main_class));
    main_class.cbSize = sizeof(main_class);
    main_class.lpfnWndProc = MainWindowProc;
    main_class.hInstance = g_instance;
    main_class.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    main_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    main_class.lpszClassName = MAIN_CLASS_NAME;

    if (!RegisterClassExW(&main_class)) {
        return false;
    }

    WNDCLASSEXW overlay_class;
    ZeroMemory(&overlay_class, sizeof(overlay_class));
    overlay_class.cbSize = sizeof(overlay_class);
    overlay_class.lpfnWndProc = OverlayWindowProc;
    overlay_class.hInstance = g_instance;
    overlay_class.hCursor = NULL;
    overlay_class.hbrBackground = NULL;
    overlay_class.lpszClassName = OVERLAY_CLASS_NAME;

    return RegisterClassExW(&overlay_class) != 0;
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE previous_instance, LPWSTR command_line, int show_command)
{
    (void)previous_instance;
    (void)command_line;
    (void)show_command;

    HANDLE mutex = CreateMutexW(NULL, TRUE, L"Local\\DrawCursor.SingleInstance");
    if (!mutex) {
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"DrawCursor 已经在运行。", APP_NAME, MB_OK | MB_ICONINFORMATION);
        CloseHandle(mutex);
        return 0;
    }

    g_instance = instance;
    SetBestDpiAwareness();
    SetLatencyPriority();
    BeginTimerPrecision();
    ProfileInit();

    if (!RegisterWindowClasses()) {
        MessageBoxW(NULL, L"窗口类注册失败。", APP_NAME, MB_OK | MB_ICONERROR);
        ProfileClose();
        EndTimerPrecision();
        CloseHandle(mutex);
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        MAIN_CLASS_NAME,
        APP_NAME,
        WS_OVERLAPPED,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        0,
        0,
        NULL,
        NULL,
        instance,
        NULL);

    if (!hwnd) {
        MessageBoxW(NULL, L"DrawCursor 启动失败。", APP_NAME, MB_OK | MB_ICONERROR);
        ProfileClose();
        EndTimerPrecision();
        CloseHandle(mutex);
        return 1;
    }

    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    ProfileClose();
    EndTimerPrecision();
    CloseHandle(mutex);
    return (int)message.wParam;
}
