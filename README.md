# clamshell-ac

Keep a MacBook running with the lid closed while it's on the charger, with no external display. On battery, lid close sleeps as usual.

A small root LaunchDaemon (C, event-driven: IOKit lid and power-source notifications) runs `pmset -a disablesleep 1` on AC power and `0` on battery, and turns the screen off the instant the lid shuts; on open it comes straight up at 30% brightness, holds to 0.35s, climbs to 80% by 0.85s and to full by 1.05s. If you unplug while the lid is already closed, it sleeps the Mac immediately.

```
sudo ./install.sh            # install and start
sudo ./install.sh uninstall  # remove; lid close sleeps again
log show --predicate 'senderImagePath CONTAINS "logger"' --last 1h | grep clamshell-ac
```

With the lid closed the Mac cools less well. Light work is fine; long heavy loads will throttle.
