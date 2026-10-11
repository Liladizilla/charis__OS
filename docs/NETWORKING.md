# CharisOS networking status

## Implemented in the current networking change

- RTL8139 polling Ethernet frame transmit and receive.
- ARP replies for the configured local address and a bounded neighbour cache.
- IPv4 header validation, fragment rejection, ICMP echo replies and UDP receive dispatch.
- Static IPv4 next-hop selection using a subnet mask and optional gateway.
- Outbound UDP/IPv4 frames with IPv4 and UDP checksums.
- A bounded asynchronous DNS A-record resolver.
- A minimal single-connection TCP client with SYN/SYN-ACK handshake, checksummed segments, basic ACK handling, data callbacks and FIN close.
- A plain HTTP GET client that composes DNS and TCP, parses bounded response headers, decodes chunked transfer bodies and streams response body chunks.
- Browser integration for `http://` URLs, with a bounded HTML-tag-stripped text preview; `https://` remains explicitly unsupported.
- Browser URL validation for DNS-style hostnames and the default port 80, including fragment stripping and query-only request targets.
- Visible status feedback for HTTP redirects and server errors; redirects are reported, not followed.
- HTML document-title extraction and viewport-width text wrapping for remote and local previews.
- PageUp/PageDown scrolling that follows the same visual row boundaries as the text renderer.

## Configuration

The settings below are examples, not values to copy blindly. Replace them with values valid on the actual LAN.

```ini
net_enabled=true
net_ip=192.168.1.20
net_mask=255.255.255.0
net_gateway=192.168.1.1
net_dns=1.1.1.1
```

If `net_ip` is absent or invalid, local IPv4 protocol handling remains disabled. An invalid subnet mask falls back to `255.255.255.0`. A missing gateway allows only destinations considered on-link by the configured mask. There is no DHCP client yet.

## DNS API

Kernel code can queue one lookup with `net_dns_query(hostname, callback, context)`. The callback executes in the network polling task. Only one lookup may be active at a time, and only IPv4 A records are returned. CNAME chasing, IPv6 AAAA records, cache persistence and resolver search domains are not implemented.

The resolver uses UDP port 53 and the configured `net_dns` address. Network configuration, gateway reachability and ARP resolution must be correct for it to succeed.

## Not implemented yet

- DHCP, IPv6 and IP fragment reassembly.
- Full TCP socket support, congestion control, robust retransmission timers, multiple concurrent connections and TLS.
- HTTPS/TLS, redirects, full HTML/CSS layout, JavaScript and standards-compliant document rendering.
- End-to-end hardware tests against a real LAN.

The browser can fetch plain HTTP when static IP, gateway, DNS and ARP are configured correctly, but it remains a bounded text preview rather than a modern browser. HTTPS/TLS, redirects, JavaScript, full HTML/CSS layout and standards-compliant document rendering are not implemented. Do not treat successful kernel CI as proof of Internet connectivity.
