#include "m_alloc.h"
#include "i_system.h"
#include "d_protocol.h"
#include "d_ticcmd.h"
#include "d_net.h"
#include "doomdef.h"
#include "doomstat.h"
#include "cmdlib.h"
#include "z_zone.h"


char *ReadString (byte **stream)
{
	char *string = *((char **)stream);

	*stream += strlen (string) + 1;
	return copystring (string);
}

int ReadByte (byte **stream)
{
	byte v = **stream;
	*stream += 1;
	return v;
}

int ReadWord (byte **stream)
{
	short v = (((*stream)[0]) << 8) | (((*stream)[1]));
	*stream += 2;
	return v;
}

int ReadLong (byte **stream)
{
	int v = (((*stream)[0]) << 24) | (((*stream)[1]) << 16) | (((*stream)[2]) << 8) | (((*stream)[3]));
	*stream += 4;
	return v;
}

float ReadFloat (byte **stream)
{
	int fakeint = ReadLong (stream);
	return *((float *)&fakeint);
}

void WriteString (const char *string, byte **stream)
{
	char *p = *((char **)stream);

	while (*string) {
		*p++ = *string++;
	}

	*p++ = 0;
	*stream = (byte *)p;
}


void WriteByte (byte v, byte **stream)
{
	**stream = v;
	*stream += 1;
}

void WriteWord (short v, byte **stream)
{
	(*stream)[0] = v >> 8;
	(*stream)[1] = v & 255;
	*stream += 2;
}

void WriteLong (int v, byte **stream)
{
	(*stream)[0] = v >> 24;
	(*stream)[1] = (v >> 16) & 255;
	(*stream)[2] = (v >> 8) & 255;
	(*stream)[3] = v & 255;
	*stream += 4;
}

void WriteFloat (float v, byte **stream)
{
	WriteLong (*((int *)&v), stream);
}

// Returns the number of bytes read
int UnpackUserCmd (usercmd_t *ucmd, const usercmd_t *basis, byte **stream)
{
	byte *start = *stream;
	byte flags;

	if (basis != NULL)
		memcpy (ucmd, basis, sizeof(usercmd_t));
	else
		memset (ucmd, 0, sizeof(usercmd_t));

	flags = ReadByte (stream);

	if (flags)
	{
		if (flags & UCMDF_BUTTONS)
			ucmd->buttons = ReadByte (stream);
		if (flags & UCMDF_PITCH)
			ucmd->pitch = ReadWord (stream);
		if (flags & UCMDF_YAW)
			ucmd->yaw = ReadWord (stream);
		if (flags & UCMDF_FORWARDMOVE)
			ucmd->forwardmove = ReadWord (stream);
		if (flags & UCMDF_SIDEMOVE)
			ucmd->sidemove = ReadWord (stream);
		if (flags & UCMDF_UPMOVE)
			ucmd->upmove = ReadWord (stream);
		if (flags & UCMDF_ROLL)
			ucmd->roll = ReadWord (stream);
	}

	return *stream - start;
}

// Returns the number of bytes written
int PackUserCmd (const usercmd_t *ucmd, const usercmd_t *basis, byte **stream)
{
	byte flags = 0;
	byte *temp = *stream;
	byte *start = *stream;
	usercmd_t blank;

	if (basis == NULL)
	{
		memset (&blank, 0, sizeof(blank));
		basis = &blank;
	}

	WriteByte (0, stream);			// Make room for the packing bits

	if (ucmd->buttons != basis->buttons)
	{
		flags |= UCMDF_BUTTONS;
		WriteByte (ucmd->buttons, stream);
	}
	if (ucmd->pitch != basis->pitch)
	{
		flags |= UCMDF_PITCH;
		WriteWord (ucmd->pitch, stream);
	}
	if (ucmd->yaw != basis->yaw)
	{
		flags |= UCMDF_YAW;
		WriteWord (ucmd->yaw, stream);
	}
	if (ucmd->forwardmove != basis->forwardmove)
	{
		flags |= UCMDF_FORWARDMOVE;
		WriteWord (ucmd->forwardmove, stream);
	}
	if (ucmd->sidemove != basis->sidemove)
	{
		flags |= UCMDF_SIDEMOVE;
		WriteWord (ucmd->sidemove, stream);
	}
	if (ucmd->upmove != basis->upmove)
	{
		flags |= UCMDF_UPMOVE;
		WriteWord (ucmd->upmove, stream);
	}
	if (ucmd->roll != basis->roll)
	{
		flags |= UCMDF_ROLL;
		WriteWord (ucmd->roll, stream);
	}

	// Write the packing bits
	WriteByte (flags, &temp);

	return *stream - start;
}

FArchive &operator<< (FArchive &arc, usercmd_t &cmd)
{
	byte bytes[256];
	byte *stream = bytes;
	if (arc.IsStoring ())
	{
		BYTE len = PackUserCmd (&cmd, NULL, &stream);
		arc << len;
		arc.Write (bytes, len);
	}
	else
	{
		BYTE len;
		arc << len;
		arc.Read (bytes, len);
		UnpackUserCmd (&cmd, NULL, &stream);
	}
	return arc;
}

int WriteUserCmdMessage (usercmd_t *ucmd, const usercmd_t *basis, byte **stream)
{
	WriteByte (DEM_USERCMD, stream);
	return PackUserCmd (ucmd, basis, stream) + 1;
}


int SkipTicCmd (byte **stream, int count)
{
	int i, skip;
	byte *flow = *stream;

	for (i = count; i > 0; i--)
	{
		bool moreticdata = true;

		flow += 2;		// Skip consistancy marker
		while (moreticdata)
		{
			byte type = *flow++;

			if (type == DEM_USERCMD)
			{
				moreticdata = false;
				skip = 1;
				if (*flow & UCMDF_BUTTONS)		skip += 1;
				if (*flow & UCMDF_PITCH)		skip += 2;
				if (*flow & UCMDF_YAW)			skip += 2;
				if (*flow & UCMDF_FORWARDMOVE)	skip += 2;
				if (*flow & UCMDF_SIDEMOVE)		skip += 2;
				if (*flow & UCMDF_UPMOVE)		skip += 2;
				if (*flow & UCMDF_ROLL)			skip += 2;
				flow += skip;
			}
			else
			{
				Net_SkipCommand (type, &flow);
			}
		}
	}

	skip = flow - *stream;
	*stream = flow;

	return skip;
}

void ReadTicCmd (byte **stream, int player, int tic)
{
	int type;
	byte *start;
	ticcmd_t *tcmd;

	tic %= BACKUPTICS;

	tcmd = &netcmds[player][tic];
	tcmd->consistancy = ReadWord (stream);

	start = *stream;

	while ((type = ReadByte (stream)) != DEM_USERCMD)
		Net_SkipCommand (type, stream);

	NetSpecs[player][tic].SetData (start, *stream - start - 1);
	UnpackUserCmd (&tcmd->ucmd,
		tic ? &netcmds[player][(tic-1)%BACKUPTICS].ucmd : NULL, stream);
}

void RunNetSpecs (int player, int buf)
{
	byte *stream;
	int len;

	if (gametic % ticdup == 0)
	{
		stream = NetSpecs[player][buf].GetData (&len);
		if (stream)
		{
			byte *end = stream + len;
			while (stream < end)
			{
				int type = ReadByte (&stream);
				Net_DoCommand (type, &stream, player);
			}
			if (!demorecording)
				NetSpecs[player][buf].SetData (NULL, 0);
		}
	}
}

byte *lenspot;

// Write the header of an IFF chunk and leave space
// for the length field.
void StartChunk (int id, byte **stream)
{
	WriteLong (id, stream);
	lenspot = *stream;
	*stream += 4;
}

// Write the length field for the chunk and insert
// pad byte if the chunk is odd-sized.
void FinishChunk (byte **stream)
{
	int len;
	
	if (!lenspot)
		return;

	len = *stream - lenspot - 4;
	WriteLong (len, &lenspot);
	if (len & 1)
		WriteByte (0, stream);

	lenspot = NULL;
}

// Skip past an unknown chunk. *stream should be
// pointing to the chunk's length field.
void SkipChunk (byte **stream)
{
	int len;

	len = ReadLong (stream);
	stream += len + (len & 1);
}