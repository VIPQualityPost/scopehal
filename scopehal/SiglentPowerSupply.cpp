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
* THIS SOFTWARE IS PROVIDED BY THE AUTHORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED   *
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
	@brief Implementation of SiglentPowerSupply
	@ingroup psudrivers
 */

#include "scopehal.h"
#include "SiglentPowerSupply.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SiglentPowerSupply::SiglentPowerSupply(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, m_ch3on(false)
{
	//Detect model from *IDN? (parsed by SCPIDevice constructor)
	if(m_model.find("SPD1168X") != string::npos ||
	   m_model.find("SPD1305X") != string::npos)
	{
		m_numChannels = 1;

		//SPD1000X series supports OCP via SCPI (confirmed working on fw 2.1.1.9R1)
		//OVP? times out even on newer firmware; leave it disabled
		m_hasOCP = true;
	}
	else
	{
		//SPD3303X, SPD3303X-E, etc.
		m_numChannels = 3;

		//SPD3303X-E does NOT support OVP/OCP via SCPI (front-panel only)
		m_hasOCP = false;
	}

	for(int i=0; i<m_numChannels; i++)
	{
		string color = "#808080";
		if(i == 0)
			color = "#008000";
		else if(i == 1)
			color = "#ffff00";
		m_channels.push_back(
			new PowerSupplyChannel(string("CH") + to_string(i+1), this, color, i));
	}
}

SiglentPowerSupply::~SiglentPowerSupply()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device info

string SiglentPowerSupply::GetDriverNameInternal()
{
	return "siglent_spd";
}

uint32_t SiglentPowerSupply::GetInstrumentTypesForChannel(size_t /*i*/) const
{
	return INST_PSU;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device capabilities

bool SiglentPowerSupply::SupportsIndividualOutputSwitching()
{
	return true;
}

bool SiglentPowerSupply::SupportsVoltageCurrentControl(int chan)
{
	//CH3 on SPD3303X is fixed (2.5/3.3/5V via DIP switch, 3.2A limit)
	if(m_numChannels == 3)
		return (chan == 0) || (chan == 1);
	return (chan == 0);
}

bool SiglentPowerSupply::SupportsOvercurrentShutdown()
{
	return m_hasOCP;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Status register

/*
	Status register bits (SPD3303X):
		Bit 0: CH1 CC mode
		Bit 1: CH2 CC mode
		Bit 2-3: 01=independent, 10=parallel
		Bit 4: CH1 ON
		Bit 5: CH2 ON
		Bit 6: TIMER1 ON
		Bit 7: TIMER2 ON
		Bit 8: CH1 waveform display
		Bit 9: CH2 waveform display

	Status register bits (SPD1000X):
		Bit 0: CC mode
		Bit 4: Output ON
		Bit 5: 2W/4W mode
		Bit 6: TIMER ON
		Bit 8: waveform display
 */
unsigned int SiglentPowerSupply::GetStatusRegister()
{
	auto str = m_transport->SendCommandQueuedWithReply("SYST:STAT?");
	unsigned int ret;
	sscanf(str.c_str(), "0x%x", &ret);
	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Read sensors — each method queries SCPI directly, no cache

double SiglentPowerSupply::GetPowerVoltageActual(int chan)
{
	if(chan >= m_numChannels)
		return 0;

	//CH3 on SPD3303X is fixed voltage (DIP switch) — no SCPI query available
	if(m_numChannels == 3 && chan == 2)
		return 0;

	auto r = Trim(m_transport->SendCommandQueuedWithReply(
		string("MEAS:VOLT? ") + m_channels[chan]->GetHwname()));
	return r.empty() ? 0.0 : stod(r);
}

double SiglentPowerSupply::GetPowerVoltageNominal(int chan)
{
	if(chan >= m_numChannels)
		return 0;

	//CH3 on SPD3303X is fixed voltage (DIP switch) — no SCPI query available
	if(m_numChannels == 3 && chan == 2)
		return 0;

	//CH<n>:VOLT? returns the voltage set-point
	auto r = Trim(m_transport->SendCommandQueuedWithReply(
		m_channels[chan]->GetHwname() + ":VOLT?"));
	return r.empty() ? 0.0 : stod(r);
}

double SiglentPowerSupply::GetPowerCurrentActual(int chan)
{
	if(chan >= m_numChannels)
		return 0;

	//CH3 on SPD3303X has a fixed 3.2A hardware limit — no SCPI query
	if(m_numChannels == 3 && chan == 2)
		return 3.2;

	auto r = Trim(m_transport->SendCommandQueuedWithReply(
		string("MEAS:CURR? ") + m_channels[chan]->GetHwname()));
	return r.empty() ? 0.0 : stod(r);
}

double SiglentPowerSupply::GetPowerCurrentNominal(int chan)
{
	if(chan >= m_numChannels)
		return 0;

	//CH3 on SPD3303X has a fixed 3.2A hardware limit — no SCPI query
	if(m_numChannels == 3 && chan == 2)
		return 3.2;

	auto r = Trim(m_transport->SendCommandQueuedWithReply(
		m_channels[chan]->GetHwname() + ":CURR?"));
	return r.empty() ? 0.0 : stod(r);
}

bool SiglentPowerSupply::GetPowerChannelActive(int chan)
{
	if(chan >= m_numChannels)
		return false;

	//CH3 on SPD3303X has no SCPI query for output state
	if(m_numChannels == 3 && chan == 2)
		return m_ch3on;

	unsigned int sr = GetStatusRegister();
	if(m_numChannels == 1)
	{
		//SPD1000X: bit 4 = output ON
		return (sr & 0x10) ? true : false;
	}
	else
	{
		//SPD3303X: bit 4 = CH1 ON, bit 5 = CH2 ON
		return (sr & (0x10 << chan)) ? true : false;
	}
}

bool SiglentPowerSupply::IsPowerConstantCurrent(int chan)
{
	if(chan >= m_numChannels)
		return false;

	//CH3 on SPD3303X is always CV
	if(m_numChannels == 3 && chan == 2)
		return false;

	unsigned int sr = GetStatusRegister();
	if(m_numChannels == 1)
	{
		//SPD1000X: bit 0 = CC mode
		return (sr & 1) ? true : false;
	}
	else
	{
		//SPD3303X: bit 0 = CH1 CC, bit 1 = CH2 CC
		return (sr & (1 << chan)) ? true : false;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Configuration

void SiglentPowerSupply::SetPowerVoltage(int chan, double volts)
{
	if(chan >= m_numChannels)
		return;
	m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":VOLT " + to_string(volts));
}

void SiglentPowerSupply::SetPowerCurrent(int chan, double amps)
{
	if(chan >= m_numChannels)
		return;
	m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":CURR " + to_string(amps));
}

void SiglentPowerSupply::SetPowerChannelActive(int chan, bool on)
{
	if(chan >= m_numChannels)
		return;

	//CH3 on SPD3303X — track state locally, no SCPI for query but we can set it
	if(m_numChannels == 3 && chan == 2)
	{
		m_transport->SendCommandQueued(string("OUTP ") + m_channels[chan]->GetHwname() + (on ? ",ON" : ",OFF"));
		m_ch3on = on;
		return;
	}

	m_transport->SendCommandQueued(string("OUTP ") + m_channels[chan]->GetHwname() + (on ? ",ON" : ",OFF"));
}

bool SiglentPowerSupply::GetPowerOvercurrentShutdownEnabled(int chan)
{
	if(!m_hasOCP || chan >= m_numChannels)
		return false;

	//OCP? returns the threshold in amps (0 = disabled)
	auto r = Trim(m_transport->SendCommandQueuedWithReply("OCP?"));
	return r.empty() ? false : (stod(r) > 0);
}

void SiglentPowerSupply::SetPowerOvercurrentShutdownEnabled(int chan, bool enable)
{
	if(!m_hasOCP || chan >= m_numChannels)
		return;

	if(enable)
		m_transport->SendCommandQueued(string("OCP ") + to_string(GetPowerCurrentNominal(chan)));
	else
		m_transport->SendCommandQueued(string("OCP 0"));
}

bool SiglentPowerSupply::GetPowerOvercurrentShutdownTripped(int chan)
{
	if(!m_hasOCP || chan >= m_numChannels)
		return false;

	//On fw 2.1.1.9R1, OCP? returns the threshold (not empty) even after a trip.
	//OUTP:RES:PROT times out on this firmware so we can't clear remotely.
	//No reliable SCPI indication of tripped state — return false and let the
	//user check the front panel.
	return false;
}
