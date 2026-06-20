# BaseStation

ESP-IDF project (ESP32-P4, IDF v5.5.4).

## Build

ESP-IDF lives at `C:\esp\v5.5.4\esp-idf`, not on PATH. `idf.py` only works in PowerShell
(the bash tool's MSYS/Git Bash is explicitly rejected by ESP-IDF's export script).
Environment variables don't persist between tool calls, so activate IDF and build in one
PowerShell invocation:

```powershell
cd "c:\Users\Harald\Documents\Projekte\Wetterstation-ESP32-3.0\Code\BaseStation"; & "C:\esp\v5.5.4\esp-idf\export.ps1" > $null; idf.py build
```
