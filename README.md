# EasyMacro

A tiny Windows tray app that types a recorded key sequence when you press a hotkey.

- Record any keys or shortcuts, such as `Ctrl+Shift+Esc`, `Ctrl+C, Ctrl+V` or a line of text.
- Assign it to any hotkey: `F9`, `Ctrl+Alt+T`, `Win+Shift+K` and so on.
- Lives in the system tray. It follows the Windows light/dark theme and accent color, and scales cleanly on high-DPI displays.

## Footprint

EasyMacro is native Win32 code, built without the C runtime. The whole program is a single ~100 KB `.exe` that needs no installer and has no dependencies.

| | |
|---|---|
| Idle in the tray | ~250 KB private memory, 0% CPU |
| Window open | ~3 MB private memory |

It does no polling and sets no timers. Hotkeys are registered with Windows (`RegisterHotKey`), so EasyMacro sleeps until one is pressed. A keyboard hook is installed only while you are recording or setting a hotkey, and is removed as soon as you finish.

## Using it

1. Click **New macro**.
2. Click the **Hotkey** box and press the shortcut that should trigger the macro.
3. Click **Record**, type the keys to send, then click **Stop**. While recording, your keystrokes are captured and do not reach other apps.
4. Click **Save**. The hotkey works everywhere until you pause it.

Closing the window keeps EasyMacro running in the tray. Click the tray icon to reopen it, or right-click it to pause hotkeys, toggle **Start with Windows**, or exit. In the list, each macro has its own on/off switch, and you can click a macro to edit or delete it.

Keyboard: `Ctrl+N` creates a new macro. In the editor, `Enter` saves and `Esc` cancels. In the list, `Esc` hides the window.

## Notes

- Before playing a macro, EasyMacro releases the hotkey's modifiers so they don't mix into the keys it sends. To play the same macro again, release the hotkey and press it again.
- Windows does not let a normal app send keystrokes to apps running as administrator. To use macros in elevated apps, run EasyMacro as administrator too.
- Some shortcuts, such as `Win+L`, are reserved by Windows. EasyMacro tells you when a hotkey is unavailable.
- Settings are stored in `%APPDATA%\EasyMacro\macros.cfg`.

## Building

You need Visual Studio (2019 or later) with the **Desktop development with C++** workload.

```
build.cmd
```

This produces `build\EasyMacro.exe`. To regenerate the icons in `res\`, run `powershell -ExecutionPolicy Bypass -File tools\make-icons.ps1`.

| Source | |
|---|---|
| `src/main.cpp` | Entry point, tray icon, window and theme |
| `src/ui.cpp` | The list and editor views (custom drawn) |
| `src/gfx.cpp` | A small renderer: antialiased rounded shapes, with text drawn by GDI |
| `src/macro.cpp` | Recording hook, playback, hotkeys, key names, settings |
| `src/crt.cpp` | The few C runtime symbols the compiler needs when building without the CRT |
