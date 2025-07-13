# DNS Cache Health Monitor
This tool runs from a non privileged windows user account. It shows which entries in the DNS resolver cache
are working/not working. 

It tests 10 entries every 15 seconds. It sets them into pages with status.

Sometimes if upstream, the IT department has migrated a lot of services around. The cache can
cause connection failure that is shown as connected but unauthenticated ( what I saw ).
I'm sure as other services move, it causes different issues. This tool hopes to identify when things aren't reachable in the cache.

## Screenshot

```
========================================================================================
                     DNS CACHE HEALTH MONITOR - 20:37:43                     
========================================================================================

CACHE HEALTH ANALYSIS:
----------------------------------------------------------------------------------------
Total Entries: 6   Reachable: 6   Stale: 0   Timeouts: 0   Slow: 0
Health: 100.0% EXCELLENT   Avg Response: 6.8ms
Recommendation: HEALTHY - Cache performing well

DNS CACHE ENTRIES (Page 1 of 1):
----------------------------------------------------------------------------------------
Status  Hostname                             IP Address       TTL    Response
----------------------------------------------------------------------------------------
  ●     array811.prod.do.dsp.mp.microsof...  20.191.76.110     808     1ms
  ?     blob.iad02prdstr17a.store.core.w...  57.150.0.33     39154   UNTESTED
  ●     prod-agic-ncu-1.northcentralus.c...  52.159.108.190      7     32ms
  ?     blob.sjc22prdstr10c.store.core.w...  20.209.102.193  26004   UNTESTED
  ?     array816.prod.do.dsp.mp.microsof...  52.137.125.63    1006   UNTESTED
  ?     avatars.githubusercontent.com        185.199.109.133   225   UNTESTED

LEGEND:
----------------------------------------------------------------------------------------
Status: ● Fast (<50ms)  ○ OK (<200ms)  ◦ Slow (>200ms)  X Timeout  S Stale

CONTROLS:
----------------------------------------------------------------------------------------
[F] Flush DNS Cache   [R] Refresh Cache List   [P] Pause/Resume   [Q] Quit
[→] Next Page   [←] Previous Page   [V] View Full Cache   [C] Network Config
```


### The Problem
When your DNS cache contains unreachable or stale entries, it can:
- Slow down DNS resolution for new requests
- Cause timeouts when the system tries to use bad cached entries
- Block access to updated IP addresses for services
- Degrade overall network performance

## Building and Running

### Requirements
- Windows 10/11
- Visual Studio 2019+ or compatible C++ compiler
- Administrator privileges (recommended for DNS cache operations)

### Compilation
```bash
# Using Visual Studio
cl /EHsc dns_cache_monitor.cpp ws2_32.lib iphlpapi.lib

# Using g++
g++ -o dns_cache_monitor.exe dns_cache_monitor.cpp -lws2_32 -liphlpapi
```

## Troubleshooting

### "UNTESTED" Entries
If entries show "UNTESTED" for extended periods:
- The tool tests entries gradually to avoid blocking
- Some entries may be tested less frequently
- Press `R` to force a cache refresh and restart testing

### No Cache Entries
If no entries are shown:
- DNS cache may be empty (recently flushed)
- Run some web browsing to populate the cache
- Check if running with sufficient privileges

### High Memory Usage
- The tool is lightweight and should use minimal resources
- If experiencing issues, restart the application
