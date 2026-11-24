/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers

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
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// view.c -- player eye positioning

#include "quakedef.h"
#include "ps2/engine_hooks.h" // [PS2_QUAKE]

/*

The view is allowed to move slightly from it's true position for bobbing,
but if it exceeds 8 pixels linear distance (spherical, not box), the list of
entities sent from the server may not include everything in the pvs, especially
when crossing a water boudnary.

*/

cvar_t	scr_ofsx = {"scr_ofsx","0", CVAR_NONE};
cvar_t	scr_ofsy = {"scr_ofsy","0", CVAR_NONE};
cvar_t	scr_ofsz = {"scr_ofsz","0", CVAR_NONE};

cvar_t	cl_rollspeed = {"cl_rollspeed", "200", CVAR_NONE};
cvar_t	cl_rollangle = {"cl_rollangle", "2.0", CVAR_NONE};

cvar_t	cl_bob = {"cl_bob","0.02", CVAR_NONE};
cvar_t	cl_bobcycle = {"cl_bobcycle","0.6", CVAR_NONE};
cvar_t	cl_bobup = {"cl_bobup","0.5", CVAR_NONE};

cvar_t	v_kicktime = {"v_kicktime", "0.5", CVAR_NONE};
cvar_t	v_kickroll = {"v_kickroll", "0.6", CVAR_NONE};
cvar_t	v_kickpitch = {"v_kickpitch", "0.6", CVAR_NONE};
cvar_t	v_gunkick = {"v_gunkick", "1", CVAR_NONE}; //johnfitz

cvar_t	v_iyaw_cycle = {"v_iyaw_cycle", "2", CVAR_NONE};
cvar_t	v_iroll_cycle = {"v_iroll_cycle", "0.5", CVAR_NONE};
cvar_t	v_ipitch_cycle = {"v_ipitch_cycle", "1", CVAR_NONE};
cvar_t	v_iyaw_level = {"v_iyaw_level", "0.3", CVAR_NONE};
cvar_t	v_iroll_level = {"v_iroll_level", "0.1", CVAR_NONE};
cvar_t	v_ipitch_level = {"v_ipitch_level", "0.3", CVAR_NONE};

cvar_t	v_idlescale = {"v_idlescale", "0", CVAR_NONE};

cvar_t	crosshair = {"crosshair", "0", CVAR_ARCHIVE};

cvar_t	gl_cshiftpercent = {"gl_cshiftpercent", "100", CVAR_NONE};
cvar_t	gl_cshiftpercent_contents = {"gl_cshiftpercent_contents", "100", CVAR_NONE}; // QuakeSpasm
cvar_t	gl_cshiftpercent_damage = {"gl_cshiftpercent_damage", "100", CVAR_NONE}; // QuakeSpasm
cvar_t	gl_cshiftpercent_bonus = {"gl_cshiftpercent_bonus", "100", CVAR_NONE}; // QuakeSpasm
cvar_t	gl_cshiftpercent_powerup = {"gl_cshiftpercent_powerup", "100", CVAR_NONE}; // QuakeSpasm

cvar_t	r_viewmodel_quake = {"r_viewmodel_quake", "0", CVAR_ARCHIVE};

vec3_t	v_punchangles[2]; //johnfitz -- copied from cl.punchangle.  0 is current, 1 is previous value. never the same unless map just loaded

player_state_t *view_message;
frame_t *view_frame;

/*
===============
V_CalcRoll

Used by view and sv_user
===============
*/
float V_CalcRoll (vec3_t angles, vec3_t velocity)
{
	vec3_t	forward, right, up;
	float	sign;
	float	side;
	float	value;

	AngleVectors (angles, forward, right, up);
	side = DotProduct (velocity, right);
	sign = side < 0 ? -1 : 1;
	side = PS2_Fabsf(side); // [PS2_QUAKE]: single precision (see mathlib.h)

	value = cl_rollangle.value;
//	if (cl.inwater)
//		value *= 6;

	if (side < cl_rollspeed.value)
		side = side * value / cl_rollspeed.value;
	else
		side = value;

	return side*sign;
}


/*
===============
V_CalcBob

===============
*/
float V_CalcBob (void)
{
	float	bob;
	float	cycle;

	if (!cl_bobcycle.value)
	{
		return 0.0f;
	}

	cycle = cl.time - (int)(cl.time/cl_bobcycle.value)*cl_bobcycle.value;
	cycle /= cl_bobcycle.value;
	if (cycle < cl_bobup.value)
	{
		cycle = M_PI * cycle / cl_bobup.value;
	}
	else
	{
		cycle = M_PI + M_PI*(cycle-cl_bobup.value)/(1.0 - cl_bobup.value);
	}
		
	// [PS2_QUAKE]: single precision (see mathlib.h)
	bob = PS2_Sqrtf(cl.simvel[0]*cl.simvel[0] + cl.simvel[1]*cl.simvel[1]) * cl_bob.value;
	bob = bob*0.3 + bob*0.7*PS2_Sinf(cycle);
	
	if (bob > 4)
	{
		bob = 4;
	}
	else if (bob < -7)
	{
		bob = -7;
	}
		
	return bob;
}

//=============================================================================


cvar_t	v_centermove = {"v_centermove", "0.15", CVAR_NONE};
cvar_t	v_centerspeed = {"v_centerspeed","500", CVAR_NONE};


void V_StartPitchDrift (void)
{
#if 1
	if (cl.laststop == cl.time)
	{
		return;		// something else is keeping it from drifting
	}
#endif
	if (cl.nodrift || !cl.pitchvel)
	{
		cl.pitchvel = v_centerspeed.value;
		cl.nodrift = false;
		cl.driftmove = 0;
	}
}

void V_StopPitchDrift (void)
{
	cl.laststop = cl.time;
	cl.nodrift = true;
	cl.pitchvel = 0;
}

/*
==============================================================================

	VIEW BLENDING

==============================================================================
*/

static cshift_t cshift_empty = { {130,80,50}, 0 };
static cshift_t cshift_water = { {130,80,50}, 128 };
static cshift_t cshift_slime = { {0,25,5}, 150 };
static cshift_t cshift_lava = { {255,80,0}, 150 };

static float v_dmg_time, v_dmg_roll, v_dmg_pitch;

float		v_blend[4];		// rgba 0.0 - 1.0

//johnfitz -- deleted BuildGammaTable(), V_CheckGamma(), gammatable[], and ramps[][]

/*
===============
V_ParseDamage
===============
*/
void V_ParseDamage (void)
{
	int		armor, blood;
	vec3_t	from;
	int		i;
	vec3_t	forward, right, up;
	float	side;
	float	count;

	armor = MSG_ReadByte ();
	blood = MSG_ReadByte ();
	for (i=0 ; i<3 ; i++)
	{
		from[i] = MSG_ReadCoord ();
	}
		
	PS2_RumbleDamage (armor, blood); // [PS2_QUAKE]: the pad's rumble

	count = blood*0.5 + armor*0.5;
	if (count < 10)
	{
		count = 10;
	}
		
	cl.faceanimtime = cl.time + 0.2;
	cl.cshifts[CSHIFT_DAMAGE].percent += 3*count;

	if (cl.cshifts[CSHIFT_DAMAGE].percent < 0)
	{
		cl.cshifts[CSHIFT_DAMAGE].percent = 0;
	}
		
	if (cl.cshifts[CSHIFT_DAMAGE].percent > 150)
	{
		cl.cshifts[CSHIFT_DAMAGE].percent = 150;
	}
		
	if (armor > blood)
	{
		cl.cshifts[CSHIFT_DAMAGE].destcolor[0] = 200;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[1] = 100;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[2] = 100;
	}
	else if (armor)
	{
		cl.cshifts[CSHIFT_DAMAGE].destcolor[0] = 220;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[1] = 50;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[2] = 50;
	}
	else
	{
		cl.cshifts[CSHIFT_DAMAGE].destcolor[0] = 255;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[1] = 0;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[2] = 0;
	}

	//
	// calculate view angle kicks
	//
	VectorSubtract (from, cl.simorg, from);
	VectorNormalize (from);

	AngleVectors (cl.simangles, forward, right, up);

	side = DotProduct (from, right);
	v_dmg_roll = count*side*v_kickroll.value;

	side = DotProduct (from, forward);
	v_dmg_pitch = count*side*v_kickpitch.value;

	v_dmg_time = v_kicktime.value;
}

/*
==================
V_cshift_f
==================
*/
void V_cshift_f (void)
{
	cshift_empty.destcolor[0] = atoi(Cmd_Argv(1));
	cshift_empty.destcolor[1] = atoi(Cmd_Argv(2));
	cshift_empty.destcolor[2] = atoi(Cmd_Argv(3));
	cshift_empty.percent = atoi(Cmd_Argv(4));
}


/*
==================
V_BonusFlash_f

When you run over an item, the server sends this command
==================
*/
void V_BonusFlash_f (void)
{
	PS2_RumblePickup (); // [PS2_QUAKE]: the pad's rumble

	cl.cshifts[CSHIFT_BONUS].destcolor[0] = 215;
	cl.cshifts[CSHIFT_BONUS].destcolor[1] = 186;
	cl.cshifts[CSHIFT_BONUS].destcolor[2] = 69;
	cl.cshifts[CSHIFT_BONUS].percent = 50;
}

/*
=============
V_SetContentsColor

Underwater, lava, etc each has a color shift
=============
*/
void V_SetContentsColor (int contents)
{
	switch (contents)
	{
	case CONTENTS_EMPTY:
	case CONTENTS_SOLID:
	case CONTENTS_SKY: //johnfitz -- no blend in sky
		cl.cshifts[CSHIFT_CONTENTS] = cshift_empty;
		break;
	case CONTENTS_LAVA:
		cl.cshifts[CSHIFT_CONTENTS] = cshift_lava;
		break;
	case CONTENTS_SLIME:
		cl.cshifts[CSHIFT_CONTENTS] = cshift_slime;
		break;
	default:
		cl.cshifts[CSHIFT_CONTENTS] = cshift_water;
	}
}

/*
=============
V_CalcPowerupCshift
=============
*/
void V_CalcPowerupCshift (void)
{
	if (cl.stats[STAT_ITEMS] & IT_QUAD)
	{
		cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 0;
		cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 0;
		cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 255;
		cl.cshifts[CSHIFT_POWERUP].percent = 30;
	}
	else if (cl.stats[STAT_ITEMS] & IT_SUIT)
	{
		cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 0;
		cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 255;
		cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 0;
		cl.cshifts[CSHIFT_POWERUP].percent = 20;
	}
	else if (cl.stats[STAT_ITEMS] & IT_INVISIBILITY)
	{
		cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 100;
		cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 100;
		cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 100;
		cl.cshifts[CSHIFT_POWERUP].percent = 100;
	}
	else if (cl.stats[STAT_ITEMS] & IT_INVULNERABILITY)
	{
		cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 255;
		cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 255;
		cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 0;
		cl.cshifts[CSHIFT_POWERUP].percent = 30;
	}
	else
	{
		cl.cshifts[CSHIFT_POWERUP].percent = 0;
	}
}

/*
=============
V_CalcBlend
=============
*/
void V_CalcBlend (void)
{
	float	r, g, b, a, a2;
	int		j;
	cvar_t	*cshiftpercent_cvars[NUM_CSHIFTS] = {
		&gl_cshiftpercent_contents,
		&gl_cshiftpercent_damage,
		&gl_cshiftpercent_bonus,
		&gl_cshiftpercent_powerup
	};

	r = 0;
	g = 0;
	b = 0;
	a = 0;

	for (j=0 ; j<NUM_CSHIFTS ; j++)
	{
		if (!gl_cshiftpercent.value)
			continue;

		//johnfitz -- only apply leaf contents color shifts during intermission
		if (cl.intermission && j != CSHIFT_CONTENTS)
			continue;
		//johnfitz

		a2 = ((cl.cshifts[j].percent * gl_cshiftpercent.value) / 100.0) / 255.0;
		// QuakeSpasm -- also scale by the specific gl_cshiftpercent_* cvar
		a2 *= (cshiftpercent_cvars[j]->value / 100.0);
		// QuakeSpasm
		if (!a2)
			continue;
		a = a + a2*(1-a);
		a2 = a2/a;
		r = r*(1-a2) + cl.cshifts[j].destcolor[0]*a2;
		g = g*(1-a2) + cl.cshifts[j].destcolor[1]*a2;
		b = b*(1-a2) + cl.cshifts[j].destcolor[2]*a2;
	}

	v_blend[0] = r/255.0;
	v_blend[1] = g/255.0;
	v_blend[2] = b/255.0;
	v_blend[3] = a;
	if (v_blend[3] > 1)
		v_blend[3] = 1;
	if (v_blend[3] < 0)
		v_blend[3] = 0;
}

/*
=============
V_UpdateBlend -- johnfitz -- V_UpdatePalette cleaned up and renamed
=============
*/
void V_UpdateBlend (void)
{
	int		i, j;
	qboolean	blend_changed;

	V_CalcPowerupCshift ();

	blend_changed = false;

	for (i=0 ; i<NUM_CSHIFTS ; i++)
	{
		if (cl.cshifts[i].percent != cl.prev_cshifts[i].percent)
		{
			blend_changed = true;
			cl.prev_cshifts[i].percent = cl.cshifts[i].percent;
		}
		for (j=0 ; j<3 ; j++)
			if (cl.cshifts[i].destcolor[j] != cl.prev_cshifts[i].destcolor[j])
			{
				blend_changed = true;
				cl.prev_cshifts[i].destcolor[j] = cl.cshifts[i].destcolor[j];
			}
	}

// drop the damage value
	cl.cshifts[CSHIFT_DAMAGE].percent -= host_frametime*150;
	if (cl.cshifts[CSHIFT_DAMAGE].percent <= 0)
		cl.cshifts[CSHIFT_DAMAGE].percent = 0;

// drop the bonus value
	cl.cshifts[CSHIFT_BONUS].percent -= host_frametime*100;
	if (cl.cshifts[CSHIFT_BONUS].percent <= 0)
		cl.cshifts[CSHIFT_BONUS].percent = 0;

	if (blend_changed)
		V_CalcBlend ();
}

/*
============
V_PolyBlend -- johnfitz -- moved here from gl_rmain.c, and rewritten to use glOrtho
============
*/
void V_PolyBlend (void)
{
	if (!gl_polyblend.value || !v_blend[3])
		return;

	PS2_DrawPolyBlend (v_blend); // [PS2_QUAKE]: was a GL full-screen quad
}

/*
==============================================================================

	VIEW RENDERING

==============================================================================
*/

float angledelta (float a)
{
	a = anglemod(a);
	if (a > 180)
		a -= 360;
	return a;
}

/*
==================
CalcGunAngle
==================
*/
void CalcGunAngle (void)
{
	float	yaw, pitch, move;
	static float oldyaw = 0;
	static float oldpitch = 0;

	yaw = r_refdef.viewangles[YAW];
	pitch = -r_refdef.viewangles[PITCH];

	yaw = angledelta(yaw - r_refdef.viewangles[YAW]) * 0.4;
	if (yaw > 10)
		yaw = 10;
	if (yaw < -10)
		yaw = -10;
	pitch = angledelta(-pitch - r_refdef.viewangles[PITCH]) * 0.4;
	if (pitch > 10)
		pitch = 10;
	if (pitch < -10)
		pitch = -10;
	move = host_frametime*20;
	if (yaw > oldyaw)
	{
		if (oldyaw + move < yaw)
			yaw = oldyaw + move;
	}
	else
	{
		if (oldyaw - move > yaw)
			yaw = oldyaw - move;
	}

	if (pitch > oldpitch)
	{
		if (oldpitch + move < pitch)
			pitch = oldpitch + move;
	}
	else
	{
		if (oldpitch - move > pitch)
			pitch = oldpitch - move;
	}

	oldyaw = yaw;
	oldpitch = pitch;

	cl.viewent.angles[YAW] = r_refdef.viewangles[YAW] + yaw;
	cl.viewent.angles[PITCH] = - (r_refdef.viewangles[PITCH] + pitch);

	// [PS2_QUAKE]: single precision (see mathlib.h), here and in V_AddIdle
	cl.viewent.angles[ROLL] -= v_idlescale.value * PS2_Sinf(cl.time*v_iroll_cycle.value) * v_iroll_level.value;
	cl.viewent.angles[PITCH] -= v_idlescale.value * PS2_Sinf(cl.time*v_ipitch_cycle.value) * v_ipitch_level.value;
	cl.viewent.angles[YAW] -= v_idlescale.value * PS2_Sinf(cl.time*v_iyaw_cycle.value) * v_iyaw_level.value;
}

/*
==============
V_BoundOffsets
==============
*/
void V_BoundOffsets (void)
{
	if (r_refdef.vieworg[0] < cl.simorg[0] - 14)
	{
		r_refdef.vieworg[0] = cl.simorg[0] - 14;
	}
	else if (r_refdef.vieworg[0] > cl.simorg[0] + 14)
	{
		r_refdef.vieworg[0] = cl.simorg[0] + 14;
	}
		
	if (r_refdef.vieworg[1] < cl.simorg[1] - 14)
	{
		r_refdef.vieworg[1] = cl.simorg[1] - 14;
	}
	else if (r_refdef.vieworg[1] > cl.simorg[1] + 14)
	{
		r_refdef.vieworg[1] = cl.simorg[1] + 14;
	}
		
	if (r_refdef.vieworg[2] < cl.simorg[2] - 22)
	{
		r_refdef.vieworg[2] = cl.simorg[2] - 22;
	}
	else if (r_refdef.vieworg[2] > cl.simorg[2] + 30)
	{
		r_refdef.vieworg[2] = cl.simorg[2] + 30;
	}
}

/*
==============
V_AddIdle

Idle swaying
==============
*/
void V_AddIdle (void)
{
	r_refdef.viewangles[ROLL] += v_idlescale.value * PS2_Sinf(cl.time*v_iroll_cycle.value) * v_iroll_level.value;
	r_refdef.viewangles[PITCH] += v_idlescale.value * PS2_Sinf(cl.time*v_ipitch_cycle.value) * v_ipitch_level.value;
	r_refdef.viewangles[YAW] += v_idlescale.value * PS2_Sinf(cl.time*v_iyaw_cycle.value) * v_iyaw_level.value;
}

/*
==============
V_CalcViewRoll

Roll is induced by movement and damage
==============
*/
void V_CalcViewRoll (void)
{
	float		side;

	side = V_CalcRoll (cl.simangles, cl.simvel);
	r_refdef.viewangles[ROLL] += side;

	if (v_dmg_time > 0)
	{
		r_refdef.viewangles[ROLL] += v_dmg_time/v_kicktime.value*v_dmg_roll;
		r_refdef.viewangles[PITCH] += v_dmg_time/v_kicktime.value*v_dmg_pitch;
		v_dmg_time -= host_frametime;
	}

	if (cl.stats[STAT_HEALTH] <= 0)
	{
		r_refdef.viewangles[ROLL] = 80;

		return;
	}
}

/*
==================
V_CalcIntermissionRefdef

==================
*/
void V_CalcIntermissionRefdef (void)
{
	entity_t	*view;
	float		old;

	// view is the weapon model (only visible from inside body)
	view = &cl.viewent;

	VectorCopy (cl.simorg, r_refdef.vieworg);
	VectorCopy (cl.simangles, r_refdef.viewangles);
	view->model = NULL;

	// allways idle in intermission
	old = v_idlescale.value;
	v_idlescale.value = 1;
	V_AddIdle ();
	v_idlescale.value = old;
}

void V_DriftPitch()
{
    float delta, move;

    if (!view_message->onground || cls.demoplayback) {
        cl.driftmove = 0;
        cl.pitchvel = 0;

        return;
    }

    // don't count small mouse motion
    if (cl.nodrift) {
        const int seq = cls.netchan.outgoing_sequence - 1;
        if (fabsf(cl.frames[seq & UPDATE_MASK].cmd.forwardmove) < 200.0f)
            cl.driftmove = 0;
        else
            cl.driftmove += host_frametime;

        if (cl.driftmove > v_centermove.value)
            if (lookspring.value)
                V_StartPitchDrift();

        return;
    }

    delta = 0 - cl.viewangles[PITCH];
    if (!delta) {
        cl.pitchvel = 0;
        return;
    }

    move = host_frametime * cl.pitchvel;
    cl.pitchvel += host_frametime * v_centerspeed.value;

    if (delta > 0) {
        if (move > delta) {
            cl.pitchvel = 0;
            move = delta;
        }
        cl.viewangles[PITCH] += move;
    } else if (delta < 0) {
        if (move > -delta) {
            cl.pitchvel = 0;
            move = -delta;
        }
        cl.viewangles[PITCH] -= move;
    }
}

/*
==================
V_CalcRefdef
==================
*/
void V_CalcRefdef (void)
{
	entity_t	*view;
	int			i;
	vec3_t		forward, right, up;
	float		bob;
	static float oldz = 0;

	/* Origin and angles for the player */
	vec_t *player_origin = cl.simorg;
	vec_t *player_angles = cl.simangles;

	V_DriftPitch ();

	// view is the weapon model (only visible from inside body)
	view = &cl.viewent;

	bob = V_CalcBob ();

	// refresh position from simulated origin
	VectorCopy (player_origin, r_refdef.vieworg);
	r_refdef.vieworg[2] += bob;

	// never let it sit exactly on a node line, because a water plane can
	// disappear when viewed with the eye exactly on it.
	// the server protocol only specifies to 1/8 pixel, so add 1/16 in each axis
	r_refdef.vieworg[0] += 1.0f / 16.0f;
	r_refdef.vieworg[1] += 1.0f / 16.0f;
	r_refdef.vieworg[2] += 1.0f / 16.0f;

	VectorCopy (player_angles, r_refdef.viewangles);
	V_CalcViewRoll ();
	V_AddIdle ();

	if (view_message->flags & PF_GIB)
	{
		r_refdef.vieworg[2] += 8;	// gib view height
	}
	else if (view_message->flags & PF_DEAD)
	{
		r_refdef.vieworg[2] -= 16;	// corpse view height
	}
	else
	{
		r_refdef.vieworg[2] += 22;	// view height
	}
		
	if (view_message->flags & PF_DEAD)	// PF_GIB will also set PF_DEAD
	{
		r_refdef.viewangles[ROLL] = 80;	// dead view angle
	}
		
	// offsets
	AngleVectors (player_angles, forward, right, up);

	V_BoundOffsets ();

	// set up gun position
	VectorCopy (player_angles, view->angles);

	CalcGunAngle ();

	VectorCopy (player_origin, view->origin);
	view->origin[2] += 22;

	for (i = 0; i < 3; i++)
	{
		view->origin[i] += forward[i] * bob * 0.4f;
	}
		
	view->origin[2] += bob;

	if (view_message->flags & (PF_GIB | PF_DEAD))
	{
		view->model = NULL;
	}
	else
	{
		view->model = cl.model_precache[cl.stats[STAT_WEAPON]];
	}
		
	view->frame = view_message->weaponframe;
	view->colormap = vid.colormap;
	view->scale = ENTSCALE_DEFAULT;

	// set up the refresh position
	if (v_gunkick.value)
	{
		r_refdef.viewangles[PITCH] += cl.punchangle;
	}
		
	// smooth out stair step ups
	if (view_message->onground && player_origin[2] - oldz > 0)
	{
		float steptime = host_frametime;

		oldz += steptime * 80;
		if (oldz > player_origin[2])
		{
			oldz = player_origin[2];
		}
			
		if (player_origin[2] - oldz > 12)
		{
			oldz = player_origin[2] - 12;
		}
			
		r_refdef.vieworg[2] += oldz - player_origin[2];
		view->origin[2] += oldz - player_origin[2];
	}
	else
	{
		oldz = player_origin[2];
	}
}

/*
=============
DropPunchAngle
=============
*/
void
DropPunchAngle(void)
{
    cl.punchangle -= 10 * host_frametime;
    if (cl.punchangle < 0)
	{
		cl.punchangle = 0;
	}
}

/*
==================
V_RenderView

The player's clipping box goes from (-16 -16 -24) to (16 16 32) from
the entity origin, so any view position inside that will be valid
==================
*/
extern vrect_t	scr_vrect;

void V_RenderView (void)
{
	cl.simangles[ROLL] = 0;

	if (con_forcedup || cls.state != ca_active)
	{
		return;
	}

	view_frame = &cl.frames[cls.netchan.incoming_sequence & UPDATE_MASK];
	view_message = &view_frame->playerstate[cl.playernum];

	DropPunchAngle ();

	if (cl.intermission)
	{
		V_CalcIntermissionRefdef ();
	}
	else
	{
		V_CalcRefdef ();
	}
		
	R_PushDlights ();
	R_RenderView ();

	V_PolyBlend (); //johnfitz -- moved here from R_Renderview ();
}

/*
==============================================================================

	INIT

==============================================================================
*/

/*
=============
V_Init
=============
*/
void V_Init (void)
{
	Cmd_AddCommand ("v_cshift", V_cshift_f);
	Cmd_AddCommand ("bf", V_BonusFlash_f);
	Cmd_AddCommand ("centerview", V_StartPitchDrift);

	Cvar_RegisterVariable (&v_centermove);
	Cvar_RegisterVariable (&v_centerspeed);

	Cvar_RegisterVariable (&v_iyaw_cycle);
	Cvar_RegisterVariable (&v_iroll_cycle);
	Cvar_RegisterVariable (&v_ipitch_cycle);
	Cvar_RegisterVariable (&v_iyaw_level);
	Cvar_RegisterVariable (&v_iroll_level);
	Cvar_RegisterVariable (&v_ipitch_level);

	Cvar_RegisterVariable (&v_idlescale);
	Cvar_RegisterVariable (&crosshair);
	Cvar_RegisterVariable (&gl_cshiftpercent);
	Cvar_RegisterVariable (&gl_cshiftpercent_contents); // QuakeSpasm
	Cvar_RegisterVariable (&gl_cshiftpercent_damage); // QuakeSpasm
	Cvar_RegisterVariable (&gl_cshiftpercent_bonus); // QuakeSpasm
	Cvar_RegisterVariable (&gl_cshiftpercent_powerup); // QuakeSpasm

	Cvar_RegisterVariable (&scr_ofsx);
	Cvar_RegisterVariable (&scr_ofsy);
	Cvar_RegisterVariable (&scr_ofsz);
	Cvar_RegisterVariable (&cl_rollspeed);
	Cvar_RegisterVariable (&cl_rollangle);
	Cvar_RegisterVariable (&cl_bob);
	Cvar_RegisterVariable (&cl_bobcycle);
	Cvar_RegisterVariable (&cl_bobup);

	Cvar_RegisterVariable (&v_kicktime);
	Cvar_RegisterVariable (&v_kickroll);
	Cvar_RegisterVariable (&v_kickpitch);
	Cvar_RegisterVariable (&v_gunkick); //johnfitz

	Cvar_RegisterVariable (&r_viewmodel_quake); //MarkV
}

