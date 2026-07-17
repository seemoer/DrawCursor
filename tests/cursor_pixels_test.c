#define wWinMain DrawCursorApplicationMain
#include "../src/main.c"

typedef struct CursorTestCase {
    LPCWSTR resource;
    const char *name;
    bool expect_high_contrast;
} CursorTestCase;

static bool TestCanvasSizing(void)
{
    g_cursor.width = 48;
    g_cursor.height = 48;
    g_cursor.hotspot_x = 4;
    g_cursor.hotspot_y = 4;
    g_compat_canvas_size = CURSOR_CANVAS_SIZE_DEFAULT;
    if (CursorCanvasSize() != CURSOR_CANVAS_SIZE_DEFAULT) {
        fprintf(stderr, "default canvas size changed unexpectedly\n");
        return false;
    }

    g_compat_canvas_size = CURSOR_CANVAS_SIZE_EXPERIMENTAL;
    if (CursorCanvasSize() != CURSOR_CANVAS_SIZE_EXPERIMENTAL) {
        fprintf(stderr, "experimental canvas size was not selected\n");
        return false;
    }

    g_cursor.width = 300;
    g_cursor.height = 50;
    g_cursor.hotspot_x = 0;
    g_cursor.hotspot_y = 25;
    int required = CursorCanvasSize();
    POINT position = {1000, 500};
    g_overlay_visible = true;
    g_canvas_w = required;
    g_canvas_h = required;
    g_canvas_x = position.x - required / 2;
    g_canvas_y = position.y - required / 2;
    bool fits = CursorFitsCanvas(position);
    g_overlay_visible = false;
    g_canvas_w = 0;
    g_canvas_h = 0;
    if (required < (300 + CURSOR_CANVAS_MARGIN) * 2 || !fits) {
        fprintf(stderr, "asymmetric hotspot is not safely contained\n");
        return false;
    }

    printf("Canvas: default 384, experimental 256, asymmetric hotspot safe\n");
    return true;
}

static bool TestSystemCursor(CursorTestCase test)
{
    HCURSOR cursor = LoadCursorW(NULL, test.resource);
    if (!cursor) {
        fprintf(stderr, "%s: LoadCursorW failed\n", test.name);
        return false;
    }

    UpdateCursorMetrics(cursor);
    if (!EnsureCursorBitmapResources()) {
        fprintf(stderr, "%s: cursor bitmap conversion failed\n", test.name);
        return false;
    }

    int visible = 0;
    int black = 0;
    int white = 0;
    int pixel_count = g_cursor_bitmap_w * g_cursor_bitmap_h;
    for (int i = 0; i < pixel_count; ++i) {
        DWORD pixel = g_cursor_pixels[i];
        if ((pixel >> 24) == 0) {
            continue;
        }
        ++visible;
        if (pixel == 0xFF000000) {
            ++black;
        } else if (pixel == 0xFFFFFFFF) {
            ++white;
        }
    }

    if (visible == 0) {
        fprintf(stderr, "%s: converted cursor is fully transparent\n", test.name);
        return false;
    }
    if (test.expect_high_contrast && (black == 0 || white == 0)) {
        fprintf(
            stderr,
            "%s: XOR cursor lacks black core or white outline (%d black, %d white)\n",
            test.name,
            black,
            white);
        return false;
    }

    printf(
        "%s: %dx%d, %d visible, %d black, %d white\n",
        test.name,
        g_cursor_bitmap_w,
        g_cursor_bitmap_h,
        visible,
        black,
        white);
    return true;
}

int main(void)
{
    SetBestDpiAwareness();

    const CursorTestCase tests[] = {
        {IDC_ARROW, "Arrow", false},
        {IDC_IBEAM, "IBeam", true},
        {IDC_WAIT, "Wait", false},
        {IDC_CROSS, "Cross", true},
        {IDC_HAND, "Hand", false},
        {IDC_SIZEALL, "SizeAll", false},
        {IDC_NO, "No", false},
    };

    bool ok = true;
    ok = TestCanvasSizing() && ok;
    for (size_t i = 0; i < ARRAYSIZE(tests); ++i) {
        if (!TestSystemCursor(tests[i])) {
            ok = false;
        }
    }

    ReleaseCanvasResources();
    ReleaseCursorBitmapResources();
    return ok ? 0 : 1;
}
