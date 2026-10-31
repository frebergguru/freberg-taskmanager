# Sensors that need permission

Two kinds of reading are often closed to normal users. The app runs fine without them and shows "Access denied" in those rows. It never asks for root by itself and installs nothing by itself. Both grants below are optional.

`tools/grant-sensor-access.sh` does these steps for you, after showing what it will do. Log out and back in afterwards.

## CPU power (`/sys/class/powercap/*/energy_uj`)

Since kernel 5.10 this is root-only on most systems, as a safeguard against a side-channel attack that infers secrets from power use. A group of trusted users can be given read access with a udev rule:

```sh
sudo groupadd --system powercap-readers
sudo usermod -aG powercap-readers "$USER"
sudo tee /etc/udev/rules.d/90-powercap-readers.rules >/dev/null <<'RULE'
SUBSYSTEM=="powercap", ACTION=="add", RUN+="/bin/chgrp powercap-readers /sys%p/energy_uj", RUN+="/bin/chmod g+r /sys%p/energy_uj"
RULE
sudo udevadm control --reload
sudo udevadm trigger --action=add --subsystem-match=powercap
```

## Memory module details (`/sys/firmware/dmi/tables/DMI`)

Speed, slots, form factor and type come from this table. It also holds serial numbers and the machine UUID, so anyone in the group can read those.

```sh
sudo groupadd --system dmi-readers
sudo usermod -aG dmi-readers "$USER"
echo 'z /sys/firmware/dmi/tables/DMI 0440 root dmi-readers -' | sudo tee /etc/tmpfiles.d/dmi-readers.conf
sudo systemd-tmpfiles --create /etc/tmpfiles.d/dmi-readers.conf
```

## Disk health

The "Check Health" button on a disk's Performance page runs `pkexec smartctl -H /dev/<disk>` when you press it, which asks for your password. It needs the smartmontools package.

## Running the whole app as root

This also opens the counters, but it isn't recommended, since it gives everything the app does those rights. System Info shows a "Running as" row so you can always see the situation, for example `root (sudo: alice)`.

## Undoing it

```sh
sudo gpasswd -d "$USER" powercap-readers
sudo gpasswd -d "$USER" dmi-readers
sudo rm -f /etc/udev/rules.d/90-powercap-readers.rules /etc/tmpfiles.d/dmi-readers.conf
```

Then reboot.
