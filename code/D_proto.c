#include "m_alloc.h"
#include "i_system.h"
#include "d_proto.h"
#include "d_ticcmd.h"
#include "d_net.h"
#include "doomdef.h"
#include "doomstat.h"
#include "cmdlib.h"
#include "z_zone.h"

demoqueue_t DemoQueue[MAXPLAYERS];


char *ReadString (byte **stream)
{
	char *string = *stream;

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

void WriteString (const char *string, byte **stream)
{
	char *p = *stream;

	while (*string) {
		*p++ = *string++;
	}

	*p++ = 0;
	*stream = p;
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

// Returns the number of bytes read
int UnpackUserCmd (usercmd_t *ucmd, byte **stream)
{
	byte *start = *stream;
	byte flags;
	byte flags2;

	// make sure the ucmd is empty
	memset (ucmd, 0, sizeof(usercmd_t));

	flags = ReadByte (stream);
	if (flags & UCMDF_MORE)
		flags2 = ReadByte (stream);
	else
		flags2 = 0;

	if (flags) {
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
		if (flags & UCMDF_IMPULSE)
			ucmd->impulse = ReadByte (stream);

		if (flags2) {
			if (flags2 & UCMDF_ROLL)
				ucmd->roll = ReadWord (stream);
			if (flags2 & UCMDF_USE)
				ucmd->use = ReadByte (stream);
		}
	}

	return *stream - start;
}

// Returns the number of bytes written
int PackUserCmd (usercmd_t *ucmd, byte **stream)
{
	byte flags = 0;
	byte flags2 = 0;
	byte *temp = *stream;
	byte *start = *stream;

	WriteByte (0, stream);			// Make room for the packing bits
	if (ucmd->roll || ucmd->use) {
		flags |= UCMDF_MORE;
		WriteByte (0, stream);		// Make room for more packing bits
	}

	if (ucmd->buttons) {
		flags |= UCMDF_BUTTONS;
		WriteByte (ucmd->buttons, stream);
	}
	if (ucmd->pitch) {
		flags |= UCMDF_PITCH;
		WriteWord (ucmd->pitch, stream);
	}
	if (ucmd->yaw) {
		flags |= UCMDF_YAW;
		WriteWord (ucmd->yaw, stream);
	}
	if (ucmd->forwardmove) {
		flags |= UCMDF_FORWARDMOVE;
		WriteWord (ucmd->forwardmove, stream);
	}
	if (ucmd->sidemove) {
		flags |= UCMDF_SIDEMOVE;
		WriteWord (ucmd->sidemove, stream);
	}
	if (ucmd->upmove) {
		flags |= UCMDF_UPMOVE;
		WriteWord (ucmd->upmove, stream);
	}
	if (ucmd->impulse) {
		flags |= UCMDF_IMPULSE;
		WriteByte (ucmd->impulse, stream);
	}

	if (ucmd->roll) {
		flags2 |= UCMDF_ROLL;
		WriteWord (ucmd->roll, stream);
	}
	if (ucmd->use) {
		flags2 |= UCMDF_USE;
		WriteByte (ucmd->use, stream);
	}

	// Write the packing bits
	WriteByte (flags, &temp);
	if (flags2)
		WriteByte (flags2, &temp);

	return *stream - start;
}

int WriteUserCmdMessage (usercmd_t *ucmd, byte **stream)
{
	WriteByte (DEM_USERCMD, stream);
	return PackUserCmd (ucmd, stream) + 1;
}


int SkipTicCmd (byte **stream, int count)
{
	int i;
	byte *flow = *stream;
	int skip;
	int type;
	BOOL moreticdata;

	for (i = count; i > 0; i--) {
		flow += 2;		// Skip consistancy marker

		moreticdata = true;

		while (moreticdata) {
			type = *flow++;

			switch (type) {
				case DEM_MUSICCHANGE:
				case DEM_PRINT:
				case DEM_CENTERPRINT:
				case DEM_UINFCHANGED:
				case DEM_SINFCHANGED:
				case DEM_GIVECHEAT:
				case DEM_CHANGEMAP:
					skip = strlen (flow) + 1;
					break;

				case DEM_GENERICCHEAT:
				case DEM_DROPPLAYER:
					skip = 1;
					break;

				case DEM_SAY:
					skip = 2 + strlen (flow + 1);
					break;

				case DEM_USERCMD:
					moreticdata = false;
					skip = 1;
					if (*flow & UCMDF_BUTTONS)		skip += 1;
					if (*flow & UCMDF_PITCH)		skip += 2;
					if (*flow & UCMDF_YAW)			skip += 2;
					if (*flow & UCMDF_FORWARDMOVE)	skip += 2;
					if (*flow & UCMDF_SIDEMOVE)		skip += 2;
					if (*flow & UCMDF_UPMOVE)		skip += 2;
					if (*flow & UCMDF_IMPULSE)		skip += 1;

					if (*flow & UCMDF_MORE) {
						flow++;
						if (*flow & UCMDF_ROLL)		skip += 2;
						if (*flow & UCMDF_USE)		skip += 1;
					}
					break;
			
				default:
					// All other commands are assumed to have zero parameters
					skip = 0;
					break;
			}
			flow += skip;
		}
	}

	skip = flow - *stream;
	*stream = flow;

	return skip;
}

void ReadTicCmd (ticcmd_t *tcmd, byte **stream, int player)
{
	int type, queuelen;
	byte *start;

	tcmd->consistancy = ReadWord (stream);

	start = *stream;

	while ((type = ReadByte (stream)) != DEM_USERCMD)
		Net_DoCommand (type, stream, player);

	// Save special "ticcmds" for later saving to a demo
	if (demorecording && ((queuelen = *stream - start - 1) > 0)) {
		if (DemoQueue[player].queue)
			Z_Free (DemoQueue[player].queue);

		DemoQueue[player].queue = Z_Malloc (queuelen, PU_STATIC, NULL);
		memcpy (DemoQueue[player].queue, start, queuelen);
		DemoQueue[player].size = queuelen;
	}

	UnpackUserCmd (&tcmd->ucmd, stream);
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