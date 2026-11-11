// network_mac.cpp - adapters, addresses, ports and connections on macOS.
//
// Adapters come from getifaddrs. Sockets come from libproc, the library
// behind tools like lsof: it lets a process look at the open files and
// sockets of other processes, as far as its rights allow.
//
// Not compiled or tested yet. See the note in README.md.

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <libproc.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/proc_info.h>
#include <sys/socket.h>

#include <bit>
#include <cerrno>
#include <cstdio>
#include <map>

#include "platform/platform.h"

namespace platform {

namespace {

// A netmask such as 255.255.255.0 is 24 one-bits followed by zeros. Counting
// the one-bits gives the short form "/24" that is shown after the address.
int prefixLength(const sockaddr* mask) {
  if (mask == nullptr) return -1;
  if (mask->sa_family == AF_INET) {
    return std::popcount(
        static_cast<std::uint32_t>(reinterpret_cast<const sockaddr_in*>(mask)->sin_addr.s_addr));
  }
  if (mask->sa_family == AF_INET6) {
    int bits = 0;
    for (unsigned char byte : reinterpret_cast<const sockaddr_in6*>(mask)->sin6_addr.s6_addr) {
      bits += std::popcount(static_cast<unsigned int>(byte));
    }
    return bits;
  }
  return -1;
}

const char* tcpState(int state) {
  switch (state) {
    case TSI_S_CLOSED: return "CLOSED";
    case TSI_S_LISTEN: return "LISTEN";
    case TSI_S_SYN_SENT: return "SYN_SENT";
    case TSI_S_SYN_RECEIVED: return "SYN_RECEIVED";
    case TSI_S_ESTABLISHED: return "ESTABLISHED";
    case TSI_S__CLOSE_WAIT: return "CLOSE_WAIT";
    case TSI_S_FIN_WAIT_1: return "FIN_WAIT_1";
    case TSI_S_CLOSING: return "CLOSING";
    case TSI_S_LAST_ACK: return "LAST_ACK";
    case TSI_S_FIN_WAIT_2: return "FIN_WAIT_2";
    case TSI_S_TIME_WAIT: return "TIME_WAIT";
    default: return "UNKNOWN";
  }
}

std::string addressText(const in_sockinfo& info, bool ipv6, bool local) {
  char buffer[INET6_ADDRSTRLEN] = {};
  if (ipv6) {
    const in6_addr& address = local ? info.insi_laddr.ina_6 : info.insi_faddr.ina_6;
    if (inet_ntop(AF_INET6, &address, buffer, sizeof(buffer)) == nullptr) return {};
  } else {
    const in_addr& address =
        local ? info.insi_laddr.ina_46.i46a_addr4 : info.insi_faddr.ina_46.i46a_addr4;
    if (inet_ntop(AF_INET, &address, buffer, sizeof(buffer)) == nullptr) return {};
  }
  return buffer;
}

}

std::optional<std::vector<AdapterInfo>> readAdapters() {
  // getifaddrs returns a linked list with one node per address, not per
  // adapter. An adapter with an IPv4, an IPv6 and a hardware address shows up
  // three times, so nodes are grouped by adapter name. The list is allocated
  // by the system and released with freeifaddrs at the end.
  ifaddrs* list = nullptr;
  if (getifaddrs(&list) != 0) return std::nullopt;

  std::vector<AdapterInfo> adapters;
  std::map<std::string, std::size_t> index;
  for (const ifaddrs* item = list; item != nullptr; item = item->ifa_next) {
    if (item->ifa_name == nullptr) continue;
    const std::string name = item->ifa_name;
    auto [position, inserted] = index.try_emplace(name, adapters.size());
    if (inserted) {
      AdapterInfo adapter;
      adapter.name = name;
      adapter.up = (item->ifa_flags & IFF_UP) != 0 && (item->ifa_flags & IFF_RUNNING) != 0;
      adapter.loopback = (item->ifa_flags & IFF_LOOPBACK) != 0;
      if (adapter.loopback) adapter.type = "Loopback";
      adapters.push_back(std::move(adapter));
    }
    AdapterInfo& adapter = adapters[position->second];
    const sockaddr* address = item->ifa_addr;
    if (address == nullptr) continue;

    // AF_LINK is the hardware level: this node carries the MAC address.
    if (address->sa_family == AF_LINK) {
      const sockaddr_dl* link = reinterpret_cast<const sockaddr_dl*>(address);
      if (link->sdl_alen == 6) {
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(LLADDR(link));
        char mac[32];
        std::snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", bytes[0], bytes[1],
                      bytes[2], bytes[3], bytes[4], bytes[5]);
        adapter.mac = mac;
      }
    } else if (address->sa_family == AF_INET || address->sa_family == AF_INET6) {
      char host[NI_MAXHOST] = {};
      if (getnameinfo(address, address->sa_len, host, sizeof(host), nullptr, 0, NI_NUMERICHOST) !=
          0) {
        continue;
      }
      std::string text = host;
      const std::size_t scope = text.find('%');
      if (scope != std::string::npos) text.resize(scope);
      const int prefix = prefixLength(item->ifa_netmask);
      if (prefix >= 0) text += "/" + std::to_string(prefix);
      adapter.addresses.push_back(text);
    }
  }
  freeifaddrs(list);
  return adapters;
}

SocketTable readSockets() {
  // macOS has no single "all sockets with their owner" call for normal
  // users. Instead we go process by process:
  //   1. proc_listpids  -> all process IDs
  //   2. proc_pidinfo   -> the open file descriptors of one process
  //   3. proc_pidfdinfo -> details of one descriptor, if it is a socket
  SocketTable table;
  int bytes = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
  if (bytes <= 0) return table;
  std::vector<pid_t> pids(static_cast<std::size_t>(bytes) / sizeof(pid_t) + 64);
  bytes = proc_listpids(PROC_ALL_PIDS, 0, pids.data(),
                        static_cast<int>(pids.size() * sizeof(pid_t)));
  if (bytes <= 0) return table;
  pids.resize(static_cast<std::size_t>(bytes) / sizeof(pid_t));
  table.ok = true;

  unsigned unreadable = 0;
  for (pid_t pid : pids) {
    if (pid <= 0) continue;
    // This is where missing admin rights show. A normal user may inspect
    // only their own processes; for all others the call fails with EPERM.
    // Those processes are counted, and the count is reported in a note, so
    // the user knows the list is incomplete. Other errors usually mean the
    // process ended in the meantime and are ignored.
    errno = 0;
    int size = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, nullptr, 0);
    if (size <= 0) {
      if (errno == EPERM || errno == EACCES) ++unreadable;
      continue;
    }
    std::vector<proc_fdinfo> fds(static_cast<std::size_t>(size) / sizeof(proc_fdinfo) + 16);
    errno = 0;
    size = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, fds.data(),
                        static_cast<int>(fds.size() * sizeof(proc_fdinfo)));
    if (size <= 0) {
      if (errno == EPERM || errno == EACCES) ++unreadable;
      continue;
    }
    fds.resize(static_cast<std::size_t>(size) / sizeof(proc_fdinfo));

    std::optional<std::string> processName;
    char nameBuffer[2 * MAXCOMLEN + 1] = {};
    if (proc_name(pid, nameBuffer, sizeof(nameBuffer)) > 0) processName = nameBuffer;

    for (const proc_fdinfo& fd : fds) {
      if (fd.proc_fdtype != PROX_FDTYPE_SOCKET) continue;
      socket_fdinfo socketInfo{};
      if (proc_pidfdinfo(pid, fd.proc_fd, PROC_PIDFDSOCKETINFO, &socketInfo, sizeof(socketInfo)) !=
          static_cast<int>(sizeof(socketInfo))) {
        continue;
      }
      const int family = socketInfo.psi.soi_family;
      if (family != AF_INET && family != AF_INET6) continue;

      SocketEntry entry;
      entry.ipv6 = family == AF_INET6;
      // TCP and UDP sockets keep their addresses in different members of
      // the same union. "inet" points at whichever one applies.
      const in_sockinfo* inet = nullptr;
      if (socketInfo.psi.soi_kind == SOCKINFO_TCP) {
        const tcp_sockinfo& tcp = socketInfo.psi.soi_proto.pri_tcp;
        inet = &tcp.tcpsi_ini;
        entry.protocol = "TCP";
        entry.state = tcpState(tcp.tcpsi_state);
        entry.listening = tcp.tcpsi_state == TSI_S_LISTEN;
        if (tcp.tcpsi_state == TSI_S_CLOSED) continue;
      } else if (socketInfo.psi.soi_kind == SOCKINFO_IN &&
                 socketInfo.psi.soi_protocol == IPPROTO_UDP) {
        inet = &socketInfo.psi.soi_proto.pri_in;
        entry.protocol = "UDP";
        entry.state = "BOUND";
        entry.listening = true;
      } else {
        continue;
      }

      // Ports are stored in network byte order; ntohs converts to the CPU's
      // order. Port 0 means the socket exists but is not bound to anything.
      entry.localPort = ntohs(static_cast<std::uint16_t>(inet->insi_lport));
      if (entry.localPort == 0) continue;
      entry.localAddress = addressText(*inet, entry.ipv6, true);
      if (!entry.listening) {
        entry.remoteAddress = addressText(*inet, entry.ipv6, false);
        entry.remotePort = ntohs(static_cast<std::uint16_t>(inet->insi_fport));
      }
      entry.pid = static_cast<std::uint32_t>(pid);
      if (processName) {
        entry.process = processName;
      } else {
        entry.processNote = kNotAvailable;
      }
      table.entries.push_back(std::move(entry));
    }
  }

  if (unreadable > 0) {
    table.notes.push_back("Sockets of " + std::to_string(unreadable) +
                          " processes owned by other users or the system: " + kNeedsAdmin);
  }
  return table;
}

}
