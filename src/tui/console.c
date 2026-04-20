#include "tui.h"

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <conio.h>

static DWORD g_saved_out_mode = 0;
static DWORD g_saved_in_mode  = 0;
static int   g_saved_valid    = 0;

static void tui_restore(void) {
    HANDLE ho = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hi = GetStdHandle(STD_INPUT_HANDLE);
    if (g_saved_valid) {
        SetConsoleMode(ho, g_saved_out_mode);
        SetConsoleMode(hi, g_saved_in_mode);
    }
    fputs("\x1b[0m", stdout);
    fputs("\x1b[?25h", stdout);
    fputs("\x1b[999;1H", stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

void tui_console_init(void) {
    HANDLE ho = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hi = GetStdHandle(STD_INPUT_HANDLE);
    DWORD om = 0, im = 0;
    if (GetConsoleMode(ho, &om) && GetConsoleMode(hi, &im)) {
        g_saved_out_mode = om;
        g_saved_in_mode  = im;
        g_saved_valid    = 1;
        SetConsoleMode(ho, om | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT);
        DWORD new_in = im;
        new_in &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
        SetConsoleMode(hi, new_in);
    }
    SetConsoleOutputCP(CP_UTF8);
    atexit(tui_restore);
    fputs("\x1b[2J", stdout);
    fputs("\x1b[?25l", stdout);
    fflush(stdout);
}

void tui_console_shutdown(void) {
    tui_restore();
}

int tui_kbhit(void) { return _kbhit(); }

int tui_getch(void) {
    int c = _getch();
    if (c == 0 || c == 0xE0) {
        (void)_getch();
        return -1;
    }
    return c;
}
