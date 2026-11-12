# CharisOS networking status

## Implemented in the current networking change

- RTL8139 polling Ethernet frame transmit and receive.
- ARP replies for the configured local address and a bounded neighbour cache.
- IPv4 header validation, fragment rejection, ICMP echo replies and UDP receive dispatch.
- Static IPv4 next-hop selection using a subnet mask and optional gateway.
- Outbound UDP/IPv4 frames with IPv4 and UDP checksums.
- A bounded asynchronous DNS A-record resolver.
- A minimal single-connection TCP client with SYN/SYN-ACK handshake, checksummed segments, basic ACK handling, data callbacks and FIN close.
- A plain HTTP GET client that composes DNS and TCP, parses bounded response headers and streams response body chunks.
- A bounded DHCPv4 client that performs DISCOVER/OFFER/REQUEST/ACK, installs subnet/router/DNS settings, renews at T1 by unicast and rebinds at T2 by broadcast, and stops using the lease after expiry.
- Browser integration for `http://` URLs, with a bounded HTML-tag-stripped text preview; `https://` remains explicitly unsupported.

## Configuration

The settings below are examples, not values to copy blindly. Replace them with values valid on the actual LAN.

```ini
# DHCP example (default when net_ip is absent or invalid):
net_enabled=true
net_dhcp=true

# Static alternative:
# net_dhcp=false
# net_ip=192.168.1.20
# net_mask=255.255.255.0
# net_gateway=192.168.1.1
# net_dns=1.1.1.1
```

When a valid `net_ip` is present, static IPv4 configuration is used by default. When no valid static address exists, DHCP is enabled by default; `net_dhcp=true` explicitly enables DHCP and `net_dhcp=false` disables it. DHCP lease addresses, subnet mask, router and DNS options are validated before application. DHCP leases are runtime state and are not written to persistent configuration. If DHCP fails, the interface remains without an IPv4 address rather than inventing one.

## DNS API

Kernel code can queue one lookup with `net_dns_query(hostname, callback, context)`. The callback executes in the network polling task. Only one lookup may be active at a time, and only IPv4 A records are returned. CNAME chasing, IPv6 AAAA records, cache persistence and resolver search domains are not implemented.

The resolver uses UDP port 53. It starts with `net_dns` and adopts the first DNS server supplied by a validated DHCP lease. Network configuration, gateway reachability and ARP resolution must be correct for it to succeed.

## Current limits

- DHCP is implemented but only exercised by packet-parser host tests and QEMU's virtual NIC capture. Physical LAN DHCP, relay agents, reconnect under loss and long-term lease behavior still need validation. IPv6 and IP fragment reassembly are not implemented.
- TCP is a constrained single-connection client, not a complete socket implementation. Congestion control, robust retransmission, multiple concurrent connections and broad interoperability are missing.
- TLS/HTTPS, certificate validation, redirects, chunked transfer decoding, full HTML/CSS layout, JavaScript and standards-compliant document rendering are not implemented.
- No Wi-Fi driver exists. Current physical networking is the RTL8139 wired Ethernet path only.
- End-to-end hardware tests against a real LAN and public Internet are still required.

The kernel has a meaningful wired-networking foundation, but it is not yet a dependable general-purpose Internet stack. Do not treat successful kernel CI as proof of Internet connectivity.
