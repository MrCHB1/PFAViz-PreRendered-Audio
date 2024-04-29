#include "BASSMIDI.h"
#include <algorithm>
#include <vector>
#include <sstream>

static WAVEFORMATEX m_wfWaveFormatStatic = WAVEFORMATEX{};
static BASS_MIDI_FONTEX* m_bmFontArr = (BASS_MIDI_FONTEX*)malloc(sizeof(BASS_MIDI_FONTEX));
static std::mutex sfLock;
static std::mutex m_bmMutex;

void BASSMIDI::InitBASS(WAVEFORMATEX format) {
	m_wfWaveFormatStatic = format;
	BASS_Free();
	if (!BASS_Init(0, m_wfWaveFormatStatic.nSamplesPerSec, BASS_DEVICE_NOSPEAKER, NULL, NULL))
		MessageBoxW(NULL, L"BASSMIDI failed to initialize, proceeding without audio.\0", L"BASSMIDI Error", MB_ICONERROR);
}

BASSMIDI::BASSMIDI(int voices, bool nofx = true) {
	m_hsHandle = BASS_MIDI_StreamCreate(16,
		BASS_SAMPLE_FLOAT |
		BASS_STREAM_DECODE |
		BASS_MIDI_SINCINTER |
		BASS_MIDI_NOTEOFF1,
		m_wfWaveFormatStatic.nSamplesPerSec);

	if (m_hsHandle == -1)
	{
		int err = BASS_ErrorGetCode();
		MessageBoxW(NULL, L"BASSMIDI Handle failed to load!\0", L"Error\0", MB_ICONERROR);
	}

	BASS_ChannelSetAttribute(m_hsHandle, BASS_ATTRIB_MIDI_VOICES, voices);
	BASS_ChannelSetAttribute(m_hsHandle, BASS_ATTRIB_SRC, 3);
	BASS_ChannelSetAttribute(m_hsHandle, BASS_ATTRIB_MIDI_CHANS, 16);
	//BASS_SetConfig(BASS_CONFIG_FLOATDSP, TRUE);

	if (nofx) BASS_ChannelFlags(m_hsHandle, BASS_MIDI_NOFX, BASS_MIDI_NOFX);

	{
		sfLock.lock();
		BASS_MIDI_StreamSetFonts(m_hsHandle, m_bmFontArr, 1);
		sfLock.unlock();
	}
	
}

// only one soundfont because i'm lazy lmfao
void BASSMIDI::FreeSoundfont()
{
	if (m_bmFontArr != NULL)
	{
		BASS_MIDI_FontFree((*m_bmFontArr).font);
		m_bmFontArr = NULL;
	}
}

void BASSMIDI::LoadSoundfont(const wchar_t* path)
{
	{
		sfLock.lock();
		FreeSoundfont();
		BASS_MIDI_FONTEX* fonts = (BASS_MIDI_FONTEX*)malloc(sizeof(BASS_MIDI_FONTEX));

		// one soundfont due to laziness
		HSOUNDFONT font = BASS_MIDI_FontInit(path, 0);
		if (font != 0)
		{
			fonts[0].font = font;
			fonts[0].spreset = -1;
			fonts[0].sbank = -1;
			fonts[0].dpreset = -1;
			fonts[0].dbank = 0;
			fonts[0].dbanklsb = 0;

			BASS_MIDI_FontLoad(font, -1, -1);
		}
		else
		{
			std::wstringstream err;
			err << L"Soudfont failed to load! Err Code: " << BASS_ErrorGetCode() << "\0";
			MessageBoxW(NULL, err.str().c_str(), L"Error\0", MB_ICONERROR);
		}

		m_bmFontArr = fonts;

		//BASS_MIDI_StreamSetFonts(m_hsHandle, &fonts[0], 1);

		sfLock.unlock();
	}
}

bool BASSMIDI::WriteBass(int buflen, unsigned long *progress)
{
	buflen <<= 3;
	unsigned char* buf;

	DWORD ret = BASS_ChannelGetData(m_hsHandle, &buf, buflen);
	if (ret > 0)
	{
		(*progress) += (unsigned int)ret;
		//
		return true;
	}
	else
	{
		int err = BASS_ErrorGetCode();
		return false;
	}
}

float* BASSMIDI::WriteFloatArray(int buflen, unsigned long* progress)
{
	unsigned char* buf = (unsigned char*)malloc(buflen * 4 * sizeof(unsigned char));
	float* flt = (float*)malloc(buflen * sizeof(float));

	DWORD ret = BASS_ChannelGetData(m_hsHandle, &buf, buflen * 4);
	if (ret > 0) {
		(*progress) += (unsigned int)ret;
		memcpy(flt, buf, sizeof(buf));
		return flt;
	}
	else
	{
		int err = BASS_ErrorGetCode();
		return nullptr;
	}
}

int BASSMIDI::KShortMessage(int dwParam1, int sampleoffset)
{
	if ((unsigned char)dwParam1 == 0xFF) return 1;

	unsigned char cmd = (unsigned char)dwParam1;

	BASS_MIDI_EVENT ev;

	if (cmd < 0xA0)
	{

		ev.event = MIDI_EVENT_NOTE;
		ev.param = cmd < 0x90 ? (unsigned char)(dwParam1 >> 8) : (unsigned short)(dwParam1 >> 8);
		ev.chan = (int)dwParam1 & 0xF;
		ev.tick = 0;
		ev.pos = sampleoffset << 3;
	}
	else if (cmd < 0xB0)
	{
		ev.event = MIDI_EVENT_KEYPRES;
		ev.param = (unsigned short)dwParam1 >> 8;
		ev.chan = (int)dwParam1 & 0xF;
		ev.tick = 0;
		ev.pos = sampleoffset << 3;
	}
	else if (cmd < 0xC0)
	{
		// TODO
		return 0;
	}
	else if (cmd < 0xD0)
	{
		ev.event = MIDI_EVENT_PROGRAM;
		ev.param = (unsigned char)(dwParam1 >> 8);
		ev.chan = (int)dwParam1 & 0xF;
		ev.tick = 0;
		ev.pos = sampleoffset << 3;
	}
	else if (cmd < 0xE0)
	{
		ev.event = MIDI_EVENT_CHANPRES;
		ev.param = (unsigned char)(dwParam1 >> 8);
		ev.chan = (int)dwParam1 & 0xF;
		ev.tick = 0;
		ev.pos = sampleoffset << 3;
	}
	else if (cmd == 0xF0)
	{
		ev.event = MIDI_EVENT_PITCH;
		ev.param = (int)((unsigned char)(dwParam1 >> 16) | ((dwParam1 & 0x7F00) >> 1));
		ev.chan = (int)dwParam1 & 0xF;
		ev.tick = 0;
		ev.pos = sampleoffset << 3;
	}
	else return 0;

	BASS_MIDI_EVENT evs[1] = { ev };

	BassStreamEvents(evs);

	return 0;
}

DWORD BASSMIDI::Read(float* buffer, int offset, int count) {
	DWORD size = count * sizeof(float);
	DWORD ret = BASS_ChannelGetData(m_hsHandle, buffer + offset, size | BASS_DATA_FLOAT);

	if (ret == 0)
	{
		int err = BASS_ErrorGetCode();
		MessageBox(NULL, L"Error\0", L"Error\0", MB_ICONERROR);
	}
	return ret / 4;
}