
# QuakeWorld port for the PlayStation 2 (based on quake-ps2)

## WORK IN PROGRESS

This is a work-in-progress port of QW for the PS2 based on the existing NQ quake-ps2 port.

This is a standalone QW client (no single player campaign).  The QW netcode based off Tyrquake has been grafted on to this port, and the NQ code thrown away.

The eventual goal is to be able to play the best QW mods (MegaTF, CA) on my PS2, borrowing features from great server software like MVDSV, and great clients like ezQuake.

### Features

1. Supports both DHCP and static ip configurations using `net_dhcp`, `net_ip`, `net_netmask`, and `net_gateway` cvars
2. In game bot submenu to add / remove frogbots using controller
3. Overscan X/Y settings so UI elements don't get clipped on a CRT
4. PS2 controller friendly option selection menu for joining teams in CA, classes in QWTF, etc.

### Known Issues

1. No listen server yet, so you need to connect to an external QW server for the time being.
2. No server browser yet, so you have to to connect to a server using autoexec.cfg or console
3. Timing issue connecting to servers with dhcp enabled.  This has only occurred twice during my testing where "connecting x.x.x.x" will just spam until you reconnect.  It probably because when I test, I connect to a server automatically in autoexec.cfg.  If this happens to you, just use static ip config for now.
4. Everyone's shirt/pants color is the same in CA
5. Keyboard / mouse support hasn't been tested, it may work, but I want to focus on having a quality controller experience first

### Milestones

1. ~~Merge Tyrquake QW netcode~~
2. ~~.QVD demo playback~~
3. ~~Work on real hardware and PCSX2 (including netplay~~)
4. .MVD demo playback
5. Add listen server based off MVDSV
6. Add ezQuake inspired server browser
7. Verify mod support (especially MegaTF)
8. Bot supprt (Frogbot CA)

### Building

Refer to the upstream repo for in depth info, but all you should have to do is clone the repo with submodules and type ```make```
