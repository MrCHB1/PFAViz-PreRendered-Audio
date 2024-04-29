#pragma once

#include "sdl2/SDL.h"
#include "bassmidi/basswasapi.h"
#include "MIDIAudio.h"

typedef struct CircularBufferStruct
{
	float* buffer;
	float* readPoint;
	float* writePoint;
} CircularBuffer;

static CircularBuffer g_preRenderBuffer;
static SDL_AudioSpec wanted;
static SDL_AudioSpec obtained;

extern MIDIAudio *PRE_MIDIAudio;

static double PRE_BufferSecs = 60.0;

static int PRE_BufferSize = 48000 * 2;
static int PRE_SampleRate = 48000;

extern void PRE_FillAudio(void* udata, Uint8* stream, int len);
extern void WrappedCopy(const Uint8* src, int pos, Uint8* dst, int pos2, int count);

static DWORD wasapi_frames = 30;

static int PRE_InitAudio()
{
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
	{
		MessageBoxW(NULL, L"Something happened.", L"Error", MB_ICONERROR);
		return 1;
	}

	g_preRenderBuffer.buffer = (float*)malloc(PRE_BufferSize * sizeof(float) * PRE_BufferSecs);
	memset(g_preRenderBuffer.buffer, 0, PRE_BufferSize * sizeof(float) * PRE_BufferSecs);
	g_preRenderBuffer.readPoint = g_preRenderBuffer.buffer;

	wanted.freq = PRE_SampleRate;
	wanted.format = AUDIO_F32;
	wanted.channels = 2;
	wanted.samples = 2048;
	wanted.padding = 0;
	wanted.callback = PRE_FillAudio;
	wanted.userdata = NULL;

	obtained.freq = PRE_SampleRate;
	obtained.format = AUDIO_F32;
	obtained.channels = 2;
	obtained.samples = 2048;
	obtained.padding = 0;
	obtained.callback = PRE_FillAudio;
	obtained.userdata = NULL;

	/*std::vector<MIDIChannelEvent> testEvents(2);
	testEvents[0].m_iEventCode = 0x90;
	testEvents[0].SetChannel(0);
	testEvents[0].SetAbsMicroSec(0);
	testEvents[0].SetParam1(36); testEvents[0].SetParam2(127);
	testEvents[1].m_iEventCode = 0x90;
	testEvents[1].SetChannel(0);
	testEvents[1].SetAbsMicroSec(1000000 / 60);
	testEvents[1].SetParam1(36); testEvents[1].SetParam2(127);
	PRE_MIDIAudio->m_bPaused = false;
	PRE_MIDIAudio->Start(0.0, (testEvents), 1.0);*/

	if (SDL_OpenAudio(&wanted, NULL) < 0)
	{
		return -1;
	}
	return 48000;
}

static void PRE_FillAudio(void* udata, Uint8* stream, int len)
{
	SDL_memset(stream, 0, len);
	PRE_MIDIAudio->m_asAudioStream.ReadLM(g_preRenderBuffer.buffer, 0, len / sizeof(float));
	SDL_memcpy(stream, (const Uint8*)g_preRenderBuffer.buffer, len);
	//g_preRenderBuffer.readPoint += len;
}

static void WrappedCopy(const Uint8* src, int pos, Uint8* dst, int pos2, int count)
{
	int ptr_len = sizeof(src);
	if (pos + count > ptr_len)
	{
		memcpy(dst + pos2, src + pos, (ptr_len - pos));
		count -= ptr_len - pos;
	}
	memcpy(dst + pos2, src + pos, count);
}

static void PRE_Reset()
{
	//SDL_CloseAudio();
	memset(g_preRenderBuffer.buffer, 0, PRE_BufferSize * sizeof(float) * PRE_BufferSecs);
	g_preRenderBuffer.readPoint = g_preRenderBuffer.buffer;
}