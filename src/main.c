#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define APP_NAME L"DrawCursor"
#define MAIN_CLASS_NAME L"DrawCursor.MainWindow"
#define OVERLAY_CLASS_NAME L"DrawCursor.CursorOverlay"

#define WM_TRAYICON (WM_APP + 1)
#define WM_RENDER_MODE_FALLBACK (WM_APP + 2)
#define TIMER_CURSOR 1
#define TIMER_INTERVAL_MS 50
#define RENDER_INTERVAL_MS 4
#define MOTION_IDLE_MS 20
#define CURSOR_CANVAS_SIZE 384
#define CURSOR_CANVAS_MARGIN 32

#define IDM_ENABLE 1001
#define IDM_DISABLE 1002
#define IDM_EXIT 1003
#define IDM_MODE_COMPAT 1004
#define IDM_MODE_FAST 1005

#define TRAY_ICON_ID 1

#define RENDER_MODE_COMPAT 0
#define RENDER_MODE_FAST 1

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

typedef HANDLE DPI_AWARENESS_CONTEXT;
typedef BOOL(WINAPI *SetProcessDpiAwarenessContextFn)(DPI_AWARENESS_CONTEXT);
typedef BOOL(WINAPI *SetProcessDpiAwareFn)(void);
typedef UINT(WINAPI *TimePeriodFn)(UINT);
typedef HANDLE(WINAPI *CreateWaitableTimerExFn)(
    LPSECURITY_ATTRIBUTES,
    LPCWSTR,
    DWORD,
    DWORD);
typedef HANDLE(WINAPI *AvSetMmThreadCharacteristicsFn)(LPCWSTR, LPDWORD);
typedef BOOL(WINAPI *AvSetMmThreadPriorityFn)(HANDLE, int);
typedef BOOL(WINAPI *AvRevertMmThreadCharacteristicsFn)(HANDLE);
typedef HRESULT(WINAPI *DwmGetCompositionTimingInfoFn)(HWND, DWM_TIMING_INFO *);

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
    volatile LONG64 count;
    volatile LONG64 total_us;
    volatile LONG64 max_us;
} DurationStats;

typedef struct ProfileStats {
    volatile LONG64 input_events;
    volatile LONG64 render_requests;
    volatile LONG64 render_timer_ticks;
    volatile LONG64 state_timer_ticks;
    volatile LONG64 render_attempts;
    volatile LONG64 render_success;
    volatile LONG64 render_fail;
    volatile LONG64 render_noop_same_pos;
    volatile LONG64 render_force;
    volatile LONG64 canvas_recenter;
    volatile LONG64 canvas_recreated;
    volatile LONG64 cursor_changed;
    volatile LONG64 cursor_hidden;
    volatile LONG64 timer_started;
    volatile LONG64 timer_stopped;
    volatile LONG64 overlay_shown;
    volatile LONG64 dwm_timing_success;
    volatile LONG64 dwm_timing_fail;
    volatile LONG64 input_batches;
    volatile LONG64 input_buffered_events;
    volatile LONG64 input_coalesced;
    volatile LONG64 input_buffer_failures;
    volatile LONG64 fast_move_success;
    volatile LONG64 fast_full_update;
    volatile LONG64 fast_fallback;
    DurationStats get_cursor_info;
    DurationStats fill_canvas;
    DurationStats draw_icon;
    DurationStats update_layered;
    DurationStats render_total;
    DurationStats input_to_render;
    DurationStats refresh_period;
    DurationStats deadline_late;
} ProfileStats;

typedef struct ProfileRow {
    ULONGLONG since_start_ms;
    ULONGLONG interval_ms;
    ProfileStats stats;
} ProfileRow;

#define PROFILE_QUEUE_CAPACITY 16
#define PROFILE_INC(field) InterlockedIncrement64(&(field))
#define PROFILE_ADD(field, value) InterlockedAdd64(&(field), (LONG64)(value))

typedef struct BitmapInfo1bpp {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD bmiColors[2];
} BitmapInfo1bpp;

static HINSTANCE g_instance;
static HWND g_main_hwnd;
static HWND g_overlay_hwnd;
static NOTIFYICONDATAW g_tray;
static volatile LONG g_redraw_enabled = 1;
static bool g_overlay_visible = false;
static bool g_cursor_showing = false;
static bool g_render_timer_active = false;
static HANDLE g_render_timer;
static HANDLE g_render_thread;
static HANDLE g_render_wake_event;
static HANDLE g_render_ready_event;
static volatile LONG g_render_stop = 0;
static volatile LONG g_render_thread_ready = 0;
static volatile LONG g_render_requested = 0;
static volatile LONG g_state_sync_requested = 0;
static volatile LONG g_force_render_requested = 0;
static volatile LONG g_last_input_tick = 0;
static volatile LONG g_render_mode = RENDER_MODE_COMPAT;
static LONG g_render_mode_applied = -1;
static unsigned int g_fast_position_failures = 0;
static CursorMetrics g_cursor;
static HMODULE g_winmm;
static TimePeriodFn g_time_begin_period;
static TimePeriodFn g_time_end_period;
static bool g_timer_precision_active = false;
static HDC g_canvas_dc;
static HBITMAP g_canvas_bitmap;
static HGDIOBJ g_canvas_old_bitmap;
static DWORD *g_canvas_pixels;
static HDC g_cursor_dc;
static HBITMAP g_cursor_bitmap;
static HGDIOBJ g_cursor_old_bitmap;
static DWORD *g_cursor_pixels;
static int g_cursor_bitmap_w = 0;
static int g_cursor_bitmap_h = 0;
static int g_canvas_x = 0;
static int g_canvas_y = 0;
static int g_canvas_w = 0;
static int g_canvas_h = 0;
static POINT g_last_rendered_cursor_pos = {0, 0};
static bool g_have_rendered_cursor = false;
static RECT g_last_canvas_cursor_rect = {0, 0, 0, 0};
static bool g_have_canvas_cursor_rect = false;
static HANDLE g_profile_file = INVALID_HANDLE_VALUE;
static HANDLE g_profile_event;
static HANDLE g_profile_thread;
static CRITICAL_SECTION g_profile_queue_lock;
static bool g_profile_queue_lock_ready = false;
static volatile LONG g_profile_stop = 0;
static ProfileRow g_profile_queue[PROFILE_QUEUE_CAPACITY];
static unsigned int g_profile_queue_head = 0;
static unsigned int g_profile_queue_count = 0;
static LARGE_INTEGER g_qpc_frequency;
static LARGE_INTEGER g_profile_start_time;
static LARGE_INTEGER g_profile_last_snapshot_time;
static ProfileStats g_profile;
static volatile LONG64 g_latest_input_qpc = 0;
static HMODULE g_dwmapi;
static DwmGetCompositionTimingInfoFn g_dwm_get_timing;
static bool g_dwm_timing_disabled = false;
static LONG64 g_refresh_period_qpc = 0;
static LONG64 g_last_vblank_qpc = 0;
static LONG64 g_last_dwm_query_qpc = 0;
static LONG64 g_render_timer_deadline_qpc = 0;
static LONG64 g_render_cadence_deadline_qpc = 0;
static ULONGLONG g_submit_cost_ewma_us = 750;
static HMONITOR g_last_cursor_monitor;
static BYTE *g_raw_input_buffer;
static UINT g_raw_input_buffer_size = 0;
static bool g_raw_input_buffer_disabled = false;

static void ReleaseCursorBitmapResources(void);

static bool IsRedrawEnabled(void)
{
    return InterlockedCompareExchange(&g_redraw_enabled, 0, 0) != 0;
}

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

static void ProfileAddValue(DurationStats *stats, ULONGLONG value_us)
{
    LONG64 elapsed_us = (LONG64)value_us;
    PROFILE_INC(stats->count);
    InterlockedAdd64(&stats->total_us, elapsed_us);

    LONG64 previous_max = stats->max_us;
    while (elapsed_us > previous_max) {
        LONG64 observed = InterlockedCompareExchange64(&stats->max_us, elapsed_us, previous_max);
        if (observed == previous_max) {
            break;
        }
        previous_max = observed;
    }
}

static void ProfileAddDuration(DurationStats *stats, LARGE_INTEGER start, LARGE_INTEGER end)
{
    ProfileAddValue(stats, ProfileElapsedUs(start, end));
}

static ULONGLONG ProfileAvgUs(DurationStats stats)
{
    return stats.count ? (ULONGLONG)(stats.total_us / stats.count) : 0;
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

static void ProfileTakeDuration(DurationStats *source, DurationStats *destination)
{
    destination->count = InterlockedExchange64(&source->count, 0);
    destination->total_us = InterlockedExchange64(&source->total_us, 0);
    destination->max_us = InterlockedExchange64(&source->max_us, 0);
}

static void ProfileTakeStats(ProfileStats *snapshot)
{
    ZeroMemory(snapshot, sizeof(*snapshot));

#define PROFILE_TAKE_COUNTER(name) snapshot->name = InterlockedExchange64(&g_profile.name, 0)
    PROFILE_TAKE_COUNTER(input_events);
    PROFILE_TAKE_COUNTER(render_requests);
    PROFILE_TAKE_COUNTER(render_timer_ticks);
    PROFILE_TAKE_COUNTER(state_timer_ticks);
    PROFILE_TAKE_COUNTER(render_attempts);
    PROFILE_TAKE_COUNTER(render_success);
    PROFILE_TAKE_COUNTER(render_fail);
    PROFILE_TAKE_COUNTER(render_noop_same_pos);
    PROFILE_TAKE_COUNTER(render_force);
    PROFILE_TAKE_COUNTER(canvas_recenter);
    PROFILE_TAKE_COUNTER(canvas_recreated);
    PROFILE_TAKE_COUNTER(cursor_changed);
    PROFILE_TAKE_COUNTER(cursor_hidden);
    PROFILE_TAKE_COUNTER(timer_started);
    PROFILE_TAKE_COUNTER(timer_stopped);
    PROFILE_TAKE_COUNTER(overlay_shown);
    PROFILE_TAKE_COUNTER(dwm_timing_success);
    PROFILE_TAKE_COUNTER(dwm_timing_fail);
    PROFILE_TAKE_COUNTER(input_batches);
    PROFILE_TAKE_COUNTER(input_buffered_events);
    PROFILE_TAKE_COUNTER(input_coalesced);
    PROFILE_TAKE_COUNTER(input_buffer_failures);
    PROFILE_TAKE_COUNTER(fast_move_success);
    PROFILE_TAKE_COUNTER(fast_full_update);
    PROFILE_TAKE_COUNTER(fast_fallback);
#undef PROFILE_TAKE_COUNTER

    ProfileTakeDuration(&g_profile.get_cursor_info, &snapshot->get_cursor_info);
    ProfileTakeDuration(&g_profile.fill_canvas, &snapshot->fill_canvas);
    ProfileTakeDuration(&g_profile.draw_icon, &snapshot->draw_icon);
    ProfileTakeDuration(&g_profile.update_layered, &snapshot->update_layered);
    ProfileTakeDuration(&g_profile.render_total, &snapshot->render_total);
    ProfileTakeDuration(&g_profile.input_to_render, &snapshot->input_to_render);
    ProfileTakeDuration(&g_profile.refresh_period, &snapshot->refresh_period);
    ProfileTakeDuration(&g_profile.deadline_late, &snapshot->deadline_late);
}

static void ProfileWriteRow(const ProfileRow *row)
{
    const ProfileStats *stats = &row->stats;
    char line[2304];
    int len = snprintf(
        line,
        sizeof(line),
        "%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,"
        "%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,"
        "%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u,%I64u\n",
        row->since_start_ms,
        row->interval_ms,
        (ULONGLONG)stats->input_events,
        (ULONGLONG)stats->render_requests,
        (ULONGLONG)stats->render_timer_ticks,
        (ULONGLONG)stats->state_timer_ticks,
        (ULONGLONG)stats->render_attempts,
        (ULONGLONG)stats->render_success,
        (ULONGLONG)stats->render_fail,
        (ULONGLONG)stats->render_noop_same_pos,
        (ULONGLONG)stats->render_force,
        (ULONGLONG)stats->canvas_recenter,
        (ULONGLONG)stats->canvas_recreated,
        (ULONGLONG)stats->cursor_changed,
        (ULONGLONG)stats->cursor_hidden,
        (ULONGLONG)stats->timer_started,
        (ULONGLONG)stats->timer_stopped,
        (ULONGLONG)stats->overlay_shown,
        ProfileAvgUs(stats->get_cursor_info),
        (ULONGLONG)stats->get_cursor_info.max_us,
        ProfileAvgUs(stats->fill_canvas),
        (ULONGLONG)stats->fill_canvas.max_us,
        ProfileAvgUs(stats->draw_icon),
        (ULONGLONG)stats->draw_icon.max_us,
        ProfileAvgUs(stats->update_layered),
        (ULONGLONG)stats->update_layered.max_us,
        ProfileAvgUs(stats->render_total),
        (ULONGLONG)stats->render_total.max_us,
        (ULONGLONG)stats->update_layered.count,
        (ULONGLONG)stats->render_total.count,
        ProfileAvgUs(stats->input_to_render),
        (ULONGLONG)stats->input_to_render.max_us,
        (ULONGLONG)stats->dwm_timing_success,
        (ULONGLONG)stats->dwm_timing_fail,
        ProfileAvgUs(stats->refresh_period),
        (ULONGLONG)stats->refresh_period.max_us,
        ProfileAvgUs(stats->deadline_late),
        (ULONGLONG)stats->deadline_late.max_us,
        (ULONGLONG)stats->input_batches,
        (ULONGLONG)stats->input_buffered_events,
        (ULONGLONG)stats->input_coalesced,
        (ULONGLONG)stats->input_buffer_failures,
        (ULONGLONG)stats->fast_move_success,
        (ULONGLONG)stats->fast_full_update,
        (ULONGLONG)stats->fast_fallback);

    if (len > 0) {
        ProfileWrite(line);
    }
}

static DWORD WINAPI ProfileWriterThread(void *parameter)
{
    (void)parameter;

    for (;;) {
        WaitForSingleObject(g_profile_event, INFINITE);

        for (;;) {
            ProfileRow row;
            bool have_row = false;

            EnterCriticalSection(&g_profile_queue_lock);
            if (g_profile_queue_count > 0) {
                row = g_profile_queue[g_profile_queue_head];
                g_profile_queue_head = (g_profile_queue_head + 1) % PROFILE_QUEUE_CAPACITY;
                --g_profile_queue_count;
                have_row = true;
            }
            LeaveCriticalSection(&g_profile_queue_lock);

            if (!have_row) {
                break;
            }
            ProfileWriteRow(&row);
        }

        if (InterlockedCompareExchange(&g_profile_stop, 0, 0)) {
            FlushFileBuffers(g_profile_file);
            return 0;
        }
    }
}

static void ProfileQueueSnapshot(bool force)
{
    if (g_profile_file == INVALID_HANDLE_VALUE || !g_profile_thread) {
        return;
    }

    LARGE_INTEGER now = ProfileNow();
    ULONGLONG interval_us = ProfileElapsedUs(g_profile_last_snapshot_time, now);
    if (!force && interval_us < 1000000ULL) {
        return;
    }

    ProfileRow row;
    row.since_start_ms = ProfileElapsedUs(g_profile_start_time, now) / 1000ULL;
    row.interval_ms = interval_us / 1000ULL;
    ProfileTakeStats(&row.stats);

    EnterCriticalSection(&g_profile_queue_lock);
    if (g_profile_queue_count == PROFILE_QUEUE_CAPACITY) {
        g_profile_queue_head = (g_profile_queue_head + 1) % PROFILE_QUEUE_CAPACITY;
        --g_profile_queue_count;
    }
    unsigned int tail = (g_profile_queue_head + g_profile_queue_count) % PROFILE_QUEUE_CAPACITY;
    g_profile_queue[tail] = row;
    ++g_profile_queue_count;
    LeaveCriticalSection(&g_profile_queue_lock);

    g_profile_last_snapshot_time = now;
    SetEvent(g_profile_event);
}

static void ProfileInit(void)
{
    QueryPerformanceFrequency(&g_qpc_frequency);
    g_profile_start_time = ProfileNow();
    g_profile_last_snapshot_time = g_profile_start_time;

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
        "getcursorinfo_avg_us,getcursorinfo_max_us,fill_avg_us,fill_max_us,drawicon_avg_us,drawicon_max_us,"
        "update_layered_avg_us,update_layered_max_us,render_total_avg_us,"
        "render_total_max_us,update_layered_calls,render_total_calls,input_to_render_avg_us,"
        "input_to_render_max_us,dwm_timing_success,dwm_timing_fail,refresh_period_avg_us,"
        "refresh_period_max_us,deadline_late_avg_us,deadline_late_max_us,input_batches,"
        "input_buffered_events,input_coalesced,input_buffer_failures,fast_move_success,"
        "fast_full_update,fast_fallback\n");

    InitializeCriticalSection(&g_profile_queue_lock);
    g_profile_queue_lock_ready = true;
    g_profile_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (g_profile_event) {
        g_profile_thread = CreateThread(NULL, 0, ProfileWriterThread, NULL, 0, NULL);
    }

    if (!g_profile_thread) {
        if (g_profile_event) {
            CloseHandle(g_profile_event);
            g_profile_event = NULL;
        }
        DeleteCriticalSection(&g_profile_queue_lock);
        g_profile_queue_lock_ready = false;
        CloseHandle(g_profile_file);
        g_profile_file = INVALID_HANDLE_VALUE;
    }
}

static void ProfileClose(void)
{
    ProfileQueueSnapshot(true);
    if (g_profile_thread) {
        InterlockedExchange(&g_profile_stop, 1);
        SetEvent(g_profile_event);
        WaitForSingleObject(g_profile_thread, INFINITE);
        CloseHandle(g_profile_thread);
        g_profile_thread = NULL;
    }
    if (g_profile_event) {
        CloseHandle(g_profile_event);
        g_profile_event = NULL;
    }
    if (g_profile_queue_lock_ready) {
        DeleteCriticalSection(&g_profile_queue_lock);
        g_profile_queue_lock_ready = false;
    }
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

static DWORD PremultiplyPixel(DWORD pixel)
{
    BYTE alpha = (BYTE)(pixel >> 24);
    if (alpha == 0) {
        return 0;
    }
    if (alpha == 255) {
        return pixel;
    }

    BYTE red = (BYTE)(pixel >> 16);
    BYTE green = (BYTE)(pixel >> 8);
    BYTE blue = (BYTE)pixel;

    red = (BYTE)((red * alpha + 127) / 255);
    green = (BYTE)((green * alpha + 127) / 255);
    blue = (BYTE)((blue * alpha + 127) / 255);

    return ((DWORD)alpha << 24) | ((DWORD)red << 16) | ((DWORD)green << 8) | blue;
}

static bool BitmapNeedsPremultiply(const DWORD *pixels, int count)
{
    for (int i = 0; i < count; ++i) {
        BYTE alpha = (BYTE)(pixels[i] >> 24);
        if (alpha > 0 && alpha < 255) {
            BYTE red = (BYTE)(pixels[i] >> 16);
            BYTE green = (BYTE)(pixels[i] >> 8);
            BYTE blue = (BYTE)pixels[i];
            if (red > alpha || green > alpha || blue > alpha) {
                return true;
            }
        }
    }

    return false;
}

static bool ReadBitmap32(HBITMAP bitmap, int width, int height, DWORD *pixels)
{
    if (!bitmap || width <= 0 || height <= 0 || !pixels) {
        return false;
    }

    HDC dc = CreateCompatibleDC(NULL);
    if (!dc) {
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

    int rows = GetDIBits(
        dc,
        bitmap,
        0,
        (UINT)height,
        pixels,
        &bitmap_info,
        DIB_RGB_COLORS);

    DeleteDC(dc);
    return rows == height;
}

static BYTE *ReadMonoMaskBits(HBITMAP bitmap, int width, int height, int *stride_out)
{
    if (!bitmap || width <= 0 || height <= 0 || !stride_out) {
        return NULL;
    }

    int stride = ((width + 31) / 32) * 4;
    size_t bytes = (size_t)stride * (size_t)height;
    BYTE *bits = (BYTE *)malloc(bytes);
    if (!bits) {
        return NULL;
    }

    HDC dc = CreateCompatibleDC(NULL);
    if (!dc) {
        free(bits);
        return NULL;
    }

    BitmapInfo1bpp bitmap_info;
    ZeroMemory(&bitmap_info, sizeof(bitmap_info));
    bitmap_info.bmiHeader.biSize = sizeof(bitmap_info.bmiHeader);
    bitmap_info.bmiHeader.biWidth = width;
    bitmap_info.bmiHeader.biHeight = -height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 1;
    bitmap_info.bmiHeader.biCompression = BI_RGB;
    bitmap_info.bmiColors[0].rgbBlue = 0;
    bitmap_info.bmiColors[0].rgbGreen = 0;
    bitmap_info.bmiColors[0].rgbRed = 0;
    bitmap_info.bmiColors[1].rgbBlue = 255;
    bitmap_info.bmiColors[1].rgbGreen = 255;
    bitmap_info.bmiColors[1].rgbRed = 255;

    int rows = GetDIBits(
        dc,
        bitmap,
        0,
        (UINT)height,
        bits,
        (BITMAPINFO *)&bitmap_info,
        DIB_RGB_COLORS);

    DeleteDC(dc);

    if (rows != height) {
        free(bits);
        return NULL;
    }

    *stride_out = stride;
    return bits;
}

static bool GetMaskBit(const BYTE *bits, int stride, int x, int y)
{
    return (bits[y * stride + x / 8] & (BYTE)(0x80 >> (x % 8))) != 0;
}

static void ApplyHighContrastXorPixels(
    DWORD *pixels,
    const BYTE *xor_pixels,
    int width,
    int height)
{
    if (!pixels || !xor_pixels || width <= 0 || height <= 0) {
        return;
    }

    /*
     * Per-pixel alpha cannot represent the invert operation used by classic
     * XOR cursors. Draw a white one-pixel outline around a black core so the
     * cursor remains visible on both light and dark backgrounds.
     */
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!xor_pixels[y * width + x]) {
                continue;
            }

            for (int dy = -1; dy <= 1; ++dy) {
                int outline_y = y + dy;
                if (outline_y < 0 || outline_y >= height) {
                    continue;
                }
                for (int dx = -1; dx <= 1; ++dx) {
                    int outline_x = x + dx;
                    if (outline_x < 0 || outline_x >= width || (dx == 0 && dy == 0)) {
                        continue;
                    }

                    int outline_index = outline_y * width + outline_x;
                    if (!xor_pixels[outline_index] && pixels[outline_index] == 0) {
                        pixels[outline_index] = 0xFFFFFFFF;
                    }
                }
            }
        }
    }

    for (int i = 0; i < width * height; ++i) {
        if (xor_pixels[i]) {
            pixels[i] = 0xFF000000;
        }
    }
}

static void BuildCursorFromMonochromeMask(const BYTE *mask_bits, int mask_stride, int mask_height)
{
    int xor_offset = mask_height >= g_cursor.height * 2 ? g_cursor.height : 0;
    int pixel_count = g_cursor.width * g_cursor.height;
    BYTE *xor_pixels = (BYTE *)calloc((size_t)pixel_count, sizeof(BYTE));

    for (int y = 0; y < g_cursor.height; ++y) {
        for (int x = 0; x < g_cursor.width; ++x) {
            bool and_bit = GetMaskBit(mask_bits, mask_stride, x, y);
            bool xor_bit = xor_offset > 0 ? GetMaskBit(mask_bits, mask_stride, x, y + xor_offset) : false;
            DWORD pixel = 0;

            if (!and_bit && !xor_bit) {
                pixel = 0xFF000000;
            } else if (!and_bit && xor_bit) {
                pixel = 0xFFFFFFFF;
            } else if (and_bit && xor_bit) {
                if (xor_pixels) {
                    xor_pixels[y * g_cursor.width + x] = 1;
                } else {
                    pixel = 0xFF000000;
                }
            }

            g_cursor_pixels[y * g_cursor.width + x] = pixel;
        }
    }

    if (xor_pixels) {
        ApplyHighContrastXorPixels(
            g_cursor_pixels,
            xor_pixels,
            g_cursor.width,
            g_cursor.height);
        free(xor_pixels);
    }
}

static bool BuildCursorBitmapPixels(void)
{
    ICONINFO icon_info;
    ZeroMemory(&icon_info, sizeof(icon_info));

    if (!g_cursor.handle || !GetIconInfo(g_cursor.handle, &icon_info)) {
        return false;
    }

    bool ok = false;
    int pixel_count = g_cursor.width * g_cursor.height;
    ZeroMemory(g_cursor_pixels, (size_t)pixel_count * sizeof(DWORD));

    if (icon_info.hbmColor) {
        DWORD *color_pixels = (DWORD *)malloc((size_t)pixel_count * sizeof(DWORD));
        if (color_pixels && ReadBitmap32(icon_info.hbmColor, g_cursor.width, g_cursor.height, color_pixels)) {
            bool has_alpha = false;
            for (int i = 0; i < pixel_count; ++i) {
                if ((color_pixels[i] >> 24) != 0) {
                    has_alpha = true;
                    break;
                }
            }

            if (has_alpha) {
                bool needs_premultiply = BitmapNeedsPremultiply(color_pixels, pixel_count);
                for (int i = 0; i < pixel_count; ++i) {
                    DWORD pixel = color_pixels[i];
                    if ((pixel >> 24) == 0) {
                        pixel = 0;
                    } else if (needs_premultiply) {
                        pixel = PremultiplyPixel(pixel);
                    }
                    g_cursor_pixels[i] = pixel;
                }
                ok = true;
            } else {
                int mask_stride = 0;
                BYTE *mask_bits = NULL;
                BYTE *xor_pixels = (BYTE *)calloc((size_t)pixel_count, sizeof(BYTE));
                BITMAP mask_bitmap;
                ZeroMemory(&mask_bitmap, sizeof(mask_bitmap));

                if (icon_info.hbmMask &&
                    GetObjectW(icon_info.hbmMask, sizeof(mask_bitmap), &mask_bitmap) == sizeof(mask_bitmap)) {
                    mask_bits = ReadMonoMaskBits(
                        icon_info.hbmMask,
                        mask_bitmap.bmWidth,
                        mask_bitmap.bmHeight,
                        &mask_stride);
                }

                for (int y = 0; y < g_cursor.height; ++y) {
                    for (int x = 0; x < g_cursor.width; ++x) {
                        int i = y * g_cursor.width + x;
                        bool transparent = false;
                        if (mask_bits && x < mask_bitmap.bmWidth && y < mask_bitmap.bmHeight) {
                            transparent = GetMaskBit(mask_bits, mask_stride, x, y);
                        }

                        if (!transparent) {
                            g_cursor_pixels[i] = color_pixels[i] | 0xFF000000;
                        } else if ((color_pixels[i] & 0x00FFFFFF) != 0) {
                            /* Color cursors such as the system I-beam can put
                             * all visible pixels in an XOR-only RGB bitmap. */
                            if (xor_pixels) {
                                xor_pixels[i] = 1;
                            } else {
                                g_cursor_pixels[i] = 0xFF000000;
                            }
                        }
                    }
                }

                if (xor_pixels) {
                    ApplyHighContrastXorPixels(
                        g_cursor_pixels,
                        xor_pixels,
                        g_cursor.width,
                        g_cursor.height);
                    free(xor_pixels);
                }

                if (mask_bits) {
                    free(mask_bits);
                }
                ok = true;
            }
        }

        if (color_pixels) {
            free(color_pixels);
        }
    } else if (icon_info.hbmMask) {
        BITMAP mask_bitmap;
        ZeroMemory(&mask_bitmap, sizeof(mask_bitmap));
        if (GetObjectW(icon_info.hbmMask, sizeof(mask_bitmap), &mask_bitmap) == sizeof(mask_bitmap)) {
            int mask_stride = 0;
            BYTE *mask_bits = ReadMonoMaskBits(
                icon_info.hbmMask,
                mask_bitmap.bmWidth,
                mask_bitmap.bmHeight,
                &mask_stride);

            if (mask_bits) {
                BuildCursorFromMonochromeMask(mask_bits, mask_stride, mask_bitmap.bmHeight);
                free(mask_bits);
                ok = true;
            }
        }
    }

    DeleteIconInfoBitmaps(&icon_info);
    return ok;
}

static bool ClipCanvasRect(int image_x, int image_y, int width, int height, RECT *rect)
{
    rect->left = image_x < 0 ? 0 : image_x;
    rect->top = image_y < 0 ? 0 : image_y;
    rect->right = image_x + width;
    rect->bottom = image_y + height;

    if (rect->right > g_canvas_w) {
        rect->right = g_canvas_w;
    }
    if (rect->bottom > g_canvas_h) {
        rect->bottom = g_canvas_h;
    }
    return rect->left < rect->right && rect->top < rect->bottom;
}

static void ClearCanvasRect(RECT rect)
{
    if (rect.left < 0) {
        rect.left = 0;
    }
    if (rect.top < 0) {
        rect.top = 0;
    }
    if (rect.right > g_canvas_w) {
        rect.right = g_canvas_w;
    }
    if (rect.bottom > g_canvas_h) {
        rect.bottom = g_canvas_h;
    }
    if (rect.left >= rect.right || rect.top >= rect.bottom) {
        return;
    }

    SIZE_T row_bytes = (SIZE_T)(rect.right - rect.left) * sizeof(DWORD);
    for (int y = rect.top; y < rect.bottom; ++y) {
        ZeroMemory(g_canvas_pixels + y * g_canvas_w + rect.left, row_bytes);
    }
}

static bool CopyCursorToCanvas(int image_x, int image_y, RECT *copied_rect)
{
    RECT rect;
    if (!ClipCanvasRect(image_x, image_y, g_cursor_bitmap_w, g_cursor_bitmap_h, &rect)) {
        return false;
    }

    int src_x = rect.left - image_x;
    int src_y = rect.top - image_y;
    int copy_w = rect.right - rect.left;
    int copy_h = rect.bottom - rect.top;

    for (int y = 0; y < copy_h; ++y) {
        DWORD *dst = g_canvas_pixels + (rect.top + y) * g_canvas_w + rect.left;
        DWORD *src = g_cursor_pixels + (src_y + y) * g_cursor_bitmap_w + src_x;
        CopyMemory(dst, src, (SIZE_T)copy_w * sizeof(DWORD));
    }

    *copied_rect = rect;
    return true;
}

#ifdef DRAWCURSOR_VALIDATE_CANVAS
static bool CanvasPixelsStayInside(RECT allowed)
{
    for (int y = 0; y < g_canvas_h; ++y) {
        for (int x = 0; x < g_canvas_w; ++x) {
            bool inside =
                x >= allowed.left && x < allowed.right &&
                y >= allowed.top && y < allowed.bottom;
            if (!inside && g_canvas_pixels[y * g_canvas_w + x] != 0) {
                return false;
            }
        }
    }
    return true;
}
#endif

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
    g_canvas_pixels = NULL;
    g_canvas_w = 0;
    g_canvas_h = 0;
    g_have_rendered_cursor = false;
    g_have_canvas_cursor_rect = false;
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
    g_cursor_pixels = NULL;
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
        (void **)&g_cursor_pixels,
        NULL,
        0);

    if (!g_cursor_bitmap || !g_cursor_pixels) {
        ReleaseCursorBitmapResources();
        return false;
    }

    g_cursor_old_bitmap = SelectObject(g_cursor_dc, g_cursor_bitmap);
    if (!g_cursor_old_bitmap) {
        ReleaseCursorBitmapResources();
        return false;
    }

    if (!BuildCursorBitmapPixels()) {
        ReleaseCursorBitmapResources();
        return false;
    }

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
        (void **)&g_canvas_pixels,
        NULL,
        0);

    if (!g_canvas_bitmap || !g_canvas_pixels) {
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
    PROFILE_INC(g_profile.canvas_recreated);
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

static bool RenderCursorCompatibility(POINT cursor_pos, bool force_update)
{
    LARGE_INTEGER render_start = ProfileNow();
    PROFILE_INC(g_profile.render_attempts);
    if (force_update) {
        PROFILE_INC(g_profile.render_force);
    }

    if (!IsRedrawEnabled() || !g_overlay_hwnd || !g_cursor_showing || !g_cursor.handle) {
        PROFILE_INC(g_profile.render_fail);
        ProfileAddDuration(&g_profile.render_total, render_start, ProfileNow());
        return false;
    }

    int canvas_size = CursorCanvasSize();
    if (!EnsureCanvasResources(canvas_size, canvas_size) || !EnsureCursorBitmapResources()) {
        PROFILE_INC(g_profile.render_fail);
        ProfileAddDuration(&g_profile.render_total, render_start, ProfileNow());
        return false;
    }

    bool recenter_canvas =
        force_update ||
        g_canvas_w != canvas_size ||
        g_canvas_h != canvas_size ||
        !CursorFitsCanvas(cursor_pos);

    if (recenter_canvas) {
        PROFILE_INC(g_profile.canvas_recenter);
        g_canvas_x = cursor_pos.x - canvas_size / 2;
        g_canvas_y = cursor_pos.y - canvas_size / 2;
    }

    if (!force_update &&
        !recenter_canvas &&
        g_have_rendered_cursor &&
        cursor_pos.x == g_last_rendered_cursor_pos.x &&
        cursor_pos.y == g_last_rendered_cursor_pos.y) {
        PROFILE_INC(g_profile.render_noop_same_pos);
        ProfileAddDuration(&g_profile.render_total, render_start, ProfileNow());
        return true;
    }

    LARGE_INTEGER step_start = ProfileNow();
    if (force_update || !g_have_canvas_cursor_rect) {
        ZeroMemory(g_canvas_pixels, (size_t)g_canvas_w * (size_t)g_canvas_h * sizeof(DWORD));
    } else {
        ClearCanvasRect(g_last_canvas_cursor_rect);
    }
    ProfileAddDuration(&g_profile.fill_canvas, step_start, ProfileNow());

    int image_x = cursor_pos.x - g_cursor.hotspot_x - g_canvas_x;
    int image_y = cursor_pos.y - g_cursor.hotspot_y - g_canvas_y;

    step_start = ProfileNow();
    RECT copied_rect;
    bool copied_cursor = CopyCursorToCanvas(image_x, image_y, &copied_rect);
    ProfileAddDuration(&g_profile.draw_icon, step_start, ProfileNow());

    g_have_canvas_cursor_rect = copied_cursor;
    if (copied_cursor) {
        g_last_canvas_cursor_rect = copied_rect;
    }

#ifdef DRAWCURSOR_VALIDATE_CANVAS
    RECT allowed = copied_cursor ? copied_rect : (RECT){0, 0, 0, 0};
    if (!CanvasPixelsStayInside(allowed)) {
        OutputDebugStringW(L"DrawCursor: canvas pixel escaped current cursor bounds\n");
        PROFILE_INC(g_profile.render_fail);
        ProfileAddDuration(&g_profile.render_total, render_start, ProfileNow());
        return false;
    }
#endif

    POINT dst = {g_canvas_x, g_canvas_y};
    POINT src = {0, 0};
    SIZE size = {g_canvas_w, g_canvas_h};
    BLENDFUNCTION blend;
    ZeroMemory(&blend, sizeof(blend));
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    step_start = ProfileNow();
    BOOL ok = UpdateLayeredWindow(
        g_overlay_hwnd,
        NULL,
        &dst,
        &size,
        g_canvas_dc,
        &src,
        0,
        &blend,
        ULW_ALPHA);
    LARGE_INTEGER update_end = ProfileNow();
    ULONGLONG update_us = ProfileElapsedUs(step_start, update_end);
    ProfileAddValue(&g_profile.update_layered, update_us);

    if (!ok) {
        PROFILE_INC(g_profile.render_fail);
        ProfileAddDuration(&g_profile.render_total, render_start, ProfileNow());
        return false;
    }

    ULONGLONG bounded_update_us = update_us > 4000 ? 4000 : update_us;
    g_submit_cost_ewma_us = (g_submit_cost_ewma_us * 7 + bounded_update_us) / 8;

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
        PROFILE_INC(g_profile.overlay_shown);
    }

    g_last_rendered_cursor_pos = cursor_pos;
    g_have_rendered_cursor = true;
    PROFILE_INC(g_profile.render_success);
    ProfileAddDuration(&g_profile.render_total, render_start, ProfileNow());
    return true;
}

static bool FallbackToCompatibility(POINT cursor_pos, LARGE_INTEGER fast_render_start)
{
    PROFILE_INC(g_profile.fast_fallback);
    InterlockedExchange(&g_render_mode, RENDER_MODE_COMPAT);
    g_render_mode_applied = RENDER_MODE_COMPAT;
    g_fast_position_failures = 0;
    SetOverlayVisible(false);
    ReleaseCanvasResources();
    if (g_main_hwnd) {
        PostMessageW(g_main_hwnd, WM_RENDER_MODE_FALLBACK, 0, 0);
    }
    ProfileAddDuration(&g_profile.render_total, fast_render_start, ProfileNow());
    return RenderCursorCompatibility(cursor_pos, true);
}

static bool RenderCursorFast(POINT cursor_pos, bool force_update)
{
    LARGE_INTEGER render_start = ProfileNow();
    PROFILE_INC(g_profile.render_attempts);
    if (force_update) {
        PROFILE_INC(g_profile.render_force);
    }

    if (!IsRedrawEnabled() || !g_overlay_hwnd || !g_cursor_showing || !g_cursor.handle) {
        PROFILE_INC(g_profile.render_fail);
        ProfileAddDuration(&g_profile.render_total, render_start, ProfileNow());
        return false;
    }

    int destination_x = cursor_pos.x - g_cursor.hotspot_x;
    int destination_y = cursor_pos.y - g_cursor.hotspot_y;
    bool surface_ready =
        g_canvas_dc &&
        g_canvas_bitmap &&
        g_canvas_w == g_cursor.width &&
        g_canvas_h == g_cursor.height &&
        g_have_canvas_cursor_rect;

    if (!force_update &&
        surface_ready &&
        g_overlay_visible &&
        g_have_rendered_cursor &&
        cursor_pos.x == g_last_rendered_cursor_pos.x &&
        cursor_pos.y == g_last_rendered_cursor_pos.y) {
        PROFILE_INC(g_profile.render_noop_same_pos);
        ProfileAddDuration(&g_profile.render_total, render_start, ProfileNow());
        return true;
    }

    if (!force_update && surface_ready && g_overlay_visible) {
        LARGE_INTEGER move_start = ProfileNow();
#ifdef DRAWCURSOR_FAST_TEST_FAIL_POSITION
        BOOL moved = FALSE;
#else
        POINT destination = {destination_x, destination_y};
        BOOL moved = UpdateLayeredWindow(
            g_overlay_hwnd,
            NULL,
            &destination,
            NULL,
            NULL,
            NULL,
            0,
            NULL,
            0);
#endif
        LARGE_INTEGER move_end = ProfileNow();
        ProfileAddDuration(&g_profile.update_layered, move_start, move_end);

        if (moved) {
            g_fast_position_failures = 0;
            g_canvas_x = destination_x;
            g_canvas_y = destination_y;
            g_last_rendered_cursor_pos = cursor_pos;
            g_have_rendered_cursor = true;
            PROFILE_INC(g_profile.fast_move_success);
            PROFILE_INC(g_profile.render_success);
            ProfileAddDuration(&g_profile.render_total, render_start, ProfileNow());
            return true;
        }

        ++g_fast_position_failures;
        if (g_fast_position_failures >= 3) {
            return FallbackToCompatibility(cursor_pos, render_start);
        }
    }

    bool redraw_surface = force_update || !surface_ready;
    if (!EnsureCursorBitmapResources() ||
        !EnsureCanvasResources(g_cursor.width, g_cursor.height)) {
        PROFILE_INC(g_profile.render_fail);
        return FallbackToCompatibility(cursor_pos, render_start);
    }

    if (redraw_surface) {
        LARGE_INTEGER fill_start = ProfileNow();
        ZeroMemory(g_canvas_pixels, (size_t)g_canvas_w * (size_t)g_canvas_h * sizeof(DWORD));
        ProfileAddDuration(&g_profile.fill_canvas, fill_start, ProfileNow());

        LARGE_INTEGER draw_start = ProfileNow();
        RECT copied_rect;
        bool copied_cursor = CopyCursorToCanvas(0, 0, &copied_rect);
        ProfileAddDuration(&g_profile.draw_icon, draw_start, ProfileNow());
        g_have_canvas_cursor_rect = copied_cursor;
        if (copied_cursor) {
            g_last_canvas_cursor_rect = copied_rect;
        }
        if (!copied_cursor) {
            PROFILE_INC(g_profile.render_fail);
            return FallbackToCompatibility(cursor_pos, render_start);
        }

#ifdef DRAWCURSOR_VALIDATE_CANVAS
        if (!CanvasPixelsStayInside(copied_rect)) {
            OutputDebugStringW(L"DrawCursor: fast canvas pixel escaped cursor bounds\n");
            PROFILE_INC(g_profile.render_fail);
            return FallbackToCompatibility(cursor_pos, render_start);
        }
#endif
    }

    POINT destination = {destination_x, destination_y};
    POINT source = {0, 0};
    SIZE size = {g_canvas_w, g_canvas_h};
    BLENDFUNCTION blend;
    ZeroMemory(&blend, sizeof(blend));
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    LARGE_INTEGER update_start = ProfileNow();
    BOOL updated = UpdateLayeredWindow(
        g_overlay_hwnd,
        NULL,
        &destination,
        &size,
        g_canvas_dc,
        &source,
        0,
        &blend,
        ULW_ALPHA);
    LARGE_INTEGER update_end = ProfileNow();
    ULONGLONG update_us = ProfileElapsedUs(update_start, update_end);
    ProfileAddValue(&g_profile.update_layered, update_us);
    if (!updated) {
        PROFILE_INC(g_profile.render_fail);
        return FallbackToCompatibility(cursor_pos, render_start);
    }

    ULONGLONG bounded_update_us = update_us > 4000 ? 4000 : update_us;
    g_submit_cost_ewma_us = (g_submit_cost_ewma_us * 7 + bounded_update_us) / 8;
    g_canvas_x = destination_x;
    g_canvas_y = destination_y;

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
        PROFILE_INC(g_profile.overlay_shown);
    }

    g_last_rendered_cursor_pos = cursor_pos;
    g_have_rendered_cursor = true;
    PROFILE_INC(g_profile.fast_full_update);
    PROFILE_INC(g_profile.render_success);
    ProfileAddDuration(&g_profile.render_total, render_start, ProfileNow());
    return true;
}

static bool RenderCursorForMode(POINT cursor_pos, bool force_update)
{
    if (g_render_mode_applied == RENDER_MODE_FAST) {
        return RenderCursorFast(cursor_pos, force_update);
    }
    return RenderCursorCompatibility(cursor_pos, force_update);
}

static bool SampleAndRenderCursor(bool force_update)
{
    LARGE_INTEGER step_start = ProfileNow();
    LONG64 input_qpc = InterlockedExchange64(&g_latest_input_qpc, 0);
    if (input_qpc > 0 && step_start.QuadPart >= input_qpc) {
        LARGE_INTEGER input_time;
        input_time.QuadPart = input_qpc;
        ProfileAddDuration(&g_profile.input_to_render, input_time, step_start);
    }

    if (!IsRedrawEnabled() || !g_overlay_hwnd) {
        g_cursor_showing = false;
        g_have_rendered_cursor = false;
        g_have_canvas_cursor_rect = false;
        SetOverlayVisible(false);
        return false;
    }

    CURSORINFO cursor_info;
    ZeroMemory(&cursor_info, sizeof(cursor_info));
    cursor_info.cbSize = sizeof(cursor_info);
    BOOL got_cursor = GetCursorInfo(&cursor_info);
    ProfileAddDuration(&g_profile.get_cursor_info, step_start, ProfileNow());

    if (!got_cursor ||
        !(cursor_info.flags & CURSOR_SHOWING) ||
        cursor_info.hCursor == NULL) {
        g_cursor_showing = false;
        g_have_rendered_cursor = false;
        g_have_canvas_cursor_rect = false;
        PROFILE_INC(g_profile.cursor_hidden);
        SetOverlayVisible(false);
        return false;
    }

    g_cursor_showing = true;
    HMONITOR cursor_monitor = MonitorFromPoint(cursor_info.ptScreenPos, MONITOR_DEFAULTTONEAREST);
    if (cursor_monitor != g_last_cursor_monitor) {
        g_last_cursor_monitor = cursor_monitor;
        g_last_dwm_query_qpc = 0;
    }

    LONG requested_mode = InterlockedCompareExchange(&g_render_mode, 0, 0);
    if (requested_mode != g_render_mode_applied) {
        SetOverlayVisible(false);
        ReleaseCanvasResources();
        g_render_mode_applied = requested_mode;
        g_fast_position_failures = 0;
        force_update = true;
    }
    bool cursor_changed = cursor_info.hCursor != g_cursor.handle;
    if (cursor_changed) {
        PROFILE_INC(g_profile.cursor_changed);
        UpdateCursorMetrics(cursor_info.hCursor);
    }

    if (!RenderCursorForMode(
            cursor_info.ptScreenPos,
            force_update || cursor_changed || !g_overlay_visible)) {
        g_cursor_showing = false;
        g_have_rendered_cursor = false;
        g_have_canvas_cursor_rect = false;
        SetOverlayVisible(false);
        return false;
    }
    return true;
}

static LONG64 QpcTicksFromUs(ULONGLONG microseconds)
{
    return (LONG64)((microseconds * (ULONGLONG)g_qpc_frequency.QuadPart) / 1000000ULL);
}

static void InitDwmTiming(void)
{
    WCHAR disabled[2];
    g_dwm_timing_disabled = GetEnvironmentVariableW(
        L"DRAWCURSOR_DISABLE_DWM_TIMING",
        disabled,
        ARRAYSIZE(disabled)) > 0;
    if (g_dwm_timing_disabled) {
        return;
    }

    g_dwmapi = LoadLibraryW(L"dwmapi.dll");
    if (!g_dwmapi) {
        return;
    }

    union {
        FARPROC proc;
        DwmGetCompositionTimingInfoFn fn;
    } dwm;
    dwm.proc = GetProcAddress(g_dwmapi, "DwmGetCompositionTimingInfo");
    g_dwm_get_timing = dwm.fn;
}

static void CloseDwmTiming(void)
{
    g_dwm_get_timing = NULL;
    if (g_dwmapi) {
        FreeLibrary(g_dwmapi);
        g_dwmapi = NULL;
    }
    g_refresh_period_qpc = 0;
    g_last_vblank_qpc = 0;
    g_last_dwm_query_qpc = 0;
    g_last_cursor_monitor = NULL;
}

static void RefreshDwmTiming(LONG64 now_qpc, bool force)
{
    LONG64 query_interval = g_qpc_frequency.QuadPart;
    if (!force &&
        g_last_dwm_query_qpc > 0 &&
        now_qpc - g_last_dwm_query_qpc < query_interval) {
        return;
    }
    g_last_dwm_query_qpc = now_qpc;

    if (!g_dwm_get_timing) {
        g_refresh_period_qpc = 0;
        g_last_vblank_qpc = 0;
        PROFILE_INC(g_profile.dwm_timing_fail);
        return;
    }

    DWM_TIMING_INFO timing;
    ZeroMemory(&timing, sizeof(timing));
    timing.cbSize = sizeof(timing);
    HRESULT result = g_dwm_get_timing(NULL, &timing);

    ULONGLONG min_period = (ULONGLONG)g_qpc_frequency.QuadPart / 1000ULL;
    ULONGLONG max_period = (ULONGLONG)g_qpc_frequency.QuadPart / 10ULL;
    if (FAILED(result) ||
        timing.qpcRefreshPeriod == 0 ||
        timing.qpcRefreshPeriod < min_period ||
        timing.qpcRefreshPeriod > max_period ||
        timing.qpcVBlank == 0) {
        g_refresh_period_qpc = 0;
        g_last_vblank_qpc = 0;
        PROFILE_INC(g_profile.dwm_timing_fail);
        return;
    }

    g_refresh_period_qpc = (LONG64)timing.qpcRefreshPeriod;
    g_last_vblank_qpc = (LONG64)timing.qpcVBlank;
    PROFILE_INC(g_profile.dwm_timing_success);
    ProfileAddValue(
        &g_profile.refresh_period,
        (ULONGLONG)((timing.qpcRefreshPeriod * 1000000LL) / g_qpc_frequency.QuadPart));
}

static LONG64 ComputeNextRenderDeadline(LONG64 now_qpc)
{
    RefreshDwmTiming(now_qpc, false);

    LONG64 maximum_interval = QpcTicksFromUs(RENDER_INTERVAL_MS * 1000ULL);
    if (g_refresh_period_qpc > 0 && g_refresh_period_qpc / 2 < maximum_interval) {
        maximum_interval = g_refresh_period_qpc / 2;
    }
    LONG64 minimum_interval = QpcTicksFromUs(500);
    if (maximum_interval < minimum_interval) {
        maximum_interval = minimum_interval;
    }

    if (g_render_cadence_deadline_qpc == 0 ||
        g_render_cadence_deadline_qpc > now_qpc + maximum_interval) {
        g_render_cadence_deadline_qpc = now_qpc + maximum_interval;
    } else {
        while (g_render_cadence_deadline_qpc <= now_qpc) {
            g_render_cadence_deadline_qpc += maximum_interval;
        }
    }

    LONG64 deadline = g_render_cadence_deadline_qpc;
    if (g_refresh_period_qpc > 0 && g_last_vblank_qpc > 0) {
        LONG64 next_vblank = g_last_vblank_qpc;
        if (next_vblank <= now_qpc) {
            LONG64 periods = (now_qpc - next_vblank) / g_refresh_period_qpc + 1;
            next_vblank += periods * g_refresh_period_qpc;
        }

        ULONGLONG lead_us = g_submit_cost_ewma_us + 250;
        if (lead_us < 500) {
            lead_us = 500;
        }
        if (lead_us > 2000) {
            lead_us = 2000;
        }
        LONG64 phase_deadline = next_vblank - QpcTicksFromUs(lead_us);
        while (phase_deadline <= now_qpc) {
            phase_deadline += g_refresh_period_qpc;
        }
        if (phase_deadline < deadline) {
            deadline = phase_deadline;
        }
    }
    return deadline;
}

static void StopRenderTimer(void)
{
    if (g_render_timer_active && g_render_timer) {
        CancelWaitableTimer(g_render_timer);
        PROFILE_INC(g_profile.timer_stopped);
    }

    g_render_timer_active = false;
    g_render_timer_deadline_qpc = 0;
    g_render_cadence_deadline_qpc = 0;
}

static bool ArmRenderTimerDeadline(LONG64 deadline_qpc)
{
    if (!g_render_timer) {
        return false;
    }

    LARGE_INTEGER now = ProfileNow();
    LONG64 remaining_qpc = deadline_qpc - now.QuadPart;
    LONGLONG remaining_100ns =
        remaining_qpc > 0
            ? (remaining_qpc * 10000000LL) / g_qpc_frequency.QuadPart
            : 1;
    if (remaining_100ns < 1) {
        remaining_100ns = 1;
    }

    LARGE_INTEGER due_time;
    due_time.QuadPart = -remaining_100ns;
    if (!SetWaitableTimer(g_render_timer, &due_time, 0, NULL, NULL, FALSE)) {
        g_render_timer_active = false;
        return false;
    }

    if (!g_render_timer_active) {
        PROFILE_INC(g_profile.timer_started);
    }
    g_render_timer_active = true;
    g_render_timer_deadline_qpc = deadline_qpc;
    return true;
}

static bool ScheduleNextRender(void)
{
    LARGE_INTEGER now = ProfileNow();
    return ArmRenderTimerDeadline(ComputeNextRenderDeadline(now.QuadPart));
}

static void StartRenderTimer(void)
{
    if (!g_render_timer_active) {
        ScheduleNextRender();
    }
}

static void ProcessRenderTimer(void)
{
    if (!g_render_timer_active) {
        return;
    }

    PROFILE_INC(g_profile.render_timer_ticks);
    LARGE_INTEGER timer_fired = ProfileNow();
    if (g_render_timer_deadline_qpc > 0 && timer_fired.QuadPart > g_render_timer_deadline_qpc) {
        LARGE_INTEGER deadline;
        deadline.QuadPart = g_render_timer_deadline_qpc;
        ProfileAddDuration(&g_profile.deadline_late, deadline, timer_fired);
    }
    bool cursor_available = SampleAndRenderCursor(false);

    DWORD last_input_tick = (DWORD)InterlockedCompareExchange(&g_last_input_tick, 0, 0);
    if (cursor_available && IsRedrawEnabled() && GetTickCount() - last_input_tick <= MOTION_IDLE_MS) {
        ScheduleNextRender();
    } else {
        StopRenderTimer();
    }
}

static void RequestCursorRender(void)
{
    PROFILE_INC(g_profile.render_requests);
    LARGE_INTEGER input_time = ProfileNow();
    InterlockedExchange64(&g_latest_input_qpc, input_time.QuadPart);
    InterlockedExchange(&g_last_input_tick, (LONG)GetTickCount());
    InterlockedExchange(&g_render_requested, 1);

    if (g_render_wake_event) {
        SetEvent(g_render_wake_event);
    }
}

static void RequestCursorStateSync(void)
{
    InterlockedExchange(&g_state_sync_requested, 1);
    if (g_render_wake_event) {
        SetEvent(g_render_wake_event);
    }
}

static bool ResizeRawInputBuffer(UINT required_size)
{
    const UINT default_size = 64 * 1024;
    UINT new_size = required_size > default_size ? required_size : default_size;
    new_size = (new_size + 7U) & ~7U;

    BYTE *new_buffer = (BYTE *)realloc(g_raw_input_buffer, new_size);
    if (!new_buffer) {
        return false;
    }
    g_raw_input_buffer = new_buffer;
    g_raw_input_buffer_size = new_size;
    return true;
}

static void InitRawInputBuffer(void)
{
    WCHAR disabled[2];
    g_raw_input_buffer_disabled = GetEnvironmentVariableW(
        L"DRAWCURSOR_DISABLE_RAW_INPUT_BUFFER",
        disabled,
        ARRAYSIZE(disabled)) > 0;
    if (!g_raw_input_buffer_disabled) {
        ResizeRawInputBuffer(0);
    }
}

static void ReleaseRawInputBuffer(void)
{
    free(g_raw_input_buffer);
    g_raw_input_buffer = NULL;
    g_raw_input_buffer_size = 0;
    g_raw_input_buffer_disabled = false;
}

static bool ReadCurrentRawMouse(HRAWINPUT input_handle, bool *is_mouse)
{
    RAWINPUT input;
    UINT size = sizeof(input);
    UINT result = GetRawInputData(
        input_handle,
        RID_INPUT,
        &input,
        &size,
        sizeof(RAWINPUTHEADER));
    if (result == (UINT)-1 || result < sizeof(RAWINPUTHEADER)) {
        return false;
    }

    *is_mouse = input.header.dwType == RIM_TYPEMOUSE;
    return true;
}

static PRAWINPUT NextRawInputBlockAligned(PRAWINPUT input)
{
    ULONG_PTR next = (ULONG_PTR)input + input->header.dwSize;
    next = (next + sizeof(ULONGLONG) - 1) & ~(ULONG_PTR)(sizeof(ULONGLONG) - 1);
    return (PRAWINPUT)next;
}

static UINT DrainBufferedRawMouseInputs(bool *failed)
{
    *failed = false;
    if (g_raw_input_buffer_disabled) {
        *failed = true;
        return 0;
    }
    if (!g_raw_input_buffer && !ResizeRawInputBuffer(0)) {
        *failed = true;
        return 0;
    }

    UINT mouse_events = 0;
    for (;;) {
        UINT buffer_size = g_raw_input_buffer_size;
        UINT count = GetRawInputBuffer(
            (PRAWINPUT)g_raw_input_buffer,
            &buffer_size,
            sizeof(RAWINPUTHEADER));

        if (count == (UINT)-1) {
            DWORD error = GetLastError();
            if (error == ERROR_INSUFFICIENT_BUFFER &&
                buffer_size > g_raw_input_buffer_size &&
                ResizeRawInputBuffer(buffer_size)) {
                continue;
            }
            *failed = true;
            return mouse_events;
        }
        if (count == 0) {
            return mouse_events;
        }

        PRAWINPUT input = (PRAWINPUT)g_raw_input_buffer;
        for (UINT i = 0; i < count; ++i) {
            if (input->header.dwType == RIM_TYPEMOUSE) {
                ++mouse_events;
            }

            PRAWINPUT input_for_default = input;
            DefRawInputProc(&input_for_default, 1, sizeof(RAWINPUTHEADER));
            input = NextRawInputBlockAligned(input);
        }
    }
}

static LRESULT ProcessRawInputMessage(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
    bool current_is_mouse = false;
    bool current_read = ReadCurrentRawMouse((HRAWINPUT)lparam, &current_is_mouse);
    if (!current_read) {
        PROFILE_INC(g_profile.input_buffer_failures);
        current_is_mouse = true;
    }

#ifdef DRAWCURSOR_RAW_INPUT_TEST_HOLD_MS
    Sleep(DRAWCURSOR_RAW_INPUT_TEST_HOLD_MS);
#endif

    bool buffer_failed = false;
    UINT buffered_events = DrainBufferedRawMouseInputs(&buffer_failed);
    if (buffer_failed) {
        PROFILE_INC(g_profile.input_buffer_failures);
    }

    UINT total_events = (current_is_mouse ? 1U : 0U) + buffered_events;
    if (total_events > 0) {
        PROFILE_ADD(g_profile.input_events, total_events);
        PROFILE_INC(g_profile.input_batches);
        PROFILE_ADD(g_profile.input_buffered_events, buffered_events);
        if (total_events > 1) {
            PROFILE_ADD(g_profile.input_coalesced, total_events - 1);
        }
        if (IsRedrawEnabled()) {
            RequestCursorRender();
        }
    }

    if (GET_RAWINPUT_CODE_WPARAM(wparam) == RIM_INPUT) {
        return DefWindowProcW(hwnd, WM_INPUT, wparam, lparam);
    }
    return 0;
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
    bool fast_mode = InterlockedCompareExchange(&g_render_mode, 0, 0) == RENDER_MODE_FAST;
    const WCHAR *tip;
    if (IsRedrawEnabled()) {
        tip = fast_mode ? L"DrawCursor - 重绘已打开（极速模式）" : L"DrawCursor - 重绘已打开（兼容模式）";
    } else {
        tip = fast_mode ? L"DrawCursor - 重绘已关闭（极速模式）" : L"DrawCursor - 重绘已关闭（兼容模式）";
    }
    lstrcpynW(g_tray.szTip, tip, ARRAYSIZE(g_tray.szTip));

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
    lstrcpynW(g_tray.szTip, L"DrawCursor - 重绘已打开（兼容模式）", ARRAYSIZE(g_tray.szTip));

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

    AppendMenuW(menu, MF_STRING, IDM_ENABLE, IsRedrawEnabled() ? L"打开重绘  \x2022" : L"打开重绘");
    AppendMenuW(menu, MF_STRING, IDM_DISABLE, IsRedrawEnabled() ? L"关闭重绘" : L"关闭重绘  \x2022");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    bool fast_mode = InterlockedCompareExchange(&g_render_mode, 0, 0) == RENDER_MODE_FAST;
    AppendMenuW(menu, MF_STRING, IDM_MODE_COMPAT, fast_mode ? L"兼容模式" : L"兼容模式  \x2022");
    AppendMenuW(menu, MF_STRING, IDM_MODE_FAST, fast_mode ? L"极速模式  \x2022" : L"极速模式");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"关闭 DrawCursor");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
    PostMessageW(hwnd, WM_NULL, 0, 0);

    DestroyMenu(menu);
}

static void SetRedrawEnabled(bool enabled)
{
    LONG enabled_value = enabled ? 1 : 0;
    if (InterlockedExchange(&g_redraw_enabled, enabled_value) == enabled_value) {
        return;
    }

    InterlockedExchange(&g_force_render_requested, enabled_value);
    RequestCursorStateSync();
    UpdateTrayIconTip();
}

static void SetRenderMode(LONG mode)
{
    if (InterlockedExchange(&g_render_mode, mode) == mode) {
        return;
    }

    InterlockedExchange(&g_force_render_requested, 1);
    RequestCursorStateSync();
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
        InitRawInputBuffer();
        if (!AddTrayIcon(hwnd) ||
            !RegisterRawMouseInput(hwnd)) {
            DestroyWindow(hwnd);
            return -1;
        }
        SetTimer(hwnd, TIMER_CURSOR, TIMER_INTERVAL_MS, NULL);
        return 0;

    case WM_INPUT:
        return ProcessRawInputMessage(hwnd, wparam, lparam);

    case WM_TIMER:
        if (wparam == TIMER_CURSOR) {
            PROFILE_INC(g_profile.state_timer_ticks);
            DWORD last_input_tick = (DWORD)InterlockedCompareExchange(&g_last_input_tick, 0, 0);
            if (GetTickCount() - last_input_tick > MOTION_IDLE_MS) {
                RequestCursorStateSync();
            }
            ProfileQueueSnapshot(false);
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
        case IDM_MODE_COMPAT:
            SetRenderMode(RENDER_MODE_COMPAT);
            return 0;
        case IDM_MODE_FAST:
            SetRenderMode(RENDER_MODE_FAST);
            return 0;
        case IDM_EXIT:
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_RENDER_MODE_FALLBACK:
        UpdateTrayIconTip();
        return 0;

    case WM_TRAYICON:
        if (LOWORD(lparam) == WM_RBUTTONUP || LOWORD(lparam) == WM_CONTEXTMENU) {
            ShowTrayMenu(hwnd);
            return 0;
        }
        break;

    case WM_DESTROY:
        UnregisterRawMouseInput();
        ReleaseRawInputBuffer();
        KillTimer(hwnd, TIMER_CURSOR);
        RemoveTrayIcon();
        g_main_hwnd = NULL;
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

static bool CreateRenderTimer(void)
{
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32) {
        union {
            FARPROC proc;
            CreateWaitableTimerExFn fn;
        } timer_api;
        timer_api.proc = GetProcAddress(kernel32, "CreateWaitableTimerExW");
        if (timer_api.fn) {
            g_render_timer = timer_api.fn(
                NULL,
                NULL,
                CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                TIMER_ALL_ACCESS);
        }
    }

    if (!g_render_timer) {
        g_render_timer = CreateWaitableTimerW(NULL, FALSE, NULL);
        if (g_render_timer) {
            BeginTimerPrecision();
        }
    }
    return g_render_timer != NULL;
}

typedef struct RenderMmcssState {
    HMODULE module;
    HANDLE task;
    AvRevertMmThreadCharacteristicsFn revert;
} RenderMmcssState;

static RenderMmcssState BeginRenderMmcss(void)
{
    RenderMmcssState state;
    ZeroMemory(&state, sizeof(state));

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    state.module = LoadLibraryW(L"avrt.dll");
    if (!state.module) {
        return state;
    }

    union {
        FARPROC proc;
        AvSetMmThreadCharacteristicsFn characteristics;
        AvSetMmThreadPriorityFn priority;
        AvRevertMmThreadCharacteristicsFn revert;
    } avrt;

    avrt.proc = GetProcAddress(state.module, "AvSetMmThreadCharacteristicsW");
    AvSetMmThreadCharacteristicsFn set_characteristics = avrt.characteristics;
    avrt.proc = GetProcAddress(state.module, "AvSetMmThreadPriority");
    AvSetMmThreadPriorityFn set_priority = avrt.priority;
    avrt.proc = GetProcAddress(state.module, "AvRevertMmThreadCharacteristics");
    state.revert = avrt.revert;

    if (set_characteristics) {
        DWORD task_index = 0;
        state.task = set_characteristics(L"Capture", &task_index);
        if (state.task && set_priority) {
            set_priority(state.task, 1);
        }
    }

    return state;
}

static void EndRenderMmcss(RenderMmcssState *state)
{
    if (state->task && state->revert) {
        state->revert(state->task);
    }
    if (state->module) {
        FreeLibrary(state->module);
    }
    ZeroMemory(state, sizeof(*state));
}

static void ProcessRenderWake(void)
{
    bool state_sync = InterlockedExchange(&g_state_sync_requested, 0) != 0;
    bool render_requested = InterlockedExchange(&g_render_requested, 0) != 0;
    bool force_render = InterlockedExchange(&g_force_render_requested, 0) != 0;

    bool should_sample =
        state_sync ||
        (render_requested && IsRedrawEnabled() && (!g_render_timer_active || force_render));

    if (should_sample) {
        bool cursor_available = SampleAndRenderCursor(force_render);
        if (!cursor_available) {
            StopRenderTimer();
        } else if (render_requested && IsRedrawEnabled()) {
            StartRenderTimer();
        }
    }
}

static DWORD WINAPI RenderThreadMain(void *parameter)
{
    (void)parameter;
    RenderMmcssState mmcss = BeginRenderMmcss();
    InitDwmTiming();

    bool initialized = CreateRenderTimer() && CreateOverlayWindow();
    InterlockedExchange(&g_render_thread_ready, initialized ? 1 : -1);
    SetEvent(g_render_ready_event);

    if (!initialized) {
        if (g_overlay_hwnd) {
            DestroyWindow(g_overlay_hwnd);
            g_overlay_hwnd = NULL;
        }
        if (g_render_timer) {
            CloseHandle(g_render_timer);
            g_render_timer = NULL;
        }
        CloseDwmTiming();
        EndRenderMmcss(&mmcss);
        return 1;
    }

    HANDLE wait_handles[2] = {g_render_timer, g_render_wake_event};
    bool running = true;
    while (running) {
        DWORD wait_result = MsgWaitForMultipleObjectsEx(
            ARRAYSIZE(wait_handles),
            wait_handles,
            INFINITE,
            QS_ALLINPUT,
            MWMO_INPUTAVAILABLE);

        if (InterlockedCompareExchange(&g_render_stop, 0, 0)) {
            break;
        }
        if (wait_result == WAIT_OBJECT_0) {
            ProcessRenderTimer();
            continue;
        }
        if (wait_result == WAIT_OBJECT_0 + 1) {
            ProcessRenderWake();
            continue;
        }
        if (wait_result != WAIT_OBJECT_0 + ARRAYSIZE(wait_handles)) {
            break;
        }

        MSG message;
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    StopRenderTimer();
    SetOverlayVisible(false);
    if (g_overlay_hwnd) {
        DestroyWindow(g_overlay_hwnd);
        g_overlay_hwnd = NULL;
    }
    ReleaseCanvasResources();
    ReleaseCursorBitmapResources();
    CloseHandle(g_render_timer);
    g_render_timer = NULL;
    CloseDwmTiming();
    EndRenderMmcss(&mmcss);
    return 0;
}

static bool StartRenderThread(void)
{
    InterlockedExchange(&g_render_stop, 0);
    InterlockedExchange(&g_render_thread_ready, 0);
    g_render_wake_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_render_ready_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_render_wake_event || !g_render_ready_event) {
        return false;
    }

    g_render_thread = CreateThread(NULL, 0, RenderThreadMain, NULL, 0, NULL);
    if (!g_render_thread) {
        return false;
    }

    WaitForSingleObject(g_render_ready_event, INFINITE);
    CloseHandle(g_render_ready_event);
    g_render_ready_event = NULL;
    return InterlockedCompareExchange(&g_render_thread_ready, 0, 0) > 0;
}

static void StopRenderThread(void)
{
    if (g_render_thread) {
        InterlockedExchange(&g_render_stop, 1);
        if (g_render_wake_event) {
            SetEvent(g_render_wake_event);
        }
        WaitForSingleObject(g_render_thread, INFINITE);
        CloseHandle(g_render_thread);
        g_render_thread = NULL;
    }
    if (g_render_ready_event) {
        CloseHandle(g_render_ready_event);
        g_render_ready_event = NULL;
    }
    if (g_render_wake_event) {
        CloseHandle(g_render_wake_event);
        g_render_wake_event = NULL;
    }
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

    if (!StartRenderThread()) {
        MessageBoxW(NULL, L"渲染线程启动失败。", APP_NAME, MB_OK | MB_ICONERROR);
        DestroyWindow(hwnd);
        StopRenderThread();
        ProfileClose();
        EndTimerPrecision();
        CloseHandle(mutex);
        return 1;
    }
    RequestCursorStateSync();

    MSG message;
    ZeroMemory(&message, sizeof(message));
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    StopRenderThread();
    ProfileClose();
    EndTimerPrecision();
    CloseHandle(mutex);
    return (int)message.wParam;
}
