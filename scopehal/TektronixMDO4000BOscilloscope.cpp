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
* THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES             *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR       *
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE       *
* POSSIBILITY OF SUCH DAMAGE.                                                                                          *
*                                                                                                                      *
***********************************************************************************************************************/

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of TektronixMDO4000BOscilloscope

	@ingroup scopedrivers
 */

#include "scopehal.h"
#include "TektronixMDO4000BOscilloscope.h"
#include "EdgeTrigger.h"
#include "PulseWidthTrigger.h"
#include "DropoutTrigger.h"
#include "RuntTrigger.h"
#include "SlewRateTrigger.h"
#include "WindowTrigger.h"
#include "Waveform.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

TektronixMDO4000BOscilloscope::TektronixMDO4000BOscilloscope(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, TektronixOscilloscope(transport)
	, m_digitalChannelBaseMDO(0)
	, m_digitalChannelCountMDO(0)
	, m_hasRF(false)
	, m_rfClipping(false)
{
	//The base class (TektronixOscilloscope) already ran its constructor.

	//Now send MDO-specific setup commands
	//Re-check for digital channels (MSO option)
	string cfg_digital = m_transport->SendCommandImmediateWithReply("CONFIG:DIGITAL:NUMCHAN?");
	if(!cfg_digital.empty())
	{
		m_digitalChannelCountMDO = stoul(cfg_digital);
		LogDebug(" * MDO has %zu digital channels\n", m_digitalChannelCountMDO);
	}

	//MDO models always have RF built in
	m_hasRF = true;
	LogDebug(" * MDO has RF (model %s)\n", m_model.c_str());

	//Create RF spectrum channel
	if(m_hasRF)
	{
		m_spectrumChannelBase = m_channels.size();
		auto rfchan = new SpectrumChannel(
			this,
			"RF",
			"#ff6400",
			m_channels.size());
		m_channels.push_back(rfchan);
	}

	//Add digital channels if present
	if(m_digitalChannelCountMDO > 0)
	{
		m_digitalChannelBaseMDO = m_channels.size();

		for(size_t i = 0; i < m_digitalChannelCountMDO; i++)
		{
			string hwname = string("D") + to_string(i);

			auto chan = new OscilloscopeChannel(
				this,
				hwname,
				"#ffffff",
				Unit(Unit::UNIT_FS),
				Unit(Unit::UNIT_COUNTS),
				Stream::STREAM_TYPE_DIGITAL,
				m_channels.size());

			m_digitalChannelIndex[i] = m_channels.size();
			m_channels.push_back(chan);
		}
	}
}

TektronixMDO4000BOscilloscope::~TektronixMDO4000BOscilloscope()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string TektronixMDO4000BOscilloscope::GetDriverNameInternal()
{
	return "tektronix.mdo4000b";
}

string TektronixMDO4000BOscilloscope::GetProbeName(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	if(m_probeNames.find(i) != m_probeNames.end())
		return m_probeNames[i];
	return "";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel configuration

bool TektronixMDO4000BOscilloscope::IsChannelEnabled(size_t i)
{
	auto ochan = GetOscilloscopeChannel(i);
	if(!ochan)
		return false;

	//Check cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelsEnabled.find(i) != m_channelsEnabled.end())
			return m_channelsEnabled[i];
	}

	//Analog channels
	if(i < m_analogChannelCount)
	{
		string reply = m_transport->SendCommandQueuedWithReply(
			string("SELECT:CH") + to_string(i+1) + "?");
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelsEnabled[i] = (reply == "1");
		return m_channelsEnabled[i];
	}

	//RF channel is always on if present
	if(m_hasRF && (i == m_spectrumChannelBase))
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelsEnabled[i] = true;
		return true;
	}

	//Digital channels
	if(m_digitalChannelCountMDO > 0 && (i >= m_digitalChannelBaseMDO) &&
		(i < m_digitalChannelBaseMDO + m_digitalChannelCountMDO))
	{
		int dchan = i - m_digitalChannelBaseMDO;
		string reply = m_transport->SendCommandQueuedWithReply(
			string("SELECT:D") + to_string(dchan) + "?");
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelsEnabled[i] = (reply == "1");
		return m_channelsEnabled[i];
	}

	//External trigger
	if(m_extTrigChannel && i == m_extTrigChannel->GetIndex())
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelsEnabled[i] = false;
		return false;
	}

	return false;
}

void TektronixMDO4000BOscilloscope::EnableChannel(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelsEnabled[i] = true;

	if(i < m_analogChannelCount)
		m_transport->SendCommandQueued(string("SELECT:CH") + to_string(i+1) + " 1");
	else if(m_hasRF && (i == m_spectrumChannelBase))
	{} // RF is always on
	else if(m_digitalChannelCountMDO > 0 && (i >= m_digitalChannelBaseMDO) &&
		(i < m_digitalChannelBaseMDO + m_digitalChannelCountMDO))
	{
		int dchan = i - m_digitalChannelBaseMDO;
		m_transport->SendCommandQueued(string("SELECT:D") + to_string(dchan) + " 1");
	}
}

void TektronixMDO4000BOscilloscope::DisableChannel(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelsEnabled[i] = false;

	if(i < m_analogChannelCount)
		m_transport->SendCommandQueued(string("SELECT:CH") + to_string(i+1) + " 0");
	else if(m_hasRF && (i == m_spectrumChannelBase))
	{} // RF is always on
	else if(m_digitalChannelCountMDO > 0 && (i >= m_digitalChannelBaseMDO) &&
		(i < m_digitalChannelBaseMDO + m_digitalChannelCountMDO))
	{
		int dchan = i - m_digitalChannelBaseMDO;
		m_transport->SendCommandQueued(string("SELECT:D") + to_string(dchan) + " 0");
	}
}

OscilloscopeChannel::CouplingType TektronixMDO4000BOscilloscope::GetChannelCoupling(size_t i)
{
	if(!IsAnalog(i))
		return OscilloscopeChannel::COUPLE_DC_1M;

	//Check cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelCouplings.find(i) != m_channelCouplings.end())
			return m_channelCouplings[i];
	}

	string coup = m_transport->SendCommandQueuedWithReply(
		GetOscilloscopeChannel(i)->GetHwname() + ":COUP?");
	string term = m_transport->SendCommandQueuedWithReply(
		GetOscilloscopeChannel(i)->GetHwname() + ":TER?");

	OscilloscopeChannel::CouplingType coupling;
	if(coup == "AC")
		coupling = OscilloscopeChannel::COUPLE_AC_1M;
	else if(coup == "DC")
	{
		float nterm = stof(term);
		if(nterm == 50)
			coupling = OscilloscopeChannel::COUPLE_DC_50;
		else
			coupling = OscilloscopeChannel::COUPLE_DC_1M;
	}
	else
		coupling = OscilloscopeChannel::COUPLE_DC_1M;

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelCouplings[i] = coupling;
	return coupling;
}

void TektronixMDO4000BOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	if(!IsAnalog(i))
		return;

	switch(type)
	{
		case OscilloscopeChannel::COUPLE_DC_50:
			m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":COUP DC");
			m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":TER 50");
			break;
		case OscilloscopeChannel::COUPLE_AC_1M:
			m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":TER 1E+6");
			m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":COUP AC");
			break;
		case OscilloscopeChannel::COUPLE_DC_1M:
			m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":TER 1E+6");
			m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":COUP DC");
			break;
		default:
			LogError("Invalid coupling for channel\n");
			return;
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelCouplings[i] = type;
}

double TektronixMDO4000BOscilloscope::GetChannelAttenuation(size_t i)
{
	if(!IsAnalog(i))
		return 1;

	//Check cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelAttenuations.find(i) != m_channelAttenuations.end())
			return m_channelAttenuations[i];
	}

	string probegain_str = m_transport->SendCommandQueuedWithReply(
		GetOscilloscopeChannel(i)->GetHwname() + ":PROBE:GAIN?");
	float probegain = stof(probegain_str);
	if(probegain == 0)
		probegain = 1.0;
	double atten = 1.0 / probegain;

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelAttenuations[i] = atten;
	return atten;
}

void TektronixMDO4000BOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	if(!IsAnalog(i))
		return;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelAttenuations[i] = atten;
	}

	m_transport->SendCommandQueued(
		GetOscilloscopeChannel(i)->GetHwname() + ":PROBE:GAIN " + to_string(1.0 / atten));
}

unsigned int TektronixMDO4000BOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	if(!IsAnalog(i))
		return 0;

	//Check cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelBandwidthLimits.find(i) != m_channelBandwidthLimits.end())
			return m_channelBandwidthLimits[i];
	}

	string reply = m_transport->SendCommandQueuedWithReply(
		GetOscilloscopeChannel(i)->GetHwname() + ":BAN?");
	unsigned int bwl = 0;
	if(reply != "FUL")
		bwl = stof(reply) * 1e-6;

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelBandwidthLimits[i] = bwl;
	return bwl;
}

void TektronixMDO4000BOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	if(!IsAnalog(i))
		return;

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	if(limit_mhz == 0)
	{
		m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":BAN FUL");
		m_channelBandwidthLimits[i] = 0;
	}
	else
	{
		string val = to_string(limit_mhz * 1000000);
		m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":BAN " + val);
		m_channelBandwidthLimits[i] = limit_mhz;
	}
}

vector<unsigned int> TektronixMDO4000BOscilloscope::GetChannelBandwidthLimiters(size_t /*i*/)
{
	vector<unsigned int> ret;
	ret.push_back(20);		//20 MHz
	if(m_maxBandwidth >= 200)
		ret.push_back(200);	//200 MHz
	if(m_maxBandwidth >= 250)
		ret.push_back(250);	//250 MHz
	ret.push_back(0);		//Full
	return ret;
}

float TektronixMDO4000BOscilloscope::GetChannelOffset(size_t i, size_t /*stream*/)
{
	//Check cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelOffsets.find(i) != m_channelOffsets.end())
			return m_channelOffsets[i];
	}

	if(!IsAnalog(i))
		return 0;

	float offset = -stof(m_transport->SendCommandQueuedWithReply(
		GetOscilloscopeChannel(i)->GetHwname() + ":OFFS?"));

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
	return offset;
}

void TektronixMDO4000BOscilloscope::SetChannelOffset(size_t i, size_t /*stream*/, float offset)
{
	if(!IsAnalog(i))
		return;

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
	m_transport->SendCommandQueued(
		GetOscilloscopeChannel(i)->GetHwname() + ":OFFS " + to_string(-offset));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering

vector<string> TektronixMDO4000BOscilloscope::GetTriggerTypes()
{
	vector<string> ret;
	ret.push_back("EDGE");
	ret.push_back("PULSE");
	ret.push_back("WIDTH");
	ret.push_back("RUNT");
	ret.push_back("TRANSITION");
	ret.push_back("TIMEOUT");
	return ret;
}

Oscilloscope::TriggerMode TektronixMDO4000BOscilloscope::PollTrigger()
{
	if(!m_triggerArmed)
		return TRIGGER_MODE_STOP;

	lock_guard<recursive_mutex> lock(m_transport->GetMutex());
	m_transport->FlushCommandQueue();

	string busy = m_transport->SendCommandImmediateWithReply("BUSY?");
	if(busy == "0")
	{
		string nacq = m_transport->SendCommandImmediateWithReply("ACQ:NUMAC?");
		int64_t numAcq = stoll(nacq);
		if(numAcq > 0)
		{
			m_triggerArmed = false;
			return TRIGGER_MODE_TRIGGERED;
		}
	}

	return TRIGGER_MODE_RUN;
}

bool TektronixMDO4000BOscilloscope::PeekTriggerArmed()
{
	return m_triggerArmed;
}

void TektronixMDO4000BOscilloscope::Start()
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());
	lock_guard<recursive_mutex> lock2(m_cacheMutex);

	m_transport->SendCommandQueued("ACQ:STOPA RUNST");
	m_transport->SendCommandQueued("ACQ:STATE ON");
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void TektronixMDO4000BOscilloscope::StartSingleTrigger()
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());
	lock_guard<recursive_mutex> lock2(m_cacheMutex);

	m_transport->SendCommandQueued("ACQ:STOPA SEQ");
	m_transport->SendCommandQueued("ACQ:STATE ON");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void TektronixMDO4000BOscilloscope::Stop()
{
	m_triggerArmed = false;
	m_transport->SendCommandQueued("ACQ:STATE STOP");
}

void TektronixMDO4000BOscilloscope::ForceTrigger()
{
	m_triggerArmed = true;
	m_transport->SendCommandQueued("TRIG FORC");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Preamble parsing

bool TektronixMDO4000BOscilloscope::ReadWFMOutprePreamble(
	const string& preamble_in, struct mdo4k_preamble& preamble_out)
{
	struct mdo4k_preamble& p = preamble_out;
	memset(&p, 0, sizeof(p));

	//MDO4000B WFMOutpre format:
	// BYT_NR;BIT_NR;ENCDG;BN_FMT;BYT_OR;WFID;NR_PT;PT_FMT;PT_ORDER;XUNIT;XINCR;XZERO;PT_OFF;YUNIT;YMULT;YOFF;YZERO;DOMAIN;WFMTYPE;CENTERFREQ;SPAN

	//Dump raw preamble on first failure so we can diagnose the format
	static int dump_count = 0;

	size_t semicolons = std::count(preamble_in.begin(), preamble_in.end(), ';');
	if(semicolons < 18)
	{
		LogWarning("MDO4000B: preamble too short (%zu semicolons)\n", semicolons);
		if(dump_count < 3)
		{
			dump_count++;
			LogWarning("MDO4000B: raw preamble: %s\n", preamble_in.c_str());
		}
		return false;
	}

	//Strip the ":WFMOutpre:BYT_NR " / ":WFMOUTPRE:BYT_NR " header prefix if present
	string buf = preamble_in;
	if(buf.size() > 8)
	{
		auto colon = buf.find(':');
		if(colon != string::npos)
		{
			auto bytnr = buf.find("BYT_NR", colon);
			if(bytnr != string::npos)
				buf = buf.substr(bytnr + 7);
		}
	}

	//Parse raw semicolon-separated values with sscanf
	int read = sscanf(buf.c_str(),
		"%d;%d;%31[^;];%31[^;];%31[^;];"
		"%255[^;];%d;%7[^;];%31[^;];%31[^;];%lf;"
		"%lf;%d;%31[^;];%lf;%lf;%lf;%31[^;];%31[^;];%lf;%lf",
		&p.byte_nr, &p.bit_nr, p.encdg, p.bn_fmt, p.byt_or,
		p.wfid, &p.nr_pt, p.pt_fmt, p.pt_order, p.xunit, &p.xincrement,
		&p.xzero, &p.pt_off, p.yunit, &p.ymult, &p.yoff, &p.yzero,
		p.domain, p.wfmtype, &p.centerfreq, &p.span);

	if(read >= 21)
		return true;

	//sscanf failed -- try manual token-based parsing as fallback
	if(dump_count < 3)
	{
		dump_count++;
		LogWarning("MDO4000B: sscanf got %d fields, raw preamble: %s\n", read, preamble_in.c_str());
		LogWarning("MDO4000B: stripped preamble: %s\n", buf.c_str());
	}

	//Retry with token-by-token parsing
	size_t prev = 0;
	int field = 0;

	for(size_t i = 0; i <= semicolons; i++)
	{
		auto next = buf.find(';', prev);
		string token = buf.substr(prev, (next == string::npos) ? string::npos : next - prev);
		prev = (next == string::npos) ? string::npos : next + 1;

		//Skip any leading "KEYWORD " prefix (header-on format like "BYT_NR 1" vs "1")
		auto space = token.rfind(' ');
		if(space != string::npos)
		{
			auto suffix = token.substr(space + 1);
			if(!suffix.empty() && (isdigit(suffix[0]) || suffix[0] == '-' || suffix[0] == '+' || suffix[0] == '"' || suffix[0] == '.'))
				token = suffix;
		}

		if(token.empty())
		{
			field++;
			continue;
		}

		//Strip surrounding quotes
		if(token.size() >= 2 && token[0] == '"' && token[token.size()-1] == '"')
			token = token.substr(1, token.size()-2);

		//Map field index to target
		switch(field)
		{
			case 0: p.byte_nr = strtol(token.c_str(), nullptr, 10); break;
			case 1: p.bit_nr = strtol(token.c_str(), nullptr, 10); break;
			case 2: strncpy(p.encdg, token.c_str(), 31); break;
			case 3: strncpy(p.bn_fmt, token.c_str(), 31); break;
			case 4: strncpy(p.byt_or, token.c_str(), 31); break;
			case 5: strncpy(p.wfid, token.c_str(), 255); break;
			case 6: p.nr_pt = strtol(token.c_str(), nullptr, 10); break;
			case 7: strncpy(p.pt_fmt, token.c_str(), 7); break;
			case 8: strncpy(p.pt_order, token.c_str(), 31); break;
			case 9: strncpy(p.xunit, token.c_str(), 31); break;
			case 10: p.xincrement = strtod(token.c_str(), nullptr); break;
			case 11: p.xzero = strtod(token.c_str(), nullptr); break;
			case 12: p.pt_off = strtol(token.c_str(), nullptr, 10); break;
			case 13: strncpy(p.yunit, token.c_str(), 31); break;
			case 14: p.ymult = strtod(token.c_str(), nullptr); break;
			case 15: p.yoff = strtod(token.c_str(), nullptr); break;
			case 16: p.yzero = strtod(token.c_str(), nullptr); break;
			case 17: strncpy(p.domain, token.c_str(), 31); break;
			case 18: strncpy(p.wfmtype, token.c_str(), 31); break;
			case 19: p.centerfreq = strtod(token.c_str(), nullptr); break;
			case 20: p.span = strtod(token.c_str(), nullptr); break;
			default: break;
		}
		field++;
	}

	if(field >= 21)
		return true;

	LogWarning("MDO4000B: token parser also failed (%d/%zu fields)\n", field, semicolons);
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Data acquisition

bool TektronixMDO4000BOscilloscope::AcquireData()
{
	map<int, vector<WaveformBase*> > pending_waveforms;

	lock_guard<recursive_mutex> lock(m_transport->GetMutex());
	LogIndenter li;

	//Acquire analog channels
	if(!AcquireAnalogData(pending_waveforms))
	{
		for(auto& it : pending_waveforms)
			for(auto w : it.second)
				delete w;
		return false;
	}

	//Acquire RF spectrum data
	if(m_hasRF)
	{
		if(!AcquireRFData(pending_waveforms))
		{
			for(auto& it : pending_waveforms)
				for(auto w : it.second)
					delete w;
			return false;
		}
	}

	//Acquire digital data
	if(m_digitalChannelCountMDO > 0)
	{
		if(!AcquireDigitalData(pending_waveforms))
		{
			for(auto& it : pending_waveforms)
				for(auto w : it.second)
					delete w;
			return false;
		}
	}

	//Now save all pending waveforms as a single time-correlated set
	m_pendingWaveformsMutex.lock();
	for(size_t seg = 0; seg < 1; seg++)
	{
		SequenceSet s;
		for(size_t j = 0; j < m_channels.size(); j++)
		{
			if(IsChannelEnabled(j))
			{
				auto it = pending_waveforms.find(j);
				if((it != pending_waveforms.end()) && (seg < it->second.size()))
					s[GetOscilloscopeChannel(j)] = pending_waveforms[j][seg];
			}
		}
		m_pendingWaveforms.push_back(s);
	}
	m_pendingWaveformsMutex.unlock();

	//Re-arm the trigger if not in one-shot mode
	if(!m_triggerOneShot)
	{
		lock_guard<recursive_mutex> lock3(m_cacheMutex);
		m_transport->SendCommandImmediate("ACQ:STATE ON");
		m_triggerArmed = true;
	}

	return true;
}

bool TektronixMDO4000BOscilloscope::AcquireAnalogData(
	map<int, vector<WaveformBase*> >& pending_waveforms)
{
	bool first = true;

	for(size_t i = 0; i < m_analogChannelCount; i++)
	{
		if(!IsChannelEnabled(i))
		{
			pending_waveforms[i].push_back(nullptr);
			continue;
		}

		auto chan = GetOscilloscopeChannel(i);
		if(!chan)
		{
			pending_waveforms[i].push_back(nullptr);
			continue;
		}

		chan->SetYAxisUnits(Unit(Unit::UNIT_VOLTS), 0);

		bool succeeded = false;
		for(int retry = 0; retry < 3; retry++)
		{
			m_transport->SendCommandImmediate(string("DAT:SOU ") + chan->GetHwname());

			if(first || retry)
			{
				m_transport->SendCommandImmediate("DAT:WID 1");
				m_transport->SendCommandImmediate("DAT:ENC RIB");
				first = false;
			}

			//Get waveform preamble
			string preamble_str = m_transport->SendCommandImmediateWithReply("WFMO?", false);
			mdo4k_preamble preamble;
			if(!ReadWFMOutprePreamble(preamble_str, preamble))
				continue;

			//Constrain DAT:STOP to match the preamble's point count so CURV?
			//returns exactly the expected number of samples, not the full memory depth
			m_transport->SendCommandImmediate(
				string("DAT:STOP ") + to_string(preamble.nr_pt));

			size_t timebase = (size_t)(preamble.xincrement * (double)FS_PER_SECOND);
			m_channelOffsets[i] = -preamble.yoff;

			//Read the data block
			size_t nsamples;
			int8_t* samples = (int8_t*)m_transport->SendCommandImmediateWithRawBlockReply("CURV?", nsamples);
			if(samples == NULL)
			{
				LogWarning("MDO4000B: Didn't get samples for %s\n", chan->GetHwname().c_str());
				continue;
			}

			if(nsamples != (size_t)preamble.nr_pt)
			{
				LogWarning("MDO4000B: Wrong sample count for %s (got %zu, expected %d)\n",
					chan->GetHwname().c_str(), nsamples, preamble.nr_pt);
				delete[] samples;
				continue;
			}

			//Create the capture
			auto wf = new UniformAnalogWaveform;

			wf->m_timescale = timebase;
			wf->m_triggerPhase = 0;
			wf->m_startTimestamp = time(NULL);
			double t = GetTime();
			wf->m_startFemtoseconds = (t - floor(t)) * FS_PER_SECOND;
			wf->Resize(nsamples);
			wf->PrepareForCpuAccess();

			//Convert samples using preamble scale/offset
			for(size_t j = 0; j < nsamples; j++)
				wf->m_samples[j] = preamble.ymult * (samples[j] - preamble.yoff) + preamble.yzero;

			wf->MarkSamplesModifiedFromCpu();
			pending_waveforms[i].push_back(wf);

			delete[] samples;

			//Discard trailing newline
			m_transport->ReadReply();

			succeeded = true;
			break;
		}

		if(!succeeded)
		{
			LogError("MDO4000B: Failed to acquire %s\n", chan->GetHwname().c_str());
			return false;
		}
	}

	return true;
}

bool TektronixMDO4000BOscilloscope::AcquireDigitalData(
	map<int, vector<WaveformBase*> >& pending_waveforms)
{
	if(m_digitalChannelCountMDO == 0)
		return true;

	//Check if any digital channel is enabled
	bool anyEnabled = false;
	for(size_t i = 0; i < m_digitalChannelCountMDO; i++)
	{
		if(IsChannelEnabled(m_digitalChannelBaseMDO + i))
		{
			anyEnabled = true;
			break;
		}
	}
	if(!anyEnabled)
		return true;

	bool succeeded = false;
	for(int retry = 0; retry < 3; retry++)
	{
		m_transport->SendCommandImmediate("DAT:SOU DIG");
		m_transport->SendCommandImmediate("DAT:WID 4");
		m_transport->SendCommandImmediate("DAT:ENC SRI");

		string preamble_str = m_transport->SendCommandImmediateWithReply("WFMO?", false);
		mdo4k_preamble preamble;
		if(!ReadWFMOutprePreamble(preamble_str, preamble))
			continue;

		m_transport->SendCommandImmediate(
			string("DAT:STOP ") + to_string(preamble.nr_pt));

		size_t timebase = (size_t)(preamble.xincrement * (double)FS_PER_SECOND);

		size_t msglen;
		char* samples = (char*)m_transport->SendCommandImmediateWithRawBlockReply("CURV?", msglen);
		if(samples == NULL)
		{
			LogWarning("MDO4000B: Didn't get digital samples\n");
			continue;
		}

		size_t nsamples = msglen / 4;

		//Process each digital channel
		for(size_t j = 0; j < min<size_t>(m_digitalChannelCountMDO, 16); j++)
		{
			auto cap = new SparseDigitalWaveform;
			cap->m_timescale = timebase;
			cap->m_triggerPhase = 0;
			cap->m_startTimestamp = time(NULL);
			double t = GetTime();
			cap->m_startFemtoseconds = (t - floor(t)) * FS_PER_SECOND;
			cap->Reserve(nsamples);
			cap->PrepareForCpuAccess();

			int mask = (1 << j);
			bool last = (samples[0] & mask) ? true : false;

			cap->m_offsets.push_back(0);
			cap->m_durations.push_back(1);
			cap->m_samples.push_back(last);

			for(size_t m = 1; m < nsamples; m++)
			{
				bool sample = (samples[m * 4] & mask) ? true : false;
				if(last == sample)
					cap->m_durations[cap->m_samples.size() - 1]++;
				else
				{
					cap->m_offsets.push_back(m);
					cap->m_durations.push_back(1);
					cap->m_samples.push_back(sample);
					last = sample;
				}
			}

			cap->MarkSamplesModifiedFromCpu();
			cap->MarkTimestampsModifiedFromCpu();
			pending_waveforms[m_digitalChannelBaseMDO + j].push_back(cap);
		}

		delete[] samples;
		m_transport->ReadReply();

		succeeded = true;
		break;
	}

	if(!succeeded)
	{
		LogError("MDO4000B: Failed to acquire digital data\n");
		return false;
	}

	return true;
}

bool TektronixMDO4000BOscilloscope::AcquireRFData(
	map<int, vector<WaveformBase*> >& pending_waveforms)
{
	if(!m_hasRF)
		return true;

	size_t nchan = m_spectrumChannelBase;
	if(!IsChannelEnabled(nchan))
	{
		pending_waveforms[nchan].push_back(nullptr);
		return true;
	}

	//Set source to RF frequency domain trace
	m_transport->SendCommandImmediate("DAT:SOU RF_NORM");
	m_transport->SendCommandImmediate("DAT:WID 4");	//4-byte float
	m_transport->SendCommandImmediate("DAT:ENC SFP"); //IEEE754 float, little-endian

	string preamble_str = m_transport->SendCommandImmediateWithReply("WFMO?", false);
	mdo4k_preamble preamble;
	if(!ReadWFMOutprePreamble(preamble_str, preamble))
	{
		LogWarning("MDO4000B: bad RF preamble\n");
		return false;
	}

	size_t msglen;
	float* samples = (float*)m_transport->SendCommandImmediateWithRawBlockReply("CURV?", msglen);
	if(samples == NULL)
	{
		LogWarning("MDO4000B: Didn't get RF samples\n");
		return false;
	}

	size_t nsamples = msglen / 4;

	auto cap = new UniformAnalogWaveform;
	cap->m_timescale = (size_t)(preamble.xincrement * (double)FS_PER_SECOND);
	cap->m_triggerPhase = 0;
	cap->m_startTimestamp = time(nullptr);
	double t = GetTime();
	cap->m_startFemtoseconds = (t - floor(t)) * FS_PER_SECOND;
	cap->Resize(nsamples);
	cap->PrepareForCpuAccess();

	for(size_t j = 0; j < nsamples; j++)
		cap->m_samples[j] = preamble.ymult * samples[j] + preamble.yzero;

	cap->MarkSamplesModifiedFromCpu();

	pending_waveforms[nchan].push_back(cap);
	delete[] samples;
	m_transport->ReadReply();

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Trigger configuration

float TektronixMDO4000BOscilloscope::ReadTriggerLevel(OscilloscopeChannel* chan)
{
	return stof(m_transport->SendCommandQueuedWithReply(
		string("TRIG:A:LEV:") + chan->GetHwname() + "?"));
}

void TektronixMDO4000BOscilloscope::SetTriggerLevel(Trigger* trig)
{
	auto chan = trig->GetInput(0).m_channel;
	if(chan)
	{
		float level = trig->GetLevel();
		m_transport->SendCommandQueued(
			string("TRIG:A:LEV:") + chan->GetHwname() + " " + to_string_sci(level));
	}
}

void TektronixMDO4000BOscilloscope::PushTrigger()
{
	auto trig = m_trigger;
	if(!trig)
		return;

	auto et = dynamic_cast<EdgeTrigger*>(trig);
	auto pt = dynamic_cast<PulseWidthTrigger*>(trig);
	auto dt = dynamic_cast<DropoutTrigger*>(trig);
	auto rt = dynamic_cast<RuntTrigger*>(trig);
	auto st = dynamic_cast<SlewRateTrigger*>(trig);
	auto wt = dynamic_cast<WindowTrigger*>(trig);

	if(pt)
		PushPulseWidthTrigger(pt);
	else if(dt)
		PushDropoutTrigger(dt);
	else if(rt)
		PushRuntTrigger(rt);
	else if(st)
		PushSlewRateTrigger(st);
	else if(wt)
		PushWindowTrigger(wt);
	else if(et)
		PushEdgeTrigger(et);
	else
	{
		LogWarning("MDO4000B: unknown trigger type\n");
		return;
	}

	SetTriggerLevel(trig);
}

void TektronixMDO4000BOscilloscope::PullTrigger()
{
	string ttype = m_transport->SendCommandQueuedWithReply("TRIG:A:TYP?");
	Trim(ttype);

	if(ttype == "EDGE")
		PullEdgeTrigger();
	else if(ttype == "PUL")
	{
		string pclass = m_transport->SendCommandQueuedWithReply("TRIG:A:PUL:CLAS?");
		Trim(pclass);

		if(pclass == "WID" || pclass == "WIDTH")
			PullPulseWidthTrigger();
		else if(pclass == "TIM" || pclass == "TIMEOUT")
			PullDropoutTrigger();
		else if(pclass == "RUN" || pclass == "RUNT")
			PullRuntTrigger();
		else if(pclass == "TRA" || pclass == "TRANSITION")
			PullSlewRateTrigger();
		else
		{
			LogWarning("MDO4000B: unknown pulse trigger class %s\n", pclass.c_str());
			m_trigger = nullptr;
		}
	}
	else
	{
		LogWarning("MDO4000B: unknown trigger type %s\n", ttype.c_str());
		m_trigger = nullptr;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Push/Pull trigger helpers

void TektronixMDO4000BOscilloscope::PushEdgeTrigger(EdgeTrigger* trig)
{
	m_transport->SendCommandQueued("TRIG:A:TYP EDGE");

	m_transport->SendCommandQueued(
		string("TRIG:A:EDGE:SOU ") + trig->GetInput(0).m_channel->GetHwname());

	switch(trig->GetType())
	{
		case EdgeTrigger::EDGE_RISING:
			m_transport->SendCommandQueued("TRIG:A:EDGE:SLO RISE");
			break;
		case EdgeTrigger::EDGE_FALLING:
			m_transport->SendCommandQueued("TRIG:A:EDGE:SLO FALL");
			break;
		case EdgeTrigger::EDGE_ANY:
			m_transport->SendCommandQueued("TRIG:A:EDGE:SLO ANY");
			break;
		default:
			break;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sample rate and depth

uint64_t TektronixMDO4000BOscilloscope::GetSampleRate()
{
	if(m_sampleRateValid)
		return m_sampleRate;

	m_sampleRate = stod(m_transport->SendCommandQueuedWithReply("HOR:SAMPLER?"));
	m_sampleRateValid = true;
	return m_sampleRate;
}

uint64_t TektronixMDO4000BOscilloscope::GetSampleDepth()
{
	if(m_sampleDepthValid)
		return m_sampleDepth;

	m_sampleDepth = stoull(m_transport->SendCommandQueuedWithReply("HOR:RECO?"));
	m_transport->SendCommandQueued("DAT:START 1");
	m_transport->SendCommandQueued(string("DAT:STOP ") + to_string(m_sampleDepth));
	m_sampleDepthValid = true;
	return m_sampleDepth;
}

void TektronixMDO4000BOscilloscope::SetSampleDepth(uint64_t depth)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_sampleDepth = depth;
	m_sampleDepthValid = true;

	m_transport->SendCommandQueued(string("HOR:RECO ") + to_string(depth));
	m_transport->SendCommandQueued("DAT:START 1");
	m_transport->SendCommandQueued(string("DAT:STOP ") + to_string(depth));
}

void TektronixMDO4000BOscilloscope::SetSampleRate(uint64_t /*rate*/)
{
	//MDO uses HOR:SCAle to set timebase, not direct sample rate
}

void TektronixMDO4000BOscilloscope::SetTriggerOffset(int64_t offset)
{
	double capture_len_sec = 1.0 * GetSampleDepth() / GetSampleRate();
	double offset_sec = offset * SECONDS_PER_FS;
	double pct = (offset_sec / capture_len_sec) * 100.0;

	m_transport->SendCommandQueued(string("HOR:POS ") + to_string(pct));

	m_triggerOffsetValid = false;
}

void TektronixMDO4000BOscilloscope::PushPulseWidthTrigger(PulseWidthTrigger* trig)
{
	m_transport->SendCommandQueued("TRIG:A:TYP PUL");
	m_transport->SendCommandQueued("TRIG:A:PUL:CLAS WID");

	m_transport->SendCommandQueued(
		string("TRIG:A:PUL:SOU ") + trig->GetInput(0).m_channel->GetHwname());

	if(trig->GetType() == EdgeTrigger::EDGE_RISING)
		m_transport->SendCommandQueued("TRIG:A:PUL:POL POS");
	else
		m_transport->SendCommandQueued("TRIG:A:PUL:POL NEG");

	switch(trig->GetCondition())
	{
		case Trigger::CONDITION_EQUAL:
			m_transport->SendCommandQueued("TRIG:A:PUL:WHE EQU");
			break;
		case Trigger::CONDITION_GREATER:
			m_transport->SendCommandQueued("TRIG:A:PUL:WHE GRE");
			break;
		case Trigger::CONDITION_LESS:
			m_transport->SendCommandQueued("TRIG:A:PUL:WHE LES");
			break;
		default:
			break;
	}

	m_transport->SendCommandQueued(string("TRIG:A:PUL:WID ") + to_string_sci(trig->GetUpperBound()));
}

void TektronixMDO4000BOscilloscope::PullPulseWidthTrigger()
{
	//Clear out any triggers of the wrong type
	if( (m_trigger != NULL) && (dynamic_cast<PulseWidthTrigger*>(m_trigger) != NULL) )
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new PulseWidthTrigger(this);
	PulseWidthTrigger* et = dynamic_cast<PulseWidthTrigger*>(m_trigger);

	//Source channel
	string reply = Trim(m_transport->SendCommandQueuedWithReply("TRIG:A:PUL:SOU?"));
	for(size_t i = 0; i < m_channels.size(); i++)
	{
		auto chan = GetOscilloscopeChannel(i);
		if(chan && (chan->GetHwname() == reply))
		{
			et->SetInput(0, StreamDescriptor(chan, 0));

			//Trigger level
			et->SetLevel(ReadTriggerLevel(chan));
			break;
		}
	}

	//Edge slope
	reply = Trim(m_transport->SendCommandQueuedWithReply("TRIG:A:PUL:POL?"));
	if(reply == "POS")
		et->SetType(EdgeTrigger::EDGE_RISING);
	else
		et->SetType(EdgeTrigger::EDGE_FALLING);

	//Condition
	reply = Trim(m_transport->SendCommandQueuedWithReply("TRIG:A:PUL:WHE?"));
	if(reply == "LES")
		et->SetCondition(Trigger::CONDITION_LESS);
	else if(reply == "GRE")
		et->SetCondition(Trigger::CONDITION_GREATER);
	else if(reply == "EQU")
		et->SetCondition(Trigger::CONDITION_EQUAL);

	//Pulse width (in seconds)
	Unit fs(Unit::UNIT_FS);
	et->SetUpperBound(fs.ParseString(m_transport->SendCommandQueuedWithReply("TRIG:A:PUL:WID?")));
}

void TektronixMDO4000BOscilloscope::PushDropoutTrigger(DropoutTrigger* trig)
{
	m_transport->SendCommandQueued("TRIG:A:TYP PUL");
	m_transport->SendCommandQueued("TRIG:A:PUL:CLAS TIM");

	m_transport->SendCommandQueued(
		string("TRIG:A:TIME:SOU ") + trig->GetInput(0).m_channel->GetHwname());

	if(trig->GetType() == DropoutTrigger::EDGE_RISING)
		m_transport->SendCommandQueued("TRIG:A:TIME:POL POS");
	else if(trig->GetType() == DropoutTrigger::EDGE_FALLING)
		m_transport->SendCommandQueued("TRIG:A:TIME:POL NEG");
	else
		m_transport->SendCommandQueued("TRIG:A:TIME:POL EIT");

	m_transport->SendCommandQueued(string("TRIG:A:TIME:TIM ") + to_string_sci(trig->GetDropoutTime()));
}

void TektronixMDO4000BOscilloscope::PullDropoutTrigger()
{
	//Clear out any triggers of the wrong type
	if( (m_trigger != NULL) && (dynamic_cast<DropoutTrigger*>(m_trigger) != NULL) )
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new DropoutTrigger(this);
	DropoutTrigger* et = dynamic_cast<DropoutTrigger*>(m_trigger);

	//Source channel
	string reply = Trim(m_transport->SendCommandQueuedWithReply("TRIG:A:TIME:SOU?"));
	for(size_t i = 0; i < m_channels.size(); i++)
	{
		auto chan = GetOscilloscopeChannel(i);
		if(chan && (chan->GetHwname() == reply))
		{
			et->SetInput(0, StreamDescriptor(chan, 0));

			//Trigger level
			et->SetLevel(ReadTriggerLevel(chan));
			break;
		}
	}

	//Edge slope
	reply = Trim(m_transport->SendCommandQueuedWithReply("TRIG:A:TIME:POL?"));
	if(reply == "POS")
		et->SetType(DropoutTrigger::EDGE_RISING);
	else if(reply == "NEG")
		et->SetType(DropoutTrigger::EDGE_FALLING);
	else if(reply == "EIT")
		et->SetType(DropoutTrigger::EDGE_ANY);

	//Timeout time
	Unit fs(Unit::UNIT_FS);
	et->SetDropoutTime(fs.ParseString(m_transport->SendCommandQueuedWithReply("TRIG:A:TIME:TIM?")));
}

void TektronixMDO4000BOscilloscope::PushRuntTrigger(RuntTrigger* trig)
{
	m_transport->SendCommandQueued("TRIG:A:TYP PUL");
	m_transport->SendCommandQueued("TRIG:A:PUL:CLAS RUN");

	m_transport->SendCommandQueued(
		string("TRIG:A:RUN:SOU ") + trig->GetInput(0).m_channel->GetHwname());

	{
		auto hwname = trig->GetInput(0).m_channel->GetHwname();
		m_transport->SendCommandQueued(
			string("TRIG:A:LOW:") + hwname + " " + to_string_sci(trig->GetLowerBound()));
		m_transport->SendCommandQueued(
			string("TRIG:A:UPP:") + hwname + " " + to_string_sci(trig->GetUpperBound()));
	}

	if(trig->GetSlope() == RuntTrigger::EDGE_RISING)
		m_transport->SendCommandQueued("TRIG:A:RUN:POL POS");
	else
		m_transport->SendCommandQueued("TRIG:A:RUN:POL NEG");
}

void TektronixMDO4000BOscilloscope::PullRuntTrigger()
{
	//Clear out any triggers of the wrong type
	if( (m_trigger != NULL) && (dynamic_cast<RuntTrigger*>(m_trigger) != NULL) )
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new RuntTrigger(this);
	RuntTrigger* et = dynamic_cast<RuntTrigger*>(m_trigger);

	//Source channel
	string reply = Trim(m_transport->SendCommandQueuedWithReply("TRIG:A:RUN:SOU?"));
	auto chan = GetOscilloscopeChannelByHwName(reply);
	if(chan)
	{
		et->SetInput(0, StreamDescriptor(chan, 0));

		//Lower and upper thresholds
		et->SetLowerBound(stof(m_transport->SendCommandQueuedWithReply(
			string("TRIG:A:LOW:") + reply + "?")));
		et->SetUpperBound(stof(m_transport->SendCommandQueuedWithReply(
			string("TRIG:A:UPP:") + reply + "?")));
	}

	//Edge slope
	reply = Trim(m_transport->SendCommandQueuedWithReply("TRIG:A:RUN:POL?"));
	if(reply == "POS")
		et->SetSlope(RuntTrigger::EDGE_RISING);
	else
		et->SetSlope(RuntTrigger::EDGE_FALLING);
}

void TektronixMDO4000BOscilloscope::PushSlewRateTrigger(SlewRateTrigger* trig)
{
	m_transport->SendCommandQueued("TRIG:A:TYP PUL");
	m_transport->SendCommandQueued("TRIG:A:PUL:CLAS TRA");

	m_transport->SendCommandQueued(
		string("TRIG:A:TRA:SOU ") + trig->GetInput(0).m_channel->GetHwname());

	{
		auto hwname = trig->GetInput(0).m_channel->GetHwname();
		m_transport->SendCommandQueued(
			string("TRIG:A:LOW:") + hwname + " " + to_string_sci(trig->GetLowerBound()));
		m_transport->SendCommandQueued(
			string("TRIG:A:UPP:") + hwname + " " + to_string_sci(trig->GetUpperBound()));
	}

	if(trig->GetSlope() == SlewRateTrigger::EDGE_RISING)
		m_transport->SendCommandQueued("TRIG:A:TRA:POL POS");
	else if(trig->GetSlope() == SlewRateTrigger::EDGE_FALLING)
		m_transport->SendCommandQueued("TRIG:A:TRA:POL NEG");
	else
		m_transport->SendCommandQueued("TRIG:A:TRA:POL EIT");

	switch(trig->GetCondition())
	{
		case Trigger::CONDITION_LESS:
			m_transport->SendCommandQueued("TRIG:A:TRA:WHE FAST");
			break;
		case Trigger::CONDITION_GREATER:
			m_transport->SendCommandQueued("TRIG:A:TRA:WHE SLOW");
			break;
		case Trigger::CONDITION_EQUAL:
			m_transport->SendCommandQueued("TRIG:A:TRA:WHE EQU");
			break;
		case Trigger::CONDITION_NOT_EQUAL:
			m_transport->SendCommandQueued("TRIG:A:TRA:WHE UNEQ");
			break;
		default:
			break;
	}

	m_transport->SendCommandQueued(string("TRIG:A:TRA:DELT ") +
		to_string_sci(trig->GetLowerInterval() * SECONDS_PER_FS));
}

void TektronixMDO4000BOscilloscope::PullSlewRateTrigger()
{
	//Clear out any triggers of the wrong type
	if( (m_trigger != NULL) && (dynamic_cast<SlewRateTrigger*>(m_trigger) != NULL) )
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new SlewRateTrigger(this);
	SlewRateTrigger* et = dynamic_cast<SlewRateTrigger*>(m_trigger);

	//Source channel
	string reply = Trim(m_transport->SendCommandQueuedWithReply("TRIG:A:TRA:SOU?"));
	auto chan = GetOscilloscopeChannelByHwName(reply);
	if(chan)
	{
		et->SetInput(0, StreamDescriptor(chan, 0));

		//Lower and upper thresholds
		et->SetLowerBound(stof(m_transport->SendCommandQueuedWithReply(
			string("TRIG:A:LOW:") + reply + "?")));
		et->SetUpperBound(stof(m_transport->SendCommandQueuedWithReply(
			string("TRIG:A:UPP:") + reply + "?")));
	}

	//Edge slope
	reply = Trim(m_transport->SendCommandQueuedWithReply("TRIG:A:TRA:POL?"));
	if(reply == "POS")
		et->SetSlope(SlewRateTrigger::EDGE_RISING);
	else if(reply == "NEG")
		et->SetSlope(SlewRateTrigger::EDGE_FALLING);
	else if(reply == "EIT")
		et->SetSlope(SlewRateTrigger::EDGE_ANY);

	//Condition
	reply = Trim(m_transport->SendCommandQueuedWithReply("TRIG:A:TRA:WHE?"));
	if(reply == "FAST")
		et->SetCondition(Trigger::CONDITION_LESS);
	else if(reply == "SLOW")
		et->SetCondition(Trigger::CONDITION_GREATER);
	else if(reply == "EQU")
		et->SetCondition(Trigger::CONDITION_EQUAL);
	else if(reply == "UNEQ")
		et->SetCondition(Trigger::CONDITION_NOT_EQUAL);

	//Delta time (transition time)
	Unit fs(Unit::UNIT_FS);
	int64_t delt = fs.ParseString(m_transport->SendCommandQueuedWithReply("TRIG:A:TRA:DELT?"));
	et->SetLowerInterval(delt);
	et->SetUpperInterval(delt);
}

void TektronixMDO4000BOscilloscope::PushWindowTrigger(WindowTrigger* /*trig*/)
{
	LogWarning("MDO4000B: window trigger not supported\n");
}

void TektronixMDO4000BOscilloscope::PullWindowTrigger()
{
	LogWarning("MDO4000B: window trigger not supported, cannot pull\n");
	m_trigger = NULL;
}

void TektronixMDO4000BOscilloscope::PullEdgeTrigger()
{
	auto trig = new EdgeTrigger(this);
	m_trigger = trig;
	trig->SetType(EdgeTrigger::EDGE_RISING);

	string src = m_transport->SendCommandQueuedWithReply("TRIG:A:EDGE:SOU?");
	Trim(src);
	for(size_t i = 0; i < m_channels.size(); i++)
	{
		auto chan = GetOscilloscopeChannel(i);
		if(chan && (chan->GetHwname() == src))
			trig->SetInput(0, StreamDescriptor(chan, 0));
	}

	string slope = m_transport->SendCommandQueuedWithReply("TRIG:A:EDGE:SLO?");
	Trim(slope);
	if(slope == "RISE")
		trig->SetType(EdgeTrigger::EDGE_RISING);
	else if(slope == "FALL")
		trig->SetType(EdgeTrigger::EDGE_FALLING);
	else if(slope == "ANY")
		trig->SetType(EdgeTrigger::EDGE_ANY);
}

int64_t TektronixMDO4000BOscilloscope::GetTriggerOffset()
{
	if(m_triggerOffsetValid)
		return m_triggerOffset;

	m_transport->FlushCommandQueue();

	string reply = m_transport->SendCommandQueuedWithReply("HOR:POS?");
	double pct = stod(reply);

	double capture_len_sec = 1.0 * GetSampleDepth() / GetSampleRate();
	double offset_sec = (pct / 100.0) * capture_len_sec;

	m_triggerOffset = (int64_t)round(offset_sec * FS_PER_SECOND);
	m_triggerOffsetValid = true;
	return m_triggerOffset;
}

bool TektronixMDO4000BOscilloscope::IsInterleaving()
{
	return false;
}

bool TektronixMDO4000BOscilloscope::SetInterleaving(bool /*combine*/)
{
	return false;
}

vector<uint64_t> TektronixMDO4000BOscilloscope::GetSampleRatesNonInterleaved()
{
	vector<uint64_t> ret;
	const int64_t k = 1000;
	const int64_t m = k * k;

	uint64_t bases[] = { 1000, 1250, 2500, 3125, 5000, 6250 };
	vector<uint64_t> scales = {1, 10, 100, 1*k};

	for(auto b : bases)
		ret.push_back(b / 10);

	for(auto scale : scales)
	{
		for(auto b : bases)
			ret.push_back(b * scale);
	}

	ret.push_back(12500 * k);
	ret.push_back(25 * m);
	ret.push_back(31250 * k);
	ret.push_back(62500 * k);
	ret.push_back(125 * m);
	ret.push_back(250 * m);
	ret.push_back(312500 * k);
	ret.push_back(625 * m);
	ret.push_back(1250 * m);
	ret.push_back(1562500 * k);
	ret.push_back(3125 * m);
	ret.push_back(6250 * m);
	ret.push_back(12500 * m);

	return ret;
}

vector<uint64_t> TektronixMDO4000BOscilloscope::GetSampleRatesInterleaved()
{
	return GetSampleRatesNonInterleaved();
}

set<Oscilloscope::InterleaveConflict> TektronixMDO4000BOscilloscope::GetInterleaveConflicts()
{
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> TektronixMDO4000BOscilloscope::GetSampleDepthsNonInterleaved()
{
	vector<uint64_t> ret;
	const int64_t k = 1000;
	const int64_t m = k * k;

	ret.push_back(1 * k);
	ret.push_back(10 * k);
	ret.push_back(100 * k);
	ret.push_back(1 * m);
	ret.push_back(5 * m);
	ret.push_back(10 * m);
	ret.push_back(20 * m);

	return ret;
}

vector<uint64_t> TektronixMDO4000BOscilloscope::GetSampleDepthsInterleaved()
{
	return GetSampleDepthsNonInterleaved();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Digital (logic analyzer)

vector<Oscilloscope::DigitalBank> TektronixMDO4000BOscilloscope::GetDigitalBanks()
{
	vector<DigitalBank> ret;
	if(m_digitalChannelCountMDO > 0)
	{
		DigitalBank bank;
		for(size_t i = 0; i < m_digitalChannelCountMDO; i++)
		{
			auto chan = GetOscilloscopeChannel(m_digitalChannelBaseMDO + i);
			if(chan)
				bank.push_back(chan);
		}
		ret.push_back(bank);
	}
	return ret;
}

Oscilloscope::DigitalBank TektronixMDO4000BOscilloscope::GetDigitalBank(size_t channel)
{
	DigitalBank bank;
	if(channel >= m_digitalChannelBaseMDO &&
		channel < m_digitalChannelBaseMDO + m_digitalChannelCountMDO)
	{
		auto chan = GetOscilloscopeChannel(channel);
		if(chan)
			bank.push_back(chan);
	}
	return bank;
}

bool TektronixMDO4000BOscilloscope::IsDigitalHysteresisConfigurable()
{
	return false;
}

bool TektronixMDO4000BOscilloscope::IsDigitalThresholdConfigurable()
{
	return (m_digitalChannelCountMDO > 0);
}

float TektronixMDO4000BOscilloscope::GetDigitalThreshold(size_t channel)
{
	if(channel >= m_digitalChannelBaseMDO &&
		channel < m_digitalChannelBaseMDO + m_digitalChannelCountMDO)
	{
		int dchan = channel - m_digitalChannelBaseMDO;
		return stof(m_transport->SendCommandQueuedWithReply(
			string("D") + to_string(dchan) + ":THR?"));
	}
	return 1.4;	//Default TTL threshold
}

void TektronixMDO4000BOscilloscope::SetDigitalThreshold(size_t channel, float level)
{
	if(channel >= m_digitalChannelBaseMDO &&
		channel < m_digitalChannelBaseMDO + m_digitalChannelCountMDO)
	{
		int dchan = channel - m_digitalChannelBaseMDO;
		m_transport->SendCommandQueued(
			string("D") + to_string(dchan) + ":THR " + to_string(level));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Spectrum analyzer (RF input)

bool TektronixMDO4000BOscilloscope::HasFrequencyControls()
{
	return m_hasRF;
}

void TektronixMDO4000BOscilloscope::SetSpan(int64_t span)
{
	m_spanValid = false;
	m_transport->SendCommandQueued(string("RF:SPAN ") + to_string(span));
	m_span = span;
	m_spanValid = true;
}

int64_t TektronixMDO4000BOscilloscope::GetSpan()
{
	if(m_spanValid)
		return m_span;

	m_span = stoll(m_transport->SendCommandQueuedWithReply("RF:SPAN?"));
	m_spanValid = true;
	return m_span;
}

void TektronixMDO4000BOscilloscope::SetCenterFrequency(size_t /*channel*/, int64_t freq)
{
	m_transport->SendCommandQueued(string("RF:FREQ ") + to_string(freq));
	m_rbwValid = false;
}

int64_t TektronixMDO4000BOscilloscope::GetCenterFrequency(size_t channel)
{
	if(m_hasRF && (channel == m_spectrumChannelBase))
	{
		auto it = m_channelCenterFrequencies.find(channel);
		if(it != m_channelCenterFrequencies.end())
			return it->second;
	}

	return stoll(m_transport->SendCommandQueuedWithReply("RF:FREQ?"));
}

void TektronixMDO4000BOscilloscope::SetResolutionBandwidth(int64_t rbw)
{
	m_rbwValid = false;
	m_transport->SendCommandQueued("RF:RBW:MOD MAN");
	m_transport->SendCommandQueued(string("RF:RBW ") + to_string(rbw));
	m_rbw = rbw;
	m_rbwValid = true;
}

int64_t TektronixMDO4000BOscilloscope::GetResolutionBandwidth()
{
	if(m_rbwValid)
		return m_rbw;

	m_rbw = stoll(m_transport->SendCommandQueuedWithReply("RF:RBW?"));
	m_rbwValid = true;
	return m_rbw;
}
