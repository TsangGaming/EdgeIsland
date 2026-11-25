#include "audio.h"
/* Only output endpoint volume notifications are registered. No sound samples are read. */
typedef struct AudioObj {
    void **v;
} AudioObj;
typedef struct Callback {
    void **v;
    const GUID *iid;
    LONG refs;
} Callback;
typedef struct {
    GUID context;
    BOOL muted;
    float level;
    UINT channels;
    float channel[1];
} VolumeNotice;
typedef struct {
    GUID fmtid;
    DWORD pid;
} PropertyKey;
#ifdef __TINYC__
extern HRESULT WINAPI CoInitializeEx(void *, DWORD);
extern void WINAPI CoUninitialize(void);
extern HRESULT WINAPI CoCreateInstance(const GUID *, void *, DWORD, const GUID *, void **);
#else
#include <objbase.h>
#endif
static const GUID enumerator_class = {0xbcde0395, 0xe52f, 0x467c, {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}};
static const GUID enumerator_iid = {0xa95664d2, 0x9614, 0x4f35, {0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6}};
static const GUID endpoint_iid = {0x5cdf2c82, 0x841e, 0x4546, {0x97, 0x22, 0x0c, 0xf7, 0x40, 0x78, 0x22, 0x9a}};
static const GUID callback_iid = {0x657804fa, 0xd6ad, 0x4496, {0x8a, 0x60, 0x35, 0x27, 0x52, 0xaf, 0x4f, 0x89}};
static const GUID device_cb_iid = {0x7991eec9, 0x7e89, 0x4d85, {0x83, 0x90, 0x6c, 0x70, 0x3c, 0xec, 0x60, 0xc0}};
static const GUID unk_iid = {0, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
static AudioObj *devices, *volume;
static HWND window;
static int initialized, watch_device, watch_volume;
static HRESULT WINAPI query(Callback *cb, const GUID *id, void **out)
{
    if (!out) {
        return E_POINTER;
    }
    *out = NULL;
    if (memcmp(id, &unk_iid, sizeof *id) && memcmp(id, cb->iid, sizeof *id)) {
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&cb->refs);
    *out = cb;
    return S_OK;
}
static ULONG WINAPI retain(Callback *cb)
{
    return InterlockedIncrement(&cb->refs);
}
static ULONG WINAPI release(Callback *cb)
{
    return InterlockedDecrement(&cb->refs);
}
static HRESULT WINAPI changed(Callback *cb, VolumeNotice *v)
{
    (void)cb;
    if (v) {
        PostMessageW(window, MSG_VOLUME, (WPARAM)bounded((int)(v->level * 100 + 0.5f), 0, 100), (LPARAM)!!v->muted);
    }
    return S_OK;
}
static HRESULT WINAPI state_changed(Callback *cb, const wchar_t *id, DWORD state)
{
    (void)cb;
    (void)id;
    (void)state;
    return S_OK;
}
static HRESULT WINAPI device_event(Callback *cb, const wchar_t *id)
{
    (void)cb;
    (void)id;
    return S_OK;
}
static HRESULT WINAPI default_changed(Callback *cb, int flow, int role, const wchar_t *id)
{
    (void)cb;
    (void)id;
    if (flow == 0 && role == 1) {
        PostMessageW(window, MSG_REBIND_AUDIO, 0, 0);
    }
    return S_OK;
}
static HRESULT WINAPI property_changed(Callback *cb, const wchar_t *id, PropertyKey key)
{
    (void)cb;
    (void)id;
    (void)key;
    return S_OK;
}
static void *volume_table[] = {(void *)query, (void *)retain, (void *)release, (void *)changed};
static void *device_table[] = {(void *)query, (void *)retain, (void *)release, (void *)state_changed, (void *)device_event, (void *)device_event, (void *)default_changed, (void *)property_changed};
static Callback volume_cb = {volume_table, &callback_iid, 1}, device_cb = {device_table, &device_cb_iid, 1};
typedef HRESULT(WINAPI *Watch)(AudioObj *, Callback *);
typedef HRESULT(WINAPI *DefaultEndpoint)(AudioObj *, int, int, AudioObj **);
typedef HRESULT(WINAPI *Activate)(AudioObj *, const GUID *, DWORD, void *, AudioObj **);
typedef ULONG(WINAPI *FreeObject)(AudioObj *);
static void drop_audio(AudioObj *o)
{
    if (o) {
        ((FreeObject)o->v[2])(o);
    }
}
static void unbind(void)
{
    if (watch_volume) {
        ((Watch)volume->v[4])(volume, &volume_cb);
    }
    watch_volume = 0;
    drop_audio(volume);
    volume = NULL;
}
void audio_rebind(void)
{
    AudioObj *device = NULL;
    unbind();
    if (!devices) {
        return;
    }
    if (SUCCEEDED(((DefaultEndpoint)devices->v[4])(devices, 0, 1, &device)) && device) {
        if (SUCCEEDED(((Activate)device->v[3])(device, &endpoint_iid, 23, NULL, &volume)) && volume) {
            watch_volume = SUCCEEDED(((Watch)volume->v[3])(volume, &volume_cb));
        }
        drop_audio(device);
    }
}
void audio_start(HWND w)
{
    window = w;
    initialized = SUCCEEDED(CoInitializeEx(NULL, 2));
    if (!initialized) {
        return;
    }
    if (SUCCEEDED(CoCreateInstance(&enumerator_class, NULL, 1, &enumerator_iid, (void * *)&devices)) && devices) {
        watch_device = SUCCEEDED(((Watch)devices->v[6])(devices, &device_cb));
        audio_rebind();
    }
}
void audio_stop(void)
{
    unbind();
    if (watch_device) {
        ((Watch)devices->v[7])(devices, &device_cb);
    }
    drop_audio(devices);
    devices = NULL;
    if (initialized) {
        CoUninitialize();
    }
}
