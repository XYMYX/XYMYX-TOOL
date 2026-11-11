// security_win.cpp - the quick security check on Windows.
//
// Five read-only checks. Several of them use COM, the Windows mechanism for
// talking to system components as objects: you ask for an object with
// CoCreateInstance, call its methods, and release it. Every method returns
// an HRESULT; FAILED(hr) means it did not work.
//
// The rule for every check: report only what was actually read. If a call
// fails, the finding becomes "not available", with "requires admin" added
// only when Windows said "access denied".

#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <netfw.h>
#include <wuapi.h>
#include <shobjidl.h>
#include <propsys.h>
#include <lm.h>

#include <cstdio>
#include <vector>

#include "platform/platform.h"
#include "platform/win/win_util.h"

namespace platform {

namespace {

Finding unavailable(const std::string& name, HRESULT hr) {
  return {name, Status::NotAvailable, hr == E_ACCESSDENIED ? kNeedsAdmin : kNotAvailable};
}

Finding firewall() {
  const std::string name = "Firewall";
  win::ComPtr<INetFwPolicy2> policy;
  // INetFwPolicy2 is the official firewall API. Reading from it works without
  // admin rights. The alternatives are worse: "netsh" output is translated
  // into the system language and hard to parse, and the registry values do
  // not show settings that a company policy overrides.
  HRESULT hr = CoCreateInstance(__uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&policy));
  if (FAILED(hr) || !policy) return unavailable(name, hr);

  // Windows has three firewall profiles (Domain, Private, Public) and picks
  // one per network. The result is a bit mask because several can be active
  // at once, e.g. Wi-Fi on a public network and a VPN on a domain network.
  long active = 0;
  hr = policy->get_CurrentProfileTypes(&active);
  if (FAILED(hr)) return unavailable(name, hr);

  struct Profile {
    NET_FW_PROFILE_TYPE2 type;
    const char* label;
  };
  const Profile profiles[] = {{NET_FW_PROFILE2_DOMAIN, "Domain"},
                              {NET_FW_PROFILE2_PRIVATE, "Private"},
                              {NET_FW_PROFILE2_PUBLIC, "Public"}};
  bool allOn = true;
  std::string detail;
  std::string activeNames;
  for (const Profile& profile : profiles) {
    VARIANT_BOOL enabled = VARIANT_FALSE;
    hr = policy->get_FirewallEnabled(profile.type, &enabled);
    if (FAILED(hr)) return unavailable(name, hr);
    const bool on = enabled != VARIANT_FALSE;
    if (!on) allOn = false;
    if (!detail.empty()) detail += ", ";
    detail += std::string(profile.label) + ": " + (on ? "on" : "off");
    if (active & profile.type) {
      if (!activeNames.empty()) activeNames += ", ";
      activeNames += profile.label;
    }
  }
  detail = "Windows Firewall " + detail;
  if (!activeNames.empty()) detail += " (active profile: " + activeNames + ")";
  return {name, allOn ? Status::Ok : Status::Warning, detail};
}

// COM dates (DATE) are a double counting days since 30 December 1899, in UTC.
// To get an age we convert both that date and "now" to FILETIME, which counts
// 100-nanosecond steps as one 64-bit number, and subtract.
bool describeDate(DATE date, std::string& text, long long& ageDays) {
  SYSTEMTIME utc{};
  if (!VariantTimeToSystemTime(date, &utc)) return false;
  FILETIME then{}, now{};
  if (!SystemTimeToFileTime(&utc, &then)) return false;
  GetSystemTimeAsFileTime(&now);
  ULARGE_INTEGER a{}, b{};
  a.LowPart = then.dwLowDateTime;
  a.HighPart = then.dwHighDateTime;
  b.LowPart = now.dwLowDateTime;
  b.HighPart = now.dwHighDateTime;
  ageDays = b.QuadPart > a.QuadPart
                ? static_cast<long long>((b.QuadPart - a.QuadPart) / (10000000ULL * 86400ULL))
                : 0;
  SYSTEMTIME local{};
  if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) local = utc;
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u", local.wYear, local.wMonth, local.wDay);
  text = buffer;
  return true;
}

// Finds the date of the newest successfully installed update.
//
// This uses the Windows Update Agent COM API, but only QueryHistory, which
// reads the update history stored on this PC. The Search method of the same
// object would contact Microsoft's servers. It is never called, because this
// tool makes no network connections.
bool lastInstalledUpdate(std::string& dateText, long long& ageDays, HRESULT& failure) {
  win::ComPtr<IUpdateSession> session;
  HRESULT hr = CoCreateInstance(__uuidof(UpdateSession), nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&session));
  if (FAILED(hr) || !session) {
    failure = hr;
    return false;
  }
  win::ComPtr<IUpdateSearcher> searcher;
  hr = session->CreateUpdateSearcher(&searcher);
  if (FAILED(hr) || !searcher) {
    failure = hr;
    return false;
  }
  LONG total = 0;
  hr = searcher->GetTotalHistoryCount(&total);
  if (FAILED(hr)) {
    failure = hr;
    return false;
  }

  // The history is returned newest first. It is read in small chunks and the
  // loop stops at the first chunk that contains a match, so on most PCs only
  // one chunk is ever fetched.
  const LONG chunk = 50;
  const LONG limit = total < 1000 ? total : 1000;
  bool found = false;
  DATE newest = 0;
  for (LONG start = 0; start < limit && !found; start += chunk) {
    const LONG count = (limit - start) < chunk ? (limit - start) : chunk;
    win::ComPtr<IUpdateHistoryEntryCollection> entries;
    hr = searcher->QueryHistory(start, count, &entries);
    if (FAILED(hr) || !entries) {
      failure = hr;
      return false;
    }
    LONG got = 0;
    if (FAILED(entries->get_Count(&got))) break;
    for (LONG i = 0; i < got; ++i) {
      win::ComPtr<IUpdateHistoryEntry> entry;
      if (FAILED(entries->get_Item(i, &entry)) || !entry) continue;
      UpdateOperation operation = uoUninstallation;
      OperationResultCode result = orcNotStarted;
      DATE date = 0;
      if (FAILED(entry->get_Operation(&operation)) || FAILED(entry->get_ResultCode(&result)) ||
          FAILED(entry->get_Date(&date))) {
        continue;
      }
      if (operation != uoInstallation || result != orcSucceeded) continue;
      BSTR title = nullptr;
      bool definitionUpdate = false;
      if (SUCCEEDED(entry->get_Title(&title)) && title != nullptr) {
        // KB2267602 is the Microsoft Defender virus definition update. It
        // arrives several times a day, so counting it would make every PC
        // look freshly patched. The KB number is used because the title text
        // is translated on non-English systems.
        definitionUpdate = wcsstr(title, L"KB2267602") != nullptr;
        SysFreeString(title);
      }
      if (definitionUpdate) continue;
      if (!found || date > newest) newest = date;
      found = true;
    }
  }
  if (!found) {
    failure = S_OK;
    return false;
  }
  return describeDate(newest, dateText, ageDays);
}

Finding updates() {
  const std::string name = "OS updates";
  // Windows creates this registry key when an installed update is waiting
  // for a restart, and deletes it afterwards. So the question is only "does
  // the key exist?". Three outcomes are kept apart: it exists (pending),
  // "not found" (nothing pending), any other error (unknown, stays empty).
  HKEY key = nullptr;
  const LONG opened = RegOpenKeyExW(
      HKEY_LOCAL_MACHINE,
      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\RebootRequired",
      0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key);
  std::optional<bool> rebootPending;
  if (opened == ERROR_SUCCESS) {
    rebootPending = true;
    RegCloseKey(key);
  } else if (opened == ERROR_FILE_NOT_FOUND) {
    rebootPending = false;
  }

  std::string dateText;
  long long ageDays = 0;
  HRESULT failure = S_OK;
  const bool haveDate = lastInstalledUpdate(dateText, ageDays, failure);

  if (!haveDate && !rebootPending) return unavailable(name, failure);

  std::string detail;
  Status status = Status::Ok;
  if (haveDate) {
    detail = "last installed update " + dateText + " (" + std::to_string(ageDays) +
             " days ago, Defender definitions not counted)";
    // Microsoft releases security updates once a month. 35 days allows one
    // full cycle plus a few days before the finding turns into a warning.
    if (ageDays > 35) status = Status::Warning;
  } else {
    detail = std::string("install history ") +
             (failure == E_ACCESSDENIED ? kNeedsAdmin : kNotAvailable);
    status = Status::NotAvailable;
  }
  if (rebootPending) {
    detail += *rebootPending ? "; restart pending to finish updates" : "; no restart pending";
    if (*rebootPending) status = Status::Warning;
  }
  return {name, status, detail};
}

void encryption(std::vector<Finding>& findings) {
  // The well-known ways to query BitLocker (manage-bde, or the WMI class
  // Win32_EncryptableVolume) both require admin rights. But Explorer shows a
  // lock icon on encrypted drives for every user. It gets that from the shell
  // property "System.Volume.BitLockerProtection", which we read the same way.
  // PSGetPropertyKeyFromName translates the property name into the ID that
  // the shell API expects.
  PROPERTYKEY key{};
  HRESULT hr = PSGetPropertyKeyFromName(L"System.Volume.BitLockerProtection", &key);
  if (FAILED(hr)) {
    findings.push_back(unavailable("Disk encryption", hr));
    return;
  }
  const DWORD length = GetLogicalDriveStringsW(0, nullptr);
  std::vector<wchar_t> buffer(length + 1, L'\0');
  if (length == 0 || GetLogicalDriveStringsW(length, buffer.data()) == 0) {
    findings.push_back({"Disk encryption", Status::NotAvailable, kNotAvailable});
    return;
  }
  bool any = false;
  for (const wchar_t* root = buffer.data(); *root != L'\0'; root += wcslen(root) + 1) {
    if (GetDriveTypeW(root) != DRIVE_FIXED) continue;
    any = true;
    std::string drive = util::narrow(root);
    if (!drive.empty() && drive.back() == '\\') drive.pop_back();
    Finding finding{"Disk encryption (" + drive + ")", Status::NotAvailable, kNotAvailable};

    // SHCreateItemFromParsingName gives us the shell's object for a path,
    // here the drive root. IShellItem2::GetProperty then reads one property
    // of it into a PROPVARIANT, a container that can hold many value types.
    win::ComPtr<IShellItem2> item;
    hr = SHCreateItemFromParsingName(root, nullptr, IID_PPV_ARGS(&item));
    if (SUCCEEDED(hr) && item) {
      PROPVARIANT value;
      PropVariantInit(&value);
      hr = item->GetProperty(key, &value);
      if (SUCCEEDED(hr) && (value.vt == VT_I4 || value.vt == VT_UI4)) {
        // The property is a small number. Only the values with a known
        // meaning are translated; anything else is reported as not available
        // rather than guessed.
        switch (value.vt == VT_I4 ? value.lVal : static_cast<LONG>(value.ulVal)) {
          case 1:
            finding = {finding.name, Status::Ok, "BitLocker on"};
            break;
          case 2:
            finding = {finding.name, Status::Warning, "BitLocker off"};
            break;
          case 3:
            finding = {finding.name, Status::Warning, "BitLocker encryption in progress"};
            break;
          case 4:
            finding = {finding.name, Status::Warning, "BitLocker decryption in progress"};
            break;
          case 5:
            finding = {finding.name, Status::Warning, "BitLocker suspended"};
            break;
          case 6:
            finding = {finding.name, Status::Ok, "BitLocker on (volume locked)"};
            break;
          default:
            finding.detail = "not available (BitLocker state not reported for this volume)";
            break;
        }
      } else if (SUCCEEDED(hr)) {
        finding.detail = "not available (BitLocker state not reported for this volume)";
      } else if (hr == E_ACCESSDENIED) {
        finding.detail = kNeedsAdmin;
      }
      PropVariantClear(&value);
    } else if (hr == E_ACCESSDENIED) {
      finding.detail = kNeedsAdmin;
    }
    findings.push_back(std::move(finding));
  }
  if (!any) findings.push_back({"Disk encryption", Status::NotAvailable, "no fixed drive found"});
}

Finding autoLogin() {
  const std::string name = "Auto-login";
  // Automatic sign-in is controlled by the value AutoAdminLogon ("1" = on)
  // under the Winlogon key. The key is opened with KEY_QUERY_VALUE only, the
  // smallest right that allows reading values.
  const wchar_t* path = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";
  HKEY key = nullptr;
  const LONG opened =
      RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key);
  if (opened != ERROR_SUCCESS) {
    return {name, Status::NotAvailable,
            opened == ERROR_ACCESS_DENIED ? kNeedsAdmin : kNotAvailable};
  }
  wchar_t value[16] = {};
  DWORD size = sizeof(value) - sizeof(wchar_t);
  DWORD type = 0;
  const LONG read = RegQueryValueExW(key, L"AutoAdminLogon", nullptr, &type,
                                     reinterpret_cast<BYTE*>(value), &size);
  // Old setups store the password as plain text in DefaultPassword. Passing
  // null for the data buffer asks only "does this value exist?". The
  // password itself is never read into this program.
  const bool passwordStored =
      RegQueryValueExW(key, L"DefaultPassword", nullptr, nullptr, nullptr, nullptr) ==
      ERROR_SUCCESS;
  RegCloseKey(key);

  if (read == ERROR_FILE_NOT_FOUND) return {name, Status::Ok, "disabled"};
  if (read != ERROR_SUCCESS || type != REG_SZ) {
    return {name, Status::NotAvailable,
            read == ERROR_ACCESS_DENIED ? kNeedsAdmin : kNotAvailable};
  }
  if (std::wstring(value) == L"1") {
    return {name, Status::Warning,
            passwordStored ? "enabled, and a password is stored in the registry"
                           : "enabled (a user is signed in automatically at startup)"};
  }
  return {name, Status::Ok, "disabled"};
}

Finding guestAccount() {
  const std::string name = "Guest account";
  // The guest account cannot be found by name: it is "Guest" in English,
  // "Gast" in German, and it can be renamed. What never changes is its
  // relative ID (RID) 501. NetUserEnum with information level 20 is the
  // smallest level that includes the RID and the account flags.
  // The list can arrive in several parts; "resume" continues where the last
  // call stopped. Each buffer is allocated by Windows and must be handed
  // back with NetApiBufferFree.
  DWORD resume = 0;
  NET_API_STATUS status = ERROR_MORE_DATA;
  while (status == ERROR_MORE_DATA) {
    LPBYTE buffer = nullptr;
    DWORD read = 0;
    DWORD total = 0;
    status = NetUserEnum(nullptr, 20, FILTER_NORMAL_ACCOUNT, &buffer, MAX_PREFERRED_LENGTH, &read,
                         &total, &resume);
    if (status != NERR_Success && status != ERROR_MORE_DATA) {
      if (buffer != nullptr) NetApiBufferFree(buffer);
      return {name, Status::NotAvailable,
              status == ERROR_ACCESS_DENIED ? kNeedsAdmin : kNotAvailable};
    }
    const USER_INFO_20* users = reinterpret_cast<const USER_INFO_20*>(buffer);
    for (DWORD i = 0; buffer != nullptr && i < read; ++i) {
      if (users[i].usri20_user_id != DOMAIN_USER_RID_GUEST) continue;
      const bool disabled = (users[i].usri20_flags & UF_ACCOUNTDISABLE) != 0;
      const std::string account =
          util::narrow(users[i].usri20_name ? users[i].usri20_name : L"");
      NetApiBufferFree(buffer);
      return {name, disabled ? Status::Ok : Status::Warning,
              std::string(disabled ? "disabled" : "enabled") + " (account \"" + account + "\")"};
    }
    if (buffer != nullptr) NetApiBufferFree(buffer);
  }
  return {name, Status::NotAvailable, "not available (built-in guest account not found)"};
}

}

std::vector<Finding> readSecurity() {
  std::vector<Finding> findings;
  // COM must be initialised once per thread before the first COM call, and
  // every successful CoInitializeEx needs a matching CoUninitialize. The
  // shell objects used for BitLocker expect the "apartment threaded" model.
  // If COM cannot start, the three COM-based checks are marked not available
  // and the two registry/account checks still run.
  const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool comReady = SUCCEEDED(init);

  if (comReady) {
    findings.push_back(firewall());
    findings.push_back(updates());
    encryption(findings);
  } else {
    findings.push_back({"Firewall", Status::NotAvailable, kNotAvailable});
    findings.push_back({"OS updates", Status::NotAvailable, kNotAvailable});
    findings.push_back({"Disk encryption", Status::NotAvailable, kNotAvailable});
  }
  findings.push_back(autoLogin());
  findings.push_back(guestAccount());

  if (comReady) CoUninitialize();
  return findings;
}

}
