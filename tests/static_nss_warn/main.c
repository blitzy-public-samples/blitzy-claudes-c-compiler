// Test: -static NSS function warning
//
// When compiled with -static, the CCC linker should emit a warning
// that NSS-dependent functions (getaddrinfo, getpwnam, gethostbyname)
// require shared libraries at runtime due to glibc's NSS dlopen() usage.
//
// glibc's Name Service Switch (NSS) module uses dlopen() internally to load
// service provider libraries (e.g., libnss_dns.so, libnss_files.so). When a
// program is statically linked, dlopen() is either unavailable or limited,
// which can cause runtime failures in hostname resolution, user/group lookups,
// and similar operations — even though the program compiles and links without
// error.
//
// Compile: ccc -static -o test main.c
// Expected: Linker emits NSS incompatibility warning on stderr
//           Program still compiles, links, and runs (exit code 0)

// Forward-declare NSS-dependent functions without including system headers.
// This keeps the test minimal, portable, and avoids header dependency issues
// across architectures. These use glibc's NSS (Name Service Switch) which
// internally calls dlopen(), making them problematic for static linking.

// DNS/hostname resolution — primary NSS consumer
struct addrinfo;
int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res);
void freeaddrinfo(struct addrinfo *res);

// Password database lookup by name
struct passwd;
struct passwd *getpwnam(const char *name);

// Legacy hostname resolution
struct hostent;
struct hostent *gethostbyname(const char *name);

int main(void) {
    // Take addresses of NSS-dependent functions to ensure the linker
    // references them as required symbols. Use volatile to prevent the
    // optimizer from eliminating these references at any optimization level.
    //
    // We deliberately do NOT call these functions at runtime — in a statically
    // linked binary, NSS-backed functions may fail or behave incorrectly due
    // to the absence of dynamically loaded NSS service modules. We only need
    // the linker to see the symbol references so it can emit the NSS warning.
    volatile void *p1 = (void *)(long)getaddrinfo;
    volatile void *p2 = (void *)(long)getpwnam;
    volatile void *p3 = (void *)(long)gethostbyname;

    // Suppress unused-variable warnings
    (void)p1;
    (void)p2;
    (void)p3;

    return 0;
}
