#include "media.h"

/* Projections of the Windows.Media.Control WinRT interfaces, in Windows SDK ABI order.
   All COM calls and async waits run on one MTA thread. */
typedef struct Object {
    void **methods;
} Object;
typedef HRESULT(WINAPI *Getter)(Object *, void *);
typedef HRESULT(WINAPI *Query)(Object *, const GUID *, void **);
typedef ULONG(WINAPI *Release)(Object *);
typedef HRESULT(WINAPI *Call0)(Object *);
typedef HRESULT(WINAPI *Seek)(Object *, long long, Object **);
#define GET(o,slot,out) (((Getter)(o)->methods[slot])((o),(void*)(out)))
#define QI(o,iid,out) (((Query)(o)->methods[0])((o),(iid),(void**)(out)))
extern HRESULT WINAPI RoInitialize(int);
extern void WINAPI RoUninitialize(void);
extern HRESULT WINAPI WindowsCreateString(const wchar_t *, UINT, void **);
extern HRESULT WINAPI WindowsDeleteString(void *);
extern const wchar_t *WINAPI WindowsGetStringRawBuffer(void *, UINT *);
extern HRESULT WINAPI RoGetActivationFactory(void *, const GUID *, void **);
struct IUnknown;
extern HRESULT WINAPI CreateStreamOverRandomAccessStream(struct IUnknown *, const GUID *, void **);
extern int WINAPI GdipLoadImageFromStream(void *, void **);
extern int WINAPI GdipGetImageWidth(void *, UINT *);
extern int WINAPI GdipGetImageHeight(void *, UINT *);
extern int WINAPI GdipCreateBitmapFromScan0(int, int, int, int, void *, void **);
extern int WINAPI GdipGetImageGraphicsContext(void *, void **);
extern int WINAPI GdipSetInterpolationMode(void *, int);
extern int WINAPI GdipDrawImageRectRectI(void *, void *, int, int, int, int, int, int, int, int, int, void *,
        void *, void *);
extern int WINAPI GdipCreateHBITMAPFromBitmap(void *, HBITMAP *, DWORD);
extern int WINAPI GdipDeleteGraphics(void *);
extern int WINAPI GdipDisposeImage(void *);
static const GUID manager_statics = {0x2050c4ee, 0x11a0, 0x57de, {0xae, 0xd7, 0xc9, 0x7c, 0x70, 0x33, 0x82, 0x45}};
static const GUID async_info = {0x00000036, 0x0000, 0x0000, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID stream_iid = {0x0000000c, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID random_iid = {0x905a0fe1, 0xbc53, 0x11df, {0x8c, 0x49, 0, 0x1e, 0x4f, 0xc6, 0x86, 0xda}};
static const GUID unknown_iid = {0, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
static HWND owner;
static HANDLE wake_event, worker;
static volatile LONG stopping;
static CRITICAL_SECTION queue_lock;
static struct {
    int command, fraction;
} commands[16];
static int queued, dirty;
typedef struct {
    wchar_t title[256], artist[180];
    unsigned long long last_title_ms, next_art_ms;
    int has_art, art_attempts;
} TrackCache;

static void drop(Object *o)
{
    if (o) {
        ((Release)o->methods[2])(o);
    }
}
static int same_object(Object *a, Object *b)
{
    Object *ia = NULL, *ib = NULL;
    int same;
    if (a == b) { return 1; }
    if (!a || !b) { return 0; }
    QI(a, &unknown_iid, &ia);
    QI(b, &unknown_iid, &ib);
    same = ia && ia == ib;
    drop(ia);
    drop(ib);
    return same;
}
/* TypedEventHandler IIDs are published in windows.media.control.h. Subscriptions and
   their targets live on the MTA. Callbacks only coalesce a refresh request. */
typedef struct Listener Listener;
typedef struct {
    HRESULT(WINAPI *query)(Listener *, const GUID *, void **);
    ULONG(WINAPI *retain)(Listener *);
    ULONG(WINAPI *release)(Listener *);
    HRESULT(WINAPI *invoke)(Listener *, Object *, Object *);
} ListenerMethods;
struct Listener {
    const ListenerMethods *vt;
    GUID iid;
    LONG refs;
};
static const GUID agile_iid = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
static volatile LONG suspended;
static volatile LONG metadata_event;
static Listener on_track;
static ULONG WINAPI listener_retain(Listener *e)
{
    return InterlockedIncrement(&e->refs);
}
static ULONG WINAPI listener_release(Listener *e)
{
    return InterlockedDecrement(&e->refs);
}
static HRESULT WINAPI listener_query(Listener *e, const GUID *id, void **out)
{
    if (!out) {
        return E_POINTER;
    }
    *out = NULL;
    if (memcmp(id, &e->iid, sizeof *id) && memcmp(id, &unknown_iid, sizeof *id) &&
            memcmp(id, &agile_iid, sizeof *id)) {
        return E_NOINTERFACE;
    }
    *out = e;
    listener_retain(e);
    return S_OK;
}
static HRESULT WINAPI listener_invoke(Listener *e, Object *sender, Object *args)
{
    (void)sender;
    (void)args;
    if (!suspended && !stopping) {
        if (e == &on_track) { InterlockedExchange(&metadata_event, 1); }
        media_request(MEDIA_REFRESH, 0);
    }
    return S_OK;
}
static const ListenerMethods methods = {listener_query, listener_retain, listener_release, listener_invoke};
static Listener on_session = {&methods, {0x228bd0ed, 0x1fa2, 0x5e9b, {0xa6, 0xec, 0x42, 0x56, 0x61, 0x73, 0x10, 0x3b}}, 1};
static Listener on_list = {&methods, {0x2e2a8630, 0xdc8c, 0x530a, {0x97, 0x46, 0xbc, 0x98, 0x4d, 0x4b, 0x02, 0x9e}}, 1};
static Listener on_play = {&methods, {0x2bdf1426, 0xd41f, 0x5896, {0x89, 0x7f, 0xef, 0xc0, 0xb0, 0xfa, 0x73, 0x92}}, 1};
static Listener on_track = {&methods, {0x0f2ce2b7, 0xafa7, 0x5ed0, {0x8c, 0xb6, 0x8c, 0x40, 0xcf, 0x9b, 0x3a, 0x5f}}, 1};
typedef struct {
    Object *source;
    int slot;
    long long token;
} Subscription;
typedef HRESULT(WINAPI *Listen)(Object *, Listener *, long long *);
typedef HRESULT(WINAPI *Unlisten)(Object *, long long);
static void unwatch(Subscription *s)
{
    if (s->source) {
        ((Unlisten)s->source->methods[s->slot + 1])(s->source, s->token);
        drop(s->source);
        s->source = NULL;
    }
}
static void watch(Subscription *s, Object *source, int slot, Listener *e)
{
    if (same_object(s->source, source)) {
        return;
    }
    unwatch(s);
    if (source && SUCCEEDED(((Listen)source->methods[slot])(source, e, &s->token))) {
        ((Release)source->methods[1])(source);
        s->source = source;
        s->slot = slot;
    }
}
void media_suspend(int yes)
{
    InterlockedExchange(&suspended, yes);
}

static int await_result(Object *op, void *answer)
{
    Object *info = NULL;
    int status = 0, ok = 0, waits = 0;
    if (!op) {
        return 0;
    }
    if (SUCCEEDED(QI(op, &async_info, &info))) {
        while (!stopping && waits++ < 100 && SUCCEEDED(GET(info, 7, &status)) && status == 0) {
            Sleep(20);
        }
        if (status == 1) {
            ok = SUCCEEDED(GET(op, 8, answer));
        } else if (status == 0) {
            ((Call0)info->methods[9])(info);
        }
        drop(info);
    }
    drop(op);
    return ok;
}
static void read_string(Object *o, int slot, wchar_t *dst, int cap)
{
    void *s = NULL;
    if (o && SUCCEEDED(GET(o, slot, &s)) && s) {
        copy_text(dst, cap, WindowsGetStringRawBuffer(s, NULL));
        WindowsDeleteString(s);
    }
}
static int supported(Object *o, int slot)
{
    BYTE b = 0;
    return o && SUCCEEDED(GET(o, slot, &b)) && b;
}
static Object *manager_open(void)
{
    static const wchar_t name[] = L"Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager";
    void *str = NULL;
    Object *factory = NULL, *op = NULL, *manager = NULL;
    if (FAILED(WindowsCreateString(name, COUNT(name) - 1, &str))) {
        return NULL;
    }
    if (SUCCEEDED(RoGetActivationFactory(str, &manager_statics, (void * *)&factory))) {
        if (SUCCEEDED(GET(factory, 6, &op))) {
            await_result(op, &manager);
        }
        drop(factory);
    }
    WindowsDeleteString(str);
    return manager;
}
static int playing(Object *session)
{
    Object *info = NULL;
    int status = 0;
    if (session && SUCCEEDED(GET(session, 9, &info)) && info) { GET(info, 7, &status); }
    drop(info);
    return status == 4;
}
typedef HRESULT(WINAPI *SessionAt)(Object *, UINT, Object **);
static HRESULT choose_session(Object *manager, Object *previous, Object **selected, int *check_other_sessions)
{
    Object *best = NULL, *list = NULL;
    UINT count = 0, i;
    HRESULT current_hr = GET(manager, 6, &best), list_hr = S_OK;
    if (!playing(best)) {
        list_hr = GET(manager, 7, &list);
        if (list && SUCCEEDED(GET(list, 7, &count))) {
            *check_other_sessions = count > 1;
            for (i = 0; i < count; i++) {
                Object *candidate = NULL;
                int active;
                if (FAILED(((SessionAt)list->methods[6])(list, i, &candidate)) || !candidate) { continue; }
                active = playing(candidate);
                if (active || !best || same_object(candidate, previous)) {
                    drop(best);
                    best = candidate;
                    if (active) { *check_other_sessions = 0; break; }
                } else { drop(candidate); }
            }
        } else { *check_other_sessions = 1; }
        drop(list);
    }
    *selected = best;
    return best || SUCCEEDED(current_hr) ? S_OK : list_hr;
}
static HBITMAP artwork(Object *properties)
{
    Object *ref = NULL, *op = NULL, *input = NULL, *random = NULL, *stream = NULL;
    void *image = NULL, *thumb = NULL, *graphics = NULL;
    unsigned long long size = 0;
    UINT w = 0, h = 0, side;
    HBITMAP result = NULL;
    if (FAILED(GET(properties, 15, &ref)) || !ref) {
        goto done;
    }
    if (FAILED(GET(ref, 6, &op)) || !await_result(op, &input) || !input) {
        goto done;
    }
    if (FAILED(QI(input, &random_iid, &random)) || FAILED(GET(random, 6, &size)) || !size || size > 8 * 1024 * 1024) {
        goto done;
    }
    if (FAILED(CreateStreamOverRandomAccessStream((struct IUnknown *)random, &stream_iid, (void **)&stream))) {
        goto done;
    }
    if (GdipLoadImageFromStream(stream, &image) || !image) {
        goto done;
    }
    GdipGetImageWidth(image, &w);
    GdipGetImageHeight(image, &h);
    if (!w || !h || w > 4096 || h > 4096) {
        goto done;
    }
    if (GdipCreateBitmapFromScan0(128, 128, 0, 0x000e200b, NULL, &thumb) ||
            GdipGetImageGraphicsContext(thumb, &graphics)) {
        goto done;
    }
    side = w < h ? w : h;
    GdipSetInterpolationMode(graphics, 7);
    if (!GdipDrawImageRectRectI(graphics, image, 0, 0, 128, 128, (w - side) / 2, (h - side) / 2, side, side, 2, NULL,
                                NULL, NULL)) {
        GdipCreateHBITMAPFromBitmap(thumb, &result, 0xff282831);
    }
done:
    if (graphics) {
        GdipDeleteGraphics(graphics);
    }
    if (thumb) {
        GdipDisposeImage(thumb);
    }
    if (image) {
        GdipDisposeImage(image);
    }
    drop(stream);
    drop(random);
    drop(input);
    drop(ref);
    return result;
}
static void read_track(Object *session, Media *m, TrackCache *cache, int event)
{
    Object *op = NULL, *props = NULL;
    unsigned long long now = GetTickCount64();
    int changed;
    if (SUCCEEDED(GET(session, 7, &op)) && await_result(op, &props) && props) {
        read_string(props, 6, m->title, COUNT(m->title));
        read_string(props, 9, m->artist, COUNT(m->artist));
    }
    if (*m->title) {
        cache->last_title_ms = now;
    } else {
        m->needs_retry = 1;
        if (*cache->title && now - cache->last_title_ms < 8000 &&
            (!*m->artist || !lstrcmpW(cache->artist, m->artist))) {
            copy_text(m->title, COUNT(m->title), cache->title);
            copy_text(m->artist, COUNT(m->artist), cache->artist);
            drop(props);
            return;
        }
    }
    changed = lstrcmpW(cache->title, m->title) || lstrcmpW(cache->artist, m->artist);
    if (changed) {
        copy_text(cache->title, COUNT(cache->title), m->title);
        copy_text(cache->artist, COUNT(cache->artist), m->artist);
        cache->has_art = cache->art_attempts = 0;
        cache->next_art_ms = 0;
        m->cover_changed = 1;
    }
    if (props && (event || (!cache->has_art && now >= cache->next_art_ms))) {
        m->cover = artwork(props);
        if (m->cover) {
            m->cover_changed = cache->has_art = 1;
        } else {
            cache->next_art_ms = now + (cache->art_attempts ? 30000 : 2500);
            if (cache->art_attempts < 2) { cache->art_attempts++; }
        }
    }
    if (!cache->has_art && cache->art_attempts < 2) { m->needs_retry = 1; }
    drop(props);
}
static void refresh(Object *session, Media *m, TrackCache *cache, int event)
{
    Object *play = NULL, *controls = NULL, *time = NULL, *rate = NULL;
    int status = 0;
    long long updated = 0;
    m->available = 1;
    m->rate = 1.0;
    m->observed = wall_ms();
    read_track(session, m, cache, event);
    if (SUCCEEDED(GET(session, 9, &play)) && play) {
        GET(play, 7, &status);
        m->playing = status == 4;
        if (SUCCEEDED(GET(play, 10, &rate)) && rate) {
            GET(rate, 6, &m->rate);
            drop(rate);
        }
        if (m->rate < 0 || m->rate > 16) {
            m->rate = 1;
        }
        if (SUCCEEDED(GET(play, 6, &controls)) && controls) {
            m->can_play = supported(controls, 6);
            m->can_pause = supported(controls, 7);
            m->can_next = supported(controls, 12);
            m->can_prev = supported(controls, 13);
            m->can_toggle = supported(controls, 16);
            m->can_seek = supported(controls, 20);
            drop(controls);
        }
        drop(play);
    }
    if (SUCCEEDED(GET(session, 8, &time)) && time) {
        GET(time, 6, &m->begin);
        GET(time, 7, &m->end);
        GET(time, 8, &m->min_seek);
        GET(time, 9, &m->max_seek);
        GET(time, 10, &m->position);
        GET(time, 11, &updated);
        drop(time);
        if (updated > 0 && (unsigned long long)updated / 10000 <= m->observed) {
            m->observed = (unsigned long long)updated / 10000;
        }
    }
}
static void transport(Object *session, int cmd, int frac)
{
    Object *op = NULL, *play = NULL, *controls = NULL, *time = NULL;
    int status = 0, slot = -1;
    BYTE result = 0;
    if (FAILED(GET(session, 9, &play)) || !play) {
        return;
    }
    GET(play, 7, &status);
    GET(play, 6, &controls);
    if (cmd == MEDIA_TOGGLE) {
        if (supported(controls, 16)) {
            slot = 20;
        } else if (status == 4 && supported(controls, 7)) {
            slot = 11;
        } else if (status != 4 && supported(controls, 6)) {
            slot = 10;
        }
    } else if (cmd == MEDIA_PREV && supported(controls, 13)) {
        slot = 17;
    } else if (cmd == MEDIA_NEXT && supported(controls, 12)) {
        slot = 16;
    } else if (cmd == MEDIA_SEEK && supported(controls, 20) && SUCCEEDED(GET(session, 8, &time)) && time) {
        long long start = 0, end = 0, min = 0, max = 0, position;
        GET(time, 6, &start);
        GET(time, 7, &end);
        GET(time, 8, &min);
        GET(time, 9, &max);
        if (end > start && max >= min) {
            position = start + (long long)((end - start) * (bounded(frac, 0, 1000) / 1000.0));
            if (position < min) {
                position = min;
            }
            if (position > max) {
                position = max;
            }
            ((Seek)session->methods[24])(session, position, &op);
        }
    }
    if (slot >= 0) {
        GET(session, slot, &op);
    }
    if (op) {
        await_result(op, &result);
    }
    drop(time);
    drop(controls);
    drop(play);
}
static DWORD WINAPI media_loop(void *unused)
{
    Object *manager = NULL, *selected = NULL;
    TrackCache cache = {0};
    Subscription manager_sub = {0}, list_sub = {0}, play_sub = {0}, track_sub = {0};
    HRESULT init = RoInitialize(1);
    (void)unused;
    while (WaitForSingleObject(wake_event, INFINITE) == WAIT_OBJECT_0 && !stopping) {
        int cmd = -1, frac = 0;
        do {
            Object *session = NULL;
            Media *snapshot;
            HRESULT hr = E_FAIL;
            int scan_soon = 0, changed, event;
            EnterCriticalSection(&queue_lock);
            if (queued > 0) {
                cmd = commands[0].command;
                frac = commands[0].fraction;
                --queued;
                memmove(commands, commands + 1, queued *sizeof(commands[0]));
            } else if (dirty) {
                cmd = MEDIA_REFRESH;
                dirty = 0;
            } else {
                cmd = -1;
            }
            LeaveCriticalSection(&queue_lock);
            if (cmd < 0 || stopping) {
                break;
            }
            if (!manager && SUCCEEDED(init)) {
                manager = manager_open();
            }
            watch(&manager_sub, manager, 8, &on_session);
            watch(&list_sub, manager, 10, &on_list);
            if (selected && cmd != MEDIA_REFRESH) { transport(selected, cmd, frac); }
            if (manager) { hr = choose_session(manager, selected, &session, &scan_soon); }
            if (FAILED(hr)) {
                unwatch(&manager_sub);
                unwatch(&list_sub);
                drop(manager);
                manager = NULL;
            }
            watch(&play_sub, session, 27, &on_play);
            watch(&track_sub, session, 29, &on_track);
            changed = !same_object(selected, session);
            snapshot = (Media *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Media));
            if (snapshot) {
                event = InterlockedExchange(&metadata_event, 0);
                if (changed) { ZeroMemory(&cache, sizeof cache); }
                snapshot->service_ok = manager != NULL;
                snapshot->events_ok = manager_sub.source && list_sub.source && (!session || (play_sub.source && track_sub.source));
                snapshot->needs_retry = scan_soon || !snapshot->events_ok;
                snapshot->cover_changed = snapshot->session_changed = changed;
                if (session) {
                    refresh(session, snapshot, &cache, event);
                }
                if (stopping || !PostMessageW(owner, MSG_MEDIA, 0, (LPARAM)snapshot)) {
                    if (snapshot->cover) {
                        DeleteObject(snapshot->cover);
                    }
                    HeapFree(GetProcessHeap(), 0, snapshot);
                    ZeroMemory(&cache, sizeof cache);
                } else {
                    drop(selected);
                    selected = session;
                    session = NULL;
                }
            }
            drop(session);
        } while (!stopping);
    }
    unwatch(&play_sub);
    unwatch(&track_sub);
    unwatch(&list_sub);
    unwatch(&manager_sub);
    drop(selected);
    drop(manager);
    if (SUCCEEDED(init)) {
        RoUninitialize();
    }
    return 0;
}
int media_start(HWND hwnd)
{
    owner = hwnd;
    InitializeCriticalSection(&queue_lock);
    wake_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (wake_event) {
        worker = CreateThread(NULL, 0, media_loop, NULL, 0, NULL);
    }
    if (!worker) {
        if (wake_event) { CloseHandle(wake_event); wake_event = NULL; }
        DeleteCriticalSection(&queue_lock);
    }
    return worker != NULL;
}
void media_request(int cmd, int frac)
{
    if (!worker || stopping) {
        return;
    }
    EnterCriticalSection(&queue_lock);
    if (cmd == MEDIA_REFRESH && dirty) {
        LeaveCriticalSection(&queue_lock);
        return;
    }
    if (cmd == MEDIA_REFRESH) {
        dirty = 1;
    } else if (queued < COUNT(commands)) {
        commands[queued].command = cmd;
        commands[queued++].fraction = frac;
    }
    LeaveCriticalSection(&queue_lock);
    SetEvent(wake_event);
}
void media_stop(void)
{
    if (worker) {
        InterlockedExchange(&stopping, 1);
        SetEvent(wake_event);
        WaitForSingleObject(worker, INFINITE);
        CloseHandle(worker);
    }
    if (wake_event) {
        CloseHandle(wake_event);
    }
    DeleteCriticalSection(&queue_lock);
}
