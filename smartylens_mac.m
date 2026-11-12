/* Smarty Lens - versione macOS (Cocoa + libcurl).
 * Il testo evidenziato si legge con le API di Accessibilita' (senza toccare gli appunti); nelle
 * applicazioni che non le espongono si ripiega su Cmd+C simulato, ripristinando poi gli appunti.
 * Entrambe richiedono il permesso "Accessibilita'" in Impostazioni di Sistema.
 * Parti comuni: smartylens_core.h e smartylens_posix.h. Compilazione: vedi build_mac.sh
 */
#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>
#define SL_FONT_UI "Helvetica Neue"
#define SL_FONT_MONO "Menlo"
#define SL_FONT_HALFPT 25
#include "smartylens_core.h"
#include "smartylens_posix.h"

#define ICON_K 2
#define HEAD_H 34.0
#define POP_W 520.0

static int g_active, g_pop_vis, g_ui, g_only_ibeam = 1;
static unsigned g_gen, g_close_gen;
static Res *g_res;
static NSPoint g_anchor;
static Theme g_th;
static NSPanel *g_icon, *g_pop;
static NSView *g_iconView, *g_popView, *g_foot;
static NSTextField *g_title, *g_doc, *g_msg;
static NSButton *g_bsave, *g_bopen, *g_bclose;
static NSScrollView *g_scroll;
static NSTextView *g_text;
static id g_ctl;

static NSColor *col(unsigned c) { return [NSColor colorWithSRGBRed:SL_R(c) / 255.0 green:SL_G(c) / 255.0 blue:SL_B(c) / 255.0 alpha:1.0]; }
static NSString *u8(const char *s) { NSString *r = s ? [NSString stringWithUTF8String:s] : nil; return r ? r : @""; }
static void set_btn(NSButton *b, NSString *title, unsigned color) {
    b.attributedTitle = [[NSAttributedString alloc] initWithString:title attributes:@{ NSForegroundColorAttributeName: col(color), NSFontAttributeName: [NSFont systemFontOfSize:13] }];
    [b sizeToFit];
}

/* ------------------------------------------------------------------- riquadro */
static void popup_close(void) {
    if (!g_pop) return;
    [g_pop orderOut:nil];
    res_free(g_res); g_res = NULL; g_pop_vis = 0;
    g_gen++; g_close_gen++;                          /* scarta risposte e chiusure automatiche in sospeso */
}
static void popup_place(CGFloat w, CGFloat h) {
    NSScreen *sc = [NSScreen mainScreen];
    for (NSScreen *s in [NSScreen screens]) if (NSPointInRect(g_anchor, s.frame)) sc = s;
    NSRect vf = sc.visibleFrame;
    CGFloat x = g_anchor.x + 12, y = g_anchor.y - 18 - h;      /* coordinate Cocoa: origine in basso a sinistra */
    if (y < NSMinY(vf)) y = g_anchor.y + 14;
    if (y + h > NSMaxY(vf)) y = NSMaxY(vf) - h - 4;
    if (x + w > NSMaxX(vf)) x = NSMaxX(vf) - w - 4;
    if (x < NSMinX(vf)) x = NSMinX(vf) + 4;
    [g_pop setFrame:NSMakeRect(x, y, w, h) display:YES];
    [g_popView setNeedsDisplay:YES];
    [g_pop orderFrontRegardless];
    g_pop_vis = 1;
}
static void popup_message(NSString *text, double autoclose_s) {
    res_free(g_res); g_res = NULL;
    g_msg.stringValue = text; [g_msg sizeToFit];
    CGFloat w = g_msg.frame.size.width + 28; if (w < 140) w = 140; if (w > 560) w = 560;
    g_msg.frame = NSMakeRect(14, 8, w - 28, 18);
    for (NSView *v in @[ g_title, g_doc, g_bsave, g_bopen, g_bclose, g_scroll, g_foot ]) v.hidden = YES;
    g_msg.hidden = NO;
    popup_place(w, 34);
    unsigned my = ++g_close_gen;
    if (autoclose_s > 0)
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(autoclose_s * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ if (my == g_close_gen) popup_close(); });
}
static void update_save_button(void) {
    BOOL s = g_res && g_res->saved;
    set_btn(g_bsave, s ? @"★ Salvata" : @"☆ Salva", s ? g_th.rose : g_th.accent);
}
static void layout_header(void) {
    CGFloat x = POP_W - 10;
    for (NSButton *b in @[ g_bclose, g_bopen, g_bsave ]) {
        NSSize s = b.frame.size; x -= s.width;
        b.frame = NSMakeRect(x, (HEAD_H - s.height) / 2, s.width, s.height); x -= 10;
    }
    [g_title sizeToFit]; [g_doc sizeToFit];
    CGFloat tw = MIN(g_title.frame.size.width, x - 12 - 60);
    g_title.frame = NSMakeRect(12, (HEAD_H - 18) / 2, tw, 18);
    CGFloat dx = 12 + tw + 8, dw = MIN(g_doc.frame.size.width, x - dx);
    g_doc.frame = NSMakeRect(dx, (HEAD_H - 18) / 2, dw > 0 ? dw : 0, 18);
    g_doc.hidden = dw < 30;
}
static void popup_result(Res *rs) {
    res_free(g_res); g_res = rs; g_close_gen++;
    g_title.stringValue = u8(rs->title); g_doc.stringValue = u8(rs->label);
    update_save_button();
    NSAttributedString *as = [[NSAttributedString alloc] initWithRTF:[NSData dataWithBytes:rs->rtf length:strlen(rs->rtf)] documentAttributes:nil];
    if (!as) as = [[NSAttributedString alloc] initWithString:@"(testo non leggibile)"];
    g_scroll.frame = NSMakeRect(1, HEAD_H, POP_W - 2, 100);
    g_text.frame = NSMakeRect(0, 0, g_scroll.contentSize.width, 100);
    [g_text.textStorage setAttributedString:as];
    [g_text.layoutManager ensureLayoutForTextContainer:g_text.textContainer];
    CGFloat th = [g_text.layoutManager usedRectForTextContainer:g_text.textContainer].size.height + 2 * g_text.textContainerInset.height;
    CGFloat bh = MIN(MAX(th, 30), 380);
    for (NSView *v in [g_foot.subviews copy]) [v removeFromSuperview];
    CGFloat fh = 6;
    if (rs->ncand > 1) {                                            /* altre corrispondenze */
        fh = 30;
        NSTextField *l = [NSTextField labelWithString:@"Altri:"]; l.textColor = col(g_th.dim); l.font = [NSFont systemFontOfSize:13]; [l sizeToFit];
        l.frame = NSMakeRect(12, 6, l.frame.size.width, 18); [g_foot addSubview:l];
        CGFloat ax = 12 + l.frame.size.width + 8;
        for (int i = 0; i < rs->ncand; i++) {
            if (i == rs->pick) continue;
            const char *f = rs->cand[i].full; size_t fl = strlen(f);
            NSString *t = fl > 28 ? [@"…" stringByAppendingString:u8(f + fl - 27)] : u8(f);
            NSButton *b = [NSButton buttonWithTitle:@"" target:g_ctl action:@selector(alt:)];
            b.bordered = NO; b.tag = i; set_btn(b, t, g_th.accent);
            if (ax + b.frame.size.width > POP_W - 10) break;
            b.frame = NSMakeRect(ax, (30 - b.frame.size.height) / 2, b.frame.size.width, b.frame.size.height);
            [g_foot addSubview:b]; ax += b.frame.size.width + 12;
        }
    }
    layout_header();
    g_scroll.frame = NSMakeRect(1, HEAD_H, POP_W - 2, bh);
    g_foot.frame = NSMakeRect(1, HEAD_H + bh, POP_W - 2, fh - 1);
    [g_text scrollPoint:NSZeroPoint];
    for (NSView *v in @[ g_title, g_bsave, g_bopen, g_bclose, g_scroll, g_foot ]) v.hidden = NO;
    g_msg.hidden = YES;
    popup_place(POP_W, HEAD_H + bh + fh);
    const char *st = getenv("SMARTYLENS_SELFTEST");                /* usato dalle prove automatiche */
    if (st) { FILE *f = fopen(st, "w"); if (f) { fprintf(f, "riquadro %dx%d titolo=%s\n", (int)POP_W, (int)(HEAD_H + bh + fh), rs->title); fclose(f); } }
}
static void plat_status(unsigned gen, char *text) {
    if (!g_ui) { free(text); return; }
    dispatch_async(dispatch_get_main_queue(), ^{ if (gen == g_gen) popup_message(u8(text), 0); free(text); });
}
static void deliver(Res *rs) {
    if (!rs) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (rs->gen != g_gen) { res_free(rs); return; }
        if (rs->ok) popup_result(rs);
        else { popup_message(rs->msg ? u8(rs->msg) : @"Errore", 3.5); res_free(rs); }
    });
}
static void start_req(Req *rq) {
    rq->gen = ++g_gen;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{ deliver(run_request(rq)); });
}

/* ------------------------------------------- cattura del testo selezionato */
static NSString *ax_selected_text(void) {
    NSString *out = nil; AXUIElementRef sys = AXUIElementCreateSystemWide(); CFTypeRef foc = NULL, val = NULL;
    if (AXUIElementCopyAttributeValue(sys, kAXFocusedUIElementAttribute, &foc) == kAXErrorSuccess && foc) {
        if (AXUIElementCopyAttributeValue((AXUIElementRef)foc, kAXSelectedTextAttribute, &val) == kAXErrorSuccess && val) {
            if (CFGetTypeID(val) == CFStringGetTypeID()) out = [(__bridge NSString *)val copy];
            CFRelease(val);
        }
        CFRelease(foc);
    }
    CFRelease(sys);
    return out;
}
/* Copia la selezione con Cmd+C simulato, la legge e ripristina gli appunti di prima. */
static NSString *copy_selected_text(void) {
    NSPasteboard *pb = [NSPasteboard generalPasteboard]; NSInteger cc = pb.changeCount;
    NSMutableArray *saved = [NSMutableArray array];
    for (NSPasteboardItem *it in pb.pasteboardItems) {
        NSPasteboardItem *n = [[NSPasteboardItem alloc] init];
        for (NSPasteboardType t in it.types) { NSData *d = [it dataForType:t]; if (d) [n setData:d forType:t]; }
        [saved addObject:n];
    }
    CGEventSourceRef src = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    CGEventRef dn = CGEventCreateKeyboardEvent(src, 8, true), up = CGEventCreateKeyboardEvent(src, 8, false);   /* 8 = tasto C */
    CGEventSetFlags(dn, kCGEventFlagMaskCommand); CGEventSetFlags(up, kCGEventFlagMaskCommand);
    CGEventPost(kCGHIDEventTap, dn); CGEventPost(kCGHIDEventTap, up);
    CFRelease(dn); CFRelease(up); if (src) CFRelease(src);
    BOOL changed = NO;
    for (int i = 0; i < 40 && !changed; i++) { usleep(15000); changed = pb.changeCount != cc; }
    if (!changed) return nil;
    usleep(40000);
    NSString *text = [pb stringForType:NSPasteboardTypeString];
    [pb clearContents];
    if (saved.count) [pb writeObjects:saved];
    return text;
}
static BOOL cursor_ibeam(void) {
    NSCursor *c = [NSCursor currentSystemCursor], *ib = [NSCursor IBeamCursor];
    if (!c) return YES;
    return NSEqualPoints(c.hotSpot, ib.hotSpot) && NSEqualSizes(c.image.size, ib.image.size);
}
static BOOL ensure_trusted(BOOL prompt) {
    NSDictionary *o = @{ (__bridge NSString *)kAXTrustedCheckOptionPrompt: @(prompt) };
    return AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)o);
}
static void trigger(BOOL ibeam) {
    if (!g_active) return;
    if ([NSWorkspace sharedWorkspace].frontmostApplication.processIdentifier == getpid()) return;
    g_anchor = [NSEvent mouseLocation];
    if (!ensure_trusted(NO)) { popup_message(@"Manca il permesso Accessibilità: Impostazioni di Sistema → Privacy e sicurezza → Accessibilità", 6); return; }
    unsigned gen = ++g_gen;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        usleep(60000);
        NSString *text = ax_selected_text();
        if (!text.length && (ibeam || !g_only_ibeam)) text = copy_selected_text();
        if (!text.length || gen != g_gen) return;
        Req *rq = (Req *)calloc(1, sizeof(Req)); rq->type = REQ_TEXT; rq->gen = gen;
        snprintf(rq->text, sizeof rq->text, "%s", text.UTF8String);
        deliver(run_request(rq));
    });
}

/* ---------------------------------------------------------------- finestre */
@interface SLPanel : NSPanel
@end
@implementation SLPanel
- (BOOL)canBecomeKeyWindow { return NO; }
- (BOOL)canBecomeMainWindow { return NO; }
@end

@interface SLPopView : NSView
@end
@implementation SLPopView
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)r {
    [col(g_th.bg) setFill]; NSRectFill(self.bounds);
    if (g_res) { [col(g_th.head) setFill]; NSRectFill(NSMakeRect(0, 0, self.bounds.size.width, HEAD_H)); }
    [col(g_th.border) setFill]; NSFrameRect(self.bounds);
}
@end

@interface SLFootView : NSView
@end
@implementation SLFootView
- (BOOL)isFlipped { return YES; }
@end

@interface SLIconView : NSView {
    NSPoint down, origin; BOOL moved;
}
@end
@implementation SLIconView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }
- (void)drawRect:(NSRect)r {
    for (int y = 0; y < 24; y++) for (int x = 0; x < 24; x++) {
        char c = SQ[y][x < 12 ? x : 23 - x]; if (c == '.') continue;
        unsigned k = sq_color(c, g_active);
        [[NSColor colorWithSRGBRed:((k >> 16) & 255) / 255.0 green:((k >> 8) & 255) / 255.0 blue:(k & 255) / 255.0 alpha:1.0] setFill];
        NSRectFill(NSMakeRect(x * ICON_K, y * ICON_K, ICON_K, ICON_K));
    }
}
- (void)mouseDown:(NSEvent *)e { down = [NSEvent mouseLocation]; origin = self.window.frame.origin; moved = NO; }
- (void)mouseDragged:(NSEvent *)e {
    NSPoint p = [NSEvent mouseLocation]; CGFloat dx = p.x - down.x, dy = p.y - down.y;
    if (moved || fabs(dx) > 4 || fabs(dy) > 4) { moved = YES; [self.window setFrameOrigin:NSMakePoint(origin.x + dx, origin.y + dy)]; }
}
- (void)mouseUp:(NSEvent *)e {
    if (!moved) [g_ctl performSelector:@selector(toggle:) withObject:nil];
    else { NSPoint o = self.window.frame.origin; ini_set_int("x", (int)o.x); ini_set_int("y", (int)o.y); }
}
- (void)rightMouseDown:(NSEvent *)e { [g_ctl performSelector:@selector(showMenu:) withObject:e]; }
@end

@interface SLCtl : NSObject
@end
@implementation SLCtl
- (void)toggle:(id)s {
    g_active = !g_active; [g_iconView setNeedsDisplay:YES];
    if (g_active) {
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{ ensure_docs(0); });
        if (!ensure_trusted(YES)) {
            g_anchor = [NSEvent mouseLocation];
            popup_message(@"Concedi a Smarty Lens il permesso Accessibilità nella finestra appena aperta, poi evidenzia una funzione", 7);
        }
    }
}
- (void)save:(id)s {
    if (!g_res || !g_res->blob) return;
    if (g_res->saved) { unlink(g_res->save_path); g_res->saved = 0; }
    else if (write_file(g_res->save_path, g_res->blob, g_res->blob_len)) g_res->saved = 1;
    update_save_button(); layout_header();
}
- (void)open:(id)s { if (g_res) { NSURL *u = [NSURL URLWithString:u8(g_res->url)]; if (u) [[NSWorkspace sharedWorkspace] openURL:u]; } }
- (void)closePop:(id)s { popup_close(); }
- (void)alt:(NSButton *)b {
    if (!g_res) return;
    Req *rq = (Req *)calloc(1, sizeof(Req));
    rq->type = REQ_CAND; rq->ncand = g_res->ncand; rq->pick = (int)b.tag;
    memcpy(rq->cand, g_res->cand, sizeof rq->cand);
    start_req(rq);
}
- (void)openData:(id)s { [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:u8(g_data)]]; }
- (void)openConfig:(id)s { [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:u8(g_ini)]]; }
- (NSString *)agentPath { return [NSHomeDirectory() stringByAppendingPathComponent:@"Library/LaunchAgents/io.github.smartylens.plist"]; }
- (void)login:(id)s {
    NSString *p = [self agentPath]; NSFileManager *fm = [NSFileManager defaultManager];
    if ([fm fileExistsAtPath:p]) { [fm removeItemAtPath:p error:nil]; return; }
    [fm createDirectoryAtPath:[p stringByDeletingLastPathComponent] withIntermediateDirectories:YES attributes:nil error:nil];
    NSDictionary *d = @{ @"Label": @"io.github.smartylens", @"ProgramArguments": @[ [NSBundle mainBundle].executablePath ], @"RunAtLoad": @YES };
    [d writeToFile:p atomically:YES];
}
- (void)savedItem:(NSMenuItem *)it {
    Req *rq = (Req *)calloc(1, sizeof(Req)); rq->type = REQ_SAVED;
    snprintf(rq->file, sizeof rq->file, "%s", [it.representedObject UTF8String]);
    NSRect f = g_icon.frame; g_anchor = NSMakePoint(f.origin.x - POP_W - 20, f.origin.y + f.size.height);
    start_req(rq);
}
- (void)quit:(id)s { [NSApp terminate:nil]; }
- (NSMenuItem *)item:(NSString *)t action:(SEL)a menu:(NSMenu *)m {
    NSMenuItem *it = [m addItemWithTitle:t action:a keyEquivalent:@""]; it.target = self; return it;
}
- (void)showMenu:(NSEvent *)e {
    NSMenu *m = [[NSMenu alloc] initWithTitle:@""], *sub = [[NSMenu alloc] initWithTitle:@""];
    m.autoenablesItems = NO; sub.autoenablesItems = NO;
    [self item:@"Evidenziazione attiva" action:@selector(toggle:) menu:m].state = g_active ? NSControlStateValueOn : NSControlStateValueOff;
    [m addItem:[NSMenuItem separatorItem]];
    NSString *dir = u8(g_savedir); int n = 0;
    for (NSString *name in [[NSFileManager defaultManager] contentsOfDirectoryAtPath:dir error:nil]) {
        if (![name hasSuffix:@".html"] || n >= 60) continue;
        NSString *fp = [dir stringByAppendingPathComponent:name]; size_t len; char *b = read_file(fp.UTF8String, &len);
        if (b && len > 8 && !memcmp(b, "LENTE1\n", 7)) {
            char *nl = strchr(b + 7, '\n'); if (nl) *nl = 0;
            [self item:u8(b + 7) action:@selector(savedItem:) menu:sub].representedObject = fp; n++;
        }
        free(b);
    }
    if (!n) [sub addItemWithTitle:@"(nessuna)" action:nil keyEquivalent:@""].enabled = NO;
    NSMenuItem *sv = [m addItemWithTitle:@"Funzioni salvate" action:nil keyEquivalent:@""]; sv.submenu = sub;
    [self item:@"Apri cartella dati" action:@selector(openData:) menu:m];
    [self item:@"Apri configurazione" action:@selector(openConfig:) menu:m];
    [self item:@"Avvia all'accesso" action:@selector(login:) menu:m].state =
        [[NSFileManager defaultManager] fileExistsAtPath:[self agentPath]] ? NSControlStateValueOn : NSControlStateValueOff;
    [m addItem:[NSMenuItem separatorItem]];
    [self item:@"Esci" action:@selector(quit:) menu:m];
    [NSMenu popUpContextMenu:m withEvent:e forView:g_iconView];
}
@end

static NSPanel *make_panel(NSRect r) {
    SLPanel *p = [[SLPanel alloc] initWithContentRect:r styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                              backing:NSBackingStoreBuffered defer:NO];
    p.level = NSStatusWindowLevel; p.opaque = NO; p.backgroundColor = [NSColor clearColor];
    p.hidesOnDeactivate = NO; p.floatingPanel = YES; p.releasedWhenClosed = NO;
    p.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorStationary | NSWindowCollectionBehaviorFullScreenAuxiliary;
    return p;
}
static NSTextField *make_label(NSFont *f, unsigned color) {
    NSTextField *l = [NSTextField labelWithString:@""];
    l.font = f; l.textColor = col(color); l.lineBreakMode = NSLineBreakByTruncatingTail;
    return l;
}
static NSButton *make_button(SEL a) {
    NSButton *b = [NSButton buttonWithTitle:@"" target:g_ctl action:a]; b.bordered = NO; return b;
}
static void build_ui(void) {
    g_pop = make_panel(NSMakeRect(0, 0, POP_W, 100)); g_pop.hasShadow = YES;
    g_popView = [[SLPopView alloc] initWithFrame:NSMakeRect(0, 0, POP_W, 100)];
    g_popView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    g_pop.contentView = g_popView;
    g_msg = make_label([NSFont systemFontOfSize:13], g_th.text);
    g_title = make_label([NSFont boldSystemFontOfSize:13], g_th.text);
    g_doc = make_label([NSFont systemFontOfSize:13], g_th.dim);
    g_bsave = make_button(@selector(save:)); g_bopen = make_button(@selector(open:)); g_bclose = make_button(@selector(closePop:));
    set_btn(g_bopen, @"Apri ↗", g_th.accent); set_btn(g_bclose, @"✕", g_th.dim); set_btn(g_bsave, @"☆ Salva", g_th.accent);
    g_scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(1, HEAD_H, POP_W - 2, 100)];
    g_scroll.hasVerticalScroller = YES; g_scroll.hasHorizontalScroller = NO; g_scroll.borderType = NSNoBorder;
    g_scroll.drawsBackground = YES; g_scroll.backgroundColor = col(g_th.bg);
    g_text = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, g_scroll.contentSize.width, 100)];
    g_text.editable = NO; g_text.selectable = YES; g_text.richText = YES;
    g_text.drawsBackground = YES; g_text.backgroundColor = col(g_th.bg);
    g_text.textContainerInset = NSMakeSize(8, 8);
    g_text.verticallyResizable = YES; g_text.horizontallyResizable = NO;
    g_text.minSize = NSMakeSize(0, 0); g_text.maxSize = NSMakeSize(FLT_MAX, FLT_MAX);
    g_text.autoresizingMask = NSViewWidthSizable;
    g_text.textContainer.widthTracksTextView = YES;
    g_scroll.documentView = g_text;
    g_foot = [[SLFootView alloc] initWithFrame:NSMakeRect(1, 0, POP_W - 2, 30)];
    for (NSView *v in @[ g_msg, g_title, g_doc, g_bsave, g_bopen, g_bclose, g_scroll, g_foot ]) [g_popView addSubview:v];

    CGFloat N = 24 * ICON_K;
    NSRect vf = [NSScreen mainScreen].visibleFrame;
    CGFloat x = ini_int("x", -1), y = ini_int("y", -1); BOOL ok = NO;
    for (NSScreen *s in [NSScreen screens]) if (NSPointInRect(NSMakePoint(x + N / 2, y + N / 2), s.frame)) ok = YES;
    if (!ok) { x = NSMaxX(vf) - N - 28; y = NSMinY(vf) + vf.size.height * 2 / 3; }
    g_icon = make_panel(NSMakeRect(x, y, N, N));
    g_iconView = [[SLIconView alloc] initWithFrame:NSMakeRect(0, 0, N, N)];
    g_icon.contentView = g_iconView;
    [g_icon orderFrontRegardless];
}
static void install_monitors(void) {
    static NSPoint dpt, upt; static NSTimeInterval uptime; static BOOL dibeam;
    [NSEvent addGlobalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp | NSEventMaskScrollWheel handler:^(NSEvent *e) {
        NSPoint p = [NSEvent mouseLocation];
        if (e.type == NSEventTypeLeftMouseDown) {                 /* un clic in un'altra applicazione chiude il riquadro */
            dpt = p; dibeam = cursor_ibeam();
            if (g_pop_vis) popup_close();
        } else if (e.type == NSEventTypeScrollWheel) {
            if (g_pop_vis && !NSPointInRect(p, g_pop.frame)) popup_close();
        } else if (g_active) {
            CGFloat dx = p.x - dpt.x, dy = p.y - dpt.y;
            BOOL drag = dx * dx + dy * dy > 36;
            BOOL dbl = e.clickCount >= 2 || (e.timestamp - uptime < [NSEvent doubleClickInterval] && fabs(p.x - upt.x) < 4 && fabs(p.y - upt.y) < 4);
            upt = p; uptime = e.timestamp;
            BOOL ib = dibeam || cursor_ibeam();
            if (drag || dbl)
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.12 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ trigger(ib); });
        }
    }];
}

int main(int argc, char **argv) {
    @autoreleasepool {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        NSString *sup = [NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES) firstObject];
        init_data([[sup stringByAppendingPathComponent:@"SmartyLens"] fileSystemRepresentation]);
        if (argc >= 3 && !strcmp(argv[1], "--test")) return run_test(argv[2]);
        if (!single_instance()) return 0;                          /* gia' in esecuzione */
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        g_ui = 1;
        g_dark = [[[NSUserDefaults standardUserDefaults] stringForKey:@"AppleInterfaceStyle"] isEqualToString:@"Dark"];
        g_only_ibeam = ini_int("solo_cursore_testo", 1);
        get_theme(&g_th);
        g_ctl = [[SLCtl alloc] init];
        build_ui();
        install_monitors();
        if (ini_int("attivo_all_avvio", 0)) [g_ctl toggle:nil];
        if (argc >= 3 && !strcmp(argv[1], "--cerca")) {           /* apre subito il riquadro per un nome */
            NSRect vf = [NSScreen mainScreen].visibleFrame;
            g_anchor = NSMakePoint(NSMidX(vf) - POP_W / 2, NSMinY(vf) + vf.size.height * 3 / 4);
            Req *rq = (Req *)calloc(1, sizeof(Req)); rq->type = REQ_TEXT; snprintf(rq->text, sizeof rq->text, "%s", argv[2]);
            start_req(rq);
        }
        [NSApp run];
    }
    return 0;
}
