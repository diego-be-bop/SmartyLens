/* Smarty Lens - parte comune a Linux e macOS (POSIX + libcurl).
 * File, configurazione, rete, elenco delle documentazioni, caricamento delle voci e
 * ciclo di una richiesta. Si include dopo smartylens_core.h.
 * Il file di piattaforma deve definire plat_status() (mostra un messaggio di avanzamento).
 */
#ifndef SMARTYLENS_POSIX_H
#define SMARTYLENS_POSIX_H
#include <curl/curl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>

#define DOCS_URL "https://documents.devdocs.io"
#define DEFAULT_DOCS "python,numpy,pandas,matplotlib,scikit_learn,pytorch,tensorflow,statsmodels,requests"

enum { REQ_TEXT, REQ_CAND, REQ_SAVED };
typedef struct { int type; unsigned gen; char text[1200]; int ncand, pick; Cand cand[MAXC]; char file[PATH_MAX]; } Req;
typedef struct {
    unsigned gen; int ok;
    char *title, *label, *url, *msg, *rtf;
    char *blob; size_t blob_len; char save_path[PATH_MAX]; int saved;
    int ncand, pick; Cand cand[MAXC];
} Res;

static char g_data[PATH_MAX], g_idx[PATH_MAX], g_savedir[PATH_MAX], g_ini[PATH_MAX];
static int g_autosave, g_dark, g_docs_ready;
static long long g_net_fail;
static pthread_mutex_t g_cs = PTHREAD_MUTEX_INITIALIZER;

/* Messaggio di avanzamento per la richiesta gen; text e' allocato con malloc e passa di proprieta'.
   Puo' essere chiamata da un thread secondario. */
static void plat_status(unsigned gen, char *text);

static long long now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static char *sl_fmt(const char *fmt, va_list ap) {
    va_list c; va_copy(c, ap); int n = vsnprintf(NULL, 0, fmt, c); va_end(c);
    char *s = (char *)malloc((size_t)n + 1); vsnprintf(s, (size_t)n + 1, fmt, ap); return s;
}

/* ------------------------------------------------------------------ file */
static char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    char *p = (char *)malloc((size_t)sz + 1); size_t rd = fread(p, 1, (size_t)sz, f); fclose(f);
    p[rd] = 0; if (len) *len = rd; return p;
}
static int write_file(const char *path, const char *data, size_t len) {
    FILE *f = fopen(path, "wb"); if (!f) return 0;
    size_t wr = fwrite(data, 1, len, f);
    return fclose(f) == 0 && wr == len;
}
static double file_age_days(const char *path) {
    struct stat st; if (stat(path, &st)) return -1;
    double d = difftime(time(NULL), st.st_mtime) / 86400.0; return d > 0 ? d : 0;
}
static void mkdirs(const char *path) {                        /* come mkdir -p */
    char t[PATH_MAX]; snprintf(t, sizeof t, "%s", path);
    for (char *p = t + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(t, 0755); *p = '/'; }
    mkdir(t, 0755);
}
static char *plat_read_index(const Doc *d, size_t *len) {
    char p[PATH_MAX]; snprintf(p, sizeof p, "%.3900s/%s.json", g_idx, d->slug); return read_file(p, len);
}
/* Palette: Platinum EDEFF3, Alabaster Grey E0E3EA, Charcoal Blue 383C45, Ruby Red 970126,
   Crimson Violet 690731, Rosewood BC0555. In tema scuro i rossi sono schiariti per restare leggibili. */
static void get_theme(Theme *t) {
    t->dark = g_dark;
    if (!g_dark) { t->bg = SL_RGB(0xED,0xEF,0xF3); t->head = SL_RGB(0xE0,0xE3,0xEA); t->text = SL_RGB(0x38,0x3C,0x45); t->dim = SL_RGB(0x6E,0x73,0x7E);
                   t->accent = SL_RGB(0x97,0x01,0x26); t->code = SL_RGB(0x69,0x07,0x31); t->rose = SL_RGB(0xBC,0x05,0x55); t->border = SL_RGB(0xC3,0xC8,0xD2); }
    else         { t->bg = SL_RGB(0x38,0x3C,0x45); t->head = SL_RGB(0x2D,0x30,0x38); t->text = SL_RGB(0xED,0xEF,0xF3); t->dim = SL_RGB(0xA9,0xAE,0xB9);
                   t->accent = SL_RGB(0xFF,0x8F,0xB5); t->code = SL_RGB(0xF2,0xB8,0xCC); t->rose = SL_RGB(0xFF,0x5C,0x9A); t->border = SL_RGB(0x55,0x5A,0x66); }
}
/* Una sola copia in esecuzione per cartella dati. Ritorna 0 se ce n'e' gia' un'altra. */
static int single_instance(void) {
    char lock[PATH_MAX]; snprintf(lock, sizeof lock, "%.3900s/.lock", g_data);
    int lf = open(lock, O_CREAT | O_RDWR, 0644);
    return !(lf >= 0 && flock(lf, LOCK_EX | LOCK_NB));
}

/* --------------------------------------------------------- configurazione */
static const char DEFAULT_INI[] =
    "; Smarty Lens - configurazione (riavvia Smarty Lens dopo averla modificata)\n"
    "[lente]\n"
    "; 1 = evidenziazione gia' attiva all'avvio\n"
    "attivo_all_avvio=0\n"
    "; 1 = salva automaticamente ogni funzione consultata, per rileggerla offline\n"
    "salva_automatico=0\n"
    "; documentazioni da consultare, in ordine di priorita' (nomi come su https://devdocs.io)\n"
    "docs=" DEFAULT_DOCS "\n"
    "x=-1\n"
    "y=-1\n"
    "\n"
    "; abbreviazioni usate negli import: nome=modulo\n"
    "[alias]\n"
    "np=numpy\npd=pandas\nplt=matplotlib.pyplot\nmpl=matplotlib\nnn=torch.nn\n"
    "F=torch.nn.functional\noptim=torch.optim\ntensorflow=tf\nkeras=tf.keras\n"
    "sm=statsmodels.api\nsmf=statsmodels.formula.api\n";

/* Scorre il file .ini; cb riceve sezione, chiave, valore. */
static void ini_each(void (*cb)(const char *, const char *, const char *, void *), void *ud) {
    size_t n; char *txt = read_file(g_ini, &n); if (!txt) return;
    char sec[40] = "", *save = NULL;
    for (char *ln = strtok_r(txt, "\r\n", &save); ln; ln = strtok_r(NULL, "\r\n", &save)) {
        while (*ln == ' ' || *ln == '\t') ln++;
        if (*ln == ';' || *ln == '#' || !*ln) continue;
        if (*ln == '[') { char *e = strchr(ln, ']'); if (e) { *e = 0; snprintf(sec, sizeof sec, "%s", ln + 1); } continue; }
        char *eq = strchr(ln, '='); if (!eq) continue;
        *eq = 0; cb(sec, ln, eq + 1, ud);
    }
    free(txt);
}
typedef struct { const char *sec, *key; char *out; size_t cap; } IniGet;
static void ini_get_cb(const char *s, const char *k, const char *v, void *ud) {
    IniGet *g = (IniGet *)ud; if (!strcmp(s, g->sec) && !strcmp(k, g->key)) snprintf(g->out, g->cap, "%s", v);
}
static void ini_get(const char *key, const char *def, char *out, size_t cap) {
    IniGet g = { "lente", key, out, cap }; snprintf(out, cap, "%s", def); ini_each(ini_get_cb, &g);
}
static int ini_int(const char *key, int def) { char b[32], d[32]; snprintf(d, sizeof d, "%d", def); ini_get(key, d, b, sizeof b); return atoi(b); }
static void ini_set_int(const char *key, int val) {              /* sostituisce "key=..." mantenendo il resto */
    size_t n; char *txt = read_file(g_ini, &n); if (!txt) return;
    Buf b = {0}; size_t kl = strlen(key); int done = 0;
    for (char *p = txt; *p;) {
        char *e = strchr(p, '\n'); size_t ll = e ? (size_t)(e - p) + 1 : strlen(p);
        if (!done && !strncmp(p, key, kl) && p[kl] == '=') { buf_printf(&b, "%s=%d\n", key, val); done = 1; }
        else buf_put(&b, p, ll);
        p += ll;
    }
    if (b.p) write_file(g_ini, b.p, b.n);
    free(b.p); free(txt);
}
static void alias_cb(const char *s, const char *k, const char *v, void *ud) {
    (void)ud; if (strcmp(s, "alias") || g_nalias >= 40) return;
    snprintf(g_alias_k[g_nalias], 24, "%s", k); snprintf(g_alias_v[g_nalias], 64, "%s", v); g_nalias++;
}
/* Prepara la cartella dati (indici, salvate, config.ini) e legge la configurazione. */
static void init_data(const char *dir) {
    snprintf(g_data, sizeof g_data, "%.3900s", dir); mkdirs(g_data);
    snprintf(g_idx, sizeof g_idx, "%.3900s/indici", g_data);      mkdir(g_idx, 0755);
    snprintf(g_savedir, sizeof g_savedir, "%.3900s/salvate", g_data); mkdir(g_savedir, 0755);
    snprintf(g_ini, sizeof g_ini, "%.3900s/config.ini", g_data);
    if (access(g_ini, F_OK)) write_file(g_ini, DEFAULT_INI, sizeof DEFAULT_INI - 1);
    g_autosave = ini_int("salva_automatico", 0);
    ini_each(alias_cb, NULL);
}

/* -------------------------------------------------------------------- rete */
typedef struct { Buf b; long long t0; long wait_ms; } Dl;
static size_t dl_write(char *p, size_t sz, size_t n, void *ud) { buf_put(&((Dl *)ud)->b, p, sz * n); return sz * n; }
static int dl_progress(void *ud, curl_off_t dt, curl_off_t dn, curl_off_t ut, curl_off_t un) {
    (void)dt; (void)ut; (void)un; Dl *d = (Dl *)ud;        /* nessun byte entro il tempo previsto: annulla e riprova */
    return dn == 0 && now_ms() - d->t0 > d->wait_ms;
}
static char *http_get(const char *url, size_t *outLen, int *status) {
    static const long WAIT[3] = { 2000, 3500, 6000 };
    *status = 0;
    for (int attempt = 0; attempt < 3; attempt++) {
        /* La CDN a volte blocca per 20-30 s la prima richiesta di una pagina non in cache:
           dal secondo tentativo si aggiunge un parametro che aggira la cache bloccata. */
        char u[1200]; if (attempt) snprintf(u, sizeof u, "%s?r=%d", url, attempt); else snprintf(u, sizeof u, "%s", url);
        Dl d; memset(&d, 0, sizeof d); d.t0 = now_ms(); d.wait_ms = WAIT[attempt];
        CURL *c = curl_easy_init(); if (!c) break;
        curl_easy_setopt(c, CURLOPT_URL, u);
        curl_easy_setopt(c, CURLOPT_USERAGENT, "SmartyLens/1.0");
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 3000L);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 40L);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, dl_write);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &d);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, dl_progress);
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, &d);
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        CURLcode rc = curl_easy_perform(c);
        long code = 0; curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
        curl_easy_cleanup(c);
        if (rc == CURLE_OK) *status = (int)code;
        if (rc == CURLE_OK && code == 200) {
            if (!d.b.p) buf_puts(&d.b, "");
            *outLen = d.b.n; g_net_fail = 0; return d.b.p;
        }
        free(d.b.p);
        if (*status >= 300 && *status < 500) return NULL;          /* es. 404: inutile riprovare */
    }
    g_net_fail = now_ms() | 1;
    return NULL;
}
static int net_ok(void) { return !g_net_fail || now_ms() - g_net_fail > 60000; }

/* ------------------------------------------------- elenco documentazioni */
static void post_status(unsigned gen, const char *fmt, ...) {
    if (!gen) return;
    va_list ap; va_start(ap, fmt); char *s = sl_fmt(fmt, ap); va_end(ap);
    plat_status(gen, s);
}
/* Prepara l'elenco delle documentazioni e scarica gli indici mancanti. Ritorna quante sono usabili. */
static int ensure_docs(unsigned gen) {
    pthread_mutex_lock(&g_cs);
    char sl[PATH_MAX], p[PATH_MAX], *save = NULL; size_t n; int st;
    snprintf(sl, sizeof sl, "%.3900s/_versioni.txt", g_idx);
    if (!g_docs_ready) {
        char docs[600]; ini_get("docs", DEFAULT_DOCS, docs, sizeof docs);
        g_ndocs = 0;
        for (char *t = strtok_r(docs, ", ;", &save); t && g_ndocs < 20; t = strtok_r(NULL, ", ;", &save)) {
            Doc *d = &g_docs[g_ndocs++]; memset(d, 0, sizeof *d);
            snprintf(d->base, sizeof d->base, "%s", t);
            for (int i = 0; ROOTS[i][0]; i++) if (!strcmp(ROOTS[i][0], t)) strcpy(d->root, ROOTS[i][1]);
        }
        char old[20][64]; memset(old, 0, sizeof old);
        char *txt = read_file(sl, &n);                           /* versioni gia' note: base \t slug \t etichetta */
        if (txt) {
            for (char *ln = strtok_r(txt, "\r\n", &save); ln; ln = strtok_r(NULL, "\r\n", &save)) {
                char *a = strchr(ln, '\t'); if (!a) continue; *a++ = 0;
                char *b = strchr(a, '\t'); if (!b) continue; *b++ = 0;
                for (int i = 0; i < g_ndocs; i++) if (!strcmp(g_docs[i].base, ln)) {
                    snprintf(g_docs[i].slug, 64, "%s", a); snprintf(g_docs[i].label, 64, "%s", b); strcpy(old[i], g_docs[i].slug);
                }
            }
            free(txt);
        }
        int missing = 0, done = 1; for (int i = 0; i < g_ndocs; i++) if (!g_docs[i].slug[0]) missing = 1;
        double age = file_age_days(sl);
        if ((missing || age < 0 || age > 14) && net_ok()) {
            char *js = http_get("https://devdocs.io/docs.json", &n, &st);
            if (js) {
                for (int i = 0; i < g_ndocs; i++) g_docs[i].slug[0] = 0;
                resolve_slugs(js, n); free(js);
                Buf b = {0};
                for (int i = 0; i < g_ndocs; i++) {
                    Doc *d = &g_docs[i];
                    if (!d->slug[0] && old[i][0]) strcpy(d->slug, old[i]);
                    if (!d->slug[0]) continue;
                    buf_printf(&b, "%s\t%s\t%s\n", d->base, d->slug, d->label);
                    if (old[i][0] && strcmp(old[i], d->slug)) { snprintf(p, sizeof p, "%.3900s/%s.json", g_idx, old[i]); unlink(p); }
                }
                if (b.p) { write_file(sl, b.p, b.n); free(b.p); }
            } else if (missing) done = 0;
        }
        g_docs_ready = done;
    }
    int usable = 0, todo = 0, k = 0;
    for (int i = 0; i < g_ndocs; i++) if (g_docs[i].slug[0]) { snprintf(p, sizeof p, "%.3900s/%s.json", g_idx, g_docs[i].slug); if (file_age_days(p) < 0) todo++; }
    for (int i = 0; i < g_ndocs; i++) {
        Doc *d = &g_docs[i];
        if (!d->slug[0]) { d->ok = 0; continue; }
        snprintf(p, sizeof p, "%.3900s/%s.json", g_idx, d->slug);
        double age = file_age_days(p);
        if ((age < 0 || age > 30) && net_ok()) {
            if (age < 0) post_status(gen, "Scarico gli indici, solo la prima volta… %d/%d", ++k, todo);
            char url[300]; snprintf(url, sizeof url, DOCS_URL "/%s/index.json", d->slug);
            char *js = http_get(url, &n, &st);
            if (js) { if (n > 20) write_file(p, js, n); free(js); }
        }
        d->ok = file_age_days(p) >= 0;
        usable += d->ok;
    }
    pthread_mutex_unlock(&g_cs);
    return usable;
}

/* ------------------------------------------------------ caricamento voce */
static void res_free(Res *r) {
    if (!r) return;
    free(r->title); free(r->label); free(r->url); free(r->msg); free(r->rtf); free(r->blob); free(r);
}
/* Formato dei file salvati: "LENTE1\n titolo\n etichetta\n url\n firma\n" + HTML del corpo (uguale a Windows). */
static int res_from_blob(Res *rs, char *blob, size_t len) {
    if (len < 7 || memcmp(blob, "LENTE1\n", 7)) return 0;
    const char *p = blob + 7, *e = blob + len, *f[4]; size_t fl[4];
    for (int i = 0; i < 4; i++) {
        const char *nl = (const char *)memchr(p, '\n', (size_t)(e - p));
        if (!nl) return 0;
        f[i] = p; fl[i] = (size_t)(nl - p); p = nl + 1;
    }
    rs->title = strndup(f[0], fl[0]); rs->label = strndup(f[1], fl[1]); rs->url = strndup(f[2], fl[2]);
    rs->rtf = build_rtf(f[3], fl[3], p, (size_t)(e - p));
    rs->blob = blob; rs->blob_len = len; rs->ok = 1;
    return 1;
}
static void res_msg(Res *rs, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); free(rs->msg); rs->msg = sl_fmt(fmt, ap); va_end(ap); rs->ok = 0;
}
static int load_entry(Res *rs, const Cand *c) {
    const Doc *d = &g_docs[c->doc];
    char key[400]; snprintf(key, sizeof key, "%s__%s", d->base, c->path);
    for (char *k = key; *k; k++) if (!isalnum((unsigned char)*k) && *k != '.' && *k != '-' && *k != '_') *k = '_';
    key[150] = 0;
    snprintf(rs->save_path, sizeof rs->save_path, "%.3800s/%s.html", g_savedir, key);
    size_t len; char *blob = read_file(rs->save_path, &len);
    if (blob && res_from_blob(rs, blob, len)) { rs->saved = 1; return 1; }
    free(blob);
    if (!net_ok()) { res_msg(rs, "Sei offline e «%s» non è tra le funzioni salvate", c->full); return 0; }
    char pth[300]; snprintf(pth, sizeof pth, "%s", c->path);
    char *fr = strchr(pth, '#'); if (fr) *fr++ = 0;
    char url[800]; snprintf(url, sizeof url, DOCS_URL "/%s/%s.html", d->slug, pth);
    size_t hl; int st;
    char *html = http_get(url, &hl, &st);
    if (!html) {
        if (st) res_msg(rs, "Pagina non trovata per «%s» (HTTP %d)", c->full, st);
        else res_msg(rs, "Nessuna connessione: «%s» non è tra le funzioni salvate", c->full);
        return 0;
    }
    Buf sig = {0}, b = {0}; const char *body; size_t blen;
    extract_entry(html, hl, fr, &sig, &body, &blen);
    buf_printf(&b, "LENTE1\n%s\n%s\nhttps://devdocs.io/%s/%s\n", c->full, d->label, d->slug, c->path);
    if (sig.p) buf_put(&b, sig.p, sig.n);
    buf_puts(&b, "\n");
    buf_put(&b, body, blen);
    free(sig.p); free(html);
    if (!res_from_blob(rs, b.p, b.n)) { free(b.p); res_msg(rs, "Errore interno"); return 0; }
    if (g_autosave && write_file(rs->save_path, rs->blob, rs->blob_len)) rs->saved = 1;
    return 1;
}

/* Esegue una richiesta (in un thread secondario) e libera rq.
   Ritorna il risultato da mostrare, oppure NULL se nel testo non c'e' nessun nome riconoscibile. */
static Res *run_request(Req *rq) {
    Res *rs = (Res *)calloc(1, sizeof(Res));
    rs->gen = rq->gen;
    if (rq->type == REQ_SAVED) {
        size_t len; char *blob = read_file(rq->file, &len);
        snprintf(rs->save_path, sizeof rs->save_path, "%s", rq->file);
        if (blob && res_from_blob(rs, blob, len)) rs->saved = 1;
        else { free(blob); res_msg(rs, "File salvato non leggibile"); }
    } else if (rq->type == REQ_CAND) {
        memcpy(rs->cand, rq->cand, sizeof rs->cand); rs->ncand = rq->ncand; rs->pick = rq->pick;
        load_entry(rs, &rs->cand[rs->pick]);
    } else {
        Query q;
        if (!parse_query(rq->text, &q)) { free(rq); res_free(rs); return NULL; }
        post_status(rq->gen, "Cerco «%s»…", q.shown);
        int usable = ensure_docs(rq->gen);
        rs->ncand = usable ? search_all(&q, rs->cand) : 0;
        if (!usable) res_msg(rs, "Nessuna connessione e nessun indice scaricato: serve internet almeno la prima volta");
        else if (!rs->ncand) res_msg(rs, "Nessun risultato per «%s»", q.shown);
        else load_entry(rs, &rs->cand[0]);
    }
    free(rq);
    return rs;
}

/* ------------------------------------------------- prova senza interfaccia */
static int run_test(const char *text) {
    Buf o = {0}; Query q; long long t0 = now_ms();
    if (!parse_query(text, &q)) buf_printf(&o, "selezione: [%s]\nnessun nome riconosciuto\n", text);
    else {
        buf_printf(&o, "selezione: [%s]\nquery=%s mostrato=%s suggerimento=%s segmenti=%d\n", text, q.q, q.shown, q.hint, q.nseg);
        int nd = ensure_docs(0);
        buf_printf(&o, "documentazioni usabili=%d (%d ms)\n", nd, (int)(now_ms() - t0));
        Cand c[MAXC]; int n = search_all(&q, c);
        buf_printf(&o, "candidati=%d\n", n);
        for (int i = 0; i < n; i++) buf_printf(&o, "  %d  %s  %s  (%s)\n", c[i].score, g_docs[c[i].doc].base, c[i].full, c[i].path);
        if (n) {
            Res *rs = (Res *)calloc(1, sizeof(Res)); t0 = now_ms();
            int ok = load_entry(rs, &c[0]);
            buf_printf(&o, "voce ok=%d salvata=%d (%d ms)\n", ok, rs->saved, (int)(now_ms() - t0));
            if (ok) buf_printf(&o, "titolo=%s\netichetta=%s\nurl=%s\nrtf (%d byte)\n", rs->title, rs->label, rs->url, (int)strlen(rs->rtf));
            else if (rs->msg) buf_printf(&o, "messaggio=%s\n", rs->msg);
            res_free(rs);
        }
    }
    fputs(o.p ? o.p : "", stdout); free(o.p);
    return 0;
}
#endif
