# Orange Pi + Waveshare SIM800C: PPP Setup Guide

## Overview

This document describes how to use the Waveshare SIM800C GSM/GPRS HAT as
a cellular data modem for an Orange Pi using PPP over UART.

The result will be a working `ppp0` network interface providing internet
access via 2G GPRS.

------------------------------------------------------------------------

## 1) Prerequisites

Install required packages:

``` bash
sudo apt update
sudo apt install ppp pppoe pppconfig chat minicom
```

You will use: - `pppd` --- the PPP daemon\
- `chat` --- to send AT commands\
- `minicom` --- for manual testing

------------------------------------------------------------------------

## 2) Identify UART device

List available serial ports:

``` bash
ls -l /dev/ttyS*
```

In this guide we assume:

    /dev/ttyS1

If your board uses another port, replace `ttyS1` everywhere below.

------------------------------------------------------------------------

## 3) Verify modem manually

Open the port:

``` bash
sudo minicom -D /dev/ttyS1 -b 115200
```

Type:

    AT

Expected:

    OK

Check SIM:

    AT+CPIN?

Expected:

    +CPIN: READY
    OK

Check network registration:

    AT+CREG?

Good result:

    +CREG: 0,1
    OK

Exit minicom: `Ctrl-A` then `X`.

------------------------------------------------------------------------

## 4) Create chat script

Create file:

``` bash
sudo nano /etc/chatscripts/sim800
```

Paste:

    ABORT "BUSY"
    ABORT "NO CARRIER"
    ABORT "ERROR"
    REPORT CONNECT

    "" AT
    OK AT+CFUN=1
    OK AT+CGATT=1
    OK AT+SAPBR=3,1,"CONTYPE","GPRS"
    OK AT+SAPBR=3,1,"APN","internet"
    OK AT+SAPBR=1,1
    OK AT+SAPBR=2,1
    OK ATD*99#
    CONNECT ""

**Important:** Replace `"internet"` with your carrier APN.

------------------------------------------------------------------------

## 5) Create PPP peer file

Create:

``` bash
sudo nano /etc/ppp/peers/sim800
```

```
/dev/ttyS5
115200
modem
noauth
persist
maxfail 0
holdoff 10
connect-delay 5000
lcp-echo-interval 30
lcp-echo-failure 4
novj
novjccomp
nocrtscts
local
lock
nodefaultroute
connect "/usr/sbin/chat -v -f /etc/chatscripts/sim800"
```

**Reconnection options explained:**
- `persist` - Automatically reconnect if connection drops
- `maxfail 0` - Never give up trying to reconnect (unlimited retries)
- `holdoff 10` - Wait 10 seconds between reconnection attempts
- `connect-delay 5000` - Wait 5 seconds before starting reconnection
- `lcp-echo-interval 30` - Send keepalive packets every 30 seconds
- `lcp-echo-failure 4` - Declare connection dead after 4 failed keepalives (120 seconds total)
------------------------------------------------------------------------

## 6) Permissions

Add your user to `dialout`:

``` bash
sudo usermod -aG dialout $USER
```

Log out and back in (or reboot).

------------------------------------------------------------------------

## 7) Start PPP

``` bash
sudo pppd call sim800
```

Check logs:

``` bash
tail -f /var/log/syslog
```

Look for:

    local IP address ...
    remote IP address ...

Verify interface:

``` bash
ip a
```

You should see `ppp0`.

------------------------------------------------------------------------

## 8) Test internet

``` bash
ping -I ppp0 8.8.8.8
ping google.com
```

------------------------------------------------------------------------

## 9) Stop PPP

``` bash
sudo killall pppd
```

------------------------------------------------------------------------

## 10) Connection monitoring script (optional)

For additional reliability, create a monitoring script that checks connectivity and restarts PPP if needed:

Create `/usr/local/bin/check-ppp.sh`:

```bash
#!/bin/bash

# Configuration
PING_HOST="8.8.8.8"
MAX_FAILURES=3
FAILURE_COUNT=0

while true; do
    # Check if ppp0 exists
    if ! ip link show ppp0 &>/dev/null; then
        echo "$(date): ppp0 interface not found, starting pppd"
        sudo pppd call sim800
        sleep 30
        continue
    fi
    
    # Try to ping
    if ping -I ppp0 -c 1 -W 5 $PING_HOST &>/dev/null; then
        FAILURE_COUNT=0
    else
        ((FAILURE_COUNT++))
        echo "$(date): Ping failed ($FAILURE_COUNT/$MAX_FAILURES)"
        
        if [ $FAILURE_COUNT -ge $MAX_FAILURES ]; then
            echo "$(date): Max failures reached, restarting pppd"
            sudo killall pppd
            sleep 5
            sudo pppd call sim800
            FAILURE_COUNT=0
            sleep 30
        fi
    fi
    
    sleep 60
done
```

Make it executable:

```bash
sudo chmod +x /usr/local/bin/check-ppp.sh
```

Run in background:

```bash
nohup /usr/local/bin/check-ppp.sh > /var/log/ppp-monitor.log 2>&1 &
```

Or create a systemd service for automatic startup (see section 11).

------------------------------------------------------------------------

## 11) Auto-start on boot with systemd (recommended)

Create a systemd service for automatic startup and restart on failure:

Create `/etc/systemd/system/ppp-sim800.service`:

```ini
[Unit]
Description=PPP connection via SIM800
After=network.target
Wants=network.target

[Service]
Type=simple
ExecStart=/usr/sbin/pppd call sim800 nodetach
Restart=always
RestartSec=10
User=root

[Install]
WantedBy=multi-user.target
```

Enable and start the service:

```bash
sudo systemctl daemon-reload
sudo systemctl enable ppp-sim800.service
sudo systemctl start ppp-sim800.service
```

Check status:

```bash
sudo systemctl status ppp-sim800.service
```

View logs:

```bash
sudo journalctl -u ppp-sim800.service -f
```

**Optional:** Add monitoring service

Create `/etc/systemd/system/ppp-monitor.service`:

```ini
[Unit]
Description=PPP Connection Monitor
After=ppp-sim800.service
Requires=ppp-sim800.service

[Service]
Type=simple
ExecStart=/usr/local/bin/check-ppp.sh
Restart=always
RestartSec=10
User=root

[Install]
WantedBy=multi-user.target
```

Enable it:

```bash
sudo systemctl daemon-reload
sudo systemctl enable ppp-monitor.service
sudo systemctl start ppp-monitor.service
```

------------------------------------------------------------------------

## 12) Alternative: rc.local method

Edit `/etc/rc.local` and add before `exit 0`:

``` bash
pppd call sim800 &
```

------------------------------------------------------------------------

## Troubleshooting

### Connection keeps dropping

If your connection frequently drops and reconnects, adjust these parameters in `/etc/ppp/peers/sim800`:

**For unstable networks:**
```
holdoff 30              # Wait longer between attempts (30 seconds)
lcp-echo-interval 60    # Less frequent keepalives (every 60 seconds)
lcp-echo-failure 6      # More tolerant of packet loss (360 seconds timeout)
```

**For stable networks (faster reconnection):**
```
holdoff 5               # Quick retry (5 seconds)
lcp-echo-interval 15    # Frequent keepalives (every 15 seconds)
lcp-echo-failure 3      # Quick failure detection (45 seconds)
```

**Monitor reconnection attempts:**
```bash
# Watch PPP logs in real-time
sudo tail -f /var/log/syslog | grep pppd

# Check connection status
sudo pppd call sim800 debug dump logfd 2 nodetach
```

### No carrier

-   Wrong APN\
-   No data plan\
-   Weak signal

Check signal:

    AT+CSQ

Aim for `15` or higher.

### PPP up but no internet

Ensure default route points to `ppp0`:

``` bash
ip route
```

You should see:

    default dev ppp0 scope link

------------------------------------------------------------------------

End of document.
