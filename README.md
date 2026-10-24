# clamshell-ac

Keep a MacBook running with the lid closed while it's on the charger, with no external display. On battery, lid close sleeps as usual.

A root LaunchDaemon follows `pmset -g pslog` and runs `pmset -a disablesleep 1` on AC power and `0` on battery. If you unplug while the lid is already closed, it sleeps the Mac immediately.

```
sudo ./install.sh            # install and start
sudo ./install.sh uninstall  # remove; lid close sleeps again
log show --predicate 'senderImagePath CONTAINS "logger"' --last 1h | grep clamshell-ac
```

With the lid closed the Mac cools less well. Light work is fine; long heavy loads will throttle.
