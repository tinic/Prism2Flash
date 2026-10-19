# Prism2Flash

Update a Prism2 wireless card directly on an Amiga A600/A1200.

## Setup

Extract the [Amiga release](https://github.com/tinic/Prism2Flash/releases).
Copy its `Libs/pccard.library` to `LIBS:` if version 2.2 or newer is absent.
Stop WirelessManager and disconnect the card from your network stack.
Open a Shell in the extracted drawer:

```text
Stack 16384
MakeDir Work:P2Backups
```

Keep 20 MiB free. Backups and checksums are automatic. No clock setup is needed.

## Netgear MA401 V2.5 / MA401RA

Download [pk010101.hex](https://junsun.net/linux/intersil-prism/firmware/1.7.4/pk010101.hex)
and [sf010704.hex](https://junsun.net/linux/intersil-prism/firmware/1.7.4/sf010704.hex)
into the extracted drawer, then run:

```text
C/Prism2Flash program --primary pk010101.hex --station sf010704.hex --backup-dir Work:P2Backups
```

## Linksys WPC11 v3 / BroMax WL300

Download [pf010100.hex](https://junsun.net/linux/intersil-prism/firmware/1.4.9/pf010100.hex)
and [sf010704.hex](https://junsun.net/linux/intersil-prism/firmware/1.7.4/sf010704.hex)
into the extracted drawer, then run:

```text
C/Prism2Flash program --primary pf010100.hex --station sf010704.hex --backup-dir Work:P2Backups
```

## Earlier Prism2 cards

For early MA401, DWL-650, SMC2632W, Compaq WL100 and U.S. Robotics 2410
versions using the original Prism2 chipset: download
[s1010701.hex](https://junsun.net/linux/intersil-prism/firmware/1.7.1/s1010701.hex)
into the extracted drawer. Keep the installed primary firmware:

```text
C/Prism2Flash program --station s1010701.hex --backup-dir Work:P2Backups
```

## Other models or an unknown hardware revision

For Linksys WCF12, Buffalo, Pretec, Canon, Allied Telesyn, Addtron, ZoomAir,
Corega, Level-One, Belkin, ASUS, Airvast, EMTAC, Fulbond and Intersil cards,
let the tool select files using the card's actual identity.

Create a `Firmware` drawer and download these four files into it:

- [pk010101.hex](https://junsun.net/linux/intersil-prism/firmware/1.7.4/pk010101.hex)
- [pf010100.hex](https://junsun.net/linux/intersil-prism/firmware/1.4.9/pf010100.hex)
- [sf010704.hex](https://junsun.net/linux/intersil-prism/firmware/1.7.4/sf010704.hex)
- [s1010701.hex](https://junsun.net/linux/intersil-prism/firmware/1.7.1/s1010701.hex)

```text
C/Prism2Flash program --firmware-dir Firmware --backup-dir Work:P2Backups
```

You can also put your own compatible firmware files in that drawer.

## When it finishes

Keep the backup drawer. After `SUCCESS`, reconnect the card to your network stack.
If it reports `Recovery required`, leave power on and keep the files.

[Help and recovery](docs/NATIVE-FLASH.md) · [Hardware results](docs/HARDWARE-TESTING.md) ·
[Build from source](CONTRIBUTING.md)
