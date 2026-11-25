// MSIX-only replacement for src/startup.c. The regular EXE build is unchanged.
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.h>
#include <thread>
using winrt::Windows::ApplicationModel::StartupTask;
using winrt::Windows::ApplicationModel::StartupTaskState;

static bool is_enabled(StartupTaskState state) noexcept {
    return state == StartupTaskState::Enabled || state == StartupTaskState::EnabledByPolicy;
}

// A dedicated MTA avoids blocking an STA on WinRT async APIs. For a packaged
// desktop app RequestEnableAsync does not show the UWP consent dialog.
// Never override a startup decision made by the user in Windows or by policy.
static int startup_operation(int operation) noexcept {
    int result = 0;
    try {
        std::thread worker([&]() noexcept {
            try {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
            } catch (...) { return; }
            try {
                auto task = StartupTask::GetAsync(L"EdgeIslandStartup").get();
                auto state = task.State();
                if (operation < 0) {
                    result = is_enabled(state);
                } else if (operation == 0) {
                    if (state == StartupTaskState::Enabled) task.Disable();
                    result = !is_enabled(task.State());
                } else if (is_enabled(state)) {
                    result = 1;
                } else if (state == StartupTaskState::Disabled) {
                    result = is_enabled(task.RequestEnableAsync().get());
                }
            } catch (...) { result = 0; }
            winrt::uninit_apartment();
        });
        worker.join();
    } catch (...) { return 0; }
    return result;
}
extern "C" int startup_registered(void) { return startup_operation(-1); }
extern "C" int startup_set(int enabled) { return startup_operation(enabled ? 1 : 0); }
