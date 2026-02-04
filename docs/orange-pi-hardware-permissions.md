<!--
Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
All rights reserved.
This file is a part of fuelflux application
-->

# Hardware Access Setup (SPI / GPIO)
## Orange Pi Zero 2W — No sudo required

This document explains how to correctly configure **SPI and GPIO access** on an Orange Pi (or similar Linux SBC) so that applications can access hardware **without running as root**.

---

## 1. Problem Description

By default, Linux exposes hardware devices like this:

```
crw------- 1 root root /dev/spidev1.0
crw------- 1 root root /dev/gpiochip0
```

This means:
- Only `root` can access SPI/GPIO
- Normal applications fail
- Using `sudo` is unsafe and not recommended

### Correct solution
✔ udev rules  
✔ group-based permissions  
✔ no sudo  
✔ persistent across reboots  

---

## 2. Create Required Groups

```bash
sudo groupadd -f spi
sudo groupadd -f gpio
```

---

## 3. Create udev Rules

Create a rules file:

```bash
sudo nano /etc/udev/rules.d/99-hardware-permissions.rules
```

Paste the following:

```udev
# SPI devices
SUBSYSTEM=="spidev", GROUP="spi", MODE="0660"

# GPIO character devices (libgpiod)
SUBSYSTEM=="gpio", GROUP="gpio", MODE="0660"
SUBSYSTEM=="gpiochip", GROUP="gpio", MODE="0660"
```

Save and exit.

---

## 4. Reload udev Rules

```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

---

## 5. Add User to Groups

```bash
sudo usermod -aG spi,gpio $USER
```

⚠️ Log out and log back in (or reboot).

---

## 6. Verify Permissions

```bash
ls -l /dev/spidev*
ls -l /dev/gpiochip*
```

Expected output:

```
crw-rw---- 1 root spi  ...
crw-rw---- 1 root gpio ...
```

Check groups:

```bash
groups
```

Must include:
```
spi gpio
```

---

## 7. Test Access (No sudo)

### SPI test
```bash
spidev_test -D /dev/spidev1.0
```

### GPIO test
```bash
gpiodetect
gpioinfo
```

If both work — configuration is correct.

---

## 8. systemd Service Example (Optional)

```ini
[Unit]
Description=My Hardware App

[Service]
User=maxirmx
Group=spi
SupplementaryGroups=gpio
ExecStart=/usr/local/bin/myapp

[Install]
WantedBy=multi-user.target
```

---

## 9. Best Practices

✅ Use libgpiod  
✅ Use udev + groups  
✅ Run as normal user  
❌ Do NOT use sudo  
❌ Do NOT chmod /dev manually  
❌ Do NOT use /sys/class/gpio  

---

## 10. Result

✔ Secure  
✔ Persistent  
✔ Production-safe  
✔ Compatible with Orange Pi Zero 2W  

---

If needed, this setup can be extended with:
- SPI bus configuration
- Pinmux overlays
- C / Python examples
- systemd service templates

