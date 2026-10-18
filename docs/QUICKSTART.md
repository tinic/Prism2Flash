# Quick start

## Flash your Prism2 card

Prism2 PC Cards documented by Host AP and prism2.device are recognized.
SRAM width and firmware compatibility are read from the card itself.
[Card and firmware selection](COMPATIBILITY.md).

1. Extract the latest [Amiga release](https://github.com/tinic/Prism2Flash/releases).
   Install the included `Libs/pccard.library` in `LIBS:` if version 2.2 or newer is absent.
2. Download the initial, PRI and STA files matching your card.
   MA401 example: [initial](https://junsun.net/linux/intersil-prism/dos-resurrection/ID010001.HEX), [PRI 1.1.1](https://junsun.net/linux/intersil-prism/firmware/1.7.4/pk010101.hex), [STA 1.7.4](https://junsun.net/linux/intersil-prism/firmware/1.7.4/sf010704.hex).
3. Stop WirelessManager and disconnect the card from your network stack.
4. From the extracted drawer, run (substitute your three firmware filenames):

```text
Stack 16384
MakeDir Work:P2Backups
C/Prism2Flash program --initial ID010001.HEX --primary pk010101.hex --station sf010704.hex --backup-dir Work:P2Backups
```

Create the backup drawer once; keep **20 MiB free**. Timestamped subdrawers,
one full before backup, one full after readback and checksum checks are automatic.
No clock setup is required.

5. After **success**, power-cycle the card/Amiga and verify:

```text
C/Prism2Flash verify --primary pk010101.hex --station sf010704.hex --backup-dir Work:P2Backups
```

**Recovery required:** keep power on; do not retry programming or reboot.
[Help and recovery details](NATIVE-FLASH.md). No firmware is bundled.

