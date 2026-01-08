#!/bin/bash
if [[ $EUID -ne 0 ]]; then
  echo "This script must be run as root."
  exit 1
else
  echo 'Removing battery charge threshold service.'
fi
systemctl disable --now batteryd.service
rm /usr/local/bin/battery{d,ctl} /etc/batteryd.conf /usr/lib/systemd/system/batteryd.service
systemctl daemon-reload
echo 'Done!'