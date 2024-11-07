#pragma once

#include "bassmidi/bass.h"
#include "bassmidi/bassmidi.h"
#include "BASSMIDI.h"
#include "Misc.h"
#include "resource.h"
#include "MIDI.h"
#include <chrono>
#include <mutex>

extern std::atomic<double> g_preVolume;

class MIDIAudio;

class AudioBufferStream {
public:
	bool m_bCanSeek;
	WAVEFORMAT m_wfWaveFormat;
	long long m_llPosition;
	long long m_llLength;

	AudioBufferStream(MIDIAudio* source);
	~AudioBufferStream() {};

	int Read(float* buffer, int offset, int count);
	int ReadLM(float* buffer, int offset, int count);

private:
	MIDIAudio* m_maAudioSource;
};

class MIDIAudio {
public:
	int m_iDefaultVoices = 1000;
	bool m_bDefaultNoFx = false;
	double m_dSimulatedLagScale = 0.01;
	double m_dStartTime = 0;
	double m_dFPS = 0.0;
	double m_dInstability = 1.0;
	int m_iSkippingVelocity;
	float* m_fAudioBuffer;

	double m_dAttack = 1.0;
	double m_dRelease = 0.005;

	int m_iVelThreshLow = 0;
	int m_iVelThreshUpp = 127;

	double m_dBufferSeconds = max(0, m_iBufferWritePos - m_iBufferReadPos) / 48000.0;
	double m_dPlayerTime = m_dStartTime + (double)m_iBufferReadPos / 48000.0;

	static WAVEFORMATEX m_wfFormat;
	std::vector<MIDIChannelEvent*> m_vEvents;

	bool m_bPaused = true;
	bool m_bRequestedAudioCancel = false;

	friend class AudioBufferStream;
	AudioBufferStream m_asAudioStream;

	std::thread* m_tGeneratorThread;

	int GetSkippingVelocity()
	{
		if (m_bPaused) return 0;
		int diff = 127 + 10 - (m_iBufferWritePos - m_iBufferReadPos) / 100;
		if (diff > 127) diff = 127;
		if (diff < 0) diff = 0;
		return diff;
	}

	static void Init()
	{
		m_wfFormat.nSamplesPerSec = 48000;
		m_wfFormat.nChannels = 2;
		m_wfFormat.nBlockAlign = (2 * 8) / 8;
		m_wfFormat.wBitsPerSample = 8;
		m_wfFormat.nAvgBytesPerSec = m_wfFormat.nSamplesPerSec * (2 * 8) / 8;
		m_wfFormat.wFormatTag = WAVE_FORMAT_PCM;
		BASSMIDI::InitBASS(m_wfFormat);
	}

	MIDIAudio(int bufferLength);
	~MIDIAudio()
	{
		//Stop();
		KillLastGenerator();
		delete &m_asAudioStream;
	}

	void Start(double time, std::vector<MIDIChannelEvent*>* events, double speed, int start = 0);
	void Stop();
	void Reset();
	void StartRender(long long llStartTime, bool force, std::vector<MIDIChannelEvent*>* events, double speed = 1.0, long long iStartPos = 0);
	void LoadSoundfont(const wchar_t* path);

	void SyncPlayer(double time, double speed);
	void ResizeBuffer(int size);

	double GetPlayerTime() { return m_dStartTime + m_iBufferReadPos / 96000.0; }
	double GetBufferSeconds() { return max(0, m_iBufferWritePos - m_iBufferReadPos) / 96000.0; }

private:
	int m_iBufferReadPos = 0;
	int m_iBufferWritePos = 0;
	int m_iBufferLength;
	bool m_bAudioStarted = false;

	bool m_bAwaitingReset = false;
	std::chrono::duration<long long, std::milli> lastReadTime = std::chrono::time_point_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now()
	).time_since_epoch();

	BASSMIDI* m_bBass = nullptr;

	static void WrappedCopy(float* src, int pos, int srcCount, float* dst, int pos2, int count);
	void BassWriteWrapped(BASSMIDI* bass, int start, int count);

	void GeneratorFunc(double speed, double time, std::vector<MIDIChannelEvent*>* events, int start = 0);

	void KillLastGenerator();
};