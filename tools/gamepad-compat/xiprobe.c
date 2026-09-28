/* Minimal XInput probe for Wine/Proton: prints every XInput state change of slots 0-3 with a
 * millisecond timestamp. Freestanding (no Windows SDK): only kernel32 imports. */
typedef unsigned long DWORD; typedef unsigned short WORD; typedef unsigned char BYTE; typedef short SHORT;
typedef void *HANDLE; typedef int BOOL; typedef void *HMODULE; typedef void *FARPROC;
typedef struct { WORD wButtons; BYTE bLeftTrigger; BYTE bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XINPUT_GAMEPAD;
typedef struct { DWORD dwPacketNumber; XINPUT_GAMEPAD Gamepad; } XINPUT_STATE;
typedef DWORD (__stdcall *XInputGetState_t)(DWORD, XINPUT_STATE *);
__declspec(dllimport) HANDLE __stdcall GetStdHandle(DWORD);
__declspec(dllimport) BOOL __stdcall WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
__declspec(dllimport) void __stdcall Sleep(DWORD);
__declspec(dllimport) void __stdcall ExitProcess(unsigned);
__declspec(dllimport) HMODULE __stdcall LoadLibraryA(const char *);
__declspec(dllimport) FARPROC __stdcall GetProcAddress(HMODULE, const char *);
__declspec(dllimport) DWORD __stdcall GetTickCount(void);
__declspec(dllimport) HANDLE __stdcall CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
static HANDLE out;
static void put(const char *s) { DWORD n = 0, l = 0; while (s[l]) l++; WriteFile(out, s, l, &n, 0); }
static char *num(char *p, long v) { char t[16]; int i = 0; if (v < 0) { *p++ = '-'; v = -v; } do { t[i++] = '0' + v % 10; v /= 10; } while (v); while (i) *p++ = t[--i]; return p; }
void __stdcall start(void) {
    /* Log to the file named Z:\...\xiprobe.log so output survives umu/pressure-vessel. */
    out = CreateFileA("xiprobe.log", 0x40000000 /*GENERIC_WRITE*/, 1, 0, 2 /*CREATE_ALWAYS*/, 0x80, 0);
    HMODULE x = LoadLibraryA("xinput1_4.dll"); if (!x) x = LoadLibraryA("xinput1_3.dll");
    XInputGetState_t get = x ? (XInputGetState_t)GetProcAddress(x, "XInputGetState") : 0;
    if (!get) { put("no xinput\r\n"); ExitProcess(1); }
    XINPUT_STATE last[4]; DWORD conn[4]; for (int i = 0; i < 4; i++) { conn[i] = 99; last[i].dwPacketNumber = 0xffffffff; }
    DWORD t0 = GetTickCount();
    put("xiprobe ready\r\n");
    for (;;) {
        for (DWORD i = 0; i < 4; i++) {
            XINPUT_STATE s; DWORD r = get(i, &s);
            if (r != conn[i]) { char b[64], *p = b; p = num(p, GetTickCount() - t0); *p++ = ' '; *p++ = 's'; p = num(p, i); for (const char *q = r ? " disconnected\r\n" : " connected\r\n"; *q; ) *p++ = *q++; *p = 0; put(b); conn[i] = r; }
            if (r) continue;
            XINPUT_GAMEPAD *g = &s.Gamepad, *o = &last[i].Gamepad;
            if (last[i].dwPacketNumber != 0xffffffff && g->wButtons == o->wButtons && g->bLeftTrigger == o->bLeftTrigger && g->bRightTrigger == o->bRightTrigger &&
                g->sThumbLX == o->sThumbLX && g->sThumbLY == o->sThumbLY && g->sThumbRX == o->sThumbRX && g->sThumbRY == o->sThumbRY) continue;
            last[i] = s;
            char b[160], *p = b; p = num(p, GetTickCount() - t0); *p++ = ' '; *p++ = 's'; p = num(p, i);
            *p++ = ' '; *p++ = 'b'; p = num(p, g->wButtons); *p++ = ' '; *p++ = 'l'; *p++ = 't'; p = num(p, g->bLeftTrigger); *p++ = ' '; *p++ = 'r'; *p++ = 't'; p = num(p, g->bRightTrigger);
            *p++ = ' '; *p++ = 'L'; p = num(p, g->sThumbLX); *p++ = ','; p = num(p, g->sThumbLY); *p++ = ' '; *p++ = 'R'; p = num(p, g->sThumbRX); *p++ = ','; p = num(p, g->sThumbRY);
            *p++ = '\r'; *p++ = '\n'; *p = 0; put(b);
        }
        if (GetTickCount() - t0 > 90000) ExitProcess(0);
        Sleep(10);
    }
}
