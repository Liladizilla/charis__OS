# CharisOS networking status

## Implemented in the current networking change

- RTL8139 polling Ethernet frame transmit and receive.
- ARP replies for the configured local address, a bounded neighbour cache, sender identity checks and bounded neighbour resolution retries.
- IPv4 header validation, fragment rejection, ICMP echo replies and UDP receive dispatch.
- Static IPv4 next-hop selection using a subnet mask and optional gateway.
- IPv4 interface addresses and default gateways are validated as usable hosts for the configured subnet; network and directed-broadcast IDs are rejected. `/31` point-to-point endpoints and `/32` host routes are supported. Subnet masks must be contiguous and nonzero (`/1` through `/32`); `/0` is rejected because it would make every IPv4 destination appear on-link.
- ARP neighbour resolution is restricted to on-link hosts. Off-subnet traffic resolves only the configured, distinct on-link gateway MAC while retaining the remote IP destination; without a valid gateway, off-subnet sends fail closed.
- Outbound UDP/IPv4 frames with IPv4 and UDP checksums.
- A bounded asynchronous DNS A-record resolver that validates the echoed single QNAME, A/IN question and matching A-record owner, including bounded backward compression-pointer traversal.
- A minimal single-connection TCP client with SYN/SYN-ACK handshake, checksummed segments, validated single-segment ACK completion, ordered payload/FIN handling, data callbacks and FIN close.
- A plain HTTP GET client that composes DNS and TCP, parses bounded response headers, supports strict `Transfer-Encoding: chunked` framing and streams at most 256 KiB of decoded response body.
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

ARP replies are learned only when they match an outstanding neighbour request, target the local IPv4/MAC pair, and the ARP sender MAC matches the Ethernet source. Unrelated ARP requests and invalid or multicast sender identities are ignored. Repeated cache misses are coalesced, retransmitted on a one-second cadence for a bounded number of attempts, then held down before a later retry.

## DNS API

Kernel code can queue one lookup with `net_dns_query(hostname, callback, context)`. The callback executes in the network polling task. Only one lookup may be active at a time, and only IPv4 A records are returned. CNAME chasing, IPv6 AAAA records, cache persistence and resolver search domains are not implemented. The resolver rejects mismatched echoed questions, unrelated A-record owners, malformed compression pointers and unsupported query types/classes.

The resolver uses UDP port 53. It starts with `net_dns` and adopts the first DNS server supplied by a validated DHCP lease. Network configuration, gateway reachability and ARP resolution must be correct for it to succeed.

## Current limits

- DHCP is implemented but only exercised by packet-parser host tests and QEMU's virtual NIC capture. Physical LAN DHCP, relay agents, reconnect under loss and long-term lease behavior still need validation. IPv6 and IP fragment reassembly are not implemented.
- TCP is a constrained single-connection client, not a complete socket implementation. Congestion control, robust retransmission, multiple concurrent connections, receive-window reassembly, SACK and broad interoperability are missing. The current single-outstanding-segment client retires data only on an exact cumulative ACK and does not implement a general TCP receive window.
- TLS/HTTPS, certificate validation, redirects, chunked transfer decoding, full HTML/CSS layout, JavaScript and standards-compliant document rendering are not implemented.
- No Wi-Fi driver exists. Current physical networking is the RTL8139 wired Ethernet path only.
- IPv4 remains a small static/DHCPv4 client stack, not a full routing daemon. It does not implement multiple routes, route metrics, policy routing, ICMP redirects or IPv4 forwarding.
- End-to-end hardware tests against a real LAN and public Internet are still required.

The HTTP parser bounds headers to 2 KiB, chunk-size/trailer lines to 127 bytes, aggregate trailers to 1 KiB and decoded bodies to 256 KiB. It rejects conflicting Content-Length/Transfer-Encoding framing, unsupported transfer codings, malformed chunk delimiters and framing fields inside trailers. The browser validates DNS-style hostnames and request paths, strips URL fragments, extracts document titles and offers bounded text-preview scrolling. It remains a plain-HTTP text preview, not a general-purpose browser.

The kernel has a meaningful wired-networking foundation, but it is not yet a dependable general-purpose Internet stack. Do not treat successful kernel CI as proof of Internet connectivity.
