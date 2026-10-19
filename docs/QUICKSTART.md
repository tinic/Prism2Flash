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

## Netgear MA401 V2.5

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

## Other models or an unknown hardware revision

For other MA401/MA401RA revisions, D-Link DWL-650, SMC2632W,
Compaq WL100, U.S. Robotics 2410,
Buffalo, Pretec, Canon and other Prism2 cards, let the tool choose from
firmware files using the card's actual identity.

Create a `Firmware` drawer and download these common flash images into it:

- [pk010101.hex](https://junsun.net/linux/intersil-prism/firmware/1.7.4/pk010101.hex)
- [pf010100.hex](https://junsun.net/linux/intersil-prism/firmware/1.4.9/pf010100.hex)
- [sf010704.hex](https://junsun.net/linux/intersil-prism/firmware/1.7.4/sf010704.hex)
- [s1010701.hex](https://junsun.net/linux/intersil-prism/firmware/1.7.1/s1010701.hex)

```text
C/Prism2Flash program --firmware-dir Firmware --backup-dir Work:P2Backups
```

For your own firmware, put its flash `.hex` files in `Firmware` and use the same command.

## When it finishes

Keep the backup drawer. After `SUCCESS`, reconnect the card to your network stack.
If it reports `Recovery required`, leave power on and keep the files.

[Help and recovery](NATIVE-FLASH.md) · [Hardware results](HARDWARE-TESTING.md) ·
[Build from source](../CONTRIBUTING.md)
