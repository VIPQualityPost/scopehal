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

#ifndef Keysight34461AMultimeter_h
#define Keysight34461AMultimeter_h

#include "AgilentMultimeter.h"

/**
	@brief Driver for Keysight 34461A Truevolt series DMM

	Inherits from the Agilent 34401A driver since the 34461A uses backward-compatible SCPI commands
	for all base measurements, and adds capacitance, temperature, and secondary measurements.
 */
class Keysight34461AMultimeter
	: public virtual AgilentMultimeter
{
public:
	Keysight34461AMultimeter(SCPITransport* transport);
	virtual ~Keysight34461AMultimeter();

	virtual unsigned int GetMeasurementTypes() override;
	virtual unsigned int GetSecondaryMeasurementTypes() override;

	//Meter operating mode
	virtual MeasurementTypes GetMeterMode() override;
	virtual MeasurementTypes GetSecondaryMeterMode() override;
	virtual void SetMeterMode(MeasurementTypes type) override;
	virtual void SetSecondaryMeterMode(MeasurementTypes type) override;

	//Control
	virtual void SetMeterAutoRange(bool enable) override;
	virtual bool GetMeterAutoRange() override;

	//Get readings
	virtual double GetMeterValue() override;
	virtual double GetSecondaryMeterValue() override;

protected:
	bool m_secmodeValid;
	MeasurementTypes m_secmode;

public:
	static std::string GetDriverNameInternal();
	static std::vector<SCPIInstrumentModel> GetDriverSupportedModels()
	{
		return {
#ifdef _WIN32
			{"Keysight 34461A", {{ SCPITransportType::TRANSPORT_LAN, "<ip_address>:5025" }}},
			{"Keysight 34461A", {{ SCPITransportType::TRANSPORT_USBTMC, "USB::<vendor>::<product>::<serial>::INSTR" }}},
			{"Keysight 34461A", {{ SCPITransportType::TRANSPORT_UART, "COM<x>" }}}
#else
			{"Keysight 34461A", {{ SCPITransportType::TRANSPORT_LAN, "<ip_address>:5025" }}},
			{"Keysight 34461A", {{ SCPITransportType::TRANSPORT_USBTMC, "/dev/usbtmc<x>" }}},
			{"Keysight 34461A", {{ SCPITransportType::TRANSPORT_UART, "/dev/ttyUSB<x>" }}}
#endif
		};
	}
	METER_INITPROC(Keysight34461AMultimeter)
};

#endif
