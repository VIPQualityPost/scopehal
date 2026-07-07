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
	@brief Declaration of KeysightEDU36311APowerSupply
	@ingroup psudrivers
 */

#ifndef KeysightEDU36311APowerSupply_h
#define KeysightEDU36311APowerSupply_h

/**
	@brief A Keysight EDU36311A triple output power supply

	Three channels:
	   - CH1 (P6V):  +6 V / 5 A
	   - CH2 (P30V): +30 V / 1 A
	   - CH3 (N30V): +30 V / 1 A (negative rail, -30.9 V to 0 V)

	This instrument uses (@<chanlist>) addressing (1-based channel numbers) for most
	SCPI commands, distinct from the CH1/SOURCE1 prefix model used by Rigol / Siglent supplies.
 */
class KeysightEDU36311APowerSupply
	: public virtual SCPIPowerSupply
	, public virtual SCPIDevice
{
public:
	KeysightEDU36311APowerSupply(SCPITransport* transport);
	virtual ~KeysightEDU36311APowerSupply();

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
	std::string Chanlist(int chan);
	bool GetPowerOvervoltageTripped(int chan);

	// Cached per-channel state (updated by AcquireData, ~20ms interval)
	double m_cachedVoltageActual[3];
	double m_cachedVoltageNominal[3];
	double m_cachedCurrentActual[3];
	double m_cachedCurrentNominal[3];
	bool m_cachedConstantCurrent[3];
	bool m_cachedOCPTripped[3];
	bool m_cachedChannelActive[3];
	bool m_cachedOVTripped[3];

public:
	static std::string GetDriverNameInternal();
	POWER_INITPROC(KeysightEDU36311APowerSupply)

	// This is intentionally not virtual since it's a static method used by enumeration
	// cppcheck-suppress duplInheritedMember
	static std::vector<SCPIInstrumentModel> GetDriverSupportedModels()
	{
		return {
        {"Keysight EDU36311A", {{ SCPITransportType::TRANSPORT_LAN, "192.168.x.x" }}}
        };
	}
};

#endif
