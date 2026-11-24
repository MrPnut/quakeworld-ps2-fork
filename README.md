
# QuakeWorld port for the PlayStation 2 (based on quake-ps2)

## WORK IN PROGRESS

This is a work-in-progress port of QW for the PS2 based on the existing NQ quake-ps2 port.

I've only tested connecting to a QWSV on my local network with PCSX2.  I haven't tested on actual hardware yet.

This is a standalone QW client (no single player campaign).  The QW netcode based off Tyrquake has been grafted on to this port, and the NQ code thrown away.

The eventual goal is to be able to play the best QW mods (MegaTF, CA) on my PS2, borrowing features from great server software like MVDSV, and great clients like ezQuake.

### Milestones

1. ~~Merge Tyrquake QW netcode~~
2. ~~.QVD demo playback~~
3. .MVD demo playback
4. Add listen server based off MVDSV
5. Add ezquake inspired server browser
6. Add progs/maps/sounds/skins download to USB mass or PS2 HDD
7. Verify mod support (especially MegaTF)
8. Bot supprt (Frogbot CA)
