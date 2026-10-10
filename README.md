# AntiKeylogger

*(อ่านภาษาไทยด้านล่าง / Thai translation below)*

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

---

# คำอธิบายโปรเจกต์ (ภาษาไทย)

**AntiKeylogger** เป็นโปรแกรมอรรถประโยชน์ที่เขียนด้วย C++ ทำหน้าที่เหมือน "ไม้กวาด" เพื่อตามลบร่องรอยและทำความสะอาดระบบ Windows ที่ติดมัลแวร์ `KeyloggerBQ` (หรือ `RobloxKLG`)

## ฟีเจอร์และการทำงาน
โปรแกรมนี้จะทำงาน 8 ขั้นตอนเพื่อทำความสะอาดเครื่องอย่างหมดจด:
1. **ฆ่า Process (Kill Processes):** บังคับปิดการทำงานของ Keylogger ที่แอบรันอยู่เบื้องหลัง (`RobloxKLG.exe`, `KeyloggerBQ.exe`) รวมถึงฆ่า Process ใดๆ ก็ตามที่กำลังใช้ Port 8888
2. **ล้าง Registry (Registry Cleanup):** ลบการตั้งค่า Autorun ที่ Keylogger แอบสร้างไว้เพื่อเปิดตัวเองตอนเปิดเครื่อง (`WindowsUpdate`)
3. **ลบไฟล์ขยะ (Delete Files):** ตามลบไฟล์ Log (.log) ที่แอบบันทึกการพิมพ์ และโฟลเดอร์ Temp (เช่น SysHelper) ที่ตัวมัลแวร์สร้างทิ้งไว้
4. **ลบ Extension อันตราย:** ตรวจหาและลบ Extension เถื่อน ("Sys Helper") ที่ถูกแอบฝังไว้ในโปรไฟล์ของ Google Chrome และ Microsoft Edge
5. **ซ่อมแซม Shortcut:** สแกนหา Shortcut ของ Chrome และ Edge ทั่วทั้งเครื่อง (Desktop, Start Menu, Taskbar) และลบคำสั่งฝัง Extension ทิ้ง เพื่อให้เบราว์เซอร์กลับมาทำงานปกติ
6. **ลบ Task Scheduler:** ลบตารางการทำงานอัตโนมัติ (Scheduled Tasks) ที่มัลแวร์สร้างไว้หลอกๆ
7. **ปิดเบราว์เซอร์:** บังคับปิด Chrome และ Edge ชั่วคราว เพื่อให้ระบบยอมปลดล็อกและอนุญาตให้ลบไฟล์ Extension ได้
8. **ตรวจสอบความเรียบร้อย (Verify):** รันการสแกนระบบอีกครั้งในตอนท้าย เพื่อยืนยันว่าไม่มีมัลแวร์ตัวนี้หลงเหลืออยู่แล้วจริงๆ

## การใช้งาน (Usage)
แนะนำอย่างยิ่งให้เปิดโปรแกรมนี้ด้วยสิทธิ์ **Run as Administrator** (ผู้ดูแลระบบ) เพื่อให้โปรแกรมมีสิทธิ์เพียงพอในการบังคับปิด Process และเข้าถึง Registry ของระบบได้ครบถ้วน
