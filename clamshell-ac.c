// Lid closed on AC: keep running, screen off. On battery: sleep as normal.
// Runs as root from /Library/LaunchDaemons/com.ssyphon.clamshell-ac.plist. Event-driven: reacts the instant
// the lid or power source changes, so the panel goes dark as the lid shuts, like stock clamshell sleep.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#include <spawn.h>
#include <syslog.h>
#include <sys/wait.h>

#define kIOPMMessageClamshellStateChange iokit_family_msg(sub_iokit_powermanagement, 0x100)
extern char **environ;
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

static void lid_changed(void *ctx, io_service_t s, natural_t msg, void *arg) {
    if (msg != kIOPMMessageClamshellStateChange) return;
    int shut = lid_shut();
    // sleep is disabled on AC, so macOS leaves the panel lit behind the lid; darken it as the lid shuts
    if (shut && on_ac()) { run("displaysleepnow", NULL, NULL); syslog(LOG_NOTICE, "lid shut, screen off"); }
}

int main(void) {
    openlog("clamshell-ac", 0, LOG_DAEMON);
    root = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("IOPMrootDomain"));
    IONotificationPortRef port = IONotificationPortCreate(kIOMainPortDefault);
    io_object_t note;
    IOServiceAddInterestNotification(port, root, kIOGeneralInterest, lid_changed, NULL, &note);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), IONotificationPortGetRunLoopSource(port), kCFRunLoopDefaultMode);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), IOPSNotificationCreateRunLoopSource(power_changed, NULL), kCFRunLoopDefaultMode);
    power_changed(NULL);
    CFRunLoopRun();
}
