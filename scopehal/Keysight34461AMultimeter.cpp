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
* THE AUTHORS BE HELD LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES        *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR       *
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE       *
* POSSIBILITY OF SUCH DAMAGE.                                                                                          *
*                                                                                                                      *
***********************************************************************************************************************/

/**
	@file
	@author Matei Jordache
	@brief Keysight 34461A multimeter driver
 */

#include "scopehal.h"
#include "Keysight34461AMultimeter.h"
#include "MultimeterChannel.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

Keysight34461AMultimeter::Keysight34461AMultimeter(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, AgilentMultimeter(transport)
	, m_secmodeValid(false)
	, m_secmode(NONE)
{
	//The base constructor already:
	// - Creates the MultimeterChannel
	// - Sends SYST:REM and *CLS
	// - Calls GetMeterMode() using the base class vtable
	//
	//If the instrument was left in CAP or TEMP mode, the base GetMeterMode()
	//logged a warning and set m_mode = NONE. Re-read to get the real mode.
	m_modeValid = false;
	GetMeterMode();
}

Keysight34461AMultimeter::~Keysight34461AMultimeter()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device info

string Keysight34461AMultimeter::GetDriverNameInternal()
{
	return "keysight_34461a";
}

unsigned int Keysight34461AMultimeter::GetMeasurementTypes()
{
	return AC_RMS_AMPLITUDE | DC_VOLTAGE | DC_CURRENT | AC_CURRENT |
		RESISTANCE | CONTINUITY | DIODE | FREQUENCY |
		CAPACITANCE | TEMPERATURE;
}

unsigned int Keysight34461AMultimeter::GetSecondaryMeasurementTypes()
{
	switch(GetMeterMode())
	{
		case AC_RMS_AMPLITUDE:
		case AC_CURRENT:
			return FREQUENCY;

		default:
			return 0;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DMM mode

Multimeter::MeasurementTypes Keysight34461AMultimeter::GetMeterMode()
{
	if(m_modeValid)
		return m_mode;

	auto s_modeReply = TrimQuotes(Trim(m_transport->SendCommandQueuedWithReply("FUNC?")));

	//Default to no alternate mode
	m_secmode = NONE;

	if(s_modeReply == "VOLT:AC")
		m_mode = AC_RMS_AMPLITUDE;
	else if(s_modeReply == "VOLT")
		m_mode = DC_VOLTAGE;
	else if(s_modeReply == "CURR:AC")
		m_mode = AC_CURRENT;
	else if(s_modeReply == "CURR")
		m_mode = DC_CURRENT;
	else if(s_modeReply == "FREQ")
		m_mode = FREQUENCY;
	else if(s_modeReply == "CONT")
		m_mode = CONTINUITY;
	else if(s_modeReply == "DIOD")
		m_mode = DIODE;
	else if(s_modeReply == "RES")
		m_mode = RESISTANCE;
	else if(s_modeReply == "FRES")
		m_mode = RESISTANCE;		//4-wire resistance, same measurement type
	else if(s_modeReply == "PER")
		m_mode = FREQUENCY;			//Period, map to frequency for now
	else if(s_modeReply == "CAP")
		m_mode = CAPACITANCE;
	else if(s_modeReply == "TEMP")
		m_mode = TEMPERATURE;
	//TODO: RAT for DCV ratio

	//unknown, pick something
	else
	{
		LogWarning("Unknown mode = '%s'\n", s_modeReply.c_str());
		m_mode = NONE;
	}

	m_modeValid = true;
	m_secmodeValid = true;
	return m_mode;
}

Multimeter::MeasurementTypes Keysight34461AMultimeter::GetSecondaryMeterMode()
{
	if(m_secmodeValid)
		return m_secmode;

	//Read the primary mode (which also updates m_secmode if applicable)
	GetMeterMode();
	return m_secmode;
}

void Keysight34461AMultimeter::SetMeterMode(Multimeter::MeasurementTypes type)
{
	m_secmode = NONE;
	m_secmodeValid = true;

	switch(type)
	{
		case DC_VOLTAGE:
			m_transport->SendCommandQueuedWithReply("CONF:VOLT:DC;*OPC?");
			break;

		case AC_RMS_AMPLITUDE:
			m_transport->SendCommandQueuedWithReply("CONF:VOLT:AC;*OPC?");
			break;

		case DC_CURRENT:
			m_transport->SendCommandQueuedWithReply("CONF:CURR:DC;*OPC?");
			break;

		case AC_CURRENT:
			m_transport->SendCommandQueuedWithReply("CONF:CURR:AC;*OPC?");
			break;

		case RESISTANCE:
			m_transport->SendCommandQueuedWithReply("CONF:RES;*OPC?");
			break;

		case FREQUENCY:
			m_transport->SendCommandQueuedWithReply("CONF:FREQ;*OPC?");
			break;

		case DIODE:
			m_transport->SendCommandQueuedWithReply("CONF:DIOD;*OPC?");
			break;

		case CONTINUITY:
			m_transport->SendCommandQueuedWithReply("CONF:CONT;*OPC?");
			break;

		case CAPACITANCE:
			m_transport->SendCommandQueuedWithReply("CONF:CAP;*OPC?");
			break;

		case TEMPERATURE:
			m_transport->SendCommandQueuedWithReply("CONF:TEMP;*OPC?");
			break;

		//whatever it is, not supported
		default:
			LogWarning("Unexpected multimeter mode = '%d'\n", type);
			return;
	}

	m_mode = type;
}

void Keysight34461AMultimeter::SetSecondaryMeterMode(Multimeter::MeasurementTypes type)
{
	auto mode = GetMeterMode();

	switch(type)
	{
		case FREQUENCY:
		{
			switch(mode)
			{
				case AC_RMS_AMPLITUDE:
					m_transport->SendCommandQueued("CONF:VOLT:AC");
					m_transport->SendCommandQueued("VOLT:AC:SEC \"FREQ\"");
					break;

				case AC_CURRENT:
					m_transport->SendCommandQueued("CONF:CURR:AC");
					m_transport->SendCommandQueued("CURR:AC:SEC \"FREQ\"");
					break;

				//not supported in this mode
				default:
					return;
			}
			break;
		}

		case NONE:
			//Reset to primary-only by reconfiguring the function
			SetMeterMode(mode);
			return;

		//not supported
		default:
			return;
	}

	m_secmode = type;
	m_secmodeValid = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Control

bool Keysight34461AMultimeter::GetMeterAutoRange()
{
	string reply;
	auto mode = GetMeterMode();

	switch(mode)
	{
		case AC_RMS_AMPLITUDE:
			reply = m_transport->SendCommandQueuedWithReply("SENS:VOLT:AC:RANG:AUTO?");
			break;

		case DC_VOLTAGE:
			reply = m_transport->SendCommandQueuedWithReply("SENS:VOLT:DC:RANG:AUTO?");
			break;

		case DC_CURRENT:
			reply = m_transport->SendCommandQueuedWithReply("SENS:CURR:DC:RANG:AUTO?");
			break;

		case AC_CURRENT:
			reply = m_transport->SendCommandQueuedWithReply("SENS:CURR:AC:RANG:AUTO?");
			break;

		case RESISTANCE:
			reply = m_transport->SendCommandQueuedWithReply("SENS:RES:RANG:AUTO?");
			break;

		case FREQUENCY:
			reply = m_transport->SendCommandQueuedWithReply("SENS:FREQ:VOLT:RANG:AUTO?");
			break;

		case CAPACITANCE:
			reply = m_transport->SendCommandQueuedWithReply("SENS:CAP:RANG:AUTO?");
			break;

		case TEMPERATURE:
		case CONTINUITY:
		case DIODE:
			//No autoranging in these modes
			return false;

		default:
			LogError("Unknown meter mode in GetMeterAutoRange\n");
			return false;
	}

	return (Trim(reply) == "1");
}

void Keysight34461AMultimeter::SetMeterAutoRange(bool enable)
{
	string cmd = enable ? "ON" : "OFF";
	auto mode = GetMeterMode();

	switch(mode)
	{
		case AC_RMS_AMPLITUDE:
			m_transport->SendCommandQueuedWithReply("SENS:VOLT:AC:RANG:AUTO " + cmd + ";*OPC?");
			break;

		case DC_VOLTAGE:
			m_transport->SendCommandQueuedWithReply("SENS:VOLT:DC:RANG:AUTO " + cmd + ";*OPC?");
			break;

		case DC_CURRENT:
			m_transport->SendCommandQueuedWithReply("SENS:CURR:DC:RANG:AUTO " + cmd + ";*OPC?");
			break;

		case AC_CURRENT:
			m_transport->SendCommandQueuedWithReply("SENS:CURR:AC:RANG:AUTO " + cmd + ";*OPC?");
			break;

		case RESISTANCE:
			m_transport->SendCommandQueuedWithReply("SENS:RES:RANG:AUTO " + cmd + ";*OPC?");
			break;

		case FREQUENCY:
			m_transport->SendCommandQueuedWithReply("SENS:FREQ:VOLT:RANG:AUTO " + cmd + ";*OPC?");
			break;

		case CAPACITANCE:
			m_transport->SendCommandQueuedWithReply("SENS:CAP:RANG:AUTO " + cmd + ";*OPC?");
			break;

		case TEMPERATURE:
		case CONTINUITY:
		case DIODE:
			if(enable)
				LogWarning("Auto-range not supported in current mode\n");
			break;

		default:
			LogError("Unknown meter mode in SetMeterAutoRange\n");
			break;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Meter readings

double Keysight34461AMultimeter::GetMeterValue()
{
	string value;
	while(true)
	{
		value = Trim(m_transport->SendCommandQueuedWithReply("READ?"));
		if(value.empty())
		{
			LogWarning("Failed to read value: got '%s'\n", value.c_str());
			continue;
		}
		else if(value == "+9.90000000E+37") //Overload
			return std::numeric_limits<double>::max();

		istringstream os(value);
		double result;
		os >> result;
		return result;
	}
}

double Keysight34461AMultimeter::GetSecondaryMeterValue()
{
	if(GetSecondaryMeterMode() == NONE)
		return 0.0;

	auto reply = Trim(m_transport->SendCommandQueuedWithReply("DATA2?"));
	if(reply.empty())
		return 0.0;

	//DATA2? returns one or more comma-separated values.
	//For FREQ secondary it's a single value.
	//For PTP secondary it's min,max,ptp (3 values).
	//Take the first value.
	auto comma = reply.find(',');
	if(comma != string::npos)
		reply = reply.substr(0, comma);

	return stod(reply);
}
