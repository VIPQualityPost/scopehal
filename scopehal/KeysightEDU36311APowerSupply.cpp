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
	@brief Implementation of KeysightEDU36311APowerSupply
	@ingroup psudrivers
 */

#include "scopehal.h"
#include "KeysightEDU36311APowerSupply.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initialize the driver

	@param transport	SCPITransport pointing to the PSU
 */
KeysightEDU36311APowerSupply::KeysightEDU36311APowerSupply(SCPITransport* transport)
	: SCPIDevice(transport, true)
	, SCPIInstrument(transport, true)
{
	//EDU36311A always has 3 channels
	for(int i=0; i<3; i++)
	{
		m_channels.push_back(
			new PowerSupplyChannel(string("CH") + to_string(i+1), this, "#808080", i));
	}
}

KeysightEDU36311APowerSupply::~KeysightEDU36311APowerSupply()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device info

///@brief Return the constant driver name "keysight_edu36311a"
string KeysightEDU36311APowerSupply::GetDriverNameInternal()
{
	return "keysight_edu36311a";
}

uint32_t KeysightEDU36311APowerSupply::GetInstrumentTypesForChannel([[maybe_unused]] size_t i) const
{
	return INST_PSU;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device capabilities

bool KeysightEDU36311APowerSupply::SupportsSoftStart()
{
	return false;
}

bool KeysightEDU36311APowerSupply::SupportsIndividualOutputSwitching()
{
	return true;
}

bool KeysightEDU36311APowerSupply::SupportsMasterOutputSwitching()
{
	return true;
}

bool KeysightEDU36311APowerSupply::SupportsOvercurrentShutdown()
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helpers

string KeysightEDU36311APowerSupply::Chanlist(int chan)
{
	// EDU36311A uses 1-based channel indexing in (@<chanlist>) format
	// Our API uses 0-based channel index, so add 1
	return "(@" + to_string(chan + 1) + ")";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual hardware interfacing

bool KeysightEDU36311APowerSupply::AcquireData()
{
	//Batch all SCPI queries for this poll cycle to avoid overwhelming the PSU
	//The instrument thread calls this + IsPowerConstantCurrent/OCPTripped/ChannelActive
	//every cycle — so we cache everything here and let the query methods return cached values.
	for(int i=0; i<3; i++)
	{
		string chanlist = Chanlist(i);

		{
			auto reply = Trim(m_transport->SendCommandQueuedWithReply("MEAS:SCAL:VOLT:DC? " + chanlist));
			m_cachedVoltageActual[i] = reply.empty() ? 0.0 : stof(reply);
		}
		{
			auto reply = Trim(m_transport->SendCommandQueuedWithReply("MEAS:SCAL:CURR:DC? " + chanlist));
			m_cachedCurrentActual[i] = reply.empty() ? 0.0 : stof(reply);
		}
		{
			auto reply = Trim(m_transport->SendCommandQueuedWithReply("SOUR:VOLT? " + chanlist));
			m_cachedVoltageNominal[i] = reply.empty() ? 0.0 : stof(reply);
		}
		{
			auto reply = Trim(m_transport->SendCommandQueuedWithReply("SOUR:CURR? " + chanlist));
			m_cachedCurrentNominal[i] = reply.empty() ? 0.0 : stof(reply);
		}

		// IsPowerConstantCurrent: STAT:QUES:INST:ISUM<n>:COND? returns 0=off, 1=CC, 2=CV, 3=hardware failure
		// NOTE: ISUM<n> already specifies the channel — no (@chanlist) parameter is needed
		{
			string cmd = "STAT:QUES:INST:ISUM" + to_string(i + 1) + ":COND?";
			auto reply = Trim(m_transport->SendCommandQueuedWithReply(cmd));
			m_cachedConstantCurrent[i] = (!reply.empty() && stoi(reply) == 1);
		}

		{
			auto reply = Trim(m_transport->SendCommandQueuedWithReply("OUTP:STAT? " + chanlist));
			m_cachedChannelActive[i] = (reply == "1");
		}

		{
			auto reply = Trim(m_transport->SendCommandQueuedWithReply("SOUR:CURR:PROT:TRIP? " + chanlist));
			m_cachedOCPTripped[i] = (reply == "1");
		}

		{
			auto reply = Trim(m_transport->SendCommandQueuedWithReply("SOUR:VOLT:PROT:TRIP? " + chanlist));
			m_cachedOVTripped[i] = (reply == "1");
		}

		//Populate scalar streams (same as base class AcquireData)
		auto pchan = dynamic_cast<PowerSupplyChannel*>(m_channels[i]);
		if(pchan)
		{
			pchan->SetScalarValue(PowerSupplyChannel::STREAM_VOLTAGE_MEASURED, m_cachedVoltageActual[i]);
			pchan->SetScalarValue(PowerSupplyChannel::STREAM_VOLTAGE_SET_POINT, m_cachedVoltageNominal[i]);
			pchan->SetScalarValue(PowerSupplyChannel::STREAM_CURRENT_MEASURED, m_cachedCurrentActual[i]);
			pchan->SetScalarValue(PowerSupplyChannel::STREAM_CURRENT_SET_POINT, m_cachedCurrentNominal[i]);
		}
	}
	return true;
}

bool KeysightEDU36311APowerSupply::IsPowerConstantCurrent(int chan)
{
	return m_cachedConstantCurrent[chan];
}

double KeysightEDU36311APowerSupply::GetPowerVoltageActual(int chan)
{
	return m_cachedVoltageActual[chan];
}

double KeysightEDU36311APowerSupply::GetPowerVoltageNominal(int chan)
{
	return m_cachedVoltageNominal[chan];
}

double KeysightEDU36311APowerSupply::GetPowerCurrentActual(int chan)
{
	return m_cachedCurrentActual[chan];
}

double KeysightEDU36311APowerSupply::GetPowerCurrentNominal(int chan)
{
	return m_cachedCurrentNominal[chan];
}

bool KeysightEDU36311APowerSupply::GetPowerChannelActive(int chan)
{
	return m_cachedChannelActive[chan];
}

void KeysightEDU36311APowerSupply::SetPowerOvercurrentShutdownEnabled(int chan, bool enable)
{
	m_transport->SendCommandQueued("SOUR:CURR:PROT:STAT " + string(enable ? "ON" : "OFF") + ", " + Chanlist(chan));
}

bool KeysightEDU36311APowerSupply::GetPowerOvercurrentShutdownEnabled(int chan)
{
	return m_transport->SendCommandQueuedWithReply("SOUR:CURR:PROT:STAT? " + Chanlist(chan)) == "1";
}

bool KeysightEDU36311APowerSupply::GetPowerOvercurrentShutdownTripped(int chan)
{
	return m_cachedOCPTripped[chan];
}

bool KeysightEDU36311APowerSupply::GetPowerOvervoltageTripped(int chan)
{
	return m_cachedOVTripped[chan];
}

void KeysightEDU36311APowerSupply::SetPowerVoltage(int chan, double volts)
{
	m_transport->SendCommandQueued("SOUR:VOLT " + to_string(volts) + ", " + Chanlist(chan));
}

void KeysightEDU36311APowerSupply::SetPowerCurrent(int chan, double amps)
{
	m_transport->SendCommandQueued("SOUR:CURR " + to_string(amps) + ", " + Chanlist(chan));
}

void KeysightEDU36311APowerSupply::SetPowerChannelActive(int chan, bool on)
{
	m_transport->SendCommandQueued("OUTP:STAT " + string(on ? "ON" : "OFF") + ", " + Chanlist(chan));
}
