// Lid closed on AC: keep running, screen off. On battery: sleep as normal. Event-driven.
//   clamshell-ac        root LaunchDaemon: power source -> pmset disablesleep (root only)
//   clamshell-ac lid    user LaunchAgent: lid shut -> screen dark at once; lid open -> wake dark, then fade in
// Brightness lives in the user's GUI session, so the lid half runs there.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#include <spawn.h>
#include <syslog.h>
#include <sys/wait.h>
#include <CoreGraphics/CoreGraphics.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <time.h>

#define kIOPMMessageClamshellStateChange iokit_family_msg(sub_iokit_powermanagement, 0x100)
extern char **environ;
extern int DisplayServicesGetBrightness(CGDirectDisplayID, float *);
extern int DisplayServicesSetBrightness(CGDirectDisplayID, float);
static char saved_path[1024];   // brightness to fade back to; present only while the lid is shut by us
static io_service_t root;
static int cur = -1;

static void run(const char *a, const char *b, const char *c) {
    const char *argv[] = {"/usr/bin/pmset", a, b, c, NULL};
    pid_t p;
    if (posix_spawn(&p, argv[0], NULL, NULL, (char **)argv, environ) == 0) waitpid(p, NULL, 0);
}

static int on_ac(void) {
    CFTypeRef info = IOPSCopyPowerSourcesInfo();
    CFStringRef t = IOPSGetProvidingPowerSourceType(info);
    int ac = t && CFEqual(t, CFSTR(kIOPMACPowerKey));
    if (info) CFRelease(info);
    return ac;
}

static int lid_shut(void) {
    CFTypeRef v = IORegistryEntryCreateCFProperty(root, CFSTR("AppleClamshellState"), NULL, 0);
    int shut = v == kCFBooleanTrue;
    if (v) CFRelease(v);
    return shut;
}

static void power_changed(void *ctx) {
    int ac = on_ac();
    if (ac == cur) return;
    cur = ac;
    run("-a", "disablesleep", ac ? "1" : "0");
    syslog(LOG_NOTICE, "disablesleep %d", ac);
    // unplugged with the lid already shut: sleep now rather than run hot in a bag
    if (!ac && lid_shut()) { syslog(LOG_NOTICE, "lid shut on battery, sleeping"); run("sleepnow", NULL, NULL); }
}

static float ramp(double t);

static float saved(void) {
    float b = -1; FILE *f = fopen(saved_path, "r");
    if (f) { if (fscanf(f, "%f", &b) != 1) b = -1; fclose(f); }
    return b;
}

static double now(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9;
}

// Lid open, 1.05s in all, timed by the clock: straight to 30% of the old brightness (lower shows the panel's
// unevenness while the lid flexes) and hold to 0.35s, climb to 80% by
// 0.85s, then to 100% by 1.05s. Never below what macOS woke the panel at, so it never dips.
static float ramp(double t) {
    if (t < 0.35) return 0.30f;
    if (t < 0.85) return 0.30f + 0.50f * (t - 0.35) / 0.5;
    if (t < 1.05) return 0.80f + 0.20f * (t - 0.85) / 0.2;
    return 1;
}

static void fade_in(float to) {
    CGDirectDisplayID d = CGMainDisplayID();
    float woke = 0;
    DisplayServicesGetBrightness(d, &woke);
    double start = now();
    for (double t = 0; t < 1.05; t = now() - start) {
        float b = to * ramp(t);
        DisplayServicesSetBrightness(d, b > woke ? b : woke);
        usleep(10000);
    }
    DisplayServicesSetBrightness(d, to);
    unlink(saved_path);
}

static void lid_changed(void *ctx, io_service_t s, natural_t msg, void *arg) {
    if (msg != kIOPMMessageClamshellStateChange) return;
    CGDirectDisplayID d = CGMainDisplayID();
    if (lid_shut()) {
        if (!on_ac()) return;                         // battery: macOS sleeps the Mac itself
        float b;
        if (saved() < 0 && DisplayServicesGetBrightness(d, &b) == 0) {
            FILE *f = fopen(saved_path, "w"); if (f) { fprintf(f, "%f\n", b); fclose(f); }
        }
        // Park it at the ramp's first level: the panel is off, and macOS wakes it there and re-applies its stored
        // brightness ~0.2s and ~0.4s later; parked at 0 that pulled the screen down mid-ramp (a dip, read as flicker).
        DisplayServicesSetBrightness(d, saved() * ramp(0));
        run("displaysleepnow", NULL, NULL);
    } else {
        float b = saved();
        if (b >= 0) fade_in(b);
    }
}

int main(int argc, char **argv) {
    int lid = argc > 1 && !strcmp(argv[1], "lid");
    openlog("clamshell-ac", 0, LOG_DAEMON);
    root = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("IOPMrootDomain"));
    if (lid) {
        snprintf(saved_path, sizeof saved_path, "%s/Library/Caches/clamshell-ac.brightness", getenv("HOME"));
        if (!lid_shut() && saved() >= 0) fade_in(saved());   // restarted mid-cycle: never leave the screen at 0
        IONotificationPortRef port = IONotificationPortCreate(kIOMainPortDefault);
        io_object_t note;
        IOServiceAddInterestNotification(port, root, kIOGeneralInterest, lid_changed, NULL, &note);
        CFRunLoopAddSource(CFRunLoopGetCurrent(), IONotificationPortGetRunLoopSource(port), kCFRunLoopDefaultMode);
    } else {
        CFRunLoopAddSource(CFRunLoopGetCurrent(), IOPSNotificationCreateRunLoopSource(power_changed, NULL), kCFRunLoopDefaultMode);
        power_changed(NULL);
    }
    CFRunLoopRun();
}
