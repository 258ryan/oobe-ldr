# Vista OOBE replacement on Windows 10 (VM only)

**Use only in a disposable VM, after taking a snapshot.** The installer mirrors the complete staged Vista OOBE folder
over `%WINDIR%\System32\oobe`, removing Windows OOBE files that are not in the Vista source. The staged `msoobe.exe`
is this project's launcher; the Vista wizard is kept as `vistamsoobe.exe`. The original Windows OOBE folder and its
file ACLs are saved for fallback and rollback. This does not replace Windows setup components located outside that
folder, and Vista's setup host may not support every Windows 10 setup phase.
Windows servicing can replace these files, and replacing the folder can prevent setup from starting; keep the VM snapshot.

## What it does

* Windows' OOBE loader starts the replacement `msoobe.exe` and waits for its exit code.
* The replacement starts the Vista wizard, renamed `vistamsoobe.exe`, with the original arguments, waits, and returns its exit code.
* The Vista OOBE folder's `setup.exe` and all other source files, language folders, and dependencies are included in
  the system-folder replacement. Windows servicing and specialize/device-setup behavior may be affected.
* If the Vista wizard cannot start or fails, the launcher attempts to start the preserved Windows wizard from
  `C:\vista-oobe\original-oobe\msoobe.exe`. This is only a wizard-level fallback; it does not restore the replaced
  folder or guarantee Windows setup can continue. Use the restore script or revert the snapshot for a full rollback.
* The Vista wizard imports `winbrand.dll`; the proxy `winbrand.dll` staged beside it is loaded first. It forwards
  `BrandingLoadBitmap` to the real one, fixes DUser's `SetGadgetRootInfo` (old 0x20-byte structure -> 0x30),
  records where the ordinal imports land (`shell32 #262/#754`, `userenv #206`, `Input #104-108`) and logs
  crashes with a call stack.
* Every Vista `msoobe.exe.mui` language folder found in the supplied OOBE source is staged beside
  `vistamsoobe.exe` under the same culture name (for example `en-US\vistamsoobe.exe.mui`). The wizard uses
  Vista's available translations and Windows' normal MUI language fallback; languages absent from the source
  remain untranslated. The Windows native fallback keeps its original files and language resources.
* The compatibility hooks are process-local hooks installed by the `winbrand.dll` proxy inside the Vista
  wizard—not system-wide hooks—and are not removed by replacing the System32 launcher.
* The bundled modded `DUser.dll` and `dui72.dll` are staged beside `vistamsoobe.exe` as `DUser.dll` and
  `DUI70.dll` (the latter's internal PE name). Windows' normal app-local DLL search then selects them for
  the Vista wizard rather than replacing the OS copies. The proxy logs the resolved module paths as `[module]`.
* Everything is written to `C:\Windows\Temp\vista-oobe.log`.

## Steps (in the Windows 10 VM)

1. Download and extract the `vista-oobe-kit.zip` artifact from the latest successful **Build Windows binaries**
   Actions run, then copy the Vista `oobe` folder (from the .tar.gz) into the VM. The kit includes the launcher,
   proxy, modded compatibility DLLs, preparation script, and restore scripts.
2. In the VM, take a snapshot. From an elevated prompt run
   `prepare-vista-oobe.bat C:\path\to\vista\oobe`.
   It stages the complete Vista OOBE folder in `C:\vista-oobe\oobe`, preserves the original Windows folder and ACLs,
   then replaces the contents of `System32\oobe`. The original files remain at `C:\vista-oobe\original-oobe` for
   rollback and the launcher's Windows fallback.
3. Run `%windir%\System32\Sysprep\sysprep.exe /oobe /reboot` - Windows restarts into OOBE, which should now show
   the Vista wizard.
4. Send `C:\Windows\Temp\vista-oobe.log` if anything fails or the screen stays blank/transition screen.

To restore the Windows OOBE folder, run `restore-system-msoobe.bat` elevated before starting OOBE, or revert the snapshot.
Do not delete `C:\vista-oobe\original-oobe` or `C:\vista-oobe\original-oobe-acl.xml`; they are required for fallback and rollback.
The legacy `ifeo-install.reg` no longer installs a debugger redirect and is not part of this flow.

The update notes below describe diagnostics from earlier IFEO-based trials. Do not follow their historical
instructions to import `ifeo-install.reg`; use the direct replacement workflow above.

## Not known yet (the log will tell)

* Whether Windows 10's "transition" screen that `oobeldr.exe` shows stays on top of the Vista wizard.
* Whether the ordinal imports land on the right functions.
* How the wizard creates the account on Windows 10 (it does the work itself, so this is the part to watch).
* Whether the renamed `vistamsoobe.exe.mui` loads (the wizard text would be missing if not).

Build (Linux): `x86_64-w64-mingw32-gcc -O2 -Wall -mwindows -municode -static -o oobelaunch.exe oobelaunch.c -lwtsapi32 -luserenv`.
`impcheck.exe` was the initial compatibility check.
Rebuild the proxy after changes with `x86_64-w64-mingw32-gcc -O2 -Wall -Werror -shared -o winbrand.dll winbrand_proxy.c winbrand.def -static-libgcc -lkernel32 -lgdi32`.


## If it crashes and you cannot get the log

If the Vista wizard cannot start, crashes, or fails at once, the replacement runs the preserved Windows wizard,
so setup can finish and you can read the log afterwards. The log is written to two
places: `C:\vista-oobe\vista-oobe.log` (easiest) and `C:\Windows\Temp\vista-oobe.log`.

If you are stuck inside OOBE:

* Press **Shift+F10** in OOBE for a command prompt, then `notepad C:\vista-oobe\vista-oobe.log`
  (or `type C:\vista-oobe\vista-oobe.log`). To select Windows' own wizard, use
  `echo x > C:\vista-oobe\disable.txt`. To roll back the file replacement, run
  `powershell -NoProfile -ExecutionPolicy Bypass -File C:\vista-oobe\restore-system-msoobe.ps1`
  from an elevated prompt after setup/OOBE processes have stopped, or revert the VM snapshot. `disable.txt` only
  selects Windows' `msoobe.exe`; it does not undo the complete folder replacement.
* If OOBE restarts in a loop and Shift+F10 does not help: boot the VM into the Windows Recovery Environment
  (or from the Windows 10 ISO: Repair your computer > Troubleshoot > Command Prompt), find the Windows drive
  (try `dir C:\`, `dir D:\` ...) and run, with D: replaced by that drive:
      type D:\vista-oobe\vista-oobe.log
      type D:\Windows\Temp\vista-oobe.log
      echo x > D:\vista-oobe\disable.txt
  For offline rollback, revert the VM snapshot; replacing only `msoobe.exe` is not sufficient after the full-folder swap.
* Or revert the snapshot, and use the Hyper-V / VirtualBox / VMware "attach virtual disk to another VM" feature to read
  `C:\vista-oobe\vista-oobe.log` from the crashed disk.

If **no log file exists at all**, neither the launcher nor the proxy ran: check whether
`%WINDIR%\System32\oobe\msoobe.exe` and `C:\vista-oobe\oobelaunch.exe` exist.


## Update (after the first log)

The first log showed: the DUser fix works (`SetGadgetRootInfo(cbSize=0x20) -> 0`, retried as 0x30 -> 1), then
`vistamsoobe.exe` called `DestroyWindow` on its main window - the page initialisation had failed - and DUser crashed
(null pointer, `DUser.dll+0x11A08`) while the half-built window was destroyed. The wizard exited with code 0xFF, which
the first launcher did not treat as a failure. Now:

* `winbrand.dll` (proxy) also **traces every DUser call** the wizard makes (27 of its 35 DUser functions: name,
  where it was called from, arguments, result) - this shows which call fails before the window is destroyed.
* On a crash the proxy writes `C:\vista-oobe\crash.flag`; `oobelaunch.exe` sees it and falls back to Windows' own wizard.

To update an existing setup: copy the new `winbrand.dll` to `C:\vista-oobe\oobe\` and `oobelaunch.exe` to `C:\vista-oobe\`
(delete `C:\vista-oobe\vista-oobe.log` first so the new log is short), then rerun the preparation script before Sysprep.
The old IFEO re-import step no longer applies.


## Update 2 (after the second log)

The second log showed the fallback working (Windows' own wizard finished with exit code 0) and the DUser trace:
`InitGadgets` x2, `CreateGadget` (the window host) + `SetGadgetRootInfo` fix OK, ten `FindStdColor` calls (-1 is normal:
the wizard then tries another way to read the colour), a second `CreateGadget` (OK), and then both gadgets are
deleted again and the window destroyed - so the wizard's own page set-up failed in code that is not in DUser.

The new `winbrand.dll` additionally logs (a) readable string arguments of each DUser call and (b) the call stack
(`[stack] ...`) at the first `DeleteHandle` / `CreateGadget` calls: the functions on that stack are the ones that gave up.

To update: copy the new `winbrand.dll` to `C:\vista-oobe\oobe\`, delete `C:\vista-oobe\vista-oobe.log`,
rerun the preparation script, and run `sysprep /oobe /reboot` as before.


## Update 3 (after reading Windows 10's duser.dll, 10.0.19041.3636)

* The crash at `DUser.dll+0x11A08` (read at address 0x78) is DUser's window clean-up calling its destroy-gadget function
  on a null pointer: Vista's wizard deletes the gadget first and then calls `DestroyWindow`; Vista's DUser tolerated that,
  Windows 10's does not. This only happens on the failure path (page set-up failed) - it is a side effect, not the cause.
  `winbrand.dll` now guards it: before `DestroyWindow` it sets a flag in DUser's window record so DUser skips that call,
  logs `[guard] ...` and writes `crash.flag` (so the launcher still falls back to Windows' own wizard).
  (Offsets are for duser.dll 10.0.19041.x, found by reading its code.)
* Structure sizes: the only DUser function the wizard calls with a mismatching structure is `SetGadgetRootInfo`
  (already handled). `InitGadgets` (0x18), `SetGadgetBufferInfo` (0x14) and `CreateAction` (0x28) get the sizes Windows 10 expects.
* Still unknown: why the wizard's page set-up fails. The `[stack]` lines in the log (call chain at the first
  `DeleteHandle`/`CreateGadget`) should show it.

To update: copy the new `winbrand.dll` to `C:\vista-oobe\oobe\`, delete `C:\vista-oobe\vista-oobe.log`, import
`ifeo-install.reg` again, run `sysprep /oobe /reboot` from your snapshot as before, and send the log.


## Update 4 (after the fourth log)

* Removed a false "crash" in the log: my own pointer check (`IsBadReadPtr`) caused a handled access violation that was logged
  as a crash and wrongly created `crash.flag`. Pointer checks now use `VirtualQuery`.
* The `DestroyWindow` guard now also tracks host gadgets the wizard already deleted (the log showed a stale, non-null pointer, not a null one).
* `winbrand.dll` now captures the wizard's own debug output as `[exe-debug] ...`. The wizard prints
  `DUIXMLParserFailure ... DUI_E_<reason> '<element>' ... @ line N col M` when a page cannot be built - this names the real cause.
* `oobelaunch.exe` also falls back to Windows' own wizard when the Vista wizard exits with 0xFF (its own failure code).

To update: copy the new `winbrand.dll` to `C:\vista-oobe\oobe\` and the new `oobelaunch.exe` to `C:\vista-oobe\`,
delete `C:\vista-oobe\vista-oobe.log`, rerun the preparation script, revert to your snapshot, and run
`sysprep /oobe /reboot`. Look for `[exe-debug]`, `[guard]` and `[stack]` lines.


## Update 5 (after the fifth log) - found the reason the pages fail

The wizard's pages use the style sheet `common`, which Vista keeps inside `duser.dll` as UIFILE resource 1010 (XML text).
Some newer `duser.dll` builds use a compiled binary format there (`duib`) that the Vista parser cannot read; the supplied
modded DUser build instead contains a full XML style sheet. `winbrand.dll` now passes through the full sheet when the
resource is XML, and supplies its minimal XML fallback only when the resource is missing or binary (`[resource]` log line).
Also: the DUser clean-up crash is now caught directly (`[guard] DUser clean-up on a null gadget skipped`) instead of
relying on the `DestroyWindow` hook (which was never called in the last log).

Update steps are the same as in Update 4 (only `winbrand.dll` changed).


## Update 6 - product-key page skipped, basic window frame

* The wizard now renders. Its product-key page cannot accept a Windows 10 key (it uses Vista's key check), so it is skipped:
  `oobelaunch.exe` sets `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Setup\OOBE\SetupDisplayedProductKey = 1`
  (the value Vista's Setup writes when it already asked for a key; the wizard reads it and hides the page).
  Activate later in Settings. Create `C:\vista-oobe\askkey.txt` to get the page back.
* Basic theme (as in BasicThemer2): `winbrand.dll` turns off DWM non-client rendering for the wizard's top-level windows
  (`DwmSetWindowAttribute(DWMWA_NCRENDERING_POLICY, DWMNCRP_DISABLED)`), log line `[theme]`.
  Create `C:\vista-oobe\aero.txt` to keep the Aero frame.

To update: copy `winbrand.dll` to `C:\vista-oobe\oobe\` and `oobelaunch.exe` to `C:\vista-oobe\`.


## Update 7 - cursor, extra diagnostics

* The wizard ran start to finish (76 s, exit code 0). Windows 10 hides the mouse cursor during OOBE ("cursor suppression");
  `oobelaunch.exe` now sets `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System\EnableCursorSuppression = 0`.
* The log did not show what happens after the last page (the "checking your computer's performance" screen, WinSAT).
  `winbrand.dll` now logs windows (`[window]` CreateWindowEx/ShowWindow with class and size), libraries (`[loadlib]`) and
  started processes (`[process]`), and the DUser call trace is shorter so the log reaches the end of the run.


## Update 8 - own branding, basic frame colour

* Own banner: put `Basebrd_121.png` (or `.bmp`/`.jpg`/`.gif`, or just `banner.png`) into `C:\vista-oobe\oobe\branding\`
  (the `branding` folder of this kit is copied there by `prepare-vista-oobe.bat`; see `branding\README.txt`).
  The wizard asks Windows for its banner with `BrandingLoadBitmap(L"Basebrd", 121)`; if a file exists it is used instead.
  Every request is logged as `[branding] ...`.
* Basic frame look: the title/header areas of Vista's wizard are transparent (glass under Aero). Without glass they showed
  white; they now get the Vista Basic colour (#BCD0E8) as window background. This is a first attempt (I could not test it):
  tell me what it looks like. `C:\vista-oobe\aero.txt` turns both this and the frame change off.


## Update 9 - basic frame only on windows with a caption

Following the Windhawk "Basic Themer" mod: `DWMWA_NCRENDERING_POLICY = DISABLED` is applied only to windows that have `WS_CAPTION`
(it breaks captionless windows). Checked at creation and again at `ShowWindow`. The log shows, per window,
`[theme] basic frame applied ... style 0x...` or `[theme] window ... has no WS_CAPTION (style 0x...)`.


## Update 10 - basic frame applied from outside, like BasicThemer2

The frame was still drawn by DWM. BasicThemer2 does not call `DwmSetWindowAttribute` inside the program, it calls it from another process,
again and again, on the visible windows. `oobelaunch.exe` now does that for every visible top-level window of the Vista wizard
(every 100 ms while it runs; log lines `basic frame: window ... style=0x... -> hr=...`). `aero.txt` still turns it off.
Only `oobelaunch.exe` changed (copy to `C:\vista-oobe\`).


## Update 11 - what is on screen (diagnostics only)

* The log shows the wizard loads `ooberesources.dll` a second time (10:54:22) just before it finishes - that is the code that builds the
  "performance" screen window - but the window log had hit its limit, so I could not see whether that window was created/shown.
* `winbrand.dll`: top-level windows and `ShowWindow` are now logged without the early cut-off (child controls are limited).
* `oobelaunch.exe`: every 250 ms it checks all visible top-level windows on screen and logs the list whenever it changes
  (`[screen] visible windows now:` - window, class, owning program, size, position, foreground window). This shows what is in front of the
  wizard, and what is shown between the phases.
Copy both files again (`winbrand.dll` to `C:\vista-oobe\oobe\`, `oobelaunch.exe` to `C:\vista-oobe\`).

(Update 11b) `oobelaunch.exe` also copies `C:\Windows\Panther\UnattendGC\setupact.log` / `setuperr.log` (where the wizard writes its own messages,
e.g. "Running WinSAT" / "Failed to run WinSAT [hr=...]") to `C:\vista-oobe\unattendgc-setupact.log` / `unattendgc-setuperr.log` after it exits.


## Update 12 - the performance screen was covered; bridge window; composition off

What the last logs showed: the wizard does everything (computer name, user "Ryan", settings, 34 s of "mandatory tasks") and exits 0.
The "Connect and communicate / checking your computer's performance" screen is drawn by the wizard's own full-screen `msoobe` window - but Windows 10's
own full-screen `FirstUXWndClass` window (winlogon.exe) was IN FRONT of it (z-order in the `[screen]` log), and it is also the blue boot-screen-like
screen you see between the phases. `Failed to install product key [hr=0x8000ffff]` is harmless (Vista's licensing call on Windows 10).

* `oobelaunch.exe` puts every visible wizard window (backdrop, dialog) TOPMOST, so they stay above `FirstUXWndClass`.
* Bridge: `oobelaunch.exe` starts a second copy of itself (`/bridge`) before the wizard: a full-screen window with Vista's background (`C:\vista-oobe\oobe\msoobe.jpg`),
  topmost, which stays until `FirstUXWndClass` has been gone for 6 s (max 10 min) - so the gaps and the move to the logon screen show the Vista background.
  It is closed at once if the launcher falls back to Windows' own wizard (`bridge-stop.flag`). `C:\vista-oobe\nobridge.txt` switches it off.
* `winbrand.dll`: like the Windhawk "DWM Unextend Frames" mod, `uxtheme!IsCompositionActive` (FALSE), `dwmapi!DwmIsCompositionEnabled` (FALSE),
  `DwmExtendFrameIntoClientArea` and `DwmEnableBlurBehindWindow` (composition disabled error) are replaced inside the wizard's process (log `[unextend]`).
  `aero.txt` switches it off. I did not port `SetWindowCompositionAttribute` / `DwmSetWindowAttribute` / comctl32's `_InitializeTables` hook.
Copy both files again (`winbrand.dll` -> `C:\vista-oobe\oobe\`, `oobelaunch.exe` -> `C:\vista-oobe\`).

(Update 12b) `FirstUXWndClass` (winlogon's full-screen window) is now also hidden (`ShowWindow SW_HIDE`, every 250 ms, by the launcher and the bridge; log line
`hid FirstUXWndClass window ...`). It is hidden, not closed. `C:\vista-oobe\showfirstux.txt` keeps it visible. The bridge now waits for the window to be
gone (not just hidden) before closing. Only `oobelaunch.exe` changed.


## Update 13 - Windows 10 first sign-in animation off, reg file

The blue "This might take several minutes / Don't turn off your PC" screen is Windows 10's first sign-in animation (`msoobeFirstLogonAnim.dll`: "Hi / We're getting
everything ready for you"). It is switched off with `EnableFirstLogonAnimation = 0` (policy key and Winlogon key) - added to `ifeo-install.reg` (with
`DisablePrivacyExperience` and `EnableCursorSuppression`; `ifeo-uninstall.reg` removes them) and also set by `oobelaunch.exe` itself. The bridge now also logs what is on screen
(`[screen/bridge]`) until it closes, so a screen that still appears can be identified.


## verify-oobe.bat

Run it once after the first sign-in (elevated prompt if possible). It writes `C:\vista-oobe\verify.txt`: computer name, accounts and admin membership, time zone,
region/keyboard, wallpaper, Windows Update/Defender/error-reporting settings, network profile, OOBE/setup state flags, activation status, and the errors from the wizard's
own log - to check what the wizard really wrote.


## Update 14 - the wizard's wallpaper choice is applied

The wizard stores the picture chosen on its wallpaper page in `HKLM\...\OOBE\Wallpaper`; Windows 10 then puts its Spotlight picture on the new user's desktop.
After a successful wizard run, `oobelaunch.exe` registers a RunOnce entry (`oobelaunch.exe /wallpaper`); at the new user's first sign-in it reads that value and applies the
picture (4 passes over ~35 s, in case Windows 10 changes it back; log lines `wallpaper: ...`). If the picture does not exist in `C:\Windows\Web\Wallpaper` (Windows 10 lacks
Vista's pictures) it looks in `C:\vista-oobe\wallpapers\` (copy the `wallpapers` folder there and add the pictures). `nowallpaper.txt` switches this off.


## CopyProfile (copy your Vista customisation to every new user)

`unattend-copyprofile.xml` contains only the `CopyProfile` setting (specialize pass); `run-sysprep.bat` runs
`sysprep /generalize /oobe /reboot /unattend:C:\vista-oobe\unattend-copyprofile.xml`. Copy both to `C:\vista-oobe\`.
* CopyProfile copies the profile of the account that runs sysprep, so run it from the BUILT-IN Administrator account (audit mode / enabled Administrator) after you set up the
  Vista look there (theme, cursors, wallpaper, Start menu, taskbar...).
* It is only processed in the specialize pass, and specialize only runs after `/generalize`. Plain `sysprep /oobe /reboot` (what was used so far) ignores it.
* Only the CopyProfile setting is in the file: no oobeSystem settings, so the Vista wizard still shows all its pages.
* If sysprep refuses: read `C:\Windows\System32\Sysprep\Panther\setupact.log` / `setuperr.log` (typically a Store app installed for a user).


## CopyProfile caused a black screen at first sign-in -> use copy-to-default.bat instead

`CopyProfile` copies the whole profile including Start/taskbar/cloud-store state, which Microsoft does not support on current Windows 10 builds (black screen, no Explorer).
`copy-to-default.bat` copies only look-and-feel settings (colours, cursors, desktop, sounds, mouse, DWM, themes, Explorer\Advanced/Accent/VisualEffects, theme files)
from the current user's profile into `C:\Users\Default`; run it elevated before a plain `sysprep /oobe /reboot`. Log: `C:\vista-oobe\copy-to-default.log`.
`run-sysprep.bat` / `unattend-copyprofile.xml` are kept for reference but should not be used on this build.

Windhawk: mods and their settings live in `C:\ProgramData\Windhawk` and `HKLM\SOFTWARE\Windhawk` (machine-wide), so they are not part of the profile and need no copying.
`copy-to-default.bat` also copies `HKCU\Software\Windhawk` if it exists; `verify-oobe.bat` now lists the Windhawk service, mods, settings keys and autostart entries.

OpenGlass: `find-openglass.bat` (run where OpenGlass is configured) writes `C:\vista-oobe\openglass-where.txt` listing its registry keys, service/task, autostart entries and files, so I can see
what is per-user and what is machine-wide. `copy-to-default.bat` already copies `HKCU\Software\Microsoft\Windows\DWM` and now also any `HKCU\Software\...glass...` keys.

## copy-to-default is now PowerShell (copy-to-default.ps1, started by copy-to-default.bat)
Copies whole HKCU branches into the default user hive: Control Panel (except International / Input Method), AppEvents, Console, DWM, Themes, ThemeManager, Policies (this is where your
NonEnum.reg lives), Software\Policies and Explorer (preview-pane size is in Explorer\Modules), minus keys with per-user paths or shell state (StartPage*, CloudStore, Shell Folders,
User Shell Folders, Taskband, FileExts, ...). Also copies top-level HKCU\Software\<name> keys whose name looks like a customisation tool (glass, aero, explorer, classic, vista, windhawk, ...).
Log: C:\vista-oobe\copy-to-default.log. `dump-hkcu.bat` (optional) dumps the look-and-feel branches so two accounts can be compared.
`verify-oobe.bat` also lists scheduled tasks (glass/dwm/windhawk) with the account they run as, and the modules loaded in dwm.exe.


## Update 15 - first-session corners (OpenGlass): automatic DWM + Explorer restart once

Signing out and in fixed the corners, so after a successful wizard run `oobelaunch.exe` creates a scheduled task `VistaOobeFixShell` (SYSTEM, at log on, 25 s delay). It starts
`oobelaunch.exe /fixshell`, which deletes its own task, waits for the user's explorer.exe, then restarts dwm.exe and explorer.exe once (both restart by themselves; Windhawk/OpenGlass hook the new
processes). Log lines `fixshell: ...`. Expect one screen flicker ~30 s after the first sign-in.
Files in C:\vista-oobe: `nofixshell.txt` = disabled, `skip-dwm.txt` = do not restart DWM, `skip-explorer.txt` = do not restart Explorer (to find out which step is needed).
If the task did not run: `schtasks /query /tn VistaOobeFixShell` and the `fixshell:` lines in the log.


## Update 16 - Explorer is started again, DWM restart is off by default, earlier restart

From the log: ending dwm.exe left the session without DWM (OpenGlass hooked in: it did not come back); ending explorer.exe left no shell, because Windows only restarts Explorer
by itself after a crash.
* `fixshell` task: no 25 s delay any more; it polls every 250 ms for the user's explorer.exe, waits 1.5 s, ends it, waits 2.5 s for it to come back by itself and otherwise starts
  `explorer.exe` as the signed-in user (`WTSQueryUserToken` + `CreateProcessAsUser`, log `fixshell: explorer.exe started ...`).
* DWM restart is now OPT-IN: create `C:\vista-oobe\fix-dwm.txt` (was `skip-dwm.txt`). `skip-explorer.txt` / `nofixshell.txt` still work.


## Update 17 - the Explorer restart did not fix the corners: it is now opt-in

Result: the `fixshell` task ran (explorer restarted and was started again as the user) and the corners were still wrong; only a sign-out fixes them; ending dwm.exe is not safe
(DWM does not come back with OpenGlass). So the task is only created when `C:\vista-oobe\fixshell.txt` exists. Explorer is now ended directly instead of through taskkill (that wait cost ~15 s).
To find what a sign-out changes: run `dump-hkcu.bat` as the new user BEFORE signing out (rename its output), sign out and in, run it again, and send both files.


## Update 18 - OpenGlass first-session corners: re-send the logon message to DWM

From the OpenGlass source: its service injects into dwm.exe and hands it the signed-in user's HKCU keys; DWM reloads them when its notification window (class "DWM") receives
`WM_WTSSESSION_CHANGE` / `WTS_SESSION_LOGON` (`GlassEngine::LoadRegistry`). At the first sign-in after setup that reload does not get the new user's keys (the session was an OOBE session
before), so the first session uses machine defaults until a sign-out. `oobelaunch.exe` now registers a RunOnce entry (`oobelaunch.exe /glassreload`) that runs as the new user at first sign-in
and sends that same message to the DWM window 4 times (after 4, 10, 20 and 35 s). Log lines `glassreload: attempt N, DWM window ..., session ... -> ...`
(DWM window 0000000000000000 = the window could not be found from the user's desktop). `noglassreload.txt` switches it off. The Explorer restart (`fixshell.txt`) stays opt-in.


## Update 19 - registry is not the cause; redraw + graphics reset

The HKCU dumps (new user before / after a sign-out) differ only in session counters and shell history: nothing about DWM, themes, colours or WindowMetrics. The `glassreload` message reached DWM
(window found, delivered 4x) and the corners stayed wrong, so the first-session DWM state itself is the problem, like the overlay that goes away with Ctrl+Alt+Del.
* The wallpaper step now starts `/glassreload` immediately (RunOnce entries run one after the other: before, the reload started ~60 s after sign-in); only one instance runs.
* After the 4 reload messages: a full-screen redraw request, then the Windows graphics-reset shortcut Win+Ctrl+Shift+B is sent (DWM re-creates its surfaces). `nogfxreset.txt` switches the key press off.
* The bridge window requests a full redraw when it closes.
Test the overlay without the bridge: create `C:\vista-oobe\nobridge.txt` (and `showfirstux.txt` to see whether Windows 10's own screen is the overlay).


## Update 20 - OpenGlass starts after the first sign-in (glassdelay)

OpenGlassHost injects OpenGlass into dwm.exe and hands it the signed-in user's registry keys at that moment; during setup nobody is signed in, so the first session had machine defaults
(wrong corners) until a sign-out; the black overlay may be related. Run `openglass-delay.bat` (administrator) before the snapshot: it sets the service to manual start, stops it and
creates `C:\vista-oobe\glassdelay.txt`. After a successful wizard run `oobelaunch.exe` creates a SYSTEM task `VistaOobeStartGlass` (at log on); it waits for the new user's desktop, then
sets the service back to automatic and starts it (log `startglass: ...`). The bridge window now stays until the new user's explorer.exe is up (3 s) as well as Windows 10's FirstUX window being gone.
The graphics reset key press (Win+Ctrl+Shift+B) is now opt-in (`gfxreset.txt`).


## Update 21 - what is the overlay? (diagnostics)

The overlay that stays until Ctrl+Alt+Del could be Windows 10's own logon UI (LogonUI / AuthUX) not releasing the desktop. The wallpaper step now also starts `/screenlog`, which runs as the new
user for 3 minutes and logs every change of the visible windows (`[screen/user]`: window, class, owning program, size, position, foreground). To tell whether the Vista wizard is involved at all:
create `C:\vista-oobe\disable.txt` (Windows' own wizard runs) and see if the overlay still appears.


## Update 22 - the overlay: AuthUX window that is never hidden

The logon UI in use is AuthUX (Win7-style, explorer7-team/AuthUX). Its `AuthUX::Hide()` is empty; the full-screen window `AUTHUI.DLL: LogonUI Logon Window` is only destroyed in `Stop()`.
After the first sign-in Windows apparently hides the logon UI without stopping it, so the window stays until Ctrl+Alt+Del restarts LogonUI.
Workaround in the kit: SYSTEM task `VistaOobeAuthUXFix` (at log on) -> waits for the new user's desktop -> starts `oobelaunch.exe /hideauthux` as SYSTEM in the user's session, which hides
visible windows of that class on the Default and Winlogon desktops for 90 s (log: `authuxfix:` / `hideauthux:`). `noauthuxfix.txt` switches it off. The proper fix is in AuthUX itself: see `AuthUX-Hide-patch.txt`.
`C:\log.txt` is written by AuthUX::Stop - if it is missing after the overlay appeared, Stop was never called.


## Update 23 - the overlay is the Winlogon desktop staying the input desktop (deskfix)

`C:\log.txt` showed AuthUX::Stop is called. The user-session screen log showed `foreground=(none)` for 70 s although explorer and other windows were up: the user's desktop did not have the input.
The earlier /hideauthux only hid AuthUX's Ctrl+Alt+Del screen (after the user pressed it). `/hideauthux` (started by the `authuxfix` SYSTEM task) now logs the input desktop and the big windows on the
Default and Winlogon desktops whenever the input desktop changes (`deskfix:` lines), and if the Winlogon desktop has been the input desktop for 6 s while the user's explorer is up it calls
`SwitchDesktop(Default)` (max 3 times, within 2 minutes) - the same switch Ctrl+Alt+Del + Cancel causes. `noswitchdesktop.txt` = only log; `noauthuxfix.txt` = off.
