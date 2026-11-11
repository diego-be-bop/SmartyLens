/* Smarty Lens - parte comune a tutti i sistemi operativi.
 * Riconoscimento del nome evidenziato, ricerca negli indici DevDocs, estrazione della voce
 * dalla pagina HTML e conversione in testo formattato (RTF minimale).
 * Il file di piattaforma che lo include deve definire: get_theme() e plat_read_index().
 */
#ifndef SMARTYLENS_CORE_H
#define SMARTYLENS_CORE_H
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#ifndef _WIN32
#include <strings.h>
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#endif

#define MAXC 6
#define SL_R(c) ((int)((c) & 0xFF))
#define SL_G(c) ((int)(((c) >> 8) & 0xFF))
#define SL_B(c) ((int)(((c) >> 16) & 0xFF))
#define SL_RGB(r, g, b) ((unsigned)(r) | ((unsigned)(g) << 8) | ((unsigned)(b) << 16))

/* ------------------------------------------------------------------ tipi */
typedef struct { char *p; size_t n, cap; } Buf;
typedef struct { char base[40], slug[64], label[64], root[24]; int ok; } Doc;
typedef struct { int doc, score; char full[200], path[260]; } Cand;
typedef struct { char q[200], last[80], hint[24], dochint[24], shown[200]; int nseg, only; } Query;
typedef struct { unsigned bg, head, text, dim, accent, code, rose, border; int dark; } Theme;   /* colori 0x00BBGGRR */

static Doc g_docs[20]; static int g_ndocs;
static char g_alias_k[40][24], g_alias_v[40][64]; static int g_nalias;

static void get_theme(Theme *t);                                /* definita dalla piattaforma */
static char *plat_read_index(const Doc *d, size_t *len);        /* legge il file indice di una documentazione */

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
static const char *find_n(const char *h, const char *end, const char *needle) {
    size_t n = strlen(needle);
    if (!n || (size_t)(end - h) < n) return NULL;
    for (const char *e = end - n; h <= e; h++)
        if (*h == *needle && !memcmp(h, needle, n)) return h;
    return NULL;
}

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
    size_t len;
    char *data = plat_read_index(&g_docs[di], &len);
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
        SL_R(t.text), SL_G(t.text), SL_B(t.text), SL_R(t.dim), SL_G(t.dim), SL_B(t.dim),
        SL_R(t.code), SL_G(t.code), SL_B(t.code), SL_R(t.accent), SL_G(t.accent), SL_B(t.accent));
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

/* ---------------------------------------------------------------------- icona */
/* Icona: testa di scoiattolo robot in pixel art 24x24 (meta' sinistra, specchiata).
   o contorno, f/h/s metallo, e orecchio, v visiera, n naso, Y/y/w occhio, r raggio di energia. */
static const char *SQ[24] = {
    "............", "..r........r", "...r.......r", "....oo......", "...oeeo.....", "...oeefooooo",
    "...offfffhhh", "..offhhfffff", "..oooooooooo", "..ovvvvvvvvv", "rrovvvYYYvvv", "..ovvvywyvvv",
    "..ovvvYYYvvv", "..oooooooooo", "..osffffffnn", "..osfffffffn", "...osffffoff", "....osssffoo",
    ".....ooooooo", "...r........", "..r........r", "...........r", "............", "............" };
static unsigned sq_color(char c, int on) {
    switch (c) {
    case 'o': case 'n': return 0x383C45;
    case 'f': return 0xE0E3EA;
    case 'h': return 0xF6F7FA;
    case 's': return 0xB4BAC6;
    case 'e': return 0x690731;
    case 'v': return 0x23262C;
    case 'r': return on ? 0xBC0555 : 0x9AA0AB;           /* 0xRRGGBB; raggi: Rosewood da attivo, grigi da spento */
    case 'Y': return on ? 0xBC0555 : 0x23262C;           /* occhio aperto / palpebra chiusa */
    case 'y': return on ? 0xBC0555 : 0x9AA0AB;
    case 'w': return on ? 0xFFFFFF : 0x9AA0AB;
    }
    return 0;
}
#endif
