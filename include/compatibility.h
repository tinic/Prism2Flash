/* SPDX-License-Identifier: GPL-2.0-only
 * PC Card IDs: prism2.device 2.9 (Neil Cafferkey) and Linux v4.4 Host AP.
 * Platform IDs: Intersil platform release-ID table; see docs/COMPATIBILITY.md.
 */
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
namespace p2::compatibility {
inline constexpr std::array<uint32_t, 48> PcCards{
    0x000b7100, 0x000b7300, 0x0032000c, 0x00890001, 0x00890002, 0x01010001,
    0x01010777, 0x01050000, 0x01260002, 0x01268000, 0x01380002, 0x014d0801,
    0x01560002, 0x01560003, 0x016b0001, 0x016c0001, 0x01bf2328, 0x01bf3301,
    0x01eb080a, 0x01ff0008, 0x02500002, 0x02610002, 0x02680001, 0x02680003,
    0x026f0305, 0x026f030b, 0x02741612, 0x02741613, 0x02743301, 0x028a0002,
    0x028a0673, 0x02aa0002, 0x02ac0002, 0x02ac3021, 0x02d20001, 0x14eab001,
    0x16680101, 0x50c20001, 0x50c27300, 0x90050021, 0xc0010008, 0xc00f0000,
    0xc0380000, 0xc2500002, 0xd6010002, 0xd6010004, 0xd6010005, 0xd6010010,
};
[[nodiscard]] constexpr bool cis(uint16_t manufacturer, uint16_t product) {
  return std::find(PcCards.begin(), PcCards.end(),
                   (uint32_t(manufacturer) << 16) | product) != PcCards.end();
}
// USB-only platforms are intentionally absent: this backend is a PC Card slot.
inline constexpr std::array<uint16_t, 24> Platforms{
    0x8000, 0x8002, 0x8003, 0x8008, 0x8009, 0x800a, 0x800b, 0x800c,
    0x800d, 0x800e, 0x8012, 0x8013, 0x8014, 0x8015, 0x8016, 0x8017,
    0x8018, 0x8019, 0x801a, 0x801b, 0x801c, 0x801d, 0x8022, 0x8023};
[[nodiscard]] constexpr bool platform(uint16_t id) {
  return std::find(Platforms.begin(), Platforms.end(), id) != Platforms.end();
}
struct Product {
  std::string_view first, second, third;
};
inline constexpr std::array Products{
    Product{"Addtron", "AWP-100 Wireless PCMCIA", "Version 01.02"},
    Product{"Canon", "Wireless LAN CF Card K30225", "Version 01.00"},
    Product{"D", "Link DWL-650 11Mbps WLAN Card", "Version 01.02"},
    Product{"Instant Wireless ", " Network PC CARD", "Version 01.02"},
    Product{"SMC", "SMC2632W", "Version 01.02"},
    Product{"BUFFALO", "WLI-CF-S11G", {}},
    Product{"Compaq", "WL200_11Mbps_Wireless_PCI_Card", {}},
    Product{"INTERSIL", "HFA384x/IEEE", {}},
    Product{"Linksys", "Wireless CompactFlash Card", {}},
    Product{"ZoomAir 11Mbps High", "Rate wireless Networking", {}},
    Product{"NETGEAR MA401 Wireless PC", "Card", {}},
    Product{"Pretec", "CompactWLAN Card 802.11b", "2.5"},
    Product{"U.S. Robotics", "IEEE 802.11b PC-CARD", "Version 01.02"},
    Product{"Allied Telesyn", "AT-WCL452 Wireless PCMCIA Radio", "Ver. 1.00"},
    Product{"Wireless LAN", "11Mbps PC Card", "Version 01.02"},
    Product{"SanDisk", "ConnectPlus", {}}};
[[nodiscard]] constexpr bool product(uint16_t manufacturer, uint16_t card,
                                     const std::array<std::string_view, 4> &s) {
  if (cis(manufacturer, card))
    return true;
  if (manufacturer == 0x0156 && card == 2 &&
      (s[0] == "INTERSIL" || s[0] == "Intersil" || s[2] == "Version 01.02"))
    return true;
  for (const auto &p : Products)
    if (s[0] == p.first && s[1] == p.second &&
        (p.third.empty() || s[2] == p.third))
      return true;
  return s[2] == "HFA3863" || s[2] == "ISL37100P" || s[2] == "ISL37101P-10" ||
         s[2] == "ISL37300P";
}
} // namespace p2::compatibility
