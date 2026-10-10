# AntiKeylogger

AntiKeylogger is a C++ cleanup tool ("broom") designed to remove the `KeyloggerBQ` / `RobloxKLG` malware from an infected Windows system.

## Features
This tool performs an 8-step cleanup process:
1. **Kill Processes:** Terminates active keylogger processes (`RobloxKLG.exe`, `KeyloggerBQ.exe`, etc.) and kills any process binding to the keylogger's local HTTP port (8888).
2. **Registry Cleanup:** Removes autorun entries from the registry (`HKCU` and `HKLM` `\Run\WindowsUpdate`).
3. **Delete Files:** Removes log files, temp files, and folders created by the keylogger.
4. **Remove Extension:** Finds and deletes the injected "Sys Helper" extension from Google Chrome and Microsoft Edge profiles.
5. **Restore Shortcuts:** Scans all desktop, start menu, and taskbar shortcuts for Chrome and Edge, and strips out malicious `--load-extension` flags added by the keylogger.
6. **Task Scheduler:** Deletes scheduled tasks created by the malware.
7. **Close Browsers:** Force closes browsers to unlock extension folders for deletion.
8. **Verify:** Runs a final system check to ensure no traces of the malware remain.

## Usage
It is highly recommended to run this tool as **Administrator** to ensure it has sufficient privileges to terminate processes and clean registry keys located in HKLM.

## Requirements
- Visual Studio (C++)
- Windows OS
