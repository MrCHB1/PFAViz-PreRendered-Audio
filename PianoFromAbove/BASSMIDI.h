#pragma once

#include "bassmidi/bass.h"
#include "bassmidi/bassmidi.h"
#include <mutex>

class BASSMIDI
{
public:
	HSTREAM m_hsHandle;
	//static WAVEFORMATEX m_wfWaveFormatStatic;

	bool m_bCanSeek;
	long long m_llPosition;
	long long m_llLength;

	static void InitBASS(WAVEFORMATEX format);
	static void DisposeBASS() { BASS_Free(); }

	BASSMIDI(int voices, bool nofx);
	~BASSMIDI() { BASS_StreamFree(m_hsHandle); }

	static void FreeSoundfont();
	static void LoadSoundfont(const wchar_t* path);

	bool WriteBass(int buflen, unsigned long* progress);
	float* WriteFloatArray(int buflen, unsigned long* progress);
	int KShortMessage(int dwParm1, int sampleoffset);
	DWORD SendEvent(DWORD type, DWORD param, DWORD chan, DWORD tick, DWORD time)
	{
		BASS_MIDI_EVENT ev = {
			type = type,
			param = param,
			chan = chan,
			tick = tick,
			time = time << 3
		};
		DWORD mode = BASS_MIDI_EVENTS_TIME | BASS_MIDI_EVENTS_STRUCT;
		return BASS_MIDI_StreamEvents(m_hsHandle, mode, &ev, 1);
	}

	DWORD SendEventRaw(BYTE data[], DWORD channel)
	{
		return BASS_MIDI_StreamEvents(m_hsHandle, BASS_MIDI_EVENTS_RAW | BASS_MIDI_EVENTS_NORSTATUS, data, sizeof(data)/sizeof(BYTE));
	}

	DWORD BassStreamEvents(BASS_MIDI_EVENT events[]) {
		DWORD mode = BASS_MIDI_EVENTS_TIME | BASS_MIDI_EVENTS_STRUCT;
		return BASS_MIDI_StreamEvents(m_hsHandle, mode, events, sizeof(events) / sizeof(BASS_MIDI_EVENT));
	}

	DWORD Read(float buffer[], int offset, int count);
};