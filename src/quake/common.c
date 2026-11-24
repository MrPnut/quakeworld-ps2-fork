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

// common.c -- misc functions used in client and server

#include "quakedef.h"
#include "q_ctype.h"
#include "ps2/engine_hooks.h" // [PS2_QUAKE]: COM_FindFile names what it opens, for the frame log and the loading screen; the compressed packs
#include "ps2/fs/async_io_c.h" // [PS2_QUAKE]: PS2_AsyncIoDrain before a pack closes
#include "ps2/fs/readahead_stream.h" // [PS2_QUAKE]: demos stream through the loader
#include <fcntl.h>  // [PS2_QUAKE]: open, for COM_LocateFile
#include <unistd.h> // [PS2_QUAKE]: close
#include <errno.h>
#include <ctype.h>

static char	*largv[MAX_NUM_ARGVS + 1];
static char	argvdummy[] = " ";

int		safemode;

cvar_t	registered = {"registered","1",CVAR_ROM}; /* set to correct value in COM_CheckRegistered() */
cvar_t	cmdline = {"cmdline","",CVAR_ROM/*|CVAR_SERVERINFO*/}; /* sending cmdline upon CCREQ_RULE_INFO is evil */

static qboolean		com_modified;	// set true if using non-id files

qboolean		fitzmode;

static void COM_Path_f (void);

// if a packfile directory differs from this, it is assumed to be hacked
#define PAK0_COUNT		339	/* id1/pak0.pak - v1.0x */
#define PAK0_CRC_V100		13900	/* id1/pak0.pak - v1.00 */
#define PAK0_CRC_V101		62751	/* id1/pak0.pak - v1.01 */
#define PAK0_CRC_V106		32981	/* id1/pak0.pak - v1.06 */
#define PAK0_CRC	(PAK0_CRC_V106)
#define PAK0_COUNT_V091		308	/* id1/pak0.pak - v0.91/0.92, not supported */
#define PAK0_CRC_V091		28804	/* id1/pak0.pak - v0.91/0.92, not supported */

char	com_token[1024];
int		com_argc;
char	**com_argv;

unsigned int	com_fxrandseed = 1; // [PS2_QUAKE]: see COM_FxRand in common.h

#define CMDLINE_LENGTH	256		/* johnfitz -- mirrored in cmd.c */
char	com_cmdline[CMDLINE_LENGTH];

qboolean standard_quake = true, rogue, hipnotic;

// this graphic needs to be in the pak file to use registered features
static unsigned short pop[] =
{
	0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
	0x0000,0x0000,0x6600,0x0000,0x0000,0x0000,0x6600,0x0000,
	0x0000,0x0066,0x0000,0x0000,0x0000,0x0000,0x0067,0x0000,
	0x0000,0x6665,0x0000,0x0000,0x0000,0x0000,0x0065,0x6600,
	0x0063,0x6561,0x0000,0x0000,0x0000,0x0000,0x0061,0x6563,
	0x0064,0x6561,0x0000,0x0000,0x0000,0x0000,0x0061,0x6564,
	0x0064,0x6564,0x0000,0x6469,0x6969,0x6400,0x0064,0x6564,
	0x0063,0x6568,0x6200,0x0064,0x6864,0x0000,0x6268,0x6563,
	0x0000,0x6567,0x6963,0x0064,0x6764,0x0063,0x6967,0x6500,
	0x0000,0x6266,0x6769,0x6a68,0x6768,0x6a69,0x6766,0x6200,
	0x0000,0x0062,0x6566,0x6666,0x6666,0x6666,0x6562,0x0000,
	0x0000,0x0000,0x0062,0x6364,0x6664,0x6362,0x0000,0x0000,
	0x0000,0x0000,0x0000,0x0062,0x6662,0x0000,0x0000,0x0000,
	0x0000,0x0000,0x0000,0x0061,0x6661,0x0000,0x0000,0x0000,
	0x0000,0x0000,0x0000,0x0000,0x6500,0x0000,0x0000,0x0000,
	0x0000,0x0000,0x0000,0x0000,0x6400,0x0000,0x0000,0x0000
};

/*

All of Quake's data access is through a hierchal file system, but the contents
of the file system can be transparently merged from several sources.

The "base directory" is the path to the directory holding the quake.exe and all
game directories.  The sys_* files pass this to host_init in quakeparms_t->basedir.
This can be overridden with the "-basedir" command line parm to allow code
debugging in a different directory.  The base directory is only used during
filesystem initialization.

The "game directory" is the first tree on the search path and directory that all
generated files (savegames, screenshots, demos, config files) will be saved to.
This can be overridden with the "-game" command line parameter.  The game
directory can never be changed while quake is executing.  This is a precacution
against having a malicious server instruct clients to write files over areas they
shouldn't.

The "cache directory" is only used during development to save network bandwidth,
especially over ISDN / T1 lines.  If there is a cache directory specified, when
a file is found by the normal search path, it will be mirrored into the cache
directory, then opened there.

FIXME:
The file "parms.txt" will be read out of the game directory and appended to the
current command line arguments to allow different games to initialize startup
parms differently.  This could be used to add a "-sspeed 22050" for the high
quality sound edition.  Because they are added at the end, they will not
override an explicit setting on the original command line.

*/

//============================================================================


// ClearLink is used for new headnodes
void ClearLink (link_t *l)
{
	l->prev = l->next = l;
}

void RemoveLink (link_t *l)
{
	l->next->prev = l->prev;
	l->prev->next = l->next;
}

void InsertLinkBefore (link_t *l, link_t *before)
{
	l->next = before;
	l->prev = before->prev;
	l->prev->next = l;
	l->next->prev = l;
}

void InsertLinkAfter (link_t *l, link_t *after)
{
	l->next = after->next;
	l->prev = after;
	l->prev->next = l;
	l->next->prev = l;
}

/*
============================================================================

							DYNAMIC VECTORS

============================================================================
*/

void Vec_Grow (void **pvec, size_t element_size, size_t count)
{
	vec_header_t header;
	if (*pvec)
		header = VEC_HEADER(*pvec);
	else
		header.size = header.capacity = 0;

	if (header.size + count > header.capacity)
	{
		void *new_buffer;
		size_t total_size;

		header.capacity = header.size + count;
		header.capacity += header.capacity >> 1;
		if (header.capacity < 16)
			header.capacity = 16;
		total_size = sizeof(vec_header_t) + header.capacity * element_size;

		if (*pvec)
			new_buffer = realloc (((vec_header_t*)*pvec) - 1, total_size);
		else
			new_buffer = malloc (total_size);
		if (!new_buffer)
			Sys_Error ("Vec_Grow: failed to allocate %lu bytes\n", (unsigned long)total_size);

		*pvec = 1 + (vec_header_t*)new_buffer;
		VEC_HEADER(*pvec) = header;
	}
}

void Vec_Append (void **pvec, size_t element_size, const void *data, size_t count)
{
	if (!count)
		return;
	Vec_Grow (pvec, element_size, count);
	memcpy ((byte *)*pvec + VEC_HEADER(*pvec).size * element_size, data, count * element_size);
	VEC_HEADER(*pvec).size += count;
}

void Vec_Clear (void **pvec)
{
	if (*pvec)
		VEC_HEADER(*pvec).size = 0;
}

void Vec_Free (void **pvec)
{
	if (*pvec)
	{
		free(&VEC_HEADER(*pvec));
		*pvec = NULL;
	}
}

/*
============================================================================

					LIBRARY REPLACEMENT FUNCTIONS

============================================================================
*/

int q_strcasecmp(const char * s1, const char * s2)
{
	const char * p1 = s1;
	const char * p2 = s2;
	char c1, c2;

	if (p1 == p2)
		return 0;

	do
	{
		c1 = q_tolower (*p1++);
		c2 = q_tolower (*p2++);
		if (c1 == '\0')
			break;
	} while (c1 == c2);

	return (int)(c1 - c2);
}

int q_strncasecmp(const char *s1, const char *s2, size_t n)
{
	const char * p1 = s1;
	const char * p2 = s2;
	char c1, c2;

	if (p1 == p2 || n == 0)
		return 0;

	do
	{
		c1 = q_tolower (*p1++);
		c2 = q_tolower (*p2++);
		if (c1 == '\0' || c1 != c2)
			break;
	} while (--n > 0);

	return (int)(c1 - c2);
}

char *q_strcasestr(const char *haystack, const char *needle)
{
	const size_t len = strlen(needle);

	while (*haystack)
	{
		if (!q_strncasecmp(haystack, needle, len))
			return (char *)haystack;

		++haystack;
	}

	return NULL;
}

char *q_strlwr (char *str)
{
	char	*c;
	c = str;
	while (*c)
	{
		*c = q_tolower(*c);
		c++;
	}
	return str;
}

char *q_strupr (char *str)
{
	char	*c;
	c = str;
	while (*c)
	{
		*c = q_toupper(*c);
		c++;
	}
	return str;
}

char *q_strdup (const char *str)
{
	size_t len = strlen (str) + 1;
	char  *newstr = (char *)malloc (len);
	memcpy (newstr, str, len);
	return newstr;
}

/* platform dependant (v)snprintf function names: */
#if defined(_WIN32)
#define	snprintf_func		_snprintf
#define	vsnprintf_func		_vsnprintf
#else
#define	snprintf_func		snprintf
#define	vsnprintf_func		vsnprintf
#endif

int q_vsnprintf(char *str, size_t size, const char *format, va_list args)
{
	int		ret;

	ret = vsnprintf_func (str, size, format, args);

	if (ret < 0)
		ret = (int)size;
	if (size == 0)	/* no buffer */
		return ret;
	if ((size_t)ret >= size)
		str[size - 1] = '\0';

	return ret;
}

int q_snprintf (char *str, size_t size, const char *format, ...)
{
	int		ret;
	va_list		argptr;

	va_start (argptr, format);
	ret = q_vsnprintf (str, size, format, argptr);
	va_end (argptr);

	return ret;
}

void Q_memset (void *dest, int fill, size_t count)
{
	size_t		i;

	if ( (((uintptr_t)dest | count) & 3) == 0)
	{
		count >>= 2;
		fill = fill | (fill<<8) | (fill<<16) | (fill<<24);
		for (i = 0; i < count; i++)
			((int *)dest)[i] = fill;
	}
	else
		for (i = 0; i < count; i++)
			((byte *)dest)[i] = fill;
}

void Q_memcpy (void *dest, const void *src, size_t count)
{
	size_t		i;

	if (( ( (uintptr_t)dest | (uintptr_t)src | count) & 3) == 0)
	{
		count >>= 2;
		for (i = 0; i < count; i++)
			((int *)dest)[i] = ((int *)src)[i];
	}
	else
		for (i = 0; i < count; i++)
			((byte *)dest)[i] = ((byte *)src)[i];
}

int Q_memcmp (const void *m1, const void *m2, size_t count)
{
	while(count)
	{
		count--;
		if (((byte *)m1)[count] != ((byte *)m2)[count])
			return -1;
	}
	return 0;
}

void Q_strcpy (char *dest, const char *src)
{
	while (*src)
	{
		*dest++ = *src++;
	}
	*dest++ = 0;
}

void Q_strncpy (char *dest, const char *src, int count)
{
	while (*src && count--)
	{
		*dest++ = *src++;
	}
	if (count)
		*dest++ = 0;
}

int Q_strlen (const char *str)
{
	int		count;

	count = 0;
	while (str[count])
		count++;

	return count;
}

char *Q_strrchr(const char *s, char c)
{
	int len = Q_strlen(s);
	s += len;
	while (len--)
	{
		if (*--s == c)
			return (char *)s;
	}
	return NULL;
}

void Q_strcat (char *dest, const char *src)
{
	dest += Q_strlen(dest);
	Q_strcpy (dest, src);
}

int Q_strcmp (const char *s1, const char *s2)
{
	while (1)
	{
		if (*s1 != *s2)
			return -1;		// strings not equal
		if (!*s1)
			return 0;		// strings are equal
		s1++;
		s2++;
	}

	return -1;
}

int Q_strncmp (const char *s1, const char *s2, int count)
{
	while (1)
	{
		if (!count--)
			return 0;
		if (*s1 != *s2)
			return -1;		// strings not equal
		if (!*s1)
			return 0;		// strings are equal
		s1++;
		s2++;
	}

	return -1;
}

int Q_atoi (const char *str)
{
	int		val;
	int		sign;
	int		c;

	while (q_isspace (*str))
		++str;

	if (*str == '-')
	{
		sign = -1;
		str++;
	}
	else
		sign = 1;

	val = 0;

//
// check for hex
//
	if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X') )
	{
		str += 2;
		while (1)
		{
			c = *str++;
			if (c >= '0' && c <= '9')
				val = (val<<4) + c - '0';
			else if (c >= 'a' && c <= 'f')
				val = (val<<4) + c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				val = (val<<4) + c - 'A' + 10;
			else
				return val*sign;
		}
	}

//
// check for character
//
	if (str[0] == '\'')
	{
		return sign * str[1];
	}

//
// assume decimal
//
	while (1)
	{
		c = *str++;
		if (c <'0' || c > '9')
			return val*sign;
		val = val*10 + c - '0';
	}

	return 0;
}


float Q_atof (const char *str)
{
	double		val;
	int		sign;
	int		c;
	int	decimal, total;

	while (q_isspace (*str))
		++str;

	if (*str == '-')
	{
		sign = -1;
		str++;
	}
	else
		sign = 1;

	val = 0;

//
// check for hex
//
	if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X') )
	{
		str += 2;
		while (1)
		{
			c = *str++;
			if (c >= '0' && c <= '9')
				val = (val*16) + c - '0';
			else if (c >= 'a' && c <= 'f')
				val = (val*16) + c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				val = (val*16) + c - 'A' + 10;
			else
				return val*sign;
		}
	}

//
// check for character
//
	if (str[0] == '\'')
	{
		return sign * str[1];
	}

//
// assume decimal
//
	decimal = -1;
	total = 0;
	while (1)
	{
		c = *str++;
		if (c == '.')
		{
			decimal = total;
			continue;
		}
		if (c <'0' || c > '9')
			break;
		val = val*10 + c - '0';
		total++;
	}

	if (decimal == -1)
		return val*sign;
	while (total > decimal)
	{
		val /= 10;
		total--;
	}

	return val*sign;
}

/*
============================================================================

					BYTE ORDER FUNCTIONS

============================================================================
*/

qboolean	host_bigendian;

short	(*BigShort) (short l);
short	(*LittleShort) (short l);
int	(*BigLong) (int l);
int	(*LittleLong) (int l);
float	(*BigFloat) (float l);
float	(*LittleFloat) (float l);

short ShortSwap (short l)
{
	byte	b1, b2;

	b1 = l&255;
	b2 = (l>>8)&255;

	return (b1<<8) + b2;
}

short ShortNoSwap (short l)
{
	return l;
}

int LongSwap (int l)
{
	byte	b1, b2, b3, b4;

	b1 = l&255;
	b2 = (l>>8)&255;
	b3 = (l>>16)&255;
	b4 = (l>>24)&255;

	return ((int)b1<<24) + ((int)b2<<16) + ((int)b3<<8) + b4;
}

int LongNoSwap (int l)
{
	return l;
}

float FloatSwap (float f)
{
	union
	{
		float	f;
		byte	b[4];
	} dat1, dat2;


	dat1.f = f;
	dat2.b[0] = dat1.b[3];
	dat2.b[1] = dat1.b[2];
	dat2.b[2] = dat1.b[1];
	dat2.b[3] = dat1.b[0];
	return dat2.f;
}

float FloatNoSwap (float f)
{
	return f;
}

/*
==============================================================================

			MESSAGE IO FUNCTIONS

Handles byte ordering and avoids alignment errors
==============================================================================
*/

//
// writing functions
//

usercmd_t nullcmd;

void MSG_WriteChar (sizebuf_t *sb, int c)
{
	byte	*buf;

#ifdef PARANOID
	if (c < -128 || c > 127)
		Sys_Error ("MSG_WriteChar: range error");
#endif

	buf = (byte *) SZ_GetSpace (sb, 1);
	buf[0] = c;
}

void MSG_WriteByte (sizebuf_t *sb, int c)
{
	byte	*buf;

#ifdef PARANOID
	if (c < 0 || c > 255)
		Sys_Error ("MSG_WriteByte: range error");
#endif

	buf = (byte *) SZ_GetSpace (sb, 1);
	buf[0] = c;
}

void MSG_WriteShort (sizebuf_t *sb, int c)
{
	byte	*buf;

#ifdef PARANOID
	if (c < ((short)0x8000) || c > (short)0x7fff)
		Sys_Error ("MSG_WriteShort: range error");
#endif

	buf = (byte *) SZ_GetSpace (sb, 2);
	buf[0] = c&0xff;
	buf[1] = c>>8;
}

void MSG_WriteLong (sizebuf_t *sb, int c)
{
	byte	*buf;

	buf = (byte *) SZ_GetSpace (sb, 4);
	buf[0] = c&0xff;
	buf[1] = (c>>8)&0xff;
	buf[2] = (c>>16)&0xff;
	buf[3] = c>>24;
}

void MSG_WriteFloat (sizebuf_t *sb, float f)
{
	union
	{
		float	f;
		int	l;
	} dat;

	dat.f = f;
	dat.l = LittleLong (dat.l);

	SZ_Write (sb, &dat.l, 4);
}

void MSG_WriteString (sizebuf_t *sb, const char *s)
{
	if (!s)
		SZ_Write (sb, "", 1);
	else
		SZ_Write (sb, s, Q_strlen(s)+1);
}

void MSG_WriteStringf(sizebuf_t *sb, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    MSG_WriteStringvf(sb, fmt, ap);
    va_end(ap);
}

void MSG_WriteStringvf(sizebuf_t *sb, const char *fmt, va_list ap)
{
    int maxlen, len;

    /*
     * FIXME - Kind of ugly to check space first then do getspace
     * afterwards, but we don't know how much we'll need before
     * hand. Update the SZ interface?
     */
    maxlen = sb->maxsize - sb->cursize;
    len = qvsnprintf((char *)sb->data + sb->cursize, maxlen, fmt, ap);

    /* Use SZ_GetSpace to check for overflow */
    SZ_GetSpace(sb, len + 1);
}

//johnfitz -- original behavior, 13.3 fixed point coords, max range +-4096
void MSG_WriteCoord16 (sizebuf_t *sb, float f)
{
	MSG_WriteShort (sb, Q_rint(f*8));
}

//johnfitz -- 16.8 fixed point coords, max range +-32768
void MSG_WriteCoord24 (sizebuf_t *sb, float f)
{
	MSG_WriteShort (sb, f);
	MSG_WriteByte (sb, (int)(f*255)%255);
}

//johnfitz -- 32-bit float coords
void MSG_WriteCoord32f (sizebuf_t *sb, float f)
{
	MSG_WriteFloat (sb, f);
}

void MSG_WriteCoord(sizebuf_t *sb, float f)
{
    /*
     * Co-ords are send as shorts, with the low 3 bits being the fractional
     * component
     */
    MSG_WriteShort(sb, (int)(f * (1 << 3)));
}

void MSG_WriteAngle(sizebuf_t *sb, float f)
{
    MSG_WriteByte(sb, (int)floorf((f * 256 / 360) + 0.5f) & 255);
}

void MSG_WriteAngle16(sizebuf_t *sb, float f)
{
    MSG_WriteShort(sb, (int)floorf((f * 65536 / 360) + 0.5f) & 65535);
}

//
// reading functions
//
int		msg_readcount;
qboolean	msg_badread;

void MSG_BeginReading (void)
{
	msg_readcount = 0;
	msg_badread = false;
}

// returns -1 and sets msg_badread if no more characters are available
int MSG_ReadChar (void)
{
	int	c;

	if (msg_readcount+1 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (signed char)net_message.data[msg_readcount];
	msg_readcount++;

	return c;
}

int MSG_ReadByte (void)
{
	int	c;

	if (msg_readcount+1 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (unsigned char)net_message.data[msg_readcount];
	msg_readcount++;

	return c;
}

int MSG_ReadShort (void)
{
	int	c;

	if (msg_readcount+2 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (short)(net_message.data[msg_readcount]
			+ (net_message.data[msg_readcount+1]<<8));

	msg_readcount += 2;

	return c;
}

int MSG_ReadLong (void)
{
	int	c;

	if (msg_readcount+4 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = net_message.data[msg_readcount]
			+ (net_message.data[msg_readcount+1]<<8)
			+ (net_message.data[msg_readcount+2]<<16)
			+ (net_message.data[msg_readcount+3]<<24);

	msg_readcount += 4;

	return c;
}

float MSG_ReadFloat (void)
{
	union
	{
		byte	b[4];
		float	f;
		int	l;
	} dat;

	dat.b[0] = net_message.data[msg_readcount];
	dat.b[1] = net_message.data[msg_readcount+1];
	dat.b[2] = net_message.data[msg_readcount+2];
	dat.b[3] = net_message.data[msg_readcount+3];
	msg_readcount += 4;

	dat.l = LittleLong (dat.l);

	return dat.f;
}

const char *MSG_ReadString (void)
{
	static char	string[2048];
	int		c;
	size_t		l;

	l = 0;
	do
	{
		c = MSG_ReadByte ();
		if (c == -1 || c == 0)
			break;
		string[l] = c;
		l++;
	} while (l < sizeof(string) - 1);

	string[l] = 0;

	return string;
}

//johnfitz -- original behavior, 13.3 fixed point coords, max range +-4096
float MSG_ReadCoord16 (void)
{
	return MSG_ReadShort() * (1.0/8);
}

//johnfitz -- 16.8 fixed point coords, max range +-32768
float MSG_ReadCoord24 (void)
{
	return MSG_ReadShort() + MSG_ReadByte() * (1.0/255);
}

//johnfitz -- 32-bit float coords
float MSG_ReadCoord32f (void)
{
	return MSG_ReadFloat();
}

float MSG_ReadCoord(void)
{
    return MSG_ReadShort() * (1.0 / (1 << 3));
}

float MSG_ReadAngle(void)
{
    return MSG_ReadChar() * (360.0 / 256);
}

float MSG_ReadAngle16(void)
{
    return MSG_ReadShort() * (360.0 / 65536);
}

void MSG_ReadDeltaUsercmd(const usercmd_t *from, usercmd_t *move)
{
    int bits;

    memcpy(move, from, sizeof(*move));

    bits = MSG_ReadByte();

// read current angles
    if (bits & CM_ANGLE1)
	move->angles[0] = MSG_ReadAngle16();
    if (bits & CM_ANGLE2)
	move->angles[1] = MSG_ReadAngle16();
    if (bits & CM_ANGLE3)
	move->angles[2] = MSG_ReadAngle16();

// read movement
    if (bits & CM_FORWARD)
	move->forwardmove = MSG_ReadShort();
    if (bits & CM_SIDE)
	move->sidemove = MSG_ReadShort();
    if (bits & CM_UP)
	move->upmove = MSG_ReadShort();

// read buttons
    if (bits & CM_BUTTONS)
	move->buttons = MSG_ReadByte();

    if (bits & CM_IMPULSE)
	move->impulse = MSG_ReadByte();

// read time to run command
    move->msec = MSG_ReadByte();
}

void MSG_WriteDeltaUsercmd(sizebuf_t *buf, const usercmd_t *from, const usercmd_t *cmd)
{
    int bits;

//
// send the movement message
//
    bits = 0;
    if (cmd->angles[0] != from->angles[0])
	bits |= CM_ANGLE1;
    if (cmd->angles[1] != from->angles[1])
	bits |= CM_ANGLE2;
    if (cmd->angles[2] != from->angles[2])
	bits |= CM_ANGLE3;
    if (cmd->forwardmove != from->forwardmove)
	bits |= CM_FORWARD;
    if (cmd->sidemove != from->sidemove)
	bits |= CM_SIDE;
    if (cmd->upmove != from->upmove)
	bits |= CM_UP;
    if (cmd->buttons != from->buttons)
	bits |= CM_BUTTONS;
    if (cmd->impulse != from->impulse)
	bits |= CM_IMPULSE;

    MSG_WriteByte(buf, bits);

    if (bits & CM_ANGLE1)
	MSG_WriteAngle16(buf, cmd->angles[0]);
    if (bits & CM_ANGLE2)
	MSG_WriteAngle16(buf, cmd->angles[1]);
    if (bits & CM_ANGLE3)
	MSG_WriteAngle16(buf, cmd->angles[2]);

    if (bits & CM_FORWARD)
	MSG_WriteShort(buf, cmd->forwardmove);
    if (bits & CM_SIDE)
	MSG_WriteShort(buf, cmd->sidemove);
    if (bits & CM_UP)
	MSG_WriteShort(buf, cmd->upmove);

    if (bits & CM_BUTTONS)
	MSG_WriteByte(buf, cmd->buttons);
    if (bits & CM_IMPULSE)
	MSG_WriteByte(buf, cmd->impulse);
    MSG_WriteByte(buf, cmd->msec);
}

//===========================================================================

void SZ_Alloc (sizebuf_t *buf, int startsize)
{
	if (startsize < 256)
		startsize = 256;
	buf->data = (byte *) Hunk_AllocName (startsize, "sizebuf");
	buf->maxsize = startsize;
	buf->cursize = 0;
}


void SZ_Free (sizebuf_t *buf)
{
//	Z_Free (buf->data);
//	buf->data = NULL;
//	buf->maxsize = 0;
	buf->cursize = 0;
}

void SZ_Clear (sizebuf_t *buf)
{
	buf->cursize = 0;
}

void *SZ_GetSpace (sizebuf_t *buf, int length)
{
	void	*data;

	if (buf->cursize + length > buf->maxsize)
	{
		if (!buf->allowoverflow)
			Host_Error ("SZ_GetSpace: overflow without allowoverflow set"); // ericw -- made Host_Error to be less annoying

		if (length > buf->maxsize)
			Sys_Error ("SZ_GetSpace: %i is > full buffer size", length);

		buf->overflowed = true;
		Con_Printf ("SZ_GetSpace: overflow\n");
		SZ_Clear (buf);
	}

	data = buf->data + buf->cursize;
	buf->cursize += length;

	return data;
}

void SZ_Write (sizebuf_t *buf, const void *data, int length)
{
	Q_memcpy (SZ_GetSpace(buf,length),data,length);
}

void SZ_Print (sizebuf_t *buf, const char *data)
{
	int		len = Q_strlen(data) + 1;

	if (buf->data[buf->cursize-1])
	{	/* no trailing 0 */
		Q_memcpy ((byte *)SZ_GetSpace(buf, len  )  , data, len);
	}
	else
	{	/* write over trailing 0 */
		Q_memcpy ((byte *)SZ_GetSpace(buf, len-1)-1, data, len);
	}
}


//============================================================================

/*
============
COM_SkipPath
============
*/
const char *COM_SkipPath (const char *pathname)
{
	const char	*last;

	last = pathname;
	while (*pathname)
	{
		if (*pathname == '/')
			last = pathname + 1;
		pathname++;
	}
	return last;
}

/*
============
COM_StripExtension
============
*/
void COM_StripExtension (const char *in, char *out, size_t outsize)
{
	int	length;

	if (!*in)
	{
		*out = '\0';
		return;
	}
	if (in != out)	/* copy when not in-place editing */
		q_strlcpy (out, in, outsize);
	length = (int)strlen(out) - 1;
	while (length > 0 && out[length] != '.')
	{
		--length;
		if (out[length] == '/' || out[length] == '\\')
			return;	/* no extension */
	}
	if (length > 0)
		out[length] = '\0';
}

/*
============
COM_FileGetExtension - doesn't return NULL
============
*/
const char *COM_FileGetExtension (const char *in)
{
	const char	*src;
	size_t		len;

	len = strlen(in);
	if (len < 2)	/* nothing meaningful */
		return "";

	src = in + len - 1;
	while (src != in && src[-1] != '.')
		src--;
	if (src == in || strchr(src, '/') != NULL || strchr(src, '\\') != NULL)
		return "";	/* no extension, or parent directory has a dot */

	return src;
}

/*
============
COM_ExtractExtension
============
*/
void COM_ExtractExtension (const char *in, char *out, size_t outsize)
{
	const char *ext = COM_FileGetExtension (in);
	if (! *ext)
		*out = '\0';
	else
		q_strlcpy (out, ext, outsize);
}

/*
============
COM_FileBase
take 'somedir/otherdir/filename.ext',
write only 'filename' to the output
============
*/
void COM_FileBase (const char *in, char *out, size_t outsize)
{
	const char	*dot, *slash, *s;

	s = in;
	slash = in;
	dot = NULL;
	while (*s)
	{
		if (*s == '/' || *s == '\\')
			slash = s + 1;
		if (*s == '.')
			dot = s;
		s++;
	}
	if (dot == NULL)
		dot = s;

	if (dot - slash < 2)
		q_strlcpy (out, "?model?", outsize);
	else
	{
		size_t	len = dot - slash;
		if (len >= outsize)
			len = outsize - 1;
		memcpy (out, slash, len);
		out[len] = '\0';
	}
}

/*
==================
COM_DefaultExtension
if path doesn't have a .EXT, append extension
(extension should include the leading ".")
==================
*/
#if 0 /* can be dangerous */
void COM_DefaultExtension (char *path, const char *extension, size_t len)
{
	char	*src;

	if (!*path) return;
	src = path + strlen(path) - 1;

	while (*src != '/' && *src != '\\' && src != path)
	{
		if (*src == '.')
			return; // it has an extension
		src--;
	}

	q_strlcat(path, extension, len);
}
#endif

/*
==================
COM_AddExtension
if path extension doesn't match .EXT, append it
(extension should include the leading ".")
==================
*/
void COM_AddExtension (char *path, const char *extension, size_t len)
{
	if (strcmp(COM_FileGetExtension(path), extension + 1) != 0)
		q_strlcat(path, extension, len);
}

/*
==================
COM_DefaultExtension
Returns non-zero if the extension wouldn't fit in the output buffer
==================
*/
int COM_DefaultExtension(const char *path, const char *extension, char *out, size_t buflen)
{
    COM_StripExtension(path, out, buflen);
    if (strlen(out) + strlen(extension) + 1 > buflen)
	{
		return -1;
	}
	
    memcpy(out + strlen(out), extension, strlen(extension) + 1);

    return 0;
}

/*
==============
COM_ParseEx

Parse a token out of a string

The mode argument controls how overflow is handled:
- CPE_NOTRUNC:		return NULL (abort parsing)
- CPE_ALLOWTRUNC:	truncate com_token (ignore the extra characters in this token)
==============
*/
const char *COM_ParseEx (const char *data, cpe_mode mode)
{
	int		c;
	int		len;

	len = 0;
	com_token[0] = 0;

	if (!data)
		return NULL;

// skip whitespace
skipwhite:
	while ((c = *data) <= ' ')
	{
		if (c == 0)
			return NULL;	// end of file
		data++;
	}

// skip // comments
	if (c == '/' && data[1] == '/')
	{
		while (*data && *data != '\n')
			data++;
		goto skipwhite;
	}

// skip /*..*/ comments
	if (c == '/' && data[1] == '*')
	{
		data += 2;
		while (*data && !(*data == '*' && data[1] == '/'))
			data++;
		if (*data)
			data += 2;
		goto skipwhite;
	}

// handle quoted strings specially
	if (c == '\"')
	{
		data++;
		while (1)
		{
			if ((c = *data) != 0)
				++data;
			if (c == '\"' || !c)
			{
				com_token[len] = 0;
				return data;
			}
			if (len < Q_COUNTOF(com_token) - 1)
				com_token[len++] = c;
			else if (mode == CPE_NOTRUNC)
				return NULL;
		}
	}

// parse single characters
	if (c == '{' || c == '}'|| c == '('|| c == ')' || c == '\'' || c == ':')
	{
		if (len < Q_COUNTOF(com_token) - 1)
			com_token[len++] = c;
		else if (mode == CPE_NOTRUNC)
			return NULL;
		com_token[len] = 0;
		return data+1;
	}

// parse a regular word
	do
	{
		if (len < Q_COUNTOF(com_token) - 1)
			com_token[len++] = c;
		else if (mode == CPE_NOTRUNC)
			return NULL;
		data++;
		c = *data;
		/* commented out the check for ':' so that ip:port works */
		if (c == '{' || c == '}'|| c == '('|| c == ')' || c == '\''/* || c == ':' */)
			break;
	} while (c > 32);

	com_token[len] = 0;
	return data;
}


/*
==============
COM_Parse

Parse a token out of a string

Return NULL in case of overflow
==============
*/
const char *COM_Parse (const char *data)
{
	return COM_ParseEx (data, CPE_NOTRUNC);
}


/*
================
COM_CheckParm

Returns the position (1 to argc-1) in the program's argument list
where the given parameter apears, or 0 if not present
================
*/
int COM_CheckParm (const char *parm)
{
	int		i;

	for (i = 1; i < com_argc; i++)
	{
		if (!com_argv[i])
			continue;		// NEXTSTEP sometimes clears appkit vars.
		if (!Q_strcmp (parm,com_argv[i]))
			return i;
	}

	return 0;
}

/*
================
COM_CheckRegistered

Looks for the pop.txt file and verifies it.
Sets the "registered" cvar.
Immediately exits out if an alternate game was attempted to be started without
being registered.
================
*/
static void COM_CheckRegistered (void)
{
	int		h;
	unsigned short	check[128];
	int		i;

	COM_OpenFile("gfx/pop.lmp", &h, NULL);

	if (h == -1)
	{
		Cvar_SetROM ("registered", "0");
		Con_Printf ("Playing shareware version.\n");
		if (com_modified)
			Sys_Error ("You must have the registered version to use modified games.\n\n"
				   "Basedir is: %s\n\n"
				   "Check that this has an " GAMENAME " subdirectory containing pak0.pak and pak1.pak, "
				   "or use the -basedir command-line option to specify another directory.",
				   com_basedir);
		return;
	}

	i = Sys_FileRead (h, check, sizeof(check));
	COM_CloseFile (h);
	if (i != (int) sizeof(check))
		goto corrupt;

	for (i = 0; i < 128; i++)
	{
		if (pop[i] != (unsigned short)BigShort (check[i]))
		{ corrupt:
			Sys_Error ("Corrupted data file.");
		}
	}

	for (i = 0; com_cmdline[i]; i++)
	{
		if (com_cmdline[i]!= ' ')
			break;
	}

	Cvar_SetROM ("cmdline", &com_cmdline[i]);
	Cvar_SetROM ("registered", "1");
	Con_Printf ("Playing registered version.\n");
}


/*
================
COM_InitArgv
================
*/
void COM_InitArgv (int argc, char **argv)
{
	int		i, j, n;

// reconstitute the command line for the cmdline externally visible cvar
	n = 0;

	for (j = 0; (j<MAX_NUM_ARGVS) && (j< argc); j++)
	{
		i = 0;

		while ((n < (CMDLINE_LENGTH - 1)) && argv[j][i])
		{
			com_cmdline[n++] = argv[j][i++];
		}

		if (n < (CMDLINE_LENGTH - 1))
			com_cmdline[n++] = ' ';
		else
			break;
	}

	if (n > 0 && com_cmdline[n-1] == ' ')
		com_cmdline[n-1] = 0; //johnfitz -- kill the trailing space

	Con_Printf("Command line: %s\n", com_cmdline);

	for (com_argc = 0; (com_argc < MAX_NUM_ARGVS) && (com_argc < argc); com_argc++)
	{
		largv[com_argc] = argv[com_argc];
		if (!Q_strcmp ("-safe", argv[com_argc]))
			safemode = 1;
	}

	largv[com_argc] = argvdummy;
	com_argv = largv;

	if (COM_CheckParm ("-rogue"))
	{
		rogue = true;
		standard_quake = false;
	}

	if (COM_CheckParm ("-hipnotic") || COM_CheckParm ("-quoth")) //johnfitz -- "-quoth" support
	{
		hipnotic = true;
		standard_quake = false;
	}
}

/*
================
COM_WordLength
================
*/
int COM_WordLength (const char *text)
{
	const char *start = text;
	while (*text && !q_isspace (*text))
		text++;
	return text - start;
}

/*
================
COM_AdvanceLineWrapped

Advances text by as much as possible until the maxchars limit is hit,
avoiding splitting words if possible.

Returns the length of the consumed text, excluding a potential trailing space or newline.
================
*/
int COM_AdvanceLineWrapped (const char **text, int maxchars)
{
	const char *str = *text;
	int i;

	for (i = 0; i < maxchars && str[i]; /**/)
	{
		if (str[i] == '\n')
		{
			*text += i + 1;
			return i;
		}

		// new word
		if (!q_isspace (str[i]) && (i == 0 || q_isspace (str[i - 1])))
		{
			int len = COM_WordLength (str + i);
			// split word if longer than given limit
			if (len > maxchars)
			{
				*text += maxchars;
				return maxchars;
			}
			// not enough space left? push word to next line
			if (i + len > maxchars)
			{
				*text += i;
				return i;
			}
			// word fits, continue
			i += len;
		}
		else
			i++;
	}

	// avoid starting next line with a space
	*text += i + (q_isspace (str[i]) ? 1 : 0);

	return i;
}

/*
================
COM_WordWrap

Copies src to dst by word-wrapping lines longer than maxcols, preserving existing linefeeds.
If maxcols <= 0 no wrapping is performed (plain string copy).
dst is always NUL terminated if dstsize > 0.
================
*/
void COM_WordWrap (char *dst, const char *src, size_t dstsize, int maxcols)
{
	size_t ofs;

	if (maxcols <= 0)
	{
		q_strlcpy (dst, src, dstsize);
		return;
	}

	if (!dstsize)
		return;
	// reserve space for terminating NUL
	--dstsize;

	ofs = 0;
	while (*src)
	{
		const char *start = src;
		size_t len = (size_t) COM_AdvanceLineWrapped (&src, maxcols);
		size_t remaining = dstsize - ofs;
		len = q_min (len, remaining);
		memcpy (dst + ofs, start, len);
		ofs += len;
		if (ofs + 1 < dstsize && *src)
			dst[ofs++] = '\n';
	}

	dst[ofs++] = '\0';
}

/*
================
COM_Init
================
*/
void COM_Init (void)
{
	int	i = 0x12345678;
		/*    U N I X */

	/*
	BE_ORDER:  12 34 56 78
		   U  N  I  X

	LE_ORDER:  78 56 34 12
		   X  I  N  U

	PDP_ORDER: 34 12 78 56
		   N  U  X  I
	*/
	if ( *(char *)&i == 0x12 )
		host_bigendian = true;
	else if ( *(char *)&i == 0x78 )
		host_bigendian = false;
	else /* if ( *(char *)&i == 0x34 ) */
		Sys_Error ("Unsupported endianism.");

	if (host_bigendian)
	{
		BigShort = ShortNoSwap;
		LittleShort = ShortSwap;
		BigLong = LongNoSwap;
		LittleLong = LongSwap;
		BigFloat = FloatNoSwap;
		LittleFloat = FloatSwap;
	}
	else /* assumed LITTLE_ENDIAN. */
	{
		BigShort = ShortSwap;
		LittleShort = ShortNoSwap;
		BigLong = LongSwap;
		LittleLong = LongNoSwap;
		BigFloat = FloatSwap;
		LittleFloat = FloatNoSwap;
	}

	if (COM_CheckParm("-fitz"))
		fitzmode = true;
}


/*
============
va

does a varargs printf into a temp buffer. cycles between
4 different static buffers. the number of buffers cycled
is defined in VA_NUM_BUFFS.
FIXME: make this buffer size safe someday
============
*/
#define	VA_NUM_BUFFS	4
#if (MAX_OSPATH >= 1024)
#define	VA_BUFFERLEN	MAX_OSPATH
#else
#define	VA_BUFFERLEN	1024
#endif

static char *get_va_buffer(void)
{
	static char va_buffers[VA_NUM_BUFFS][VA_BUFFERLEN];
	static int buffer_idx = 0;
	buffer_idx = (buffer_idx + 1) & (VA_NUM_BUFFS - 1);
	return va_buffers[buffer_idx];
}

char *va (const char *format, ...)
{
	va_list		argptr;
	char		*va_buf;

	va_buf = get_va_buffer ();
	va_start (argptr, format);
	q_vsnprintf (va_buf, VA_BUFFERLEN, format, argptr);
	va_end (argptr);

	return va_buf;
}

/*
=============================================================================

QUAKE FILESYSTEM

=============================================================================
*/

int	com_filesize;


//
// on-disk pakfile
//
typedef struct
{
	char	name[56];
	int		filepos, filelen;
} dpackfile_t;

typedef struct
{
	char	id[4];
	int		dirofs;
	int		dirlen;
} dpackheader_t;

#define MAX_FILES_IN_PACK	2048

char	com_gamedir[MAX_OSPATH];
char    com_gamedirfile[MAX_OSPATH];
char	com_basedir[MAX_OSPATH];
int	file_from_pak;		// ZOID: global indicating that file came from a pak

searchpath_t	*com_searchpaths;
searchpath_t	*com_base_searchpaths;

/*
============
COM_Path_f
============
*/
static void COM_Path_f (void)
{
	searchpath_t	*s;

	Con_Printf ("Current search path:\n");
	for (s = com_searchpaths; s; s = s->next)
	{
		if (s->pack)
		{
			Con_Printf ("%s (%i files)\n", s->pack->filename, s->pack->numfiles);
		}
		else
			Con_Printf ("%s\n", s->filename);
	}
}

/*
============
COM_WriteFile

The filename will be prefixed by the current game directory
============
*/
void COM_WriteFile (const char *filename, const void *data, int len)
{
	int		handle;
	char	name[MAX_OSPATH];

	Sys_mkdir (com_gamedir); //johnfitz -- if we've switched to a nonexistant gamedir, create it now so we don't crash

	q_snprintf (name, sizeof(name), "%s/%s", com_gamedir, filename);

	handle = Sys_FileOpenWrite (name);
	if (handle == -1)
	{
		Sys_Printf ("COM_WriteFile: failed on %s\n", name);
		return;
	}

	Sys_Printf ("COM_WriteFile: %s\n", name);
	Sys_FileWrite (handle, data, len);
	Sys_FileClose (handle);
}

/*
============
COM_CreatePath
============
*/
void COM_CreatePath (char *path)
{
	char	*ofs;

	for (ofs = path + 1; *ofs; ofs++)
	{
		if (*ofs == '/')
		{	// create the directory
			*ofs = 0;
			Sys_mkdir (path);
			*ofs = '/';
		}
	}
}

/*
================
COM_filelength
================
*/
long COM_filelength (FILE *f)
{
	long		pos, end;

	pos = ftell (f);
	fseek (f, 0, SEEK_END);
	end = ftell (f);
	fseek (f, pos, SEEK_SET);

	return end;
}

/*
===========
COM_FindFile

Finds the file in the search path.
Sets com_filesize and one of handle or file
If neither of file or handle is set, this
can be used for detecting a file's presence.
===========
*/
static int COM_FindFile (const char *filename, int *handle, FILE **file,
							unsigned int *path_id)
{
	searchpath_t	*search;
	char		netpath[MAX_OSPATH];
	pack_t		*pak;
	int		i;

	if (file && handle)
		Sys_Error ("COM_FindFile: both handle and file set");

	file_from_pak = 0;

//
// search through the path, one element at a time
//
	for (search = com_searchpaths; search; search = search->next)
	{
		if (search->pack)	/* look through all the pak file elements */
		{
			pak = search->pack;
			for (i = 0; i < pak->numfiles; i++)
			{
				if (strcmp(pak->files[i].name, filename) != 0)
					continue;
				// found it!
				com_filesize = pak->files[i].filelen;
				file_from_pak = 1;
				if (handle || file) // [PS2_QUAKE]: an open, not COM_FileExists - for the perf run's log and the loading screen
					PS2_FileOpening (filename);
				if (path_id)
					*path_id = search->path_id;
				if (pak->pkz) // [PS2_QUAKE]: a compressed pack opens its files itself, at position 0
				{
					if (handle)
						*handle = PS2_PkzOpenHandle (pak->pkz, pak->files[i].filepos, filename);
					else if (file)
						*file = PS2_PkzOpenStream (pak->pkz, pak->files[i].filepos, filename);
					return com_filesize;
				}
				if (handle)
				{
					*handle = pak->handle;
					Sys_FileSeek (pak->handle, pak->files[i].filepos);
					return com_filesize;
				}
				else if (file)
				{ /* open a new file on the pakfile */
					*file = fopen (pak->filename, "rb");
					if (*file)
						fseek (*file, pak->files[i].filepos, SEEK_SET);
					return com_filesize;
				}
				else /* for COM_FileExists() */
				{
					return com_filesize;
				}
			}
		}
		else	/* check a file in the directory tree */
		{
			if (!registered.value)
			{ /* if not a registered version, don't ever go beyond base */
				if ( strchr (filename, '/') || strchr (filename,'\\'))
					continue;
			}

			q_snprintf (netpath, sizeof(netpath), "%s/%s",search->filename, filename);
			if (! (Sys_FileType(netpath) & FS_ENT_FILE))
				continue;

			if (path_id)
				*path_id = search->path_id;
			if (handle || file) // [PS2_QUAKE]: an open, not COM_FileExists - for the perf run's log and the loading screen
				PS2_FileOpening (filename);
			if (handle)
			{
				com_filesize = Sys_FileOpenRead (netpath, &i);
				*handle = i;
				return com_filesize;
			}
			else if (file)
			{
				*file = fopen (netpath, "rb");
				com_filesize = (*file == NULL) ? -1 : COM_filelength (*file);
				return com_filesize;
			}
			else
			{
				return 0; /* dummy valid value for COM_FileExists() */
			}
		}
	}

	if (strcmp(COM_FileGetExtension(filename), "pcx") != 0
		&& strcmp(COM_FileGetExtension(filename), "tga") != 0
		&& strcmp(COM_FileGetExtension(filename), "lit") != 0
		&& strcmp(COM_FileGetExtension(filename), "vis") != 0
		&& strcmp(COM_FileGetExtension(filename), "ent") != 0)
		Con_DPrintf ("FindFile: can't find %s\n", filename);
	else	Con_DPrintf2("FindFile: can't find %s\n", filename);

	if (handle)
		*handle = -1;
	if (file)
		*file = NULL;
	com_filesize = -1;
	return com_filesize;
}


/*
===========
COM_FileExists

Returns whether the file is found in the quake filesystem.
===========
*/
qboolean COM_FileExists (const char *filename, unsigned int *path_id)
{
	int ret = COM_FindFile (filename, NULL, NULL, path_id);
	return (ret == -1) ? false : true;
}

/*
===========
COM_OpenFile

filename never has a leading slash, but may contain directory walks
returns a handle and a length
it may actually be inside a pak file
===========
*/
int COM_OpenFile (const char *filename, int *handle, unsigned int *path_id)
{
	return COM_FindFile (filename, handle, NULL, path_id);
}

/*
===========
COM_FOpenFile

If the requested file is inside a packfile, a new FILE * will be opened
into the file.
===========
*/
int COM_FOpenFile (const char *filename, FILE **file, unsigned int *path_id)
{
	// [PS2_QUAKE]: a demo streams through the background loader's read-ahead ring when its source
	// can be read by range (not a deflated pkz entry) and the ring can be had; otherwise it gets
	// the plain stream below, as before. A loose file's stream is the ring's then, closed with it.
	if (PS2_WantsReadAheadStream (filename))
	{
		fs_location_t loc;
		int len = COM_LocateFile (filename, &loc, path_id);
		if (len >= 0)
		{
			if (loc.fd >= 0 && loc.kind != FS_LOC_PKZ_DEFLATED)
			{
				*file = PS2_OpenReadAheadStream (filename, &loc);
				if (*file)
					return len;
			}
			COM_ReleaseLocation (&loc);
		}
	}
	return COM_FindFile (filename, NULL, file, path_id);
}

/*
============
COM_CloseFile

If it is a pak file handle, don't really close it
============
*/
void COM_CloseFile (int h)
{
	searchpath_t	*s;

	for (s = com_searchpaths; s; s = s->next)
		if (s->pack && s->pack->handle == h)
			return;

	Sys_FileClose (h);
}


/*
===========
COM_LocateFile

[PS2_QUAKE]: COM_FindFile's search without its read, for the background loader (ps2/fs/async_io.h).
The loader only ever sees a descriptor and a byte range: a pak's second descriptor, opened here
the first time a file of it is located and closed with the pak (COM_Game_f); a pkz's reader
descriptor and the entry's (compressed) range (PS2_PkzLocate); a loose file's own stream, through
fileno, held open until COM_ReleaseLocation. The existence check stays synchronous: a missing file
is -1 here, as it is from COM_FindFile, so the callers' "missing" paths are unchanged. Sets
com_filesize and file_from_pak as COM_FindFile does.
===========
*/
int COM_LocateFile (const char *filename, fs_location_t *out, unsigned int *path_id)
{
	searchpath_t	*search;
	char		netpath[MAX_OSPATH];
	pack_t		*pak;
	int		i;

	memset (out, 0, sizeof(*out));
	out->fd = -1;
	file_from_pak = 0;

	for (search = com_searchpaths; search; search = search->next)
	{
		if (search->pack)
		{
			pak = search->pack;
			for (i = 0; i < pak->numfiles; i++)
			{
				if (strcmp(pak->files[i].name, filename) != 0)
					continue;
				com_filesize = pak->files[i].filelen;
				file_from_pak = 1;
				PS2_FileOpening (filename);
				if (path_id)
					*path_id = search->path_id;
				if (pak->pkz)
				{
					int fd, deflated;
					unsigned offset, compBytes, rawBytes;
					int found = PS2_PkzLocate (pak->pkz, pak->files[i].filepos, &fd, &offset, &compBytes, &rawBytes, &deflated);
					if (found == 0)
						Sys_Error ("COM_LocateFile: %s is damaged in %s", filename, pak->filename);
					if (found > 0)
					{
						out->kind = deflated ? FS_LOC_PKZ_DEFLATED : FS_LOC_PKZ_STORED;
						out->fd = fd;
						out->offset = offset;
						out->bytes = compBytes;
						out->rawBytes = rawBytes;
					}
					return com_filesize;
				}
				if (pak->asyncFd < 0)
					pak->asyncFd = open (pak->filename, O_RDONLY);
				out->kind = FS_LOC_PAK;
				out->fd = pak->asyncFd;
				out->offset = (unsigned)pak->files[i].filepos;
				out->bytes = (unsigned)com_filesize;
				out->rawBytes = (unsigned)com_filesize;
				return com_filesize;
			}
		}
		else
		{
			if (!registered.value)
			{ /* if not a registered version, don't ever go beyond base */
				if ( strchr (filename, '/') || strchr (filename,'\\'))
					continue;
			}

			q_snprintf (netpath, sizeof(netpath), "%s/%s",search->filename, filename);
			if (! (Sys_FileType(netpath) & FS_ENT_FILE))
				continue;

			if (path_id)
				*path_id = search->path_id;
			PS2_FileOpening (filename);
			out->looseFile = fopen (netpath, "rb");
			com_filesize = (out->looseFile == NULL) ? -1 : COM_filelength (out->looseFile);
			if (com_filesize >= 0)
			{
				out->kind = FS_LOC_LOOSE;
				out->fd = fileno (out->looseFile);
				out->bytes = (unsigned)com_filesize;
				out->rawBytes = (unsigned)com_filesize;
			}
			return com_filesize;
		}
	}

	Con_DPrintf ("LocateFile: can't find %s\n", filename);
	com_filesize = -1;
	return com_filesize;
}

void COM_ReleaseLocation (fs_location_t *loc)
{
	if (loc->looseFile)
	{
		fclose (loc->looseFile);
		loc->looseFile = NULL;
	}
	loc->fd = -1;
}

/*
============
COM_LoadFile

Filename are reletive to the quake directory.
Allways appends a 0 byte.
============
*/
#define	LOADFILE_ZONE		0
#define	LOADFILE_HUNK		1
#define	LOADFILE_TEMPHUNK	2
#define	LOADFILE_CACHE		3
#define	LOADFILE_STACK		4
#define	LOADFILE_MALLOC		5

static byte	*loadbuf;
static cache_user_t *loadcache;
static int	loadsize;

byte *COM_LoadFile (const char *path, int usehunk, unsigned int *path_id)
{
	int		h;
	byte	*buf;
	char	base[32];
	int	len, nread;

	buf = NULL;	// quiet compiler warning

// look for it in the filesystem or pack files
	len = COM_OpenFile (path, &h, path_id);
	if (h == -1)
		return NULL;

// extract the filename base name for hunk tag
	COM_FileBase (path, base, sizeof(base));

	switch (usehunk)
	{
	case LOADFILE_HUNK:
		buf = (byte *) Hunk_AllocName (len+1, base);
		break;
	case LOADFILE_TEMPHUNK:
		buf = (byte *) Hunk_TempAlloc (len+1);
		break;
	case LOADFILE_ZONE:
		buf = (byte *) Z_Malloc (len+1);
		break;
	case LOADFILE_CACHE:
		buf = (byte *) Cache_Alloc (loadcache, len+1, base);
		break;
	case LOADFILE_STACK:
		if (len < loadsize)
			buf = loadbuf;
		else
			buf = (byte *) Hunk_TempAlloc (len+1);
		break;
	case LOADFILE_MALLOC:
		buf = (byte *) malloc (len+1);
		break;
	default:
		Sys_Error ("COM_LoadFile: bad usehunk");
	}

	if (!buf)
		Sys_Error ("COM_LoadFile: not enough space for %s", path);

	((byte *)buf)[len] = 0;

	nread = Sys_FileRead (h, buf, len);
	COM_CloseFile (h);
	if (nread != len)
		Sys_Error ("COM_LoadFile: Error reading %s", path);

	return buf;
}

byte *COM_LoadHunkFile (const char *path, unsigned int *path_id)
{
	return COM_LoadFile (path, LOADFILE_HUNK, path_id);
}

byte *COM_LoadZoneFile (const char *path, unsigned int *path_id)
{
	return COM_LoadFile (path, LOADFILE_ZONE, path_id);
}

byte *COM_LoadTempFile (const char *path, unsigned int *path_id)
{
	return COM_LoadFile (path, LOADFILE_TEMPHUNK, path_id);
}

void COM_LoadCacheFile (const char *path, struct cache_user_s *cu, unsigned int *path_id)
{
	loadcache = cu;
	COM_LoadFile (path, LOADFILE_CACHE, path_id);
}

// uses temp hunk if larger than bufsize
byte *COM_LoadStackFile (const char *path, void *buffer, int bufsize, unsigned int *path_id)
{
	byte	*buf;

	loadbuf = (byte *)buffer;
	loadsize = bufsize;
	buf = COM_LoadFile (path, LOADFILE_STACK, path_id);

	return buf;
}

// returns malloc'd memory
byte *COM_LoadMallocFile (const char *path, unsigned int *path_id)
{
	return COM_LoadFile (path, LOADFILE_MALLOC, path_id);
}

byte *COM_LoadMallocFile_TextMode_OSPath (const char *path, long *len_out)
{
	FILE	*f;
	byte	*data;
	long	len, actuallen;

	// ericw -- this is used by Host_Loadgame_f. Translate CRLF to LF on load games,
	// othewise multiline messages have a garbage character at the end of each line.
	// TODO: could handle in a way that allows loading CRLF savegames on mac/linux
	// without the junk characters appearing.
	f = fopen (path, "rt");
	if (f == NULL)
		return NULL;

	len = COM_filelength (f);
	if (len < 0)
	{
		fclose (f);
		return NULL;
	}

	data = (byte *) malloc (len + 1);
	if (data == NULL)
	{
		fclose (f);
		return NULL;
	}

	// (actuallen < len) if CRLF to LF translation was performed
	actuallen = fread (data, 1, len, f);
	if (ferror(f))
	{
		fclose (f);
		free (data);
		return NULL;
	}
	data[actuallen] = '\0';

	if (len_out != NULL)
		*len_out = actuallen;
	fclose (f);
	return data;
}

const char *COM_ParseIntNewline(const char *buffer, int *value)
{
	int consumed = 0;
	sscanf (buffer, "%i\n%n", value, &consumed);
	return buffer + consumed;
}

const char *COM_ParseFloatNewline(const char *buffer, float *value)
{
	int consumed = 0;
	sscanf (buffer, "%f\n%n", value, &consumed);
	return buffer + consumed;
}

const char *COM_ParseStringNewline(const char *buffer)
{
	int consumed = 0;
	com_token[0] = '\0';
	sscanf (buffer, "%1023s\n%n", com_token, &consumed);
	return buffer + consumed;
}

/*
=================
COM_LoadPackFile -- johnfitz -- modified based on topaz's tutorial

Takes an explicit (not game tree related) path to a pak file.

Loads the header and directory, adding the files at the beginning
of the list so they override previous pack files.
=================
*/
static pack_t *COM_LoadPackFile (const char *packfile)
{
	dpackheader_t	header;
	int		i;
	packfile_t	*newfiles;
	int		numpackfiles;
	pack_t		*pack;
	int		packhandle;
	dpackfile_t	info[MAX_FILES_IN_PACK];
	unsigned short	crc;

	if (Sys_FileOpenRead (packfile, &packhandle) == -1)
		return NULL;

	if (Sys_FileRead(packhandle, &header, sizeof(header)) != (int) sizeof(header) ||
	    header.id[0] != 'P' || header.id[1] != 'A' || header.id[2] != 'C' || header.id[3] != 'K')
		Sys_Error ("%s is not a packfile", packfile);

	header.dirofs = LittleLong (header.dirofs);
	header.dirlen = LittleLong (header.dirlen);

	numpackfiles = header.dirlen / sizeof(dpackfile_t);

	if (header.dirlen < 0 || header.dirofs < 0)
	{
		Sys_Error ("Invalid packfile %s (dirlen: %i, dirofs: %i)",
					packfile, header.dirlen, header.dirofs);
	}
	if (!numpackfiles)
	{
		Sys_Printf ("WARNING: %s has no files, ignored\n", packfile);
		Sys_FileClose (packhandle);
		return NULL;
	}
	if (numpackfiles > MAX_FILES_IN_PACK)
		Sys_Error ("%s has %i files", packfile, numpackfiles);

	if (numpackfiles != PAK0_COUNT)
		com_modified = true;	// not the original file

	newfiles = (packfile_t *) Z_Malloc(numpackfiles * sizeof(packfile_t));

	Sys_FileSeek (packhandle, header.dirofs);
	if (Sys_FileRead(packhandle, info, header.dirlen) != header.dirlen)
		Sys_Error ("Error reading %s", packfile);

	// crc the directory to check for modifications
	CRC_Init (&crc);
	for (i = 0; i < header.dirlen; i++)
		CRC_ProcessByte (&crc, ((byte *)info)[i]);
	if (crc != PAK0_CRC_V106 && crc != PAK0_CRC_V101 && crc != PAK0_CRC_V100)
		com_modified = true;

	// parse the directory
	for (i = 0; i < numpackfiles; i++)
	{
		q_strlcpy (newfiles[i].name, info[i].name, sizeof(newfiles[i].name));
		newfiles[i].filepos = LittleLong(info[i].filepos);
		newfiles[i].filelen = LittleLong(info[i].filelen);
	}

	pack = (pack_t *) Z_Malloc (sizeof (pack_t));
	q_strlcpy (pack->filename, packfile, sizeof(pack->filename));
	pack->handle = packhandle;
	pack->asyncFd = -1; // [PS2_QUAKE]
	pack->numfiles = numpackfiles;
	pack->files = newfiles;

	//Sys_Printf ("Added packfile %s (%i files)\n", packfile, numpackfiles);
	return pack;
}

/*
=================
COM_LoadPkzFile

[PS2_QUAKE]: COM_LoadPackFile for a compressed pakN.pkz (ps2/engine_hooks.h), which reads its own
directory. This builds the packfile_t list from it, so the lookups don't change, with each file's
index in the pkz as its filepos.
=================
*/
static void COM_AddPkzFile (void *context, int index, const char *name, int length)
{
	packfile_t	*file = (packfile_t *) context + index;

	q_strlcpy (file->name, name, sizeof(file->name));
	file->filepos = index;
	file->filelen = length;
}

static pack_t *COM_LoadPkzFile (const char *packfile)
{
	ps2_pkz_t	*pkz;
	pack_t		*pack;
	int		numpackfiles;

	pkz = PS2_PkzOpen (packfile);
	if (!pkz)
		return NULL;

	numpackfiles = PS2_PkzNumFiles (pkz);
	if (!numpackfiles)
	{
		Sys_Printf ("WARNING: %s has no files, ignored\n", packfile);
		PS2_PkzClose (pkz);
		return NULL;
	}

	// Of COM_LoadPackFile's test for id's own pak0 only the file count can be made: its CRC is
	// of the pak's directory as stored, junk bytes after the names and file offsets included,
	// which a pkz doesn't keep. So a pkz of id's pak0 still plays as the shareware version, and
	// one whose count differs is a modified game, as its pak was.
	if (numpackfiles != PAK0_COUNT)
		com_modified = true;	// not the original file

	pack = (pack_t *) Z_Malloc (sizeof (pack_t));
	pack->files = (packfile_t *) Z_Malloc (numpackfiles * sizeof(packfile_t));
	PS2_PkzListFiles (pkz, COM_AddPkzFile, pack->files);

	q_strlcpy (pack->filename, packfile, sizeof(pack->filename));
	pack->handle = -1;
	pack->asyncFd = -1;
	pack->numfiles = numpackfiles;
	pack->pkz = pkz;

	Con_Printf ("Added packfile %s (%i files, compressed)\n", packfile, numpackfiles);
	return pack;
}

/*
=================
COM_AddGameDirectory -- johnfitz -- modified based on topaz's tutorial
=================
*/
static void COM_AddGameDirectory (const char *base, const char *dir)
{
	int i;
	unsigned int path_id;
	searchpath_t *search;
	pack_t *pak, *qspak;
	char pakfile[MAX_OSPATH];
	qboolean been_here = false;

	q_strlcpy (com_gamedir, va("%s/%s", base, dir), sizeof(com_gamedir));

	// assign a path_id to this game directory
	if (com_searchpaths)
		path_id = com_searchpaths->path_id << 1;
	else	path_id = 1U;

_add_path:
	// add the directory to the search path
	search = (searchpath_t *) Z_Malloc(sizeof(searchpath_t));
	search->path_id = path_id;
	q_strlcpy (search->filename, com_gamedir, sizeof(search->filename));
	search->next = com_searchpaths;
	com_searchpaths = search;

	// add any pak files in the format pak0.pak pak1.pak, ...
	for (i = 0; ; i++)
	{
		// [PS2_QUAKE]: a compressed pakN.pkz is loaded instead of pakN.pak when there is one. The
		// two hold the same files, so loading both would only cost the memory of a second list.
		q_snprintf (pakfile, sizeof(pakfile), "%s/pak%i.pkz", com_gamedir, i);
		pak = COM_LoadPkzFile (pakfile);
		if (!pak)
		{
			q_snprintf (pakfile, sizeof(pakfile), "%s/pak%i.pak", com_gamedir, i);
			pak = COM_LoadPackFile (pakfile);
		}
		if (i != 0 || path_id != 1 || fitzmode)
			qspak = NULL;
		else {
			qboolean old = com_modified;
			if (been_here) base = host_parms->userdir;
			q_snprintf (pakfile, sizeof(pakfile), "%s/quakespasm.pak", base);
			qspak = COM_LoadPackFile (pakfile);
			com_modified = old;
		}
		if (pak) {
			search = (searchpath_t *) Z_Malloc(sizeof(searchpath_t));
			search->path_id = path_id;
			search->pack = pak;
			search->next = com_searchpaths;
			com_searchpaths = search;
		}
		if (qspak) {
			search = (searchpath_t *) Z_Malloc(sizeof(searchpath_t));
			search->path_id = path_id;
			search->pack = qspak;
			search->next = com_searchpaths;
			com_searchpaths = search;
		}
		if (!pak) break;
	}

	if (!been_here && host_parms->userdir != host_parms->basedir)
	{
		been_here = true;
		q_strlcpy(com_gamedir, va("%s/%s", host_parms->userdir, dir), sizeof(com_gamedir));
		Sys_mkdir(com_gamedir);
		goto _add_path;
	}
}

/*
=================
COM_InitFilesystem
=================
*/
void COM_InitFilesystem (void)
{
	int i, j;

	Cvar_RegisterVariable (&registered);
	Cvar_RegisterVariable (&cmdline);
	Cmd_AddCommand ("path", COM_Path_f);

	i = COM_CheckParm ("-basedir");
	if (i && i < com_argc-1)
		q_strlcpy (com_basedir, com_argv[i + 1], sizeof(com_basedir));
	else
		q_strlcpy (com_basedir, host_parms->basedir, sizeof(com_basedir));

	j = strlen (com_basedir);
	if (j < 1) Sys_Error("Bad argument to -basedir");
	if ((com_basedir[j-1] == '\\') || (com_basedir[j-1] == '/'))
		com_basedir[j-1] = 0;

	// start up with GAMENAME by default (id1), then always mount qw on top
	COM_AddGameDirectory (com_basedir, GAMENAME);
	COM_AddGameDirectory (com_basedir, "qw");

	/* Lock id1 + qw as the permanent base searchpath */
	com_base_searchpaths = com_searchpaths;

	// Support both -gamedir (QW standard) and -game (QuakeSpasm habit)
	i = COM_CheckParm ("-gamedir");
	if (!i)
		i = COM_CheckParm ("-game");

	if (i && i < com_argc-1)
	{
		const char *p = com_argv[i + 1];
		if (!*p || !strcmp(p, ".") || strstr(p, "..") || strstr(p, "/") || strstr(p, "\\") || strstr(p, ":"))
			Sys_Error ("gamedir should be a single directory name, not a path\n");

		com_modified = true;
		if (q_strcasecmp(p, GAMENAME) && q_strcasecmp(p, "qw"))
			COM_AddGameDirectory (com_basedir, p);
	}

	COM_CheckRegistered ();
}

qboolean COM_Gamedir (const char *directory, enum game_type game_type)
{
	searchpath_t *search;

	if (!directory || !*directory)
		directory = "qw";

	if (!strcmp(directory, ".") || strstr(directory, "..") || strstr(directory, "/") || strstr(directory, "\\") || strstr(directory, ":"))
	{
		Con_Printf("gamedir should be a single directory name, not a path\n");
		return false;
	}

	if (!COM_CheckForGameDirectoryChange(directory, game_type))
		return false;

	com_modified = true;

	// [PS2_QUAKE]: Wait for any background IOP/CDVD read to finish before closing packs
	PS2_AsyncIoDrain();

	while (com_searchpaths != com_base_searchpaths)
	{
		if (com_searchpaths->pack)
		{
			if (com_searchpaths->pack->pkz)
				PS2_PkzClose(com_searchpaths->pack->pkz);
			else
				Sys_FileClose(com_searchpaths->pack->handle);

			if (com_searchpaths->pack->asyncFd >= 0)
				close(com_searchpaths->pack->asyncFd);

			Z_Free(com_searchpaths->pack->files);
			Z_Free(com_searchpaths->pack);
		}
		search = com_searchpaths->next;
		Z_Free(com_searchpaths);
		com_searchpaths = search;
	}

	// Mount the requested mod directory on top of id1 + qw (which are locked in com_base_searchpaths)
	if (q_strcasecmp(directory, GAMENAME) && q_strcasecmp(directory, "qw"))
	{
		COM_AddGameDirectory(com_basedir, directory);
	}
	else
	{
		// Reset com_gamedir string back to qw (or id1)
		q_snprintf(com_gamedir, sizeof(com_gamedir), "%s/%s",
			(host_parms->userdir != host_parms->basedir) ? host_parms->userdir : com_basedir,
			directory);
	}

	// Flush and reload QuakeSpasm / PS2 renderer & cache assets
	Cache_Flush();
	Mod_ResetAll();
	Sky_ClearAll();
	TexMgr_NewGame();
	Draw_NewGame();
	R_NewGame();

	Con_Printf("\"gamedir\" changed to \"%s\"\n", COM_SkipPath(com_gamedir));

	return true;
}

/* The following FS_*() stdio replacements are necessary if one is
 * to perform non-sequential reads on files reopened on pak files
 * because we need the bookkeeping about file start/end positions.
 * Allocating and filling in the fshandle_t structure is the users'
 * responsibility when the file is initially opened. */

size_t FS_fread(void *ptr, size_t size, size_t nmemb, fshandle_t *fh)
{
	long byte_size;
	long bytes_read;
	size_t nmemb_read;

	if (!fh) {
		errno = EBADF;
		return 0;
	}
	if (!ptr) {
		errno = EFAULT;
		return 0;
	}
	if (!size || !nmemb) {	/* no error, just zero bytes wanted */
		errno = 0;
		return 0;
	}

	byte_size = nmemb * size;
	if (byte_size > fh->length - fh->pos)	/* just read to end */
		byte_size = fh->length - fh->pos;
	bytes_read = fread(ptr, 1, byte_size, fh->file);
	fh->pos += bytes_read;

	/* fread() must return the number of elements read,
	 * not the total number of bytes. */
	nmemb_read = bytes_read / size;
	/* even if the last member is only read partially
	 * it is counted as a whole in the return value. */
	if (bytes_read % size)
		nmemb_read++;

	return nmemb_read;
}

int FS_fseek(fshandle_t *fh, long offset, int whence)
{
/* I don't care about 64 bit off_t or fseeko() here.
 * the quake/hexen2 file system is 32 bits, anyway. */
	int ret;

	if (!fh) {
		errno = EBADF;
		return -1;
	}

	/* the relative file position shouldn't be smaller
	 * than zero or bigger than the filesize. */
	switch (whence)
	{
	case SEEK_SET:
		break;
	case SEEK_CUR:
		offset += fh->pos;
		break;
	case SEEK_END:
		offset = fh->length + offset;
		break;
	default:
		errno = EINVAL;
		return -1;
	}

	if (offset < 0) {
		errno = EINVAL;
		return -1;
	}

	if (offset > fh->length)	/* just seek to end */
		offset = fh->length;

	ret = fseek(fh->file, fh->start + offset, SEEK_SET);
	if (ret < 0)
		return ret;

	fh->pos = offset;
	return 0;
}

int FS_fclose(fshandle_t *fh)
{
	if (!fh) {
		errno = EBADF;
		return -1;
	}
	return fclose(fh->file);
}

long FS_ftell(fshandle_t *fh)
{
	if (!fh) {
		errno = EBADF;
		return -1;
	}
	return fh->pos;
}

void FS_rewind(fshandle_t *fh)
{
	if (!fh) return;
	clearerr(fh->file);
	fseek(fh->file, fh->start, SEEK_SET);
	fh->pos = 0;
}

int FS_feof(fshandle_t *fh)
{
	if (!fh) {
		errno = EBADF;
		return -1;
	}
	if (fh->pos >= fh->length)
		return -1;
	return 0;
}

int FS_ferror(fshandle_t *fh)
{
	if (!fh) {
		errno = EBADF;
		return -1;
	}
	return ferror(fh->file);
}

int FS_fgetc(fshandle_t *fh)
{
	if (!fh) {
		errno = EBADF;
		return EOF;
	}
	if (fh->pos >= fh->length)
		return EOF;
	fh->pos += 1;
	return fgetc(fh->file);
}

char *FS_fgets(char *s, int size, fshandle_t *fh)
{
	char *ret;

	if (FS_feof(fh))
		return NULL;

	if (size > (fh->length - fh->pos) + 1)
		size = (fh->length - fh->pos) + 1;

	ret = fgets(s, size, fh->file);
	fh->pos = ftell(fh->file) - fh->start;

	return ret;
}

long FS_filelength (fshandle_t *fh)
{
	if (!fh) {
		errno = EBADF;
		return -1;
	}
	return fh->length;
}

/*
============================================================================
								LOCALIZATION
============================================================================
*/
typedef struct
{
	char *key;
	char *value;
} locentry_t;

typedef struct
{
	int			numentries;
	int			maxnumentries;
	int			numindices;
	unsigned	*indices;
	locentry_t	*entries;
	char		*text;
} localization_t;

static localization_t localization;

/*
================
COM_HashString
Computes the FNV-1a hash of string str
================
*/
unsigned COM_HashString (const char *str)
{
	unsigned hash = 0x811c9dc5u;
	while (*str)
	{
		hash ^= *str++;
		hash *= 0x01000193u;
	}
	return hash;
}

/*
================
LOC_LoadFile
================
*/
void LOC_LoadFile (const char *file)
{
	int i,lineno;
	char *cursor;

	// clear existing data
	if (localization.text)
	{
		free(localization.text);
		localization.text = NULL;
	}
	localization.numentries = 0;
	localization.numindices = 0;

	if (!file || !*file)
		return;

	Con_Printf("\nLanguage initialization\n");

	// [PS2_QUAKE]: the search path only. Without SDL there is no fallback to unzipping the file
	// out of the 2021 re-release's QuakeEX.kpf, and the original game data has neither.
	localization.text = (char *) COM_LoadMallocFile(file, NULL);
	if (!localization.text)
	{
		Con_Printf("Couldn't load '%s'\nfrom '%s'\n", file, com_basedir);
		return;
	}

	cursor = localization.text;

	// skip BOM
	if ((unsigned char)(cursor[0]) == 0xEF && (unsigned char)(cursor[1]) == 0xBB && (unsigned char)(cursor[2]) == 0xBF)
		cursor += 3;

	lineno = 0;
	while (*cursor)
	{
		char *line, *equals;

		lineno++;

		// skip leading whitespace
		while (q_isblank(*cursor))
			++cursor;

		line = cursor;
		equals = NULL;
		// find line end and first equals sign, if any
		while (*cursor && *cursor != '\n')
		{
			if (*cursor == '=' && !equals)
				equals = cursor;
			cursor++;
		}

		if (line[0] == '/')
		{
			if (line[1] != '/')
				Con_DPrintf("LOC_LoadFile: malformed comment on line %d\n", lineno);
		}
		else if (equals)
		{
			char *key_end = equals;
			qboolean leading_quote;
			qboolean trailing_quote;
			locentry_t *entry;
			char *value_src;
			char *value_dst;
			char *value;

			// trim whitespace before equals sign
			while (key_end != line && q_isspace(key_end[-1]))
				key_end--;
			*key_end = 0;

			value = equals + 1;
			// skip whitespace after equals sign
			while (value != cursor && q_isspace(*value))
				value++;

			leading_quote = (*value == '\"');
			trailing_quote = false;
			value += leading_quote;

			// transform escape sequences in-place
			value_src = value;
			value_dst = value;
			while (value_src != cursor)
			{
				if (*value_src == '\\' && value_src + 1 != cursor)
				{
					char c = value_src[1];
					value_src += 2;
					switch (c)
					{
						case 'n': *value_dst++ = '\n'; break;
						case 't': *value_dst++ = '\t'; break;
						case 'v': *value_dst++ = '\v'; break;
						case 'b': *value_dst++ = '\b'; break;
						case 'f': *value_dst++ = '\f'; break;

						case '"':
						case '\'':
							*value_dst++ = c;
							break;

						default:
							Con_Printf("LOC_LoadFile: unrecognized escape sequence \\%c on line %d\n", c, lineno);
							*value_dst++ = c;
							break;
					}
					continue;
				}

				if (*value_src == '\"')
				{
					trailing_quote = true;
					*value_dst = 0;
					break;
				}

				*value_dst++ = *value_src++;
			}

			// if not a quoted string, trim trailing whitespace
			if (!trailing_quote)
			{
				while (value_dst != value && q_isblank(value_dst[-1]))
				{
					*value_dst = 0;
					value_dst--;
				}
			}

			if (localization.numentries == localization.maxnumentries)
			{
				// grow by 50%
				localization.maxnumentries += localization.maxnumentries >> 1;
				localization.maxnumentries = q_max(localization.maxnumentries, 32);
				localization.entries = (locentry_t*) realloc(localization.entries, sizeof(*localization.entries) * localization.maxnumentries);
			}

			entry = &localization.entries[localization.numentries++];
			entry->key = line;
			entry->value = value;
		}

		if (*cursor)
			*cursor++ = 0; // terminate line and advance to next
	}

	// hash all entries

	localization.numindices = localization.numentries * 2; // 50% load factor
	if (localization.numindices == 0)
	{
		Con_Printf("No localized strings in file '%s'\n", file);
		return;
	}

	localization.indices = (unsigned*) realloc(localization.indices, localization.numindices * sizeof(*localization.indices));
	memset(localization.indices, 0, localization.numindices * sizeof(*localization.indices));

	for (i = 0; i < localization.numentries; i++)
	{
		locentry_t *entry = &localization.entries[i];
		unsigned pos = COM_HashString(entry->key) % localization.numindices, end = pos;

		for (;;)
		{
			if (!localization.indices[pos])
			{
				localization.indices[pos] = i + 1;
				break;
			}

			++pos;
			if (pos == localization.numindices)
				pos = 0;

			if (pos == end)
				Sys_Error("LOC_LoadFile failed");
		}
	}

	Con_Printf("Loaded %d strings from '%s'\n", localization.numentries, file);
}

/*
================
LOC_Init
================
*/
void LOC_Init(void)
{
	LOC_LoadFile("localization/loc_english.txt");
}

/*
================
LOC_Shutdown
================
*/
void LOC_Shutdown(void)
{
	free(localization.indices);
	free(localization.entries);
	free(localization.text);
	memset(&localization, 0, sizeof(localization));
}

/*
================
LOC_GetRawString

Returns localized string if available, or NULL otherwise
================
*/
const char* LOC_GetRawString (const char *key)
{
	unsigned pos, end;

	if (!localization.numindices || !key || !*key || *key != '$')
		return NULL;
	key++;

	pos = COM_HashString(key) % localization.numindices;
	end = pos;

	do
	{
		unsigned idx = localization.indices[pos];
		locentry_t *entry;
		if (!idx)
			return NULL;

		entry = &localization.entries[idx - 1];
		if (!Q_strcmp(entry->key, key))
			return entry->value;

		++pos;
		if (pos == localization.numindices)
			pos = 0;
	} while (pos != end);

	return NULL;
}

/*
================
LOC_GetString

Returns localized string if available, or input string otherwise
================
*/
const char* LOC_GetString (const char *key)
{
	const char* value = LOC_GetRawString(key);
	return value ? value : key;
}

/*
================
LOC_ParseArg

Returns argument index (>= 0) and advances the string if it starts with a placeholder ({} or {N}),
otherwise returns a negative value and leaves the pointer unchanged
================
*/
static int LOC_ParseArg (const char **pstr)
{
	int arg;
	const char *str = *pstr;

	// opening brace
	if (*str != '{')
		return -1;
	++str;

	// optional index, defaulting to 0
	arg = 0;
	while (q_isdigit(*str))
		arg = arg * 10 + *str++ - '0';

	// closing brace
	if (*str != '}')
		return -1;
	*pstr = ++str;

	return arg;
}

/*
================
LOC_HasPlaceholders
================
*/
qboolean LOC_HasPlaceholders (const char *str)
{
	if (!localization.numindices)
		return false;
	while (*str)
	{
		if (LOC_ParseArg(&str) >= 0)
			return true;
		str++;
	}
	return false;
}

/*
================
LOC_Format

Replaces placeholders (of the form {} or {N}) with the corresponding arguments

Returns number of written chars, excluding the NUL terminator
If len > 0, output is always NUL-terminated
================
*/
size_t LOC_Format (const char *format, const char* (*getarg_fn) (int idx, void* userdata), void* userdata, char* out, size_t len)
{
	size_t written = 0;
	int numargs = 0;

	if (!len)
	{
		Con_DPrintf("LOC_Format: no output space\n");
		return 0;
	}
	--len; // reserve space for the terminator

	while (*format && written < len)
	{
		const char* insert;
		size_t space_left;
		size_t insert_len;
		int argindex = LOC_ParseArg(&format);

		if (argindex < 0)
		{
			out[written++] = *format++;
			continue;
		}

		insert = getarg_fn(argindex, userdata);
		space_left = len - written;
		insert_len = Q_strlen(insert);

		if (insert_len > space_left)
		{
			Con_DPrintf("LOC_Format: overflow at argument #%d\n", numargs);
			insert_len = space_left;
		}

		Q_memcpy(out + written, insert, insert_len);
		written += insert_len;
	}

	if (*format)
		Con_DPrintf("LOC_Format: overflow\n");

	out[written] = 0;

	return written;
}

static const char *Info_ReadString(const char *infostring, char *buffer, int buflen)
{
    char *out = buffer;

    while (out - buffer < buflen - 1)
	{
		if (!*infostring || *infostring == '\\')
		{
			break;
		}
	    
		*out++ = *infostring++;
    }
    *out = 0;

    return infostring;
}

static const char *Info_ReadValue(const char *infostring, char *buffer, int buflen)
{
    infostring = Info_ReadString(infostring, buffer, buflen);

    if (*infostring && *infostring != '\\')
	{
		Con_DPrintf("WARNING: info value too long? (%s)\n", buffer);
	}
	
    return infostring;
}

static const char *Info_ReadKey(const char *infostring, char *buffer, int buflen)
{
    const char *pkey;

    if (*infostring == '\\')
	{
		infostring++;
	}
	
    pkey = infostring;
    infostring = Info_ReadString(infostring, buffer, buflen);

    /* If we aren't at a separator, then the key was too long */
    if (*buffer && *infostring != '\\')
	{
		Con_DPrintf("WARNING: No separator after info key (%s)\n", pkey);
	}

    return infostring;
}

void Info_RemoveKey(char *infostring, const char *key)
{
    char *start;
    char pkey[MAX_INFO_STRING];
    char value[MAX_INFO_STRING];

    if (strstr(key, "\\"))
	{
		Con_Printf("Can't use a key with a \\\n");
	
		return;
    }

    while (1) {
		start = infostring;

		infostring = (char *)Info_ReadKey(infostring, pkey, sizeof(pkey));
		if (*infostring)
		{
			infostring++;
		}
	    
		infostring = (char *)Info_ReadValue(infostring, value, sizeof(value));
		if (*infostring)
		{
			infostring++;
		}
	    
		if (!strcmp(key, pkey))
		{
	    	memmove(start, infostring, strlen(infostring) + 1);
	    
			return;
		}

		if (!*infostring)
		{
			return;
		}
    }
}

void Info_SetValueForStarKey(char *infostring, const char *key, const char *value, int maxsize)
{
    char buffer[MAX_INFO_STRING * 2];
    char *oldvalue, *info;
    int c, len;

    if (strstr(key, "\\") || strstr(value, "\\")) {
	Con_Printf("Can't use keys or values with a \\\n");
	return;
    }
    if (strstr(key, "\"") || strstr(value, "\"")) {
	Con_Printf("Can't use keys or values with a \"\n");
	return;
    }
    if (strlen(key) > 63 || strlen(value) > 63) {
	Con_Printf("Keys and values must be < 64 characters.\n");
	return;
    }

    oldvalue = Info_ValueForKey(infostring, key);
    if (*oldvalue) {
	/*
	 * Key exists. Make sure we have enough room for new value.
	 * If we don't, don't change it!
	 */
	len = strlen(infostring) - strlen(oldvalue) + strlen(value);
	if (len > maxsize - 1)
	    goto ErrTooLong;
    }

    Info_RemoveKey(infostring, key);
    if (!value || !strlen(value))
	return;

    len = qsnprintf(buffer, sizeof(buffer), "\\%s\\%s", key, value);
    if (len > sizeof(buffer) - 1)
	goto ErrTooLong;

    len += strlen(infostring);
    if (len > maxsize - 1)
	goto ErrTooLong;

    /* Append the new key/value pair to the info string */
    infostring += strlen(infostring);
    info = buffer;
    while (*info) {
	c = (unsigned char)*info++;
#ifndef SERVERONLY
	/* client only allows highbits on name */
	if (strcasecmp(key, "name")) {
	    c &= 127;
	    if (c < 32 || c > 127)
		continue;
	    /* auto lowercase team */
	    if (!strcasecmp(key, "team"))
		c = tolower(c);
	}
#else
	if (!sv_highchars.value) {
	    c &= 127;
	    if (c < 32 || c > 127)
		continue;
	}
#endif
	if (c > 13)
	    *infostring++ = c;
    }
    *infostring = 0;
    return;

 ErrTooLong:
	Con_Printf("Info string length exceeded\n");
}

void
Info_Print(const char *infostring)
{
    char key[MAX_INFO_STRING];
    char value[MAX_INFO_STRING];

    while (*infostring) {
	infostring = Info_ReadKey(infostring, key, sizeof(key));
	if (*infostring)
	    infostring++;
	infostring = Info_ReadValue(infostring, value, sizeof(value));
	if (*infostring)
	    infostring++;

	Con_Printf("%-20.20s %s\n", key, *value ? value : "MISSING VALUE");
    }
}

qboolean COM_CheckForGameDirectoryChange(const char *directory, enum game_type game_type)
{
    const char *current = COM_SkipPath(com_gamedir);

    if (!directory || !directory[0])
	{
		directory = "qw";
	}

    return q_strcasecmp(current, directory) != 0;
}

/*
===============
Info_ValueForKey

Searches the string for the given
key and returns the associated value, or an empty string.
===============
*/
char *
Info_ValueForKey(const char *infostring, const char *key)
{
    /* use multiple buffers so compares work without stomping on each other */
    static char buffers[4][MAX_INFO_STRING];
    static int buffer_index;

    char pkey[MAX_INFO_STRING];
    char *buf;

    buffer_index = (buffer_index + 1) & 3;
    buf = buffers[buffer_index];

    while (1) {
	/* Retrieve the key */
	infostring = Info_ReadKey(infostring, pkey, sizeof(pkey));
	if (*infostring != '\\') {
	    *buf = 0;
	    return buf;
	}
	infostring++;

	/* Retrieve the value */
	infostring = Info_ReadString(infostring, buf, sizeof(buffers[0]));
	if (*infostring && *infostring != '\\') {
	    *buf = 0;
	    return buf;
	}

	/* If the keys match, return the value in the buffer */
	if (!strcmp(key, pkey))
	    return buf;

	/* Return if we've reached the end of the infostring */
	if (!*infostring) {
	    *buf = 0;
	    return buf;
	}
	infostring++;
    }
}

void
Info_SetValueForKey(char *infostring, const char *key, const char *value, int maxsize)
{
    if (key[0] == '*') {
	Con_Printf("Can't set * keys\n");
	return;
    }
    Info_SetValueForStarKey(infostring, key, value, maxsize);
}

static byte chktbl[1024 + 4] = {
    0x78, 0xd2, 0x94, 0xe3, 0x41, 0xec, 0xd6, 0xd5, 0xcb, 0xfc, 0xdb, 0x8a, 0x4b, 0xcc, 0x85, 0x01,
    0x23, 0xd2, 0xe5, 0xf2, 0x29, 0xa7, 0x45, 0x94, 0x4a, 0x62, 0xe3, 0xa5, 0x6f, 0x3f, 0xe1, 0x7a,
    0x64, 0xed, 0x5c, 0x99, 0x29, 0x87, 0xa8, 0x78, 0x59, 0x0d, 0xaa, 0x0f, 0x25, 0x0a, 0x5c, 0x58,
    0xfb, 0x00, 0xa7, 0xa8, 0x8a, 0x1d, 0x86, 0x80, 0xc5, 0x1f, 0xd2, 0x28, 0x69, 0x71, 0x58, 0xc3,
    0x51, 0x90, 0xe1, 0xf8, 0x6a, 0xf3, 0x8f, 0xb0, 0x68, 0xdf, 0x95, 0x40, 0x5c, 0xe4, 0x24, 0x6b,
    0x29, 0x19, 0x71, 0x3f, 0x42, 0x63, 0x6c, 0x48, 0xe7, 0xad, 0xa8, 0x4b, 0x91, 0x8f, 0x42, 0x36,
    0x34, 0xe7, 0x32, 0x55, 0x59, 0x2d, 0x36, 0x38, 0x38, 0x59, 0x9b, 0x08, 0x16, 0x4d, 0x8d, 0xf8,
    0x0a, 0xa4, 0x52, 0x01, 0xbb, 0x52, 0xa9, 0xfd, 0x40, 0x18, 0x97, 0x37, 0xff, 0xc9, 0x82, 0x27,
    0xb2, 0x64, 0x60, 0xce, 0x00, 0xd9, 0x04, 0xf0, 0x9e, 0x99, 0xbd, 0xce, 0x8f, 0x90, 0x4a, 0xdd,
    0xe1, 0xec, 0x19, 0x14, 0xb1, 0xfb, 0xca, 0x1e, 0x98, 0x0f, 0xd4, 0xcb, 0x80, 0xd6, 0x05, 0x63,
    0xfd, 0xa0, 0x74, 0xa6, 0x86, 0xf6, 0x19, 0x98, 0x76, 0x27, 0x68, 0xf7, 0xe9, 0x09, 0x9a, 0xf2,
    0x2e, 0x42, 0xe1, 0xbe, 0x64, 0x48, 0x2a, 0x74, 0x30, 0xbb, 0x07, 0xcc, 0x1f, 0xd4, 0x91, 0x9d,
    0xac, 0x55, 0x53, 0x25, 0xb9, 0x64, 0xf7, 0x58, 0x4c, 0x34, 0x16, 0xbc, 0xf6, 0x12, 0x2b, 0x65,
    0x68, 0x25, 0x2e, 0x29, 0x1f, 0xbb, 0xb9, 0xee, 0x6d, 0x0c, 0x8e, 0xbb, 0xd2, 0x5f, 0x1d, 0x8f,
    0xc1, 0x39, 0xf9, 0x8d, 0xc0, 0x39, 0x75, 0xcf, 0x25, 0x17, 0xbe, 0x96, 0xaf, 0x98, 0x9f, 0x5f,
    0x65, 0x15, 0xc4, 0x62, 0xf8, 0x55, 0xfc, 0xab, 0x54, 0xcf, 0xdc, 0x14, 0x06, 0xc8, 0xfc, 0x42,
    0xd3, 0xf0, 0xad, 0x10, 0x08, 0xcd, 0xd4, 0x11, 0xbb, 0xca, 0x67, 0xc6, 0x48, 0x5f, 0x9d, 0x59,
    0xe3, 0xe8, 0x53, 0x67, 0x27, 0x2d, 0x34, 0x9e, 0x9e, 0x24, 0x29, 0xdb, 0x69, 0x99, 0x86, 0xf9,
    0x20, 0xb5, 0xbb, 0x5b, 0xb0, 0xf9, 0xc3, 0x67, 0xad, 0x1c, 0x9c, 0xf7, 0xcc, 0xef, 0xce, 0x69,
    0xe0, 0x26, 0x8f, 0x79, 0xbd, 0xca, 0x10, 0x17, 0xda, 0xa9, 0x88, 0x57, 0x9b, 0x15, 0x24, 0xba,
    0x84, 0xd0, 0xeb, 0x4d, 0x14, 0xf5, 0xfc, 0xe6, 0x51, 0x6c, 0x6f, 0x64, 0x6b, 0x73, 0xec, 0x85,
    0xf1, 0x6f, 0xe1, 0x67, 0x25, 0x10, 0x77, 0x32, 0x9e, 0x85, 0x6e, 0x69, 0xb1, 0x83, 0x00, 0xe4,
    0x13, 0xa4, 0x45, 0x34, 0x3b, 0x40, 0xff, 0x41, 0x82, 0x89, 0x79, 0x57, 0xfd, 0xd2, 0x8e, 0xe8,
    0xfc, 0x1d, 0x19, 0x21, 0x12, 0x00, 0xd7, 0x66, 0xe5, 0xc7, 0x10, 0x1d, 0xcb, 0x75, 0xe8, 0xfa,
    0xb6, 0xee, 0x7b, 0x2f, 0x1a, 0x25, 0x24, 0xb9, 0x9f, 0x1d, 0x78, 0xfb, 0x84, 0xd0, 0x17, 0x05,
    0x71, 0xb3, 0xc8, 0x18, 0xff, 0x62, 0xee, 0xed, 0x53, 0xab, 0x78, 0xd3, 0x65, 0x2d, 0xbb, 0xc7,
    0xc1, 0xe7, 0x70, 0xa2, 0x43, 0x2c, 0x7c, 0xc7, 0x16, 0x04, 0xd2, 0x45, 0xd5, 0x6b, 0x6c, 0x7a,
    0x5e, 0xa1, 0x50, 0x2e, 0x31, 0x5b, 0xcc, 0xe8, 0x65, 0x8b, 0x16, 0x85, 0xbf, 0x82, 0x83, 0xfb,
    0xde, 0x9f, 0x36, 0x48, 0x32, 0x79, 0xd6, 0x9b, 0xfb, 0x52, 0x45, 0xbf, 0x43, 0xf7, 0x0b, 0x0b,
    0x19, 0x19, 0x31, 0xc3, 0x85, 0xec, 0x1d, 0x8c, 0x20, 0xf0, 0x3a, 0xfa, 0x80, 0x4d, 0x2c, 0x7d,
    0xac, 0x60, 0x09, 0xc0, 0x40, 0xee, 0xb9, 0xeb, 0x13, 0x5b, 0xe8, 0x2b, 0xb1, 0x20, 0xf0, 0xce,
    0x4c, 0xbd, 0xc6, 0x04, 0x86, 0x70, 0xc6, 0x33, 0xc3, 0x15, 0x0f, 0x65, 0x19, 0xfd, 0xc2, 0xd3,

// map checksum goes here
    0x00, 0x00, 0x00, 0x00
};

/*
====================
COM_BlockSequenceCRCByte

For proxy protecting
====================
*/
byte COM_BlockSequenceCRCByte(const byte *base, int length, int sequence)
{
    unsigned short crc;
    const byte *p;
    byte chkb[60 + 4];

    p = chktbl + (sequence % (sizeof(chktbl) - 8));

    if (length > 60)
	length = 60;
    memcpy(chkb, base, length);

    chkb[length] = (sequence & 0xff) ^ p[0];
    chkb[length + 1] = p[1];
    chkb[length + 2] = ((sequence >> 8) & 0xff) ^ p[2];
    chkb[length + 3] = p[3];

    length += 4;

    crc = CRC_Block(chkb, length);

    crc &= 0xff;

    return crc;
}
