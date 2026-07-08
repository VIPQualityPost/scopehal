/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
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
* THIS SOFTWARE IS PROVIDED BY THE AUTHORS \"AS IS\" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED   *
* TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL *
* THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES        *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR       *
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE       *
* POSSIBILITY OF SUCH DAMAGE.                                                                                          *
*                                                                                                                      *
***********************************************************************************************************************/

/**
	@file
	@brief Implementation of RigolDL3000Load
	@ingroup loaddrivers
 */

#include "scopehal.h"
#include "RigolDL3000Load.h"
#include "LoadChannel.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initialize the driver

	@param transport	SCPITransport pointing to the load
 */
RigolDL3000Load::RigolDL3000Load(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
{
	m_channels.push_back(new LoadChannel("Input", this, "#808080", 0));
}

RigolDL3000Load::~RigolDL3000Load()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// System info / configuration

string RigolDL3000Load::GetDriverNameInternal()
{
	return "rigol_dl3000";
}

unsigned int RigolDL3000Load::GetInstrumentTypes() const
{
	return INST_LOAD;
}

uint32_t RigolDL3000Load::GetInstrumentTypesForChannel(size_t i) const
{
	if(i == 0)
		return Instrument::INST_LOAD;
	else
		return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Operating mode

/*
	FUNCtion sets the static operation mode:
		SOUR:FUNC CURR -> CC mode
		SOUR:FUNC VOLT -> CV mode
		SOUR:FUNC RES  -> CR mode
		SOUR:FUNC POW  -> CP mode

	FUNCtion? returns CC, CV, CR, or CP.
 */

Load::LoadMode RigolDL3000Load::GetLoadMode(size_t /*channel*/)
{
	auto reply = Trim(m_transport->SendCommandQueuedWithReply("SOUR:FUNC?"));
	if(reply == "CC")
		return MODE_CONSTANT_CURRENT;
	else if(reply == "CV")
		return MODE_CONSTANT_VOLTAGE;
	else if(reply == "CR")
		return MODE_CONSTANT_RESISTANCE;
	else if(reply == "CP")
		return MODE_CONSTANT_POWER;

	LogWarning("[RigolDL3000Load::GetLoadMode] Unknown mode %s\n", reply.c_str());
	return MODE_CONSTANT_CURRENT;
}

void RigolDL3000Load::SetLoadMode(size_t /*channel*/, LoadMode mode)
{
	switch(mode)
	{
		case MODE_CONSTANT_CURRENT:
			m_transport->SendCommandQueued("SOUR:FUNC CURR");
			break;

		case MODE_CONSTANT_VOLTAGE:
			m_transport->SendCommandQueued("SOUR:FUNC VOLT");
			break;

		case MODE_CONSTANT_RESISTANCE:
			m_transport->SendCommandQueued("SOUR:FUNC RES");
			break;

		case MODE_CONSTANT_POWER:
			m_transport->SendCommandQueued("SOUR:FUNC POW");
			break;

		default:
			LogWarning("[RigolDL3000Load::SetLoadMode] Unknown mode %d\n", mode);
			break;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Range selection

/*
	Current ranges: low (5 A) and high (40 A for DL3021).
	Set via SOUR:CURR:RANG <value> — 5 for low, 40 for high.
	Query returns the range value in amps.
 */
vector<float> RigolDL3000Load::GetLoadCurrentRanges(size_t /*channel*/)
{
	vector<float> ranges;
	ranges.push_back(5);	// low range
	ranges.push_back(40);	// high range (DL3021)
	return ranges;
}

size_t RigolDL3000Load::GetLoadCurrentRange(size_t /*channel*/)
{
	auto reply = Trim(m_transport->SendCommandQueuedWithReply("SOUR:CURR:RANG?"));
	if(reply.empty())
		return 0;
	float val = stof(reply);
	// 5 A = low range (index 0), anything larger = high range (index 1)
	return (val > 5.1f) ? 1 : 0;
}

void RigolDL3000Load::SetLoadCurrentRange(size_t channel, size_t rangeIndex)
{
	auto ranges = GetLoadCurrentRanges(channel);
	float fullScale = ranges[rangeIndex];

	// Cannot change range while load is enabled
	bool wasOn = GetLoadActive(channel);
	if(wasOn)
		SetLoadActive(channel, false);

	m_transport->SendCommandQueued(string("SOUR:CURR:RANG ") + to_string(fullScale));

	if(wasOn)
		SetLoadActive(channel, true);
}

/*
	Voltage ranges: low (18 V) and high (150 V).
	Set via SOUR:VOLT:RANG <value> — 18 for low, 150 for high.
	Query returns the range value in volts.
 */
vector<float> RigolDL3000Load::GetLoadVoltageRanges(size_t /*channel*/)
{
	vector<float> ranges;
	ranges.push_back(18);	// low range
	ranges.push_back(150);	// high range
	return ranges;
}

size_t RigolDL3000Load::GetLoadVoltageRange(size_t /*channel*/)
{
	auto reply = Trim(m_transport->SendCommandQueuedWithReply("SOUR:VOLT:RANG?"));
	if(reply.empty())
		return 0;
	float val = stof(reply);
	// 18 V = low range (index 0), anything larger = high range (index 1)
	return (val > 18.1f) ? 1 : 0;
}

void RigolDL3000Load::SetLoadVoltageRange(size_t channel, size_t rangeIndex)
{
	auto ranges = GetLoadVoltageRanges(channel);
	float fullScale = ranges[rangeIndex];

	// Cannot change range while load is enabled
	bool wasOn = GetLoadActive(channel);
	if(wasOn)
		SetLoadActive(channel, false);

	m_transport->SendCommandQueued(string("SOUR:VOLT:RANG ") + to_string(fullScale));

	if(wasOn)
		SetLoadActive(channel, true);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Input (load on/off)

bool RigolDL3000Load::GetLoadActive(size_t /*channel*/)
{
	auto reply = Trim(m_transport->SendCommandQueuedWithReply("SOUR:INP:STAT?"));
	return (reply == "1");
}

void RigolDL3000Load::SetLoadActive(size_t /*channel*/, bool active)
{
	if(active)
		m_transport->SendCommandQueued("SOUR:INP:STAT 1");
	else
		m_transport->SendCommandQueued("SOUR:INP:STAT 0");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set point

float RigolDL3000Load::GetLoadSetPoint(size_t channel)
{
	auto mode = GetLoadMode(channel);
	switch(mode)
	{
		case MODE_CONSTANT_CURRENT:
			return stof(Trim(m_transport->SendCommandQueuedWithReply("SOUR:CURR:LEV:IMM?")));

		case MODE_CONSTANT_VOLTAGE:
			return stof(Trim(m_transport->SendCommandQueuedWithReply("SOUR:VOLT:LEV:IMM?")));

		case MODE_CONSTANT_RESISTANCE:
			return stof(Trim(m_transport->SendCommandQueuedWithReply("SOUR:RES:LEV:IMM?")));

		case MODE_CONSTANT_POWER:
			return stof(Trim(m_transport->SendCommandQueuedWithReply("SOUR:POW:LEV:IMM?")));

		default:
			LogWarning("[RigolDL3000Load::GetLoadSetPoint] Unknown mode %d\n", mode);
			return 0;
	}
}

void RigolDL3000Load::SetLoadSetPoint(size_t channel, float target)
{
	auto mode = GetLoadMode(channel);
	switch(mode)
	{
		case MODE_CONSTANT_CURRENT:
			m_transport->SendCommandQueued(string("SOUR:CURR:LEV:IMM ") + to_string(target));
			break;

		case MODE_CONSTANT_VOLTAGE:
			m_transport->SendCommandQueued(string("SOUR:VOLT:LEV:IMM ") + to_string(target));
			break;

		case MODE_CONSTANT_RESISTANCE:
			m_transport->SendCommandQueued(string("SOUR:RES:LEV:IMM ") + to_string(target));
			break;

		case MODE_CONSTANT_POWER:
			m_transport->SendCommandQueued(string("SOUR:POW:LEV:IMM ") + to_string(target));
			break;

		default:
			LogWarning("[RigolDL3000Load::SetLoadSetPoint] Unknown mode %d\n", mode);
			break;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Readback

float RigolDL3000Load::GetLoadVoltageActual(size_t /*channel*/)
{
	return stof(Trim(m_transport->SendCommandQueuedWithReply("MEAS:VOLT:DC?")));
}

float RigolDL3000Load::GetLoadCurrentActual(size_t /*channel*/)
{
	return stof(Trim(m_transport->SendCommandQueuedWithReply("MEAS:CURR:DC?")));
}
