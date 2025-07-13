#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <iostream>
#include <chrono>
#include <vector>

// Link with Winsock library
#pragma comment(lib, "ws2_32.lib")

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
};

// Global variables for monitoring
static BOOL g_shouldExit = FALSE;
static HANDLE g_exitEvent = NULL;
static DNSStats g_stats = { 0 };

// Test hostnames for DNS monitoring
const char* TEST_HOSTNAMES[] = {
    "www.google.com",
    "www.microsoft.com",
    "www.github.com",
    "www.stackoverflow.com",
    "www.cloudflare.com"
};
const int NUM_TEST_HOSTS = sizeof(TEST_HOSTNAMES) / sizeof(TEST_HOSTNAMES[0]);

// Console control handler for graceful shutdown
BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType) {
    switch (dwCtrlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
        printf("\nShutting down DNS monitor...\n");
        g_shouldExit = TRUE;
        if (g_exitEvent) {
            SetEvent(g_exitEvent);
        }
        return TRUE;
    }
    return FALSE;
}

// Display current time
void PrintCurrentTime() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    printf("[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
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

// Analyze DNS performance and update statistics
void AnalyzeDNSPerformance() {
    std::vector<DWORD> responseTimes;
    DWORD totalResponseTime = 0;
    DWORD validQueries = 0;

    printf("Testing DNS resolution performance...\n");

    // Test each hostname multiple times for better statistics
    for (int round = 0; round < 2; round++) {
        for (int i = 0; i < NUM_TEST_HOSTS; i++) {
            DWORD responseTime = PerformDNSLookup(TEST_HOSTNAMES[i]);

            if (responseTime != MAXDWORD) {
                responseTimes.push_back(responseTime);
                totalResponseTime += responseTime;
                validQueries++;

                g_stats.totalQueries++;

                // Classify responses (heuristic for cache hits/misses)
                if (responseTime < 10) {
                    g_stats.cacheHits++;
                }
                else if (responseTime < 50) {
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

                Sleep(100);
            }
        }
    }

    // Calculate statistics
    if (validQueries > 0) {
        g_stats.avgResponseTime = (double)totalResponseTime / validQueries;
        g_stats.hitRatio = g_stats.totalQueries > 0 ? ((double)g_stats.cacheHits / g_stats.totalQueries) * 100.0 : 0.0;
        g_stats.dataValid = TRUE;
    }
}

// Display detailed DNS statistics
void DisplayDNSStats() {
    PrintCurrentTime();

    if (!g_stats.dataValid) {
        printf("No valid DNS data available\n");
        return;
    }

    printf("DNS Performance Analysis:\n");
    printf("  Total Queries: %lu\n", g_stats.totalQueries);
    printf("  Est. Cache Hits: %lu (%.1f%%)\n", g_stats.cacheHits, g_stats.hitRatio);
    printf("  Est. Cache Misses: %lu\n", g_stats.cacheMisses);
    printf("  Avg Response Time: %.1f ms\n", g_stats.avgResponseTime);
    printf("  Slow Queries (>100ms): %lu\n", g_stats.slowQueries);
    printf("  Very Slow Queries (>500ms): %lu\n", g_stats.verySlowQueries);

    // Performance analysis
    if (g_stats.hitRatio < 40.0 && g_stats.totalQueries > 10) {
        printf("  *** WARNING: Low estimated cache hit ratio! ***\n");
        printf("  Recommendation: Consider flushing DNS cache (ipconfig /flushdns)\n");
    }
    else if (g_stats.avgResponseTime > 200.0) {
        printf("  *** WARNING: High average response time! ***\n");
        printf("  Recommendation: Check network connection or DNS server\n");
    }
    else if (g_stats.verySlowQueries > g_stats.totalQueries / 4) {
        printf("  *** WARNING: Many very slow queries detected! ***\n");
        printf("  Recommendation: Flush DNS cache or check DNS servers\n");
    }
    else {
        printf("  DNS performance appears normal\n");
    }
}

// Check DNS cache status by examining response patterns
void CheckDNSCacheStatus() {
    printf("Checking DNS cache effectiveness...\n");

    const char* testHost = "www.google.com";
    std::vector<DWORD> times;

    for (int i = 0; i < 5; i++) {
        DWORD responseTime = PerformDNSLookup(testHost);
        if (responseTime != MAXDWORD) {
            times.push_back(responseTime);
            printf("  Query %d: %lu ms\n", i + 1, responseTime);
        }
        Sleep(50);
    }

    if (times.size() >= 3) {
        DWORD firstTime = times[0];
        DWORD avgSubsequent = 0;
        for (size_t i = 1; i < times.size(); i++) {
            avgSubsequent += times[i];
        }
        avgSubsequent /= (DWORD)(times.size() - 1);

        if (firstTime > avgSubsequent * 2 && avgSubsequent < 20) {
            printf("  Cache appears to be working well (first: %lu ms, avg subsequent: %lu ms)\n",
                firstTime, avgSubsequent);
        }
        else if (avgSubsequent > 100) {
            printf("  Cache may not be effective - consider flushing\n");
        }
    }
    printf("\n");
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

// Show basic network recommendations
void DisplayDNSConfig() {
    printf("DNS Cache Monitoring\n");
    printf("==================\n");
    printf("This tool measures DNS resolution times to estimate cache performance.\n");
    printf("No admin privileges required.\n\n");

    printf("System DNS Commands:\n");
    printf("  View DNS cache: ipconfig /displaydns\n");
    printf("  Flush DNS cache: ipconfig /flushdns\n");
    printf("  View network config: ipconfig /all\n");
    printf("  Test DNS resolution: nslookup <hostname>\n");
    printf("\n");
}

// Main monitoring loop
void MonitorDNS() {
    const int MONITOR_INTERVAL_MS = 30000; // 30 seconds
    int cycleCount = 0;

    printf("DNS Cache Performance Monitor\n");
    printf("============================\n");
    printf("Monitoring interval: %d seconds\n", MONITOR_INTERVAL_MS / 1000);
    printf("Press Ctrl+C to exit\n\n");

    DisplayDNSConfig();

    while (!g_shouldExit) {
        printf("=== Monitoring Cycle %d ===\n", ++cycleCount);

        AnalyzeDNSPerformance();
        DisplayDNSStats();

        if (cycleCount % 3 == 0) {
            printf("\n");
            CheckDNSCacheStatus();
        }

        printf("Next check in %d seconds...\n\n", MONITOR_INTERVAL_MS / 1000);

        DWORD waitResult = WaitForSingleObject(g_exitEvent, MONITOR_INTERVAL_MS);
        if (waitResult == WAIT_OBJECT_0) {
            break;
        }
    }

    printf("\nDNS Monitor stopped.\n");
}

// Main function
int main() {
    printf("DNS Cache Performance Monitor\n");
    printf("============================\n");
    printf("Measuring DNS response times to estimate cache effectiveness.\n\n");

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

    MonitorDNS();

    CloseHandle(g_exitEvent);
    CleanupWinsock();
    return 0;
}