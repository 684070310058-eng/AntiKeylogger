// language: C++, file: antikeylogger.cpp, target: Windows 11, MSVC
// Anti-keylogger v3 - kill + cleanup + restore
// ============================================================
// โปรแกรมนี้ทำหน้าที่เป็น "ไม้กวาด" ล้างร่องรอยของ keylogger
// โดยทำงาน 8 ขั้นตอน: ฆ่า process, ลบ registry, ลบไฟล์,
// คืนค่า shortcut, ลบ task, และตรวจสอบความสะอาดในตอนท้าย
// ============================================================

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <winsock2.h>          // Windows Sockets API (สำหรับตรวจ port)
#include <ws2tcpip.h>          // TCP/IP utilities
#include <iphlpapi.h>          // สำหรับ GetExtendedTcpTable
#include <windows.h>           // Windows API หลัก
#include <shlobj.h>            // Shell API (path)
#include <shobjidl.h>          // COM interface ของ Shell Link
#include <tlhelp32.h>          // Tool Help API (snapshot process)
#include <psapi.h>             // Process Status API
#include <fstream>             // ไฟล์ I/O (อ่าน manifest ของ extension)
#include <string>              // std::string / std::wstring
#include <vector>              // vector (buffer)
#include <iostream>            // cout / wcout
#include <filesystem>          // std::filesystem (C++17) - ลบไฟล์
#include <thread>              // sleep_for
#include <chrono>              // chrono (เวลาหน่วง)
#include <mutex>               // mutex (ล็อก log)

// ลิงก์ library ที่จำเป็น
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")     // ต้องมีเพื่อใช้ GetExtendedTcpTable
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

using namespace std;
namespace fs = std::filesystem;

// ============================================================
// Config — รายการเป้าหมายที่จะลบ
// ============================================================

// ชื่อ process ของ keylogger ที่ต้องฆ่า
const wstring TARGET_PROCESSES[] = {
    L"RobloxKLG.exe",
    L"RobloxKLG-TAB.exe",
    L"KeyloggerBQ.exe",
    L"RobloxKLG",
    L"KeyloggerBQ",
};

// ชื่อไฟล์ log ที่ต้องลบ (อยู่ใน Documents)
const wstring TARGET_FILES[] = {
    L"RobloxKLG.log",
};

// ชื่อโฟลเดอร์ใน %TEMP% ที่ keylogger สร้าง
const wstring TARGET_FOLDERS_TEMP[] = {
    L"SysHelperExt",       // โฟลเดอร์เก็บ Chrome extension
    L"SysHelperProfile",   // โฟลเดอร์ Chrome profile ที่ keylogger สร้าง
};

const int HTTP_PORT = 8888;             // port ที่ keylogger ใช้เป็น HTTP server
const int KILL_RETRY = 3;               // จำนวนครั้งที่ลองฆ่า process ซ้ำ
const int KILL_RETRY_DELAY_MS = 500;    // ระยะห่างระหว่างการ retry (ms)

// ============================================================
// Logging — ฟังก์ชันแสดงข้อความทาง console
// ============================================================
mutex g_logMutex;  // ป้องกันไม่ให้หลาย thread เขียนพร้อมกัน

// เขียนบรรทัดหนึ่งบรรทัดออก console (thread-safe)
void LogLine(const wstring& s) {
    lock_guard<mutex> lk(g_logMutex);
    wcout << s << endl;
    wcout.flush();
}

// log ระดับต่างๆ พร้อม prefix บอกความหมาย
void LogInfo(const wstring& s) { LogLine(L"[*] " + s); }      // ข้อมูลทั่วไป
void LogOK(const wstring& s) { LogLine(L"  [+] " + s); }      // สำเร็จ
void LogSkip(const wstring& s) { LogLine(L"  [-] " + s); }    // ข้ามไป ไม่มีอะไรต้องทำ
void LogErr(const wstring& s) { LogLine(L"  [!] " + s); }     // ข้อผิดพลาด
void LogStep(const wstring& s) { LogLine(L""); LogLine(L"=== " + s + L" ==="); }  // ขึ้นหัวข้อใหม่

// ============================================================
// Path helpers — หา path มาตรฐานของ Windows
// ============================================================

// คืน path ของโฟลเดอร์ Documents (เช่น C:\Users\user\Documents)
wstring GetDocumentsDir() {
    wchar_t p[MAX_PATH] = { 0 };
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, 0, p)))
        return wstring(p);
    return L"";
}

// คืน path ของโฟลเดอร์ %TEMP%
wstring GetTempDir() {
    wchar_t p[MAX_PATH] = { 0 };
    GetTempPathW(MAX_PATH, p);
    return wstring(p);
}

// คืน path ของ %LOCALAPPDATA% (เช่น C:\Users\user\AppData\Local)
wstring GetLocalAppData() {
    wchar_t p[MAX_PATH] = { 0 };
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, p)))
        return wstring(p);
    return L"";
}

// คืน path ของ %APPDATA% (Roaming)
wstring GetAppData() {
    wchar_t p[MAX_PATH] = { 0 };
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, p)))
        return wstring(p);
    return L"";
}

// คืน path ของ user profile (C:\Users\user)
wstring GetUserProfile() {
    wchar_t p[MAX_PATH] = { 0 };
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, p)))
        return wstring(p);
    return L"";
}

// คืน path ของ ProgramData (C:\ProgramData)
wstring GetProgramData() {
    wchar_t p[MAX_PATH] = { 0 };
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, p)))
        return wstring(p);
    return L"";
}

// ============================================================
// Process tools — เครื่องมือจัดการ process
// ============================================================

// ตรวจว่าชื่อ process ตรงกับที่เราต้องการฆ่าหรือไม่ (case-insensitive)
bool IsTargetProcess(const wstring& name) {
    for (auto& t : TARGET_PROCESSES) {
        if (_wcsicmp(name.c_str(), t.c_str()) == 0) return true;
    }
    return false;
}

// ลองฆ่า process เป้าหมายหนึ่งรอบ (verbose = แสดง log หรือไม่)
int KillTargetProcessesOnce(bool verbose) {
    int killed = 0;

    // Snapshot รายการ process ทั้งหมดในระบบ
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe = { 0 };
    pe.dwSize = sizeof(pe);

    // วนลูปทุก process
    if (Process32FirstW(snap, &pe)) {
        do {
            if (IsTargetProcess(pe.szExeFile)) {
                // เปิด handle ของ process เป้าหมายเพื่อสั่ง terminate
                HANDLE h = OpenProcess(
                    PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION,
                    FALSE, pe.th32ProcessID);
                if (h) {
                    if (TerminateProcess(h, 0)) {
                        // ฆ่าสำเร็จ
                        if (verbose) LogOK(L"killed: " + wstring(pe.szExeFile) +
                            L" (PID " + to_wstring(pe.th32ProcessID) + L")");
                        killed++;
                    }
                    else {
                        if (verbose) LogErr(L"terminate fail: " + wstring(pe.szExeFile) +
                            L" err=" + to_wstring(GetLastError()));
                    }
                    CloseHandle(h);
                }
                else {
                    // เปิด handle ไม่ได้ (มักเป็นเพราะไม่ได้รัน as admin)
                    DWORD err = GetLastError();
                    if (verbose) {
                        if (err == ERROR_ACCESS_DENIED)
                            LogErr(L"access denied: " + wstring(pe.szExeFile) +
                                L" (run as Administrator)");
                        else
                            LogErr(L"open fail: " + wstring(pe.szExeFile) +
                                L" err=" + to_wstring(err));
                    }
                }
            }
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return killed;
}

// ลองฆ่า process หลายรอบ — กันกรณี keylogger respawn ตัวเอง
int KillTargetProcesses() {
    int totalKilled = 0;

    // ลอง 3 รอบ ห่างกัน 500ms
    for (int i = 0; i < KILL_RETRY; i++) {
        int n = KillTargetProcessesOnce(i == 0);  // รอบแรกแสดง log
        totalKilled += n;
        if (n == 0) break;  // ไม่มี process แล้ว → หยุด
        this_thread::sleep_for(chrono::milliseconds(KILL_RETRY_DELAY_MS));
    }

    // ตรวจอีกครั้ง: ถ้ายังเจอ → keylogger กำลัง respawn → ฆ่าแบบ force
    if (totalKilled > 0 && KillTargetProcessesOnce(false) > 0) {
        LogErr(L"process respawning - final force kill");
        KillTargetProcessesOnce(true);
    }

    return totalKilled;
}

// ฆ่า process ที่ bind port 8888 อยู่ (อาจเป็น keylogger)
int KillPort8888() {
    int killed = 0;

    // ขั้นแรก: ขอ size ของ buffer ก่อน
    DWORD size = 0;
    GetExtendedTcpTable(NULL, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (size == 0) return 0;

    // ขั้นที่สอง: ดึงตาราง TCP listening ทั้งหมด
    vector<BYTE> buf(size);
    if (GetExtendedTcpTable(buf.data(), &size, FALSE, AF_INET,
        TCP_TABLE_OWNER_PID_LISTENER, 0) != NO_ERROR)
        return 0;

    // วนดูทุก entry — หา port 8888
    MIB_TCPTABLE_OWNER_PID* tbl = (MIB_TCPTABLE_OWNER_PID*)buf.data();
    for (DWORD i = 0; i < tbl->dwNumEntries; i++) {
        DWORD port = ntohs((u_short)tbl->table[i].dwLocalPort);
        if (port == HTTP_PORT) {
            // เจอ! ดึง PID ของเจ้าของ port แล้วฆ่า
            DWORD pid = tbl->table[i].dwOwningPid;
            HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
            if (h) {
                if (TerminateProcess(h, 0)) {
                    LogOK(L"killed port " + to_wstring(HTTP_PORT) +
                        L" owner (PID " + to_wstring(pid) + L")");
                    killed++;
                }
                CloseHandle(h);
            }
        }
    }
    return killed;
}

// ============================================================
// Delete file / folder — ลบไฟล์และโฟลเดอร์
// ============================================================

// ลบไฟล์แบบ force — ถ้าลบไม่ได้ให้ schedule ลบตอน reboot
bool DeleteFileForce(const wstring& path) {
    if (!fs::exists(path)) return false;

    // ลองลบตรงๆ
    if (DeleteFileW(path.c_str())) return true;

    // ลบตรงๆ ไม่ได้ (ไฟล์อาจถูกล็อก) → ย้ายชื่อก่อนแล้วลบ
    wstring tmp = path + L".del_" + to_wstring(GetTickCount());
    if (MoveFileExW(path.c_str(), tmp.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        if (DeleteFileW(tmp.c_str())) return true;
        // ถ้าย้ายสำเร็จแต่ลบไม่ได้ → schedule ตอน reboot
        MoveFileExW(tmp.c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
        return true;
    }

    // ย้ายไม่ได้เลย → schedule ลบตอน reboot โดยตรง
    if (MoveFileExW(path.c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT))
        return true;

    return false;
}

// ลบโฟลเดอร์แบบ recursive
bool DeleteFolderForce(const wstring& path) {
    if (!fs::exists(path)) return false;

    try {
        // ล้าง read-only attribute ของทุกไฟล์ก่อน
        for (auto& p : fs::recursive_directory_iterator(path)) {
            SetFileAttributesW(p.path().c_str(), FILE_ATTRIBUTE_NORMAL);
        }
        // ลบทั้งโฟลเดอร์
        fs::remove_all(path);
        return true;
    }
    catch (...) {
        return false;  // อาจถูกล็อกโดย Chrome
    }
}

// ============================================================
// Chrome shortcut restore — คืนค่า shortcut ที่ keylogger แก้
// ============================================================

// ลบ flag --load-extension และ --disable-features ออกจาก argument
wstring StripLoadExtension(const wstring& args) {
    wstring out = args;

    // ลบ --load-extension="..." (รวม quote ทั้งคู่)
    while (true) {
        auto p = out.find(L"--load-extension=");
        if (p == wstring::npos) break;
        auto q = out.find(L"\"", p + 17);
        if (q == wstring::npos) {
            out.erase(p);
        }
        else {
            auto sp = out.find(L" ", q + 1);
            if (sp == wstring::npos) sp = out.size();
            out.erase(p, sp - p);
        }
    }

    // ลบ --disable-features=DisableLoadExtensionCommandLineSwitch
    while (true) {
        auto p = out.find(L"--disable-features=DisableLoadExtensionCommandLineSwitch");
        if (p == wstring::npos) break;
        auto sp = out.find(L" ", p);
        if (sp == wstring::npos) sp = out.size();
        out.erase(p, sp - p);
    }

    // ลดช่องว่างซ้อน
    while (out.find(L"  ") != wstring::npos) {
        out.replace(out.find(L"  "), 2, L" ");
    }

    // trim หัว-ท้าย
    size_t s = out.find_first_not_of(L" ");
    size_t e = out.find_last_not_of(L" ");
    if (s == wstring::npos) return L"";
    return out.substr(s, e - s + 1);
}

// คืนค่า shortcut เดียว (ใช้ COM interface ของ Windows)
int RestoreShortcut(const wstring& lnkPath) {
    // สร้าง Shell Link object
    IShellLinkW* psl = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL,
        CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&psl);
    if (FAILED(hr)) return 0;

    // ขอ IPersistFile interface เพื่อโหลด/บันทึกไฟล์ .lnk
    IPersistFile* ppf = nullptr;
    hr = psl->QueryInterface(IID_IPersistFile, (void**)&ppf);
    if (FAILED(hr)) { psl->Release(); return 0; }

    // โหลดไฟล์ .lnk เข้ามาในหน่วยความจำ
    hr = ppf->Load(lnkPath.c_str(), STGM_READWRITE);
    if (FAILED(hr)) { ppf->Release(); psl->Release(); return 0; }

    // อ่าน arguments ปัจจุบัน
    wchar_t args[8192] = { 0 };
    psl->GetArguments(args, 8192);
    wstring oldArgs(args);

    // ถ้าไม่มี flag ที่ keylogger ใส่ → ไม่ต้องแก้อะไร
    if (oldArgs.find(L"--load-extension") == wstring::npos &&
        oldArgs.find(L"DisableLoadExtensionCommandLineSwitch") == wstring::npos) {
        ppf->Release(); psl->Release();
        return 0;
    }

    // ลบ flag ออกแล้วบันทึกกลับ
    wstring newArgs = StripLoadExtension(oldArgs);
    psl->SetArguments(newArgs.c_str());
    ppf->Save(lnkPath.c_str(), TRUE);

    LogOK(L"restored: " + lnkPath);
    wcout << L"      old: " << oldArgs << endl;
    wcout << L"      new: " << newArgs << endl;

    ppf->Release();
    psl->Release();
    return 1;
}

// คืนค่า shortcut ของ Chrome/Edge ทุกอันในเครื่อง
int RestoreAllChromeShortcuts() {
    wstring profile = GetUserProfile();
    wstring appData = GetAppData();
    wstring programData = GetProgramData();

    // รายการโฟลเดอร์ที่มักมี shortcut ของ Chrome/Edge
    vector<wstring> dirs;
    dirs.push_back(profile + L"\\Desktop");                                     // Desktop
    dirs.push_back(profile + L"\\OneDrive\\Desktop");                           // OneDrive Desktop
    dirs.push_back(appData + L"\\Microsoft\\Windows\\Start Menu\\Programs");    // Start Menu
    dirs.push_back(appData + L"\\...\\TaskBar");                                // Taskbar
    dirs.push_back(programData + L"\\Microsoft\\Windows\\Start Menu\\Programs"); // All Users Start Menu

    int restored = 0;

    // วนทุกโฟลเดอร์
    for (auto& dir : dirs) {
        if (!fs::exists(dir)) continue;
        try {
            // วนทุกไฟล์ในโฟลเดอร์แบบ recursive
            for (auto& entry : fs::recursive_directory_iterator(dir)) {
                wstring path = entry.path().wstring();
                if (path.size() < 4) continue;

                // กรองเฉพาะไฟล์ .lnk
                wstring lower = path;
                for (auto& c : lower) c = towlower(c);
                if (lower.substr(lower.size() - 4) != L".lnk") continue;

                // เช็คว่าชื่อมีคำว่า chrome หรือ edge
                wstring name = entry.path().filename().wstring();
                wstring lname = name;
                for (auto& c : lname) c = towlower(c);

                if (lname.find(L"chrome") != wstring::npos ||
                    lname.find(L"edge") != wstring::npos) {
                    restored += RestoreShortcut(path);
                }
            }
        }
        catch (...) {}
    }

    return restored;
}

// ============================================================
// Registry cleanup — ลบ autorun ที่ keylogger สร้าง
// ============================================================
int CleanRegistry() {
    int cleaned = 0;

    HKEY hKey;

    // 1. ลบ HKCU\...\Run\WindowsUpdate (รันโดย user)
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE | KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {

        if (RegDeleteValueW(hKey, L"WindowsUpdate") == ERROR_SUCCESS) {
            LogOK(L"deleted HKCU Run WindowsUpdate");
            cleaned++;
        }
        else {
            LogSkip(L"WindowsUpdate not found in HKCU");
        }
        RegCloseKey(hKey);
    }

    // 2. ลบ HKLM\...\Run\WindowsUpdate (รันโดยทุก user — ต้อง admin)
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE | KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {

        if (RegDeleteValueW(hKey, L"WindowsUpdate") == ERROR_SUCCESS) {
            LogOK(L"deleted HKLM Run WindowsUpdate");
            cleaned++;
        }
        RegCloseKey(hKey);
    }

    return cleaned;
}

// ============================================================
// Clean files — ลบไฟล์และโฟลเดอร์ที่ keylogger สร้าง
// ============================================================
int CleanFiles() {
    int cleaned = 0;

    wstring docs = GetDocumentsDir();
    wstring temp = GetTempDir();
    wstring localAppData = GetLocalAppData();

    // 1. ลบไฟล์ log ใน Documents
    if (!docs.empty()) {
        for (auto& f : TARGET_FILES) {
            wstring p = docs + L"\\" + f;
            if (fs::exists(p) && DeleteFileForce(p)) {
                LogOK(L"deleted: " + p);
                cleaned++;
            }
        }
    }

    // 2. ลบไฟล์ temp ที่เกิดจากการส่งเมล
    {
        wstring files[] = {
            L"C:\\Windows\\Temp\\email_body.txt",
            L"C:\\Windows\\Temp\\debug_last.eml",
        };
        for (auto& p : files) {
            if (fs::exists(p) && DeleteFileForce(p)) {
                LogOK(L"deleted: " + p);
                cleaned++;
            }
        }
    }

    // 3. ลบโฟลเดอร์ SysHelperExt และ SysHelperProfile ใน %TEMP%
    for (auto& d : TARGET_FOLDERS_TEMP) {
        wstring p = temp + d;
        if (fs::exists(p)) {
            if (DeleteFolderForce(p)) {
                LogOK(L"deleted folder: " + p);
                cleaned++;
            }
            else {
                // ถ้าลบไม่ได้ (อาจถูกล็อก) → schedule ลบตอน reboot
                LogErr(L"locked (reboot): " + p);
                try {
                    for (auto& entry : fs::recursive_directory_iterator(p)) {
                        if (fs::is_regular_file(entry.path())) {
                            MoveFileExW(entry.path().c_str(), NULL,
                                MOVEFILE_DELAY_UNTIL_REBOOT);
                        }
                    }
                }
                catch (...) {}
            }
        }
    }

    // 4. ลบโฟลเดอร์ %LOCALAPPDATA%\SysHelper (extension ที่เขียนใน v2)
    {
        wstring sysHelper = localAppData + L"\\SysHelper";
        if (fs::exists(sysHelper)) {
            if (DeleteFolderForce(sysHelper)) {
                LogOK(L"deleted folder: " + sysHelper);
                cleaned++;
            }
            else {
                LogErr(L"locked: " + sysHelper);
            }
        }
    }

    // 5. ลบ extension ที่ฝังเข้าไปใน Chrome/Edge profile
    {
        // Chrome
        wstring chromeExt = localAppData +
            L"\\Google\\Chrome\\User Data\\Default\\Extensions";
        if (fs::exists(chromeExt)) {
            for (auto& entry : fs::directory_iterator(chromeExt)) {
                try {
                    // อ่าน manifest.json เพื่อเช็คว่าเป็น "Sys Helper" ไหม
                    wstring manifest = entry.path().wstring() +
                        L"\\1.0.0_0\\manifest.json";
                    if (fs::exists(manifest)) {
                        ifstream f(manifest);
                        stringstream ss;
                        ss << f.rdbuf();
                        if (ss.str().find("Sys Helper") != string::npos) {
                            if (DeleteFolderForce(entry.path().wstring())) {
                                LogOK(L"deleted chrome ext: " + entry.path().wstring());
                                cleaned++;
                            }
                            else {
                                LogErr(L"locked ext: " + entry.path().wstring());
                            }
                        }
                    }
                }
                catch (...) {}
            }
        }

        // Edge (โครงสร้างเหมือน Chrome)
        wstring edgeExt = localAppData +
            L"\\Microsoft\\Edge\\User Data\\Default\\Extensions";
        if (fs::exists(edgeExt)) {
            for (auto& entry : fs::directory_iterator(edgeExt)) {
                try {
                    wstring manifest = entry.path().wstring() +
                        L"\\1.0.0_0\\manifest.json";
                    if (fs::exists(manifest)) {
                        ifstream f(manifest);
                        stringstream ss;
                        ss << f.rdbuf();
                        if (ss.str().find("Sys Helper") != string::npos) {
                            if (DeleteFolderForce(entry.path().wstring())) {
                                LogOK(L"deleted edge ext: " + entry.path().wstring());
                                cleaned++;
                            }
                        }
                    }
                }
                catch (...) {}
            }
        }
    }

    return cleaned;
}

// ============================================================
// Scheduled tasks — ลบ task ที่ keylogger สร้าง
// ============================================================
void CleanScheduledTasks() {
    const wchar_t* names[] = {
        L"RobloxKLG",
        L"KeyloggerBQ",
        L"WindowsUpdate",
    };

    // เรียก schtasks.exe ของ Windows เพื่อลบ task
    for (auto n : names) {
        wstring cmd = L"schtasks /Delete /TN \"" + wstring(n) + L"\" /F >NUL 2>&1";
        int rc = _wsystem(cmd.c_str());
        if (rc == 0) {
            LogOK(L"scheduled task deleted: " + wstring(n));
        }
    }
}

// ============================================================
// Close browsers — ปิด Chrome/Edge (เพื่อ unlock extension folder)
// ============================================================
int CloseBrowsers() {
    int closed = 0;
    const wchar_t* names[] = { L"chrome.exe", L"msedge.exe" };

    for (auto name : names) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) continue;

        PROCESSENTRY32W pe = { 0 };
        pe.dwSize = sizeof(pe);

        if (Process32FirstW(snap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, name) == 0) {
                    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (h) {
                        if (TerminateProcess(h, 0)) closed++;
                        CloseHandle(h);
                    }
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }

    if (closed > 0) {
        LogOK(L"closed " + to_wstring(closed) + L" browser process(es)");
        // รอให้ file handle คลายก่อนลบโฟลเดอร์
        this_thread::sleep_for(chrono::milliseconds(1000));
    }

    return closed;
}

// ============================================================
// Verify — ตรวจสอบว่าลบครบจริงหรือไม่
// ============================================================
struct VerifyResult {
    bool clean;    // true = สะอาด
    int issues;    // จำนวนปัญหาที่เจอ
};

VerifyResult VerifyClean() {
    VerifyResult r;
    r.clean = true;
    r.issues = 0;

    // 1. ตรวจว่า process ยังรันอยู่ไหม
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = { 0 };
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do {
                if (IsTargetProcess(pe.szExeFile)) {
                    LogErr(L"process still running: " + wstring(pe.szExeFile));
                    r.clean = false;
                    r.issues++;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }

    // 2. ตรวจว่าไฟล์/โฟลเดอร์ยังอยู่ไหม
    wstring docs = GetDocumentsDir();
    wstring temp = GetTempDir();
    wstring localAppData = GetLocalAppData();

    struct Check { wstring path; wstring label; };
    Check checks[] = {
        { docs + L"\\RobloxKLG.log", L"log" },
        { temp + L"SysHelperExt", L"ext-temp" },
        { temp + L"SysHelperProfile", L"profile-temp" },
        { localAppData + L"\\SysHelper", L"SysHelper dir" },
    };

    for (auto& c : checks) {
        if (fs::exists(c.path)) {
            LogErr(c.label + L" still exists: " + c.path);
            r.clean = false;
            r.issues++;
        }
        else {
            LogOK(c.label + L" removed");
        }
    }

    // 3. ตรวจ registry
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
        wchar_t v[MAX_PATH] = { 0 };
        DWORD sz = sizeof(v);
        if (RegQueryValueExW(hKey, L"WindowsUpdate", NULL, NULL,
            (LPBYTE)v, &sz) == ERROR_SUCCESS) {
            LogErr(L"registry WindowsUpdate still present");
            r.clean = false;
            r.issues++;
        }
        else {
            LogOK(L"registry WindowsUpdate removed");
        }
        RegCloseKey(hKey);
    }

    // 4. ตรวจ port 8888 ว่ามี process ยึดอยู่ไหม
    {
        DWORD size = 0;
        GetExtendedTcpTable(NULL, &size, FALSE, AF_INET,
            TCP_TABLE_OWNER_PID_LISTENER, 0);
        if (size > 0) {
            vector<BYTE> buf(size);
            if (GetExtendedTcpTable(buf.data(), &size, FALSE, AF_INET,
                TCP_TABLE_OWNER_PID_LISTENER, 0) == NO_ERROR) {
                MIB_TCPTABLE_OWNER_PID* tbl = (MIB_TCPTABLE_OWNER_PID*)buf.data();
                bool found = false;
                for (DWORD i = 0; i < tbl->dwNumEntries; i++) {
                    DWORD port = ntohs((u_short)tbl->table[i].dwLocalPort);
                    if (port == HTTP_PORT) { found = true; break; }
                }
                if (found) {
                    LogErr(L"port 8888 still in use");
                    r.clean = false;
                    r.issues++;
                }
                else {
                    LogOK(L"port 8888 free");
                }
            }
        }
    }

    return r;
}

// ============================================================
// main — ควบคุมลำดับ 8 ขั้นตอน
// ============================================================
int main() {
    SetConsoleTitleW(L"Anti-Keylogger v3");
    SetConsoleOutputCP(CP_UTF8);
    wcout.imbue(locale(""));

    LogLine(L"=============================================");
    LogLine(L"   Anti-Keylogger v3");
    LogLine(L"=============================================");

    // --- ตรวจสอบสิทธิ์ admin ---
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuth, 2,
        SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    if (!isAdmin) {
        LogErr(L"recommend: run as Administrator");
    }

    // --- Step 1: ฆ่า process ---
    LogStep(L"Step 1/8: kill keylogger process");
    int killed = KillTargetProcesses();
    if (killed == 0) LogSkip(L"no target process running");
    else LogInfo(L"total killed: " + to_wstring(killed));

    // --- Step 2: ฆ่า process ที่ยึด port 8888 ---
    LogStep(L"Step 2/8: kill process on port 8888");
    int portKilled = KillPort8888();
    if (portKilled == 0) LogSkip(L"port 8888 not held");

    // --- Step 3: ปิด Chrome/Edge ---
    LogStep(L"Step 3/8: close browsers");
    CloseBrowsers();

    // --- Step 4: ลบ registry autorun ---
    LogStep(L"Step 4/8: clean registry");
    CleanRegistry();

    // --- Step 5: คืนค่า shortcut ---
    LogStep(L"Step 5/8: restore Chrome/Edge shortcuts");
    int restored = RestoreAllChromeShortcuts();
    if (restored == 0) LogSkip(L"no shortcut to restore");
    else LogInfo(L"total restored: " + to_wstring(restored));

    // --- Step 6: ลบไฟล์และโฟลเดอร์ ---
    LogStep(L"Step 6/8: delete files and folders");
    int filesCleaned = CleanFiles();
    if (filesCleaned == 0) LogSkip(L"no files to delete");
    else LogInfo(L"total deleted: " + to_wstring(filesCleaned));

    // --- Step 7: ลบ scheduled tasks ---
    LogStep(L"Step 7/8: clean scheduled tasks");
    CleanScheduledTasks();

    // --- Step 8: ตรวจสอบความสะอาด ---
    LogStep(L"Step 8/8: verify clean state");
    VerifyResult vr = VerifyClean();

    // --- แสดงสรุป ---
    LogLine(L"");
    LogLine(L"=============================================");
    LogLine(L"   SUMMARY");
    LogLine(L"   Processes killed : " + to_wstring(killed + portKilled));
    LogLine(L"   Shortcuts fixed  : " + to_wstring(restored));
    LogLine(L"   Files deleted    : " + to_wstring(filesCleaned));
    if (vr.clean) {
        LogLine(L"   STATUS           : CLEAN");
    }
    else {
        LogLine(L"   STATUS           : " + to_wstring(vr.issues) +
            L" issue(s) - reboot required");
    }
    LogLine(L"=============================================");
    LogLine(L"");
    LogLine(L"Press ENTER to exit...");
    cin.get();

    return 0;
}