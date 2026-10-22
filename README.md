# Copilot Key Remapper ⌨✨

A lightweight, high-performance native Windows utility written in **modern C++ (C++17)** from scratch to repurpose the dedicated laptop **Copilot key** into any custom action (launch programs, execute shell commands, open web applications, or trigger alternative keystrokes).

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
* **Smart Window Activation & UAC Avoidance**:
  * **Already in Foreground**: If the target application is already focused, pressing the Copilot key does nothing—preventing duplicate launches, window flickers, or unassigned Settings popups.
  * **Running in Background**: If the target application is already running, the remapper immediately brings its existing window to the foreground, restores it from minimized state, and bypasses Windows focus-stealing locks (`AttachThreadInput` & `AllowSetForegroundWindow`).
  * **UAC Avoidance**: If the target application is already running with administrator privileges, the existing elevated instance is brought to focus directly with **zero additional UAC prompts**. A UAC confirmation is only displayed if no elevated instance exists yet.
* **Zero-Latency Dispatch**: Low-level keyboard hooks (`WH_KEYBOARD_LL`) run synchronously on the Windows message thread. Our hook consumes the event, injects the mask, and offloads action execution to an asynchronous background worker thread in under **0.05 milliseconds**, ensuring zero system keyboard lag.
* **Debouncing**: A configurable debounce timer (default $300\text{ ms}$) prevents hardware auto-repeat from launching multiple instances when holding down the key.

---

## 🚀 Key Features

* **100% Native C++ (C++17)**: Fully migrated from C# to modern native C++ with zero runtime framework dependencies (no .NET runtime, no external DLLs). Statically linked into a single standalone binary.
* **Ultra-Low Resource Footprint**: Consumes **<1 MB RAM (typically ~600 KB)** and **0% CPU** while idle.
* **Featherweight Executable**: Compact standalone binary of only **~230 KB** with Link-Time dead code stripping and zero external runtime frameworks.
* **100% Native Windows UI**: Built with standard Windows Common Controls (`ComCtl32.dll` v6.0), Win32 dialogs, and native layout containers with zero third-party UI libraries.
* **High-DPI PerMonitorV2 Aware**: Crisp, sharp text and layout across any monitor DPI scaling via `app.manifest` and `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2`.
* **DWM System Theming**: Automatically detects Windows Light/Dark mode and activates `DWMWA_USE_IMMERSIVE_DARK_MODE` on the title bar, reacting dynamically to system theme changes.
* **Single-Instance IPC**: Double-clicking `CopilotRemapper.exe` while running smoothly brings the existing Settings window to the foreground via OS kernel `CreateMutexW` and `CreateEventW`.
* **Portable Mode with Fallback**: Saves configuration locally if writable, or safely falls back to `%APPDATA%\CopilotRemapper\` when running in protected directories.
* **Customizable Actions**:
  * **Launch Application**: Select any `.exe`, `.bat`, `.cmd`, `.ps1`, `.lnk`, or file with optional arguments and working directory.
  * **Run Command / Script**: Execute PowerShell commands seamlessly in the background.
  * **Open Webpage / URL**: Instant one-key access to any URL (e.g., `https://claude.ai`, `https://chatgpt.com`).
* **Live Hardware Status Bar**: Native status bar reports timestamped confirmation when the physical Copilot key is pressed.
* **Windows Auto-Start**: One-click toggle in Settings to automatically launch silently on user login.
* **Python Alternative Included**: Includes `copilot_remapper.py` for Python users, requiring zero external `pip` packages.

---

## 📦 Project Files

```
copilot-remapper/
│
├── .gitignore              # Ignores build output, local config, and compiler tools
├── CMakeLists.txt          # Standard CMake build script (MSVC / GCC / Clang)
├── CopilotRemapper.exe     # Standalone native Windows executable (build output)
├── app.ico                 # Multi-resolution application icon (16x16 to 256x256)
├── app.manifest            # PerMonitorV2 High-DPI application manifest
├── build.bat               # 1-click batch build script (auto-detects / bootstraps compiler)
├── build.ps1               # PowerShell build script alternative
├── copilot_config.json     # Your live config (git-ignored, created on first run)
├── copilot_config.example.json  # Sample config to copy if you want to start manually
├── copilot_remapper.py     # Pure Python implementation (using ctypes)
├── resource.h              # Resource identifier constants
├── resource.rc             # Win32 resource script (manifest, dialog, version info)
│
└── src/
    ├── ActionRunner.cpp    # Action execution, Start Menu mask, modifier release
    ├── ActionRunner.hpp
    ├── Config.cpp          # Config management, zero-dep JSON engine, Task Scheduler auto-start
    ├── Config.hpp
    ├── IconHelper.cpp      # Native high-DPI icon loader & Win32 GDI fallback (zero GDI+ runtime)
    ├── IconHelper.hpp
    ├── KeyboardHook.cpp    # Low-level keyboard hook (WH_KEYBOARD_LL), F23 interceptor
    ├── KeyboardHook.hpp
    ├── ProcessManager.cpp  # Process enumeration, window activation, elevation detection
    ├── ProcessManager.hpp
    ├── SettingsWindow.cpp  # Win32 Settings dialog, ComCtl32, dark theme, live status feedback
    ├── SettingsWindow.hpp
    ├── SingleInstance.cpp  # Named Mutex & Named Event IPC listener
    ├── SingleInstance.hpp
    ├── Theme.cpp           # DWM Immersive Dark Mode & theme change handler
    ├── Theme.hpp
    ├── TrayIcon.cpp        # System tray notification icon & context menu
    ├── TrayIcon.hpp
    └── main.cpp            # Application entry point, CLI arguments, message loop
```

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
| `CopilotRemapper.exe --silent` (or `-s`) | Starts directly minimized in system tray (used by Windows startup) |
| `CopilotRemapper.exe --test` (or `-t`) | Performs a self-test of config loading and exits |
| `CopilotRemapper.exe --install-startup` | Registers silent elevated logon task via Windows Task Scheduler |
| `CopilotRemapper.exe --uninstall-startup` | Removes logon task from Windows Task Scheduler |

---

### 3. Recompiling from Source

You can build `CopilotRemapper.exe` using any standard Windows C++ compiler:

#### Option A: 1-Click Build (Batch or PowerShell)
Simply double-click **`build.bat`** or run in PowerShell:
```powershell
.\build.ps1
```
The build script automatically detects installed compilers (`g++`, `cl.exe`, `clang++`). If no C++ compiler is found in your `PATH`, it automatically fetches and unpacks **w64devkit** (a lightweight ~45 MB standalone GCC toolchain) into `.tools\` using built-in Windows tools, requiring zero manual configuration or administrator rights.

#### Option B: CMake Build
```powershell
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

---

## ⚙ Configuration (`copilot_config.json`)

Settings are stored in JSON format and can be modified via the Settings GUI or a text editor. The file is created automatically on first run. To bootstrap it manually, copy `copilot_config.example.json` to `copilot_config.json`:

```json
{
  "ActionType": "LaunchApp",
  "TargetPath": "wt.exe",
  "Arguments": "",
  "WorkingDirectory": "",
  "RunAsAdmin": false,
  "StartWithWindows": true,
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
