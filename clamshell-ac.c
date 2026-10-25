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

static float saved(void) {
    float b = -1; FILE *f = fopen(saved_path, "r");
    if (f) { if (fscanf(f, "%f", &b) != 1) b = -1; fclose(f); }
    return b;
}

static void fade_in(float to) {
    CGDirectDisplayID d = CGMainDisplayID();
    usleep(400000);                                   // a beat of dark after the lid opens
    for (int i = 1; i <= 40; i++) {                   // 0.8s ease-out to the old brightness
        float t = i / 40.0f;
        DisplayServicesSetBrightness(d, to * (1 - powf(1 - t, 3)));
        usleep(20000);
    }
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
        DisplayServicesSetBrightness(d, 0);           // wakes dark when the lid opens
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
