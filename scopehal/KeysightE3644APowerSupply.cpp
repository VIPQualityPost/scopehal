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

/**
	@file
	@brief Implementation of KeysightE3644APowerSupply
	@ingroup psudrivers
 */

#include "scopehal.h"
#include "KeysightE3644APowerSupply.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initialize the driver

	@param transport	SCPITransport pointing to the PSU
 */
KeysightE3644APowerSupply::KeysightE3644APowerSupply(SCPITransport* transport)
	: SCPIDevice(transport, true)
	, SCPIInstrument(transport, true)
{
	// E364xA always has 1 channel
	m_channels.push_back(
		new PowerSupplyChannel("CH1", this, "#808080", 0));
}

KeysightE3644APowerSupply::~KeysightE3644APowerSupply()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device info

///@brief Return the constant driver name "keysight_e3644a"
string KeysightE3644APowerSupply::GetDriverNameInternal()
{
	return "keysight_e3644a";
}

uint32_t KeysightE3644APowerSupply::GetInstrumentTypesForChannel([[maybe_unused]] size_t i) const
{
	return INST_PSU;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device capabilities

bool KeysightE3644APowerSupply::SupportsSoftStart()
{
	return false;
}

bool KeysightE3644APowerSupply::SupportsIndividualOutputSwitching()
{
	return true;
}

bool KeysightE3644APowerSupply::SupportsMasterOutputSwitching()
{
	return false;
}

bool KeysightE3644APowerSupply::SupportsOvercurrentShutdown()
{
	// E364xA supports CV/CC operation but not OCP shutdown mode;
	// OVP is available but OCP (foldback) is not implemented.
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual hardware interfacing

bool KeysightE3644APowerSupply::AcquireData()
{
	// Batch all SCPI queries for this poll cycle to avoid overwhelming the PSU
	{
		auto reply = Trim(m_transport->SendCommandQueuedWithReply("MEAS:VOLT:DC?"));
		m_cachedVoltageActual = reply.empty() ? 0.0 : stof(reply);
	}
	{
		auto reply = Trim(m_transport->SendCommandQueuedWithReply("MEAS:CURR:DC?"));
		m_cachedCurrentActual = reply.empty() ? 0.0 : stof(reply);
	}
	{
		auto reply = Trim(m_transport->SendCommandQueuedWithReply("SOUR:VOLT?"));
		m_cachedVoltageNominal = reply.empty() ? 0.0 : stof(reply);
	}
	{
		auto reply = Trim(m_transport->SendCommandQueuedWithReply("SOUR:CURR?"));
		m_cachedCurrentNominal = reply.empty() ? 0.0 : stof(reply);
	}

	// STAT:QUES:COND? returns: 0=off/unreg, 1=CC, 2=CV, 3=failure
	{
		auto reply = Trim(m_transport->SendCommandQueuedWithReply("STAT:QUES:COND?"));
		m_cachedConstantCurrent = (!reply.empty() && stoi(reply) == 1);
	}

	{
		auto reply = Trim(m_transport->SendCommandQueuedWithReply("OUTP:STAT?"));
		m_cachedChannelActive = (reply == "1");
	}

	// Overvoltage protection trip status
	{
		auto reply = Trim(m_transport->SendCommandQueuedWithReply("VOLT:PROT:TRIP?"));
		m_cachedOVTripped = (reply == "1");
	}

	// No CURR:PROT:TRIP? on E364xA — overcurrent protection is not supported
	m_cachedOCPTripped = false;

	// Populate scalar streams
	auto pchan = dynamic_cast<PowerSupplyChannel*>(m_channels[0]);
	if(pchan)
	{
		pchan->SetScalarValue(PowerSupplyChannel::STREAM_VOLTAGE_MEASURED, m_cachedVoltageActual);
		pchan->SetScalarValue(PowerSupplyChannel::STREAM_VOLTAGE_SET_POINT, m_cachedVoltageNominal);
		pchan->SetScalarValue(PowerSupplyChannel::STREAM_CURRENT_MEASURED, m_cachedCurrentActual);
		pchan->SetScalarValue(PowerSupplyChannel::STREAM_CURRENT_SET_POINT, m_cachedCurrentNominal);
	}
	return true;
}

bool KeysightE3644APowerSupply::IsPowerConstantCurrent([[maybe_unused]] int chan)
{
	return m_cachedConstantCurrent;
}

double KeysightE3644APowerSupply::GetPowerVoltageActual([[maybe_unused]] int chan)
{
	return m_cachedVoltageActual;
}

double KeysightE3644APowerSupply::GetPowerVoltageNominal([[maybe_unused]] int chan)
{
	return m_cachedVoltageNominal;
}

double KeysightE3644APowerSupply::GetPowerCurrentActual([[maybe_unused]] int chan)
{
	return m_cachedCurrentActual;
}

double KeysightE3644APowerSupply::GetPowerCurrentNominal([[maybe_unused]] int chan)
{
	return m_cachedCurrentNominal;
}

bool KeysightE3644APowerSupply::GetPowerChannelActive([[maybe_unused]] int chan)
{
	return m_cachedChannelActive;
}

void KeysightE3644APowerSupply::SetPowerOvercurrentShutdownEnabled([[maybe_unused]] int chan, [[maybe_unused]] bool enable)
{
	// Not supported on E364xA — no-op
}

bool KeysightE3644APowerSupply::GetPowerOvercurrentShutdownEnabled([[maybe_unused]] int chan)
{
	return false;
}

bool KeysightE3644APowerSupply::GetPowerOvercurrentShutdownTripped([[maybe_unused]] int chan)
{
	return m_cachedOCPTripped;
}

void KeysightE3644APowerSupply::SetPowerVoltage([[maybe_unused]] int chan, double volts)
{
	m_transport->SendCommandQueued("SOUR:VOLT " + to_string(volts));
}

void KeysightE3644APowerSupply::SetPowerCurrent([[maybe_unused]] int chan, double amps)
{
	m_transport->SendCommandQueued("SOUR:CURR " + to_string(amps));
}

void KeysightE3644APowerSupply::SetPowerChannelActive([[maybe_unused]] int chan, bool on)
{
	m_transport->SendCommandQueued("OUTP:STAT " + string(on ? "ON" : "OFF"));
}
