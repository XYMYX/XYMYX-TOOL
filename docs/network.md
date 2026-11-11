# network

Files: `src/features/network.cpp`, `src/platform/win/network_win.cpp`, `src/platform/mac/network_mac.cpp`

Menu item 2. Shows local IP addresses, network adapters, listening ports and active connections.

The tool only reads tables the operating system already keeps. It opens no socket and sends no packet.

## Background in five lines

- A **network adapter** is a network interface: Ethernet, Wi-Fi, or a virtual one such as loopback.
- An adapter has a **MAC address** (hardware) and one or more **IP addresses**.
- `192.168.1.20/24` means: the first 24 bits name the network, the rest names this device.
- A **port** is a number from 1 to 65535 that tells the system which program a packet is for.
- A program **listens** on a port when it waits for others to connect. A **connection** is an established link between a local and a remote address and port.

## Feature side

`runNetwork` makes two platform calls:

- `readAdapters` feeds "Local IP addresses" and "Network adapters",
- `readSockets` feeds "Listening ports" and "Active connections".

"Local IP addresses" is a filtered view of the adapter list: only adapters that are up and not loopback.

The listening list is sorted by protocol and port and exact duplicates are removed.

`endpoint` formats an address with its port. IPv6 addresses contain colons, so they are written in brackets: `[fe80::1]:445`.

## Windows

All calls come from the IP Helper API (`iphlpapi`), the library behind `ipconfig` and `netstat`.

### Adapters: `readAdapters`

`GetAdaptersAddresses` fills one buffer with a linked list: each adapter points to the next, and each adapter has its own linked list of addresses. `inet_ntop` converts the raw address bytes to text.

### Sockets: `readSockets`

`GetExtendedTcpTable` and `GetExtendedUdpTable` return every socket together with the ID of the process that owns it. They are called four times: TCP and UDP, IPv4 and IPv6. If one call fails, the other tables are still shown and a note names the missing one.

UDP has no connections. A UDP socket bound to a port is listed under "Listening ports" with state `BOUND`.

### Byte order

Port numbers in these tables are in "network byte order": most significant byte first. Intel and AMD CPUs store numbers the other way round. `hostPort` swaps the two bytes. Forgetting this is a classic bug: port 80 would show as 20480.

### From PID to process name: `ProcessNames`

The tables contain only process IDs. Getting names is where permissions matter.

1. **First choice:** `CreateToolhelp32Snapshot` takes a snapshot of all running processes including their file names. This works for system processes too, without admin rights.
2. **Fallback:** if a PID is not in the snapshot, `OpenProcess` with `PROCESS_QUERY_LIMITED_INFORMATION` and `QueryFullProcessImageNameW`.
3. **If that fails:** the name is shown as `not available (requires admin)` when Windows answered "access denied", otherwise as `not available`.

Because of step 1, names are normally available for all ports on Windows.

## macOS

The macOS code has not been compiled or tested yet.

### Adapters

`getifaddrs` returns one list node per address, not per adapter. `readAdapters` groups the nodes by adapter name. Nodes of family `AF_LINK` carry the MAC address.

### Sockets

A normal user on macOS cannot ask for one table of all sockets with owners. `readSockets` walks the processes instead, using `libproc`:

1. `proc_listpids`: all process IDs.
2. `proc_pidinfo` with `PROC_PIDLISTFDS`: the open file descriptors of one process.
3. `proc_pidfdinfo` with `PROC_PIDFDSOCKETINFO`: the details of one descriptor that is a socket.

### The permission limit

Step 2 only works for processes owned by the current user. For processes of other users or the system it fails with `EPERM`.

The code counts those processes and adds a note such as:

```
Sockets of 312 processes owned by other users or the system: not available (requires admin)
```

So on macOS without admin rights the list is **incomplete**, and the tool says so instead of presenting a partial list as the whole truth.

## Available without admin?

| Data | Windows | macOS |
| --- | --- | --- |
| Adapters and addresses | yes | yes |
| All listening ports and connections | yes | only those of your own processes |
| Process names | yes, in practice | for your own processes |

## Check it yourself

| System | Command to compare with |
| --- | --- |
| Windows | `netstat -ano` |
| macOS | `lsof -nP -iTCP -sTCP:LISTEN` |
