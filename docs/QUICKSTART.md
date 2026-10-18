# Quick start

## Flash your MA401

**Supported:** Netgear MA401 V2.5 / PD5LMWP200 (`000b:7300`, SST BF:D5).
Other cards are refused.

1. Extract the latest [Amiga release](https://github.com/tinic/Prism2Flash/releases).
2. Download [initial code](https://junsun.net/linux/intersil-prism/dos-resurrection/ID010001.HEX) and your compatible PRI/STA files.
   Tested pair: [PRI 1.1.1](https://junsun.net/linux/intersil-prism/firmware/1.7.4/pk010101.hex), [STA 1.7.4](https://junsun.net/linux/intersil-prism/firmware/1.7.4/sf010704.hex).
3. Stop WirelessManager and disconnect the card from your network stack.
4. Check the Amiga clock. Offset examples: UTC=`0`, UTC+1=`60`, UTC−5=`-300`.
5. From the extracted drawer, run (substitute your PRI/STA filenames and offset):

```text
Stack 16384
MakeDir Work:P2Backups
C/Prism2Flash program --initial ID010001.HEX --primary pk010101.hex --station sf010704.hex --backup-dir Work:P2Backups --utc-offset 0
```

Create the backup drawer once; keep **20 MiB free**. Timestamped subdrawers,
one full before backup, one full after readback and checksum checks are automatic.

6. After **success**, power-cycle the card/Amiga and verify:

```text
C/Prism2Flash verify --primary pk010101.hex --station sf010704.hex --backup-dir Work:P2Backups --utc-offset 0
```

**Recovery required:** keep power on; do not retry programming or reboot.
[Help and recovery details](NATIVE-FLASH.md). No firmware is bundled.

