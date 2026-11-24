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
#ifdef SERVERONLY
#include "qwsvdef.h"
#else
#include "quakedef.h"
#endif

#include <string.h>
#include "console.h"
#include "model.h"
#include "pmove.h"
#include "sys.h"

/*
 * ===========================================================================
 * POINT / LINE TESTING IN HULLS
 * ===========================================================================
 */

typedef struct {
    hull_t hull;
    mplane_t planes[6];
} boxhull_t;

static const mclipnode_t box_clipnodes[6] = {
    { .planenum = 0, .children = { CONTENTS_EMPTY, 1 } },
    { .planenum = 1, .children = { 2, CONTENTS_EMPTY } },
    { .planenum = 2, .children = { CONTENTS_EMPTY, 3 } },
    { .planenum = 3, .children = { 4, CONTENTS_EMPTY } },
    { .planenum = 4, .children = { CONTENTS_EMPTY, 5 } },
    { .planenum = 5, .children = { CONTENTS_SOLID, CONTENTS_EMPTY } }
};

static const boxhull_t boxhull_template = {
    .hull = {
	.clipnodes = box_clipnodes,
	.firstclipnode = 0,
	.lastclipnode = 5
    },
    .planes = {
	{ .normal = { 1, 0, 0 }, .dist = 0, .type = 0 },
	{ .normal = { 1, 0, 0 }, .dist = 0, .type = 0 },
	{ .normal = { 0, 1, 0 }, .dist = 0, .type = 1 },
	{ .normal = { 0, 1, 0 }, .dist = 0, .type = 1 },
	{ .normal = { 0, 0, 1 }, .dist = 0, .type = 2 },
	{ .normal = { 0, 0, 1 }, .dist = 0, .type = 2 }
    }
};

/*
==================
Mod_HullPointContents
==================
*/
int
Mod_HullPointContents(const hull_t *hull, int nodenum, const vec3_t point)
{
    float dist;
    const mclipnode_t *node;
    const mplane_t *plane;

    while (nodenum >= 0) {
	if (nodenum < hull->firstclipnode || nodenum > hull->lastclipnode)
	    Sys_Error("%s: bad node number (%i)", __func__, nodenum);

	node = hull->clipnodes + nodenum;
	plane = hull->planes + node->planenum;

	if (plane->type < 3)
	    dist = point[plane->type] - plane->dist;
	else
	    dist = DotProduct(plane->normal, point) - plane->dist;
	if (dist < 0)
	    nodenum = node->children[1];
	else
	    nodenum = node->children[0];
    }

    return nodenum;
}

/*
==================
MOD_TraceHull
==================
*/
static qboolean
Mod_TraceHull_r(const hull_t *hull, int nodenum,
		const float p1f, const float p2f,
		const vec3_t p1, const vec3_t p2, trace_t *trace)
{
    const mclipnode_t *node;
    const mplane_t *plane;
    vec3_t mid;
    vec_t dist1, dist2, frac, midf;
    int i, child, side, contents;

    /* check for empty */
    if (nodenum < 0) {
	if (nodenum != CONTENTS_SOLID) {
	    trace->allsolid = false;
	    if (nodenum == CONTENTS_EMPTY)
		trace->inopen = true;
	    else
		trace->inwater = true;
	} else {
	    trace->startsolid = true;
	}
	return true;
    }

    if (nodenum < hull->firstclipnode || nodenum > hull->lastclipnode)
	Sys_Error("%s: bad node number", __func__);

    /* Find the point distances */
    node = hull->clipnodes + nodenum;
    plane = hull->planes + node->planenum;
    if (plane->type < 3) {
	dist1 = p1[plane->type] - plane->dist;
	dist2 = p2[plane->type] - plane->dist;
    } else {
	dist1 = DotProduct(plane->normal, p1) - plane->dist;
	dist2 = DotProduct(plane->normal, p2) - plane->dist;
    }

    if (dist1 >= 0 && dist2 >= 0) {
	child = node->children[0];
	return Mod_TraceHull_r(hull, child, p1f, p2f, p1, p2, trace);
    }
    if (dist1 < 0 && dist2 < 0) {
	child = node->children[1];
	return Mod_TraceHull_r(hull, child, p1f, p2f, p1, p2, trace);
    }

    /* Put the crosspoint DIST_EPSILON pixels on the near side */
    if (dist1 < 0)
	frac = (dist1 + DIST_EPSILON) / (dist1 - dist2);
    else
	frac = (dist1 - DIST_EPSILON) / (dist1 - dist2);
    if (frac < 0)
	frac = 0;
    if (frac > 1)
	frac = 1;

    midf = p1f + (p2f - p1f) * frac;
    for (i = 0; i < 3; i++)
	mid[i] = p1[i] + frac * (p2[i] - p1[i]);

    side = (dist1 < 0);

    /* move up to the node */
    child = node->children[side];
    if (!Mod_TraceHull_r(hull, child, p1f, midf, p1, mid, trace))
	return false;

    child = node->children[side ^ 1];
    if (Mod_HullPointContents(hull, child, mid) != CONTENTS_SOLID)
	/* Go past the node */
	return Mod_TraceHull_r(hull, child, midf, p2f, mid, p2, trace);

    /* Never got out of the solid area */
    if (trace->allsolid)
	return false;

    /* The other side of the node is solid, this is the impact point */
    if (!side) {
	VectorCopy(plane->normal, trace->plane.normal);
	trace->plane.dist = plane->dist;
    } else {
	VectorSubtract(vec3_origin, plane->normal, trace->plane.normal);
	trace->plane.dist = -plane->dist;
    }

    /* shouldn't really happen, but does occasionally */
    contents = Mod_HullPointContents(hull, hull->firstclipnode, mid);
    while (contents == CONTENTS_SOLID) {
	frac -= 0.1;
	if (frac < 0) {
	    trace->fraction = midf;
	    VectorCopy(mid, trace->endpos);
	    Con_DPrintf("backup past 0\n");
	    return false;
	}
	midf = p1f + (p2f - p1f) * frac;
	for (i = 0; i < 3; i++)
	    mid[i] = p1[i] + frac * (p2[i] - p1[i]);

	contents = Mod_HullPointContents(hull, hull->firstclipnode, mid);
    }

    trace->fraction = midf;
    VectorCopy(mid, trace->endpos);

    return false;
}

qboolean
Mod_TraceHull(const hull_t *hull, int nodenum,
	      const vec3_t p1, const vec3_t p2, trace_t *trace)
{
    return Mod_TraceHull_r(hull, nodenum, 0, 1, p1, p2, trace);
}

/*
===================
SV_CreateBoxHull

Set up the planes and clipnodes using the template so that the six floats
of a bounding box can just be stored out and get a proper hull_t structure.
===================
*/
void
Mod_CreateBoxhull(const vec3_t mins, const vec3_t maxs, boxhull_t *boxhull)
{
    memcpy(boxhull, &boxhull_template, sizeof(boxhull_template));

    boxhull->hull.planes = boxhull->planes;
    boxhull->planes[0].dist = maxs[0];
    boxhull->planes[1].dist = mins[0];
    boxhull->planes[2].dist = maxs[1];
    boxhull->planes[3].dist = mins[1];
    boxhull->planes[4].dist = maxs[2];
    boxhull->planes[5].dist = mins[2];
}

/*
==================
PM_PointContents

==================
*/
int
PM_PointContents(const vec3_t point, const physent_stack_t *pestack)
{
    const hull_t *hull;

    hull = &pestack->physents[0].brushmodel->hulls[0];

    return Mod_HullPointContents(hull, hull->firstclipnode, point);
}

/*
================
PM_TestPlayerPosition

Returns false if the given player position is not valid (in solid)
================
*/
qboolean
PM_TestPlayerPosition(const vec3_t pos, const physent_stack_t *pestack)
{
    vec3_t mins, maxs, test;
    const physent_t *physent;
    const hull_t *hull;
    boxhull_t boxhull;
    int i;

    physent = pestack->physents;
    for (i = 0; i < pestack->numphysent; i++, physent++) {
	/* get the clipping hull */
	if (physent->brushmodel)
	    hull = &physent->brushmodel->hulls[1];
	else {
	    VectorSubtract(physent->mins, player_maxs, mins);
	    VectorSubtract(physent->maxs, player_mins, maxs);
	    Mod_CreateBoxhull(mins, maxs, &boxhull);
	    hull = &boxhull.hull;
	}

	VectorSubtract(pos, physent->origin, test);
	if (Mod_HullPointContents(hull, hull->firstclipnode, test) ==
	    CONTENTS_SOLID)
	    return false;
    }

    return true;
}

/*
================
PM_PlayerMove
================
*/
const physent_t *
PM_PlayerMove(const vec3_t start, const vec3_t end,
	      const physent_stack_t *pestack, trace_t *trace)
{
    trace_t stacktrace;
    boxhull_t boxhull;
    vec3_t mins, maxs, start_l, end_l, offset;
    const physent_t *physent, *clipentity;
    const hull_t *hull;
    int i;

    /* fill in a default trace */
    memset(trace, 0, sizeof(*trace));
    trace->fraction = 1;
    VectorCopy(end, trace->endpos);
    clipentity = NULL;

    physent = pestack->physents;
    for (i = 0; i < pestack->numphysent; i++, physent++) {
	/* get the clipping hull */
	if (physent->brushmodel)
	    hull = &physent->brushmodel->hulls[1];
	else {
	    VectorSubtract(physent->mins, player_maxs, mins);
	    VectorSubtract(physent->maxs, player_mins, maxs);
	    Mod_CreateBoxhull(mins, maxs, &boxhull);
	    hull = &boxhull.hull;
	}

	VectorCopy(physent->origin, offset);
	VectorSubtract(start, offset, start_l);
	VectorSubtract(end, offset, end_l);

	/* fill in a default trace */
	memset(&stacktrace, 0, sizeof(stacktrace));
	stacktrace.fraction = 1;
	stacktrace.allsolid = true;
	VectorCopy(end, stacktrace.endpos);

	/* trace a line through the apropriate clipping hull */
	Mod_TraceHull(hull, hull->firstclipnode, start_l, end_l, &stacktrace);

	if (stacktrace.allsolid)
	    stacktrace.startsolid = true;
	if (stacktrace.startsolid)
	    stacktrace.fraction = 0;

	/* did we clip the move? */
	if (stacktrace.fraction < trace->fraction) {
	    /* fix trace up by the offset */
	    VectorAdd(stacktrace.endpos, offset, stacktrace.endpos);
	    *trace = stacktrace;
	    clipentity = physent;
	}
    }

    return clipentity;
}
