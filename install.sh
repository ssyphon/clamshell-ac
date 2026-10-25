#!/bin/sh
# sudo ./install.sh            install and start
# sudo ./install.sh uninstall  stop, remove, and restore normal lid sleep
set -e
cd "$(dirname "$0")"
P=/Library/LaunchDaemons/com.ssyphon.clamshell-ac.plist
A=/Library/LaunchAgents/com.ssyphon.clamshell-ac.lid.plist
U=gui/${SUDO_UID:?run with sudo}
launchctl bootout system "$P" 2>/dev/null || true
launchctl bootout "$U" "$A" 2>/dev/null || true
if [ "$1" = uninstall ]; then
  rm -f "$P" "$A" /usr/local/libexec/clamshell-ac
  pmset -a disablesleep 0
  echo "removed; lid close sleeps again"; exit
fi
# root runs these, so root owns them (a user-writable binary here would be a root backdoor)
install -d -o root -g wheel /usr/local/libexec
cc -O2 -o /tmp/clamshell-ac clamshell-ac.c -framework IOKit -framework CoreFoundation -framework CoreGraphics \
  -F/System/Library/PrivateFrameworks -framework DisplayServices
install -o root -g wheel -m 755 /tmp/clamshell-ac /usr/local/libexec/clamshell-ac && rm /tmp/clamshell-ac
install -o root -g wheel -m 644 com.ssyphon.clamshell-ac.plist "$P"
install -o root -g wheel -m 644 com.ssyphon.clamshell-ac.lid.plist "$A"
launchctl bootstrap system "$P"
launchctl bootstrap "$U" "$A"
sleep 1; pmset -g | grep -i sleepdisabled || pmset -g | head -3
