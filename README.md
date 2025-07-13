# DNS Cache Status Monitor
It tries to monitor a few websites and evaluate through dns querying
whether the resolver cache is poluted with old information.

From what I saw the other day, when windows says it's connected by has that empty globe icon
and says you're connected but unauthenticated. It might be the resolver cache.
Here I try to make this tool to test the theory. 

Doesn't need admin, written in native c.

Made after fixing a connection issue by running ipconfig /flushdns



### 🎮 Interactive Controls
- **[F]** - Flush DNS Cache (`ipconfig /flushdns`)
- **[V]** - View DNS Cache Contents (`ipconfig /displaydns`)
- **[C]** - Show Network Configuration (`ipconfig /all`)
- **[R]** - Reset Statistics
- **[P]** - Pause/Resume Monitoring
- **[T]** - Test Custom Hostname
- **[Q]** - Quit Application

### Cache Detection Algorithm
The tool estimates cache performance by analyzing response time patterns:
- **Fast responses** (<50ms): Likely cache hits
- **Slow responses** (≥50ms): Likely cache misses
- **Timeout responses**: Network or DNS server issues

### System Requirements
- Windows 10/11 or Windows Server 2019+
- Visual C++ Redistributable (if not compiling locally)
- Administrative privileges (recommended for some network commands)
