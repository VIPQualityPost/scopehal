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
	@brief Declaration of KeysightE3644APowerSupply
	@ingroup psudrivers
 */

#ifndef KeysightE3644APowerSupply_h
#define KeysightE3644APowerSupply_h

/**
	@brief A Keysight E364xA single-output DC power supply

	Supports models E3640A, E3641A, E3642A, E3643A, E3644A, and E3645A.

	Single channel with two voltage/current ranges:
	  - E3644A:  0-8V/8A (P8V)  and  0-20V/4A (P20V)
	  - E3640A:  0-8V/3A (P8V)  and  0-20V/1.5A (P20V)
	  - E3642A:  0-8V/5A (P8V)  and  0-20V/2.5A (P20V)

	This is a single-output instrument — no channel list (@<chan>) suffix
	is used in SCPI commands.
 */
class KeysightE3644APowerSupply
	: public virtual SCPIPowerSupply
	, public virtual SCPIDevice
{
public:
	KeysightE3644APowerSupply(SCPITransport* transport);
	virtual ~KeysightE3644APowerSupply();

	// Device information
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;

	// Device capabilities
	bool SupportsSoftStart() override;
	bool SupportsIndividualOutputSwitching() override;
	bool SupportsMasterOutputSwitching() override;
	bool SupportsOvercurrentShutdown() override;

	// Read sensors
	double GetPowerVoltageActual(int chan) override;	// actual voltage after current limiting
	double GetPowerVoltageNominal(int chan) override;	// set point
	double GetPowerCurrentActual(int chan) override;	// actual current drawn by the load
	double GetPowerCurrentNominal(int chan) override;	// current limit
	bool GetPowerChannelActive(int chan) override;

	// Configuration
	bool GetPowerOvercurrentShutdownEnabled(int chan) override;
	void SetPowerOvercurrentShutdownEnabled(int chan, bool enable) override;
	bool GetPowerOvercurrentShutdownTripped(int chan) override;
	void SetPowerVoltage(int chan, double volts) override;
	void SetPowerCurrent(int chan, double amps) override;
	void SetPowerChannelActive(int chan, bool on) override;
	bool IsPowerConstantCurrent(int chan) override;

	// Data acquisition
	bool AcquireData() override;

protected:
	// Cached per-channel state (updated by AcquireData)
	double m_cachedVoltageActual = 0.0;
	double m_cachedVoltageNominal = 0.0;
	double m_cachedCurrentActual = 0.0;
	double m_cachedCurrentNominal = 0.0;
	bool m_cachedConstantCurrent = false;
	bool m_cachedOCPTripped = false;
	bool m_cachedChannelActive = false;
	bool m_cachedOVTripped = false;

public:
	static std::string GetDriverNameInternal();
	POWER_INITPROC(KeysightE3644APowerSupply)

	// This is intentionally not virtual since it's a static method used by enumeration
	// cppcheck-suppress duplInheritedMember
	static std::vector<SCPIInstrumentModel> GetDriverSupportedModels()
	{
		return {
			{"Keysight E3640A", {
				{ SCPITransportType::TRANSPORT_LAN, "192.168.x.x" },
				{ SCPITransportType::TRANSPORT_UART, "/dev/usb.tty<x>" },
			}},
			{"Keysight E3641A", {
				{ SCPITransportType::TRANSPORT_LAN, "192.168.x.x" },
				{ SCPITransportType::TRANSPORT_UART, "/dev/usb.tty<x>" },
			}},
			{"Keysight E3642A", {
				{ SCPITransportType::TRANSPORT_LAN, "192.168.x.x" },
				{ SCPITransportType::TRANSPORT_UART, "/dev/usb.tty<x>" },
			}},
			{"Keysight E3643A", {
				{ SCPITransportType::TRANSPORT_LAN, "192.168.x.x" },
				{ SCPITransportType::TRANSPORT_UART, "/dev/usb.tty<x>" },
			}},
			{"Keysight E3644A", {
				{ SCPITransportType::TRANSPORT_LAN, "192.168.x.x" },
				{ SCPITransportType::TRANSPORT_UART, "/dev/usb.tty<x>" },
			}},
			{"Keysight E3645A", {
				{ SCPITransportType::TRANSPORT_LAN, "192.168.x.x" },
				{ SCPITransportType::TRANSPORT_UART, "/dev/usb.tty<x>" },
			}},
		};
	}
};

#endif
