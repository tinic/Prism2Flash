# Quick start

Flash a Prism2 wireless card directly on an Amiga A600/A1200.

## Setup once

1. Extract the [Amiga release](https://github.com/tinic/Prism2Flash/releases).
   Copy its `Libs/pccard.library` to `LIBS:` if you do not already have version 2.2 or newer.
2. Put your firmware `.HEX` files in a drawer named `Firmware` beside `C`.
   Keep their original filenames. The tool chooses the matching files automatically.
3. Stop WirelessManager and disconnect the card from your network stack.
4. Open a Shell in the extracted drawer:

```text
Stack 16384
MakeDir Work:P2Backups
```

Keep 20 MiB free for automatic backups. No clock setup is needed.

**Netgear MA401 V2.5 firmware:** download
[ID010001.HEX](https://junsun.net/linux/intersil-prism/dos-resurrection/ID010001.HEX),
[pk010101.hex](https://junsun.net/linux/intersil-prism/firmware/1.7.4/pk010101.hex) and
[sf010704.hex](https://junsun.net/linux/intersil-prism/firmware/1.7.4/sf010704.hex)
into `Firmware`.

## Copy the command for your card

The command is the same for each card. The tool reads its actual hardware identity
and selects compatible firmware; you do not need a platform table.

<details>
<summary>Netgear MA401 / MA401RA</summary>

```text
C/Prism2Flash program --firmware-dir Firmware --backup-dir Work:P2Backups
```

</details>

<details>
<summary>Linksys WPC11 (Prism2 versions)</summary>

```text
C/Prism2Flash program --firmware-dir Firmware --backup-dir Work:P2Backups
```

</details>

<details>
<summary>Linksys WCF12</summary>

```text
C/Prism2Flash program --firmware-dir Firmware --backup-dir Work:P2Backups
```

</details>

<details>
<summary>D-Link DWL-650 (Prism2 versions)</summary>

```text
C/Prism2Flash program --firmware-dir Firmware --backup-dir Work:P2Backups
```

</details>

<details>
<summary>SMC2632W (Prism2 version)</summary>

```text
C/Prism2Flash program --firmware-dir Firmware --backup-dir Work:P2Backups
```

</details>

<details>
<summary>Compaq WL100</summary>

```text
C/Prism2Flash program --firmware-dir Firmware --backup-dir Work:P2Backups
```

</details>

<details>
<summary>Buffalo WLI-CF-S11G</summary>

```text
C/Prism2Flash program --firmware-dir Firmware --backup-dir Work:P2Backups
```

</details>

<details>
<summary>Pretec CompactWLAN</summary>

```text
C/Prism2Flash program --firmware-dir Firmware --backup-dir Work:P2Backups
```

</details>

<details>
<summary>Canon K30225</summary>

```text
C/Prism2Flash program --firmware-dir Firmware --backup-dir Work:P2Backups
```

</details>

<details>
<summary>U.S. Robotics 2410</summary>

```text
C/Prism2Flash program --firmware-dir Firmware --backup-dir Work:P2Backups
```

</details>

<details>
<summary>Allied Telesyn AT-WCL452</summary>

```text
C/Prism2Flash program --firmware-dir Firmware --backup-dir Work:P2Backups
```

</details>

<details>
<summary>Addtron AWP-100</summary>

```text
C/Prism2Flash program --firmware-dir Firmware --backup-dir Work:P2Backups
```

</details>

<details>
<summary>BroMax Freeport / WL300</summary>

```text
C/Prism2Flash program --firmware-dir Firmware --backup-dir Work:P2Backups
```

</details>

<details>
<summary>ZoomAir 11Mbps</summary>

```text
C/Prism2Flash program --firmware-dir Firmware --backup-dir Work:P2Backups
```

</details>

<details>
<summary>Corega WL PCCL-11</summary>

```text
C/Prism2Flash program --firmware-dir Firmware --backup-dir Work:P2Backups
```

</details>

<details>
<summary>Level-One WPC-0100</summary>

```text
C/Prism2Flash program --firmware-dir Firmware --backup-dir Work:P2Backups
```

</details>

<details>
<summary>Belkin 802.11b Prism2</summary>

```text
C/Prism2Flash program --firmware-dir Firmware --backup-dir Work:P2Backups
```

</details>

<details>
<summary>ASUS WL-100</summary>

```text
C/Prism2Flash program --firmware-dir Firmware --backup-dir Work:P2Backups
```

</details>

<details>
<summary>Airvast WL100</summary>

```text
C/Prism2Flash program --firmware-dir Firmware --backup-dir Work:P2Backups
```

</details>

<details>
<summary>EMTAC A2424i</summary>

```text
C/Prism2Flash program --firmware-dir Firmware --backup-dir Work:P2Backups
```

</details>

<details>
<summary>Fulbond Airbond XI-300B</summary>

```text
C/Prism2Flash program --firmware-dir Firmware --backup-dir Work:P2Backups
```

</details>

<details>
<summary>Intersil Prism2 reference cards</summary>

```text
C/Prism2Flash program --firmware-dir Firmware --backup-dir Work:P2Backups
```

</details>

## After it reports SUCCESS

Power-cycle the card/Amiga, then run:

```text
C/Prism2Flash verify --firmware-dir Firmware --backup-dir Work:P2Backups
```

Backups and checksum verification are automatic. Keep the backup drawer.
If it says **Recovery required**, leave power on and keep the backup files.

## Help and reference

```text
C/Prism2Flash --help
```

- [Download binaries and source](https://github.com/tinic/Prism2Flash/releases)
- [Explicit firmware filenames and recovery](NATIVE-FLASH.md)
- [Hardware results](HARDWARE-TESTING.md)
- [Technical compatibility reference](COMPATIBILITY.md)
- [Build and contribution instructions](../CONTRIBUTING.md)

C++23 · AmigaPorts GCC16.2-rc17 · GPL-2.0-only. Firmware is supplied separately.
