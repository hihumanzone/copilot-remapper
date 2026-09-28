"""
Copilot Key Remapper (Python Edition)
=====================================
A lightweight, zero-third-party-dependency Python implementation that intercepts
the physical Copilot key macro (Win + Shift + F23) via a Win32 Low-Level Keyboard
Hook (WH_KEYBOARD_LL) and executes custom user actions:
  1. Launch Application / File
  2. Open Website / URL
  3. Run PowerShell Command

Zero pip packages needed — uses built-in ctypes, json, subprocess, and os.
"""

import sys
import os
import json
import time
import ctypes
from ctypes import wintypes
import subprocess
import threading

# Win32 Constants
WH_KEYBOARD_LL = 13
WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
WM_SYSKEYDOWN = 0x0104
WM_SYSKEYUP = 0x0105

VK_F23 = 0x86
VK_LWIN = 0x5B
VK_LSHIFT = 0xA0
VK_UNASSIGNED_E8 = 0xE8
KEYEVENTF_KEYUP = 0x0002

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

class KBDLLHOOKSTRUCT(ctypes.Structure):
    _fields_ = [
        ("vkCode", wintypes.DWORD),
        ("scanCode", wintypes.DWORD),
        ("flags", wintypes.DWORD),
        ("time", wintypes.DWORD),
        ("dwExtraInfo", ctypes.c_size_t),
    ]

HOOKPROC = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_int, wintypes.WPARAM, ctypes.POINTER(KBDLLHOOKSTRUCT))

def get_config_path():
    base_dir = os.path.dirname(os.path.abspath(__file__))
    local_path = os.path.join(base_dir, "copilot_config.json")

    try:
        test_file = os.path.join(base_dir, f".write_test_{os.getpid()}")
        with open(test_file, "w") as f:
            f.write("test")
        os.remove(test_file)
        return local_path
    except Exception:
        appdata = os.path.join(os.environ.get("APPDATA", base_dir), "CopilotRemapper")
        os.makedirs(appdata, exist_ok=True)
        return os.path.join(appdata, "copilot_config.json")

CONFIG_PATH = get_config_path()

def load_config():
    default_config = {
        "ActionType": "LaunchApp",
        "TargetPath": "wt.exe",
        "Arguments": "",
        "WorkingDirectory": "",
        "RunAsAdmin": False,
        "StartWithWindows": False,
        "DebounceMs": 300
    }
    if os.path.exists(CONFIG_PATH):
        try:
            with open(CONFIG_PATH, "r", encoding="utf-8") as f:
                data = json.load(f)
                default_config.update(data)
                return default_config
        except Exception as e:
            print(f"[Warning] Failed to read config: {e}")
    return default_config

def mask_start_menu():
    """Inject 0xE8 mask key to prevent Start Menu popup when LWin is released."""
    user32.keybd_event(VK_UNASSIGNED_E8, 0, 0, 0)
    user32.keybd_event(VK_UNASSIGNED_E8, 0, KEYEVENTF_KEYUP, 0)

def release_modifiers():
    """Logically release LShift and LWin so target actions don't inherit them."""
    user32.keybd_event(VK_LSHIFT, 0, KEYEVENTF_KEYUP, 0)
    user32.keybd_event(VK_LWIN, 0, KEYEVENTF_KEYUP, 0)

def execute_action(config):
    action_type = config.get("ActionType", "LaunchApp")
    target = config.get("TargetPath", "wt.exe")
    args = config.get("Arguments", "")
    work_dir = config.get("WorkingDirectory", "")
    run_as_admin = config.get("RunAsAdmin", False)

    print(f"[{time.strftime('%H:%M:%S')}] Executing: {action_type} -> '{target}'")

    def _run():
        try:
            release_modifiers()

            if action_type == "OpenUrl":
                url = target if ("://" in target) else f"https://{target}"
                os.startfile(url)

            elif action_type == "RunCommand":
                expanded_cmd = os.path.expandvars(target)
                if run_as_admin:
                    ctypes.windll.shell32.ShellExecuteW(None, "runas", "powershell.exe",
                                                        f'-NoProfile -ExecutionPolicy Bypass -Command "{expanded_cmd}"',
                                                        work_dir or None, 0)
                else:
                    subprocess.Popen(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", expanded_cmd],
                                     cwd=work_dir or None)

            else:
                expanded_target = os.path.expandvars(target)
                expanded_args = os.path.expandvars(args)
                if run_as_admin:
                    ctypes.windll.shell32.ShellExecuteW(None, "runas", expanded_target, expanded_args, work_dir or None, 1)
                else:
                    cmd = f'"{expanded_target}" {expanded_args}'.strip()
                    subprocess.Popen(cmd, shell=True, cwd=work_dir or None)
        except Exception as err:
            print(f"[!] Action error: {err}")

    threading.Thread(target=_run, daemon=True).start()

class CopilotHook:
    def __init__(self):
        self.hook_id = None
        self.last_trigger = 0.0
        self.proc = HOOKPROC(self.hook_callback)

    def hook_callback(self, nCode, wParam, lParam):
        if nCode >= 0:
            try:
                kb = lParam.contents
                if kb.vkCode == VK_F23:
                    if wParam in (WM_KEYDOWN, WM_SYSKEYDOWN):
                        mask_start_menu()

                        cfg = load_config()
                        now = time.time()
                        debounce_sec = max(50, cfg.get("DebounceMs", 300)) / 1000.0

                        if now - self.last_trigger >= debounce_sec:
                            self.last_trigger = now
                            execute_action(cfg)

                        return 1

                    elif wParam in (WM_KEYUP, WM_SYSKEYUP):
                        return 1
            except Exception as e:
                print(f"[Hook Error] {e}")

        return user32.CallNextHookEx(self.hook_id, nCode, wParam, lParam)

    def start(self):
        self.hook_id = user32.SetWindowsHookExW(
            WH_KEYBOARD_LL,
            self.proc,
            kernel32.GetModuleHandleW(None),
            0
        )
        if not self.hook_id:
            print(f"[!] SetWindowsHookEx failed: {kernel32.GetLastError()}")
            return False

        cfg = load_config()
        print("==========================================================")
        print("  Copilot Key Remapper (Python)")
        print(f"  Configuration: {cfg.get('ActionType')} -> {cfg.get('TargetPath')}")
        print(f"  Config File:   {CONFIG_PATH}")
        print("  Status:        HOOK ACTIVE! Press Copilot key to test.")
        print("  Press Ctrl+C to stop.")
        print("==========================================================")

        msg = wintypes.MSG()
        try:
            while user32.GetMessageW(ctypes.byref(msg), None, 0, 0) != 0:
                user32.TranslateMessage(ctypes.byref(msg))
                user32.DispatchMessageW(ctypes.byref(msg))
        except KeyboardInterrupt:
            print("\n[+] Exiting...")
        finally:
            self.stop()
        return True

    def stop(self):
        if self.hook_id:
            user32.UnhookWindowsHookEx(self.hook_id)
            self.hook_id = None
            print("[+] Hook uninstalled cleanly.")

def main():
    if "--status" in sys.argv:
        cfg = load_config()
        print(json.dumps(cfg, indent=2))
        return
    if "--test" in sys.argv:
        cfg = load_config()
        print("[*] Running test action...")
        execute_action(cfg)
        time.sleep(1.0)
        return
    if "--help" in sys.argv or "-h" in sys.argv:
        print("Usage: python copilot_remapper.py [--status | --test | --help]")
        return

    hook = CopilotHook()
    hook.start()

if __name__ == "__main__":
    main()
