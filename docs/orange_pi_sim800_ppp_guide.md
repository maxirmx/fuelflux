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

/dev/ttyS1
115200
modem
noauth
persist
maxfail 0
novj
novjccomp
nocrtscts
local
lock
nodefaultroute
connect "/usr/sbin/chat -v -f /etc/chatscripts/sim800"

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

## 10) Auto-start on boot (optional)

Edit `/etc/rc.local` and add before `exit 0`:

``` bash
pppd call sim800 &
```

------------------------------------------------------------------------

## Troubleshooting

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
