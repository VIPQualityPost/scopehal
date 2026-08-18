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
	@brief Implementation of TektronixMDOOscilloscope

	@ingroup scopedrivers
 */

#include "scopehal.h"
#include "TektronixMDOOscilloscope.h"
#include "EdgeTrigger.h"
#include "PulseWidthTrigger.h"
#include "DropoutTrigger.h"
#include "RuntTrigger.h"
#include "SlewRateTrigger.h"
#include "WindowTrigger.h"
#include "Waveform.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

TektronixMDOOscilloscope::TektronixMDOOscilloscope(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, TektronixOscilloscope(transport)
{
	//The base class (TektronixOscilloscope) already ran its constructor.

	//Now send MDO-specific setup commands
	//Re-check for digital channels (MSO option)
	string cfg_digital = m_transport->SendCommandImmediateWithReply("CONFIG:DIGITAL:NUMCHAN?");
	if(!cfg_digital.empty())
		m_digitalChannelCount = stoul(cfg_digital);

	if(m_digitalChannelCount > 0)
		LogDebug("MDO has %zu digital channels\n", m_digitalChannelCount);
	else
		LogDebug("No digital channels availablle.");


	//Determine whether the instrument has the integrated RF spectrum analyzer.
	//MSO/DPO4000B have no RF input, while the MDO4000/B series and MDO3000 always
	//do. The MDO4000C series only has the spectrum analyzer with option SA3 or SA6
	//installed; option-less C models have an Aux In connector instead, and the RF
	//input replaces the Aux In connector, so on C models their presence is
	//complementary.
	//MDO models can also encode the RF end in the model suffix: -0 = no RF,
	//-3 = 3 GHz RF, -6 = 6 GHz RF (e.g. MDO4104-0 has no RF front end). A numeric
	//suffix is authoritative; otherwise fall back to the family defaults below and,
	//for C models, disambiguate with CONFIG:AUXIN? (per the programmer manual).
	m_hasRF = false;
	if(m_model.find("MDO") == 0)
	{
		//MDO models without a numeric suffix always have RF (MDO4000/B, MDO3000)
		m_hasRF = true;
		bool rf_suffix_seen = false;

		auto dashpos = m_model.find('-');
		if((dashpos != string::npos) && (dashpos + 1 < m_model.size()))
		{
			char rfcode = m_model[dashpos + 1];
			if((rfcode >= '0') && (rfcode <= '9'))
			{
				rf_suffix_seen = true;
				m_hasRF = (rfcode != '0');
				LogDebug("MDO: model RF suffix -%c, spectrum analyzer %s\n",
					rfcode, m_hasRF ? "present" : "absent");
			}
		}

		//C models without SA3/SA6 (no RF) have an Aux In connector; if the suffix
		//did not already say, ask the instrument which is fitted
		if(!rf_suffix_seen && m_hasRF &&
			(m_model.find("MDO4") == 0) && (m_model.find('C') != string::npos))
		{
			try
			{
				m_hasRF = (Trim(m_transport->SendCommandQueuedWithReply("CONFIG:AUXIN?")) != "1");
			}
			catch(const exception& e)
			{
				//Assume RF is present, preserving the previous behavior for MDO models
				LogWarning("MDO: CONFIG:AUXIN? failed (%s), assuming RF is present\n",
					e.what());
			}
		}
	}

	//Determine whether the Aux In connector is present and usable as an external
	//trigger input. Per the programmer manual only MSO/DPO4000B series models,
	//2-channel MDO3000 models, and MDO4000C models without option SA3/SA6 have
	//it; on MDO4000/B, MDO4000C with SA3/SA6, and 4-channel MDO3000 models the
	//RF input replaces it, so there is no external trigger input at all.
	m_hasAuxIn = !m_hasRF;
	if((m_model.find("MDO3") == 0) && (m_model.size() >= 7) && (m_model[6] == '2'))
		m_hasAuxIn = true;	//2-channel MDO3000 has both RF and Aux In

	//Configure the Aux Out port to output the A trigger signal. This is the
	//documented default; set it explicitly so the EXT connector is a trigger
	//output on models where it cannot be a trigger input.
	m_transport->SendCommandQueued("AUXOUT:SOURCE ATRIGGER");

	//Create the external trigger input channel for models with an Aux In
	//connector (the base class created none for the MDO family, see the
	//FAMILY_MDO4 case in TektronixOscilloscope::TektronixOscilloscope).
	//Name it AUX to match the SCPI source literal (TRIG:A:EDGE:SOU AUX,
	//TRIG:A:LEV:AUX).
	if(!m_extTrigChannel && m_hasAuxIn)
	{
		m_extTrigChannel = new OscilloscopeChannel(
			this,
			"AUX",
			"",
			Unit(Unit::UNIT_FS),
			Unit(Unit::UNIT_VOLTS),
			Stream::STREAM_TYPE_TRIGGER,
			m_channels.size());
		m_channels.push_back(m_extTrigChannel);
	}

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

		//Set initial display range: assume ~100 dB full scale
		m_channelVoltageRanges[m_spectrumChannelBase] = 100;
		m_channelOffsets[m_spectrumChannelBase] = -50;
	}

	//Add digital channels if present
	if(m_digitalChannelCount > 0)
	{
		m_digitalChannelBase = m_channels.size();

		for(size_t i = 0; i < m_digitalChannelCount; i++)
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

			m_channels.push_back(chan);
		}
	}

	//Re-detect probe/channel state now that we're fully constructed
	//(the base class constructor's DetectProbes call had wrong virtual dispatch)
	FlushConfigCache();
}

TektronixMDOOscilloscope::~TektronixMDOOscilloscope()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string TektronixMDOOscilloscope::GetDriverNameInternal()
{
	return "tektronix.mdo";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel configuration

bool TektronixMDOOscilloscope::IsChannelEnabled(size_t i)
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

	//RF channel
	if(m_hasRF && (i == m_spectrumChannelBase))
	{
		string reply = m_transport->SendCommandQueuedWithReply("SELECT:RF_NORMAL?");
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelsEnabled[i] = (reply == "1");
		return m_channelsEnabled[i];
	}

	//Digital channels
	if(IsDigitalChannel(i))
	{
		int dchan = i - m_digitalChannelBase;
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

void TektronixMDOOscilloscope::EnableChannel(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelsEnabled[i] = true;

	if(i < m_analogChannelCount)
		m_transport->SendCommandQueued(string("SELECT:CH") + to_string(i+1) + " 1");
	else if(m_hasRF && (i == m_spectrumChannelBase))
		m_transport->SendCommandQueued("SELECT:RF_NORMAL 1");
	else if(IsDigitalChannel(i))
	{
		int dchan = i - m_digitalChannelBase;
		m_transport->SendCommandQueued(string("SELECT:D") + to_string(dchan) + " 1");
	}
}

void TektronixMDOOscilloscope::DisableChannel(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelsEnabled[i] = false;

	if(i < m_analogChannelCount)
		m_transport->SendCommandQueued(string("SELECT:CH") + to_string(i+1) + " 0");
	else if(m_hasRF && (i == m_spectrumChannelBase))
		m_transport->SendCommandQueued("SELECT:RF_NORMAL 0");
	else if(IsDigitalChannel(i))
	{
		int dchan = i - m_digitalChannelBase;
		m_transport->SendCommandQueued(string("SELECT:D") + to_string(dchan) + " 0");
	}
}

OscilloscopeChannel::CouplingType TektronixMDOOscilloscope::GetChannelCoupling(size_t i)
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

void TektronixMDOOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
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

double TektronixMDOOscilloscope::GetChannelAttenuation(size_t i)
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

void TektronixMDOOscilloscope::SetChannelAttenuation(size_t i, double atten)
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

unsigned int TektronixMDOOscilloscope::GetChannelBandwidthLimit(size_t i)
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

void TektronixMDOOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
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

vector<unsigned int> TektronixMDOOscilloscope::GetChannelBandwidthLimiters(size_t /*i*/)
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

float TektronixMDOOscilloscope::GetChannelOffset(size_t i, size_t /*stream*/)
{
	//Check cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelOffsets.find(i) != m_channelOffsets.end())
			return m_channelOffsets[i];
	}

	//RF spectrum channel. Note: RF:REFLEVEL is reported in dBm.
	if(m_hasRF && (i == m_spectrumChannelBase))
	{
		//Query reference level and scale from the instrument
		float refLevel_dbm = stof(m_transport->SendCommandQueuedWithReply("RF:REFLEVEL?"));
		float scale_db = stof(m_transport->SendCommandQueuedWithReply("RF:SCALE?"));
		//Offset is the center of the display: ref - scale*5
		float offset = refLevel_dbm - scale_db * 5;
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelOffsets[i] = offset;
		m_channelVoltageRanges[i] = scale_db * 10;
		return offset;
	}

	if(!IsAnalog(i))
		return 0;

	float offset = -stof(m_transport->SendCommandQueuedWithReply(
		GetOscilloscopeChannel(i)->GetHwname() + ":OFFS?"));

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
	return offset;
}

void TektronixMDOOscilloscope::SetChannelOffset(size_t i, size_t /*stream*/, float offset)
{
	//RF spectrum channel. Note: RF:REFLEVEL expects dBm.
	if(m_hasRF && (i == m_spectrumChannelBase))
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelOffsets[i] = offset;
		float scale_db = m_channelVoltageRanges[i] / 10;
		//offset = refLevel_dbm - scale*5 => refLevel_dbm = offset + scale*5
		float refLevel_dbm = offset + scale_db * 5;
		m_transport->SendCommandQueued(string("RF:REFLEVEL ") + to_string_sci(refLevel_dbm));
		return;
	}

	if(!IsAnalog(i))
		return;

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
	m_transport->SendCommandQueued(
		GetOscilloscopeChannel(i)->GetHwname() + ":OFFS " + to_string(-offset));
}

float TektronixMDOOscilloscope::GetChannelVoltageRange(size_t i, size_t /*stream*/)
{
	//Check cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelVoltageRanges.find(i) != m_channelVoltageRanges.end())
			return m_channelVoltageRanges[i];
	}

	//RF spectrum channel
	if(m_hasRF && (i == m_spectrumChannelBase))
	{
		float scale_db = stof(m_transport->SendCommandQueuedWithReply("RF:SCALE?"));
		float range = scale_db * 10;
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelVoltageRanges[i] = range;
		return range;
	}

	//Analog channels: delegate to base class
	return TektronixOscilloscope::GetChannelVoltageRange(i, 0);
}

void TektronixMDOOscilloscope::SetChannelVoltageRange(size_t i, size_t /*stream*/, float range)
{
	//RF spectrum channel
	if(m_hasRF && (i == m_spectrumChannelBase))
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelVoltageRanges[i] = range;
		float scale_db = range / 10;
		m_transport->SendCommandQueued(string("RF:SCALE ") + to_string_sci(scale_db));
		return;
	}

	//Analog channels: delegate to base class
	TektronixOscilloscope::SetChannelVoltageRange(i, 0, range);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering

vector<string> TektronixMDOOscilloscope::GetTriggerTypes()
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

Oscilloscope::TriggerMode TektronixMDOOscilloscope::PollTrigger()
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

bool TektronixMDOOscilloscope::PeekTriggerArmed()
{
	return m_triggerArmed;
}

void TektronixMDOOscilloscope::Start()
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());
	lock_guard<recursive_mutex> lock2(m_cacheMutex);

	m_transport->SendCommandQueued("ACQ:STOPA RUNST");
	m_transport->SendCommandQueued("ACQ:STATE ON");
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void TektronixMDOOscilloscope::StartSingleTrigger()
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());
	lock_guard<recursive_mutex> lock2(m_cacheMutex);

	m_transport->SendCommandQueued("ACQ:STOPA SEQ");
	m_transport->SendCommandQueued("ACQ:STATE ON");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void TektronixMDOOscilloscope::Stop()
{
	m_triggerArmed = false;
	m_transport->FlushCommandQueue();
	m_transport->SendCommandImmediate("ACQ:STATE STOP");
}

void TektronixMDOOscilloscope::ForceTrigger()
{
	m_triggerArmed = true;
	m_transport->SendCommandQueued("TRIG FORC");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Preamble parsing

bool TektronixMDOOscilloscope::ReadWFMOutprePreamble(
	const string& preamble_in, struct mdo4k_preamble& preamble_out)
{
	struct mdo4k_preamble& p = preamble_out;
	memset(&p, 0, sizeof(p));

	//MDO WFMOutpre format:
	// BYT_NR;BIT_NR;ENCDG;BN_FMT;BYT_OR;WFID;NR_PT;PT_FMT;PT_ORDER;XUNIT;XINCR;XZERO;PT_OFF;YUNIT;YMULT;YOFF;YZERO;DOMAIN;WFMTYPE;CENTERFREQ;SPAN

	//Dump raw preamble on first failure so we can diagnose the format
	static int dump_count = 0;

	size_t semicolons = std::count(preamble_in.begin(), preamble_in.end(), ';');
	if(semicolons < 18)
	{
		LogWarning("MDO: preamble too short (%zu semicolons)\n", semicolons);
		if(dump_count < 3)
		{
			dump_count++;
			LogWarning("MDO: raw preamble: %s\n", preamble_in.c_str());
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
		LogWarning("MDO: sscanf got %d fields, raw preamble: %s\n", read, preamble_in.c_str());
		LogWarning("MDO: stripped preamble: %s\n", buf.c_str());
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

	LogWarning("MDO: token parser also failed (%d/%zu fields)\n", field, semicolons);
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Data acquisition

bool TektronixMDOOscilloscope::AcquireData()
{
	map<int, vector<WaveformBase*> > pending_waveforms;

	lock_guard<recursive_mutex> lock(m_transport->GetMutex());
	LogIndenter li;

	//Acquire analog, RF, and digital data.
	//On any failure, clean up whatever was partially acquired.
	if(!AcquireAnalogData(pending_waveforms) ||
		!AcquireRFData(pending_waveforms) ||
		!AcquireDigitalData(pending_waveforms))
	{
		for(auto& it : pending_waveforms)
			for(auto w : it.second)
				delete w;
		return false;
	}

	//Now save all pending waveforms as a single time-correlated set
	m_pendingWaveformsMutex.lock();
	size_t num_pending = 1;	//TODO: segmented capture support
	for(size_t seg = 0; seg < num_pending; seg++)
	{
		SequenceSet s;
		for(size_t j = 0; j < m_channels.size(); j++)
		{
			if(IsChannelEnabled(j))
			{
				auto it = pending_waveforms.find(j);
				if((it != pending_waveforms.end()) && (seg < it->second.size()))
				{
					auto wf = pending_waveforms[j][seg];
					bool isDigital = GetOscilloscopeChannel(j)->GetType(0) == Stream::STREAM_TYPE_DIGITAL;
					LogDebug("Save: ch%zu (%s) wf=%p %s size=%zu\n",
						j, GetOscilloscopeChannel(j)->GetHwname().c_str(),
						(void*)wf, isDigital ? "DIGITAL" : "ANALOG",
						wf ? wf->size() : 0U);
					s[GetOscilloscopeChannel(j)] = wf;
				}
				else
					LogDebug("Save: ch%zu enabled but no pending waveform\n", j);
			}
		}
		m_pendingWaveforms.push_back(s);
		LogDebug("Save: SequenceSet has %zu entries\n", s.size());
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

bool TektronixMDOOscilloscope::AcquireAnalogData(
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
				LogWarning("MDO: Didn't get samples for %s\n", chan->GetHwname().c_str());
				continue;
			}

			if(nsamples != (size_t)preamble.nr_pt)
			{
				LogWarning("MDO: Wrong sample count for %s (got %zu, expected %d)\n",
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
			LogError("MDO: Failed to acquire %s\n", chan->GetHwname().c_str());
			return false;
		}
	}

	return true;
}

bool TektronixMDOOscilloscope::AcquireDigitalData(
	map<int, vector<WaveformBase*> >& pending_waveforms)
{
	if(m_digitalChannelCount == 0)
		return true;

	//Check if any digital channel is enabled
	bool anyEnabled = false;
	for(size_t i = 0; i < m_digitalChannelCount; i++)
	{
		if(IsChannelEnabled(m_digitalChannelBase + i))
		{
			anyEnabled = true;
			break;
		}
	}
	if(!anyEnabled)
	{
		LogDebug("MDO: no digital channels enabled, skipping\n");
		return true;
	}

	LogDebug("MDO: acquiring digital data (%zu channels)\n", m_digitalChannelCount);

	bool succeeded = false;
	for(int retry = 0; retry < 3; retry++)
	{
		m_transport->SendCommandImmediate("DAT:SOU DIG");
		m_transport->SendCommandImmediate("DAT:WID 4");
		m_transport->SendCommandImmediate("DAT:ENC RIB");

		string preamble_str = m_transport->SendCommandImmediateWithReply("WFMO?", false);
		mdo4k_preamble preamble;
		if(!ReadWFMOutprePreamble(preamble_str, preamble))
			continue;

		m_transport->SendCommandImmediate(
			string("DAT:STOP ") + to_string(preamble.nr_pt));

		size_t timebase = (size_t)(preamble.xincrement * (double)FS_PER_SECOND);

		size_t msglen;
		uint8_t* samples = (uint8_t*)m_transport->SendCommandImmediateWithRawBlockReply("CURV?", msglen);
		if(samples == NULL)
		{
			LogWarning("MDO: Didn't get digital samples\n");
			continue;
		}

		size_t nsamples = msglen / 4;

		//Process each digital channel
		for(size_t j = 0; j < min<size_t>(m_digitalChannelCount, 16); j++)
		{
			//Skip disabled channels; match the analog path and avoid
			//leaking captures that the caller won't save
			if(!IsChannelEnabled(m_digitalChannelBase + j))
			{
				pending_waveforms[m_digitalChannelBase + j].push_back(nullptr);
				continue;
			}

			auto cap = new SparseDigitalWaveform;
			cap->m_timescale = timebase;
			cap->m_triggerPhase = 0;
			cap->m_startTimestamp = time(NULL);
			double t = GetTime();
			cap->m_startFemtoseconds = (t - floor(t)) * FS_PER_SECOND;
			cap->Reserve(nsamples);
			cap->PrepareForCpuAccess();

			uint32_t mask = (1U << j);
			//Data is big-endian (DAT:ENC RIB)
			uint32_t word =
				(static_cast<uint32_t>(samples[0]) << 24) |
				(static_cast<uint32_t>(samples[1]) << 16) |
				(static_cast<uint32_t>(samples[2]) << 8) |
				static_cast<uint32_t>(samples[3]);
			bool last = (word & mask) ? true : false;

			cap->m_offsets.push_back(0);
			cap->m_durations.push_back(1);
			cap->m_samples.push_back(last);

			for(size_t m = 1; m < nsamples; m++)
			{
				word =
					(static_cast<uint32_t>(samples[m * 4]) << 24) |
					(static_cast<uint32_t>(samples[m * 4 + 1]) << 16) |
					(static_cast<uint32_t>(samples[m * 4 + 2]) << 8) |
					static_cast<uint32_t>(samples[m * 4 + 3]);
				bool sample = (word & mask) ? true : false;
				//Deduplicate only in the middle of the waveform;
				//always emit segments near edges to work around flat-signal rendering
				if( (last == sample) && ((m + 5) < nsamples) && (m > 5) )
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
			LogDebug("  D%zu: wf=%p segments=%zu dur0=%lld\n",
				j, (void*)cap, cap->size(),
				(long long)(cap->size() > 0 ? cap->m_durations[0] : 0LL));
			pending_waveforms[m_digitalChannelBase + j].push_back(cap);
		}

		delete[] samples;
		m_transport->ReadReply();

		succeeded = true;
		break;
	}

	if(!succeeded)
	{
		LogError("MDO: Failed to acquire digital data\n");
		return false;
	}

	return true;
}

bool TektronixMDOOscilloscope::AcquireRFData(
	map<int, vector<WaveformBase*> >& pending_waveforms)
{
	//MSO/DPO4000B have no RF input; without this guard the phantom RF index would
	//collide with digital channel 0 and corrupt its pending waveform slot
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
		LogWarning("MDO: bad RF preamble\n");
		return false;
	}

	size_t msglen;
	float* samples = (float*)m_transport->SendCommandImmediateWithRawBlockReply("CURV?", msglen);
	if(samples == NULL)
	{
		LogWarning("MDO: Didn't get RF samples\n");
		return false;
	}

	size_t nsamples = msglen / 4;

	auto cap = new UniformAnalogWaveform;
	cap->m_timescale = (int64_t)(preamble.xincrement);
	cap->m_triggerPhase = (int64_t)(preamble.xzero);
	cap->m_startTimestamp = time(nullptr);
	double t = GetTime();
	cap->m_startFemtoseconds = (t - floor(t)) * FS_PER_SECOND;
	cap->Resize(nsamples);
	cap->PrepareForCpuAccess();

	//The scope returns RF power in Watts, but the SpectrumChannel expects dBm.
	//Convert: dBm = 10 * log10(W * 1000)
	for(size_t j = 0; j < nsamples; j++)
	{
		float power_w = preamble.ymult * samples[j] + preamble.yzero;
		//Clamp near-zero values to avoid log10(-inf)
		if(power_w < 1e-15f)
			power_w = 1e-15f;
		cap->m_samples[j] = 10.0f * log10f(power_w * 1000.0f);
	}

	cap->MarkSamplesModifiedFromCpu();

	pending_waveforms[nchan].push_back(cap);
	delete[] samples;
	m_transport->ReadReply();

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Trigger configuration

float TektronixMDOOscilloscope::ReadTriggerLevel(OscilloscopeChannel* chan)
{
	return stof(m_transport->SendCommandQueuedWithReply(
		string("TRIG:A:LEV:") + chan->GetHwname() + "?"));
}

void TektronixMDOOscilloscope::SetTriggerLevel(Trigger* trig)
{
	auto chan = trig->GetInput(0).m_channel;
	if(chan)
	{
		float level = trig->GetLevel();
		m_transport->SendCommandQueued(
			string("TRIG:A:LEV:") + chan->GetHwname() + " " + to_string_sci(level));
	}
}

void TektronixMDOOscilloscope::PushTrigger()
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
		LogWarning("MDO: unknown trigger type\n");
		return;
	}

	SetTriggerLevel(trig);
}

void TektronixMDOOscilloscope::PullTrigger()
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
			LogWarning("MDO: unknown pulse trigger class %s\n", pclass.c_str());
			m_trigger = nullptr;
		}
	}
	else
	{
		LogWarning("MDO: unknown trigger type %s\n", ttype.c_str());
		m_trigger = nullptr;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Push/Pull trigger helpers

void TektronixMDOOscilloscope::PushEdgeTrigger(EdgeTrigger* trig)
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
			m_transport->SendCommandQueued("TRIG:A:EDGE:SLO EIT");
			break;
		default:
			break;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sample rate and depth

uint64_t TektronixMDOOscilloscope::GetSampleRate()
{
	if(m_sampleRateValid)
		return m_sampleRate;

	m_sampleRate = stod(m_transport->SendCommandQueuedWithReply("HOR:SAMPLER?"));
	m_sampleRateValid = true;
	return m_sampleRate;
}

uint64_t TektronixMDOOscilloscope::GetSampleDepth()
{
	if(m_sampleDepthValid)
		return m_sampleDepth;

	m_sampleDepth = stoull(m_transport->SendCommandQueuedWithReply("HOR:RECO?"));
	m_transport->SendCommandQueued("DAT:START 1");
	m_transport->SendCommandQueued(string("DAT:STOP ") + to_string(m_sampleDepth));
	m_sampleDepthValid = true;
	return m_sampleDepth;
}

void TektronixMDOOscilloscope::SetSampleDepth(uint64_t depth)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_transport->SendCommandQueued(string("HOR:RECO ") + to_string(depth));
	m_transport->SendCommandQueued("DAT:START 1");
	m_transport->SendCommandQueued(string("DAT:STOP ") + to_string(depth));

	//Don't trust the requested depth: the scope coerces unsupported record lengths
	//to the nearest supported value. Changing the record length at a fixed time/div
	//also changes the sample rate (rate = depth / (time_per_div * 10)). Invalidate
	//both caches so the next read reports what the scope actually adopted instead of
	//forcing the previous rate back onto it.
	m_sampleDepthValid = false;
	m_sampleRateValid = false;
}

void TektronixMDOOscilloscope::SetSampleRate(uint64_t rate)
{
	//HOR:SAMPLER is query-only on this family, so the sample rate is realized by
	//choosing the record length and time/div: rate = depth / (time_per_div * 10).
	//Find an exact (record length, time/div) pair from the supported record lengths
	//and the 1-2-5 time/div steps and set both. If the requested rate is not exactly
	//reachable (e.g. it is the current coerced rate and falls off the grid), choose
	//the pair whose achieved rate is closest to the request.
	//When several pairs tie, prefer the largest record length: all of them realize
	//the requested rate, and more memory means a longer capture (a 1 kS/s request
	//must not collapse to a 1k-point, 1-second window when the scope has 10M+).
	//Pairs whose rate exceeds the ADC capability are skipped, matching the set of
	//rates offered by GetSampleRatesNonInterleaved().
	//The caches are invalidated so the readback shows the values the scope actually
	//adopted.
	uint64_t best_depth = 0;
	double best_scale = 0;
	int64_t best_delta = INT64_MAX;
	const uint64_t max_rate = GetMaxAnalogSampleRate();

	for(auto depth : GetSupportedSampleDepths())
	{
		for(double scale : GetTimebaseScales())
		{
			uint64_t achieved = static_cast<uint64_t>(round(depth / (scale * 10.0)));
			if(achieved > max_rate)
				continue;	//the ADC cannot sample this fast

			int64_t delta = static_cast<int64_t>(achieved) - static_cast<int64_t>(rate);
			if(delta < 0)
				delta = -delta;

			//Keep the closest pair; on ties prefer a longer record (more memory)
			if((delta < best_delta) || ((delta == best_delta) && (depth > best_depth)))
			{
				best_delta = delta;
				best_depth = depth;
				best_scale = scale;
			}
		}
	}

	if(best_depth == 0)
	{
		//No supported record length and time/div can produce a rate near this value;
		//refuse rather than silently keep the current configuration
		LogWarning("MDO: no record length/timebase combination found for %zu S/s\n",
			static_cast<size_t>(rate));
		return;
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_transport->SendCommandQueued(string("HOR:RECO ") + to_string(best_depth));
	m_transport->SendCommandQueued("DAT:START 1");
	m_transport->SendCommandQueued(string("DAT:STOP ") + to_string(best_depth));
	m_transport->SendCommandQueued(string("HOR:SCALE ") + to_string_sci(best_scale));
	m_sampleRateValid = false;
	m_sampleDepthValid = false;
}

void TektronixMDOOscilloscope::SetTriggerOffset(int64_t offset)
{
	double capture_len_sec = 1.0 * GetSampleDepth() / GetSampleRate();
	double offset_sec = offset * SECONDS_PER_FS;
	double pct = (offset_sec / capture_len_sec) * 100.0;

	m_transport->SendCommandQueued(string("HOR:POS ") + to_string(pct));

	m_triggerOffsetValid = false;
}

void TektronixMDOOscilloscope::PushPulseWidthTrigger(PulseWidthTrigger* trig)
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
			m_transport->SendCommandQueued("TRIG:A:PUL:WHE MORE");
			break;
		case Trigger::CONDITION_LESS:
			m_transport->SendCommandQueued("TRIG:A:PUL:WHE LES");
			break;
		default:
			break;
	}

	m_transport->SendCommandQueued(string("TRIG:A:PUL:WID ") + to_string_sci(trig->GetUpperBound()));
}

//Normalize a Tektronix enumerated-type query reply into a single uppercase keyword
//for matching. Replies may use short or canonical long forms, may carry a leading
//command header (depending on HEADer/VERBose state), or may append a trailing
//value (e.g. the manual's "TRIGGER:A:PULSEWIDTH:WHEN GREATER THAN 2.0000E-9").
static string NormalizeTekEnumReply(const string& reply)
{
	string ret = Trim(reply);

	//Strip any leading command header (everything up to and including the last ':')
	size_t last_colon = ret.rfind(':');
	if(last_colon != string::npos)
		ret = ret.substr(last_colon + 1);

	//Uppercase for case-insensitive token matching
	for(auto& c : ret)
		c = toupper(c);

	//Strip any trailing space-delimited value
	size_t space = ret.find_first_of(" \t");
	if(space != string::npos)
		ret = ret.substr(0, space);

	return ret;
}

void TektronixMDOOscilloscope::PullPulseWidthTrigger()
{
	PulseWidthTrigger* et = RecreateTrigger<PulseWidthTrigger>();

	//Source channel
	string reply = Trim(m_transport->SendCommandQueuedWithReply("TRIG:A:PUL:SOU?"));
	auto chan = GetOscilloscopeChannelByHwName(reply);
	if(chan)
	{
		et->SetInput(0, StreamDescriptor(chan, 0));

		//Trigger level
		et->SetLevel(ReadTriggerLevel(chan));
	}

	//Edge slope
	reply = NormalizeTekEnumReply(m_transport->SendCommandQueuedWithReply("TRIG:A:PUL:POL?"));
	if(reply.find("POS") != string::npos)
		et->SetType(EdgeTrigger::EDGE_RISING);
	else
		et->SetType(EdgeTrigger::EDGE_FALLING);

	//Condition: accept short and canonical long spellings of the WHEn reply
	//(manual arguments are {LESSthan|MOREthan|EQual|UNEQual}; replies use the
	//same keywords or a human-readable form such as "GREATER THAN")
	reply = NormalizeTekEnumReply(m_transport->SendCommandQueuedWithReply("TRIG:A:PUL:WHE?"));
	if(reply.find("LESS") != string::npos)
		et->SetCondition(Trigger::CONDITION_LESS);
	else if(reply.find("UNEQ") != string::npos)
		et->SetCondition(Trigger::CONDITION_NOT_EQUAL);
	else if((reply.find("MOR") != string::npos) || (reply.find("GREAT") != string::npos))
		et->SetCondition(Trigger::CONDITION_GREATER);
	else if(reply.find("EQU") != string::npos)
		et->SetCondition(Trigger::CONDITION_EQUAL);

	//Pulse width (in seconds)
	Unit fs(Unit::UNIT_FS);
	et->SetUpperBound(fs.ParseString(m_transport->SendCommandQueuedWithReply("TRIG:A:PUL:WID?")));
}

void TektronixMDOOscilloscope::PushDropoutTrigger(DropoutTrigger* trig)
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

void TektronixMDOOscilloscope::PullDropoutTrigger()
{
	DropoutTrigger* et = RecreateTrigger<DropoutTrigger>();

	//Source channel
	string reply = Trim(m_transport->SendCommandQueuedWithReply("TRIG:A:TIME:SOU?"));
	auto chan = GetOscilloscopeChannelByHwName(reply);
	if(chan)
	{
		et->SetInput(0, StreamDescriptor(chan, 0));

		//Trigger level
		et->SetLevel(ReadTriggerLevel(chan));
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

void TektronixMDOOscilloscope::PushRuntTrigger(RuntTrigger* trig)
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

void TektronixMDOOscilloscope::PullRuntTrigger()
{
	RuntTrigger* et = RecreateTrigger<RuntTrigger>();

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

void TektronixMDOOscilloscope::PushSlewRateTrigger(SlewRateTrigger* trig)
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

void TektronixMDOOscilloscope::PullSlewRateTrigger()
{
	SlewRateTrigger* et = RecreateTrigger<SlewRateTrigger>();

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

void TektronixMDOOscilloscope::PushWindowTrigger(WindowTrigger* /*trig*/)
{
	LogWarning("MDO: window trigger not supported\n");
}

void TektronixMDOOscilloscope::PullWindowTrigger()
{
	LogWarning("MDO: window trigger not supported, cannot pull\n");
	m_trigger = NULL;
}

void TektronixMDOOscilloscope::PullEdgeTrigger()
{
	EdgeTrigger* trig = RecreateTrigger<EdgeTrigger>();
	trig->SetType(EdgeTrigger::EDGE_RISING);

	string src = Trim(m_transport->SendCommandQueuedWithReply("TRIG:A:EDGE:SOU?"));
	auto chan = GetOscilloscopeChannelByHwName(src);
	if(chan)
		trig->SetInput(0, StreamDescriptor(chan, 0));

	//Note: the MDO4000 family uses EITHer where the MSO5/6 family accepts ANY
	string slope = Trim(m_transport->SendCommandQueuedWithReply("TRIG:A:EDGE:SLO?"));
	if(slope == "RISE")
		trig->SetType(EdgeTrigger::EDGE_RISING);
	else if(slope == "FALL")
		trig->SetType(EdgeTrigger::EDGE_FALLING);
	else if((slope == "EIT") || (slope == "EITHER"))
		trig->SetType(EdgeTrigger::EDGE_ANY);
}

int64_t TektronixMDOOscilloscope::GetTriggerOffset()
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

bool TektronixMDOOscilloscope::HasInterleavingControls()
{
	return false;
}

uint64_t TektronixMDOOscilloscope::GetMaxAnalogSampleRate()
{
	if(m_maxSampleRateValid)
		return m_maxSampleRate;

	//The maximum sample rate is fixed per model family and is encoded in the model
	//name: <family>4<bandwidth x2><channels>, e.g. MDO4104, MSO4054B. 1 GHz models
	//(bandwidth code "10") sample at up to 5 GS/s; all others at 2.5 GS/s. No
	//CONFIG query or fallback is needed.
	m_maxSampleRate = ((m_model.size() >= 6) && (m_model.substr(4, 2) == "10")) ?
		5000000000ULL : 2500000000ULL;
	m_maxSampleRateValid = true;
	return m_maxSampleRate;
}

vector<double> TektronixMDOOscilloscope::GetTimebaseScales()
{
	//1-2-5 time/div steps inside the manual's HOR:SCALE argument range (400 ps to
	//1000 s). Sub-ns scales are skipped: any rate they could produce exceeds the ADC
	//cap, so GetSampleRatesNonInterleaved()/SetSampleRate() never need them.
	vector<double> ret;
	const double min_scale = 400e-12;
	const double max_scale = 1000.0;
	const double coeffs[] = {1.0, 2.0, 5.0};
	for(double decade = 1e-9; decade <= max_scale; decade *= 10)
	{
		for(double coeff : coeffs)
		{
			double scale = coeff * decade;
			if((scale >= min_scale) && (scale <= max_scale))
				ret.push_back(scale);
		}
	}
	return ret;
}

vector<uint64_t> TektronixMDOOscilloscope::GetSampleRatesNonInterleaved()
{
	//Verified against the instrument: the sample-rate ladder is 1-2-5 steps
	//(1, 2, 5 times a power of ten) from 100 S/s up to 100 MS/s, then
	//250 MS/s, 500 MS/s, 1.25 GS/s, 2.5 GS/s (model-dependent above the
	//1-2-5 range; the top of the ladder is the model maximum reported by
	//CONFIG:ANALO:MAXSAMPLER?). HOR:SAMPLER is query-only on this family, so
	//the ladder is generated here rather than enumerated from the scope, and
	//the current rate is included in case it falls off the ladder (e.g. after
	//a record-length change); requesting it falls back to the nearest step.

	set<uint64_t> rates;

	rates.insert(GetSampleRate());

	const uint64_t max_rate = GetMaxAnalogSampleRate();

	//1-2-5 steps, 100 S/s .. 50 MS/s
	for(uint64_t decade = 100; decade <= (int)100e6; decade *= 10)
	{
		for(uint64_t coeff : {1ULL, 2ULL, 5ULL})
			rates.insert(coeff * decade);
	}

	//100 MS/s, then the rates above the 1-2-5 range, all model-dependent
	for(uint64_t step : {100000000ULL, 250000000ULL, 500000000ULL,
	                     1250000000ULL, 2500000000ULL})
	{
		if(step <= max_rate)
			rates.insert(step);
	}

	//Top of the ladder is always the model's maximum sample rate
	rates.insert(max_rate);

	return vector<uint64_t>(rates.begin(), rates.end());
}

vector<uint64_t> TektronixMDOOscilloscope::GetSupportedSampleDepths()
{
	if(m_supportedSampleDepthsValid)
		return m_supportedSampleDepths;

	//CONFIG:ANALO:RECLENS? is documented in the programmer manual for this family and
	//returns the exact set of supported record lengths, which varies by model
	//(e.g. MSO/DPO410x-L top out at 5M points, the MDO4000B supports 20M).
	try
	{
		string reply = m_transport->SendCommandQueuedWithReply("CONFIG:ANALO:RECLENS?");

		size_t pos = 0;
		while(pos < reply.size())
		{
			size_t comma = reply.find(',', pos);
			string tok = reply.substr(pos, (comma == string::npos) ? string::npos : (comma - pos));

			//stod skips leading whitespace and accepts integer or scientific notation;
			//reject non-positive values before converting to unsigned (a negative
			//conversion would be undefined behavior and poison the picker)
			double d = round(stod(tok));
			if(d > 0)
				m_supportedSampleDepths.push_back(static_cast<uint64_t>(d));

			if(comma == string::npos)
				break;
			pos = comma + 1;
		}
	}
	catch(const exception& e)
	{
		LogWarning("MDO: CONFIG:ANALO:RECLENS? failed (%s), using default record lengths\n",
			e.what());
		m_supportedSampleDepths.clear();
	}

	//Fall back to the standard set for this model if the query failed or returned
	//nothing usable. The manual documents smaller sets for -L models (5M max) and
	//MDO3000 (10M max).
	if(m_supportedSampleDepths.empty())
	{
		const int64_t k = 1000;
		const int64_t m = k * k;
		if(m_model.find("-L") != string::npos)
			m_supportedSampleDepths = {1 * k, 10 * k, 100 * k, 1 * m, 5 * m};
		else if(m_model.find("MDO3") == 0)
			m_supportedSampleDepths = {1 * k, 10 * k, 100 * k, 1 * m, 5 * m, 10 * m};
		else
			m_supportedSampleDepths = {1 * k, 10 * k, 100 * k, 1 * m, 5 * m, 10 * m, 20 * m};
	}

	m_supportedSampleDepthsValid = true;
	return m_supportedSampleDepths;
}

vector<uint64_t> TektronixMDOOscilloscope::GetSampleDepthsNonInterleaved()
{
	return GetSupportedSampleDepths();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Digital (logic analyzer)

vector<Oscilloscope::DigitalBank> TektronixMDOOscilloscope::GetDigitalBanks()
{
	vector<DigitalBank> ret;
	if(m_digitalChannelCount > 0)
	{
		DigitalBank bank;
		for(size_t i = 0; i < m_digitalChannelCount; i++)
		{
			auto chan = GetOscilloscopeChannel(m_digitalChannelBase + i);
			if(chan)
				bank.push_back(chan);
		}
		ret.push_back(bank);
	}
	return ret;
}

Oscilloscope::DigitalBank TektronixMDOOscilloscope::GetDigitalBank(size_t channel)
{
	DigitalBank bank;
	if(IsDigitalChannel(channel))
	{
		auto chan = GetOscilloscopeChannel(channel);
		if(chan)
			bank.push_back(chan);
	}
	return bank;
}

bool TektronixMDOOscilloscope::IsDigitalHysteresisConfigurable()
{
	return false;
}

bool TektronixMDOOscilloscope::IsDigitalThresholdConfigurable()
{
	return (m_digitalChannelCount > 0);
}

float TektronixMDOOscilloscope::GetDigitalThreshold(size_t channel)
{
	if(IsDigitalChannel(channel))
	{
		int dchan = channel - m_digitalChannelBase;
		return stof(m_transport->SendCommandQueuedWithReply(
			string("D") + to_string(dchan) + ":THR?"));
	}
	return 1.4;	//Default TTL threshold
}

void TektronixMDOOscilloscope::SetDigitalThreshold(size_t channel, float level)
{
	if(IsDigitalChannel(channel))
	{
		int dchan = channel - m_digitalChannelBase;
		m_transport->SendCommandQueued(
			string("D") + to_string(dchan) + ":THR " + to_string(level));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Spectrum analyzer (RF input)

bool TektronixMDOOscilloscope::HasFrequencyControls()
{
	//Only the MDO series has the integrated RF spectrum analyzer
	return m_hasRF;
}

void TektronixMDOOscilloscope::SetSpan(int64_t span)
{
	if(!m_hasRF)
		return;

	m_spanValid = false;
	m_transport->SendCommandQueued(string("RF:SPAN ") + to_string(span));
	m_span = span;
	m_spanValid = true;
}

int64_t TektronixMDOOscilloscope::GetSpan()
{
	if(!m_hasRF)
		return 1;	//match the base class default

	if(m_spanValid)
		return m_span;

	m_span = stoll(m_transport->SendCommandQueuedWithReply("RF:SPAN?"));
	m_spanValid = true;
	return m_span;
}

void TektronixMDOOscilloscope::SetCenterFrequency(size_t channel, int64_t freq)
{
	if(!m_hasRF)
		return;

	m_transport->SendCommandQueued(string("RF:FREQ ") + to_string(freq));
	m_channelCenterFrequencies[channel] = freq;
	m_rbwValid = false;
}

int64_t TektronixMDOOscilloscope::GetCenterFrequency(size_t channel)
{
	if(!m_hasRF)
		return 0;	//match the base class default

	auto it = m_channelCenterFrequencies.find(channel);
	if(it != m_channelCenterFrequencies.end())
		return it->second;

	auto freq = stoll(m_transport->SendCommandQueuedWithReply("RF:FREQ?"));
	m_channelCenterFrequencies[channel] = freq;
	return freq;
}

void TektronixMDOOscilloscope::SetResolutionBandwidth(int64_t rbw)
{
	if(!m_hasRF)
		return;

	m_rbwValid = false;
	m_transport->SendCommandQueued("RF:RBW:MOD MAN");
	m_transport->SendCommandQueued(string("RF:RBW ") + to_string(rbw));
	m_rbw = rbw;
	m_rbwValid = true;
}

int64_t TektronixMDOOscilloscope::GetResolutionBandwidth()
{
	if(!m_hasRF)
		return 1;	//match the base class default

	if(m_rbwValid)
		return m_rbw;

	m_rbw = stoll(m_transport->SendCommandQueuedWithReply("RF:RBW?"));
	m_rbwValid = true;
	return m_rbw;
}
