# clamshell-ac

Keep a MacBook running with the lid closed while it's on the charger, with no external display. On battery, lid close sleeps as usual.

A small root LaunchDaemon (C, event-driven: IOKit lid and power-source notifications) runs `pmset -a disablesleep 1` on AC power and `0` on battery, and turns the screen off the instant the lid shuts; on open it comes straight up at 10% brightness, holds a beat, climbs to 80% and then quickly to full (1.6s). If you unplug while the lid is already closed, it sleeps the Mac immediately.

```
sudo ./install.sh            # install and start
sudo ./install.sh uninstall  # remove; lid close sleeps again
log show --predicate 'senderImagePath CONTAINS "logger"' --last 1h | grep clamshell-ac
```

With the lid closed the Mac cools less well. Light work is fine; long heavy loads will throttle.
