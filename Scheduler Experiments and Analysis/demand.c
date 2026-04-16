#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define MB (1024 * 1024)
static void show_rss(const char *label) {
    char buf[256];
    snprintf(buf, sizeof(buf),"grep -E 'VmSize|VmRSS|VmSwap' /proc/%d/status", getpid());
    printf("\n[%s]\n", label);
    fflush(stdout);
    system(buf);
}
int main(void) {
    show_rss("1. before malloc");
    // Reserve 256 MB of VIRTUAL memory
    char *big = malloc(256 * MB);
    if (!big) { perror("malloc"); return 1; }
    show_rss("2. after malloc, before touch - virtual only!");
    // Touch first 64 MB → 64 MB / 4 KB = 16384 page faults
    memset(big, 'A', 64 * MB);
    show_rss("3. after touching 64 MB");
    // Touch all 256 MB → remaining 192 MB causes more faults
    memset(big, 'B', 256 * MB);
    show_rss("4. after touching all 256 MB");
    free(big);
    return 0;
}