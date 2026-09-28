# Copilot Key Remapper ⌨✨

A lightweight, high-performance native Windows utility written entirely from scratch to repurpose the dedicated laptop **Copilot key** into any custom action (launch programs, execute shell commands, open web applications, or trigger alternative keystrokes).

---

## 🔍 How It Works Under the Hood

### 1. Hardware Keystroke Macro
On modern Windows 11 OEM laptops (Dell, HP, Lenovo, Asus, Surface, etc.), the physical Copilot key does **not** generate a single unique hardware scan code. Instead, keyboard firmware is configured by Microsoft standard to transmit a hardwired macro combination:

$$\text{Copilot Key} \implies \text{Left Windows Key} + \text{Left Shift} + \text{F23} \quad (\text{Virtual Key } 0\text{x86}, \text{HID usage } 0\text{x0D8})$$

Because legacy physical keyboards only have keys up to `F12`, Microsoft utilized `F23` (a legacy IBM virtual key code recognized by Windows) to trigger Copilot without requiring new hardware drivers.

### 2. Why Standard Remappers Fail & How We Solved It
* **Multi-Key Macro vs Single Key**: Traditional registry-based remapping tools (like SharpKeys) modify the registry `Scancode Map`, which only remaps single keys. They cannot intercept multi-key shortcut combinations.
* **Start Menu Interference**: When the physical Copilot key is pressed, Windows registers `VK_LWIN` going down. If an application blocks `F23` without handling the Windows key, Windows Explorer assumes the Windows key was pressed and released alone, popping up the Start Menu.
  * **Our Solution**: Upon detecting `VK_F23`, our hook immediately injects an unassigned virtual key mask (`0xE8` / `VK_UNASSIGNED_E8`). This signals Windows Explorer that a multi-key combo occurred, cleanly suppressing the Start Menu without side effects.
* **Hardware Modifier Release**: Before executing an action or sending a simulated key (like `Right Ctrl`), the hook logically lifts `LWin` and `LShift`. This guarantees target applications don't inherit lingering modifier keys.
* **True Press & Hold for Modifiers**: When remapped to `Right Ctrl` or `Context Menu`, the hook sends `KeyDown` on Copilot press and `KeyUp` on Copilot release. You can use it as a true modifier (e.g. `Copilot + C`, `Copilot + V`)!
* **Zero-Latency Dispatch**: Low-level keyboard hooks (`WH_KEYBOARD_LL`) run synchronously on the Windows message thread. Our hook consumes the event, injects the mask, and offloads action execution to a background `ThreadPool` worker in under **0.1 milliseconds**, ensuring zero system keyboard lag.
* **Debouncing**: A configurable debounce timer (default $300\text{ ms}$) prevents hardware auto-repeat from launching multiple instances when holding down the key.

---

## 🚀 Key Features

* **Zero External Dependencies**: Compiles into a single standalone binary of under **30 KB** using Windows native C# compiler. Consumes **<15 MB RAM** and **0% CPU** while idle.
* **100% Native Windows UI**: Built with standard Windows Common Controls (`ComCtl32.dll` v6.0), `FlatStyle.System`, and native layout containers with zero third-party UI libraries.
* **High-DPI PerMonitorV2 Aware**: Crisp, sharp text and layout across any monitor DPI scaling via `app.manifest`.
* **DWM System Theming**: Automatically detects Windows Light/Dark mode and activates `DWMWA_USE_IMMERSIVE_DARK_MODE` on the title bar, reacting dynamically to system theme changes.
* **Single-Instance IPC**: Double-clicking `CopilotRemapper.exe` while running smoothly brings the existing Settings window to the foreground via OS kernel `EventWaitHandle`.
* **Portable Mode with Fallback**: Saves configuration locally if writable, or safely falls back to `%APPDATA%\CopilotRemapper\` when running in protected directories.
* **Customizable Actions**:
  * **Launch Application**: Select any `.exe`, `.bat`, `.cmd`, `.ps1`, `.lnk`, or file with optional arguments and working directory.
  * **Run Command / Script**: Execute PowerShell commands seamlessly in the background.
  * **Open Webpage / URL**: Instant one-key access to any URL (e.g., `https://claude.ai`, `https://chatgpt.com`).
* **Live Hardware Status Bar**: Native `StatusStrip` reports timestamped confirmation when the physical Copilot key is pressed.
* **Windows Auto-Start**: One-click toggle in Settings to automatically launch silently on user login.
* **Python Alternative Included**: Includes `copilot_remapper.py` for Python users, requiring zero external `pip` packages.

---

## 📦 Project Files

```
copilot-remapper/
│
├── .gitignore              # Ignores build output + local config
├── CopilotRemapper.exe     # Standalone native Windows executable (27 KB, build output)
├── Program.cs              # Full C# source code with Win32 hook, IPC, and GUI
├── app.manifest            # PerMonitorV2 High-DPI application manifest
├── build.bat               # 1-click build script using native Windows csc.exe
├── build.ps1               # PowerShell build script alternative
├── copilot_config.json     # Your live config (git-ignored, created on first run)
├── copilot_config.example.json  # Sample config to copy if you want to start manually
├── copilot_remapper.py     # Pure Python implementation (using ctypes)
└── README.md               # Documentation and technical guide
```

> **Note:** `CopilotRemapper.exe` and `copilot_config.json` are not tracked in git.
> The executable is generated by `build.bat` / `build.ps1`, and the config is
> machine-specific. Run the build script to produce the binary, then launch it
> once to generate your own `copilot_config.json`.

---

## 🛠 Setup & Usage Instructions

### 1. Launching the Application
Double-click **`CopilotRemapper.exe`** in this folder.

The **Settings** window will open:
1. **Choose Action**:
   * *Launch Application*: Click **Browse...** to select an executable, script, or shortcut.
   * *Run Command*: Type any PowerShell command.
   * *Open Webpage / URL*: Type a website URL (e.g. `https://claude.ai`).
2. **Live Test**:
   Press your physical **Copilot key**. The native status bar displays `Copilot key pressed at HH:mm:ss! Action executed.`, confirming your hardware signal is intercepted.
3. **Save & Close**:
   Check **Start automatically when Windows starts** if desired, click **Save** (or press `Enter`), and click **Close** (or press `Escape`).
   The application will live quietly in your Windows taskbar system tray (blue **C** icon).

---

### 2. Command Line Arguments

`CopilotRemapper.exe` supports the following flags:

| Flag | Description |
| :--- | :--- |
| `CopilotRemapper.exe` | Launches app and opens Settings GUI |
| `CopilotRemapper.exe --silent` | Starts directly minimized in system tray (used by Windows startup) |
| `CopilotRemapper.exe --test` | Performs a self-test of config loading and exits |
| `CopilotRemapper.exe --install-startup` | Registers Windows startup registry key and exits |
| `CopilotRemapper.exe --uninstall-startup` | Removes Windows startup registry key and exits |

---

### 3. Python Alternative

If you prefer running via Python (Python 3.8+ on Windows):

```powershell
# Run interactively
python copilot_remapper.py

# Check current configuration
python copilot_remapper.py --status

# Test action
python copilot_remapper.py --test

# Run silently in background without a console window
pythonw copilot_remapper.py
```

---

### 4. Recompiling from Source

No Visual Studio or external SDK required. Windows includes the C# compiler (`csc.exe`) natively:

Simply double-click **`build.bat`** or run in PowerShell:
```powershell
.\build.ps1
```
It recompiles `CopilotRemapper.exe` in under 2 seconds.

---

## ⚙ Configuration (`copilot_config.json`)

Settings are stored in JSON format and can be modified via the Settings GUI or a
text editor. The file is created automatically on first run. To bootstrap it
manually, copy `copilot_config.example.json` to `copilot_config.json` and edit
it to taste:

```json
{
  "ActionType": "LaunchApp",
  "TargetPath": "wt.exe",
  "Arguments": "",
  "WorkingDirectory": "",
  "RunAsAdmin": false,
  "StartWithWindows": true,
  "ShowNotifications": false,
  "DebounceMs": 300
}
```

---

## ❓ Frequently Asked Questions

**Q: Does this disable or affect regular Windows key shortcuts (like Win+E, Win+R, Win+D)?**  
**A:** No. The hook specifically filters for `VK_F23` (`0x86`). When you press the Windows key alone or in other shortcuts, `VK_F23` is never sent, so all native Windows shortcuts continue to function identically.

**Q: Will the Start Menu pop up when I press the Copilot key?**  
**A:** No. The app automatically sends the `0xE8` unassigned key mask before the hardware `Win` release event occurs, preventing Windows Explorer from triggering the Start Menu.

**Q: Can I use the Copilot key as a real Right Ctrl key (e.g. for Ctrl+C, Ctrl+V)?**  
**A:** Yes! When mapped to `Right Ctrl`, the application tracks physical keydown and keyup states, allowing you to hold down the Copilot key as a modifier.
