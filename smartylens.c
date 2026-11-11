/* Smarty Lens - evidenziazione intelligente.
 * Evidenzi il nome di una funzione in qualunque programma e compare, accanto alla
 * selezione, la sua documentazione ufficiale (presa da DevDocs, senza AI ne' chiavi).
 * Un solo file C, solo API Win32. Compilazione: vedi build.ps1
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000006
#endif
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <shlobj.h>
#include <richedit.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <wchar.h>
#include <ctype.h>

#ifndef WINHTTP_OPTION_DECOMPRESSION
#define WINHTTP_OPTION_DECOMPRESSION 118
#define WINHTTP_DECOMPRESSION_FLAG_ALL 3
#endif
#ifndef WINHTTP_OPTION_IPV6_FAST_FALLBACK
#define WINHTTP_OPTION_IPV6_FAST_FALLBACK 140
#endif
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#ifndef MSFTEDIT_CLASS
#define MSFTEDIT_CLASS L"RICHEDIT50W"
#endif

#define WM_APP_TRIGGER (WM_APP + 1)
#define WM_APP_STATUS  (WM_APP + 2)
#define WM_APP_RESULT  (WM_APP + 3)
#define WM_APP_CLOSE   (WM_APP + 4)
#define T_TRIGGER 1
#define T_AUTOCLOSE 2
#define MAXC 6
#define DOCS_HOST L"documents.devdocs.io"

/* ------------------------------------------------------------------ tipi */
typedef struct { char *p; size_t n, cap; } Buf;
typedef struct { char base[40], slug[64], label[64], root[24]; int ok; } Doc;
typedef struct { int doc, score; char full[200], path[260]; } Cand;
typedef struct { char q[200], last[80], hint[24], dochint[24], shown[200]; int nseg, only; } Query;
typedef struct { COLORREF bg, head, text, dim, accent, code, rose, border; int dark; } Theme;
enum { REQ_SEL, REQ_TEXT, REQ_CAND, REQ_SAVED };
typedef struct { int type; UINT gen; wchar_t text[300]; int ncand, pick; Cand cand[MAXC]; wchar_t file[MAX_PATH]; } Req;
typedef struct {
    UINT gen; int ok;
    wchar_t *title, *label, *url, *msg; char *rtf;
    char *blob; size_t blob_len; wchar_t save_path[MAX_PATH]; int saved;
    int ncand, pick; Cand cand[MAXC];
} Res;

/* --------------------------------------------------------------- globali */
static wchar_t g_data[MAX_PATH], g_idx[MAX_PATH], g_savedir[MAX_PATH], g_ini[MAX_PATH];
static HWND g_main, g_pop, g_rich;
static HHOOK g_hook;
static int g_active, g_only_ibeam = 1, g_autosave, g_dpi = 96;
static UINT g_gen;
static volatile LONG g_busy;
static CRITICAL_SECTION g_cs;
static Doc g_docs[20]; static int g_ndocs, g_docs_ready;
static ULONGLONG g_net_fail;
static char g_alias_k[40][24], g_alias_v[40][64]; static int g_nalias;
static Res *g_res; static wchar_t g_msg[300]; static int g_pop_vis, g_richH;
static POINT g_anchor, g_trig_pt;
static RECT g_rSave, g_rOpen, g_rClose, g_rAlt[MAXC]; static int g_altIdx[MAXC], g_nAlt;
static HFONT g_fUI, g_fBold; static Theme g_th;
static wchar_t (*g_menuFiles)[MAX_PATH]; static int g_nMenuFiles;
#define S(x) MulDiv((x), g_dpi, 96)

/* --------------------------------------------------------------- utilita' */
static void buf_put(Buf *b, const char *s, size_t len) {
    if (b->n + len + 1 > b->cap) {
        size_t c = b->cap ? b->cap * 2 : 1024;
        while (c < b->n + len + 1) c *= 2;
        b->p = (char *)realloc(b->p, c); b->cap = c;
    }
    memcpy(b->p + b->n, s, len); b->n += len; b->p[b->n] = 0;
}
static void buf_puts(Buf *b, const char *s) { buf_put(b, s, strlen(s)); }
static void buf_printf(Buf *b, const char *fmt, ...) {
    char t[1024]; va_list ap; va_start(ap, fmt); vsnprintf(t, sizeof t, fmt, ap); va_end(ap);
    t[sizeof t - 1] = 0; buf_puts(b, t);
}
static void wfmt(wchar_t *out, size_t n, const wchar_t *fmt, ...) {
    va_list ap; va_start(ap, fmt); _vsnwprintf(out, n, fmt, ap); va_end(ap); out[n - 1] = 0;
}
static wchar_t *u8w(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = (wchar_t *)malloc((n + 1) * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n); w[n] = 0; return w;
}
static char *wu8(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *s = (char *)malloc(n + 1);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL); s[n] = 0; return s;
}
static char *read_file(const wchar_t *path, size_t *len) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    DWORD sz = GetFileSize(h, NULL), rd = 0;
    char *p = (char *)malloc((size_t)sz + 1);
    ReadFile(h, p, sz, &rd, NULL); CloseHandle(h);
    p[rd] = 0; if (len) *len = rd; return p;
}
static int write_file(const wchar_t *path, const char *data, size_t len) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD wr = 0; BOOL ok = WriteFile(h, data, (DWORD)len, &wr, NULL); CloseHandle(h);
    return ok && wr == len;
}
static double file_age_days(const wchar_t *path) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &a)) return -1;
    FILETIME now; GetSystemTimeAsFileTime(&now);
    ULONGLONG t0 = ((ULONGLONG)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
    ULONGLONG t1 = ((ULONGLONG)now.dwHighDateTime << 32) | now.dwLowDateTime;
    return t1 > t0 ? (double)(t1 - t0) / 864000000000.0 : 0;
}
static const char *find_n(const char *h, const char *end, const char *needle) {
    size_t n = strlen(needle);
    if (!n || (size_t)(end - h) < n) return NULL;
    for (const char *e = end - n; h <= e; h++)
        if (*h == *needle && !memcmp(h, needle, n)) return h;
    return NULL;
}
static void trim_memory(void) { SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1); }

/* ------------------------------------------------- percorsi e configurazione */
static const char DEFAULT_INI[] =
    "; Smarty Lens - configurazione (riavvia Smarty Lens dopo averla modificata)\r\n"
    "[lente]\r\n"
    "; 1 = evidenziazione gia' attiva all'avvio\r\n"
    "attivo_all_avvio=0\r\n"
    "; 1 = reagisce solo se il cursore e' quello di testo (evita attivazioni trascinando finestre)\r\n"
    "solo_cursore_testo=1\r\n"
    "; 1 = salva automaticamente ogni funzione consultata, per rileggerla offline\r\n"
    "salva_automatico=0\r\n"
    "; documentazioni da consultare, in ordine di priorita' (nomi come su https://devdocs.io)\r\n"
    "docs=python,numpy,pandas,matplotlib,scikit_learn,pytorch,tensorflow,statsmodels,requests\r\n"
    "x=-1\r\n"
    "y=-1\r\n"
    "\r\n"
    "; abbreviazioni usate negli import: nome=modulo\r\n"
    "[alias]\r\n"
    "np=numpy\r\npd=pandas\r\nplt=matplotlib.pyplot\r\nmpl=matplotlib\r\nnn=torch.nn\r\n"
    "F=torch.nn.functional\r\noptim=torch.optim\r\ntensorflow=tf\r\nkeras=tf.keras\r\n"
    "sm=statsmodels.api\r\nsmf=statsmodels.formula.api\r\n";

static void init_paths(void) {
    wchar_t exe[MAX_PATH], t[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t *s = wcsrchr(exe, L'\\'); if (s) *s = 0;
    wfmt(g_data, MAX_PATH, L"%ls\\dati", exe);
    CreateDirectoryW(g_data, NULL);
    wfmt(t, MAX_PATH, L"%ls\\.prova", g_data);
    HANDLE h = CreateFileW(t, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (h == INVALID_HANDLE_VALUE) {              /* cartella non scrivibile: ripiego */
        wchar_t ad[MAX_PATH];
        SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, ad);
        wfmt(g_data, MAX_PATH, L"%ls\\SmartyLens", ad);
        CreateDirectoryW(g_data, NULL);
    } else CloseHandle(h);
    wfmt(g_idx, MAX_PATH, L"%ls\\indici", g_data);      CreateDirectoryW(g_idx, NULL);
    wfmt(g_savedir, MAX_PATH, L"%ls\\salvate", g_data); CreateDirectoryW(g_savedir, NULL);
    wfmt(g_ini, MAX_PATH, L"%ls\\config.ini", g_data);
    if (GetFileAttributesW(g_ini) == INVALID_FILE_ATTRIBUTES) write_file(g_ini, DEFAULT_INI, sizeof DEFAULT_INI - 1);
}
static void load_cfg(void) {
    g_only_ibeam = GetPrivateProfileIntW(L"lente", L"solo_cursore_testo", 1, g_ini);
    g_autosave = GetPrivateProfileIntW(L"lente", L"salva_automatico", 0, g_ini);
    wchar_t sec[4096];
    DWORD n = GetPrivateProfileSectionW(L"alias", sec, 4096, g_ini);
    for (wchar_t *p = sec; n && *p && g_nalias < 40; p += wcslen(p) + 1) {
        wchar_t *eq = wcschr(p, L'='); if (!eq) continue;
        *eq = 0;
        WideCharToMultiByte(CP_UTF8, 0, p, -1, g_alias_k[g_nalias], 24, NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, eq + 1, -1, g_alias_v[g_nalias], 64, NULL, NULL);
        g_alias_k[g_nalias][23] = 0; g_alias_v[g_nalias][63] = 0;
        *eq = L'='; g_nalias++;
    }
}
static void get_theme(Theme *t) {
    DWORD v = 1, sz = sizeof v;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &v, &sz);
    t->dark = !v;
    /* Palette: Platinum EDEFF3, Alabaster Grey E0E3EA, Charcoal Blue 383C45, Ruby Red 970126,
       Crimson Violet 690731, Rosewood BC0555. In tema scuro i rossi sono schiariti per restare leggibili. */
    if (v) { t->bg = RGB(0xED,0xEF,0xF3); t->head = RGB(0xE0,0xE3,0xEA); t->text = RGB(0x38,0x3C,0x45); t->dim = RGB(0x6E,0x73,0x7E);
             t->accent = RGB(0x97,0x01,0x26); t->code = RGB(0x69,0x07,0x31); t->rose = RGB(0xBC,0x05,0x55); t->border = RGB(0xC3,0xC8,0xD2); }
    else   { t->bg = RGB(0x38,0x3C,0x45); t->head = RGB(0x2D,0x30,0x38); t->text = RGB(0xED,0xEF,0xF3); t->dim = RGB(0xA9,0xAE,0xB9);
             t->accent = RGB(0xFF,0x8F,0xB5); t->code = RGB(0xF2,0xB8,0xCC); t->rose = RGB(0xFF,0x5C,0x9A); t->border = RGB(0x55,0x5A,0x66); }
}

/* -------------------------------------------------------------------- rete */
typedef struct { PVOID volatile req; HANDLE done; DWORD ms; } Watch;
static DWORD WINAPI watch_thread(LPVOID p) {
    Watch *w = (Watch *)p;
    if (WaitForSingleObject(w->done, w->ms) == WAIT_TIMEOUT) {
        HINTERNET h = (HINTERNET)InterlockedExchangePointer(&w->req, NULL);
        if (h) WinHttpCloseHandle(h);
    }
    return 0;
}
static char *http_get(const wchar_t *host, const wchar_t *path, size_t *outLen, int *status) {
    *status = 0;
    for (int attempt = 0; attempt < 3; attempt++) {
        char *res = NULL; size_t n = 0, cap = 0; int ok = 0;
        HINTERNET s = WinHttpOpen(L"SmartyLens/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!s) s = WinHttpOpen(L"SmartyLens/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (s) {
            WinHttpSetTimeouts(s, 3000, 3000, 4000, 5000);
            DWORD on = 1;                 /* se l'IPv6 della rete non funziona, passa subito all'IPv4 */
            WinHttpSetOption(s, WINHTTP_OPTION_IPV6_FAST_FALLBACK, &on, sizeof on);
            HINTERNET c = WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
            if (c) {
                /* La CDN a volte blocca per 20-30 s la prima richiesta di una pagina non in cache:
                   dal secondo tentativo si aggiunge un parametro che aggira la cache bloccata. */
                static const DWORD WAIT[3] = { 2000, 3500, 6000 };
                wchar_t p2[800];
                if (attempt) wfmt(p2, 800, L"%ls?r=%d", path, attempt); else wfmt(p2, 800, L"%ls", path);
                HINTERNET r = WinHttpOpenRequest(c, L"GET", p2, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
                if (r) {
                    DWORD dec = WINHTTP_DECOMPRESSION_FLAG_ALL;
                    WinHttpSetOption(r, WINHTTP_OPTION_DECOMPRESSION, &dec, sizeof dec);
                    /* guardia: se la risposta non arriva in tempo, annulla e riprova (attese crescenti) */
                    Watch w; w.req = r; w.ms = WAIT[attempt]; w.done = CreateEventW(NULL, TRUE, FALSE, NULL);
                    HANDLE th = CreateThread(NULL, 0, watch_thread, &w, 0, NULL);
                    BOOL got = WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                               WinHttpReceiveResponse(r, NULL);
                    SetEvent(w.done);
                    if (th) { WaitForSingleObject(th, INFINITE); CloseHandle(th); }
                    CloseHandle(w.done);
                    r = (HINTERNET)InterlockedExchangePointer(&w.req, NULL);      /* NULL = annullata dalla guardia */
                    if (r && got) {
                        DWORD code = 0, sz = sizeof code;
                        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                            WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
                        *status = (int)code;
                        if (code == 200) {
                            ok = 1;
                            for (;;) {
                                DWORD avail = 0, rd = 0;
                                if (!WinHttpQueryDataAvailable(r, &avail)) { ok = 0; break; }
                                if (!avail) break;
                                if (n + avail + 1 > cap) { cap = (n + avail + 1) * 2; res = (char *)realloc(res, cap); }
                                if (!WinHttpReadData(r, res + n, avail, &rd)) { ok = 0; break; }
                                n += rd;
                                if (n > (64u << 20)) break;
                            }
                        }
                    }
                    if (r) WinHttpCloseHandle(r);
                }
                WinHttpCloseHandle(c);
            }
            WinHttpCloseHandle(s);
        }
        if (ok) {
            if (!res) res = (char *)malloc(1);
            res[n] = 0; *outLen = n; g_net_fail = 0; return res;
        }
        free(res);
        if (*status >= 300 && *status < 500) return NULL;      /* es. 404: inutile riprovare */
    }
    g_net_fail = GetTickCount64() | 1;
    return NULL;
}
static int net_ok(void) { return !g_net_fail || GetTickCount64() - g_net_fail > 60000; }

/* ------------------------------------------------------------ mini JSON */
typedef struct { const char *p, *e; } JS;
static void js_ws(JS *j) { while (j->p < j->e && (unsigned char)*j->p <= ' ') j->p++; }
static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0; }
static int utf8_enc(unsigned cp, char *o) {
    if (cp < 0x80) { o[0] = (char)cp; return 1; }
    if (cp < 0x800) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) { o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[2] = (char)(0x80 | (cp & 0x3F)); return 3; }
    o[0] = (char)(0xF0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); o[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[3] = (char)(0x80 | (cp & 0x3F)); return 4;
}
static int js_str(JS *j, char *out, int cap) {
    js_ws(j);
    if (j->p >= j->e || *j->p != '"') return 0;
    j->p++;
    char *o = out, *oe = out ? out + cap - 5 : NULL;
    while (j->p < j->e && *j->p != '"') {
        unsigned c = (unsigned char)*j->p++;
        if (c == '\\' && j->p < j->e) {
            char x = *j->p++;
            if (x == 'u' && j->e - j->p >= 4) {
                unsigned cp = (hexv(j->p[0]) << 12) | (hexv(j->p[1]) << 8) | (hexv(j->p[2]) << 4) | hexv(j->p[3]);
                j->p += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && j->e - j->p >= 6 && j->p[0] == '\\' && j->p[1] == 'u') {
                    unsigned lo = (hexv(j->p[2]) << 12) | (hexv(j->p[3]) << 8) | (hexv(j->p[4]) << 4) | hexv(j->p[5]);
                    j->p += 6; cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                if (out && o < oe) o += utf8_enc(cp, o);
                continue;
            }
            c = x == 'n' ? '\n' : x == 't' ? '\t' : x == 'r' ? '\r' : x == 'b' || x == 'f' ? ' ' : (unsigned char)x;
        }
        if (out && o < oe) *o++ = (char)c;
    }
    if (j->p < j->e) j->p++;
    if (out) *o = 0;
    return 1;
}
static void js_skip(JS *j) {
    js_ws(j);
    if (j->p >= j->e) return;
    char c = *j->p;
    if (c == '"') { js_str(j, NULL, 0); return; }
    if (c == '{' || c == '[') {
        int d = 0;
        while (j->p < j->e) {
            c = *j->p;
            if (c == '"') { js_str(j, NULL, 0); continue; }
            if (c == '{' || c == '[') d++;
            else if (c == '}' || c == ']') { if (--d == 0) { j->p++; return; } }
            j->p++;
        }
        return;
    }
    while (j->p < j->e && *j->p != ',' && *j->p != '}' && *j->p != ']') j->p++;
}
static int js_obj_next(JS *j, char *key, int cap) {
    js_ws(j);
    if (j->p < j->e && (*j->p == '{' || *j->p == ',')) j->p++;
    js_ws(j);
    if (j->p >= j->e || *j->p == '}') { if (j->p < j->e) j->p++; return 0; }
    if (!js_str(j, key, cap)) return 0;
    js_ws(j);
    if (j->p < j->e && *j->p == ':') j->p++;
    return 1;
}
static int js_arr_next(JS *j) {
    js_ws(j);
    if (j->p < j->e && (*j->p == '[' || *j->p == ',')) j->p++;
    js_ws(j);
    if (j->p >= j->e || *j->p == ']') { if (j->p < j->e) j->p++; return 0; }
    return 1;
}

/* ------------------------------------------------- elenco documentazioni */
static const char *ROOTS[][2] = { {"python",""}, {"numpy","numpy"}, {"pandas","pandas"}, {"matplotlib","matplotlib"},
    {"scikit_learn","sklearn"}, {"pytorch","torch"}, {"tensorflow","tf"}, {"statsmodels","statsmodels"},
    {"requests","requests"}, {"scikit_image","skimage"}, {NULL,NULL} };

static void post_status(UINT gen, const wchar_t *fmt, ...) {
    if (!gen || !g_main) return;
    wchar_t b[300]; va_list ap; va_start(ap, fmt); _vsnwprintf(b, 300, fmt, ap); va_end(ap); b[299] = 0;
    PostMessageW(g_main, WM_APP_STATUS, gen, (LPARAM)_wcsdup(b));
}
static void idx_path(const Doc *d, wchar_t *out) { wfmt(out, MAX_PATH, L"%ls\\%hs.json", g_idx, d->slug); }

static void resolve_slugs(const char *js, size_t n) {
    JS j = { js, js + n };
    while (js_arr_next(&j)) {
        char key[32], name[80] = "", slug[80] = "", ver[40] = "";
        while (js_obj_next(&j, key, sizeof key)) {
            if (!strcmp(key, "name")) js_str(&j, name, sizeof name);
            else if (!strcmp(key, "slug")) js_str(&j, slug, sizeof slug);
            else if (!strcmp(key, "version")) js_str(&j, ver, sizeof ver);
            else js_skip(&j);
        }
        for (int i = 0; i < g_ndocs; i++) {
            Doc *d = &g_docs[i]; size_t bl = strlen(d->base);
            if (d->slug[0] || strncmp(slug, d->base, bl) || (slug[bl] && slug[bl] != '~')) continue;
            snprintf(d->slug, sizeof d->slug, "%s", slug);
            if (ver[0]) snprintf(d->label, sizeof d->label, "%s %s", name, ver);
            else snprintf(d->label, sizeof d->label, "%s", name);
        }
    }
}
/* Prepara l'elenco delle documentazioni e scarica gli indici mancanti. Ritorna quante sono usabili. */
static int ensure_docs(UINT gen) {
    EnterCriticalSection(&g_cs);
    wchar_t sl[MAX_PATH], p[MAX_PATH];
    wfmt(sl, MAX_PATH, L"%ls\\_versioni.txt", g_idx);
    if (!g_docs_ready) {
        wchar_t wd[600]; char docs[600];
        GetPrivateProfileStringW(L"lente", L"docs", L"python,numpy,pandas,matplotlib,scikit_learn,pytorch,tensorflow,statsmodels,requests", wd, 600, g_ini);
        WideCharToMultiByte(CP_UTF8, 0, wd, -1, docs, sizeof docs, NULL, NULL); docs[sizeof docs - 1] = 0;
        g_ndocs = 0;
        for (char *t = strtok(docs, ", ;"); t && g_ndocs < 20; t = strtok(NULL, ", ;")) {
            Doc *d = &g_docs[g_ndocs++]; memset(d, 0, sizeof *d);
            snprintf(d->base, sizeof d->base, "%s", t);
            for (int i = 0; ROOTS[i][0]; i++) if (!strcmp(ROOTS[i][0], t)) strcpy(d->root, ROOTS[i][1]);
        }
        char old[20][64]; memset(old, 0, sizeof old);
        size_t n; char *txt = read_file(sl, &n);           /* versioni gia' note: base \t slug \t etichetta */
        if (txt) {
            for (char *ln = strtok(txt, "\r\n"); ln; ln = strtok(NULL, "\r\n")) {
                char *a = strchr(ln, '\t'); if (!a) continue; *a++ = 0;
                char *b = strchr(a, '\t'); if (!b) continue; *b++ = 0;
                for (int i = 0; i < g_ndocs; i++) if (!strcmp(g_docs[i].base, ln)) {
                    snprintf(g_docs[i].slug, 64, "%s", a); snprintf(g_docs[i].label, 64, "%s", b); strcpy(old[i], g_docs[i].slug);
                }
            }
            free(txt);
        }
        int missing = 0; for (int i = 0; i < g_ndocs; i++) if (!g_docs[i].slug[0]) missing = 1;
        double age = file_age_days(sl);
        int done = 1;
        if ((missing || age < 0 || age > 14) && net_ok()) {
            int st; char *js = http_get(L"devdocs.io", L"/docs.json", &n, &st);
            if (js) {
                for (int i = 0; i < g_ndocs; i++) g_docs[i].slug[0] = 0;
                resolve_slugs(js, n); free(js);
                Buf b = {0};
                for (int i = 0; i < g_ndocs; i++) {
                    Doc *d = &g_docs[i];
                    if (!d->slug[0] && old[i][0]) strcpy(d->slug, old[i]);
                    if (!d->slug[0]) continue;
                    buf_printf(&b, "%s\t%s\t%s\n", d->base, d->slug, d->label);
                    if (old[i][0] && strcmp(old[i], d->slug)) {      /* nuova versione: via il vecchio indice */
                        wfmt(p, MAX_PATH, L"%ls\\%hs.json", g_idx, old[i]); DeleteFileW(p);
                    }
                }
                if (b.p) { write_file(sl, b.p, b.n); free(b.p); }
            } else if (missing) done = 0;                     /* si riprovera' al prossimo giro */
        }
        g_docs_ready = done;
    }
    int usable = 0, todo = 0, k = 0;
    for (int i = 0; i < g_ndocs; i++) if (g_docs[i].slug[0]) { idx_path(&g_docs[i], p); if (file_age_days(p) < 0) todo++; }
    for (int i = 0; i < g_ndocs; i++) {
        Doc *d = &g_docs[i];
        if (!d->slug[0]) { d->ok = 0; continue; }
        idx_path(d, p);
        double age = file_age_days(p);
        if ((age < 0 || age > 30) && net_ok()) {
            if (age < 0) post_status(gen, L"Scarico gli indici, solo la prima volta… %d/%d", ++k, todo);
            wchar_t up[200]; size_t n; int st;
            wfmt(up, 200, L"/%hs/index.json", d->slug);
            char *js = http_get(DOCS_HOST, up, &n, &st);
            if (js) { if (n > 20) write_file(p, js, n); free(js); }
        }
        d->ok = file_age_days(p) >= 0;
        usable += d->ok;
    }
    LeaveCriticalSection(&g_cs);
    return usable;
}
static DWORD WINAPI warm_thread(LPVOID a) { (void)a; ensure_docs(0); return 0; }

/* --------------------------------------------------- analisi della selezione */
static int is_id0(int c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
static int is_id(int c) { return is_id0(c) || (c >= '0' && c <= '9'); }
static int is_kw(const char *s, size_t n) {
    static const char *k[] = { "if","elif","else","while","for","and","or","not","in","is","return","yield","with",
        "assert","lambda","del","except","raise","await","async","def","class","import","from","as","try","finally", NULL };
    for (int i = 0; k[i]; i++) if (strlen(k[i]) == n && !strncmp(k[i], s, n)) return 1;
    return 0;
}
static int ident_path(const char *s) {
    if (!is_id0((unsigned char)*s)) return 0;
    for (; *s; s++) {
        if (*s == '.') { if (!is_id0((unsigned char)s[1])) return 0; }
        else if (!is_id((unsigned char)*s)) return 0;
    }
    return 1;
}
static int nsegs(const char *s) { int n = 1; for (; *s; s++) if (*s == '.') n++; return n; }
static const char *last_seg(const char *s) { const char *d = strrchr(s, '.'); return d ? d + 1 : s; }
static int path_end(const char *s, int i, int n) {       /* fine di un percorso tipo a.b.c che inizia in i */
    while (i < n && (is_id((unsigned char)s[i]) || (s[i] == '.' && i + 1 < n && is_id0((unsigned char)s[i + 1])))) i++;
    return i;
}
static int parse_query(const char *in, Query *q) {
    memset(q, 0, sizeof *q);
    while (*in && (unsigned char)*in <= ' ') in++;
    int n = (int)strlen(in);
    while (n > 0 && (unsigned char)in[n - 1] <= ' ') n--;
    if (n == 0 || n > 400) return 0;
    int nl = 0; for (int i = 0; i < n; i++) if (in[i] == '\n') nl++;
    if (nl > 3) return 0;
    const char *s = in; char P[200] = ""; int st = -1, en = -1;
    if (n > 5 && !strncmp(s, "from ", 5)) {                 /* from modulo import nome */
        int a = 5; while (a < n && s[a] == ' ') a++;
        int b = path_end(s, a, n), c = b;
        while (c < n && s[c] == ' ') c++;
        if (b > a && c + 7 < n && !strncmp(s + c, "import ", 7)) {
            c += 7; while (c < n && (s[c] == ' ' || s[c] == '(')) c++;
            int d = c; while (d < n && is_id((unsigned char)s[d])) d++;
            if (d > c && b - a + d - c + 2 < (int)sizeof P) { snprintf(P, sizeof P, "%.*s.%.*s", b - a, s + a, d - c, s + c); st = 0; }
        }
    } else if (n > 7 && !strncmp(s, "import ", 7)) {
        int a = 7; while (a < n && s[a] == ' ') a++;
        int b = path_end(s, a, n);
        if (b > a && b - a < (int)sizeof P) { snprintf(P, sizeof P, "%.*s", b - a, s + a); st = 0; }
    }
    if (st < 0) {                                           /* prima chiamata: nome( */
        for (int i = 0; i < n;) {
            if (is_id0((unsigned char)s[i]) && (i == 0 || !is_id((unsigned char)s[i - 1]))) {
                int a = i; i = path_end(s, i, n);
                if (i < n && s[i] == '(') {
                    const char *ls = s + a; for (int k = a; k < i; k++) if (s[k] == '.') ls = s + k + 1;
                    int def = (a >= 4 && !strncmp(s + a - 4, "def ", 4)) || (a >= 6 && !strncmp(s + a - 6, "class ", 6));
                    if (!def && !is_kw(ls, (size_t)(s + i - ls))) { st = a; en = i; break; }
                }
            } else i++;
        }
        if (st < 0) {                                       /* altrimenti: la selezione e' un solo nome */
            int a = 0, b = n;
            if (s[a] == '@') a++;
            while (b > a && strchr(":,;()[]. \t", s[b - 1])) b--;
            if (b <= a || !is_id0((unsigned char)s[a]) || path_end(s, a, b) != b) return 0;
            st = a; en = b;
        }
        if (en - st >= (int)sizeof P - 4) return 0;
        snprintf(P, sizeof P, "%.*s", en - st, s + st);
    }
    snprintf(q->shown, sizeof q->shown, "%s", P);
    char *dot = strchr(P, '.');
    size_t fl = dot ? (size_t)(dot - P) : strlen(P);
    char tmp[200];
    if (en >= 0 && st > 0 && s[st - 1] == '.') {            /* metodo su un'espressione: "x".split( */
        if (st > 1 && (s[st - 2] == '"' || s[st - 2] == '\'')) strcpy(q->hint, "str");
        snprintf(tmp, sizeof tmp, "?.%s", P); strcpy(P, tmp);
    } else if (dot && ((fl == 4 && !strncmp(P, "self", 4)) || (fl == 3 && !strncmp(P, "cls", 3)))) {
        snprintf(tmp, sizeof tmp, "?%s", dot); strcpy(P, tmp);
    } else {
        int found = 0;
        for (int i = 0; i < g_nalias && !found; i++)
            if (strlen(g_alias_k[i]) == fl && !strncmp(g_alias_k[i], P, fl)) {
                snprintf(tmp, sizeof tmp, "%s%s", g_alias_v[i], dot ? dot : ""); strcpy(P, tmp); found = 1;
            }
        if (!found && dot) {                                /* nomi di variabile ricorrenti */
            char v[24] = ""; for (size_t i = 0; i < fl && i < 23; i++) v[i] = (char)tolower((unsigned char)P[i]);
            if (!strncmp(v, "df", 2)) strcpy(q->hint, "DataFrame");
            else if (!strcmp(v, "s") || !strcmp(v, "ser") || !strcmp(v, "series")) strcpy(q->hint, "Series");
            else if (!strcmp(v, "arr") || !strcmp(v, "array")) strcpy(q->hint, "ndarray");
            else if (!strcmp(v, "ax") || !strcmp(v, "axs") || !strcmp(v, "axes")) strcpy(q->hint, "Axes");
            else if (!strcmp(v, "fig")) strcpy(q->hint, "Figure");
            else if (!strcmp(v, "tensor")) strcpy(q->hint, "Tensor");
            else {
                char w[28]; snprintf(w, sizeof w, " %s ", v);
                if (strstr(" model clf reg estimator classifier regressor pipe pipeline scaler encoder ", w)) strcpy(q->dochint, "sklearn");
            }
        }
    }
    snprintf(q->q, sizeof q->q, "%s", P);
    q->nseg = nsegs(q->q);
    snprintf(q->last, sizeof q->last, "%s", last_seg(q->q));
    return 1;
}

/* ---------------------------------------------------------- ricerca negli indici */
static int common_tail(const char *a, const char *b, int ci) {
    const char *ea = a + strlen(a), *eb = b + strlen(b); int k = 0;
    for (;;) {
        const char *sa = ea, *sb = eb;
        while (sa > a && sa[-1] != '.') sa--;
        while (sb > b && sb[-1] != '.') sb--;
        size_t la = (size_t)(ea - sa), lb = (size_t)(eb - sb);
        if (la != lb || la == 0 || (ci ? _strnicmp(sa, sb, la) : strncmp(sa, sb, la))) break;
        k++;
        if (sa == a || sb == b) break;
        ea = sa - 1; eb = sb - 1;
    }
    return k;
}
static void consider(int di, const Query *q, int ci, const char *name, const char *path, Cand *top, int *ntop) {
    char A[256]; snprintf(A, sizeof A, "%s", name);
    char *t = strstr(A, "   "); if (t) *t = 0;                 /* coda spuria negli indici TensorFlow */
    size_t n = strlen(A);
    while (n && A[n - 1] == ' ') A[--n] = 0;
    if (n > 2 && !strcmp(A + n - 2, "()")) { A[n - 2] = 0; n -= 2; }
    if (n && A[n - 1] == ')') {                                /* es. "with (statement)" */
        t = strstr(A, " ("); if (t) *t = 0;
        n = strlen(A);
        if (n > 2 && !strcmp(A + n - 2, "()")) A[n - 2] = 0;
    }
    if (!ident_path(A)) return;
    const char *la = last_seg(A);
    if (ci ? _stricmp(la, q->last) : strcmp(la, q->last)) return;

    const Doc *d = &g_docs[di];
    char F[320], C[256];                                       /* F = nome completo */
    const char *frag = strchr(path, '#'), *sl = strrchr(path, '/');
    snprintf(C, sizeof C, "%s", frag ? frag + 1 : sl ? sl + 1 : path);
    int nA = nsegs(A), nC = ident_path(C) ? nsegs(C) : 0;
    size_t rl = strlen(d->root);
    if (nC > nA && common_tail(C, A, 1) == nA) {
        const char *p = C; for (int skip = nC - nA; skip > 0; p++) if (*p == '.') skip--;
        snprintf(F, sizeof F, "%.*s%s", (int)(p - C), C, A);
    } else if (rl && !(!strncmp(A, d->root, rl) && (A[rl] == '.' || !A[rl]))) snprintf(F, sizeof F, "%s.%s", d->root, A);
    else snprintf(F, sizeof F, "%s", A);

    int k = common_tail(F, q->q, ci);
    if (k == 0) return;
    int nF = nsegs(F), sc = 50 + 12 * (k > 4 ? 4 : k);
    if (k == q->nseg && (nF == q->nseg || !strcmp(A, q->q))) sc += 30;        /* corrispondenza esatta */
    else if (k == q->nseg) sc += 10;
    else if (nF >= 2) {                                        /* ricevitore ignoto: meglio i metodi */
        const char *e = last_seg(F) - 1, *b = e; while (b > F && b[-1] != '.') b--;
        size_t pl = (size_t)(e - b);
        static const char *bt[] = { "str","list","dict","set","tuple","bytes","int","float","frozenset","bytearray","sequence", NULL };
        int meth = *b >= 'A' && *b <= 'Z' ? 8 : 0;
        for (int i = 0; bt[i] && !meth; i++) if (strlen(bt[i]) == pl && !strncmp(bt[i], b, pl)) meth = 14;   /* tipi di base */
        sc += meth;
        if (q->hint[0] && strlen(q->hint) == pl && !strncmp(q->hint, b, pl)) sc += 25;
        if (q->dochint[0] && !strcmp(q->dochint, d->root)) sc += 12;
    }
    if (rl && !strncmp(q->q, d->root, rl) && q->q[rl] == '.') sc += 10;
    sc -= 2 * nF + di + (ci ? 10 : 0);

    int same = 0, worst = -1;
    for (int i = 0; i < *ntop; i++) {
        if (top[i].doc != di) continue;
        if (!strcmp(top[i].full, F)) return;                   /* doppione */
        same++; worst = i;
    }
    if (q->only < 0 && same >= 3) {                            /* al massimo 3 voci per libreria: piu' varieta' */
        if (top[worst].score >= sc) return;
        for (int i = worst; i < *ntop - 1; i++) top[i] = top[i + 1];
        (*ntop)--;
    }
    int pos = *ntop;
    while (pos > 0 && top[pos - 1].score < sc) pos--;
    if (pos >= MAXC) return;
    int last = *ntop < MAXC ? *ntop : MAXC - 1;
    for (int i = last; i > pos; i--) top[i] = top[i - 1];
    top[pos].doc = di; top[pos].score = sc;
    snprintf(top[pos].full, sizeof top[pos].full, "%s", F);
    snprintf(top[pos].path, sizeof top[pos].path, "%s", path);
    if (*ntop < MAXC) (*ntop)++;
}
static void scan_index(int di, const Query *q, int ci, Cand *top, int *ntop) {
    wchar_t p[MAX_PATH]; size_t len;
    idx_path(&g_docs[di], p);
    char *data = read_file(p, &len);
    if (!data) return;
    JS j = { data, data + len }; char key[32];
    while (js_obj_next(&j, key, sizeof key)) {
        if (strcmp(key, "entries")) { js_skip(&j); continue; }
        while (js_arr_next(&j)) {
            char name[256] = "", path[300] = "", k2[16];
            while (js_obj_next(&j, k2, sizeof k2)) {
                if (!strcmp(k2, "name")) js_str(&j, name, sizeof name);
                else if (!strcmp(k2, "path")) js_str(&j, path, sizeof path);
                else js_skip(&j);
            }
            if (name[0] && path[0]) consider(di, q, ci, name, path, top, ntop);
        }
    }
    free(data);
}
static int search_all(Query *q, Cand *top) {
    int n = 0, only = -1;
    for (int i = 0; i < g_ndocs; i++) {                        /* np.x -> cerca solo in NumPy */
        size_t rl = strlen(g_docs[i].root);
        if (rl && g_docs[i].ok && !strncmp(q->q, g_docs[i].root, rl) && q->q[rl] == '.') only = i;
    }
    q->only = only;
    for (int ci = 0; ci < 2 && n == 0; ci++)
        for (int i = 0; i < g_ndocs; i++)
            if (g_docs[i].ok && (only < 0 || only == i)) scan_index(i, q, ci, top, &n);
    return n;
}

/* ------------------------------------------------------- da HTML a RTF */
static int utf8_dec(const char *p, const char *end, unsigned *cp) {
    unsigned char c = (unsigned char)*p;
    int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    if (!n || p + n > end) { *cp = c; return 1; }
    unsigned v = n == 1 ? c : c & (0xFF >> (n + 1));
    for (int i = 1; i < n; i++) { if (((unsigned char)p[i] & 0xC0) != 0x80) { *cp = c; return 1; } v = (v << 6) | ((unsigned char)p[i] & 0x3F); }
    *cp = v; return n;
}
static int decode_entity(const char *p, const char *end, unsigned *cp) {
    static const struct { const char *n; unsigned c; } E[] = { {"amp",'&'},{"lt",'<'},{"gt",'>'},{"quot",'"'},{"apos",'\''},
        {"nbsp",0xA0},{"mdash",0x2014},{"ndash",0x2013},{"hellip",0x2026},{"rarr",0x2192},{"larr",0x2190},{"times",0xD7},
        {"le",0x2264},{"ge",0x2265},{"ne",0x2260},{"copy",0xA9},{"lsquo",0x2018},{"rsquo",0x2019},{"ldquo",0x201C},
        {"rdquo",0x201D},{"para",0xB6},{"minus",0x2212},{"middot",0xB7},{"bull",0x2022},{"infin",0x221E},{"pi",0x3C0},
        {"deg",0xB0},{"plusmn",0xB1},{"sum",0x2211},{"isin",0x2208},{NULL,0} };
    const char *s = p + 1; int n = 0;
    while (s + n < end && n < 10 && s[n] != ';') n++;
    if (s + n >= end || s[n] != ';' || n == 0) return 0;
    if (s[0] == '#') {
        unsigned v = 0;
        if (n > 1 && (s[1] == 'x' || s[1] == 'X')) for (int i = 2; i < n; i++) v = v * 16 + hexv(s[i]);
        else for (int i = 1; i < n; i++) v = v * 10 + (unsigned)(s[i] - '0');
        *cp = v ? v : '?'; return n + 2;
    }
    for (int i = 0; E[i].n; i++) if ((int)strlen(E[i].n) == n && !strncmp(E[i].n, s, n)) { *cp = E[i].c; return n + 2; }
    return 0;
}
typedef struct { Buf *o; int bold, ital, mono, pre, level, started, pending, bullet, sp, nl, lastFmt, skipDepth, eat; char skipName[20]; } RC;

static void rc_esc(Buf *o, unsigned cp) {
    if (cp == '\\' || cp == '{' || cp == '}') { char t[3] = { '\\', (char)cp, 0 }; buf_puts(o, t); }
    else if (cp < 128) { char c = (char)cp; buf_put(o, &c, 1); }
    else if (cp < 0x10000) buf_printf(o, "\\u%d?", (int)(short)cp);
    else { cp -= 0x10000; buf_printf(o, "\\u%d?\\u%d?", (int)(short)(0xD800 + (cp >> 10)), (int)(short)(0xDC00 + (cp & 0x3FF))); }
}
static void rc_fmt(RC *r) {
    int mono = r->mono > 0 || r->pre > 0, code = r->mono > 0 && r->pre == 0;
    int f = (mono ? 1 : 0) | (code ? 2 : 0) | (r->bold > 0 ? 4 : 0) | (r->ital > 0 ? 8 : 0);
    if (f == r->lastFmt) return;
    r->lastFmt = f;
    buf_printf(r->o, "\\f%d\\cf%d\\b%s\\i%s ", mono ? 1 : 0, code ? 3 : 1, r->bold > 0 ? "" : "0", r->ital > 0 ? "" : "0");
}
static void rc_par(RC *r) {
    if (!r->pending) return;
    r->pending = 0; r->nl = 0;
    if (r->started) buf_puts(r->o, "\\par");
    buf_printf(r->o, "\\pard\\sa%d\\li%d ", r->pre > 0 ? 60 : 90, (r->level > 3 ? 3 : r->level) * 240);
    r->lastFmt = -1; r->sp = 1;
    if (r->bullet) { r->bullet = 0; rc_fmt(r); buf_puts(r->o, "\\u8226? "); r->started = 1; }
}
static void rc_char(RC *r, unsigned cp) {
    if (r->skipDepth || cp == 0xB6) return;
    if (r->eat) {                          /* pandas scrive gia' ":" all'inizio del tipo: evita "::" */
        if (cp == ':') { r->eat = 0; return; }
        if (cp != ' ' && cp != '\n' && cp != '\r' && cp != '\t' && cp != 0xA0) r->eat = 0;
    }
    if (r->pre > 0) {
        if (cp == '\r') return;
        if (cp == '\n') { if (!r->pending) r->nl++; return; }
        rc_par(r); rc_fmt(r);
        for (; r->nl > 0; r->nl--) buf_puts(r->o, "\\line ");
        if (cp == '\t') buf_puts(r->o, "    "); else rc_esc(r->o, cp);
        r->started = 1; return;
    }
    if (cp == ' ' || cp == '\n' || cp == '\r' || cp == '\t' || cp == 0xA0) {
        if (r->pending || r->sp) return;
        rc_fmt(r); buf_puts(r->o, " "); r->sp = 1; return;
    }
    rc_par(r); rc_fmt(r); rc_esc(r->o, cp);
    r->sp = 0; r->started = 1;
}
static int has_sub(const char *a, const char *e, const char *sub) { return find_n(a, e, sub) != NULL; }
static int in_list(const char *name, const char *list) {       /* list: " a b c " */
    char t[24]; snprintf(t, sizeof t, " %s ", name); return strstr(list, t) != NULL;
}
static void rc_tag(RC *r, const char *t, const char *te) {
    int close = 0, n = 0; char name[20];
    if (t < te && *t == '/') { close = 1; t++; }
    while (t < te && n < 19 && (isalnum((unsigned char)*t) || *t == '-')) name[n++] = (char)tolower((unsigned char)*t++);
    name[n] = 0;
    if (!n) return;
    int selfclose = te > t && te[-1] == '/';
    if (r->skipDepth) {
        if (!strcmp(name, r->skipName)) { if (close) r->skipDepth--; else if (!selfclose) r->skipDepth++; }
        return;
    }
    if (!close && !selfclose && (in_list(name, " script style math svg button template ") || has_sub(t, te, "headerlink") ||
                                 has_sub(t, te, "viewcode") || has_sub(t, te, "classifier-delimiter"))) {
        strcpy(r->skipName, name); r->skipDepth = 1; return;
    }
    int d = close ? -1 : 1;
    if (in_list(name, " p div section article blockquote table tr ul ol dl pre dt dd li h1 h2 h3 h4 h5 h6 figure figcaption hr details summary ")) r->pending = 1;
    if (in_list(name, " h1 h2 h3 h4 h5 h6 dt th strong b ")) r->bold += d;
    else if (in_list(name, " em i cite var ")) r->ital += d;
    else if (in_list(name, " code tt kbd samp ")) r->mono += d;
    if (!strcmp(name, "pre")) r->pre += d;
    if (in_list(name, " dd ul ol ")) r->level += d;
    if (!strcmp(name, "li") && !close) r->bullet = 1;
    if (!strcmp(name, "br")) { if (r->pre > 0) r->nl++; else if (r->started && !r->pending) { buf_puts(r->o, "\\line "); r->sp = 1; } }
    if (close && (!strcmp(name, "td") || !strcmp(name, "th")) && r->started && !r->pending) { buf_puts(r->o, "\\tab "); r->sp = 1; }
    if (!close && !strcmp(name, "span") && has_sub(t, te, "classifier")) { rc_char(r, ' '); rc_char(r, ':'); rc_char(r, ' '); r->eat = 1; }
    if (r->bold < 0) r->bold = 0;
    if (r->ital < 0) r->ital = 0;
    if (r->mono < 0) r->mono = 0;
    if (r->pre < 0) r->pre = 0;
    if (r->level < 0) r->level = 0;
}
static void html_to_rtf(RC *r, const char *p, const char *end) {
    while (p < end && r->o->n < 400000) {
        if (*p == '<') {
            if (end - p >= 4 && !strncmp(p, "<!--", 4)) { const char *e = find_n(p, end, "-->"); p = e ? e + 3 : end; continue; }
            const char *gt = (const char *)memchr(p, '>', (size_t)(end - p));
            if (!gt) break;
            if (p + 1 < gt && (isalpha((unsigned char)p[1]) || p[1] == '/')) { rc_tag(r, p + 1, gt); p = gt + 1; continue; }
            rc_char(r, '<'); p++; continue;
        }
        unsigned cp;
        if (*p == '&') { int adv = decode_entity(p, end, &cp); if (adv) { rc_char(r, cp); p += adv; continue; } }
        p += utf8_dec(p, end, &cp);
        rc_char(r, cp);
    }
}
/* Testo semplice di un frammento HTML (per la firma della funzione). */
static void strip_text(const char *p, const char *end, Buf *out) {
    int sp = 1;
    while (p < end) {
        unsigned cp;
        if (*p == '<') { const char *gt = (const char *)memchr(p, '>', (size_t)(end - p)); if (!gt) break; p = gt + 1; continue; }
        if (*p == '&') { int adv = decode_entity(p, end, &cp); if (adv) p += adv; else { cp = '&'; p++; } }
        else p += utf8_dec(p, end, &cp);
        if (cp == 0xB6) continue;
        if (cp == ' ' || cp == '\n' || cp == '\r' || cp == '\t' || cp == 0xA0) { if (!sp) { buf_puts(out, " "); sp = 1; } continue; }
        char u[5]; int k = utf8_enc(cp, u); buf_put(out, u, (size_t)k); sp = 0;
    }
    if (!out->p) buf_puts(out, "");
    char *s = strstr(out->p, "[source]");
    if (s) { memmove(s, s + 8, strlen(s + 8) + 1); out->n -= 8; }
    while (out->n && out->p[out->n - 1] == ' ') out->p[--out->n] = 0;
}
static char *build_rtf(const char *sig, size_t siglen, const char *body, size_t blen) {
    Theme t; get_theme(&t);
    Buf o = {0};
    buf_puts(&o, "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fswiss Segoe UI;}{\\f1\\fmodern Consolas;}}");
    buf_printf(&o, "{\\colortbl ;\\red%d\\green%d\\blue%d;\\red%d\\green%d\\blue%d;\\red%d\\green%d\\blue%d;\\red%d\\green%d\\blue%d;}",
        GetRValue(t.text), GetGValue(t.text), GetBValue(t.text), GetRValue(t.dim), GetGValue(t.dim), GetBValue(t.dim),
        GetRValue(t.code), GetGValue(t.code), GetBValue(t.code), GetRValue(t.accent), GetGValue(t.accent), GetBValue(t.accent));
    buf_puts(&o, "\\viewkind4\\uc1\\fs19 ");
    RC r; memset(&r, 0, sizeof r); r.o = &o; r.lastFmt = -1;
    if (siglen) {
        buf_puts(&o, "\\pard\\sa140\\f1\\cf4 ");
        for (const char *p = sig, *e = sig + siglen; p < e;) { unsigned cp; p += utf8_dec(p, e, &cp); rc_esc(&o, cp); }
        r.started = 1;
    }
    r.pending = 1;
    html_to_rtf(&r, body, body + blen);
    if (!r.started) buf_puts(&o, "\\pard\\cf2 (nessuna descrizione disponibile)");
    buf_puts(&o, "}");
    return o.p;
}
/* Trova nella pagina la voce richiesta: firma (dt) e corpo (dd). */
static void extract_entry(const char *html, size_t len, const char *frag, Buf *sig, const char **body, size_t *blen) {
    const char *end = html + len, *at = NULL;
    if (frag && *frag) { char pat[320]; snprintf(pat, sizeof pat, "id=\"%s\"", frag); at = find_n(html, end, pat); }
    else at = find_n(html, end, "<dt class=\"sig");
    if (at) {
        const char *ts = at; char tag[12]; int n = 0;
        while (ts > html && *ts != '<') ts--;
        for (const char *p = ts + 1; p < end && n < 11 && isalnum((unsigned char)*p); p++) tag[n++] = (char)tolower((unsigned char)*p);
        tag[n] = 0;
        if (!strcmp(tag, "dl")) { const char *d = find_n(ts + 1, end, "<dt"); if (d) { ts = d; strcpy(tag, "dt"); } }
        if (!strcmp(tag, "dt")) {
            const char *gt = (const char *)memchr(ts, '>', (size_t)(end - ts)), *de = find_n(ts, end, "</dt>");
            const char *dd = de ? find_n(de, end, "<dd") : NULL;
            const char *b0 = dd ? (const char *)memchr(dd, '>', (size_t)(end - dd)) : NULL;
            if (gt && de && b0) {
                strip_text(gt + 1, de, sig);
                b0++;
                const char *bend = end; int depth = 1;
                for (const char *p = b0; p < end;) {
                    const char *lt = (const char *)memchr(p, '<', (size_t)(end - p));
                    if (!lt || end - lt < 5) break;
                    if (!strncmp(lt, "<dd", 3) && (lt[3] == '>' || lt[3] == ' ')) depth++;
                    else if (!strncmp(lt, "</dd>", 5) && --depth == 0) { bend = lt; break; }
                    p = lt + 1;
                }
                *body = b0; *blen = (size_t)(bend - b0);
                goto cap;
            }
        }
        const char *bend = end;                                /* voce generica: fino al titolo successivo */
        for (const char *p = ts + 20; p + 3 < end; p++)
            if (p[0] == '<' && p[1] == 'h' && p[2] >= '1' && p[2] <= '3' && (p[3] == '>' || p[3] == ' ')) { bend = p; break; }
        *body = ts; *blen = (size_t)(bend - ts);
    } else {
        const char *h1 = find_n(html, html + (len < 6000 ? len : 6000), "</h1>");
        *body = h1 ? h1 + 5 : html; *blen = (size_t)(end - *body);
    }
cap:
    if (*blen > 90000) *blen = 90000;
}

/* ------------------------------------------------------ caricamento voce */
static void res_free(Res *r) {
    if (!r) return;
    free(r->title); free(r->label); free(r->url); free(r->msg); free(r->rtf); free(r->blob); free(r);
}
/* Formato dei file salvati: "LENTE1\n titolo\n etichetta\n url\n firma\n" + HTML del corpo. */
static int res_from_blob(Res *rs, char *blob, size_t len) {
    if (len < 7 || memcmp(blob, "LENTE1\n", 7)) return 0;
    const char *p = blob + 7, *e = blob + len, *f[4]; size_t fl[4];
    for (int i = 0; i < 4; i++) {
        const char *nl = (const char *)memchr(p, '\n', (size_t)(e - p));
        if (!nl) return 0;
        f[i] = p; fl[i] = (size_t)(nl - p); p = nl + 1;
    }
    char t[600];
    snprintf(t, sizeof t, "%.*s", (int)(fl[0] > 500 ? 500 : fl[0]), f[0]); rs->title = u8w(t);
    snprintf(t, sizeof t, "%.*s", (int)(fl[1] > 500 ? 500 : fl[1]), f[1]); rs->label = u8w(t);
    snprintf(t, sizeof t, "%.*s", (int)(fl[2] > 500 ? 500 : fl[2]), f[2]); rs->url = u8w(t);
    rs->rtf = build_rtf(f[3], fl[3], p, (size_t)(e - p));
    rs->blob = blob; rs->blob_len = len; rs->ok = 1;
    return 1;
}
static void res_msg(Res *rs, const wchar_t *fmt, ...) {
    wchar_t b[300]; va_list ap; va_start(ap, fmt); _vsnwprintf(b, 300, fmt, ap); va_end(ap); b[299] = 0;
    free(rs->msg); rs->msg = _wcsdup(b); rs->ok = 0;
}
static int load_entry(Res *rs, const Cand *c) {
    const Doc *d = &g_docs[c->doc];
    char key[400]; snprintf(key, sizeof key, "%s__%s", d->base, c->path);
    for (char *k = key; *k; k++) if (!isalnum((unsigned char)*k) && *k != '.' && *k != '-' && *k != '_') *k = '_';
    key[150] = 0;
    wfmt(rs->save_path, MAX_PATH, L"%ls\\%hs.html", g_savedir, key);
    size_t len; char *blob = read_file(rs->save_path, &len);
    if (blob && res_from_blob(rs, blob, len)) { rs->saved = 1; return 1; }
    free(blob);
    if (!net_ok()) { res_msg(rs, L"Sei offline e «%hs» non è tra le funzioni salvate", c->full); return 0; }
    char pth[300]; snprintf(pth, sizeof pth, "%s", c->path);
    char *fr = strchr(pth, '#'); if (fr) *fr++ = 0;
    wchar_t wp[700]; wfmt(wp, 700, L"/%hs/%hs.html", d->slug, pth);
    size_t hl; int st;
    char *html = http_get(DOCS_HOST, wp, &hl, &st);
    if (!html) {
        if (st) res_msg(rs, L"Pagina non trovata per «%hs» (HTTP %d)", c->full, st);
        else res_msg(rs, L"Nessuna connessione: «%hs» non è tra le funzioni salvate", c->full);
        return 0;
    }
    Buf sig = {0}, b = {0}; const char *body; size_t blen;
    extract_entry(html, hl, fr, &sig, &body, &blen);
    buf_printf(&b, "LENTE1\n%s\n%s\nhttps://devdocs.io/%s/%s\n", c->full, d->label, d->slug, c->path);
    if (sig.p) buf_put(&b, sig.p, sig.n);
    buf_puts(&b, "\n");
    buf_put(&b, body, blen);
    free(sig.p); free(html);
    if (!res_from_blob(rs, b.p, b.n)) { free(b.p); res_msg(rs, L"Errore interno"); return 0; }
    if (g_autosave && write_file(rs->save_path, rs->blob, rs->blob_len)) rs->saved = 1;
    return 1;
}

/* ------------------------------------------- cattura del testo selezionato */
typedef struct { UINT fmt; void *data; SIZE_T size; } ClipItem;
static int clip_open(HWND h) { for (int i = 0; i < 12; i++) { if (OpenClipboard(h)) return 1; Sleep(15); } return 0; }
static int clip_backup(HWND h, ClipItem *it, int max) {
    if (!clip_open(h)) return -1;
    int n = 0; SIZE_T total = 0; UINT f = 0;
    while ((f = EnumClipboardFormats(f)) != 0 && n < max) {
        if (f == CF_BITMAP || f == CF_PALETTE || f == CF_METAFILEPICT || f == CF_ENHMETAFILE || f == CF_OWNERDISPLAY ||
            f == CF_DSPBITMAP || f == CF_DSPMETAFILEPICT || f == CF_DSPENHMETAFILE || (f >= 0x0200 && f <= 0x03FF)) continue;
        HANDLE hd = GetClipboardData(f); if (!hd) continue;
        SIZE_T sz = GlobalSize(hd);
        if (!sz || total + sz > (32u << 20)) continue;
        void *src = GlobalLock(hd); if (!src) continue;
        it[n].data = malloc(sz); memcpy(it[n].data, src, sz); GlobalUnlock(hd);
        it[n].fmt = f; it[n].size = sz; total += sz; n++;
    }
    CloseClipboard();
    return n;
}
static void clip_restore(HWND h, ClipItem *it, int n) {
    if (!clip_open(h)) return;
    EmptyClipboard();
    for (int i = 0; i < n; i++) {
        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, it[i].size); if (!g) continue;
        void *dst = GlobalLock(g); memcpy(dst, it[i].data, it[i].size); GlobalUnlock(g);
        if (!SetClipboardData(it[i].fmt, g)) GlobalFree(g);
    }
    CloseClipboard();
}
static wchar_t *clip_text(HWND h) {
    wchar_t *out = NULL;
    if (!clip_open(h)) return NULL;
    HANDLE hd = GetClipboardData(CF_UNICODETEXT);
    if (hd) {
        const wchar_t *src = (const wchar_t *)GlobalLock(hd);
        if (src) {
            size_t max = GlobalSize(hd) / sizeof(wchar_t), n = 0;
            while (n < max && n < 1000 && src[n]) n++;
            out = (wchar_t *)malloc((n + 1) * sizeof(wchar_t));
            memcpy(out, src, n * sizeof(wchar_t)); out[n] = 0;
            GlobalUnlock(hd);
        }
    }
    CloseClipboard();
    return out;
}
static int key_down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
/* Copia la selezione corrente con Ctrl+C simulato, la legge e ripristina gli appunti di prima. */
static int g_selftest;
static wchar_t *capture_selection(void) {
    Sleep(50);
    if (key_down(VK_SHIFT) || key_down(VK_CONTROL) || key_down(VK_MENU) || key_down(VK_LWIN) || key_down(VK_RWIN) || key_down(VK_LBUTTON)) return NULL;
    HWND fg = GetForegroundWindow(); DWORD pid = 0;
    if (!fg) return NULL;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == GetCurrentProcessId() && !g_selftest) return NULL;
    HWND h = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
    ClipItem items[40]; wchar_t *text = NULL;
    int n = clip_backup(h, items, 40);
    if (n >= 0) {
        DWORD seq = GetClipboardSequenceNumber();
        INPUT in[4]; memset(in, 0, sizeof in);
        in[0].type = in[1].type = in[2].type = in[3].type = INPUT_KEYBOARD;
        in[0].ki.wVk = VK_CONTROL; in[1].ki.wVk = 'C';
        in[2].ki.wVk = 'C'; in[2].ki.dwFlags = KEYEVENTF_KEYUP;
        in[3].ki.wVk = VK_CONTROL; in[3].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(4, in, sizeof(INPUT));
        int changed = 0;
        for (int i = 0; i < 40 && !changed; i++) { Sleep(15); changed = GetClipboardSequenceNumber() != seq; }
        if (changed) { Sleep(40); text = clip_text(h); clip_restore(h, items, n); }
        for (int i = 0; i < n; i++) free(items[i].data);
    }
    DestroyWindow(h);
    return text;
}

/* -------------------------------------------------------------- lavoro in background */
static DWORD WINAPI worker(LPVOID param) {
    Req *rq = (Req *)param;
    Res *rs = (Res *)calloc(1, sizeof(Res));
    rs->gen = rq->gen;
    if (rq->type == REQ_SAVED) {
        size_t len; char *blob = read_file(rq->file, &len);
        wcscpy(rs->save_path, rq->file);
        if (blob && res_from_blob(rs, blob, len)) rs->saved = 1;
        else { free(blob); res_msg(rs, L"File salvato non leggibile"); }
    } else if (rq->type == REQ_CAND) {
        memcpy(rs->cand, rq->cand, sizeof rs->cand); rs->ncand = rq->ncand; rs->pick = rq->pick;
        load_entry(rs, &rs->cand[rs->pick]);
    } else {
        wchar_t *sel = NULL;
        if (rq->type == REQ_SEL) { sel = capture_selection(); InterlockedExchange(&g_busy, 0); }
        else sel = _wcsdup(rq->text);
        Query q; char *u = sel ? wu8(sel) : NULL;
        if (!u || !parse_query(u, &q)) { free(u); free(sel); free(rq); res_free(rs); return 0; }
        free(u); free(sel);
        post_status(rq->gen, L"Cerco «%hs»…", q.shown);
        int usable = ensure_docs(rq->gen);
        rs->ncand = usable ? search_all(&q, rs->cand) : 0;
        if (!usable) res_msg(rs, L"Nessuna connessione e nessun indice scaricato: serve internet almeno la prima volta");
        else if (!rs->ncand) res_msg(rs, L"Nessun risultato per «%hs»", q.shown);
        else load_entry(rs, &rs->cand[0]);
    }
    free(rq);
    if (!PostMessageW(g_main, WM_APP_RESULT, 0, (LPARAM)rs)) res_free(rs);
    return 0;
}
static void start_req(Req *rq) {
    rq->gen = ++g_gen;
    HANDLE t = CreateThread(NULL, 0, worker, rq, 0, NULL);
    if (t) CloseHandle(t); else free(rq);
}

/* ------------------------------------------------------------------ aggancio mouse */
static int is_ours(POINT pt) {
    HWND w = WindowFromPoint(pt);
    if (!w) return 0;
    w = GetAncestor(w, GA_ROOT);
    return w == g_main || (g_pop && w == g_pop);
}
static int cursor_ibeam(void) {
    CURSORINFO ci; ci.cbSize = sizeof ci;
    return GetCursorInfo(&ci) && ci.hCursor == LoadCursorW(NULL, IDC_IBEAM);
}
static LRESULT CALLBACK mouse_hook(int code, WPARAM wp, LPARAM lp) {
    static POINT dpt, upt; static DWORD uptime; static int dours, dibeam;
    if (code == HC_ACTION) {
        MSLLHOOKSTRUCT *m = (MSLLHOOKSTRUCT *)lp;
        if (wp == WM_LBUTTONDOWN) {
            dpt = m->pt; dours = is_ours(m->pt); dibeam = cursor_ibeam();
            if (g_pop_vis && !dours) PostMessageW(g_main, WM_APP_CLOSE, 0, 0);
        } else if (wp == WM_LBUTTONUP) {
            if (g_active && !dours) {
                int dx = m->pt.x - dpt.x, dy = m->pt.y - dpt.y, thr = S(6);
                int drag = dx * dx + dy * dy > thr * thr;
                int dbl = m->time - uptime < GetDoubleClickTime() && abs(m->pt.x - upt.x) < S(4) && abs(m->pt.y - upt.y) < S(4);
                if ((drag || dbl) && (!g_only_ibeam || dibeam || cursor_ibeam()))
                    PostMessageW(g_main, WM_APP_TRIGGER, (WPARAM)m->pt.x, (LPARAM)m->pt.y);
            }
            upt = m->pt; uptime = m->time;
        } else if (wp == WM_MOUSEWHEEL) {
            if (g_pop_vis && !is_ours(m->pt)) PostMessageW(g_main, WM_APP_CLOSE, 0, 0);
        }
    }
    return CallNextHookEx(NULL, code, wp, lp);
}
static void update_hook(void) {
    int need = g_active || g_pop_vis;
    if (need && !g_hook) g_hook = SetWindowsHookExW(WH_MOUSE_LL, mouse_hook, GetModuleHandleW(NULL), 0);
    else if (!need && g_hook) { UnhookWindowsHookEx(g_hook); g_hook = NULL; }
}

/* ------------------------------------------------------------------- riquadro */
static HFONT mkfont(int pt10, int weight) {
    LOGFONTW lf; memset(&lf, 0, sizeof lf);
    lf.lfHeight = -MulDiv(pt10, g_dpi, 720); lf.lfWeight = weight; lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy(lf.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&lf);
}
static void popup_close(void) {
    if (!g_pop) return;
    KillTimer(g_main, T_AUTOCLOSE);
    ShowWindow(g_pop, SW_HIDE);
    if (g_rich) SetWindowTextW(g_rich, L"");
    res_free(g_res); g_res = NULL; g_msg[0] = 0; g_pop_vis = 0;
    g_gen++;                                         /* scarta eventuali risposte in arrivo */
    update_hook(); trim_memory();
}
static void popup_place(int w, int h) {
    MONITORINFO mi; mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromPoint(g_anchor, MONITOR_DEFAULTTONEAREST), &mi);
    RECT wa = mi.rcWork;
    int x = g_anchor.x + S(12), y = g_anchor.y + S(18);
    if (y + h > wa.bottom) y = g_anchor.y - h - S(14);
    if (y < wa.top) y = wa.top + 4;
    if (x + w > wa.right) x = wa.right - w - 4;
    if (x < wa.left) x = wa.left + 4;
    SetWindowPos(g_pop, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_pop, NULL, TRUE);
    g_pop_vis = 1; update_hook();
}
static void popup_message(const wchar_t *text, int autoclose_ms) {
    res_free(g_res); g_res = NULL;
    get_theme(&g_th);
    wcsncpy(g_msg, text, 299); g_msg[299] = 0;
    if (g_rich) ShowWindow(g_rich, SW_HIDE);
    HDC dc = GetDC(g_pop); HGDIOBJ of = SelectObject(dc, g_fUI); SIZE sz;
    GetTextExtentPoint32W(dc, g_msg, (int)wcslen(g_msg), &sz);
    SelectObject(dc, of); ReleaseDC(g_pop, dc);
    int w = sz.cx + S(30);
    if (w < S(140)) w = S(140);
    if (w > S(520)) w = S(520);
    popup_place(w, S(34));
    KillTimer(g_main, T_AUTOCLOSE);
    if (autoclose_ms) SetTimer(g_main, T_AUTOCLOSE, (UINT)autoclose_ms, NULL);
}
static void popup_result(Res *rs) {
    res_free(g_res); g_res = rs; g_msg[0] = 0;
    KillTimer(g_main, T_AUTOCLOSE);
    get_theme(&g_th);
    if (!g_rich) {
        LoadLibraryW(L"Msftedit.dll");
        g_rich = CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                 0, 0, 10, 10, g_pop, NULL, GetModuleHandleW(NULL), NULL);
        SendMessageW(g_rich, EM_SETEVENTMASK, 0, ENM_REQUESTRESIZE);
        SendMessageW(g_rich, EM_AUTOURLDETECT, 0, 0);
    }
    SetWindowTheme(g_rich, g_th.dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
    SendMessageW(g_rich, EM_SETBKGNDCOLOR, 0, (LPARAM)g_th.bg);
    int W = S(500), pad = S(12), top = S(36) + S(8), foot = rs->ncand > 1 ? S(30) : S(8), maxBody = S(360);
    MoveWindow(g_rich, pad, top, W - 2 * pad, maxBody, FALSE);
    SETTEXTEX st; st.flags = ST_DEFAULT; st.codepage = CP_ACP;
    g_richH = 0;
    SendMessageW(g_rich, EM_SETTEXTEX, (WPARAM)&st, (LPARAM)rs->rtf);
    SendMessageW(g_rich, EM_REQUESTRESIZE, 0, 0);
    int bh = g_richH > 0 && g_richH + S(4) < maxBody ? g_richH + S(4) : maxBody;
    if (bh < S(24)) bh = S(24);
    MoveWindow(g_rich, pad, top, W - 2 * pad, bh, TRUE);
    SendMessageW(g_rich, EM_SETSEL, 0, 0);
    SendMessageW(g_rich, WM_VSCROLL, SB_TOP, 0);
    ShowWindow(g_rich, SW_SHOWNA);
    popup_place(W, top + bh + foot);
}
static int draw_btn(HDC dc, const wchar_t *txt, int right, RECT *rc, COLORREF col) {
    SIZE sz; GetTextExtentPoint32W(dc, txt, (int)wcslen(txt), &sz);
    rc->left = right - sz.cx - S(6); rc->right = right + S(6); rc->top = 0; rc->bottom = S(36);
    RECT t = { right - sz.cx, 0, right, S(36) };
    SetTextColor(dc, col);
    DrawTextW(dc, txt, -1, &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    return right - sz.cx - S(16);
}
static void popup_paint(HWND h) {
    PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
    RECT rc; GetClientRect(h, &rc);
    HBRUSH br = CreateSolidBrush(g_th.bg); FillRect(dc, &rc, br); DeleteObject(br);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ of = SelectObject(dc, g_fUI);
    g_nAlt = 0;
    if (!g_res) {
        RECT t = rc; t.left += S(14); t.right -= S(10);
        SetTextColor(dc, g_th.text);
        DrawTextW(dc, g_msg, -1, &t, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    } else {
        RECT hr = rc; hr.bottom = S(36);
        br = CreateSolidBrush(g_th.head); FillRect(dc, &hr, br); DeleteObject(br);
        int x = rc.right - S(14);
        x = draw_btn(dc, L"✕", x, &g_rClose, g_th.dim);
        x = draw_btn(dc, L"Apri ↗", x, &g_rOpen, g_th.accent);
        x = draw_btn(dc, g_res->saved ? L"★ Salvata" : L"☆ Salva", x, &g_rSave, g_res->saved ? g_th.rose : g_th.accent);
        SelectObject(dc, g_fBold); SetTextColor(dc, g_th.text);
        RECT tr = { S(12), 0, x, S(36) }; SIZE ts;
        GetTextExtentPoint32W(dc, g_res->title, (int)wcslen(g_res->title), &ts);
        DrawTextW(dc, g_res->title, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, g_fUI);
        int lx = S(12) + ts.cx + S(10);
        if (lx < x - S(40)) {
            RECT lr = { lx, 0, x, S(36) }; SetTextColor(dc, g_th.dim);
            DrawTextW(dc, g_res->label, -1, &lr, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        if (g_res->ncand > 1) {                               /* altre corrispondenze */
            int y0 = rc.bottom - S(28), ax = S(12);
            RECT a = { ax, y0, rc.right, rc.bottom - S(4) }; SIZE sz;
            SetTextColor(dc, g_th.dim);
            DrawTextW(dc, L"Altri:", -1, &a, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            GetTextExtentPoint32W(dc, L"Altri:", 6, &sz); ax += sz.cx + S(10);
            SetTextColor(dc, g_th.accent);
            for (int i = 0; i < g_res->ncand; i++) {
                if (i == g_res->pick) continue;
                const char *f = g_res->cand[i].full; size_t fl = strlen(f);
                wchar_t w[64]; wfmt(w, 64, fl > 30 ? L"…%hs" : L"%hs", fl > 30 ? f + fl - 29 : f);
                GetTextExtentPoint32W(dc, w, (int)wcslen(w), &sz);
                if (ax + sz.cx > rc.right - S(10)) break;
                RECT r = { ax, y0, ax + sz.cx, rc.bottom - S(4) };
                DrawTextW(dc, w, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                g_rAlt[g_nAlt] = r; g_altIdx[g_nAlt++] = i;
                ax += sz.cx + S(14);
            }
        }
    }
    br = CreateSolidBrush(g_th.border); FrameRect(dc, &rc, br); DeleteObject(br);
    SelectObject(dc, of);
    EndPaint(h, &ps);
}
static int popup_hit(POINT p) {            /* 1 salva, 2 apri, 3 chiudi, 10+i alternative */
    if (!g_res) return 0;
    if (PtInRect(&g_rSave, p)) return 1;
    if (PtInRect(&g_rOpen, p)) return 2;
    if (PtInRect(&g_rClose, p)) return 3;
    for (int i = 0; i < g_nAlt; i++) if (PtInRect(&g_rAlt[i], p)) return 10 + i;
    return 0;
}
static LRESULT CALLBACK pop_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_PAINT: popup_paint(h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_NOTIFY:
        if (((NMHDR *)lp)->code == EN_REQUESTRESIZE) { REQRESIZE *rr = (REQRESIZE *)lp; g_richH = rr->rc.bottom - rr->rc.top; }
        return 0;
    case WM_MOUSEACTIVATE: {                 /* solo il testo prende il fuoco (per copiare) */
        POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
        RECT r; if (g_rich && IsWindowVisible(g_rich)) { GetWindowRect(g_rich, &r); MapWindowPoints(NULL, h, (POINT *)&r, 2); if (PtInRect(&r, p)) return MA_ACTIVATE; }
        return MA_NOACTIVATE;
    }
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && g_pop_vis) PostMessageW(g_main, WM_APP_CLOSE, 0, 0);
        return 0;
    case WM_SETCURSOR: {
        POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
        if ((HWND)wp == h && popup_hit(p)) { SetCursor(LoadCursorW(NULL, IDC_HAND)); return TRUE; }
        break;
    }
    case WM_LBUTTONDOWN: {
        POINT p = { (short)LOWORD(lp), (short)HIWORD(lp) };
        int hit = popup_hit(p);
        if (hit == 1 && g_res->blob) {
            if (g_res->saved) { DeleteFileW(g_res->save_path); g_res->saved = 0; }
            else if (write_file(g_res->save_path, g_res->blob, g_res->blob_len)) g_res->saved = 1;
            InvalidateRect(h, NULL, FALSE);
        } else if (hit == 2) ShellExecuteW(NULL, L"open", g_res->url, NULL, NULL, SW_SHOWNORMAL);
        else if (hit == 3) popup_close();
        else if (hit >= 10) {
            Req *rq = (Req *)calloc(1, sizeof(Req));
            rq->type = REQ_CAND; rq->ncand = g_res->ncand; rq->pick = g_altIdx[hit - 10];
            memcpy(rq->cand, g_res->cand, sizeof rq->cand);
            start_req(rq);
        }
        return 0;
    }
    }
    return DefWindowProcW(h, m, wp, lp);
}

/* ---------------------------------------------------------------------- icona */
/* Icona: testa di scoiattolo robot in pixel art 24x24 (meta' sinistra, specchiata).
   o contorno, f/h/s metallo, e orecchio, v visiera, n naso, Y/y/w occhio, r raggio di energia. */
static const char *SQ[24] = {
    "............", "..r........r", "...r.......r", "....oo......", "...oeeo.....", "...oeefooooo",
    "...offfffhhh", "..offhhfffff", "..oooooooooo", "..ovvvvvvvvv", "rrovvvYYYvvv", "..ovvvywyvvv",
    "..ovvvYYYvvv", "..oooooooooo", "..osffffffnn", "..osfffffffn", "...osffffoff", "....osssffoo",
    ".....ooooooo", "...r........", "..r........r", "...........r", "............", "............" };
static int icon_scale(void) { int k = MulDiv(2, g_dpi, 96); return k < 2 ? 2 : k; }
static int icon_size(void) { return 24 * icon_scale(); }
static DWORD sq_color(char c, int on) {
    switch (c) {
    case 'o': case 'n': return 0x383C45;
    case 'f': return 0xE0E3EA;
    case 'h': return 0xF6F7FA;
    case 's': return 0xB4BAC6;
    case 'e': return 0x690731;
    case 'v': return 0x23262C;
    case 'r': return on ? 0xBC0555 : 0x9AA0AB;           /* raggi: Rosewood da attivo, grigi da spento */
    case 'Y': return on ? 0xBC0555 : 0x23262C;           /* occhio aperto / palpebra chiusa */
    case 'y': return on ? 0xBC0555 : 0x9AA0AB;
    case 'w': return on ? 0xFFFFFF : 0x9AA0AB;
    }
    return 0;
}
static void render_icon(void) {
    int k = icon_scale(), N = 24 * k;
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = N; bi.bmiHeader.biHeight = -N;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    HDC sdc = GetDC(NULL), mdc = CreateCompatibleDC(sdc); DWORD *px = NULL;
    HBITMAP bmp = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0);
    if (!bmp) { DeleteDC(mdc); ReleaseDC(NULL, sdc); return; }
    HGDIOBJ old = SelectObject(mdc, bmp);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        int gx = x / k, gy = y / k;
        char c = SQ[gy][gx < 12 ? gx : 23 - gx];
        px[y * N + x] = c == '.' ? 0 : (0xFF000000u | sq_color(c, g_active));
    }
    POINT src = { 0, 0 }; SIZE sz = { N, N }; BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(g_main, sdc, NULL, &sz, mdc, &src, 0, &bf, ULW_ALPHA);
    SelectObject(mdc, old); DeleteObject(bmp); DeleteDC(mdc); ReleaseDC(NULL, sdc);
}
static void set_active(int on) {
    g_active = on;
    render_icon(); update_hook();
    if (on) { HANDLE t = CreateThread(NULL, 0, warm_thread, NULL, 0, NULL); if (t) CloseHandle(t); }
}
static const wchar_t RUN_KEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static int autostart_get(void) {
    DWORD sz = 0;
    return RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, L"Smarty Lens", RRF_RT_REG_SZ, NULL, NULL, &sz) == ERROR_SUCCESS;
}
static void autostart_set(int on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t exe[MAX_PATH], v[MAX_PATH + 4];
        GetModuleFileNameW(NULL, exe, MAX_PATH); wfmt(v, MAX_PATH + 4, L"\"%ls\"", exe);
        RegSetValueExW(k, L"Smarty Lens", 0, REG_SZ, (const BYTE *)v, (DWORD)((wcslen(v) + 1) * sizeof(wchar_t)));
    } else RegDeleteValueW(k, L"Smarty Lens");
    RegCloseKey(k);
}
static void show_menu(HWND h) {
    HMENU m = CreatePopupMenu(), sub = CreatePopupMenu();
    free(g_menuFiles); g_menuFiles = (wchar_t (*)[MAX_PATH])calloc(60, sizeof *g_menuFiles); g_nMenuFiles = 0;
    wchar_t pat[MAX_PATH]; wfmt(pat, MAX_PATH, L"%ls\\*.html", g_savedir);
    WIN32_FIND_DATAW fd; HANDLE f = FindFirstFileW(pat, &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            wchar_t *fp = g_menuFiles[g_nMenuFiles]; wfmt(fp, MAX_PATH, L"%ls\\%ls", g_savedir, fd.cFileName);
            size_t len; char *b = read_file(fp, &len);
            if (b && len > 8 && !memcmp(b, "LENTE1\n", 7)) {
                char *nl = strchr(b + 7, '\n'); if (nl) *nl = 0;
                wchar_t *t = u8w(b + 7);
                AppendMenuW(sub, MF_STRING, (UINT_PTR)(100 + g_nMenuFiles), t); free(t);
                g_nMenuFiles++;
            }
            free(b);
        } while (g_nMenuFiles < 60 && FindNextFileW(f, &fd));
        FindClose(f);
    }
    if (!g_nMenuFiles) AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, L"(nessuna)");
    AppendMenuW(m, MF_STRING | (g_active ? MF_CHECKED : 0), 1, L"Evidenziazione attiva");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_POPUP, (UINT_PTR)sub, L"Funzioni salvate");
    AppendMenuW(m, MF_STRING, 2, L"Apri cartella dati");
    AppendMenuW(m, MF_STRING, 5, L"Apri configurazione");
    AppendMenuW(m, MF_STRING | (autostart_get() ? MF_CHECKED : 0), 3, L"Avvia con Windows");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, 4, L"Esci");
    POINT p; GetCursorPos(&p);
    SetForegroundWindow(h);
    int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, p.x, p.y, 0, h, NULL);
    PostMessageW(h, WM_NULL, 0, 0);
    DestroyMenu(m);
    if (cmd == 1) set_active(!g_active);
    else if (cmd == 2) ShellExecuteW(NULL, L"open", g_data, NULL, NULL, SW_SHOWNORMAL);
    else if (cmd == 5) ShellExecuteW(NULL, L"open", L"notepad.exe", g_ini, NULL, SW_SHOWNORMAL);
    else if (cmd == 3) autostart_set(!autostart_get());
    else if (cmd == 4) DestroyWindow(h);
    else if (cmd >= 100 && cmd < 100 + g_nMenuFiles) {
        Req *rq = (Req *)calloc(1, sizeof(Req));
        rq->type = REQ_SAVED; wcscpy(rq->file, g_menuFiles[cmd - 100]);
        RECT r; GetWindowRect(h, &r); g_anchor.x = r.left - S(520); g_anchor.y = r.top;
        start_req(rq);
    }
}
static LRESULT CALLBACK main_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    static POINT down, org; static int moved;
    switch (m) {
    case WM_LBUTTONDOWN: {
        RECT r; GetWindowRect(h, &r); org.x = r.left; org.y = r.top;
        GetCursorPos(&down); moved = 0; SetCapture(h);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (GetCapture() == h) {
            POINT p; GetCursorPos(&p);
            if (moved || abs(p.x - down.x) > S(4) || abs(p.y - down.y) > S(4)) {
                moved = 1;
                SetWindowPos(h, NULL, org.x + p.x - down.x, org.y + p.y - down.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == h) {
            ReleaseCapture();
            if (!moved) set_active(!g_active);
            else {
                RECT r; wchar_t v[16]; GetWindowRect(h, &r);
                wfmt(v, 16, L"%d", (int)r.left); WritePrivateProfileStringW(L"lente", L"x", v, g_ini);
                wfmt(v, 16, L"%d", (int)r.top);  WritePrivateProfileStringW(L"lente", L"y", v, g_ini);
            }
        }
        return 0;
    case WM_RBUTTONUP: show_menu(h); return 0;
    case WM_APP_TRIGGER:
        g_trig_pt.x = (int)wp; g_trig_pt.y = (int)lp;
        SetTimer(h, T_TRIGGER, 140, NULL);
        return 0;
    case WM_TIMER:
        if (wp == T_TRIGGER) {
            KillTimer(h, T_TRIGGER);
            if (g_active && InterlockedCompareExchange(&g_busy, 1, 0) == 0) {
                Req *rq = (Req *)calloc(1, sizeof(Req)); rq->type = REQ_SEL;
                g_anchor = g_trig_pt; start_req(rq);
            }
        } else if (wp == T_AUTOCLOSE) popup_close();
        return 0;
    case WM_APP_STATUS: {
        wchar_t *s = (wchar_t *)lp;
        if ((UINT)wp == g_gen) popup_message(s, 0);
        free(s);
        return 0;
    }
    case WM_APP_RESULT: {
        Res *rs = (Res *)lp;
        if (rs->gen != g_gen) { res_free(rs); return 0; }
        if (rs->ok) popup_result(rs);
        else { popup_message(rs->msg ? rs->msg : L"Errore", 3500); res_free(rs); }
        return 0;
    }
    case WM_APP_CLOSE: popup_close(); return 0;
    case WM_DESTROY:
        if (g_hook) UnhookWindowsHookEx(g_hook);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

/* ------------------------------------------------- prova senza interfaccia */
static int run_test(const wchar_t *text) {
    Buf o = {0}; Query q; char *u = wu8(text);
    ULONGLONG t0 = GetTickCount64();
    if (!parse_query(u, &q)) buf_printf(&o, "selezione: [%s]\nnessun nome riconosciuto\n", u);
    else {
        buf_printf(&o, "selezione: [%s]\nquery=%s mostrato=%s suggerimento=%s segmenti=%d\n", u, q.q, q.shown, q.hint, q.nseg);
        int nd = ensure_docs(0);
        buf_printf(&o, "documentazioni usabili=%d (%llu ms)\n", nd, GetTickCount64() - t0);
        for (int i = 0; i < g_ndocs; i++) buf_printf(&o, "  %s -> %s [%s] ok=%d\n", g_docs[i].base, g_docs[i].slug, g_docs[i].label, g_docs[i].ok);
        Cand c[MAXC]; t0 = GetTickCount64();
        int n = search_all(&q, c);
        buf_printf(&o, "candidati=%d (%llu ms)\n", n, GetTickCount64() - t0);
        for (int i = 0; i < n; i++) buf_printf(&o, "  %d  %s  %s  (%s)\n", c[i].score, g_docs[c[i].doc].base, c[i].full, c[i].path);
        if (n) {
            Res *rs = (Res *)calloc(1, sizeof(Res)); t0 = GetTickCount64();
            int ok = load_entry(rs, &c[0]);
            buf_printf(&o, "voce ok=%d salvata=%d (%llu ms)\n", ok, rs->saved, GetTickCount64() - t0);
            if (ok) {
                char *a = wu8(rs->title), *b = wu8(rs->label), *d = wu8(rs->url);
                buf_printf(&o, "titolo=%s\netichetta=%s\nurl=%s\nrtf (%d byte):\n", a, b, d, (int)strlen(rs->rtf));
                buf_puts(&o, rs->rtf); buf_puts(&o, "\n");
                free(a); free(b); free(d);
            } else if (rs->msg) { char *a = wu8(rs->msg); buf_printf(&o, "messaggio=%s\n", a); free(a); }
            res_free(rs);
        }
    }
    wchar_t p[MAX_PATH]; wfmt(p, MAX_PATH, L"%ls\\prova.txt", g_data);
    write_file(p, o.p ? o.p : "", o.n);
    free(u); free(o.p);
    return 0;
}

/* Diagnostica: prova la cattura (Ctrl+C simulato + ripristino appunti) su una finestrella propria. */
static unsigned clip_fingerprint(int *nfmt) {       /* impronta degli appunti, senza leggerne il senso */
    unsigned h = 2166136261u; *nfmt = 0;
    if (!clip_open(NULL)) return 0;
    for (UINT f = 0; (f = EnumClipboardFormats(f)) != 0;) (*nfmt)++;
    HANDLE hd = GetClipboardData(CF_UNICODETEXT);
    if (hd) {
        const unsigned char *p = (const unsigned char *)GlobalLock(hd);
        if (p) { SIZE_T n = GlobalSize(hd); for (SIZE_T i = 0; i + 1 < n && (p[i] || p[i + 1]); i++) h = (h ^ p[i]) * 16777619u; GlobalUnlock(hd); }
    }
    CloseClipboard();
    return h;
}
static DWORD WINAPI capture_thread(LPVOID out) { *(wchar_t **)out = capture_selection(); return 0; }
static int run_capture_test(HINSTANCE hi) {
    Buf o = {0}; int n0, n1; MSG msg;
    unsigned h0 = clip_fingerprint(&n0);
    HWND w = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"EDIT", L"np.linspace", WS_POPUP | WS_VISIBLE | WS_BORDER,
                             120, 120, 260, 36, NULL, NULL, hi, NULL);
    SetForegroundWindow(w); SetFocus(w);
    SendMessageW(w, EM_SETSEL, 0, -1);
    for (ULONGLONG t = GetTickCount64(); GetTickCount64() - t < 400;) { while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } Sleep(10); }
    int fg = GetForegroundWindow() == w;
    buf_printf(&o, "finestra di prova in primo piano: %s\n", fg ? "si" : "no (prova saltata: nessun tasto inviato)");
    if (fg) {
        wchar_t *text = NULL; g_selftest = 1;
        HANDLE th = CreateThread(NULL, 0, capture_thread, &text, 0, NULL);
        while (MsgWaitForMultipleObjects(1, &th, FALSE, 3000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        CloseHandle(th);
        char *u = text ? wu8(text) : NULL;
        buf_printf(&o, "testo catturato: [%s]\n", u ? u : "(niente)");
        unsigned h1 = clip_fingerprint(&n1);
        buf_printf(&o, "appunti ripristinati: %s (formati prima=%d dopo=%d)\n", h0 == h1 ? "si, identici" : "NO, diversi", n0, n1);
        free(u); free(text);
    }
    DestroyWindow(w);
    wchar_t p[MAX_PATH]; wfmt(p, MAX_PATH, L"%ls\\prova.txt", g_data);
    write_file(p, o.p, o.n); free(o.p);
    return 0;
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, PWSTR cmd, int show) {
    (void)hp; (void)show;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    InitializeCriticalSection(&g_cs);
    init_paths(); load_cfg();
    if (!wcsncmp(cmd, L"--test ", 7)) return run_test(cmd + 7);
    if (!wcsncmp(cmd, L"--test-cattura", 14)) return run_capture_test(hi);
    CreateMutexW(NULL, TRUE, L"SmartyLens.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    g_dpi = (int)GetDpiForSystem();
    get_theme(&g_th);
    g_fUI = mkfont(95, FW_NORMAL); g_fBold = mkfont(100, FW_SEMIBOLD);

    WNDCLASSW wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = main_proc; wc.hInstance = hi; wc.lpszClassName = L"SmartyLensIcon"; wc.hCursor = LoadCursorW(NULL, IDC_HAND);
    RegisterClassW(&wc);
    wc.lpfnWndProc = pop_proc; wc.lpszClassName = L"SmartyLensPopup"; wc.hCursor = LoadCursorW(NULL, IDC_ARROW); wc.style = CS_DROPSHADOW;
    RegisterClassW(&wc);

    int N = icon_size(), x = GetPrivateProfileIntW(L"lente", L"x", -1, g_ini), y = GetPrivateProfileIntW(L"lente", L"y", -1, g_ini);
    POINT probe = { x + N / 2, y + N / 2 };
    if ((x == -1 && y == -1) || !MonitorFromPoint(probe, MONITOR_DEFAULTTONULL)) {
        RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        x = wa.right - N - S(28); y = wa.top + (wa.bottom - wa.top) / 3;
    }
    g_main = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"SmartyLensIcon", L"Smarty Lens",
                             WS_POPUP, x, y, N, N, NULL, NULL, hi, NULL);
    g_pop = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"SmartyLensPopup", L"Smarty Lens", WS_POPUP | WS_CLIPCHILDREN,
                            0, 0, 10, 10, NULL, NULL, hi, NULL);
    if (!g_main || !g_pop) return 1;
    int corner = 2;                                  /* angoli arrotondati su Windows 11 */
    DwmSetWindowAttribute(g_pop, 33, &corner, sizeof corner);
    render_icon();
    ShowWindow(g_main, SW_SHOWNOACTIVATE);
    if (GetPrivateProfileIntW(L"lente", L"attivo_all_avvio", 0, g_ini)) set_active(1);
    if (!wcsncmp(cmd, L"--cerca ", 8)) {             /* apre subito il riquadro per un nome */
        Req *rq = (Req *)calloc(1, sizeof(Req)); rq->type = REQ_TEXT;
        wcsncpy(rq->text, cmd + 8, 299);
        RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        g_anchor.x = (wa.left + wa.right) / 2 - S(250); g_anchor.y = wa.top + (wa.bottom - wa.top) / 4;
        start_req(rq);
    }
    trim_memory();
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE && g_pop_vis && (msg.hwnd == g_pop || IsChild(g_pop, msg.hwnd))) { popup_close(); continue; }
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    return 0;
}
