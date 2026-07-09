/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
* All rights reserved.                                                                                                 *
*                                                                                                                      *
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that the     *
* following conditions are met:                                                                                        *
*                                                                                                                      *
*    * Redistributions of source code must retain the above copyright notice, this list of conditions, and the         *
*      following disclaimer.                                                                                           *
*                                                                                                                      *
*    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the       *
*      following disclaimer in the documentation and/or other materials provided with the distribution.                *
*                                                                                                                      *
*    * Neither the name of the author nor the names of any contributors may be used to endorse or promote products     *
*      derived from this software without specific prior written permission.                                           *
*                                                                                                                      *
* THIS SOFTWARE IS PROVIDED BY THE AUTHORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED   *
* TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL *
* THE AUTHORS BE HELD LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES        *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR       *
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE       *
* POSSIBILITY OF SUCH DAMAGE.                                                                                          *
*                                                                                                                      *
***********************************************************************************************************************/

#include "scopehal.h"
#include "RigolSpectrumAnalyzer.h"
#include "EdgeTrigger.h"

#include <cinttypes>

#ifdef _WIN32
#include <chrono>
#include <thread>
#endif

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Construction / destruction

RigolSpectrumAnalyzer::RigolSpectrumAnalyzer(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, m_rbw(1000000)	// 1 MHz default
	, m_vbw(1000000)	// 1 MHz default
	, m_sweepTime(37500) // 37.5 ms default
	, m_sweepPoints(601)
	, m_refLevel(-10)
	, m_scalePerDiv(10)
	, m_inputAttenuation(10)
	, m_preampEnabled(false)
{
	//Determine the model and frequency range
	m_freqMax = ModelToMaxFreq(m_model);
	m_freqMin = 0; // DSA800 goes down to 9 kHz, but set to 0 for flexibility

	//Default to full span
	m_span = m_freqMax - m_freqMin;
	m_centerFreq = m_freqMax / 2;

	//Create the spectrum channel
	//SpectrumChannel has x-axis in Hz, y-axis in dBm
	m_spectrumChannel = new SpectrumChannel(
		this,
		"RF Input",
		"#ffff00",	// yellow
		m_channels.size());

	m_channels.push_back(m_spectrumChannel);
	m_spectrumChannel->SetDefaultDisplayName();

	//Set up initial display range: 10 dB/div, ref level -10 dBm -> range 100 dB
	m_spectrumChannel->SetYAxisUnits(Unit::UNIT_DBM, 0);
	SetChannelVoltageRange(0, 0, 100);
	SetChannelOffset(0, 0, -50); // puts center at -60 dBm for ref=-10, 10 dB/div

	//Configure the instrument for basic operation
	//Set power unit to dBm
	m_transport->SendCommandQueued(":UNIT:POW DBM");

	//Set trace data format to REAL,32 (binary float32)
	m_transport->SendCommandQueued(":FORMat:TRACe:DATA REAL,32");
	m_transport->SendCommandQueued(":FORMat:BORDer NORMal");

	//Set continuous sweep mode
	m_transport->SendCommandQueued(":INIT:CONT ON");

	//Set input attenuation to auto
	m_transport->SendCommandQueued(":SENSe:POWer:RF:ATTenuation:AUTO ON");

	//Set RBW to auto
	m_transport->SendCommandQueued(":SENSe:BANDwidth:RESolution:AUTO ON");

	//Set VBW to auto
	m_transport->SendCommandQueued(":SENSe:BANDwidth:VIDeo:AUTO ON");

	//Set sweep time to auto
	m_transport->SendCommandQueued(":SENSe:SWEep:TIME:AUTO ON");

	//Set detector to positive peak (normal for general use)
	m_transport->SendCommandQueued(":SENSe:DETector:FUNCtion POSitive");

	//Set up an edge trigger (always triggered for spectrum analyzer)
	auto trig = new EdgeTrigger(this);
	trig->SetType(EdgeTrigger::EDGE_RISING);
	trig->SetLevel(0);
	SetTrigger(trig);
	PushTrigger();

	//Flush all pending commands
	m_transport->FlushCommandQueue();
}

RigolSpectrumAnalyzer::~RigolSpectrumAnalyzer()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Model identification

int64_t RigolSpectrumAnalyzer::ModelToMaxFreq(const string& model)
{
	//DSA800 series model numbers encode max frequency in the last 2-3 digits
	//DSA815 = 1.5 GHz, DSA832 = 3.2 GHz, DSA875 = 7.5 GHz
	//DSA810 = 1.0 GHz, DSA815E = 1.5 GHz

	int64_t maxFreq = 1500000000LL; 

	if(model.find("DSA810") != string::npos)
		maxFreq = 1000000000LL;
	else if(model.find("DSA815") != string::npos)
		maxFreq = 1500000000LL;
	else if(model.find("DSA820") != string::npos)
		maxFreq = 2000000000LL;
	else if(model.find("DSA825") != string::npos)
		maxFreq = 2500000000LL;
	else if(model.find("DSA830") != string::npos)
		maxFreq = 3000000000LL;
	else if(model.find("DSA832") != string::npos)
		maxFreq = 3200000000LL;
	else if(model.find("DSA840") != string::npos)
		maxFreq = 4000000000LL;
	else if(model.find("DSA850") != string::npos)
		maxFreq = 5000000000LL;
	else if(model.find("DSA860") != string::npos)
		maxFreq = 6000000000LL;
	else if(model.find("DSA875") != string::npos)
		maxFreq = 7500000000LL;

	return maxFreq;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Accessors

unsigned int RigolSpectrumAnalyzer::GetInstrumentTypes() const
{
	return Instrument::INST_OSCILLOSCOPE;
}

uint32_t RigolSpectrumAnalyzer::GetInstrumentTypesForChannel(size_t /*i*/) const
{
	return Instrument::INST_OSCILLOSCOPE;
}

string RigolSpectrumAnalyzer::GetDriverNameInternal()
{
	return "rigol_dsa800";
}

void RigolSpectrumAnalyzer::FlushConfigCache()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Channel configuration

bool RigolSpectrumAnalyzer::IsChannelEnabled(size_t /*i*/)
{
	//DSA800 only has one input channel, always enabled
	return true;
}

void RigolSpectrumAnalyzer::EnableChannel(size_t /*i*/)
{
}

void RigolSpectrumAnalyzer::DisableChannel(size_t /*i*/)
{
}

OscilloscopeChannel::CouplingType RigolSpectrumAnalyzer::GetChannelCoupling(size_t /*i*/)
{
	//Spectrum analyzer input is 50 ohm AC coupled
	return OscilloscopeChannel::COUPLE_AC_50;
}

void RigolSpectrumAnalyzer::SetChannelCoupling(size_t /*i*/, OscilloscopeChannel::CouplingType /*type*/)
{
	//No-op: DSA800 doesn't have switchable coupling via SCPI
}

vector<OscilloscopeChannel::CouplingType> RigolSpectrumAnalyzer::GetAvailableCouplings(size_t /*i*/)
{
	return {OscilloscopeChannel::COUPLE_AC_50};
}

double RigolSpectrumAnalyzer::GetChannelAttenuation(size_t /*i*/)
{
	//No probe attenuation for a spectrum analyzer
	return 1.0;
}

void RigolSpectrumAnalyzer::SetChannelAttenuation(size_t /*i*/, double /*atten*/)
{
}

vector<unsigned int> RigolSpectrumAnalyzer::GetChannelBandwidthLimiters(size_t /*i*/)
{
	return {};
}

unsigned int RigolSpectrumAnalyzer::GetChannelBandwidthLimit(size_t /*i*/)
{
	return 0;
}

void RigolSpectrumAnalyzer::SetChannelBandwidthLimit(size_t /*i*/, unsigned int /*limit_mhz*/)
{
}

float RigolSpectrumAnalyzer::GetChannelVoltageRange(size_t /*i*/, size_t /*stream*/)
{
	//Range = scale_per_div * number_of_divisions (10 divisions for DSA800)
	return m_scalePerDiv * 10;
}

void RigolSpectrumAnalyzer::SetChannelVoltageRange(size_t /*i*/, size_t /*stream*/, float range)
{
	m_scalePerDiv = range / 10;
	m_transport->SendCommandQueued(
		string(":DISPlay:WINdow:TRACe:Y:SCALe:PDIVision ") + to_string(m_scalePerDiv));
}

float RigolSpectrumAnalyzer::GetChannelOffset(size_t /*i*/, size_t /*stream*/)
{
	//Offset is relative to reference level: center of display = ref_level - (scale*5)
	return m_refLevel - (m_scalePerDiv * 5);
}

void RigolSpectrumAnalyzer::SetChannelOffset(size_t /*i*/, size_t /*stream*/, float offset)
{
	//offset = ref_level - (scale*5) => ref_level = offset + (scale*5)
	m_refLevel = offset + (m_scalePerDiv * 5);
	m_transport->SendCommandQueued(
		string(":DISPlay:WINdow:TRACe:Y:SCALe:RLEVel ") + to_string(m_refLevel));
}

OscilloscopeChannel* RigolSpectrumAnalyzer::GetExternalTrigger()
{
	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Triggering

Oscilloscope::TriggerMode RigolSpectrumAnalyzer::PollTrigger()
{
	m_transport->FlushCommandQueue();

	//DSA800 is always sweeping in continuous mode
	//When in single sweep, check if a sweep has completed
	return TRIGGER_MODE_TRIGGERED;
}

bool RigolSpectrumAnalyzer::AcquireData()
{
	//Flush any pending configuration commands to the instrument
	m_transport->FlushCommandQueue();

	//Read trace data in binary float32 format using the built-in definite-length block parser
	//This sends :TRACe:DATA? TRACE1 and reads the #N<bytes><data> response
	size_t len = 0;
	auto buf = m_transport->SendCommandImmediateWithRawBlockReply(":TRACe:DATA? TRACE1", len);
	auto ucbuf = reinterpret_cast<unsigned char*>(buf);
	if(!buf || len == 0)
	{
		if(ucbuf)
			delete[] ucbuf;
		LogWarning("RigolSpectrumAnalyzer: failed to read trace data\n");
		return false;
	}

	//The data is float32
	size_t npoints = len / sizeof(float);
	float* fbuf = reinterpret_cast<float*>(buf);

	//Get the current frequency configuration
	int64_t freqStart = m_centerFreq - m_span / 2;
	int64_t freqStop = m_centerFreq + m_span / 2;

	//Clamp
	freqStart = max(m_freqMin, freqStart);
	freqStop = min(m_freqMax, freqStop);

	if(freqStart >= freqStop || npoints == 0)
	{
		delete[] ucbuf;
		return false;
	}

	//Create a uniform analog waveform for the spectrum data
	int64_t stepsize = (freqStop - freqStart) / static_cast<int64_t>(npoints);

	double t = GetTime();
	int64_t fs = (t - floor(t)) * FS_PER_SECOND;

	auto cap = AllocateAnalogWaveform("data");
	cap->m_timescale = stepsize;
	cap->m_triggerPhase = freqStart;
	cap->m_startTimestamp = floor(t);
	cap->m_startFemtoseconds = fs;
	cap->Resize(npoints);

	for(size_t i = 0; i < npoints; i++)
		cap->m_samples[i] = fbuf[i];

	cap->MarkModifiedFromCpu();

	//Free the raw buffer
	delete[] ucbuf;

	//Discard trailing newline that the binary block parser leaves in the buffer
	uint8_t disregard;
	m_transport->ReadRawData(1, &disregard);

	//Build the sequence set
	SequenceSet s;
	s[StreamDescriptor(m_spectrumChannel, 0)] = cap;

	//Save the waveforms to our queue
	m_pendingWaveformsMutex.lock();
	m_pendingWaveforms.push_back(s);
	m_pendingWaveformsMutex.unlock();

	//If this was a one-shot trigger we're no longer armed
	if(m_triggerOneShot)
		m_triggerArmed = false;

	return true;
}

void RigolSpectrumAnalyzer::Start()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommandQueued(":INIT:CONT ON");
	m_transport->FlushCommandQueue();
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void RigolSpectrumAnalyzer::StartSingleTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommandQueued(":INIT:CONT OFF");
	m_transport->SendCommandQueued(":INIT:IMM");
	m_transport->FlushCommandQueue();
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void RigolSpectrumAnalyzer::Stop()
{
	m_transport->SendCommandQueued(":ABORt");
	m_transport->FlushCommandQueue();
	m_triggerArmed = false;
	m_triggerOneShot = false;
}

void RigolSpectrumAnalyzer::ForceTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommandQueued(":INIT:IMM");
	m_transport->FlushCommandQueue();
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

bool RigolSpectrumAnalyzer::IsTriggerArmed()
{
	return m_triggerArmed;
}

void RigolSpectrumAnalyzer::PushTrigger()
{
	//No trigger configuration needed for a spectrum analyzer
}

void RigolSpectrumAnalyzer::PullTrigger()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Sample rate / memory depth (not applicable to spectrum analyzers)

std::vector<uint64_t> RigolSpectrumAnalyzer::GetSampleRatesNonInterleaved()
{
	return {1000};
}

std::vector<uint64_t> RigolSpectrumAnalyzer::GetSampleRatesInterleaved()
{
	return {1000};
}

std::set<Oscilloscope::InterleaveConflict> RigolSpectrumAnalyzer::GetInterleaveConflicts()
{
	return {};
}

std::vector<uint64_t> RigolSpectrumAnalyzer::GetSampleDepthsNonInterleaved()
{
	//DSA800 supports 101 to 3001 sweep points
	return {101, 201, 301, 401, 501, 601, 751, 1001, 1501, 2001, 3001};
}

std::vector<uint64_t> RigolSpectrumAnalyzer::GetSampleDepthsInterleaved()
{
	return {601};
}

uint64_t RigolSpectrumAnalyzer::GetSampleRate()
{
	return 1;
}

uint64_t RigolSpectrumAnalyzer::GetSampleDepth()
{
	return m_sweepPoints;
}

void RigolSpectrumAnalyzer::SetSampleDepth(uint64_t depth)
{
	m_sweepPoints = depth;
	m_transport->SendCommandQueued(
		string(":SENSe:SWEep:POINts ") + to_string(m_sweepPoints));
}

void RigolSpectrumAnalyzer::SetSampleRate(uint64_t /*rate*/)
{
}

void RigolSpectrumAnalyzer::SetTriggerOffset(int64_t /*offset*/)
{
}

int64_t RigolSpectrumAnalyzer::GetTriggerOffset()
{
	return 0;
}

bool RigolSpectrumAnalyzer::IsInterleaving()
{
	return false;
}

bool RigolSpectrumAnalyzer::SetInterleaving(bool /*combine*/)
{
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Frequency domain controls

bool RigolSpectrumAnalyzer::HasFrequencyControls()
{
	return true;
}

bool RigolSpectrumAnalyzer::HasResolutionBandwidth()
{
	return true;
}

bool RigolSpectrumAnalyzer::HasTimebaseControls()
{
	return false;
}

void RigolSpectrumAnalyzer::SetSpan(int64_t span)
{
	m_span = max(span, 0LL);
	m_transport->SendCommandQueued(
		string(":SENSe:FREQuency:SPAN ") + to_string(m_span));
}

int64_t RigolSpectrumAnalyzer::GetSpan()
{
	return m_span;
}

void RigolSpectrumAnalyzer::SetCenterFrequency(size_t /*channel*/, int64_t freq)
{
	m_centerFreq = freq;
	m_transport->SendCommandQueued(
		string(":SENSe:FREQuency:CENTer ") + to_string(m_centerFreq));
}

int64_t RigolSpectrumAnalyzer::GetCenterFrequency(size_t /*channel*/)
{
	return m_centerFreq;
}

void RigolSpectrumAnalyzer::SetResolutionBandwidth(int64_t rbw)
{
	m_rbw = rbw;
	m_transport->SendCommandQueued(
		string(":SENSe:BANDwidth:RESolution ") + to_string(m_rbw));
	m_transport->SendCommandQueued(":SENSe:BANDwidth:RESolution:AUTO OFF");
}

int64_t RigolSpectrumAnalyzer::GetResolutionBandwidth()
{
	return m_rbw;
}
