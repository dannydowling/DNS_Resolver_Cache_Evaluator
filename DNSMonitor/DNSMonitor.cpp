#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <iostream>
#include <chrono>
#include <vector>
#include <conio.h>
#include <string>

// Link with Winsock library
#pragma comment(lib, "ws2_32.lib")

// Console colors
#define COLOR_RESET     7
#define COLOR_GREEN     10
#define COLOR_YELLOW    14
#define COLOR_RED       12
#define COLOR_CYAN      11
#define COLOR_MAGENTA   13
#define COLOR_WHITE     15
#define COLOR_GRAY      8

// Structure to hold DNS monitoring data
struct DNSStats {
    DWORD totalQueries;
    DWORD cacheHits;
    DWORD cacheMisses;
    DWORD slowQueries;
    DWORD verySlowQueries;
    double avgResponseTime;
    double hitRatio;
    BOOL dataValid;
    std::chrono::steady_clock::time_point lastUpdate;
};

// Structure for individual host status
struct HostStatus {
    std::string hostname;
    DWORD lastResponseTime;
    bool isReachable;
    std::chrono::steady_clock::time_point lastChecked;
};

// Global variables
static BOOL g_shouldExit = FALSE;
static HANDLE g_exitEvent = NULL;
static DNSStats g_stats = { 0 };
static std::vector<HostStatus> g_hostStatuses;
static HANDLE g_consoleHandle = NULL;
static bool g_pauseMonitoring = false;

// Test hostnames for DNS monitoring
const char* TEST_HOSTNAMES[] = {
    "www.google.com",
    "www.microsoft.com",
    "www.github.com",
    "www.stackoverflow.com",
    "www.cloudflare.com"
};
const int NUM_TEST_HOSTS = sizeof(TEST_HOSTNAMES) / sizeof(TEST_HOSTNAMES[0]);

// Console utilities
void SetConsoleColor(int color) {
    SetConsoleTextAttribute(g_consoleHandle, color);
}

void GotoXY(int x, int y) {
    COORD coord;
    coord.X = x;
    coord.Y = y;
    SetConsoleCursorPosition(g_consoleHandle, coord);
}

void ClearScreen() {
    system("cls");
}

void HideCursor() {
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(g_consoleHandle, &cursorInfo);
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(g_consoleHandle, &cursorInfo);
}

// Get response time color based on performance
int GetResponseTimeColor(DWORD responseTime) {
    if (responseTime == MAXDWORD) return COLOR_RED;
    if (responseTime <= 20) return COLOR_GREEN;
    if (responseTime <= 100) return COLOR_YELLOW;
    return COLOR_RED;
}

// Get status indicator
const char* GetStatusIndicator(bool isReachable, DWORD responseTime) {
    if (!isReachable) return "✗";
    if (responseTime <= 20) return "●";
    if (responseTime <= 100) return "◐";
    return "◯";
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

// Perform DNS lookup and measure response time
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
        g_hostStatuses[i].lastChecked = std::chrono::steady_clock::now();

        if (responseTime != MAXDWORD) {
            g_stats.totalQueries++;

            // Classify responses (heuristic for cache hits/misses)
            if (responseTime < 50) {
                g_stats.cacheHits++;
            }
            else {
                g_stats.cacheMisses++;
            }

            if (responseTime > 100) {
                g_stats.slowQueries++;
            }
            if (responseTime > 500) {
                g_stats.verySlowQueries++;
            }
        }
    }

    // Calculate statistics
    if (g_stats.totalQueries > 0) {
        g_stats.hitRatio = ((double)g_stats.cacheHits / g_stats.totalQueries) * 100.0;

        // Calculate average response time from current readings
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
        g_stats.lastUpdate = std::chrono::steady_clock::now();
    }
}

// Display the main interface
void DisplayInterface() {
    GotoXY(0, 0);

    // Header
    SetConsoleColor(COLOR_CYAN);
    printf("╔═══════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                          DNS CACHE STATUS MONITOR                            ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════════════════╝\n");

    // Current time
    SYSTEMTIME st;
    GetLocalTime(&st);
    SetConsoleColor(COLOR_WHITE);
    printf("Last Update: %02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);

    // Pause indicator
    if (g_pauseMonitoring) {
        SetConsoleColor(COLOR_YELLOW);
        printf("  [PAUSED]");
    }
    printf("\n\n");

    // Host Status Section
    SetConsoleColor(COLOR_MAGENTA);
    printf("HOST STATUS:\n");
    SetConsoleColor(COLOR_GRAY);
    printf("─────────────────────────────────────────────────────────────────────────────────\n");

    for (const auto& host : g_hostStatuses) {
        SetConsoleColor(GetResponseTimeColor(host.lastResponseTime));
        printf("%s ", GetStatusIndicator(host.isReachable, host.lastResponseTime));

        SetConsoleColor(COLOR_WHITE);
        printf("%-25s", host.hostname.c_str());

        if (host.isReachable) {
            SetConsoleColor(GetResponseTimeColor(host.lastResponseTime));
            printf("%4lu ms", host.lastResponseTime);
        }
        else {
            SetConsoleColor(COLOR_RED);
            printf("TIMEOUT");
        }
        printf("\n");
    }

    // Statistics Section
    printf("\n");
    SetConsoleColor(COLOR_MAGENTA);
    printf("CACHE PERFORMANCE:\n");
    SetConsoleColor(COLOR_GRAY);
    printf("─────────────────────────────────────────────────────────────────────────────────\n");

    if (g_stats.dataValid) {
        SetConsoleColor(COLOR_WHITE);
        printf("Total Queries: ");
        SetConsoleColor(COLOR_CYAN);
        printf("%lu\n", g_stats.totalQueries);

        SetConsoleColor(COLOR_WHITE);
        printf("Cache Hit Ratio: ");
        int hitColor = (g_stats.hitRatio >= 60) ? COLOR_GREEN : (g_stats.hitRatio >= 40) ? COLOR_YELLOW : COLOR_RED;
        SetConsoleColor(hitColor);
        printf("%.1f%%\n", g_stats.hitRatio);

        SetConsoleColor(COLOR_WHITE);
        printf("Avg Response: ");
        SetConsoleColor(GetResponseTimeColor((DWORD)g_stats.avgResponseTime));
        printf("%.1f ms\n", g_stats.avgResponseTime);

        SetConsoleColor(COLOR_WHITE);
        printf("Slow Queries: ");
        SetConsoleColor(g_stats.slowQueries > 0 ? COLOR_YELLOW : COLOR_GREEN);
        printf("%lu\n", g_stats.slowQueries);

        // Status assessment
        printf("\n");
        SetConsoleColor(COLOR_WHITE);
        printf("Status: ");
        if (g_stats.hitRatio < 40.0 && g_stats.totalQueries > 10) {
            SetConsoleColor(COLOR_RED);
            printf("POOR - Consider flushing DNS cache");
        }
        else if (g_stats.avgResponseTime > 200.0) {
            SetConsoleColor(COLOR_YELLOW);
            printf("SLOW - Check network/DNS servers");
        }
        else {
            SetConsoleColor(COLOR_GREEN);
            printf("GOOD - DNS performance normal");
        }
        printf("\n");
    }
    else {
        SetConsoleColor(COLOR_YELLOW);
        printf("Gathering initial data...\n");
    }

    // Controls Section
    printf("\n");
    SetConsoleColor(COLOR_MAGENTA);
    printf("CONTROLS:\n");
    SetConsoleColor(COLOR_GRAY);
    printf("─────────────────────────────────────────────────────────────────────────────────\n");
    SetConsoleColor(COLOR_WHITE);
    printf("[F] Flush DNS Cache    [V] View DNS Cache    [R] Reset Stats    [P] Pause/Resume\n");
    printf("[Q] Quit               [C] Network Config    [T] Test Single Host\n");

    SetConsoleColor(COLOR_RESET);
}

// Execute system commands
void ExecuteCommand(const std::string& command, const std::string& description) {
    SetConsoleColor(COLOR_YELLOW);
    printf("\nExecuting: %s\n", description.c_str());
    SetConsoleColor(COLOR_GRAY);
    printf("─────────────────────────────────────────────────────────────────────────────────\n");
    SetConsoleColor(COLOR_WHITE);

    system(command.c_str());

    SetConsoleColor(COLOR_YELLOW);
    printf("\nPress any key to return to monitor...");
    _getch();
}

// Handle user input
void ProcessInput() {
    if (_kbhit()) {
        int key = _getch();
        key = toupper(key);

        switch (key) {
        case 'F':
            ExecuteCommand("ipconfig /flushdns", "Flushing DNS Cache");
            // Reset stats after flush
            g_stats = { 0 };
            break;

        case 'V':
            ExecuteCommand("ipconfig /displaydns | more", "Viewing DNS Cache");
            break;

        case 'C':
            ExecuteCommand("ipconfig /all | more", "Network Configuration");
            break;

        case 'R':
            g_stats = { 0 };
            SetConsoleColor(COLOR_GREEN);
            printf("\nStatistics reset!\n");
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
                if (responseTime != MAXDWORD) {
                    SetConsoleColor(COLOR_GREEN);
                    printf("Response time: %lu ms\n", responseTime);
                }
                else {
                    SetConsoleColor(COLOR_RED);
                    printf("Failed to resolve hostname\n");
                }
                printf("Press any key to continue...");
                _getch();
            }
        }
        break;

        case 'Q':
        case 27: // ESC
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
    if (result != 0) {
        printf("WSAStartup failed: %d\n", result);
        return FALSE;
    }
    return TRUE;
}

// Cleanup Winsock
void CleanupWinsock() {
    WSACleanup();
}

// Initialize host status array
void InitializeHostStatuses() {
    g_hostStatuses.clear();
    for (int i = 0; i < NUM_TEST_HOSTS; i++) {
        HostStatus status;
        status.hostname = TEST_HOSTNAMES[i];
        status.lastResponseTime = 0;
        status.isReachable = false;
        status.lastChecked = std::chrono::steady_clock::now();
        g_hostStatuses.push_back(status);
    }
}

// Main monitoring loop
void MonitorDNS() {
    const int UPDATE_INTERVAL_MS = 5000; // 5 seconds
    auto lastUpdate = std::chrono::steady_clock::now();

    HideCursor();

    while (!g_shouldExit) {
        auto now = std::chrono::steady_clock::now();
        auto timeSinceUpdate = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdate);

        // Update DNS stats periodically
        if (!g_pauseMonitoring && timeSinceUpdate.count() >= UPDATE_INTERVAL_MS) {
            UpdateHostStatuses();
            lastUpdate = now;
        }

        // Always refresh display and process input
        ClearScreen();
        DisplayInterface();
        ProcessInput();

        Sleep(250); // Refresh UI every 250ms for responsiveness
    }
}

// Main function
int main() {
    g_consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);

    // Set console title and size
    SetConsoleTitle(L"DNS Cache Status Monitor");

    // Initialize Winsock
    if (!InitializeWinsock()) {
        printf("Failed to initialize Winsock\n");
        return 1;
    }

    g_exitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_exitEvent) {
        printf("Failed to create exit event\n");
        CleanupWinsock();
        return 1;
    }

    if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE)) {
        printf("Failed to set console control handler\n");
        CloseHandle(g_exitEvent);
        CleanupWinsock();
        return 1;
    }

    InitializeHostStatuses();
    MonitorDNS();

    CloseHandle(g_exitEvent);
    CleanupWinsock();
    return 0;
}