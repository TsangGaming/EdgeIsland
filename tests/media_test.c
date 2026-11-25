/* The fake provider exercises the Windows.Media.Control reader without a personal player account. */
#include "../src/media.c"
#include <stdlib.h>

static void check_case(int passed, const char *expression, int line)
{
    if (!passed) {
        fprintf(stderr, "Media test line %d failed: %s\n", line, expression);
        exit(1);
    }
}
#define CHECK(expr) check_case(!!(expr), #expr, __LINE__)

typedef struct {
    Object api;
    int refs, status;
    Object *child;
} Fake;
static Fake chrome, other_player, chrome_play, other_play, sessions, manager, properties, operation;
static Object *current, *items[2];
static UINT item_count;
static int properties_ready, thumbnail_reads;
static const wchar_t *track_title, *track_artist;

static ULONG WINAPI keep(Object *o) { return ++((Fake *)o)->refs; }
static ULONG WINAPI free_ref(Object *o) { CHECK(((Fake *)o)->refs > 1); return --((Fake *)o)->refs; }
static HRESULT give(Object *o, Object **out)
{
    *out = o;
    if (o) { keep(o); }
    return S_OK;
}
static HRESULT WINAPI query_fake(Object *o, const GUID *id, void **out)
{
    (void)id;
    return give(o, (Object **)out);
}
static HRESULT WINAPI child(Object *o, Object **out) { return give(((Fake *)o)->child, out); }
static HRESULT WINAPI status(Object *o, int *out) { *out = ((Fake *)o)->status; return S_OK; }
static HRESULT WINAPI current_session(Object *o, Object **out) { (void)o; return give(current, out); }
static HRESULT WINAPI get_list(Object *o, Object **out) { (void)o; return give(&sessions.api, out); }
static HRESULT WINAPI size(Object *o, UINT *out) { (void)o; *out = item_count; return S_OK; }
static HRESULT WINAPI item(Object *o, UINT i, Object **out)
{
    (void)o;
    return i < item_count ? give(items[i], out) : E_FAIL;
}
static HRESULT WINAPI track_op(Object *o, Object **out)
{
    (void)o;
    return properties_ready ? give(&operation.api, out) : E_FAIL;
}
static HRESULT WINAPI title(Object *o, void **out)
{
    (void)o;
    return WindowsCreateString(track_title, lstrlenW(track_title), out);
}
static HRESULT WINAPI artist(Object *o, void **out)
{
    (void)o;
    return WindowsCreateString(track_artist, lstrlenW(track_artist), out);
}
static HRESULT WINAPI no_thumbnail(Object *o, Object **out)
{
    (void)o;
    thumbnail_reads++;
    return give(NULL, out);
}

static void expect_session(Object *previous, Object *expected)
{
    Object *selected = NULL;
    int retry = 0;
    CHECK(SUCCEEDED(choose_session(&manager.api, previous, &selected, &retry)));
    CHECK(selected == expected);
    drop(selected);
}

int main(void)
{
    static void *manager_vt[] = {query_fake, keep, free_ref, 0, 0, 0, current_session, get_list};
    static void *list_vt[] = {query_fake, keep, free_ref, 0, 0, 0, item, size};
    static void *session_vt[] = {query_fake, keep, free_ref, 0, 0, 0, 0, track_op, 0, child};
    static void *play_vt[] = {query_fake, keep, free_ref, 0, 0, 0, 0, status};
    static void *op_vt[] = {query_fake, keep, free_ref, 0, 0, 0, 0, status, child};
    static void *props_vt[] = {query_fake, keep, free_ref, 0, 0, 0, title, 0, 0, artist, 0, 0, 0, 0, 0, no_thumbnail};
    TrackCache cache = {0};
    Media m = {0};
    manager = (Fake){{manager_vt}, 1};
    sessions = (Fake){{list_vt}, 1};
    chrome_play = (Fake){{play_vt}, 1, 4};
    other_play = (Fake){{play_vt}, 1, 5};
    chrome = (Fake){{session_vt}, 1, 0, &chrome_play.api};
    other_player = (Fake){{session_vt}, 1, 0, &other_play.api};
    properties = (Fake){{props_vt}, 1};
    operation = (Fake){{op_vt}, 1, 1, &properties.api};
    items[0] = &other_player.api;
    items[1] = &chrome.api;
    item_count = 2;
    current = &other_player.api;
    expect_session(NULL, &chrome.api);             /* Paused system default, playing browser. */
    current = NULL;
    expect_session(NULL, &chrome.api);             /* Browser exists before Windows picks a default. */
    other_play.status = 4;
    current = &other_player.api;
    expect_session(&chrome.api, &other_player.api); /* The playing system default wins ties. */
    other_play.status = chrome_play.status = 5;
    expect_session(&chrome.api, &chrome.api);       /* Keep the same player when the user pauses it. */
    item_count = 1;
    expect_session(&chrome.api, &other_player.api); /* Removed sessions must be released. */
    current = NULL;
    item_count = 0;
    expect_session(&chrome.api, NULL);

    properties_ready = 1;
    track_title = L"Nevada";
    track_artist = L"Vicetone";
    read_track(&chrome.api, &m, &cache, 0);
    CHECK(!lstrcmpW(m.title, track_title) && !lstrcmpW(m.artist, track_artist));
    CHECK(m.cover_changed && m.needs_retry && thumbnail_reads == 1);
    ZeroMemory(&m, sizeof m);
    read_track(&chrome.api, &m, &cache, 0);
    CHECK(thumbnail_reads == 1 && !m.cover_changed);
    cache.next_art_ms = 0;
    read_track(&chrome.api, &m, &cache, 0);
    CHECK(thumbnail_reads == 2);
    properties_ready = 0;
    ZeroMemory(&m, sizeof m);
    read_track(&chrome.api, &m, &cache, 0);
    CHECK(!lstrcmpW(m.title, L"Nevada") && !m.cover_changed && m.needs_retry);
    properties_ready = 1;
    track_title = L"下一首歌";
    ZeroMemory(&m, sizeof m);
    read_track(&chrome.api, &m, &cache, 1);
    CHECK(!lstrcmpW(m.title, track_title) && m.cover_changed && thumbnail_reads == 3);
    CHECK(chrome.refs == 1 && other_player.refs == 1 && chrome_play.refs == 1 && other_play.refs == 1);
    CHECK(sessions.refs == 1 && properties.refs == 1 && operation.refs == 1);
    puts("Playing session selection, title updates, delayed artwork and temporary metadata loss passed.");
    return 0;
}
