# security

Files: `src/features/security.cpp`, `src/platform/win/security_win.cpp`, `src/platform/mac/security_mac.cpp`

Menu item 4. Five read-only checks, each ending in `OK`, `Warning` or `Not available`.

This is a quick look at a few important settings, not a security audit. "OK" means one setting looks fine.

## Feature side

`runSecurityCheck` calls `platform::readSecurity`, prints each `Finding` with `Report::status` and counts the three outcomes.

The decision whether something is OK or a warning is made in the platform files, because the rules differ per system.

"Not available" has its own line in the summary on purpose. A summary that silently dropped unreadable checks would look better than what was actually verified.

## The checks

| Check | OK when | Warning when |
| --- | --- | --- |
| Firewall | built-in firewall is on | it is off (Windows: off for any profile) |
| OS updates | recently updated, no restart pending | last update older than 35 days, restart pending, automatic checks off |
| Disk encryption | BitLocker / FileVault on | off, suspended, or still in progress |
| Auto-login | off | a user is signed in automatically |
| Guest account | disabled | enabled |

## Windows

Three of the checks use COM. In short: COM is how Windows exposes system components as objects. You request one with `CoCreateInstance`, call methods that return an `HRESULT`, and release it when done. `readSecurity` starts COM with `CoInitializeEx` and ends it with `CoUninitialize`. The `ComPtr` helper in `win_util.h` handles the releasing.

### Firewall: `firewall`

Uses the `INetFwPolicy2` object. Windows has three firewall profiles: Domain, Private and Public. The function reads all three and which one is active.

Why this API: reading it needs no admin rights. Parsing the output of `netsh` would break on non-English systems because the text is translated. Reading the registry directly would miss settings enforced by company policy.

### OS updates: `updates` and `lastInstalledUpdate`

Two local sources:

- **Update history.** The Windows Update Agent object `IUpdateSearcher` has a method `QueryHistory` that reads the history stored on the PC. The newest successful installation gives the date.
- **Pending restart.** Windows creates the registry key `...\WindowsUpdate\Auto Update\RebootRequired` while a restart is needed. The check is simply whether the key exists.

The same COM object also has a `Search` method that asks Microsoft's servers for new updates. It is never called, because this tool makes no network connections. That is why the result is "when was the last update installed" and not "are you up to date".

Defender virus definitions (KB2267602) are installed several times a day. They are skipped, otherwise every PC would look freshly patched. The KB number is matched rather than the title, because titles are translated.

### Disk encryption: `encryption`

This is the best example in the project of working without admin rights.

The usual tools, `manage-bde` and the WMI class `Win32_EncryptableVolume`, both require elevation. But Explorer shows a lock icon on BitLocker drives for every user. It gets that from a shell property named `System.Volume.BitLockerProtection`.

`encryption` reads the same property: `PSGetPropertyKeyFromName` turns the name into a key, `SHCreateItemFromParsingName` gives the shell object for a drive, and `IShellItem2::GetProperty` reads the value.

| Value | Shown as |
| --- | --- |
| 1 | BitLocker on |
| 2 | BitLocker off |
| 3 | encryption in progress |
| 4 | decryption in progress |
| 5 | suspended |
| 6 | on (volume locked) |
| anything else | not available |

Unknown values are not interpreted. Only BitLocker is detected; a drive encrypted with another product shows as "BitLocker off", which is literally true.

### Auto-login: `autoLogin`

Reads `AutoAdminLogon` under the `Winlogon` registry key. `"1"` means a user is signed in automatically at startup.

Old setups also store the password in plain text in `DefaultPassword`. The code checks only whether that value **exists**, by asking for it with no buffer. The password is never read.

### Guest account: `guestAccount`

The account cannot be looked up by name: it is `Guest` in English, `Gast` in German, and it can be renamed. Its relative ID (RID) is always 501. `NetUserEnum` lists the local accounts with their RID and flags, and the flag `UF_ACCOUNTDISABLE` gives the answer.

## macOS

The macOS code has not been compiled or tested yet.

Two sources are used:

- **Preferences.** macOS stores settings in `.plist` files. `CFPreferencesCopyValue` reads a value from them. `systemPreference` selects the system-wide files in `/Library/Preferences`.
- **Built-in commands.** Where no public API exists, a read-only command is run through `runCommand` in `mac_util.h`.

| Check | Source |
| --- | --- |
| Firewall | `/usr/libexec/ApplicationFirewall/socketfilterfw --getglobalstate` |
| OS updates | preferences `com.apple.SoftwareUpdate`: `AutomaticCheckEnabled`, `LastRecommendedUpdatesAvailable`, `LastFullSuccessfulDate` |
| Disk encryption | `/usr/bin/fdesetup status` |
| Auto-login | preferences `com.apple.loginwindow`: `autoLoginUser` |
| Guest account | preferences `com.apple.loginwindow`: `GuestEnabled` |

### "Not set" versus "not readable"

`CFPreferencesCopyValue` returns null in two different situations: the key does not exist, or the file cannot be read. For the login settings those mean opposite things. A missing `autoLoginUser` means auto-login is off; an unreadable file means we do not know.

`loginWindowReadable` therefore checks with `access` that the file is readable before the absence of a key is treated as "off".

### Updates

As on Windows, only locally stored information is read. `softwareupdate -l` would give a fresher answer but contacts Apple.

## Limits

| Area | Limit |
| --- | --- |
| Firewall | third-party firewalls are not detected |
| Updates | no check for newer updates, only dates of the last install or check |
| Encryption | state only; no method, no key protectors; BitLocker and FileVault only |
| Guest account (Windows) | may be unreadable on domain-joined or locked-down PCs |
| FileVault | `fdesetup status` can be blocked on managed Macs |

## Check it yourself

| Check | Windows (PowerShell) | macOS |
| --- | --- | --- |
| Firewall | `Get-NetFirewallProfile` | System Settings → Network → Firewall |
| Encryption | lock icon on the drive in Explorer | `fdesetup status` |
| Guest account | `Get-LocalUser` | System Settings → Users & Groups |
