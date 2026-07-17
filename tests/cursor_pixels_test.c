#define wWinMain DrawCursorApplicationMain
#include "../src/main.c"

typedef struct CursorTestCase {
    LPCWSTR resource;
    const char *name;
    bool expect_high_contrast;
} CursorTestCase;

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
    for (size_t i = 0; i < ARRAYSIZE(tests); ++i) {
        if (!TestSystemCursor(tests[i])) {
            ok = false;
        }
    }

    ReleaseCanvasResources();
    ReleaseCursorBitmapResources();
    return ok ? 0 : 1;
}
