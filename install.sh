#!/bin/bash
set -e

echo Copying to /lib/modules. . .
sudo cp keypad.ko /lib/modules/$(uname -r)/kernel/drivers/input

echo Adding to /etc/modules. . .
echo keypad | sudo tee -a /etc/modules > /dev/null

echo Running depmod. . .
sudo depmod
