// MSIX-only startup implementation. All WinRT calls run on an MTA worker.
#include <windows.h>
#include <process.h>
#include <new>
#include <chrono>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.h>
#include "../../src/startup.h"

using winrt::Windows::ApplicationModel::StartupTask;
using winrt::Windows::ApplicationModel::StartupTaskState;
using winrt::Windows::Foundation::AsyncStatus;
using namespace std::chrono_literals;

struct StartupRequest { HWND window; bool toggle; };

static bool is_enabled(StartupTaskState state) noexcept {
    return state == StartupTaskState::Enabled || state == StartupTaskState::EnabledByPolicy;
}

// C++/WinRT wait_for installs the completion handler; use GetResults, not get(),
// after a successful wait. On timeout, cancel on the worker, never on the UI.
template <class T> static T bounded_result(winrt::Windows::Foundation::IAsyncOperation<T> const& op) {
    auto status = op.wait_for(5s);
    if (status == AsyncStatus::Started) {
        op.Cancel();
        throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    }
    return op.GetResults(); // raises on Error or Canceled
}

static unsigned __stdcall startup_worker(void* argument) noexcept {
    auto* request = static_cast<StartupRequest*>(argument);
    HWND window = request->window;
    bool toggle = request->toggle;
    delete request;
    WPARAM state_result = 0;
    LPARAM success = 0;
    bool initialized = false;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        initialized = true;
        {
            auto task = bounded_result(StartupTask::GetAsync(L"EdgeIslandStartup"));
            auto state = task.State();
            state_result = is_enabled(state) ? 2 : 1;
            if (!toggle) {
                success = 1;
            } else if (state == StartupTaskState::Enabled) {
                task.Disable();
                state = task.State();
                state_result = is_enabled(state) ? 2 : 1;
                success = !is_enabled(state);
            } else if (state == StartupTaskState::Disabled) {
                // Packaged desktop apps do not need the UWP consent dialog.
                state = bounded_result(task.RequestEnableAsync());
                state_result = is_enabled(state) ? 2 : 1;
                success = is_enabled(state);
            }
            // Do not override a user-disabled or policy-controlled startup task.
        }
    } catch (...) {
        state_result = 0; // preserve the UI's last known state on failure
        success = 0;
        OutputDebugStringW(L"EdgeIsland: StartupTask query/change failed or timed out.\n");
    }
    if (initialized) { winrt::uninit_apartment(); }
    PostMessageW(window, EDGE_STORE_STARTUP_RESULT, state_result, success);
    return 0;
}

extern "C" int startup_store_begin(HWND hwnd, int toggle) {
    auto* request = new (std::nothrow) StartupRequest{hwnd, toggle != 0};
    if (!request) return 0;
    uintptr_t thread = _beginthreadex(nullptr, 0, startup_worker, request, 0, nullptr);
    if (!thread) { delete request; return 0; }
    CloseHandle(reinterpret_cast<HANDLE>(thread)); // no join or UI wait
    return 1;
}
