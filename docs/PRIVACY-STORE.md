# EdgeIsland privacy policy — Microsoft Store edition

EdgeIsland processes data locally on the current Windows user's device. The app does not initiate network requests, require an account, contain ads or telemetry, or upload crash reports. Microsoft Store handles installation, updates and removal under Microsoft's own privacy terms.

- Media session titles, artists, artwork, duration and playback state are read from Windows to display and control the selected media session. They are retained in memory only.
- The foreground process image path is used to obtain the app name and display the current usage duration. The app does not read window titles, browser history, URLs or keystrokes.
- Default playback device volume and mute state are used to display a volume indicator; no microphone or audio sample recording occurs.
- CPU, physical memory, power and battery readings are displayed locally; the recent CPU samples are held in memory only.
- Display selection, position, language, interaction mode, sound, power-saving, music clock and timer preferences are saved locally in the current user's app registry settings. Timer end time allows the timer to resume after sleep or restart.
- Optional startup at Windows sign-in is disabled by default. The Store edition uses Windows StartupTask, which can be controlled from EdgeIsland or Windows Startup settings. It respects decisions made by the user or administrator. Windows manages package-specific data and startup registration on uninstall.

No personal data is sold or transmitted by the application. Windows, media players, and Microsoft Store operate under their own privacy policies. For questions, use [GitHub Issues](https://github.com/TsangGaming/EdgeIsland/issues); avoid sharing private media titles, usernames or file paths publicly.
