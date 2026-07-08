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
	@brief Declaration of SiglentPowerSupply
	@ingroup psudrivers
 */

#ifndef SiglentPowerSupply_h
#define SiglentPowerSupply_h

/**
	@brief A Siglent SPD series power supply (SPD1168X, SPD3303X-E, etc.)

	Each getter queries the instrument directly via SCPI. No caching layer.
	Setters use SendCommandQueued (fire-and-forget). The base class handles
	populating scalar streams by calling our getters.

	The SPD series does not distinguish between set-point and measured output
	via separate SCPI queries — CH<n>:VOLT? is used for both GetPowerVoltageActual
	and GetPowerVoltageNominal (same for current). MEASure subsystem is documented
	by Siglent but may timeout on older firmware versions.
 */
class SiglentPowerSupply
	: public virtual SCPIPowerSupply
	, public virtual SCPIDevice
{
public:
	SiglentPowerSupply(SCPITransport* transport);
	virtual ~SiglentPowerSupply();

	//Device information
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;

	//Device capabilities
	virtual bool SupportsIndividualOutputSwitching() override;
	virtual bool SupportsVoltageCurrentControl(int chan) override;
	virtual bool SupportsOvercurrentShutdown() override;

	//Read sensors — each hits SCPI directly (no cache)
	virtual double GetPowerVoltageActual(int chan) override;
	virtual double GetPowerVoltageNominal(int chan) override;
	virtual double GetPowerCurrentActual(int chan) override;
	virtual double GetPowerCurrentNominal(int chan) override;
	virtual bool GetPowerChannelActive(int chan) override;

	//Configuration
	virtual void SetPowerVoltage(int chan, double volts) override;
	virtual void SetPowerCurrent(int chan, double amps) override;
	virtual void SetPowerChannelActive(int chan, bool on) override;
	virtual bool IsPowerConstantCurrent(int chan) override;

	//Overcurrent / overvoltage protection (SPD1000X series via SCPI)
	virtual bool GetPowerOvercurrentShutdownEnabled(int chan) override;
	virtual void SetPowerOvercurrentShutdownEnabled(int chan, bool enable) override;
	virtual bool GetPowerOvercurrentShutdownTripped(int chan) override;

protected:
	/// @brief Query SYST:STAT? and return the hex status word
	unsigned int GetStatusRegister();

	//Model-specific properties set in constructor from *IDN?
	int m_numChannels;			// 1: SPD1168X/SPD1305X, 3: SPD3303X-E
	bool m_hasOCP;				// SCPI OVP/OCP support (SPD1000X yes, SPD3303X no)
	bool m_ch3on;				// CH3 output state (no SCPI query available on SPD3303X)

public:
	static std::string GetDriverNameInternal();
	POWER_INITPROC(SiglentPowerSupply);

	static std::vector<SCPIInstrumentModel> GetDriverSupportedModels()
	{
		return {
			{"Siglent SPD1168X", {{ SCPITransportType::TRANSPORT_LAN, "192.168.1.x:5025" }}},
			{"Siglent SPD1305X", {{ SCPITransportType::TRANSPORT_LAN, "192.168.1.x:5025" }}},
			{"Siglent SPD3303X-E", {{ SCPITransportType::TRANSPORT_LAN, "192.168.1.x:5025" }}}
		};
	}
};

#endif
