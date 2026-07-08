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
	@brief Declaration of RigolDL3000Load
	@ingroup loaddrivers
 */

#ifndef RigolDL3000Load_h
#define RigolDL3000Load_h

/**
	@brief A Rigol DL3000 series programmable DC electronic load (DL3021, DL3021A, DL3031, DL3031A)

	Each getter queries the instrument directly via SCPI. No caching layer.
	Setters use SendCommandQueued (fire-and-forget). The base class Load::AcquireData()
	handles populating scalar streams by calling our getters.

	Tested models expected: DL3021 (40A, 150V, 200W).
 */
class RigolDL3000Load
	: public virtual SCPILoad
{
public:
	RigolDL3000Load(SCPITransport* transport);
	virtual ~RigolDL3000Load();

	//Instrument
	virtual unsigned int GetInstrumentTypes() const override;
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;

	//Load
	virtual LoadMode GetLoadMode(size_t channel) override;
	virtual void SetLoadMode(size_t channel, LoadMode mode) override;

	virtual std::vector<float> GetLoadCurrentRanges(size_t channel) override;
	virtual size_t GetLoadCurrentRange(size_t channel) override;
	virtual void SetLoadCurrentRange(size_t channel, size_t rangeIndex) override;

	virtual std::vector<float> GetLoadVoltageRanges(size_t channel) override;
	virtual size_t GetLoadVoltageRange(size_t channel) override;
	virtual void SetLoadVoltageRange(size_t channel, size_t rangeIndex) override;

	virtual bool GetLoadActive(size_t channel) override;
	virtual void SetLoadActive(size_t channel, bool active) override;

	virtual float GetLoadSetPoint(size_t channel) override;
	virtual void SetLoadSetPoint(size_t channel, float target) override;

protected:
	virtual float GetLoadVoltageActual(size_t channel) override;
	virtual float GetLoadCurrentActual(size_t channel) override;

public:
	static std::string GetDriverNameInternal();
	LOAD_INITPROC(RigolDL3000Load)

	static std::vector<SCPIInstrumentModel> GetDriverSupportedModels()
	{
		return {
			{"Rigol DL3021", {{ SCPITransportType::TRANSPORT_LAN, "192.168.1.x:5025" }}},
			{"Rigol DL3021A", {{ SCPITransportType::TRANSPORT_LAN, "192.168.1.x:5025" }}},
			{"Rigol DL3031", {{ SCPITransportType::TRANSPORT_LAN, "192.168.1.x:5025" }}},
			{"Rigol DL3031A", {{ SCPITransportType::TRANSPORT_LAN, "192.168.1.x:5025" }}}
		};
	}
};

#endif
