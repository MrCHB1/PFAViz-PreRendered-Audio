#include <Windows.h>
#include "MIDIAudio.h"
#include "Config.h"
#include <functional>
#include <future>
#include <fstream>

// global mutable variables... o.o
static std::atomic_bool stopGenerator = false;
std::mutex m_maMtx;
std::atomic<double> g_preVolume = 1.0;

AudioBufferStream::AudioBufferStream(MIDIAudio* source)
{
	m_maAudioSource = source;
}

// ------- loudmax stuff -------
bool reduceHighPitch = false;
double loudnessL = 1;
double loudnessR = 1;
double velocityR = 0;
double velocityL = 0;
double strength = 1;
double minThresh = 0.4;
double velocityThresh = 1;

int AudioBufferStream::Read(float* buffer, int offset, int count)
{
	{
		m_maMtx.lock();
		if (m_maAudioSource->m_bPaused || m_maAudioSource->m_bAwaitingReset)
		{
			for (int i = 0; i < count; i++)
			{
				buffer[i + offset] = 0;
			}
			m_maMtx.unlock();
			return count;
		}
		int readpos = m_maAudioSource->m_iBufferReadPos % (m_maAudioSource->m_iBufferLength / 2);
		int writepos = m_maAudioSource->m_iBufferWritePos % (m_maAudioSource->m_iBufferLength / 2);
		if (m_maAudioSource->m_iBufferReadPos + count / 2 > m_maAudioSource->m_iBufferWritePos)
		{
			int copyCount = m_maAudioSource->m_iBufferReadPos - (m_maAudioSource->m_iBufferWritePos + count / 2);
			if (copyCount > count / 2) copyCount = count / 2;
			if (copyCount > 0) MIDIAudio::WrappedCopy(m_maAudioSource->m_fAudioBuffer, readpos * 2, m_maAudioSource->m_iBufferLength, buffer, offset, copyCount * 2);
			else
			{
				copyCount = 0;
			}
			for (int i = copyCount * 2; i < count; i++)
			{
				buffer[i + offset] = 0;
			}
		}
		else
		{
			MIDIAudio::WrappedCopy(m_maAudioSource->m_fAudioBuffer, readpos * 2, m_maAudioSource->m_iBufferLength, buffer, offset, count);
		}
		m_maAudioSource->m_iBufferReadPos += count / 2;
		m_maAudioSource->lastReadTime = std::chrono::time_point_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now()
		).time_since_epoch();
		m_maMtx.unlock();
	}
	return count;
}

// read with loudmax
int AudioBufferStream::ReadLM(float* buffer, int offset, int count)
{
	double attack = 48000 * m_maAudioSource->m_dAttack;
	double falloff = 48000 * m_maAudioSource->m_dRelease;

	int read = Read(buffer, offset, count);
	int end = offset + read;

	if (read % 2 != 0) {}
	for (int i = offset; i < end; i += 2)
	{
		double l = (double)fabs(buffer[i]);
		double r = (double)fabs(buffer[i + 1]);

		if (loudnessL > l) loudnessL = (loudnessL * falloff + l) / (falloff + 1.0);
		else loudnessL = (loudnessL * attack + l) / (attack + 1.0);

		if (loudnessR > r) loudnessR = (loudnessR * falloff + r) / (falloff + 1.0);
		else loudnessR = (loudnessR * attack + r) / (attack + 1.0);

		if (loudnessL < minThresh) loudnessL = minThresh;
		if (loudnessR < minThresh) loudnessR = minThresh;

		l = buffer[i] / (loudnessL * strength + 2.0 * (1 - strength)) / 2.0;
		r = buffer[i + 1] / (loudnessR * strength + 2.0 * (1 - strength)) / 2.0;

		if (i != offset)
		{
			double dl = std::abs((double)buffer[i] - l);
			double dr = std::abs((double)buffer[i + 1] - r);

			if (velocityL > dl)
				velocityL = (velocityL * falloff + dl) / (falloff + 1.0);
			else
				velocityL = (velocityL * attack + dl) / (attack + 1.0);

			if (velocityR > dr)
				velocityR = (velocityR * falloff + dr) / (falloff + 1.0);
			else
				velocityR = (velocityR * attack + dr) / (attack + 1.0);
		}

		if (reduceHighPitch)
		{
			if (velocityL > velocityThresh)
				l = l / velocityL * velocityThresh;
			if (velocityR > velocityThresh)
				r = r / velocityR * velocityThresh;
		}

		*(buffer + i) = (float)(l * g_preVolume);
		*(buffer + i + 1) = (float)(r * g_preVolume);
	}
	return read;
}

void MIDIAudio::WrappedCopy(float* src, int pos, int srcCount, float *dst, int pos2, int count)
{
	if (pos + count > srcCount)
	{
		memcpy(dst + pos2, src + pos, (srcCount - pos) * sizeof(float));
		count -= (srcCount - pos);
		pos = 0;
	}
	memcpy(dst + pos2, src + pos, count * sizeof(float));
}

MIDIAudio::MIDIAudio(int bufferLength) : m_asAudioStream(this) 
{
	m_fAudioBuffer = (float*)malloc(bufferLength * 2 * sizeof(float));
	memset(m_fAudioBuffer, 0, bufferLength * 2 * sizeof(float));
	m_iBufferLength = bufferLength * 2;
	m_asAudioStream = AudioBufferStream(this);
	m_tGeneratorThread = nullptr;
}

// we load a default soundfont if path is left blank - explains the large file size
void MIDIAudio::LoadSoundfont(const wchar_t* path)
{
	if (std::wstring(path).empty())
	{
		// LoL!
		HRSRC rc = FindResourceW(NULL, MAKEINTRESOURCE(IDR_PRESF), TEXT("MIDI\0"));
		HGLOBAL rcData = LoadResource(NULL, rc);
		int rSize = SizeofResource(NULL, rc);
		unsigned char* data = (unsigned char*)LockResource(rcData);

		WCHAR path[1024]{ };
		GetModuleFileNameW(NULL, path, 1024);
		std::wstring::size_type pos = std::wstring(path).find_last_of(L"\\/");
		wstringstream ps;
		ps << std::wstring(path).substr(0, pos).c_str() << "\\Soundfonts";

		CreateDirectory(ps.str().c_str(), NULL);

		ps << "\\GeneralUser GS v1.471.sf2";
		std::ofstream file;
		file.open(ps.str().c_str(), std::ios::binary);
		file.write((char*)data, rSize);
		file.close();

		m_bBass->LoadSoundfont(ps.str().c_str());

		return;
	}
	m_bBass->LoadSoundfont(path);
}

void MIDIAudio::Reset()
{
	memset(m_fAudioBuffer, 0, m_iBufferLength * sizeof(float));
	m_iBufferWritePos = 0;
	m_iBufferReadPos = 0;
}

void MIDIAudio::BassWriteWrapped(BASSMIDI* bass, int start, int count)
{
	start = (start * 2) % m_iBufferLength;
	count *= 2;
	if (start + count > m_iBufferLength)
	{
		bass->Read(m_fAudioBuffer, start, m_iBufferLength - start);
		count -= m_iBufferLength - start;
		bass->Read(m_fAudioBuffer, 0, count);
	}
	else
	{
		bass->Read(m_fAudioBuffer, start, count);
	}
}

// this was *sorta* copied from kiva
void MIDIAudio::GeneratorFunc(double speed, double time, std::vector<MIDIChannelEvent*>* events, int start)
{
	BASSMIDI* bass = new BASSMIDI(m_iDefaultVoices, m_bDefaultNoFx);
	m_iBufferWritePos = 0;
	m_iBufferReadPos = 0;

	for (std::vector<MIDIChannelEvent*>::iterator e = events->begin(); e != events->end(); ++e)
	{
		// do not skip events other than note events
		if ((((*e)->GetEventCode() >> 4) == 0x8 || ((*e)->GetEventCode() >> 4) == 0x9) && e - events->begin() < start) continue;

		if (m_iBufferWritePos < m_iBufferReadPos)
		{
			m_iBufferWritePos = m_iBufferReadPos;
		}

		double evTime = ((double)(*e)->GetAbsMicroSec() / 1000000) / speed;

		// quantizes events to the nearest "frame"
		if (m_dFPS != 0.0) evTime = floor(evTime * m_dFPS) / m_dFPS;
		
		double offset = evTime - m_dStartTime;
		int samples = (int)(48000 * offset) - m_iBufferWritePos;
		if (samples > 0)
		{
			while (m_iBufferWritePos + samples > m_iBufferReadPos + m_iBufferLength / 2)
			{
				auto spare = (m_iBufferReadPos + m_iBufferLength / 2) - m_iBufferWritePos;
				if (spare > 0)
				{
					if (spare > samples) spare = samples;
					if (spare != 0)
					{
						BassWriteWrapped(bass, m_iBufferWritePos, spare);
						samples -= spare;
						m_iBufferWritePos += spare;
					}
					if (samples == 0) break;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
				if (stopGenerator)
				{
					break;
				}
			}
			if (samples != 0) BassWriteWrapped(bass, m_iBufferWritePos, samples);
			m_iBufferWritePos += samples;
		}

		// skipping velocity
		if (((*e)->GetEventCode() >> 4) == 0x9 && (*e)->GetParam2() < GetSkippingVelocity()) continue;
		//if (((*e)->GetEventCode() >> 4) != 0x9 && ((*e)->GetEventCode() >> 4) != 0x8) continue;

		// skip notes with velocity lower than this value
		if (((*e)->GetEventCode() >> 4) == 0x9 && \
			((*e)->GetParam2() <= m_iVelThreshLow || (*e)->GetParam2() > m_iVelThreshUpp)) continue;

		BYTE ev[3] = { (*e)->GetEventCode(), (*e)->GetParam1(), (*e)->GetParam2() };

		int err = 1;
		err = bass->SendEventRaw(ev, 0);
		if (err <= 0) {}
		if (stopGenerator)
		{
			break;
		}
	}

	while (!stopGenerator)
	{
		auto spare = (m_iBufferReadPos + m_iBufferLength / 2) - m_iBufferWritePos;
		if (spare > 0 && spare != 0)
		{
			BassWriteWrapped(bass, m_iBufferWritePos, spare);
			m_iBufferWritePos += spare;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}

	bass->~BASSMIDI();
}

void MIDIAudio::KillLastGenerator()
{
	memset(m_fAudioBuffer, 0, m_iBufferLength * sizeof(float));
	stopGenerator = true;
	if (m_tGeneratorThread != nullptr)
	{
		m_tGeneratorThread->join();
		m_tGeneratorThread = nullptr;
	}
}

void MIDIAudio::Start(double time, std::vector<MIDIChannelEvent*>* events, double speed, int start)
{
	stopGenerator = false;
	m_dStartTime = time / speed;
	m_tGeneratorThread = new std::thread([this, speed, time, events, start] { GeneratorFunc(speed, time, events, start); });
	m_bAudioStarted = true;
	m_bAwaitingReset = false;
}

void MIDIAudio::Stop()
{
	KillLastGenerator();
	m_bPaused = true;
	m_iBufferWritePos = 0;
	m_iBufferReadPos = 0;
}

void MIDIAudio::SyncPlayer(double time, double speed)
{
	{
		m_maMtx.lock();
		time /= speed;
		double t = m_dStartTime + m_iBufferReadPos / 48000.0;
		double offset = time - t;
		int newPos = m_iBufferReadPos + (int)(offset * 48000);
		if (newPos < 0) newPos = 0;
		if (std::abs(m_iBufferReadPos - newPos) / 48000.0 > 0.03) m_iBufferReadPos = newPos;
		m_maMtx.unlock();
	}
}

void MIDIAudio::StartRender(long long llStartTime, bool force, std::vector<MIDIChannelEvent*>* events, double speed, long long iStartPos)
{
	double time = (double)llStartTime / 1000000;
	if (!force)
	{
		if (time + 0.1 > GetPlayerTime() + GetBufferSeconds() || time + 0.01 < GetPlayerTime())
		{
			force = true;
		}
	}
	if (force)
	{
		Start(time, events, speed, iStartPos);
	}
	else
	{
		SyncPlayer(time, speed);
	}
}