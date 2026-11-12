/* Smarty Lens - versione Linux (GTK 3 + libcurl).
 * Su X11 il testo evidenziato e' gia' disponibile nella selezione PRIMARY: basta leggerla,
 * senza simulare Ctrl+C. Parti comuni: smartylens_core.h e smartylens_posix.h. Compilazione: vedi build_linux.sh
 */
#define _GNU_SOURCE
#include <gtk/gtk.h>
#include "smartylens_core.h"
#include "smartylens_posix.h"

#define ICON_K 2                                   /* pixel logici per ogni pixel del disegno */

static char g_exe[PATH_MAX];
static int g_active, g_pop_vis, g_ui;
static unsigned g_gen;
static Res *g_res;
static int g_ax, g_ay;                                              /* punto accanto a cui aprire il riquadro */
static guint g_trig_timer, g_close_timer, g_watch_timer;
static GtkWidget *g_icon, *g_pop, *g_msg, *g_content, *g_title, *g_doclabel, *g_bsave, *g_view, *g_scroll, *g_foot;
static Theme g_th;

/* Cartella dati: "dati" accanto all'eseguibile, oppure ~/.local/share/SmartyLens se non e' scrivibile. */
static void init_paths(void) {
    ssize_t n = readlink("/proc/self/exe", g_exe, sizeof g_exe - 1);
    if (n > 0) g_exe[n] = 0; else snprintf(g_exe, sizeof g_exe, "./smartylens");
    char dir[PATH_MAX], data[PATH_MAX]; snprintf(dir, sizeof dir, "%s", g_exe);
    char *s = strrchr(dir, '/'); if (s) *s = 0;
    snprintf(data, sizeof data, "%.3900s/dati", dir);
    mkdir(data, 0755);
    if (access(data, W_OK)) {
        const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
        if (xdg && *xdg) snprintf(data, sizeof data, "%.3900s/SmartyLens", xdg);
        else snprintf(data, sizeof data, "%.3800s/.local/share/SmartyLens", home ? home : ".");
    }
    init_data(data);
}

/* -------------------------------------------------------------- lavoro in background */
typedef struct { unsigned gen; char *text; } StatusMsg;
static gboolean on_status(gpointer p);
static gboolean on_result(gpointer p);
static void plat_status(unsigned gen, char *text) {
    if (!g_ui) { free(text); return; }
    StatusMsg *m = g_new0(StatusMsg, 1); m->gen = gen; m->text = text; g_idle_add(on_status, m);
}
static gpointer warm_thread(gpointer a) { (void)a; ensure_docs(0); return NULL; }
static gpointer worker(gpointer param) {
    Res *rs = run_request((Req *)param);
    if (rs) g_idle_add(on_result, rs);
    return NULL;
}
static void start_req(Req *rq) { rq->gen = ++g_gen; g_thread_unref(g_thread_new("lente", worker, rq)); }

/* Finestra senza bordi sempre in primo piano. Di norma e' "override-redirect" (come un menu), la scelta
   piu' prevedibile su X11; dentro WSL quelle finestre non vengono mostrate, quindi li' si usa una
   finestra normale senza decorazioni. Si puo' forzare con SMARTYLENS_MANAGED=1 (o =0). */
static int g_managed;
static GtkWidget *make_window(void) {
    if (!g_managed) return gtk_window_new(GTK_WINDOW_POPUP);
    GtkWidget *w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(w), FALSE);
    /* niente "skip taskbar" ne' tipo "utility": WSL non mostrerebbe la finestra */
    gtk_window_set_accept_focus(GTK_WINDOW(w), FALSE); gtk_window_set_focus_on_map(GTK_WINDOW(w), FALSE);
    gtk_window_set_title(GTK_WINDOW(w), "Smarty Lens");
    return w;
}

/* ------------------------------------------------------------------- riquadro */
static void pointer_state(int *x, int *y, GdkModifierType *mask) {
    GdkDisplay *dp = gdk_display_get_default();
    GdkDevice *dev = gdk_seat_get_pointer(gdk_display_get_default_seat(dp));
    gdk_window_get_device_position(gdk_screen_get_root_window(gdk_display_get_default_screen(dp)), dev, x, y, mask);
}
static int inside(GtkWidget *w, int x, int y) {
    int wx, wy, ww, wh;
    if (!w || !gtk_widget_get_visible(w)) return 0;
    gtk_window_get_position(GTK_WINDOW(w), &wx, &wy); gtk_window_get_size(GTK_WINDOW(w), &ww, &wh);
    return x >= wx && x < wx + ww && y >= wy && y < wy + wh;
}
static void popup_close(void) {
    if (g_close_timer) { g_source_remove(g_close_timer); g_close_timer = 0; }
    if (g_watch_timer) { g_source_remove(g_watch_timer); g_watch_timer = 0; }
    gtk_widget_hide(g_pop);
    res_free(g_res); g_res = NULL; g_pop_vis = 0;
    g_gen++;                                         /* scarta eventuali risposte in arrivo */
}
static gboolean autoclose_cb(gpointer d) { (void)d; g_close_timer = 0; popup_close(); return G_SOURCE_REMOVE; }
static gboolean watch_cb(gpointer d) {               /* clic fuori dal riquadro: chiudi */
    (void)d; int x, y; GdkModifierType m; pointer_state(&x, &y, &m);
    if ((m & (GDK_BUTTON1_MASK | GDK_BUTTON2_MASK | GDK_BUTTON3_MASK)) && !inside(g_pop, x, y) && !inside(g_icon, x, y)) {
        g_watch_timer = 0; popup_close(); return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}
static gboolean place_cb(gpointer d) {
    (void)d; if (!g_pop_vis) return G_SOURCE_REMOVE;
    int w, h; gtk_window_get_size(GTK_WINDOW(g_pop), &w, &h);
    GdkRectangle wa; gdk_monitor_get_workarea(gdk_display_get_monitor_at_point(gdk_display_get_default(), g_ax, g_ay), &wa);
    int x = g_ax + 12, y = g_ay + 18;
    if (y + h > wa.y + wa.height) y = g_ay - h - 14;
    if (y < wa.y) y = wa.y + 4;
    if (x + w > wa.x + wa.width) x = wa.x + wa.width - w - 4;
    if (x < wa.x) x = wa.x + 4;
    gtk_window_move(GTK_WINDOW(g_pop), x, y);
    return G_SOURCE_REMOVE;
}
/* Adatta l'altezza del riquadro al testo (fino a 360 px), a impaginazione avvenuta. */
static gboolean fit_cb(gpointer d) {
    (void)d; if (!g_pop_vis) return G_SOURCE_REMOVE;
    if (g_res) {
        GtkTextIter e; GdkRectangle rc;
        gtk_text_buffer_get_end_iter(gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_view)), &e);
        gtk_text_view_get_iter_location(GTK_TEXT_VIEW(g_view), &e, &rc);
        int h = rc.y + rc.height + 22; if (h > 360) h = 360; if (h < 40) h = 40;
        gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(g_scroll), h);
    }
    gtk_window_resize(GTK_WINDOW(g_pop), 1, 1);
    place_cb(NULL);
    return G_SOURCE_REMOVE;
}
static void popup_show(void) {
    gtk_window_resize(GTK_WINDOW(g_pop), 1, 1);
    gtk_widget_show(g_pop);
    g_pop_vis = 1; place_cb(NULL);
    g_timeout_add(30, fit_cb, NULL); g_timeout_add(150, fit_cb, NULL); g_timeout_add(400, fit_cb, NULL);
    if (!g_watch_timer) g_watch_timer = g_timeout_add(80, watch_cb, NULL);
}
static void popup_message(const char *text, int autoclose_ms) {
    res_free(g_res); g_res = NULL;
    gtk_label_set_text(GTK_LABEL(g_msg), text);
    gtk_widget_hide(g_content); gtk_widget_show(g_msg);
    popup_show();
    if (g_close_timer) { g_source_remove(g_close_timer); g_close_timer = 0; }
    if (autoclose_ms) g_close_timer = g_timeout_add((guint)autoclose_ms, autoclose_cb, NULL);
}
/* Interpreta l'RTF minimale prodotto da build_rtf() e lo inserisce nel GtkTextBuffer con gli stili. */
static void rtf_to_buffer(GtkTextBuffer *tb, const char *rtf) {
    const char *p = strstr(rtf, "\\uc1\\fs"); p = p ? strchr(p, ' ') : NULL; p = p ? p + 1 : rtf;   /* salta l'intestazione */
    int f = 0, cf = 1, b = 0, it = 0, li = 0; unsigned hi = 0;
    GString *run = g_string_new(NULL);
    #define FLUSH() do { if (run->len) { GtkTextIter s, e; gtk_text_buffer_get_end_iter(tb, &e); int off = gtk_text_iter_get_offset(&e); \
        gtk_text_buffer_insert(tb, &e, run->str, (gint)run->len); gtk_text_buffer_get_iter_at_offset(tb, &s, off); gtk_text_buffer_get_end_iter(tb, &e); \
        if (f) gtk_text_buffer_apply_tag_by_name(tb, "mono", &s, &e); \
        if (cf == 2) gtk_text_buffer_apply_tag_by_name(tb, "dim", &s, &e); else if (cf == 3) gtk_text_buffer_apply_tag_by_name(tb, "code", &s, &e); \
        else if (cf == 4) gtk_text_buffer_apply_tag_by_name(tb, "sig", &s, &e); \
        if (b) gtk_text_buffer_apply_tag_by_name(tb, "bold", &s, &e); if (it) gtk_text_buffer_apply_tag_by_name(tb, "ital", &s, &e); \
        if (li) { char t[8]; snprintf(t, sizeof t, "li%d", li > 3 ? 3 : li); gtk_text_buffer_apply_tag_by_name(tb, t, &s, &e); } \
        g_string_truncate(run, 0); } } while (0)
    while (*p && *p != '}') {
        if (*p != '\\') { g_string_append_c(run, *p++); continue; }
        p++;
        if (*p == '\\' || *p == '{' || *p == '}') { g_string_append_c(run, *p++); continue; }
        char w[16]; int n = 0, has = 0, neg = 0; long v = 0;
        while (isalpha((unsigned char)*p) && n < 15) w[n++] = *p++;
        w[n] = 0;
        if (*p == '-') { neg = 1; p++; }
        while (isdigit((unsigned char)*p)) { v = v * 10 + (*p++ - '0'); has = 1; }
        if (neg) v = -v;
        if (!strcmp(w, "u")) {                                     /* \uN? : carattere Unicode (anche coppie surrogate) */
            if (*p == '?') p++;
            unsigned cp = (unsigned)(v & 0xFFFF);
            if (cp >= 0xD800 && cp < 0xDC00) { hi = cp; continue; }
            if (cp >= 0xDC00 && cp < 0xE000 && hi) { cp = 0x10000 + ((hi - 0xD800) << 10) + (cp - 0xDC00); hi = 0; }
            g_string_append_unichar(run, cp);
            continue;
        }
        if (*p == ' ') p++;
        if (!strcmp(w, "par")) g_string_append_c(run, '\n');
        else if (!strcmp(w, "line")) g_string_append_unichar(run, 0x2028);
        else if (!strcmp(w, "tab")) g_string_append_c(run, '\t');
        else { FLUSH();
            if (!strcmp(w, "pard")) li = 0;
            else if (!strcmp(w, "li")) li = (int)(v / 240);
            else if (!strcmp(w, "f")) f = (int)v;
            else if (!strcmp(w, "cf")) cf = (int)v;
            else if (!strcmp(w, "b")) b = has ? v != 0 : 1;
            else if (!strcmp(w, "i")) it = has ? v != 0 : 1;
        }
    }
    FLUSH();
    #undef FLUSH
    g_string_free(run, TRUE);
}
static void on_alt(GtkButton *bt, gpointer d) {
    (void)bt; if (!g_res) return;
    Req *rq = (Req *)calloc(1, sizeof(Req));
    rq->type = REQ_CAND; rq->ncand = g_res->ncand; rq->pick = GPOINTER_TO_INT(d);
    memcpy(rq->cand, g_res->cand, sizeof rq->cand);
    start_req(rq);
}
static void update_save_button(void) {
    gtk_button_set_label(GTK_BUTTON(g_bsave), g_res && g_res->saved ? "★ Salvata" : "☆ Salva");
    GtkStyleContext *sc = gtk_widget_get_style_context(g_bsave);
    if (g_res && g_res->saved) gtk_style_context_add_class(sc, "saved"); else gtk_style_context_remove_class(sc, "saved");
}
static void popup_result(Res *rs) {
    res_free(g_res); g_res = rs;
    if (g_close_timer) { g_source_remove(g_close_timer); g_close_timer = 0; }
    gtk_label_set_text(GTK_LABEL(g_title), rs->title);
    gtk_label_set_text(GTK_LABEL(g_doclabel), rs->label);
    update_save_button();
    GtkTextBuffer *tb = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_view));
    gtk_text_buffer_set_text(tb, "", 0);
    rtf_to_buffer(tb, rs->rtf);
    gtk_adjustment_set_value(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(g_scroll)), 0);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(g_scroll), 40);
    gtk_container_foreach(GTK_CONTAINER(g_foot), (GtkCallback)gtk_widget_destroy, NULL);
    if (rs->ncand > 1) {
        GtkWidget *l = gtk_label_new("Altri:"); gtk_widget_set_name(l, "sl-dim"); gtk_box_pack_start(GTK_BOX(g_foot), l, FALSE, FALSE, 4);
        int shown = 0;
        for (int i = 0; i < rs->ncand && shown < 3; i++) {
            if (i == rs->pick) continue;
            GtkWidget *bt = gtk_button_new_with_label(rs->cand[i].full);
            gtk_label_set_ellipsize(GTK_LABEL(gtk_bin_get_child(GTK_BIN(bt))), PANGO_ELLIPSIZE_START);
            gtk_button_set_relief(GTK_BUTTON(bt), GTK_RELIEF_NONE); gtk_style_context_add_class(gtk_widget_get_style_context(bt), "sl-btn");
            g_signal_connect(bt, "clicked", G_CALLBACK(on_alt), GINT_TO_POINTER(i));
            gtk_box_pack_start(GTK_BOX(g_foot), bt, TRUE, TRUE, 0); shown++;
        }
        gtk_widget_show_all(g_foot);
    } else gtk_widget_hide(g_foot);
    gtk_widget_hide(g_msg); gtk_widget_show(g_content);
    popup_show();
}
static gboolean on_status(gpointer p) {
    StatusMsg *m = (StatusMsg *)p;
    if (m->gen == g_gen) popup_message(m->text, 0);
    free(m->text); g_free(m);
    return G_SOURCE_REMOVE;
}
static gboolean on_result(gpointer p) {
    Res *rs = (Res *)p;
    if (rs->gen != g_gen) { res_free(rs); return G_SOURCE_REMOVE; }
    if (rs->ok) popup_result(rs);
    else { popup_message(rs->msg ? rs->msg : "Errore", 3500); res_free(rs); }
    return G_SOURCE_REMOVE;
}
static void on_save(GtkButton *b, gpointer d) {
    (void)b; (void)d; if (!g_res || !g_res->blob) return;
    if (g_res->saved) { unlink(g_res->save_path); g_res->saved = 0; }
    else if (write_file(g_res->save_path, g_res->blob, g_res->blob_len)) g_res->saved = 1;
    update_save_button();
}
static void on_open(GtkButton *b, gpointer d) { (void)b; (void)d; if (g_res) gtk_show_uri_on_window(NULL, g_res->url, GDK_CURRENT_TIME, NULL); }
static void on_close(GtkButton *b, gpointer d) { (void)b; (void)d; popup_close(); }
static GtkWidget *flat_button(const char *label, GCallback cb) {
    GtkWidget *b = gtk_button_new_with_label(label);
    gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
    gtk_style_context_add_class(gtk_widget_get_style_context(b), "sl-btn");
    g_signal_connect(b, "clicked", cb, NULL);
    return b;
}
static void hex(unsigned c, char *o) { snprintf(o, 8, "#%02x%02x%02x", SL_R(c), SL_G(c), SL_B(c)); }
static void build_popup(void) {
    char bg[8], head[8], text[8], dim[8], acc[8], code[8], rose[8], bord[8];
    hex(g_th.bg, bg); hex(g_th.head, head); hex(g_th.text, text); hex(g_th.dim, dim); hex(g_th.accent, acc); hex(g_th.code, code); hex(g_th.rose, rose); hex(g_th.border, bord);
    char *css = g_strdup_printf(
        "#sl-pop { background-color: %s; border: 1px solid %s; }"
        "#sl-head { background-color: %s; padding: 4px 8px 4px 12px; }"
        "#sl-title { color: %s; font-weight: bold; }"
        "#sl-dim { color: %s; }"
        "#sl-msg { color: %s; padding: 8px 14px; }"
        ".sl-btn { color: %s; background: none; border: none; box-shadow: none; padding: 2px 6px; min-height: 0; min-width: 0; }"
        ".sl-btn.saved { color: %s; }"
        "#sl-foot { padding: 2px 8px 4px 8px; }"
        "#sl-view, #sl-view text, #sl-scroll { background-color: %s; color: %s; }",
        bg, bord, head, text, dim, text, acc, rose, bg, text);
    GtkCssProvider *cp = gtk_css_provider_new();
    gtk_css_provider_load_from_data(cp, css, -1, NULL); g_free(css);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(cp), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

    g_pop = make_window(); gtk_widget_set_name(g_pop, "sl-pop");
    gtk_window_set_keep_above(GTK_WINDOW(g_pop), TRUE);
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); gtk_container_add(GTK_CONTAINER(g_pop), outer);
    g_msg = gtk_label_new(""); gtk_widget_set_name(g_msg, "sl-msg"); gtk_box_pack_start(GTK_BOX(outer), g_msg, FALSE, FALSE, 0);
    g_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); gtk_box_pack_start(GTK_BOX(outer), g_content, TRUE, TRUE, 0);
    gtk_widget_set_size_request(g_content, 500, -1);

    GtkWidget *hd = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8); gtk_widget_set_name(hd, "sl-head");
    g_title = gtk_label_new(""); gtk_widget_set_name(g_title, "sl-title"); gtk_label_set_ellipsize(GTK_LABEL(g_title), PANGO_ELLIPSIZE_END);
    g_doclabel = gtk_label_new(""); gtk_widget_set_name(g_doclabel, "sl-dim"); gtk_label_set_ellipsize(GTK_LABEL(g_doclabel), PANGO_ELLIPSIZE_END);
    GtkWidget *sp = gtk_label_new(""); gtk_widget_set_hexpand(sp, TRUE);
    g_bsave = flat_button("☆ Salva", G_CALLBACK(on_save));
    gtk_box_pack_start(GTK_BOX(hd), g_title, FALSE, TRUE, 0); gtk_box_pack_start(GTK_BOX(hd), g_doclabel, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hd), sp, TRUE, TRUE, 0); gtk_box_pack_start(GTK_BOX(hd), g_bsave, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hd), flat_button("Apri ↗", G_CALLBACK(on_open)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hd), flat_button("✕", G_CALLBACK(on_close)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(g_content), hd, FALSE, FALSE, 0);

    g_scroll = gtk_scrolled_window_new(NULL, NULL); gtk_widget_set_name(g_scroll, "sl-scroll");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(g_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(g_scroll), 40);
    g_view = gtk_text_view_new(); gtk_widget_set_name(g_view, "sl-view");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(g_view), FALSE); gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(g_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(g_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(g_view), 12); gtk_text_view_set_right_margin(GTK_TEXT_VIEW(g_view), 12);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(g_view), 8); gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(g_view), 8);
    gtk_text_view_set_pixels_below_lines(GTK_TEXT_VIEW(g_view), 6);
    GtkTextBuffer *tb = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_view));
    gtk_text_buffer_create_tag(tb, "mono", "family", "monospace", NULL);
    gtk_text_buffer_create_tag(tb, "code", "foreground", code, NULL);
    gtk_text_buffer_create_tag(tb, "sig", "foreground", acc, NULL);
    gtk_text_buffer_create_tag(tb, "dim", "foreground", dim, NULL);
    gtk_text_buffer_create_tag(tb, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
    gtk_text_buffer_create_tag(tb, "ital", "style", PANGO_STYLE_ITALIC, NULL);
    gtk_text_buffer_create_tag(tb, "li1", "left-margin", 12 + 18, NULL);
    gtk_text_buffer_create_tag(tb, "li2", "left-margin", 12 + 36, NULL);
    gtk_text_buffer_create_tag(tb, "li3", "left-margin", 12 + 54, NULL);
    gtk_container_add(GTK_CONTAINER(g_scroll), g_view);
    gtk_box_pack_start(GTK_BOX(g_content), g_scroll, TRUE, TRUE, 0);
    g_foot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2); gtk_widget_set_name(g_foot, "sl-foot");
    gtk_box_pack_start(GTK_BOX(g_content), g_foot, FALSE, FALSE, 0);
    gtk_widget_show_all(outer);
    gtk_widget_hide(g_content); gtk_widget_hide(g_foot);
}

/* ------------------------------------------------- selezione (PRIMARY) */
static void on_primary_text(GtkClipboard *cb, const gchar *text, gpointer d) {
    (void)cb; (void)d;
    if (!g_active || !text || !*text) return;
    GdkModifierType m; pointer_state(&g_ax, &g_ay, &m);
    Req *rq = (Req *)calloc(1, sizeof(Req)); rq->type = REQ_TEXT;
    snprintf(rq->text, sizeof rq->text, "%s", text);
    start_req(rq);
}
static gboolean trigger_cb(gpointer d) {
    (void)d; int x, y; GdkModifierType m; pointer_state(&x, &y, &m);
    if (m & GDK_BUTTON1_MASK) return G_SOURCE_CONTINUE;           /* selezione ancora in corso */
    g_trig_timer = 0;
    GtkClipboard *cb = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
    if (g_active && !gtk_clipboard_get_owner(cb)) gtk_clipboard_request_text(cb, on_primary_text, NULL);
    return G_SOURCE_REMOVE;
}
static void on_owner_change(GtkClipboard *cb, GdkEvent *ev, gpointer d) {
    (void)ev; (void)d;
    if (!g_active || gtk_clipboard_get_owner(cb)) return;        /* ignora le selezioni fatte dentro il riquadro */
    if (g_trig_timer) g_source_remove(g_trig_timer);
    g_trig_timer = g_timeout_add(180, trigger_cb, NULL);
}

/* ---------------------------------------------------------------------- icona */
static gboolean icon_draw(GtkWidget *w, cairo_t *cr, gpointer d) {
    (void)w; (void)d;
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE); cairo_set_source_rgba(cr, 0, 0, 0, 0); cairo_paint(cr);
    for (int y = 0; y < 24; y++) for (int x = 0; x < 24; x++) {
        char c = SQ[y][x < 12 ? x : 23 - x]; if (c == '.') continue;
        unsigned col = sq_color(c, g_active);
        cairo_set_source_rgb(cr, ((col >> 16) & 255) / 255.0, ((col >> 8) & 255) / 255.0, (col & 255) / 255.0);
        cairo_rectangle(cr, x * ICON_K, y * ICON_K, ICON_K, ICON_K); cairo_fill(cr);
    }
    return TRUE;
}
static void set_active(int on) {
    g_active = on; gtk_widget_queue_draw(g_icon);
    if (on) g_thread_unref(g_thread_new("warm", warm_thread, NULL));
}
static char *autostart_path(void) { return g_build_filename(g_get_user_config_dir(), "autostart", "smartylens.desktop", NULL); }
static void on_menu_toggle(GtkMenuItem *i, gpointer d) { (void)i; (void)d; set_active(!g_active); }
static void on_menu_folder(GtkMenuItem *i, gpointer d) { (void)i; char *u = g_filename_to_uri((const char *)d, NULL, NULL); if (u) { gtk_show_uri_on_window(NULL, u, GDK_CURRENT_TIME, NULL); g_free(u); } }
static void on_menu_autostart(GtkMenuItem *i, gpointer d) {
    (void)i; (void)d; char *p = autostart_path();
    if (!access(p, F_OK)) unlink(p);
    else {
        char *dir = g_path_get_dirname(p); g_mkdir_with_parents(dir, 0755); g_free(dir);
        char *t = g_strdup_printf("[Desktop Entry]\nType=Application\nName=Smarty Lens\nExec=\"%s\"\nX-GNOME-Autostart-enabled=true\n", g_exe);
        write_file(p, t, strlen(t)); g_free(t);
    }
    g_free(p);
}
static void on_menu_saved(GtkMenuItem *i, gpointer d) {
    (void)d; const char *file = (const char *)g_object_get_data(G_OBJECT(i), "file");
    Req *rq = (Req *)calloc(1, sizeof(Req)); rq->type = REQ_SAVED; snprintf(rq->file, sizeof rq->file, "%s", file);
    int x, y; gtk_window_get_position(GTK_WINDOW(g_icon), &x, &y); g_ax = x - 520; g_ay = y;
    start_req(rq);
}
static void show_menu(GdkEvent *ev) {
    GtkWidget *m = gtk_menu_new(), *sub = gtk_menu_new(), *it;
    it = gtk_check_menu_item_new_with_label("Evidenziazione attiva"); gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(it), g_active);
    g_signal_connect(it, "activate", G_CALLBACK(on_menu_toggle), NULL); gtk_menu_shell_append(GTK_MENU_SHELL(m), it);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    GDir *dir = g_dir_open(g_savedir, 0, NULL); int n = 0; const char *name;
    while (dir && (name = g_dir_read_name(dir)) && n < 60) {
        if (!g_str_has_suffix(name, ".html")) continue;
        char *fp = g_build_filename(g_savedir, name, NULL); size_t len; char *b = read_file(fp, &len);
        if (b && len > 8 && !memcmp(b, "LENTE1\n", 7)) {
            char *nl = strchr(b + 7, '\n'); if (nl) *nl = 0;
            it = gtk_menu_item_new_with_label(b + 7);
            g_object_set_data_full(G_OBJECT(it), "file", g_strdup(fp), g_free);
            g_signal_connect(it, "activate", G_CALLBACK(on_menu_saved), NULL); gtk_menu_shell_append(GTK_MENU_SHELL(sub), it); n++;
        }
        free(b); g_free(fp);
    }
    if (dir) g_dir_close(dir);
    if (!n) { it = gtk_menu_item_new_with_label("(nessuna)"); gtk_widget_set_sensitive(it, FALSE); gtk_menu_shell_append(GTK_MENU_SHELL(sub), it); }
    it = gtk_menu_item_new_with_label("Funzioni salvate"); gtk_menu_item_set_submenu(GTK_MENU_ITEM(it), sub); gtk_menu_shell_append(GTK_MENU_SHELL(m), it);
    it = gtk_menu_item_new_with_label("Apri cartella dati"); g_signal_connect(it, "activate", G_CALLBACK(on_menu_folder), g_data); gtk_menu_shell_append(GTK_MENU_SHELL(m), it);
    it = gtk_menu_item_new_with_label("Apri configurazione"); g_signal_connect(it, "activate", G_CALLBACK(on_menu_folder), g_ini); gtk_menu_shell_append(GTK_MENU_SHELL(m), it);
    char *ap = autostart_path();
    it = gtk_check_menu_item_new_with_label("Avvia all'accesso"); gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(it), !access(ap, F_OK)); g_free(ap);
    g_signal_connect(it, "activate", G_CALLBACK(on_menu_autostart), NULL); gtk_menu_shell_append(GTK_MENU_SHELL(m), it);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    it = gtk_menu_item_new_with_label("Esci"); g_signal_connect(it, "activate", G_CALLBACK(gtk_main_quit), NULL); gtk_menu_shell_append(GTK_MENU_SHELL(m), it);
    gtk_widget_show_all(m);
    gtk_menu_popup_at_pointer(GTK_MENU(m), ev);
}
static int g_drag, g_moved, g_dx, g_dy, g_ox, g_oy;
static gboolean icon_press(GtkWidget *w, GdkEventButton *e, gpointer d) {
    (void)d;
    if (e->button == 3) { show_menu((GdkEvent *)e); return TRUE; }
    if (e->button == 1) { g_drag = 1; g_moved = 0; g_dx = (int)e->x_root; g_dy = (int)e->y_root; gtk_window_get_position(GTK_WINDOW(w), &g_ox, &g_oy); }
    return TRUE;
}
static gboolean icon_motion(GtkWidget *w, GdkEventMotion *e, gpointer d) {
    (void)d; if (!g_drag) return FALSE;
    int mx = (int)e->x_root - g_dx, my = (int)e->y_root - g_dy;
    if (g_moved || abs(mx) > 4 || abs(my) > 4) { g_moved = 1; gtk_window_move(GTK_WINDOW(w), g_ox + mx, g_oy + my); }
    return TRUE;
}
static gboolean icon_release(GtkWidget *w, GdkEventButton *e, gpointer d) {
    (void)d; if (e->button != 1 || !g_drag) return FALSE;
    g_drag = 0;
    if (!g_moved) set_active(!g_active);
    else { int x, y; gtk_window_get_position(GTK_WINDOW(w), &x, &y); ini_set_int("x", x); ini_set_int("y", y); }
    return TRUE;
}
static void build_icon(void) {
    int N = 24 * ICON_K;
    g_icon = make_window();
    gtk_window_set_default_size(GTK_WINDOW(g_icon), N, N); gtk_widget_set_size_request(g_icon, N, N);
    gtk_window_set_keep_above(GTK_WINDOW(g_icon), TRUE);
    gtk_widget_set_app_paintable(g_icon, TRUE);
    GdkScreen *sc = gdk_screen_get_default(); GdkVisual *vis = gdk_screen_get_rgba_visual(sc);
    if (vis) gtk_widget_set_visual(g_icon, vis);
    gtk_widget_add_events(g_icon, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
    g_signal_connect(g_icon, "draw", G_CALLBACK(icon_draw), NULL);
    g_signal_connect(g_icon, "button-press-event", G_CALLBACK(icon_press), NULL);
    g_signal_connect(g_icon, "motion-notify-event", G_CALLBACK(icon_motion), NULL);
    g_signal_connect(g_icon, "button-release-event", G_CALLBACK(icon_release), NULL);
    cairo_region_t *rg = cairo_region_create();                    /* sagoma: solo i pixel disegnati */
    for (int y = 0; y < 24; y++) for (int x = 0; x < 24; x++) if (SQ[y][x < 12 ? x : 23 - x] != '.') {
        cairo_rectangle_int_t r = { x * ICON_K, y * ICON_K, ICON_K, ICON_K }; cairo_region_union_rectangle(rg, &r);
    }
    gtk_widget_shape_combine_region(g_icon, rg); gtk_widget_input_shape_combine_region(g_icon, rg);
    cairo_region_destroy(rg);
    int x = ini_int("x", -1), y = ini_int("y", -1);
    GdkRectangle wa; gdk_monitor_get_workarea(gdk_display_get_primary_monitor(gdk_display_get_default()) ? gdk_display_get_primary_monitor(gdk_display_get_default()) : gdk_display_get_monitor(gdk_display_get_default(), 0), &wa);
    GdkRectangle mg; gdk_monitor_get_geometry(gdk_display_get_monitor_at_point(gdk_display_get_default(), x, y), &mg);
    if (x < mg.x || y < mg.y || x + N > mg.x + mg.width || y + N > mg.y + mg.height) {   /* posizione assente o fuori schermo */ x = wa.x + wa.width - N - 28; y = wa.y + wa.height / 3; }
    gtk_window_move(GTK_WINDOW(g_icon), x, y);
    gtk_widget_show(g_icon);
}

int main(int argc, char **argv) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    init_paths();
    if (argc >= 3 && !strcmp(argv[1], "--test")) return run_test(argv[2]);
    if (!single_instance()) return 0;                              /* gia' in esecuzione */
    gdk_set_allowed_backends("x11,*");                             /* su Wayland usa XWayland: serve la selezione PRIMARY di X11 */
    gtk_init(&argc, &argv);
    g_ui = 1;
    gboolean prefer = FALSE; char *theme = NULL;
    g_object_get(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", &prefer, "gtk-theme-name", &theme, NULL);
    char *lower = theme ? g_ascii_strdown(theme, -1) : NULL;
    g_dark = prefer || (lower && strstr(lower, "dark")); g_free(lower); g_free(theme);
    const char *mg = getenv("SMARTYLENS_MANAGED");
    g_managed = mg ? atoi(mg) : getenv("WSL_DISTRO_NAME") != NULL;
    get_theme(&g_th);
    build_popup(); build_icon();
    g_signal_connect(gtk_clipboard_get(GDK_SELECTION_PRIMARY), "owner-change", G_CALLBACK(on_owner_change), NULL);
    if (ini_int("attivo_all_avvio", 0)) set_active(1);
    if (argc >= 3 && !strcmp(argv[1], "--cerca")) {               /* apre subito il riquadro per un nome */
        GdkRectangle wa; gdk_monitor_get_workarea(gdk_display_get_monitor(gdk_display_get_default(), 0), &wa);
        g_ax = wa.x + wa.width / 2 - 250; g_ay = wa.y + wa.height / 4;
        Req *rq = (Req *)calloc(1, sizeof(Req)); rq->type = REQ_TEXT; snprintf(rq->text, sizeof rq->text, "%s", argv[2]);
        start_req(rq);
    }
    gtk_main();
    return 0;
}
