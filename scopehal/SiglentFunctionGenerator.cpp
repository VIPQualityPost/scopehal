/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2025 Andrew D. Zonenberg and contributors                                                         *
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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of SiglentFunctionGenerator
	@ingroup funcdrivers
 */

#include "scopehal.h"
#include "SiglentFunctionGenerator.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SiglentFunctionGenerator::SiglentFunctionGenerator(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
{
	//All SDG series have two channels
	m_channels.push_back(new SiglentFunctionGeneratorChannel(this, "C1", "#008000", 0));
	m_channels.push_back(new SiglentFunctionGeneratorChannel(this, "C2", "#ffff00", 1));

	FlushConfigCache();

	//Detect if CHDR is supported.
	//SDG2000X, SDG1000X, SDG6000X/X-E, and SDG7000A do NOT support CHDR.
	//SDG800, SDG1000, and SDG5000 support CHDR.
	//Echoing causes problems for us when enabled, so turn it on for models that support it
	//to get consistent behavior.
	m_supportsCHDR = true;
	if(m_model.find("SDG2") != string::npos)
		m_supportsCHDR = false;
	else if(m_model.find("SDG1") != string::npos)
		m_supportsCHDR = false;
	else if(m_model.find("SDG6") != string::npos)
		m_supportsCHDR = false;
	else if(m_model.find("SDG7") != string::npos)
		m_supportsCHDR = false;

	if(m_supportsCHDR)
		m_transport->SendCommandQueued("CHDR ON");

	//Add waveform combine parameter to each channel (supported on SDG2000X, SDG1000X, SDG6000X/X-E, SDG7000A)
	for(size_t i=0; i<m_channels.size(); i++)
	{
		auto chan = dynamic_cast<SiglentFunctionGeneratorChannel*>(m_channels[i]);
		if(chan)
			chan->GetParam("Combine") = FilterParameter(FilterParameter::TYPE_BOOL, Unit(Unit::UNIT_COUNTS));
	}
}

SiglentFunctionGenerator::~SiglentFunctionGenerator()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Instrument

unsigned int SiglentFunctionGenerator::GetInstrumentTypes() const
{
	return INST_FUNCTION;
}

uint32_t SiglentFunctionGenerator::GetInstrumentTypesForChannel(size_t i) const
{
	if(i < 2)
		return INST_FUNCTION;
	else
		return 0;
}

bool SiglentFunctionGenerator::AcquireData()
{
	for(size_t i=0; i<m_channels.size(); i++)
	{
		auto cname = m_channels[i]->GetHwname();
		auto pchan = dynamic_cast<SiglentFunctionGeneratorChannel*>(m_channels[i]);
		if(!pchan)
			continue;

		auto& param = pchan->GetParam("Combine");
		bool uiRequest = param.GetBoolVal();

		//Push UI parameter change to hardware
		if(!m_cachedCombineValid[i] || (uiRequest != m_cachedCombine[i]))
		{
			if(uiRequest)
				m_transport->SendCommandQueued(cname + ":CMBN ON");
			else
				m_transport->SendCommandQueued(cname + ":CMBN OFF");
			m_cachedCombine[i] = uiRequest;
			m_cachedCombineValid[i] = true;
		}

		//Read hardware state back (in case it was changed from the front panel)
		auto reply = RemoveHeader(m_transport->SendCommandQueuedWithReply(cname + ":CMBN?"));
		bool hwState = (Trim(reply) == "ON");
		if(hwState != param.GetBoolVal())
		{
			param.SetBoolVal(hwState);
			m_cachedCombine[i] = hwState;
		}
	}

	return true;
}

string SiglentFunctionGenerator::GetDriverNameInternal()
{
	return "siglent_sdg";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FunctionGenerator

void SiglentFunctionGenerator::FlushConfigCache()
{
	FunctionGenerator::FlushConfigCache();

	for(size_t i=0; i<m_channels.size(); i++)
	{
		m_cachedEnableStateValid[i] = 0;
		m_cachedOutputEnable[i] = 0;

		m_cachedFrequency[i] = 0;
		m_cachedFrequencyValid[i] = false;

		m_cachedAmplitude[i] = 0;
		m_cachedAmplitudeValid[i] = false;

		m_cachedOffset[i] = 0;
		m_cachedOffsetValid[i] = false;

		m_cachedImpedance[i] = IMPEDANCE_HIGH_Z;
		m_cachedImpedanceValid[i] = 0;

		m_cachedWaveShape[i] = SHAPE_SINE;
		m_cachedWaveShapeValid[i] = false;

		m_cachedDutyCycle[i] = 0;
		m_cachedDutyCycleValid[i] = false;

		m_cachedRiseTime[i] = 0;
		m_cachedRiseTimeValid[i] = false;

		m_cachedFallTime[i] = 0;
		m_cachedFallTimeValid[i] = false;

		m_cachedCombine[i] = false;
		m_cachedCombineValid[i] = false;
	}
}

string SiglentFunctionGenerator::RemoveHeader(const string& str)
{
	auto pos = str.find(' ');
	return Trim(str.substr(pos + 1));
}

/**
	@brief Parse the response to an OUTP? query
 */
void SiglentFunctionGenerator::ParseOutputState(const string& str, size_t i)
{
	auto fields = explode(str, ',');

	//Output enable
	if(fields[0] == "ON")
		m_cachedOutputEnable[i] = true;
	else
		m_cachedOutputEnable[i] = false;
	m_cachedEnableStateValid[i] = true;

	//field 1 is always LOAD
	//field 2 is impedance
	//On SDG series, impedance is shared across both channels, so cache for both
	for(size_t j=0; j<m_channels.size(); j++)
	{
		if(fields[2] == "HZ")
			m_cachedImpedance[j] = IMPEDANCE_HIGH_Z;
		else
			m_cachedImpedance[j] = IMPEDANCE_50_OHM;
		m_cachedImpedanceValid[j] = true;
	}

	//TODO: output invert
}

/**
	@brief Parse the response to a BSWV? query
 */
void SiglentFunctionGenerator::ParseBasicWaveform(const string& str, size_t i)
{
	auto fields = explode(str, ',');
	LogTrace("ParseBasicWaveform\n");
	LogIndenter li;

	//Fields are paired as name,value consecutively
	map<string, string> fieldmap;
	for(size_t j=0; j<fields.size(); j += 2)
	{
		if(j+1 >= fields.size())
			break;
		fieldmap[fields[j]] = fields[j+1];
	}

	//Default all waveform cache stuff to invalid
	m_cachedAmplitudeValid[i] = false;
	m_cachedOffsetValid[i] = false;
	m_cachedFrequencyValid[i] = false;
	m_cachedDutyCycleValid[i] = false;
	m_cachedRiseTimeValid[i] = false;
	m_cachedFallTimeValid[i] = false;

	Unit volts(Unit::UNIT_VOLTS);
	Unit hz(Unit::UNIT_HZ);
	Unit sec(Unit::UNIT_FS);
	for(auto it : fieldmap)
	{
		if(it.first == "AMP")
		{
			m_cachedAmplitude[i] = volts.ParseString(it.second);
			m_cachedAmplitudeValid[i] = true;
		}

		if(it.first == "OFST")
		{
			m_cachedOffset[i] = volts.ParseString(it.second);
			m_cachedOffsetValid[i] = true;
		}

		if(it.first == "FRQ")
		{
			m_cachedFrequency[i] = hz.ParseString(it.second);
			m_cachedFrequencyValid[i] = true;
		}

		if(it.first == "DUTY")
		{
			m_cachedDutyCycle[i] = stof(it.second) * 1e-2;
			m_cachedDutyCycleValid[i] = true;
		}

		if(it.first == "RISE")
		{
			m_cachedRiseTime[i] = sec.ParseString(it.second);
			m_cachedRiseTimeValid[i] = true;
		}

		if(it.first == "FALL")
		{
			m_cachedFallTime[i] = sec.ParseString(it.second);
			m_cachedFallTimeValid[i] = true;
		}

		if(it.first == "WVTP")
		{
			if(it.second == "SINE")
			{
				m_cachedWaveShape[i] = SHAPE_SINE;
				m_cachedWaveShapeValid[i] = true;
			}
			else if(it.second == "SQUARE")
			{
				m_cachedWaveShape[i] = SHAPE_SQUARE;
				m_cachedWaveShapeValid[i] = true;
			}
			else if(it.second == "RAMP")
			{
				m_cachedWaveShape[i] = SHAPE_SAWTOOTH_UP;
				m_cachedWaveShapeValid[i] = true;
			}
			else if(it.second == "PULSE")
			{
				m_cachedWaveShape[i] = SHAPE_PULSE;
				m_cachedWaveShapeValid[i] = true;
			}
			else if(it.second == "NOISE")
			{
				m_cachedWaveShape[i] = SHAPE_NOISE;
				m_cachedWaveShapeValid[i] = true;
			}
			else if(it.second == "DC")
			{
				m_cachedWaveShape[i] = SHAPE_DC;
				m_cachedWaveShapeValid[i] = true;
			}
			else if(it.second == "PRBS")
			{
				m_cachedWaveShape[i] = SHAPE_PRBS_NONSTANDARD;
				m_cachedWaveShapeValid[i] = true;
			}
			else if(it.second == "ARB")
			{
				m_cachedWaveShape[i] = SHAPE_ARB;
				m_cachedWaveShapeValid[i] = true;
			}
			else if(it.second == "IQ")
			{
				m_cachedWaveShape[i] = SHAPE_ARB;
				m_cachedWaveShapeValid[i] = true;
			}
			else
			{
				LogWarning("Don't know what to do with wave shape %s\n", it.second.c_str());
				m_cachedWaveShapeValid[i] = false;
			}
		}

		//LogDebug("%10s -> %10s\n", it.first.c_str(), it.second.c_str());
	}
}

vector<FunctionGenerator::WaveShape> SiglentFunctionGenerator::GetAvailableWaveformShapes(int /*chan*/)
{
	vector<WaveShape> ret;
	ret.push_back(SHAPE_SINE);
	ret.push_back(SHAPE_SQUARE);
	ret.push_back(SHAPE_SAWTOOTH_UP);
	ret.push_back(SHAPE_PULSE);
	ret.push_back(SHAPE_NOISE);
	ret.push_back(SHAPE_DC);
	ret.push_back(SHAPE_PRBS_NONSTANDARD);
	ret.push_back(SHAPE_ARB);
	return ret;
}

bool SiglentFunctionGenerator::GetFunctionChannelActive(int chan)
{
	if(m_cachedEnableStateValid[chan])
		return m_cachedOutputEnable[chan];

	auto reply = RemoveHeader(m_transport->SendCommandQueuedWithReply(m_channels[chan]->GetHwname() + ":OUTP?"));
	ParseOutputState(reply, chan);

	return m_cachedOutputEnable[chan];
}

void SiglentFunctionGenerator::SetFunctionChannelActive(int chan, bool on)
{
	if(on)
		m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":OUTP ON");
	else
		m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":OUTP OFF");

	m_cachedOutputEnable[chan] = on;
	m_cachedEnableStateValid[chan] = true;
}

bool SiglentFunctionGenerator::HasFunctionDutyCycleControls(int /*chan*/)
{
	return true;
}

float SiglentFunctionGenerator::GetFunctionChannelDutyCycle(int chan)
{
	if(m_cachedDutyCycleValid[chan])
		return m_cachedDutyCycle[chan];

	//Fetch BSWV which also populates DUTY
	auto reply = RemoveHeader(m_transport->SendCommandQueuedWithReply(m_channels[chan]->GetHwname() + ":BSWV?"));
	ParseBasicWaveform(reply, chan);

	return m_cachedDutyCycle[chan];
}

void SiglentFunctionGenerator::SetFunctionChannelDutyCycle(int chan, float duty)
{
	int percent = round(100 * duty);
	percent = max(0, min(100, percent));
	m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":BSWV DUTY," + to_string(percent));

	m_cachedDutyCycle[chan] = duty;
	m_cachedDutyCycleValid[chan] = true;
}

float SiglentFunctionGenerator::GetFunctionChannelAmplitude(int chan)
{
	if(m_cachedAmplitudeValid[chan])
		return m_cachedAmplitude[chan];

	auto reply = RemoveHeader(m_transport->SendCommandQueuedWithReply(m_channels[chan]->GetHwname() + ":BSWV?"));
	ParseBasicWaveform(reply, chan);

	return m_cachedAmplitude[chan];
}

void SiglentFunctionGenerator::SetFunctionChannelAmplitude(int chan, float amplitude)
{
	m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":BSWV AMP," + to_string(amplitude));

	m_cachedAmplitude[chan] = amplitude;
	m_cachedAmplitudeValid[chan] = true;
}

float SiglentFunctionGenerator::GetFunctionChannelOffset(int chan)
{
	if(m_cachedOffsetValid[chan])
		return m_cachedOffset[chan];

	auto reply = RemoveHeader(m_transport->SendCommandQueuedWithReply(m_channels[chan]->GetHwname() + ":BSWV?"));
	ParseBasicWaveform(reply, chan);

	return m_cachedOffset[chan];
}

void SiglentFunctionGenerator::SetFunctionChannelOffset(int chan, float offset)
{
	m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":BSWV OFST," + to_string(offset));

	m_cachedOffset[chan] = offset;
	m_cachedOffsetValid[chan] = true;
}

float SiglentFunctionGenerator::GetFunctionChannelFrequency(int chan)
{
	if(m_cachedFrequencyValid[chan])
		return m_cachedFrequency[chan];

	auto reply = RemoveHeader(m_transport->SendCommandQueuedWithReply(m_channels[chan]->GetHwname() + ":BSWV?"));
	ParseBasicWaveform(reply, chan);

	return m_cachedFrequency[chan];
}

void SiglentFunctionGenerator::SetFunctionChannelFrequency(int chan, float hz)
{
	if(m_cachedFrequencyValid[chan] && std::abs(m_cachedFrequency[chan] - hz) < 1e-6)
	{
		return;
	}
	m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":BSWV FRQ," + to_string(hz));

	m_cachedFrequency[chan] = hz;
	m_cachedFrequencyValid[chan] = true;
}

FunctionGenerator::WaveShape SiglentFunctionGenerator::GetFunctionChannelShape(int chan)
{
	if(m_cachedWaveShapeValid[chan])
		return m_cachedWaveShape[chan];

	auto reply = RemoveHeader(m_transport->SendCommandQueuedWithReply(m_channels[chan]->GetHwname() + ":BSWV?"));
	ParseBasicWaveform(reply, chan);

	//TODO: handle arb etc

	return m_cachedWaveShape[chan];
}

void SiglentFunctionGenerator::SetFunctionChannelShape(int chan, WaveShape shape)
{
	m_cachedWaveShapeValid[chan] = true;
	m_cachedWaveShape[chan] = shape;

	switch(shape)
	{
		case SHAPE_SINE:
			m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":BSWV WVTP,SINE");
			break;

		case SHAPE_SQUARE:
			m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":BSWV WVTP,SQUARE");
			break;

		case SHAPE_SAWTOOTH_UP:
			m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":BSWV WVTP,RAMP");
			break;

		case SHAPE_PULSE:
			m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":BSWV WVTP,PULSE");
			break;

		case SHAPE_NOISE:
			m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":BSWV WVTP,NOISE");
			break;

		case SHAPE_DC:
			m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":BSWV WVTP,DC");
			break;

		case SHAPE_PRBS_NONSTANDARD:
			m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":BSWV WVTP,PRBS");
			break;

		default:
			LogWarning("[SiglentFunctionGenerator::SetFunctionChannelShape] unrecognized shape %d", shape);

			m_cachedWaveShapeValid[chan] = false;
			break;
	}
}

bool SiglentFunctionGenerator::HasFunctionRiseFallTimeControls(int chan)
{
	//Rise/fall time is only valid for PULSE waveform
	return (GetFunctionChannelShape(chan) == SHAPE_PULSE);
}

float SiglentFunctionGenerator::GetFunctionChannelRiseTime(int chan)
{
	if(m_cachedRiseTimeValid[chan])
		return m_cachedRiseTime[chan];

	auto reply = RemoveHeader(m_transport->SendCommandQueuedWithReply(m_channels[chan]->GetHwname() + ":BSWV?"));
	ParseBasicWaveform(reply, chan);

	return m_cachedRiseTime[chan];
}

void SiglentFunctionGenerator::SetFunctionChannelRiseTime(int chan, float fs)
{
	//Convert fs to seconds
	float sec = fs * 1e-15f;
	m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":BSWV RISE," + to_string(sec));

	m_cachedRiseTime[chan] = fs;
	m_cachedRiseTimeValid[chan] = true;
}

float SiglentFunctionGenerator::GetFunctionChannelFallTime(int chan)
{
	if(m_cachedFallTimeValid[chan])
		return m_cachedFallTime[chan];

	auto reply = RemoveHeader(m_transport->SendCommandQueuedWithReply(m_channels[chan]->GetHwname() + ":BSWV?"));
	ParseBasicWaveform(reply, chan);

	return m_cachedFallTime[chan];
}

void SiglentFunctionGenerator::SetFunctionChannelFallTime(int chan, float fs)
{
	//Convert fs to seconds
	float sec = fs * 1e-15f;
	m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":BSWV FALL," + to_string(sec));

	m_cachedFallTime[chan] = fs;
	m_cachedFallTimeValid[chan] = true;
}

bool SiglentFunctionGenerator::HasFunctionImpedanceControls(int /*chan*/)
{
	return true;
}

FunctionGenerator::OutputImpedance SiglentFunctionGenerator::GetFunctionChannelOutputImpedance(int chan)
{
	if(m_cachedImpedanceValid[chan])
		return m_cachedImpedance[chan];

	auto reply = RemoveHeader(m_transport->SendCommandQueuedWithReply(m_channels[chan]->GetHwname() + ":OUTP?"));
	ParseOutputState(reply, chan);

	return m_cachedImpedance[chan];
}

void SiglentFunctionGenerator::SetFunctionChannelOutputImpedance(int chan, FunctionGenerator::OutputImpedance z)
{
	if(z == IMPEDANCE_HIGH_Z)
		m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":OUTP LOAD,HZ");
	else
		m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":OUTP LOAD,50");

	m_cachedImpedance[chan] = z;
	m_cachedImpedanceValid[chan] = true;

	//Impedance is shared across both channels on SDG series
	size_t other = (chan == 0) ? 1 : 0;
	m_cachedImpedance[other] = z;
	m_cachedImpedanceValid[other] = true;
}
