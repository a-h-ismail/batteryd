# batteryd

## Purpose

This project provides easy control and persistence of battery charge limiters in Linux.

## Usage

To set battery charge limit to 80% (if your device supports it):
```
batteryctl -s 80
```

To check the current charge limit:
```
batteryctl -g
```

To temporarily remove the limit (until next system boot):

```
batteryctl -f
```

To reload current charge limit from the configuration file:
```
batteryctl -r
```

The service saves and restores the current battery charge threshold, providing persistence across reboots.

## Installation

Install `gcc` using your favorite distribution package manager, example:
```
sudo apt install gcc
```

Clone the repository and run the install script:
```
git clone https://github.com/a-h-ismail/batteryd.git
cd batteryd
chmod +x ./install.sh
sudo ./install.sh
```
Add your user to the `batteryd` group, otherwise `batteryctl` will require root to work:
```
sudo usermod -aG batteryd "$(whoami)"
```
Logout then login for group changes to take effect.

## Uninstallation

Run the `remove.sh` script as root.

## Connecting to batteryd without batteryctl

- `batteryd` is launched by a systemd service and listens on the unix socket `/run/batteryd`.
- Any client (with `batteryd` group membership) connects to the socket and writes the threshold (1 byte).
- The server validates the value and attempts to set the threshold.
- The server writes its response (success or failure with reasons) in one byte and closes the connection.
