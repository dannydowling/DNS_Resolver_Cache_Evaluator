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
#include <sstream>
#include <algorithm>
#include <regex>

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

// Cache entry structure
struct CacheEntry {
    std::string hostname;
    std::string recordType;
    std::string ipAddress;
    int ttl;
    DWORD lastResponseTime;
    bool isReachable;
    bool isStale;
    std::chrono::steady_clock::time_point lastTested;
};

// Cache statistics
struct CacheStats {
    int totalEntries;
    int reachableEntries;
    int staleEntries;
    int timeoutEntries;
    int slowEntries;
    double avgResponseTime;
    double healthPercentage;
    int pagesTotal;
    int currentPage;
    bool needsFlush;
};

// Global variables
static BOOL g_shouldExit = FALSE;
static HANDLE g_exitEvent = NULL;
static std::vector<CacheEntry> g_cacheEntries;
static CacheStats g_stats = { 0 };
static HANDLE g_consoleHandle = NULL;
static bool g_pauseMonitoring = false;
static int g_currentPage = 0;
static const int ENTRIES_PER_PAGE = 8;
static const int TEST_TIMEOUT_MS = 3000;
static const int SLOW_RESPONSE_THRESHOLD = 200;

// Console utilities
void SetConsoleColor(int color) {
    SetConsoleTextAttribute(g_consoleHandle, color);
}

void SetConsoleSize() {
    system("mode con: cols=90 lines=25");
}

// Parse DNS cache output
std::vector<CacheEntry> ParseDNSCache() {
    std::vector<CacheEntry> entries;

    // Create a temporary file to capture ipconfig output
    char tempFile[MAX_PATH];
    GetTempPathA(MAX_PATH, tempFile);
    strcat_s(tempFile, "dns_cache_temp.txt");

    std::string command = "ipconfig /displaydns > ";
    command += tempFile;
    system(command.c_str());

    // Read the file
    FILE* file = nullptr;
    fopen_s(&file, tempFile, "r");
    if (!file) return entries;

    char line[1024];
    CacheEntry currentEntry;
    bool inEntry = false;

    while (fgets(line, sizeof(line), file)) {
        std::string lineStr(line);

        // Remove trailing newline
        if (!lineStr.empty() && lineStr.back() == '\n') {
            lineStr.pop_back();
        }
        if (!lineStr.empty() && lineStr.back() == '\r') {
            lineStr.pop_back();
        }

        // Skip empty lines and headers
        if (lineStr.empty() || lineStr.find("Windows IP Configuration") != std::string::npos) {
            continue;
        }

        // Check for separator lines
        if (lineStr.find("-------") != std::string::npos) {
            if (inEntry && !currentEntry.hostname.empty()) {
                entries.push_back(currentEntry);
            }
            currentEntry = CacheEntry();
            inEntry = true;
            continue;
        }

        if (inEntry) {
            // Parse record name
            if (lineStr.find("Record Name") != std::string::npos) {
                size_t colonPos = lineStr.find(':');
                if (colonPos != std::string::npos) {
                    currentEntry.hostname = lineStr.substr(colonPos + 1);
                    // Trim whitespace
                    currentEntry.hostname.erase(0, currentEntry.hostname.find_first_not_of(" \t"));
                    currentEntry.hostname.erase(currentEntry.hostname.find_last_not_of(" \t") + 1);
                }
            }
            // Parse record type
            else if (lineStr.find("Record Type") != std::string::npos) {
                size_t colonPos = lineStr.find(':');
                if (colonPos != std::string::npos) {
                    currentEntry.recordType = lineStr.substr(colonPos + 1);
                    currentEntry.recordType.erase(0, currentEntry.recordType.find_first_not_of(" \t"));
                    currentEntry.recordType.erase(currentEntry.recordType.find_last_not_of(" \t") + 1);
                }
            }
            // Parse TTL
            else if (lineStr.find("Time To Live") != std::string::npos) {
                size_t colonPos = lineStr.find(':');
                if (colonPos != std::string::npos) {
                    std::string ttlStr = lineStr.substr(colonPos + 1);
                    ttlStr.erase(0, ttlStr.find_first_not_of(" \t"));
                    currentEntry.ttl = atoi(ttlStr.c_str());
                }
            }
            // Parse IP address (A record)
            else if (lineStr.find("A (Host) Record") != std::string::npos) {
                size_t colonPos = lineStr.find(':');
                if (colonPos != std::string::npos) {
                    currentEntry.ipAddress = lineStr.substr(colonPos + 1);
                    currentEntry.ipAddress.erase(0, currentEntry.ipAddress.find_first_not_of(" \t"));
                    currentEntry.ipAddress.erase(currentEntry.ipAddress.find_last_not_of(" \t") + 1);
                }
            }
        }
    }

    // Add the last entry
    if (inEntry && !currentEntry.hostname.empty()) {
        entries.push_back(currentEntry);
    }

    fclose(file);
    DeleteFileA(tempFile);

    // Filter for A records only and remove duplicates
    std::vector<CacheEntry> filteredEntries;
    std::map<std::string, bool> seenHosts;

    for (const auto& entry : entries) {
        if (entry.recordType == "1" || entry.recordType.find("A") != std::string::npos) {
            if (!entry.hostname.empty() && !entry.ipAddress.empty()) {
                if (seenHosts.find(entry.hostname) == seenHosts.end()) {
                    seenHosts[entry.hostname] = true;
                    CacheEntry newEntry = entry;
                    newEntry.lastResponseTime = 0;
                    newEntry.isReachable = false;
                    newEntry.isStale = (entry.ttl <= 0);
                    newEntry.lastTested = std::chrono::steady_clock::now() - std::chrono::minutes(10); // Force initial test
                    filteredEntries.push_back(newEntry);
                }
            }
        }
    }

    return filteredEntries;
}

// Test connectivity to a cached entry
DWORD TestCacheEntry(const std::string& hostname) {
    auto startTime = std::chrono::steady_clock::now();

    struct addrinfo hints = { 0 };
    struct addrinfo* result = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    // Set a timeout for the DNS lookup
    int status = getaddrinfo(hostname.c_str(), nullptr, &hints, &result);

    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    DWORD responseTime = static_cast<DWORD>(duration.count());

    if (result) {
        freeaddrinfo(result);
    }

    // Return timeout if it takes too long or fails
    if (status != 0 || responseTime > TEST_TIMEOUT_MS) {
        return MAXDWORD;
    }

    return responseTime;
}

// Update cache entry statuses
void UpdateCacheEntries() {
    if (g_cacheEntries.empty()) return;

    auto now = std::chrono::steady_clock::now();

    // Test more entries each cycle, but prioritize untested ones
    int testsPerCycle = min(10, (int)g_cacheEntries.size());
    static int testIndex = 0;

    // First, try to find untested entries
    std::vector<int> untestedIndices;
    for (int i = 0; i < g_cacheEntries.size(); i++) {
        auto timeSinceTest = std::chrono::duration_cast<std::chrono::seconds>(now - g_cacheEntries[i].lastTested);
        if (timeSinceTest.count() > 300) { // Haven't been tested in 5 minutes
            untestedIndices.push_back(i);
        }
    }

    // Test untested entries first
    int testsPerformed = 0;
    for (int idx : untestedIndices) {
        if (testsPerformed >= testsPerCycle) break;

        auto& entry = g_cacheEntries[idx];
        DWORD responseTime = TestCacheEntry(entry.hostname);

        entry.lastResponseTime = responseTime;
        entry.isReachable = (responseTime != MAXDWORD);
        entry.lastTested = now;
        testsPerformed++;
    }

    // Fill remaining test slots with regular rotation
    for (int i = testsPerformed; i < testsPerCycle; i++) {
        if (testIndex >= g_cacheEntries.size()) {
            testIndex = 0;
        }

        auto& entry = g_cacheEntries[testIndex];

        // Only test if it's been a while since last test
        auto timeSinceTest = std::chrono::duration_cast<std::chrono::seconds>(now - entry.lastTested);
        if (timeSinceTest.count() > 15) { // Test every 15 seconds
            DWORD responseTime = TestCacheEntry(entry.hostname);

            entry.lastResponseTime = responseTime;
            entry.isReachable = (responseTime != MAXDWORD);
            entry.lastTested = now;
        }

        testIndex++;
    }
}

// Calculate cache statistics
void CalculateStats() {
    g_stats.totalEntries = g_cacheEntries.size();
    g_stats.reachableEntries = 0;
    g_stats.staleEntries = 0;
    g_stats.timeoutEntries = 0;
    g_stats.slowEntries = 0;

    DWORD totalResponseTime = 0;
    int validResponses = 0;

    for (const auto& entry : g_cacheEntries) {
        if (entry.isStale) {
            g_stats.staleEntries++;
        }

        if (entry.isReachable) {
            g_stats.reachableEntries++;
            totalResponseTime += entry.lastResponseTime;
            validResponses++;

            if (entry.lastResponseTime > SLOW_RESPONSE_THRESHOLD) {
                g_stats.slowEntries++;
            }
        }
        else {
            g_stats.timeoutEntries++;
        }
    }

    g_stats.avgResponseTime = validResponses > 0 ? (double)totalResponseTime / validResponses : 0.0;
    g_stats.healthPercentage = g_stats.totalEntries > 0 ?
        ((double)g_stats.reachableEntries / g_stats.totalEntries) * 100.0 : 0.0;

    g_stats.pagesTotal = (g_stats.totalEntries + ENTRIES_PER_PAGE - 1) / ENTRIES_PER_PAGE;
    g_stats.currentPage = g_currentPage + 1;

    // Determine if cache needs flushing
    g_stats.needsFlush = (g_stats.healthPercentage < 60.0 && g_stats.totalEntries > 10) ||
        (g_stats.avgResponseTime > 300.0 && validResponses > 5) ||
        (g_stats.staleEntries > g_stats.totalEntries / 2);
}

// Get response time color
int GetResponseTimeColor(DWORD responseTime) {
    if (responseTime == MAXDWORD) return COLOR_RED;
    if (responseTime <= 50) return COLOR_GREEN;
    if (responseTime <= SLOW_RESPONSE_THRESHOLD) return COLOR_YELLOW;
    return COLOR_RED;
}

// Get status indicator
const char* GetStatusIndicator(bool isReachable, DWORD responseTime, bool isStale) {
    if (isStale) return "Stale";
    if (!isReachable) return "Missing";
    if (responseTime <= 50) return "Fast";
    if (responseTime <= SLOW_RESPONSE_THRESHOLD) return "Ok";
    return "Slow";
}

// Display current page of cache entries
void DisplayCacheEntries() {
    int startIndex = g_currentPage * ENTRIES_PER_PAGE;
    int endIndex = min(startIndex + ENTRIES_PER_PAGE, (int)g_cacheEntries.size());

    SetConsoleColor(COLOR_MAGENTA);
    printf("DNS CACHE ENTRIES (Page %d of %d):\n", g_stats.currentPage, g_stats.pagesTotal);
    SetConsoleColor(COLOR_GRAY);
    printf("----------------------------------------------------------------------------------------\n");
    SetConsoleColor(COLOR_WHITE);
    printf("Status  %-35s  %-15s  TTL    Response\n", "Hostname", "IP Address");
    SetConsoleColor(COLOR_GRAY);
    printf("----------------------------------------------------------------------------------------\n");

    for (int i = startIndex; i < endIndex; i++) {
        const auto& entry = g_cacheEntries[i];

        // Status indicator
        SetConsoleColor(GetResponseTimeColor(entry.lastResponseTime));
        printf("  %s    ", GetStatusIndicator(entry.isReachable, entry.lastResponseTime, entry.isStale));

        // Hostname (truncated if too long)
        SetConsoleColor(COLOR_WHITE);
        std::string hostname = entry.hostname;
        if (hostname.length() > 35) {
            hostname = hostname.substr(0, 32) + "...";
        }
        printf("%-35s  ", hostname.c_str());

        // IP Address
        SetConsoleColor(COLOR_CYAN);
        printf("%-15s  ", entry.ipAddress.c_str());

        // TTL
        SetConsoleColor(entry.isStale ? COLOR_RED : COLOR_WHITE);
        printf("%4d   ", entry.ttl);

        // Response time
        if (entry.isReachable && entry.lastResponseTime > 0) {
            SetConsoleColor(GetResponseTimeColor(entry.lastResponseTime));
            printf("%4lums", entry.lastResponseTime);
        }
        else if (entry.lastResponseTime == MAXDWORD) {
            SetConsoleColor(COLOR_RED);
            printf("TIMEOUT");
        }
        else {
            SetConsoleColor(COLOR_GRAY);
            printf("UNTESTED");
        }

        printf("\n");
    }

    if (g_cacheEntries.empty()) {
        SetConsoleColor(COLOR_YELLOW);
        printf("No DNS cache entries found. Cache may be empty.\n");
    }

    printf("\n");
}

// Display cache statistics and health assessment
void DisplayCacheHealth() {
    SetConsoleColor(COLOR_MAGENTA);
    printf("CACHE HEALTH ANALYSIS:\n");
    SetConsoleColor(COLOR_GRAY);
    printf("----------------------------------------------------------------------------------------\n");

    SetConsoleColor(COLOR_WHITE);
    printf("Total Entries: %d   Reachable: %d   Stale: %d   Timeouts: %d   Slow: %d\n",
        g_stats.totalEntries, g_stats.reachableEntries, g_stats.staleEntries,
        g_stats.timeoutEntries, g_stats.slowEntries);

    printf("Health: ");
    if (g_stats.healthPercentage >= 80.0) {
        SetConsoleColor(COLOR_GREEN);
        printf("%.1f%% EXCELLENT", g_stats.healthPercentage);
    }
    else if (g_stats.healthPercentage >= 60.0) {
        SetConsoleColor(COLOR_YELLOW);
        printf("%.1f%% GOOD", g_stats.healthPercentage);
    }
    else {
        SetConsoleColor(COLOR_RED);
        printf("%.1f%% POOR", g_stats.healthPercentage);
    }

    printf("   Avg Response: ");
    SetConsoleColor(GetResponseTimeColor((DWORD)g_stats.avgResponseTime));
    printf("%.1fms", g_stats.avgResponseTime);

    printf("\n");

    // Health recommendation
    SetConsoleColor(COLOR_WHITE);
    printf("Recommendation: ");
    if (g_stats.needsFlush) {
        SetConsoleColor(COLOR_RED);
        printf("FLUSH DNS CACHE - Poor performance detected");
    }
    else if (g_stats.healthPercentage < 80.0) {
        SetConsoleColor(COLOR_YELLOW);
        printf("MONITOR - Some entries may need attention");
    }
    else {
        SetConsoleColor(COLOR_GREEN);
        printf("HEALTHY - Cache performing well");
    }

    printf("\n\n");
}

// Display legend
void DisplayLegend() {
    SetConsoleColor(COLOR_MAGENTA);
    printf("LEGEND:\n");
    SetConsoleColor(COLOR_GRAY);
    printf("----------------------------------------------------------------------------------------\n");
    SetConsoleColor(COLOR_WHITE);
    printf("Status: ");
    SetConsoleColor(COLOR_GREEN);
    printf("Fast (<50ms)  ");
    SetConsoleColor(COLOR_YELLOW);
    printf("OK (<200ms)  ");
    SetConsoleColor(COLOR_RED);
    printf("Slow (>200ms)  Timeout  Stale");
    printf("\n\n");
}

// Display main interface
void DisplayInterface() {
    static int refreshCount = 0;

    if (refreshCount % 10 == 0) {
        system("cls");
    }
    refreshCount++;

    COORD coord = { 0, 0 };
    SetConsoleCursorPosition(g_consoleHandle, coord);

    // Title and time
    SYSTEMTIME st;
    GetLocalTime(&st);
    SetConsoleColor(COLOR_CYAN);
    printf("========================================================================================\n");
    printf("                     DNS CACHE HEALTH MONITOR - %02d:%02d:%02d                     \n",
        st.wHour, st.wMinute, st.wSecond);
    printf("========================================================================================\n");

    if (g_pauseMonitoring) {
        SetConsoleColor(COLOR_YELLOW);
        printf("                                    [PAUSED]                                    \n");
    }
    printf("\n");

    DisplayCacheHealth();
    DisplayCacheEntries();
    DisplayLegend();

    // Controls
    SetConsoleColor(COLOR_MAGENTA);
    printf("CONTROLS:\n");
    SetConsoleColor(COLOR_GRAY);
    printf("----------------------------------------------------------------------------------------\n");
    SetConsoleColor(COLOR_WHITE);
    printf("[F] Flush DNS Cache   [R] Refresh Cache List   [P] Pause/Resume   [Q] Quit\n");
    printf("[N] Next Page   [B] Previous Page   [V] View Full Cache   [C] Network Config\n");

    SetConsoleColor(COLOR_RESET);
    fflush(stdout);
}

// Process user input
void ProcessInput() {
    if (_kbhit()) {
        int key = _getch();

        switch (key) {
        case 'F':
        case 'f':
            system("ipconfig /flushdns");
            g_cacheEntries.clear();
            printf("\n\nDNS cache flushed! Refreshing cache list...\n");
            Sleep(1000);
            g_cacheEntries = ParseDNSCache();
            break;

        case 'R':
        case 'r':
            printf("\n\nRefreshing DNS cache list...\n");
            g_cacheEntries = ParseDNSCache();
            g_currentPage = 0;
            Sleep(1000);
            break;

        case 'V':
        case 'v':
            system("ipconfig /displaydns | more");
            break;

        case 'C':
        case 'c':
            system("ipconfig /all | more");
            break;

        case 'P':
        case 'p':
            g_pauseMonitoring = !g_pauseMonitoring;
            break;

        case 'N': 
		case 'n':
            if (g_currentPage < g_stats.pagesTotal - 1) {
                g_currentPage++;
            }
            break;

        case 'B':
		case 'b':
            if (g_currentPage > 0) {
                g_currentPage--;
            }
            break;

        case 'Q':
        case 'q':
        case 27: // Escape
            g_shouldExit = TRUE;
            SetEvent(g_exitEvent);
            break;
        }
    }
}

// Console control handler
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

// Initialize Winsock
BOOL InitializeWinsock() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    return (result == 0);
}

// Main monitoring loop
void MonitorDNS() {
    const int UPDATE_INTERVAL_MS = 5000; // 5 seconds
    auto lastUpdate = std::chrono::steady_clock::now();

    // Initial cache load
    g_cacheEntries = ParseDNSCache();

    while (!g_shouldExit) {
        auto now = std::chrono::steady_clock::now();
        auto timeSinceUpdate = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdate);

        if (!g_pauseMonitoring && timeSinceUpdate.count() >= UPDATE_INTERVAL_MS) {
            UpdateCacheEntries();
            lastUpdate = now;
        }

        CalculateStats();
        DisplayInterface();
        ProcessInput();

        Sleep(200);
    }
}

// Main function
int main() {
    g_consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);

    SetConsoleSize();
    SetConsoleTitleA("DNS Cache Health Monitor");

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
    MonitorDNS();

    CloseHandle(g_exitEvent);
    WSACleanup();
    return 0;
}