# Quick start

## Flash your MA401

**Supported:** qualified Netgear MA401 V2.5 / PD5LMWP200, CIS `000b:7300`,
SST BF:D5. Other cards are refused. Use your own compatible firmware files.

1. Download and extract the latest [Amiga release](https://github.com/tinic/Prism2Flash/releases).
2. Put `ID010001.HEX` and your matching PRI/STA `.hex` files in the same drawer.
   Tested WPA2-capable pair: `pk010101.hex` (PRI 1.1.1), `sf010704.hex` (STA 1.7.4).
3. Stop WirelessManager and disconnect the card from your TCP/IP stack.
4. Check the Amiga clock. Set the offset below: UTC=`0`, UTC+1=`60`, UTC−5=`-300`.
5. Run from the extracted drawer (replace PRI/STA filenames for your firmware):

```text
Stack 16384
MakeDir Work:P2Backups
C/Prism2Flash program --initial ID010001.HEX --primary pk010101.hex --station sf010704.hex --backup-dir Work:P2Backups --utc-offset 0
```

Use an existing persistent backup drawer with at least **20 MiB free**.
Timestamped subdrawers are automatic. The tool saves **one full before backup
and one full after readback**, checks all aliases and saved SHA-256 hashes, and
compares the programmed image. Keep the backups.

6. After **success**, power-cycle the card/Amiga and check the running firmware:

```text
C/Prism2Flash verify --primary pk010101.hex --station sf010704.hex --backup-dir Work:P2Backups --utc-offset 0
```

If the tool reports **recovery required**, keep power on and retain its files.
Do not retry programming or reboot. `C/Prism2Flash --help` shows all options.

[Input files, compatibility and recovery details](NATIVE-FLASH.md).
No firmware files are included in the release.

