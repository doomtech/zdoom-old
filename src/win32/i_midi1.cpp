#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "i_music.h"
#include "w_wad.h"

static HANDLE	BufferReturnEvent;
static DWORD	midivolume;
static DWORD	nummididevices;
static bool		nummididevicesset;
static UINT		mididevice;

//==========================================================================
//
// CVAR snd_midivolume
//
// Maximum volume of MIDI/MUS music through the MM subsystem.
//==========================================================================

CUSTOM_CVAR (Float, snd_midivolume, 0.5f, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
{
	if (self < 0.f)
		self = 0.f;
	else if (self > 1.f)
		self = 1.f;
	else
	{
		DWORD onechanvol = (DWORD)(self * 65535.f);
		midivolume = (onechanvol << 16) | onechanvol;
		if (currSong && currSong->IsMIDI ())
		{
			currSong->SetVolume (self);
		}
	}
}

CVAR (Bool, snd_midiprecache, true, CVAR_ARCHIVE|CVAR_GLOBALCONFIG);

void MIDISong::SetVolume (float volume)
{
	SetStreamVolume ();
}

void MIDISong::SetStreamVolume ()
{
	DWORD vol;
	MMRESULT ret;

	ret = midiOutGetVolume ((HMIDIOUT)mididevice, &vol);
	if (m_bVolGood)
	{
		// Check the last set vol against the current volume.
		// If they're different, then the the current volume
		// becomes the volume to set when we're done.
		if (ret == MMSYSERR_NOERROR)
		{
			if (vol != m_LastSetVol)
			{
				m_OldVolume = vol;
				m_bOldVolGood = true;
			}
		}
		else
		{
			m_bOldVolGood = false;
		}
	}
	else
	{
		// The volume hasn't been set yet, so remember the
		// current volume so that we can return to it later.
		if (ret == MMSYSERR_NOERROR)
		{
			m_OldVolume = vol;
			m_bOldVolGood = true;
		}
		else
		{
			m_bOldVolGood = false;
		}
	}
	// Now set the new volume.
	ret = midiOutSetVolume ((HMIDIOUT)mididevice, midivolume);
	if (ret == MMSYSERR_NOERROR)
	{
		m_LastSetVol = midivolume;
		m_bVolGood = true;
	}
	else
	{
		char err[MAXERRORLENGTH];
		midiOutGetErrorText (ret, err, MAXERRORLENGTH);
		Printf (PRINT_BOLD, "Could not set MIDI volume:\n%s\n", err);
		m_bVolGood = false;
	}
}

void MIDISong::UnsetStreamVolume ()
{
	if (m_bVolGood && m_bOldVolGood)
	{
		midiOutSetVolume ((HMIDIOUT)mididevice, m_OldVolume);
		m_bVolGood = false;
		m_bOldVolGood = false;
	}
}

void MIDISong::MCIError (MMRESULT res, const char *descr)
{
	char errorStr[MAXERRORLENGTH];

	mciGetErrorString (res, errorStr, MAXERRORLENGTH);
	Printf (PRINT_BOLD, "An error occured while %s:\n", descr);
	Printf ("%s\n", errorStr);
}

void MIDISong::UnprepareHeaders ()
{
	PSTREAMBUF buffer = m_Buffers;

	while (buffer)
	{
		if (buffer->prepared)
		{
			MMRESULT res = midiOutUnprepareHeader ((HMIDIOUT)m_MidiStream,
				&buffer->midiHeader, sizeof(buffer->midiHeader));
			if (res != MMSYSERR_NOERROR)
				MCIError (res, "unpreparing headers");
			else
				buffer->prepared = false;
		}
		buffer = buffer->pNext;
	}
}

bool MIDISong::PrepareHeaders ()
{
	MMRESULT res;
	PSTREAMBUF buffer = m_Buffers;

	while (buffer)
	{
		if (!buffer->prepared)
		{
			memset (&buffer->midiHeader, 0, sizeof(MIDIHDR));
			buffer->midiHeader.lpData = (char *)buffer->pBuffer;
			buffer->midiHeader.dwBufferLength = CB_STREAMBUF;
			buffer->midiHeader.dwBytesRecorded = CB_STREAMBUF - buffer->cbLeft;
			res = midiOutPrepareHeader ((HMIDIOUT)m_MidiStream,
										&buffer->midiHeader, sizeof(MIDIHDR));
			if (res != MMSYSERR_NOERROR)
			{
				MCIError (res, "preparing headers");
				UnprepareHeaders ();
				return false;
			} else
				buffer->prepared = true;
		}
		buffer = buffer->pNext;
	}

	return true;
}

void MIDISong::SubmitBuffer ()
{
	MMRESULT res;

	res = midiStreamOut (m_MidiStream,
		 &m_CurrBuffer->midiHeader, sizeof(MIDIHDR));

	if (res != MMSYSERR_NOERROR)
		MCIError (res, "sending MIDI stream");

	m_CurrBuffer = m_CurrBuffer->pNext;
	if (m_CurrBuffer == NULL && m_Looping)
		m_CurrBuffer = m_Buffers;
}

void MIDISong::AllChannelsOff ()
{
	int i;

	for (i = 0; i < 16; i++)
		midiOutShortMsg ((HMIDIOUT)m_MidiStream, MIDI_NOTEOFF | i | (64<<16) | (60<<8));
	Sleep (1);
}

static void CALLBACK MidiProc (HMIDIIN hMidi, UINT uMsg, MIDISong *info,
							  DWORD dwParam1, DWORD dwParam2)
{
	info->MidiProc (uMsg);
}

void MIDISong::MidiProc (UINT uMsg)
{
	if (m_CallbackStatus == cb_dead)
		return;

	switch (uMsg)
	{
	case MOM_DONE:
		if (m_CallbackStatus == cb_die)
		{
			SetEvent (BufferReturnEvent);
			m_CallbackStatus = cb_dead;
		}
		else 
		{
			if (m_CurrBuffer == m_Buffers)
			{
				// Stop all notes before restarting the song
				// in case any are left hanging.
				AllChannelsOff ();
			}
			else if (m_CurrBuffer == NULL) 
			{
				SetEvent (BufferReturnEvent);
				return;
			}
			SubmitBuffer ();
		}
		break;
	}
}

void MIDISong::Play (bool looping)
{
	m_Status = STATE_Stopped;
	m_Looping = looping;

	MMRESULT res;

	// note: midiStreamOpen changes mididevice if it's set to MIDI_MAPPER
	// (interesting undocumented behavior)
	if ((res = midiStreamOpen (&m_MidiStream,
							   &mididevice,
							   (DWORD)1,
							   (DWORD_PTR)::MidiProc, (DWORD_PTR)this,
							   CALLBACK_FUNCTION)) == MMSYSERR_NOERROR)
	{
		MIDIPROPTIMEDIV timedivProp;

		timedivProp.cbStruct = sizeof(timedivProp);
		timedivProp.dwTimeDiv = midTimeDiv;
		res = midiStreamProperty (m_MidiStream, (LPBYTE)&timedivProp,
								  MIDIPROP_SET | MIDIPROP_TIMEDIV);
		if (res != MMSYSERR_NOERROR)
			MCIError (res, "setting time division");

		SetStreamVolume ();

		// Preload all instruments into soundcard RAM (if necessary).
		// On my GUS PnP, this is necessary because it will fail to
		// play some instruments on some songs until the song loops
		// if the instrument isn't already in memory. (Why it doesn't
		// load them when they're needed is beyond me, because it's only
		// some instruments it doesn't play properly the first time--and
		// I don't know exactly which ones those are.) The 250 ms delay
		// between note on and note off is fairly lengthy, so I try and
		// get the instruments going on multiple channels to reduce the
		// number of times I have to sleep.
		if (snd_midiprecache)
		{
			int i, j;

			DPrintf ("MIDI uses instruments:\n");
			for (i = j = 0; i < 127; i++)
			{
				if (UsedPatches[i])
				{
					DPrintf (" %d", i);
					res = midiOutShortMsg ((HMIDIOUT)m_MidiStream,
									 MIDI_PRGMCHANGE | (i<<8) | j);
					res = midiOutShortMsg ((HMIDIOUT)m_MidiStream,
									 MIDI_NOTEON | (60<<8) | (1<<16) | j);
					++j;
					if (j == 10)
					{
						++j;
					}
					else if (j == 16)
					{
						Sleep (250);
						for (j = 0; j < 16; j++)
						{
							if (j != 10)
							{
								midiOutShortMsg ((HMIDIOUT)m_MidiStream,
												 MIDI_NOTEOFF | (60<<8) | (64<<16) | j);
							}
						}
						j = 0;
					}
				}
			}
			if (j > 0)
			{
				Sleep (250);
				for (i = 0; i < j; i++)
				{
					if (i != 10)
					{
						midiOutShortMsg ((HMIDIOUT)m_MidiStream,
										 MIDI_NOTEOFF | (60<<8) | (64<<16) | i);
					}
				}
			}

		/*
			DPrintf ("\nMIDI uses percussion keys:\n");
			for (i = 0; i < 127; i++)
				if (UsedPatches[i+128]) {
					DPrintf (" %d", i);
					midiOutShortMsg ((HMIDIOUT)info->midiStream,
									 MIDI_NOTEON | (i<<8) | (1<<16) | 10);
					Sleep (235);
					midiOutShortMsg ((HMIDIOUT)info->midiStream,
									 MIDI_NOTEOFF | (i<<8) | (64<<16));
				}
		*/
			DPrintf ("\n");
		}

		if (PrepareHeaders ())
		{
			m_CallbackStatus = cb_play;
			m_CurrBuffer = m_Buffers;
			SubmitBuffer ();
			res = midiStreamRestart (m_MidiStream);
			if (res == MMSYSERR_NOERROR)
			{
				m_Status = STATE_Playing;
			}
			else
			{
				MCIError (res, "starting playback");
				UnprepareHeaders ();
				midiStreamClose (m_MidiStream);
				m_MidiStream = NULL;
				m_Status = STATE_Stopped;
			}
		}
		else
		{
			UnprepareHeaders ();
			midiStreamClose (m_MidiStream);
			m_MidiStream = NULL;
			m_Status = STATE_Stopped;
		}
	}
	else
	{
		MCIError (res, "opening MIDI stream");
		if (snd_mididevice != -1)
		{
			Printf ("Trying again with MIDI mapper\n");
			snd_mididevice = -1;
		}
		else
		{
			m_Status = STATE_Stopped;
		}
	}
}

void MIDISong::Pause ()
{
	if (m_Status == STATE_Playing &&
		m_MidiStream &&
		midiStreamPause (m_MidiStream) == MMSYSERR_NOERROR)
	{
		m_Status = STATE_Paused;
	}
}

void MIDISong::Resume ()
{
	if (m_Status == STATE_Paused &&
		m_MidiStream &&
		midiStreamRestart (m_MidiStream) == MMSYSERR_NOERROR)
	{
		m_Status = STATE_Playing;
	}
}

void MIDISong::Stop ()
{
	if (m_Status != STATE_Stopped && m_MidiStream)
	{
		if (m_CallbackStatus != cb_dead)
			m_CallbackStatus = cb_die;
		midiStreamStop (m_MidiStream);
		WaitForSingleObject (BufferReturnEvent, 5000);
		midiOutReset ((HMIDIOUT)m_MidiStream);
		UnprepareHeaders ();
		UnsetStreamVolume ();
		midiStreamClose (m_MidiStream);
		m_MidiStream = NULL;
	}
	m_Status = STATE_Stopped;
}

MIDISong::~MIDISong ()
{
	Stop ();
	if (m_IsMUS)
		mus2strmCleanup ();
	else
		mid2strmCleanup ();
	m_Buffers = NULL;
}

MIDISong::MIDISong ()
: m_IsMUS (false), m_Buffers (NULL), m_bVolGood (false)
{
}

MIDISong::MIDISong (const void *mem, int len)
{
	m_Buffers = NULL;
	m_IsMUS = false;
	if (nummididevices > 0)
	{
		m_Buffers = mid2strmConvert ((LPBYTE)mem, len);
	}
}

MUSSong::MUSSong (const void *mem, int len) : MIDISong ()
{
	m_Buffers = NULL;
	m_IsMUS = true;
	if (nummididevices > 0)
	{
		m_Buffers = mus2strmConvert ((LPBYTE)mem, len);
	}
}

bool MIDISong::IsPlaying ()
{
	return m_Looping ? true : m_Status != STATE_Stopped;
}
