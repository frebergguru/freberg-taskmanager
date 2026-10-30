#!/bin/sh
# Optionally lets the current user read CPU power counters and memory module details.
# The app never runs this by itself. See Docs/permissions.md for what each step means.
set -eu

ME=$(id -un)
cat <<EOF
This will, using sudo:
  - create the groups powercap-readers and dmi-readers, and add $ME to both
  - add /etc/udev/rules.d/90-powercap-readers.rules (CPU power counters)
  - add /etc/tmpfiles.d/dmi-readers.conf (memory module details)

Note: members of dmi-readers can also read the machine's serial numbers and UUID.
Docs/permissions.md explains how to undo it.

EOF
printf 'Continue? [y/N] '
read -r ANSWER
case "$ANSWER" in
    y | Y) ;;
    *) echo "Nothing was changed."; exit 0 ;;
esac

sudo groupadd --force --system powercap-readers
sudo groupadd --force --system dmi-readers
sudo usermod -aG powercap-readers,dmi-readers "$ME"

sudo tee /etc/udev/rules.d/90-powercap-readers.rules >/dev/null <<'RULE'
SUBSYSTEM=="powercap", ACTION=="add", RUN+="/bin/chgrp powercap-readers /sys%p/energy_uj", RUN+="/bin/chmod g+r /sys%p/energy_uj"
RULE
sudo udevadm control --reload
sudo udevadm trigger --subsystem-match=powercap

echo 'z /sys/firmware/dmi/tables/DMI 0440 root dmi-readers -' | sudo tee /etc/tmpfiles.d/dmi-readers.conf >/dev/null
sudo systemd-tmpfiles --create /etc/tmpfiles.d/dmi-readers.conf

echo "Done. Log out and back in for the new groups to apply."
