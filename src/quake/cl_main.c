/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

*/
// cl_main.c  -- client main loop

#include "quakedef.h"
#include "bgmusic.h"
#include <setjmp.h>
#include <ctype.h>
#include "ps2/engine_hooks.h"

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include "winquake.h"
#else
#include <sys/types.h>
#include <netinet/in.h>
#endif

//#include "buildinfo.h"
#include "cdaudio.h"
#include "client.h"
#include "cmd.h"
#include "console.h"
#include "draw.h"
#include "input.h"
#include "keys.h"
#include "menu.h"
#include "model.h"
#include "pmove.h"
#include "quakedef.h"
#include "sbar.h"
#include "screen.h"
#include "sys.h"
#include "view.h"
#include "wad.h"

devstats_t dev_stats, dev_peakstats;

// FIXME - header hacks
extern cvar_t cl_hightrack;
extern cvar_t baseskin;
extern cvar_t noskins;

// we need to declare some mouse variables here, because the menu system
// references them even when on a unix system.

qboolean noclip_anglehack;	// remnant from old quake

cvar_t rcon_password = { "rcon_password", "" };
cvar_t rcon_address = { "rcon_address", "" };

cvar_t cl_timeout = { "cl_timeout", "60" };

cvar_t cl_shownet = { "cl_shownet", "0" };	// can be 0, 1, or 2

cvar_t cl_sbar = { "cl_sbar", "0", CVAR_CONFIG };
cvar_t cl_hudswap = { "cl_hudswap", "0", CVAR_CONFIG };
cvar_t cl_maxfps = { "cl_maxfps", "0", CVAR_CONFIG };

cvar_t lookspring = { "lookspring", "0", CVAR_CONFIG };
cvar_t lookstrafe = { "lookstrafe", "0", CVAR_CONFIG };
cvar_t sensitivity = { "sensitivity", "3", CVAR_CONFIG };

cvar_t m_pitch = { "m_pitch", "0.022", CVAR_CONFIG };
cvar_t m_yaw = { "m_yaw", "0.022" };
cvar_t m_forward = { "m_forward", "1" };
cvar_t m_side = { "m_side", "0.8" };

cvar_t cl_maxpitch = {"cl_maxpitch", "90", CVAR_CONFIG };
cvar_t cl_minpitch = {"cl_minpitch", "-90", CVAR_CONFIG };

cvar_t m_freelook = { "m_freelook", "0", CVAR_CONFIG };

cvar_t entlatency = { "entlatency", "20" };
cvar_t cl_predict_players = { "cl_predict_players", "1" };
cvar_t cl_predict_players2 = { "cl_predict_players2", "1" };
cvar_t cl_solid_players = { "cl_solid_players", "1" };

cvar_t localid = { "localid", "" };

static qboolean allowremotecmd = true;

//
// info mirrors
//
cvar_t password = { "password", "", CVAR_USERINFO };
cvar_t spectator = { "spectator", "", CVAR_USERINFO };
cvar_t name = { "name", "unnamed", CVAR_CONFIG | CVAR_USERINFO };
cvar_t team = { "team", "", CVAR_CONFIG | CVAR_USERINFO };
cvar_t topcolor = { "topcolor", "0", CVAR_CONFIG | CVAR_USERINFO };
cvar_t bottomcolor = { "bottomcolor", "0", CVAR_CONFIG | CVAR_USERINFO };
cvar_t rate = { "rate", "2500", CVAR_CONFIG | CVAR_USERINFO };
cvar_t noaim = { "noaim", "0", CVAR_CONFIG | CVAR_USERINFO };
cvar_t msg = { "msg", "1", CVAR_CONFIG | CVAR_USERINFO };

cvar_t skin = { "skin", "", CVAR_CONFIG | CVAR_USERINFO };

client_static_t cls;
client_state_t cl;

entity_state_t cl_baselines[MAX_EDICTS];
entity_t cl_static_entities[MAX_STATIC_ENTITIES];
lightstyle_t cl_lightstyle[MAX_LIGHTSTYLES];
dlight_t cl_dlights[MAX_DLIGHTS];

double connect_time = -1;	// for connection retransmits

quakeparms_t *host_parms;

qboolean host_initialized;	// true if into command execution

double host_frametime;
double realtime;		// without any filtering or bounding
static double oldrealtime;	// last frame run
int host_framecount;

int host_hunklevel;

int minimum_memory;

byte *host_basepal;
byte *host_colormap;
byte **host_transtables;

netadr_t master_adr;		// address of the master server

cvar_t host_speeds = { "host_speeds", "0" };	// set for running times
cvar_t developer = { "developer", "0" };

int fps_count;

static jmp_buf host_abort;
static float server_version = 0;// version of server we connected to

/*
==================
CL_Quit_f
==================
*/
void
CL_Quit_f(void)
{
    CL_Disconnect();
    Sys_Quit();
}

/*
=======================
CL_Version_f
======================
*/
void
CL_Version_f(void)
{
    Con_Printf("Version TyrQuake-%s\n", QUAKESPASM_VER_STRING);
    Con_Printf("Exe: " __TIME__ " " __DATE__ "\n");
}


/*
=======================
CL_SendConnectPacket

called by CL_Connect_f and CL_CheckResend
======================
*/
void
CL_SendConnectPacket(void)
{
    netadr_t adr;
    char data[2048];
    double t1, t2;

// JACK: Fixed bug where DNS lookups would cause two connects real fast
//       Now, adds lookup time to the connect time.
//               Should I add it to realtime instead?!?!

    if (cls.state != ca_disconnected)
	return;

    t1 = Sys_DoubleTime();

    if (!NET_StringToAdr(cls.servername, &adr)) {
	Con_Printf("Bad server address\n");
	connect_time = -1;
	return;
    }

    if (adr.port == 0)
	adr.port = BigShort(27500);
    t2 = Sys_DoubleTime();

    connect_time = realtime + t2 - t1;	// for retransmit requests

    cls.qport = Cvar_VariableValue("qport");

    Info_SetValueForStarKey(cls.userinfo, "*ip", NET_AdrToString(adr),
			    MAX_INFO_STRING);

//      Con_Printf ("Connecting to %s...\n", cls.servername);
    qsnprintf(data, sizeof(data), "%c%c%c%cconnect %i %i %i \"%s\"\n",
	      255, 255, 255, 255, PROTOCOL_VERSION, cls.qport, cls.challenge,
	      cls.userinfo);
    NET_SendPacket(strlen(data), data, adr);
}

/*
=================
CL_CheckForResend

Resend a connect message if the last one has timed out

=================
*/
void
CL_CheckForResend(void)
{
    netadr_t adr;
    char data[2048];
    double t1, t2;

    if (connect_time == -1)
	return;
    if (cls.state != ca_disconnected)
	return;
    if (connect_time && realtime - connect_time < 5.0)
	return;

    t1 = Sys_DoubleTime();
    if (!NET_StringToAdr(cls.servername, &adr)) {
	Con_Printf("Bad server address\n");
	connect_time = -1;
	return;
    }

    if (adr.port == 0)
	adr.port = BigShort(27500);
    t2 = Sys_DoubleTime();

    connect_time = realtime + t2 - t1;	// for retransmit requests

    Con_Printf("Connecting to %s...\n", cls.servername);
    qsnprintf(data, sizeof(data), "%c%c%c%cgetchallenge\n", 255, 255, 255, 255);
    NET_SendPacket(strlen(data), data, adr);
}

void
CL_BeginServerConnect(void)
{
    connect_time = 0;
    CL_CheckForResend();
}

/*
================
CL_Connect_f

================
*/
void
CL_Connect_f(void)
{
    const char *server;

    if (Cmd_Argc() != 2) {
	Con_Printf("usage: connect <server>\n");
	return;
    }

    server = Cmd_Argv(1);

    CL_Disconnect();

    strncpy(cls.servername, server, sizeof(cls.servername) - 1);
    CL_BeginServerConnect();
}


/*
=====================
CL_Rcon_f

  Send the rest of the command line over as
  an unconnected command.
=====================
*/
void
CL_Rcon_f(void)
{
    char message[1024];
    int i;
    netadr_t to;

    if (!rcon_password.string) {
	Con_Printf("You must set 'rcon_password' before\n"
		   "issuing an rcon command.\n");
	return;
    }

    message[0] = '\xff';
    message[1] = '\xff';
    message[2] = '\xff';
    message[3] = '\xff';
    message[4] = 0;

    strcat(message, "rcon ");

    strcat(message, rcon_password.string);
    strcat(message, " ");

    for (i = 1; i < Cmd_Argc(); i++) {
	strcat(message, Cmd_Argv(i));
	strcat(message, " ");
    }

    if (cls.state >= ca_connected)
	to = cls.netchan.remote_address;
    else {
	if (!strlen(rcon_address.string)) {
	    Con_Printf("You must either be connected,\n"
		       "or set the 'rcon_address' cvar\n"
		       "to issue rcon commands\n");

	    return;
	}
	NET_StringToAdr(rcon_address.string, &to);
    }

    NET_SendPacket(strlen(message) + 1, message, to);
}

void
Host_ClearMemory()
{
    Con_DPrintf("Clearing memory\n");
    D_FlushCaches();
    Mod_ClearAll();
    S_StopAllSounds(true);
    BGM_Stop();
    if (host_hunklevel)
	Hunk_FreeToLowMark(host_hunklevel);

    CL_ClearTEnts();

// clear other arrays
    memset(cl_dlights, 0, sizeof(cl_dlights));
    memset(cl_lightstyle, 0, sizeof(cl_lightstyle));

    cl.free_efrags = NULL;
    cl.num_efrags = 0;
}

/*
=====================
CL_ClearState

=====================
*/
void
CL_ClearState()
{
    S_StopAllSounds(true);

    /* Wipe the entire cl structure */
    memset(&cl, 0, sizeof(cl));

    Host_ClearMemory();

    SZ_Clear(&cls.netchan.message);

    for (int i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		cl_lightstyle[i].length = 0;
		cl_lightstyle[i].map[0] = 0;
		cl_lightstyle[i].average = 'm';
		cl_lightstyle[i].peak = 'm';
	}
}

/*
=====================
CL_Disconnect

Sends a disconnect message to the server
This is also called on Host_Error, so it shouldn't cause any errors
=====================
*/
void
CL_Disconnect(void)
{
    int i;
    byte final[10];

    connect_time = -1;

#ifdef _WIN32
    SetWindowText(mainwindow, "QuakeWorld: disconnected");
#endif

// stop sounds (especially looping!)
    BGM_Stop();
    CDAudio_Stop();
    S_StopAllSounds(true);

    /* Clear up view, remove palette shift */
    scr_centertime_off = 0;
    for (i = 0; i < NUM_CSHIFTS; i++)
        cl.cshifts[i].percent = 0;
    
// if running a local server, shut it down
    if (cls.demoplayback)
	CL_StopPlayback();
    else if (cls.state != ca_disconnected) {
	if (cls.demorecording)
	    CL_Stop_f();

	final[0] = clc_stringcmd;
	strcpy((char *)final + 1, "drop");
	Netchan_Transmit(&cls.netchan, 6, final);
	Netchan_Transmit(&cls.netchan, 6, final);
	Netchan_Transmit(&cls.netchan, 6, final);

	cls.state = ca_disconnected;
	cls.demoplayback = cls.demorecording = cls.timedemo = false;
    }
    Cam_Reset();

    if (cls.download) {
	fclose(cls.download);
	cls.download = NULL;
    }

    CL_StopUpload();
    cl.intermission = 0; /* FIXME - for SCR_UpdateScreen */
}

void
CL_Disconnect_f(void)
{
    CL_Disconnect();
}

/*
====================
CL_User_f

user <name or userid>

Dump userdata / masterdata for a user
====================
*/
void
CL_User_f(void)
{
    int uid;
    int i;

    if (Cmd_Argc() != 2) {
	Con_Printf("Usage: user <username / userid>\n");
	return;
    }

    uid = atoi(Cmd_Argv(1));

    for (i = 0; i < MAX_CLIENTS; i++) {
	if (!cl.players[i].name[0])
	    continue;
	if (cl.players[i].userid == uid
	    || !strcmp(cl.players[i].name, Cmd_Argv(1))) {
	    Info_Print(cl.players[i].userinfo);
	    return;
	}
    }
    Con_Printf("User not in server.\n");
}

/*
====================
CL_Users_f

Dump userids for all current players
====================
*/
void
CL_Users_f(void)
{
    int i, count;

    count = 0;
    Con_Printf("userid frags name\n");
    Con_Printf("------ ----- ----\n");
    for (i = 0; i < MAX_CLIENTS; i++) {
	if (cl.players[i].name[0]) {
	    Con_Printf("%6i %4i %s\n", cl.players[i].userid, cl.players[i].frags, cl.players[i].name);
	    count++;
	}
    }

    Con_Printf("%i total users\n", count);
}

void
CL_Color_f(void)
{
    // just for quake compatability...
    int top, bottom;
    char num[16];

    if (Cmd_Argc() == 1) {
	Con_Printf("\"color\" is \"%s %s\"\n",
		   Info_ValueForKey(cls.userinfo, "topcolor"),
		   Info_ValueForKey(cls.userinfo, "bottomcolor"));
	Con_Printf("color <0-13> [0-13]\n");
	return;
    }

    if (Cmd_Argc() == 2)
	top = bottom = atoi(Cmd_Argv(1));
    else {
	top = atoi(Cmd_Argv(1));
	bottom = atoi(Cmd_Argv(2));
    }

    top &= 15;
    if (top > 13)
	top = 13;
    bottom &= 15;
    if (bottom > 13)
	bottom = 13;

    qsnprintf(num, sizeof(num), "%i", top);
    Cvar_Set("topcolor", num);
    qsnprintf(num, sizeof(num), "%i", bottom);
    Cvar_Set("bottomcolor", num);
}

/*
==================
CL_FullServerinfo_f

Sent by server when serverinfo changes
==================
*/
void
CL_FullServerinfo_f(void)
{
    char *p;
    float v;

    if (Cmd_Argc() != 2) {
	Con_Printf("usage: fullserverinfo <complete info string>\n");
	return;
    }

    strcpy(cl.serverinfo, Cmd_Argv(1));

    if ((p = Info_ValueForKey(cl.serverinfo, "*vesion")) && *p) {
	v = Q_atof(p);
	if (v) {
	    if (!server_version)
		Con_Printf("Version %1.2f Server\n", v);
	    server_version = v;
	}
    }
}

/*
==================
CL_FullInfo_f

Allow clients to change userinfo
==================
Casey was here :)
*/
void
CL_FullInfo_f(void)
{
    char key[512];
    char value[512];
    char *o;
    const char *s;

    if (Cmd_Argc() != 2) {
	Con_Printf("fullinfo <complete info string>\n");
	return;
    }

    s = Cmd_Argv(1);
    if (*s == '\\')
	s++;
    while (*s) {
	o = key;
	while (*s && *s != '\\')
	    *o++ = *s++;
	*o = 0;

	if (!*s) {
	    Con_Printf("MISSING VALUE\n");
	    return;
	}

	o = value;
	s++;
	while (*s && *s != '\\')
	    *o++ = *s++;
	*o = 0;

	if (*s)
	    s++;

	if (!strcasecmp(key, "pmodel") || !strcasecmp(key, "emodel"))
	    continue;

	Info_SetValueForKey(cls.userinfo, key, value, MAX_INFO_STRING);
    }
}

/*
==================
CL_SetInfo_f

Allow clients to change userinfo
==================
*/
void
CL_SetInfo_f(void)
{
    if (Cmd_Argc() == 1) {
	Info_Print(cls.userinfo);
	return;
    }
    if (Cmd_Argc() != 3) {
	Con_Printf("usage: setinfo [ <key> <value> ]\n");
	return;
    }
    if (!strcasecmp(Cmd_Argv(1), "pmodel") || !strcmp(Cmd_Argv(1), "emodel"))
	return;

    Info_SetValueForKey(cls.userinfo, Cmd_Argv(1), Cmd_Argv(2),
			MAX_INFO_STRING);
    if (cls.state >= ca_connected)
	Cmd_ForwardToServer();
}

/*
====================
CL_Packet_f

packet <destination> <contents>

Contents allows \n escape character
====================
*/
void
CL_Packet_f(void)
{
    char send[2048];
    int i, l;
    const char *in;
    char *out;
    netadr_t adr;

    if (Cmd_Argc() != 3) {
	Con_Printf("packet <destination> <contents>\n");
	return;
    }

    if (!NET_StringToAdr(Cmd_Argv(1), &adr)) {
	Con_Printf("Bad address\n");
	return;
    }

    in = Cmd_Argv(2);
    out = send + 4;
    send[0] = send[1] = send[2] = send[3] = 0xff;

    l = strlen(in);
    for (i = 0; i < l; i++) {
	if (in[i] == '\\' && in[i + 1] == 'n') {
	    *out++ = '\n';
	    i++;
	} else
	    *out++ = in[i];
    }
    *out = 0;

    NET_SendPacket(out - send, send, adr);
}


/*
=====================
CL_NextDemo

Called to play the next demo in the demo loop
=====================
*/
void
CL_NextDemo(void)
{
    char str[1024];

    if (cls.demonum == -1)
	return;			// don't play demos

    CL_Disconnect();

    if (!cls.demos[cls.demonum][0] || cls.demonum == MAX_DEMOS) {
	cls.demonum = 0;
	if (!cls.demos[cls.demonum][0]) {
//                      Con_Printf ("No demos listed with startdemos\n");
	    cls.demonum = -1;
	    return;
	}
    }

    qsnprintf(str, sizeof(str), "playdemo %s\n", cls.demos[cls.demonum]);
    Cbuf_InsertText(str);
    cls.demonum++;
}


/*
=================
CL_Changing_f

Just sent as a hint to the client that they should
drop to full console
=================
*/
void
CL_Changing_f(void)
{
    if (cls.download)		// don't change when downloading
	return;

    S_StopAllSounds(true);
    cl.intermission = 0;
    cls.state = ca_connected;	// not active anymore, but not disconnected
    Con_Printf("\nChanging map...\n");
}


/*
=================
CL_Reconnect_f

The server is changing levels
=================
*/
void
CL_Reconnect_f(void)
{
    int i;

    if (cls.download)		// don't change when downloading
	return;

    S_StopAllSounds(true);
    scr_centertime_off = 0;
    for (i = 0; i < NUM_CSHIFTS; i++)
        cl.cshifts[i].percent = 0;

    if (cls.state == ca_connected) {
	Con_Printf("reconnecting...\n");
	MSG_WriteChar(&cls.netchan.message, clc_stringcmd);
	MSG_WriteString(&cls.netchan.message, "new");
	return;
    }

    if (!*cls.servername) {
	Con_Printf("No server to reconnect to...\n");
	return;
    }

    CL_Disconnect();
    CL_BeginServerConnect();
}

/*
=================
CL_ConnectionlessPacket

Responses to broadcasts, etc
=================
*/
void
CL_ConnectionlessPacket(void)
{
    int c;

    MSG_BeginReading();
    MSG_ReadLong();		// skip the -1

    c = MSG_ReadByte();
    if (!cls.demoplayback)
	Con_Printf("%s: ", NET_AdrToString(net_from));
//      Con_DPrintf ("%s", net_message.data + 5);
    if (c == S2C_CONNECTION) {
	Con_Printf("connection\n");
	if (cls.state >= ca_connected) {
	    if (!cls.demoplayback)
		Con_Printf("Dup connect received.  Ignored.\n");
	    return;
	}
	Netchan_Setup(&cls.netchan, net_from, cls.qport);
	MSG_WriteChar(&cls.netchan.message, clc_stringcmd);
	MSG_WriteString(&cls.netchan.message, "new");
	cls.state = ca_connected;
	Con_Printf("Connected.\n");
	allowremotecmd = false;	// localid required now for remote cmds
	return;
    }
    // remote command from gui front end
    if (c == A2C_CLIENT_COMMAND) {
	char cmdbuf[1024];
	char idbuf[64];
	char *idstring;

	Con_Printf("client command\n");

	if (net_from.ip.l != net_local_adr.ip.l
	    && net_from.ip.l != htonl(INADDR_LOOPBACK)) {
	    Con_Printf("Command packet from remote host.  Ignored.\n");
	    return;
	}

	q_strlcpy(cmdbuf, MSG_ReadString(), sizeof(cmdbuf));
	q_strlcpy(idbuf, MSG_ReadString(), sizeof(idbuf));
	idstring = idbuf;

	/* Strip leading and trailing spaces */
	while (*idstring && isspace((unsigned char)*idstring))
    {
        idstring++;
    }
	    
	while (*idstring && isspace((unsigned char)idstring[strlen(idstring) - 1]))
    {
        idstring[strlen(idstring) - 1] = 0;
    }
	    
	if (!allowremotecmd && !*localid.string) {
	    Con_Printf("===========================\n"
		       "Command packet received from local host, but no "
		       "localid has been set.  You may need to upgrade your "
		       "server browser.\n"
		       "===========================\n");
	    return;
	}
	if (!allowremotecmd && strcmp(localid.string, idstring)) {
	    Con_Printf("===========================\n"
		       "Invalid localid on command packet received from local "
		       "host.\n"
		       " |%s| != |%s|\n"
		       "You may need to reload your server browser and "
		       "QuakeWorld.\n"
		       "===========================\n",
		       idstring, localid.string);
	    Cvar_Set("localid", "");
	    return;
	}

	Cbuf_AddText(cmdbuf);
	allowremotecmd = false;
	return;
    }
    // print command from somewhere
    if (c == A2C_PRINT) {
	Con_Printf("%s", "print\n");
	Con_Printf("%s", MSG_ReadString());
	return;
    }
    // ping from somewhere
    if (c == A2A_PING) {
	char data[6];

	Con_Printf("%s", "ping\n");

	data[0] = 0xff;
	data[1] = 0xff;
	data[2] = 0xff;
	data[3] = 0xff;
	data[4] = A2A_ACK;
	data[5] = 0;

	NET_SendPacket(6, &data, net_from);
	return;
    }

    if (c == S2C_CHALLENGE) {
	Con_Printf("%s", "challenge\n");
	cls.challenge = atoi(MSG_ReadString());
	CL_SendConnectPacket();
	return;
    }
#if 0
    if (c == svc_disconnect) {
	Con_Printf("disconnect\n");
	Host_EndGame("Server disconnected");
    }
#endif

    Con_Printf("unknown:  %c\n", c);
}


/*
=================
CL_ReadPackets
=================
*/
static void
CL_ReadPackets(void)
{
    while (CL_GetMessage()) {
	if (*(int *)net_message.data == -1) {
	    /* remote command packet */
	    CL_ConnectionlessPacket();
	    continue;
	}

	if (net_message.cursize < 8) {
	    Con_Printf("%s: Runt packet\n", NET_AdrToString(net_from));
	    continue;
	}

	/* packet from server */
	if (!cls.demoplayback &&
	    !NET_CompareAdr(net_from, cls.netchan.remote_address)) {
	    Con_DPrintf("%s:sequenced packet without connection\n",
			NET_AdrToString(net_from));
	    continue;
	}
	if (!Netchan_Process(&cls.netchan))
	    continue;		/* wasn't accepted for some reason */

	CL_ParseServerMessage();
    }

    /* check timeout */
    if (cls.state >= ca_connected
	&& realtime - cls.netchan.last_received > cl_timeout.value) {
	Con_Printf("\nServer connection timed out.\n");
	CL_Disconnect();
    }
}

//=============================================================================

/*
=====================
CL_Download_f
=====================
*/
void
CL_Download_f(void)
{
    char *p, *q;

    if (cls.state == ca_disconnected) {
	Con_Printf("Must be connected.\n");
	return;
    }

    if (Cmd_Argc() != 2) {
	Con_Printf("Usage: download <datafile>\n");
	return;
    }

    qsnprintf(cls.downloadname, sizeof(cls.downloadname), "%s/%s", com_gamedir, Cmd_Argv(1));

    p = cls.downloadname;
    for (;;) {
	if ((q = strchr(p, '/')) != NULL) {
	    *q = 0;
	    Sys_mkdir(cls.downloadname);
	    *q = '/';
	    p = q + 1;
	} else
	    break;
    }

    strcpy(cls.downloadtempname, cls.downloadname);
    cls.download = fopen(cls.downloadname, "wb");
    cls.downloadtype = dl_single;

    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    MSG_WriteStringf(&cls.netchan.message, "download %s\n", Cmd_Argv(1));
}

void
CL_AddCommands()
{
    Cmd_AddCommand("version", CL_Version_f);

    Cmd_AddCommand("changing", CL_Changing_f);
    Cmd_AddCommand("disconnect", CL_Disconnect_f);
    Cmd_AddCommand("record", CL_Record_f);
    Cmd_AddCommand("rerecord", CL_ReRecord_f);
    Cmd_AddCommand("stop", CL_Stop_f);
    Cmd_AddCommand("playdemo", CL_PlayDemo_f);
    Cmd_AddCommand("timedemo", CL_TimeDemo_f);

    Cmd_AddCommand("skins", Skin_Skins_f);
    Cmd_AddCommand("allskins", Skin_AllSkins_f);

    Cmd_AddCommand("quit", CL_Quit_f);

    Cmd_AddCommand("connect", CL_Connect_f);
    Cmd_AddCommand("reconnect", CL_Reconnect_f);

    Cmd_AddCommand("rcon", CL_Rcon_f);
    Cmd_AddCommand("packet", CL_Packet_f);
    Cmd_AddCommand("user", CL_User_f);
    Cmd_AddCommand("users", CL_Users_f);

    Cmd_AddCommand("setinfo", CL_SetInfo_f);
    Cmd_AddCommand("fullinfo", CL_FullInfo_f);
    Cmd_AddCommand("fullserverinfo", CL_FullServerinfo_f);

    Cmd_AddCommand("color", CL_Color_f);
    Cmd_AddCommand("download", CL_Download_f);

    Cmd_AddCommand("nextul", CL_NextUpload);
    Cmd_AddCommand("stopul", CL_StopUpload);

    //
    // forward to server commands
    //
    Cmd_AddCommand("kill", NULL);
    Cmd_AddCommand("pause", NULL);
    Cmd_AddCommand("say", NULL);
    Cmd_AddCommand("say_team", NULL);
    Cmd_AddCommand("serverinfo", NULL);

    //
    //  Windows commands
    //
#ifdef _WIN32
    Cmd_AddCommand("windows", CL_Windows_f);
#endif

    CL_Input_AddCommands();
}

void
CL_RegisterVariables()
{
    Cvar_RegisterVariable(&host_speeds);
    Cvar_RegisterVariable(&cl_warncmd);
    Cvar_RegisterVariable(&cl_upspeed);
    Cvar_RegisterVariable(&cl_forwardspeed);
    Cvar_RegisterVariable(&cl_backspeed);
    Cvar_RegisterVariable(&cl_sidespeed);
    Cvar_RegisterVariable(&cl_movespeedkey);
    Cvar_RegisterVariable(&cl_yawspeed);
    Cvar_RegisterVariable(&cl_pitchspeed);
    Cvar_RegisterVariable(&cl_anglespeedkey);
    Cvar_RegisterVariable(&cl_run);
    Cvar_RegisterVariable(&cl_shownet);
    Cvar_RegisterVariable(&cl_sbar);
    Cvar_RegisterVariable(&cl_hudswap);
    Cvar_RegisterVariable(&cl_maxfps);
    Cvar_RegisterVariable(&cl_timeout);
    Cvar_RegisterVariable(&lookspring);
    Cvar_RegisterVariable(&lookstrafe);
    Cvar_RegisterVariable(&sensitivity);

    Cvar_RegisterVariable(&m_pitch);
    Cvar_RegisterVariable(&m_yaw);
    Cvar_RegisterVariable(&m_forward);
    Cvar_RegisterVariable(&m_side);

    Cvar_RegisterVariable(&cl_maxpitch);
    Cvar_RegisterVariable(&cl_minpitch);

    Cvar_RegisterVariable(&m_freelook);

    Cvar_RegisterVariable(&rcon_password);
    Cvar_RegisterVariable(&rcon_address);

    Cvar_RegisterVariable(&entlatency);
    Cvar_RegisterVariable(&cl_predict_players2);
    Cvar_RegisterVariable(&cl_predict_players);
    Cvar_RegisterVariable(&cl_solid_players);

    Cvar_RegisterVariable(&localid);

    Cvar_RegisterVariable(&baseskin);
    Cvar_RegisterVariable(&noskins);

    //
    // info mirrors
    //
    Cvar_RegisterVariable(&name);
    Cvar_RegisterVariable(&password);
    Cvar_RegisterVariable(&spectator);
    Cvar_RegisterVariable(&skin);
    Cvar_RegisterVariable(&team);
    Cvar_RegisterVariable(&topcolor);
    Cvar_RegisterVariable(&bottomcolor);
    Cvar_RegisterVariable(&rate);
    Cvar_RegisterVariable(&msg);
    Cvar_RegisterVariable(&noaim);

    Cvar_RegisterVariable(&developer);

    CL_Input_RegisterVariables();
    CL_Cam_RegisterVariables();
    CL_Predict_RegisterVariables();
}

/*
=================
CL_Init
=================
*/
void
CL_Init(void)
{
    char st[80];

    cls.state = ca_disconnected;

    Info_SetValueForKey(cls.userinfo, "name", "unnamed", MAX_INFO_STRING);
    Info_SetValueForKey(cls.userinfo, "topcolor", "0", MAX_INFO_STRING);
    Info_SetValueForKey(cls.userinfo, "bottomcolor", "0", MAX_INFO_STRING);
    Info_SetValueForKey(cls.userinfo, "rate", "2500", MAX_INFO_STRING);
    Info_SetValueForKey(cls.userinfo, "msg", "1", MAX_INFO_STRING);
    qsnprintf(st, sizeof(st), "TyrQuake-%s", QUAKESPASM_VER_STRING);
    Info_SetValueForStarKey(cls.userinfo, "*ver", st, MAX_INFO_STRING);

    CL_InitTEnts();

    if (COM_CheckParm("-developer"))
	Cvar_SetValue("developer", 1);
}

void
CL_Reinit()
{
    CL_InitTEnts();
}

/*
================
Host_EndGame

Call this to drop to a console without exiting the qwcl
================
*/
void
Host_EndGame(const char *message, ...)
{
    va_list argptr;
    char string[MAX_PRINTMSG];

    va_start(argptr, message);
    qvsnprintf(string, sizeof(string), message, argptr);
    va_end(argptr);
    Con_Printf("\n===========================\n");
    Con_Printf("Host_EndGame: %s\n", string);
    Con_Printf("===========================\n\n");

    CL_Disconnect();

    longjmp(host_abort, 1);
}

/*
================
Host_Error

This shuts down the client and exits qwcl
================
*/
void
Host_Error(const char *error, ...)
{
    va_list argptr;
    char string[MAX_PRINTMSG];
    static qboolean inerror = false;

    if (inerror)
	Sys_Error("Host_Error: recursively entered");
    inerror = true;

    va_start(argptr, error);
    qvsnprintf(string, sizeof(string), error, argptr);
    va_end(argptr);
    Con_Printf("Host_Error: %s\n", string);

    CL_Disconnect();
    cls.demonum = -1;

    inerror = false;

// FIXME
    Sys_Error("Host_Error: %s", string);
}

/*
===============
Host_WriteConfiguration

Writes key bindings and archived cvars to config.cfg
===============
*/
void Host_WriteConfiguration (void)
{
	FILE	*f;

// dedicated servers initialize the host but don't parse and set the
// config.cfg cvars
	if (host_initialized && !isDedicated && !host_parms->errstate)
	{
		// [PS2_QUAKE]: through the backend, which keeps it on the memory card on a console.
		f = PS2_ConfigOpenWrite ();
		if (!f)
		{
			Con_Printf ("Couldn't write config.cfg.\n");
			return;
		}

		//VID_SyncCvars (); //johnfitz -- write actual current mode to config file, in case cvars were messed with

		Key_WriteBindings (f);
		Cvar_WriteVariables (f);

		//johnfitz -- extra commands to preserve state
		fprintf (f, "vid_restart\n");
		if (in_mlook.state & 1) fprintf (f, "+mlook\n");
		// [PS2_QUAKE]: and off, which the PS2's defaults aren't: +mlook lets the look stick look
		// up and down there (input.cpp), and config.cfg runs after them.
		else fprintf (f, "-mlook\n");
		//johnfitz

		PS2_ConfigCloseWrite (f); // [PS2_QUAKE]
	}
}

//============================================================================

#if 0
/*
==================
Host_SimulationTime

This determines if enough time has passed to run a simulation frame
==================
*/
qboolean
Host_SimulationTime(float time)
{
    float fps;

    if (oldrealtime > realtime)
	oldrealtime = 0;

    if (cl_maxfps.value)
	fps = qmax(30.0, qmin(cl_maxfps.value, 72.0));
    else
	fps = qmax(30.0, qmin(rate.value / 80.0, 72.0));

    if (!cls.timedemo && (realtime + time) - oldrealtime < 1.0 / fps)
	return false;		// framerate is too high
    return true;
}
#endif


/*
==================
Host_Frame

Runs all active servers
==================
*/
void
Host_Frame(float time)
{
    static double time1 = 0;
    static double time2 = 0;
    static double time3 = 0;
    int pass1, pass2, pass3;
    float fps;
    physent_stack_t pestack;

    /* something bad happened, or the server disconnected */
    if (setjmp(host_abort))
	return;

    // decide the simulation time
    realtime += time;

    if (cl_maxfps.value)
	fps = q_max(30.0f, q_min(cl_maxfps.value, 72.0f));
    else
	fps = q_max(30.0f, q_min(rate.value / 80.0f, 72.0f));

    if (!cls.timedemo && realtime - oldrealtime < 1.0 / fps)
	return;		// framerate is too high

    host_frametime = realtime - oldrealtime;
    oldrealtime = realtime;
    if (host_frametime > 0.2)
	host_frametime = 0.2;

    // get new key events
    Sys_SendKeyEvents();

    /* allow mice or other external controllers to add commands */
    IN_Commands();

    /* process console commands */
    Cbuf_Execute();

    // fetch results from server
    CL_ReadPackets();

    /* Set the pmove physents based on current state... */
    CL_SetSolidEntities(&pestack);

    // send intentions now
    // resend a connection request if necessary
    if (cls.state == ca_disconnected) {
	CL_CheckForResend();
    } else
	CL_SendCmd(&pestack);

    // Set up prediction for other players
    CL_SetUpPlayerPrediction(&pestack, false);

    // do client side motion prediction
    CL_PredictMove(&pestack);

    // Set up prediction for other players
    CL_SetUpPlayerPrediction(&pestack, true);

    // build a refresh entity list
    CL_EmitEntities(&pestack);

    // update video
    if (host_speeds.value)
	time1 = Sys_DoubleTime();

    SCR_UpdateScreen();
    CL_RunParticles();

    if (host_speeds.value)
	time2 = Sys_DoubleTime();

    /* update audio */
    if (cls.state == ca_active) {
	S_Update(r_origin, vpn, vright, vup);
	CL_DecayLights();
    } else
	S_Update(vec3_origin, vec3_origin, vec3_origin, vec3_origin);

    CDAudio_Update();

    if (host_speeds.value) {
	pass1 = (time1 - time3) * 1000;
	time3 = Sys_DoubleTime();
	pass2 = (time2 - time1) * 1000;
	pass3 = (time3 - time2) * 1000;
	Con_Printf("%3i tot %3i server %3i gfx %3i snd\n",
		   pass1 + pass2 + pass3, pass1, pass2, pass3);
    }

    host_framecount++;
    fps_count++;
}

//============================================================================

cvar_t host_framerate = { "host_framerate", "0", CVAR_NONE };
cvar_t host_timescale = { "host_timescale", "0", CVAR_NONE };
cvar_t devstats       = { "devstats",       "0", CVAR_NONE };

void Host_InitLocal(void)
{
    Cvar_RegisterVariable(&developer);
    Cvar_RegisterVariable(&host_framerate);
    Cvar_RegisterVariable(&host_speeds);
    Cvar_RegisterVariable(&host_timescale);
    Cvar_RegisterVariable(&devstats);
}

void Host_Init(void)
{
    if (standard_quake)
        minimum_memory = MINIMUM_MEMORY;
    else
        minimum_memory = MINIMUM_MEMORY_LEVELPAK;

    if (COM_CheckParm("-minmemory"))
        host_parms->memsize = minimum_memory;

    if (host_parms->memsize < minimum_memory)
        Sys_Error("Only %4.1f megs of memory available, can't execute game",
                  host_parms->memsize / (float)0x100000);

    com_argc = host_parms->argc;
    com_argv = host_parms->argv;

    Memory_Init(host_parms->membase, host_parms->memsize);
    Cbuf_Init();
    Cmd_Init();
    LOG_Init(host_parms);
    Cvar_Init();
    COM_Init();
    COM_InitFilesystem();

    // Register QuakeWorld client commands, cvars, and host cvars (developer, etc.)
    CL_AddCommands();
    CL_RegisterVariables();
    Host_InitLocal();

    W_LoadWadFile();
    Key_Init();
    Con_Init();
    Mod_Init();
    NET_Init(PORT_CLIENT);
    Netchan_Init();

    Con_Printf("Exe: " __TIME__ " " __DATE__ "\n");
    Con_Printf("%4.1f megabyte heap\n", host_parms->memsize / (1024 * 1024.0));

    host_colormap = (byte *)COM_LoadHunkFile("gfx/colormap.lmp", NULL);
    if (!host_colormap)
        Sys_Error("Couldn't load gfx/colormap.lmp");

    V_Init();
    M_Init();
// mrp    ExtraMaps_Init();
// mrp    Modlist_Init();
// mrp    DemoList_Init();
    VID_Init();
    IN_Init();
    TexMgr_Init();
    Draw_Init();
    SCR_Init();
    R_Init();
    S_Init();
    CDAudio_Init();
    BGM_Init();
    Sbar_Init();
    CL_Init();

    LOC_Init();

    Hunk_AllocName(0, "-HOST_HUNKLEVEL-");
    host_hunklevel = Hunk_LowMark();

    cls.state = ca_disconnected;
    host_initialized = true;
    Con_Printf("\n========= QuakeWorld Initialized =========\n\n");

    Cbuf_InsertText("exec quake.rc\n");
    Cbuf_AddText("\n\nvid_unlock\n");
    Cbuf_AddText("cl_warncmd 1\n");
}

/*
 * Host_Reinit - Called after Host_NewGame() to finish setting up the host, ready to spawn the level
 */
void
Host_Reinit()
{
    S_Init();
    CL_Reinit();

    Hunk_AllocName(0, "--HOST--");
    host_hunklevel = Hunk_LowMark();
}

void
Host_Gamedir(const char *directory, enum game_type game_type)
{
    if (!COM_CheckForGameDirectoryChange(directory, game_type))
        return;

    scr_disabled_for_loading = true;

    if (COM_Gamedir(directory, game_type))
    {
        Cbuf_AddText("exec quake.rc\n");
    }

    scr_disabled_for_loading = false;
}

/*
===============
Host_Shutdown

FIXME: this is a callback from Sys_Quit and Sys_Error.  It would be better
to run quit through here before the final handoff to the sys code.
===============
*/
void
Host_Shutdown(void)
{
    static qboolean isdown = false;

    if (isdown) {
	printf("recursive shutdown\n");
	return;
    }
    isdown = true;

    Host_WriteConfiguration();

    CDAudio_Shutdown();
    NET_Shutdown();
    S_Shutdown();
    IN_Shutdown();
    if (host_basepal)
	VID_Shutdown();
}
