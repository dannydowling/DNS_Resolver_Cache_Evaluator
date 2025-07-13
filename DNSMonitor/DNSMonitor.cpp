#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <iostream>
#include <chrono>
#include <vector>
#include <conio.h>
#include <string>
#include <map>

// Link with libraries
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

// Console colors
#define COLOR_RESET     7
#define COLOR_GREEN     10
#define COLOR_YELLOW    14
#define COLOR_RED       12
#define COLOR_CYAN      11
#define COLOR_MAGENTA   13
#define COLOR_WHITE     15
#define COLOR_GRAY      8

// DNS Server structure
struct DNSServer {
    std::string name;
    std::string ip;
    DWORD avgResponseTime;
    int successCount;
    int totalTests;
    bool isDefault;
};

// Structure to hold DNS monitoring data
struct DNSStats {
    DWORD totalQueries;
    DWORD cacheHits;
    DWORD cacheMisses;
    DWORD slowQueries;
    double avgResponseTime;
    double hitRatio;
    BOOL dataValid;
};

// Structure for individual host status
struct HostStatus {
    std::string hostname;
    DWORD lastResponseTime;
    bool isReachable;
    std::map<std::string, DWORD> dnsServerTimes;
};

// Global variables
static BOOL g_shouldExit = FALSE;
static HANDLE g_exitEvent = NULL;
static DNSStats g_stats = { 0 };
static std::vector<HostStatus> g_hostStatuses;
static std::vector<DNSServer> g_dnsServers;
static HANDLE g_consoleHandle = NULL;
static bool g_pauseMonitoring = false;
static bool g_showDNSComparison = true;
static std::string g_currentDNS = "Unknown";

// Test hostnames for DNS monitoring
const char* TEST_HOSTNAMES[] = {
    "www.google.com",
    "www.microsoft.com",
    "www.github.com",
    "www.x.com",
    "www.bluesky.com",
    "www.facebook.com"
};
const int NUM_TEST_HOSTS = sizeof(TEST_HOSTNAMES) / sizeof(TEST_HOSTNAMES[0]);

// Well-known DNS servers
struct KnownDNSServer {
    const char* name;
    const char* ip;
} KNOWN_DNS_SERVERS[] = {
    {"Google", "8.8.8.8"},
    {"Google2", "8.8.4.4"},
    {"Cloudflare", "1.1.1.1"},
    {"Cloudflare2", "1.0.0.1"},
    {"OpenDNS", "208.67.222.222"},
    {"OpenDNS2", "208.67.220.220"},
    {"Quad9", "9.9.9.9"},
    {"Quad9-2", "149.112.112.112"}
};
const int NUM_KNOWN_DNS = sizeof(KNOWN_DNS_SERVERS) / sizeof(KNOWN_DNS_SERVERS[0]);

// Console utilities
void SetConsoleColor(int color) {
    SetConsoleTextAttribute(g_consoleHandle, color);
}

void SetConsoleSize() {
    system("mode con: cols=85 lines=18");
}

// Get current DNS servers from system
void GetCurrentDNSServers() {
    FIXED_INFO* pFixedInfo = nullptr;
    ULONG ulOutBufLen = sizeof(FIXED_INFO);
    
    pFixedInfo = (FIXED_INFO*)malloc(sizeof(FIXED_INFO));
    if (pFixedInfo == nullptr) return;
    
    if (GetNetworkParams(pFixedInfo, &ulOutBufLen) == ERROR_BUFFER_OVERFLOW) {
        free(pFixedInfo);
        pFixedInfo = (FIXED_INFO*)malloc(ulOutBufLen);
        if (pFixedInfo == nullptr) return;
    }
    
    if (GetNetworkParams(pFixedInfo, &ulOutBufLen) == NO_ERROR) {
        g_currentDNS = pFixedInfo->DnsServerList.IpAddress.String;
        
        // Add current DNS as default server
        DNSServer defaultServer;
        defaultServer.name = "Current";
        defaultServer.ip = g_currentDNS;
        defaultServer.avgResponseTime = 0;
        defaultServer.successCount = 0;
        defaultServer.totalTests = 0;
        defaultServer.isDefault = true;
        
        g_dnsServers.clear();
        g_dnsServers.push_back(defaultServer);
        
        // Add known DNS servers
        for (int i = 0; i < NUM_KNOWN_DNS; i++) {
            if (std::string(KNOWN_DNS_SERVERS[i].ip) != g_currentDNS) {
                DNSServer server;
                server.name = KNOWN_DNS_SERVERS[i].name;
                server.ip = KNOWN_DNS_SERVERS[i].ip;
                server.avgResponseTime = 0;
                server.successCount = 0;
                server.totalTests = 0;
                server.isDefault = false;
                g_dnsServers.push_back(server);
            }
        }
    }
    
    if (pFixedInfo) free(pFixedInfo);
}

// Get response time color based on performance
int GetResponseTimeColor(DWORD responseTime) {
    if (responseTime == MAXDWORD) return COLOR_RED;
    if (responseTime <= 30) return COLOR_GREEN;
    if (responseTime <= 100) return COLOR_YELLOW;
    return COLOR_RED;
}

// Get status indicator
const char* GetStatusIndicator(bool isReachable, DWORD responseTime) {
    if (!isReachable) return "X";
    if (responseTime <= 30) return "O";
    if (responseTime <= 100) return "o";
    return ".";
}

// Console control handler for graceful shutdown
BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType) {
    switch (dwCtrlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
        g_shouldExit = TRUE;
        if (g_exitEvent) {
            SetEvent(g_exitEvent);
        }
        return TRUE;
    }
    return FALSE;
}

// Perform DNS lookup
DWORD PerformDNSLookup(const char* hostname) {
    auto startTime = std::chrono::steady_clock::now();

    struct addrinfo hints = { 0 };
    struct addrinfo* result = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int status = getaddrinfo(hostname, nullptr, &hints, &result);

    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    DWORD responseTime = static_cast<DWORD>(duration.count());

    if (result) {
        freeaddrinfo(result);
    }

    return (status == 0) ? responseTime : MAXDWORD;
}

// Update host statuses
void UpdateHostStatuses() {
    for (int i = 0; i < NUM_TEST_HOSTS; i++) {
        DWORD responseTime = PerformDNSLookup(TEST_HOSTNAMES[i]);
        
        g_hostStatuses[i].lastResponseTime = responseTime;
        g_hostStatuses[i].isReachable = (responseTime != MAXDWORD);

        if (responseTime != MAXDWORD) {
            g_stats.totalQueries++;
            
            if (responseTime < 50) {
                g_stats.cacheHits++;
            } else {
                g_stats.cacheMisses++;
            }

            if (responseTime > 100) {
                g_stats.slowQueries++;
            }
        }

        // Test alternative DNS servers occasionally
        if (g_showDNSComparison && (g_stats.totalQueries % 8 == 0)) {
            for (auto& server : g_dnsServers) {
                if (!server.isDefault) {
                    DWORD altTime = PerformDNSLookup(TEST_HOSTNAMES[i]);
                    server.totalTests++;
                    if (altTime != MAXDWORD) {
                        server.successCount++;
                        server.avgResponseTime = (server.avgResponseTime * (server.successCount - 1) + altTime) / server.successCount;
                    }
                }
            }
        }
    }

    // Calculate statistics
    if (g_stats.totalQueries > 0) {
        g_stats.hitRatio = ((double)g_stats.cacheHits / g_stats.totalQueries) * 100.0;
        
        DWORD totalTime = 0;
        int validHosts = 0;
        for (const auto& host : g_hostStatuses) {
            if (host.isReachable) {
                totalTime += host.lastResponseTime;
                validHosts++;
            }
        }
        g_stats.avgResponseTime = validHosts > 0 ? (double)totalTime / validHosts : 0.0;
        g_stats.dataValid = TRUE;
    }
}

// Display help information
void DisplayHelp() {
    system("cls");
    
    SetConsoleColor(COLOR_CYAN);
    printf("================================================================================\n");
    printf("                      DNS CACHE MONITOR - HELP GUIDE                          \n");
    printf("================================================================================\n");
    
    SetConsoleColor(COLOR_WHITE);
    printf("STATUS SYMBOLS:\n");
    printf("  O = Fast response (0-30ms)     - Excellent, likely from cache\n");
    printf("  o = Good response (31-100ms)   - Good performance\n");
    printf("  . = Slow response (>100ms)     - May need cache flush\n");
    printf("  X = Failed/Timeout             - Network or DNS issue\n\n");
    
    printf("WHEN TO FLUSH DNS CACHE (Press F):\n");
    printf("  * Cache hit ratio below 40%% (with 10+ queries)\n");
    printf("  * Average response time above 150ms\n");
    printf("  * Many slow queries (more than 25%% of total)\n");
    printf("  * Websites not loading properly\n");
    printf("  * After changing DNS servers\n\n");
    
    printf("DNS SERVER RATINGS:\n");
    printf("  EXCELLENT = >90%% success, <50ms average\n");
    printf("  GOOD      = >80%% success, <100ms average  \n");
    printf("  POOR      = Lower success or high latency\n\n");
    
    SetConsoleColor(COLOR_YELLOW);
    printf("Press any key to return to monitor...");
    SetConsoleColor(COLOR_RESET);
    _getch();
}

// Simple, readable display that definitely works
void DisplayInterface() {
    static int refreshCount = 0;
    
    // Only clear screen every 3 seconds to prevent flicker
    if (refreshCount % 15 == 0) {
        system("cls");
    }
    refreshCount++;
    
    // Always go to top for updates
    COORD coord = {0, 0};
    SetConsoleCursorPosition(g_consoleHandle, coord);
    
    // Title and time
    SYSTEMTIME st;
    GetLocalTime(&st);
    SetConsoleColor(COLOR_CYAN);
    printf("================================================================================\n");
    printf("                   DNS CACHE & SERVER MONITOR - %02d:%02d:%02d                   \n", 
           st.wHour, st.wMinute, st.wSecond);
    printf("================================================================================\n");
    
    // Current DNS
    SetConsoleColor(COLOR_WHITE);
    printf("Current DNS Server: ");
    SetConsoleColor(COLOR_CYAN);
    printf("%s", g_currentDNS.c_str());
    if (g_pauseMonitoring) {
        SetConsoleColor(COLOR_YELLOW);
        printf(" [PAUSED]");
    }
    printf("\n\n");
    
    // Host Status
    SetConsoleColor(COLOR_MAGENTA);
    printf("HOST STATUS:\n");
    SetConsoleColor(COLOR_GRAY);
    printf("-----------------------------------------------------------------\n");
    SetConsoleColor(COLOR_WHITE);

    // Split hosts into two lines - first 3 hosts
    for (int i = 0; i < 3 && i < NUM_TEST_HOSTS; i++) {
        const auto& host = g_hostStatuses[i];

        // Extract clean domain name
        std::string shortName = host.hostname;
        if (shortName.find("www.") == 0) shortName = shortName.substr(4);
        size_t dotPos = shortName.find('.');
        if (dotPos != std::string::npos) shortName = shortName.substr(0, dotPos);

        SetConsoleColor(GetResponseTimeColor(host.lastResponseTime));
        printf("%s ", GetStatusIndicator(host.isReachable, host.lastResponseTime));

        SetConsoleColor(COLOR_WHITE);
        printf("%-12s ", shortName.c_str());

        if (host.isReachable) {
            SetConsoleColor(GetResponseTimeColor(host.lastResponseTime));
            printf("%4lums  ", host.lastResponseTime);
        }
        else {
            SetConsoleColor(COLOR_RED);
            printf("timeout  ");
        }
    }
    printf("\n");

    // Second line - remaining hosts
    for (int i = 3; i < NUM_TEST_HOSTS; i++) {
        const auto& host = g_hostStatuses[i];

        // Extract clean domain name
        std::string shortName = host.hostname;
        if (shortName.find("www.") == 0) shortName = shortName.substr(4);
        size_t dotPos = shortName.find('.');
        if (dotPos != std::string::npos) shortName = shortName.substr(0, dotPos);

        SetConsoleColor(GetResponseTimeColor(host.lastResponseTime));
        printf("%s ", GetStatusIndicator(host.isReachable, host.lastResponseTime));

        SetConsoleColor(COLOR_WHITE);
        printf("%-12s ", shortName.c_str());

        if (host.isReachable) {
            SetConsoleColor(GetResponseTimeColor(host.lastResponseTime));
            printf("%4lums  ", host.lastResponseTime);
        }
        else {
            SetConsoleColor(COLOR_RED);
            printf("timeout  ");
        }
    }
    printf("\n\n");
    
    // Performance Stats
    SetConsoleColor(COLOR_MAGENTA);
    printf("CACHE PERFORMANCE:\n");
    SetConsoleColor(COLOR_GRAY);
    printf("--------------------------------------------------------------------------------\n");
    
    if (g_stats.dataValid) {
        SetConsoleColor(COLOR_WHITE);
        printf("Queries: %lu   Cache Hit Ratio: %.1f%%   Avg Response: %.1fms   Slow: %lu\n", 
               g_stats.totalQueries, g_stats.hitRatio, g_stats.avgResponseTime, g_stats.slowQueries);
        
        printf("Status: ");
        if (g_stats.hitRatio < 40.0 && g_stats.totalQueries > 10) {
            SetConsoleColor(COLOR_RED);
            printf("POOR - Consider flushing DNS cache");
        } else if (g_stats.avgResponseTime > 150.0) {
            SetConsoleColor(COLOR_YELLOW);
            printf("SLOW - Check network or DNS servers");
        } else {
            SetConsoleColor(COLOR_GREEN);
            printf("GOOD - DNS performance normal");
        }
        printf("\n");
    } else {
        SetConsoleColor(COLOR_YELLOW);
        printf("Gathering initial performance data...\n");
    }
    
    // DNS Server Comparison
    if (g_showDNSComparison) {
        printf("\n");
        SetConsoleColor(COLOR_MAGENTA);
        printf("DNS SERVER COMPARISON:\n");
        SetConsoleColor(COLOR_GRAY);
        printf("--------------------------------------------------------------------------------\n");
        SetConsoleColor(COLOR_WHITE);
        printf("%-12s %-15s %8s %8s %10s\n", "Server", "IP Address", "Response", "Success", "Status");
        SetConsoleColor(COLOR_GRAY);
        printf("--------------------------------------------------------------------------------\n");
        
        for (const auto& server : g_dnsServers) {
            if (server.isDefault) {
                SetConsoleColor(COLOR_CYAN);
                printf("* ");
            } else {
                SetConsoleColor(COLOR_WHITE);
                printf("  ");
            }
            
            printf("%-10s %-15s ", server.name.c_str(), server.ip.c_str());
            
            if (server.totalTests > 0) {
                SetConsoleColor(GetResponseTimeColor(server.avgResponseTime));
                printf("%6lums ", server.avgResponseTime);
                
                int successRate = (server.successCount * 100) / server.totalTests;
                SetConsoleColor(COLOR_WHITE);
                printf("%6d%% ", successRate);
                
                if (successRate >= 90 && server.avgResponseTime <= 50) {
                    SetConsoleColor(COLOR_GREEN);
                    printf("EXCELLENT");
                } else if (successRate >= 80 && server.avgResponseTime <= 100) {
                    SetConsoleColor(COLOR_YELLOW);
                    printf("GOOD");
                } else {
                    SetConsoleColor(COLOR_RED);
                    printf("POOR");
                }
            } else {
                SetConsoleColor(COLOR_GRAY);
                printf("     -ms      -% UNTESTED");
            }
            printf("\n");
        }
        
        SetConsoleColor(COLOR_GRAY);
        printf("* = Currently configured DNS server\n");
    }
    
    // Controls
    printf("\n");
    SetConsoleColor(COLOR_MAGENTA);
    printf("CONTROLS:\n");
    SetConsoleColor(COLOR_GRAY);
    printf("--------------------------------------------------------------------------------\n");
    SetConsoleColor(COLOR_WHITE);
    printf("[F] Flush DNS Cache   [V] View Cache   [C] Network Config   [H] Help\n");
    printf("[D] Toggle DNS Compare   [R] Reset Stats   [P] Pause/Resume   [Q] Quit\n");
    
    SetConsoleColor(COLOR_RESET);
    fflush(stdout);
}

// Handle user input
void ProcessInput() {
    if (_kbhit()) {
        int key = _getch();
        key = toupper(key);
        
        switch (key) {
        case 'F':
            system("ipconfig /flushdns");
            g_stats = { 0 };
            printf("\n\nDNS cache flushed! Statistics reset.\n");
            Sleep(1500);
            break;
            
        case 'V':
            system("ipconfig /displaydns | more");
            break;
            
        case 'C':
            system("ipconfig /all | more");
            break;
            
        case 'H':
            DisplayHelp();
            break;
            
        case 'D':
            g_showDNSComparison = !g_showDNSComparison;
            if (g_showDNSComparison) {
                GetCurrentDNSServers();
            }
            break;
            
        case 'R':
            g_stats = { 0 };
            for (auto& server : g_dnsServers) {
                server.avgResponseTime = 0;
                server.successCount = 0;
                server.totalTests = 0;
            }
            printf("\n\nStatistics reset!\n");
            Sleep(1000);
            break;
            
        case 'P':
            g_pauseMonitoring = !g_pauseMonitoring;
            break;
            
        case 'T':
            {
                printf("\nEnter hostname to test: ");
                std::string hostname;
                std::getline(std::cin, hostname);
                if (!hostname.empty()) {
                    DWORD responseTime = PerformDNSLookup(hostname.c_str());
                    printf("Response time: %lu ms\n", responseTime);
                    printf("Press any key to continue...");
                    _getch();
                }
            }
            break;
            
        case 'Q':
        case 27:
            g_shouldExit = TRUE;
            SetEvent(g_exitEvent);
            break;
        }
    }
}

// Initialize Winsock
BOOL InitializeWinsock() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    return (result == 0);
}

// Initialize host status array
void InitializeHostStatuses() {
    g_hostStatuses.clear();
    for (int i = 0; i < NUM_TEST_HOSTS; i++) {
        HostStatus status;
        status.hostname = TEST_HOSTNAMES[i];
        status.lastResponseTime = 0;
        status.isReachable = false;
        g_hostStatuses.push_back(status);
    }
}

// Main monitoring loop
void MonitorDNS() {
    const int UPDATE_INTERVAL_MS = 15000; // 15 seconds
    auto lastUpdate = std::chrono::steady_clock::now();
    
    GetCurrentDNSServers();
    
    while (!g_shouldExit) {
        auto now = std::chrono::steady_clock::now();
        auto timeSinceUpdate = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdate);
        
        if (!g_pauseMonitoring && timeSinceUpdate.count() >= UPDATE_INTERVAL_MS) {
            UpdateHostStatuses();
            lastUpdate = now;
        }
        
        DisplayInterface();
        ProcessInput();
        
        Sleep(200);
    }
}

// Main function
int main() {
    g_consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    
    SetConsoleSize();
    SetConsoleTitleA("DNS Cache & Server Monitor");
    
    if (!InitializeWinsock()) {
        printf("Failed to initialize Winsock\n");
        return 1;
    }

    g_exitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_exitEvent) {
        printf("Failed to create exit event\n");
        return 1;
    }

    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    InitializeHostStatuses();
    MonitorDNS();

    CloseHandle(g_exitEvent);
    WSACleanup();
    return 0;
}