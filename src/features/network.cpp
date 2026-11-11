// network.cpp - menu item 2, network.
//
// Prints four sections from two platform calls: readAdapters() feeds
// "Local IP addresses" and "Network adapters", readSockets() feeds
// "Listening ports" and "Active connections".

#include <algorithm>
#include <tuple>

#include "core/report.h"
#include "core/util.h"
#include "features/features.h"
#include "platform/platform.h"

namespace {

// IPv6 addresses contain colons themselves, so the usual "address:port"
// would be ambiguous. The convention is to put the address in brackets.
std::string endpoint(const std::string& address, std::uint16_t port, bool ipv6) {
  const std::string host = address.empty() ? "?" : address;
  return (ipv6 ? "[" + host + "]" : host) + ":" + std::to_string(port);
}

std::string processText(const platform::SocketEntry& entry) {
  if (entry.process) return *entry.process;
  return entry.processNote.empty() ? std::string(platform::kNotAvailable) : entry.processNote;
}

std::string pidText(const platform::SocketEntry& entry) {
  return entry.pid ? std::to_string(*entry.pid) : std::string("-");
}

}

void runNetwork() {
  Report report("Network");

  const auto adapters = platform::readAdapters();

  report.heading("Local IP addresses");
  if (!adapters) {
    report.row("Addresses", platform::kNotAvailable);
  } else {
    bool any = false;
    for (const platform::AdapterInfo& adapter : *adapters) {
      // Loopback (127.0.0.1) and disconnected adapters are not addresses
      // this computer can be reached at, so they are left out of this list.
      if (!adapter.up || adapter.loopback) continue;
      for (const std::string& address : adapter.addresses) {
        report.line("  " + util::padRight(address, 44) + " " + adapter.name);
        any = true;
      }
    }
    if (!any) report.line("  none (no active adapter with an address)");
  }

  report.heading("Network adapters");
  if (!adapters) {
    report.row("Adapters", platform::kNotAvailable);
  } else if (adapters->empty()) {
    report.line("  none reported");
  } else {
    for (const platform::AdapterInfo& adapter : *adapters) {
      report.line("  " + adapter.name + "  [" + (adapter.up ? "up" : "down") + "]");
      if (!adapter.description.empty()) report.line("      " + adapter.description);
      std::string meta;
      if (!adapter.type.empty()) meta += "type " + adapter.type;
      if (!adapter.mac.empty()) meta += (meta.empty() ? "" : ", ") + ("MAC " + adapter.mac);
      if (!meta.empty()) report.line("      " + meta);
      for (const std::string& address : adapter.addresses) report.line("      " + address);
    }
  }

  platform::SocketTable table = platform::readSockets();

  report.heading("Listening ports");
  if (!table.ok) {
    report.row("Ports", platform::kNotAvailable);
  } else {
    std::vector<platform::SocketEntry> listening;
    for (const platform::SocketEntry& entry : table.entries) {
      if (entry.listening) listening.push_back(entry);
    }
    // Sort by protocol and port so the list is easy to scan, then drop exact
    // duplicates. On macOS a socket shared by a parent and child process is
    // reported once per file descriptor.
    auto key = [](const platform::SocketEntry& e) {
      return std::tie(e.protocol, e.localPort, e.localAddress, e.pid);
    };
    std::sort(listening.begin(), listening.end(),
              [&](const auto& a, const auto& b) { return key(a) < key(b); });
    listening.erase(std::unique(listening.begin(), listening.end(),
                                [&](const auto& a, const auto& b) { return key(a) == key(b); }),
                    listening.end());
    if (listening.empty()) {
      report.line("  none found");
    } else {
      report.line("  " + util::padRight("PROTO", 6) + util::padRight("LOCAL ADDRESS", 48) +
                  util::padRight("PID", 8) + "PROCESS");
      for (const platform::SocketEntry& entry : listening) {
        report.line("  " + util::padRight(entry.protocol, 6) +
                    util::padRight(endpoint(entry.localAddress, entry.localPort, entry.ipv6), 48) +
                    util::padRight(pidText(entry), 8) + processText(entry));
      }
    }
  }

  report.heading("Active connections");
  if (!table.ok) {
    report.row("Connections", platform::kNotAvailable);
  } else {
    std::size_t count = 0;
    for (const platform::SocketEntry& entry : table.entries) {
      if (entry.listening) continue;
      if (count == 0) {
        report.line("  " + util::padRight("PROTO", 6) + util::padRight("STATE", 14) +
                    util::padRight("PID", 8) + "PROCESS");
      }
      ++count;
      report.line("  " + util::padRight(entry.protocol, 6) + util::padRight(entry.state, 14) +
                  util::padRight(pidText(entry), 8) + processText(entry));
      report.line("        " + endpoint(entry.localAddress, entry.localPort, entry.ipv6) + "  ->  " +
                  endpoint(entry.remoteAddress, entry.remotePort, entry.ipv6));
    }
    if (count == 0) report.line("  none found");
  }

  if (!table.notes.empty()) {
    report.blank();
    for (const std::string& note : table.notes) report.note("  " + note);
  }

  offerSave(report);
}
