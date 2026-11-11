// network_win.cpp - adapters, addresses, ports and connections on Windows.
//
// All of it comes from the "IP Helper" API (iphlpapi), which is what netstat
// and ipconfig use internally. Nothing here opens a socket or sends a packet.
//
// Include order matters: winsock2.h must come before windows.h, otherwise
// windows.h pulls in the old winsock.h and the two collide.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <tlhelp32.h>

#include <cstdio>
#include <map>
#include <vector>

#include "platform/platform.h"
#include "platform/win/win_util.h"

namespace platform {

namespace {

// inet_ntop turns the raw bytes of an address into text ("192.168.1.20").
// It is a pure conversion and needs no network start-up (WSAStartup).
std::string ipv4Text(const void* address) {
  char buffer[INET_ADDRSTRLEN] = {};
  if (inet_ntop(AF_INET, const_cast<void*>(address), buffer, sizeof(buffer)) == nullptr) return {};
  return buffer;
}

std::string ipv6Text(const void* address) {
  char buffer[INET6_ADDRSTRLEN] = {};
  if (inet_ntop(AF_INET6, const_cast<void*>(address), buffer, sizeof(buffer)) == nullptr) return {};
  return buffer;
}

// Ports arrive in "network byte order": high byte first, stored in the low
// 16 bits of a DWORD. x64 CPUs store the low byte first, so the two bytes
// must be swapped. ntohs() would do the same; swapping by hand makes the
// reason visible.
std::uint16_t hostPort(DWORD networkPort) {
  return static_cast<std::uint16_t>(((networkPort & 0xFF) << 8) | ((networkPort >> 8) & 0xFF));
}

const char* adapterType(DWORD type) {
  switch (type) {
    case IF_TYPE_ETHERNET_CSMACD: return "Ethernet";
    case IF_TYPE_IEEE80211: return "Wi-Fi";
    case IF_TYPE_SOFTWARE_LOOPBACK: return "Loopback";
    case IF_TYPE_TUNNEL: return "Tunnel";
    case IF_TYPE_PPP: return "PPP";
    case IF_TYPE_PROP_VIRTUAL: return "Virtual";
    case IF_TYPE_WWANPP:
    case IF_TYPE_WWANPP2: return "Mobile broadband";
    default: return "Other";
  }
}

const char* tcpState(DWORD state) {
  switch (state) {
    case MIB_TCP_STATE_CLOSED: return "CLOSED";
    case MIB_TCP_STATE_LISTEN: return "LISTEN";
    case MIB_TCP_STATE_SYN_SENT: return "SYN_SENT";
    case MIB_TCP_STATE_SYN_RCVD: return "SYN_RECEIVED";
    case MIB_TCP_STATE_ESTAB: return "ESTABLISHED";
    case MIB_TCP_STATE_FIN_WAIT1: return "FIN_WAIT_1";
    case MIB_TCP_STATE_FIN_WAIT2: return "FIN_WAIT_2";
    case MIB_TCP_STATE_CLOSE_WAIT: return "CLOSE_WAIT";
    case MIB_TCP_STATE_CLOSING: return "CLOSING";
    case MIB_TCP_STATE_LAST_ACK: return "LAST_ACK";
    case MIB_TCP_STATE_TIME_WAIT: return "TIME_WAIT";
    case MIB_TCP_STATE_DELETE_TCB: return "DELETE_TCB";
    default: return "UNKNOWN";
  }
}

// Maps a process ID to its executable name.
//
// The socket tables only contain PIDs. The usual way to get a name is
// OpenProcess, but a normal user may not open system processes, and those
// own most listening ports. CreateToolhelp32Snapshot takes a snapshot of the
// whole process list, including the names of system processes, without admin
// rights. That is why it is the first choice here.
class ProcessNames {
 public:
  ProcessNames() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
      do {
        names_[entry.th32ProcessID] = util::narrow(entry.szExeFile);
      } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
  }

  void fill(SocketEntry& entry, DWORD pid) const {
    entry.pid = pid;
    const auto found = names_.find(pid);
    if (found != names_.end() && !found->second.empty()) {
      entry.process = found->second;
      return;
    }
    // Fallback for a process that started after the snapshot was taken.
    // PROCESS_QUERY_LIMITED_INFORMATION is the weakest access right that
    // still allows reading the image name; asking for more would fail more
    // often. If Windows answers "access denied", we say that admin rights
    // are needed. For any other error (usually: the process has ended) we
    // only say "not available", because that is all we know.
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
      entry.processNote = GetLastError() == ERROR_ACCESS_DENIED ? kNeedsAdmin : kNotAvailable;
      return;
    }
    wchar_t path[1024];
    DWORD size = 1024;
    if (QueryFullProcessImageNameW(process, 0, path, &size)) {
      std::wstring full(path, size);
      const std::size_t slash = full.find_last_of(L"\\/");
      entry.process = util::narrow(slash == std::wstring::npos ? full : full.substr(slash + 1));
    } else {
      entry.processNote = GetLastError() == ERROR_ACCESS_DENIED ? kNeedsAdmin : kNotAvailable;
    }
    CloseHandle(process);
  }

 private:
  std::map<DWORD, std::string> names_;
};

// GetExtendedTcpTable with TCP_TABLE_OWNER_PID_ALL returns every TCP socket
// (listening and connected) together with the owning PID. The simpler
// GetTcpTable has no PID column. The table can grow between the size query
// and the real call, hence the retry loop.
bool fetchTcp(ULONG family, std::vector<BYTE>& buffer) {
  DWORD size = 0;
  DWORD result = GetExtendedTcpTable(nullptr, &size, FALSE, family, TCP_TABLE_OWNER_PID_ALL, 0);
  for (int attempt = 0; attempt < 5 && result == ERROR_INSUFFICIENT_BUFFER; ++attempt) {
    buffer.resize(size);
    result = GetExtendedTcpTable(buffer.data(), &size, FALSE, family, TCP_TABLE_OWNER_PID_ALL, 0);
  }
  return result == NO_ERROR && !buffer.empty();
}

bool fetchUdp(ULONG family, std::vector<BYTE>& buffer) {
  DWORD size = 0;
  DWORD result = GetExtendedUdpTable(nullptr, &size, FALSE, family, UDP_TABLE_OWNER_PID, 0);
  for (int attempt = 0; attempt < 5 && result == ERROR_INSUFFICIENT_BUFFER; ++attempt) {
    buffer.resize(size);
    result = GetExtendedUdpTable(buffer.data(), &size, FALSE, family, UDP_TABLE_OWNER_PID, 0);
  }
  return result == NO_ERROR && !buffer.empty();
}

}

std::optional<std::vector<AdapterInfo>> readAdapters() {
  // GetAdaptersAddresses is the current API for network adapters (the older
  // GetAdaptersInfo knows no IPv6). It writes a linked list into one buffer
  // that we provide. Microsoft recommends starting with about 15 KB and
  // retrying if it reports ERROR_BUFFER_OVERFLOW. The flags skip address
  // kinds this tool does not show.
  const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
  ULONG size = 16 * 1024;
  std::vector<BYTE> buffer;
  ULONG result = ERROR_BUFFER_OVERFLOW;
  for (int attempt = 0; attempt < 5 && result == ERROR_BUFFER_OVERFLOW; ++attempt) {
    buffer.resize(size);
    result = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr,
                                  reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
  }
  if (result == ERROR_NO_DATA) return std::vector<AdapterInfo>{};
  if (result != NO_ERROR) return std::nullopt;

  std::vector<AdapterInfo> adapters;
  for (auto* item = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); item != nullptr;
       item = item->Next) {
    AdapterInfo adapter;
    adapter.name = util::narrow(item->FriendlyName ? item->FriendlyName : L"");
    adapter.description = util::narrow(item->Description ? item->Description : L"");
    adapter.type = adapterType(item->IfType);
    adapter.up = item->OperStatus == IfOperStatusUp;
    adapter.loopback = item->IfType == IF_TYPE_SOFTWARE_LOOPBACK;
    if (item->PhysicalAddressLength == 6) {
      char mac[32];
      std::snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", item->PhysicalAddress[0],
                    item->PhysicalAddress[1], item->PhysicalAddress[2], item->PhysicalAddress[3],
                    item->PhysicalAddress[4], item->PhysicalAddress[5]);
      adapter.mac = mac;
    }
    for (auto* unicast = item->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
      const sockaddr* address = unicast->Address.lpSockaddr;
      if (address == nullptr) continue;
      std::string text;
      if (address->sa_family == AF_INET) {
        text = ipv4Text(&reinterpret_cast<const sockaddr_in*>(address)->sin_addr);
      } else if (address->sa_family == AF_INET6) {
        text = ipv6Text(&reinterpret_cast<const sockaddr_in6*>(address)->sin6_addr);
      }
      if (text.empty()) continue;
      adapter.addresses.push_back(text + "/" + std::to_string(unicast->OnLinkPrefixLength));
    }
    adapters.push_back(std::move(adapter));
  }
  return adapters;
}

SocketTable readSockets() {
  SocketTable table;
  // Four tables are fetched one after another: TCP and UDP, each for IPv4
  // and IPv6. If one fails the others are still shown, and a note names the
  // missing one so the gap is visible.
  const ProcessNames names;
  bool anyTable = false;

  std::vector<BYTE> buffer;
  if (fetchTcp(AF_INET, buffer)) {
    anyTable = true;
    const auto* data = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buffer.data());
    for (DWORD i = 0; i < data->dwNumEntries; ++i) {
      const MIB_TCPROW_OWNER_PID& row = data->table[i];
      SocketEntry entry;
      entry.protocol = "TCP";
      entry.localAddress = ipv4Text(&row.dwLocalAddr);
      entry.localPort = hostPort(row.dwLocalPort);
      entry.state = tcpState(row.dwState);
      entry.listening = row.dwState == MIB_TCP_STATE_LISTEN;
      if (!entry.listening) {
        entry.remoteAddress = ipv4Text(&row.dwRemoteAddr);
        entry.remotePort = hostPort(row.dwRemotePort);
      }
      names.fill(entry, row.dwOwningPid);
      table.entries.push_back(std::move(entry));
    }
  } else {
    table.notes.push_back("TCP (IPv4) table: not available");
  }

  buffer.clear();
  if (fetchTcp(AF_INET6, buffer)) {
    anyTable = true;
    const auto* data = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(buffer.data());
    for (DWORD i = 0; i < data->dwNumEntries; ++i) {
      const MIB_TCP6ROW_OWNER_PID& row = data->table[i];
      SocketEntry entry;
      entry.protocol = "TCP";
      entry.ipv6 = true;
      entry.localAddress = ipv6Text(row.ucLocalAddr);
      entry.localPort = hostPort(row.dwLocalPort);
      entry.state = tcpState(row.dwState);
      entry.listening = row.dwState == MIB_TCP_STATE_LISTEN;
      if (!entry.listening) {
        entry.remoteAddress = ipv6Text(row.ucRemoteAddr);
        entry.remotePort = hostPort(row.dwRemotePort);
      }
      names.fill(entry, row.dwOwningPid);
      table.entries.push_back(std::move(entry));
    }
  } else {
    table.notes.push_back("TCP (IPv6) table: not available");
  }

  buffer.clear();
  // UDP has no connections and no LISTEN state. A socket bound to a port is
  // the closest thing to "listening", so bound UDP sockets are listed there.
  if (fetchUdp(AF_INET, buffer)) {
    anyTable = true;
    const auto* data = reinterpret_cast<const MIB_UDPTABLE_OWNER_PID*>(buffer.data());
    for (DWORD i = 0; i < data->dwNumEntries; ++i) {
      const MIB_UDPROW_OWNER_PID& row = data->table[i];
      SocketEntry entry;
      entry.protocol = "UDP";
      entry.localAddress = ipv4Text(&row.dwLocalAddr);
      entry.localPort = hostPort(row.dwLocalPort);
      entry.state = "BOUND";
      entry.listening = true;
      names.fill(entry, row.dwOwningPid);
      table.entries.push_back(std::move(entry));
    }
  } else {
    table.notes.push_back("UDP (IPv4) table: not available");
  }

  buffer.clear();
  if (fetchUdp(AF_INET6, buffer)) {
    anyTable = true;
    const auto* data = reinterpret_cast<const MIB_UDP6TABLE_OWNER_PID*>(buffer.data());
    for (DWORD i = 0; i < data->dwNumEntries; ++i) {
      const MIB_UDP6ROW_OWNER_PID& row = data->table[i];
      SocketEntry entry;
      entry.protocol = "UDP";
      entry.ipv6 = true;
      entry.localAddress = ipv6Text(row.ucLocalAddr);
      entry.localPort = hostPort(row.dwLocalPort);
      entry.state = "BOUND";
      entry.listening = true;
      names.fill(entry, row.dwOwningPid);
      table.entries.push_back(std::move(entry));
    }
  } else {
    table.notes.push_back("UDP (IPv6) table: not available");
  }

  table.ok = anyTable;
  return table;
}

}
