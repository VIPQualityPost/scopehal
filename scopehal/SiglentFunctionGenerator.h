/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2024 Andrew D. Zonenberg and contributors                                                         *
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
	@author Andrew D. Zonenberg
	@brief Declaration of SiglentFunctionGenerator
	@ingroup funcdrivers
 */

#ifndef SiglentFunctionGenerator_h
#define SiglentFunctionGenerator_h

#include <chrono>

/**
	@brief A Siglent SDG function generator
	@ingroup funcdrivers
 */
class SiglentFunctionGenerator : public virtual SCPIFunctionGenerator
{
public:
	SiglentFunctionGenerator(SCPITransport* transport);
	virtual ~SiglentFunctionGenerator();

	//Device information
	virtual unsigned int GetInstrumentTypes() const override;
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;

	virtual bool AcquireData() override;

	virtual void FlushConfigCache() override;

	virtual std::vector<WaveShape> GetAvailableWaveformShapes(int chan) override;

	virtual bool GetFunctionChannelActive(int chan) override;
	virtual void SetFunctionChannelActive(int chan, bool on) override;

	virtual bool HasFunctionDutyCycleControls(int chan) override;
	virtual float GetFunctionChannelDutyCycle(int chan) override;
	virtual void SetFunctionChannelDutyCycle(int chan, float duty) override;

	virtual float GetFunctionChannelAmplitude(int chan) override;
	virtual void SetFunctionChannelAmplitude(int chan, float amplitude) override;

	virtual float GetFunctionChannelOffset(int chan) override;
	virtual void SetFunctionChannelOffset(int chan, float offset) override;

	virtual float GetFunctionChannelFrequency(int chan) override;
	virtual void SetFunctionChannelFrequency(int chan, float hz) override;

	virtual WaveShape GetFunctionChannelShape(int chan) override;
	virtual void SetFunctionChannelShape(int chan, WaveShape shape) override;

	virtual bool HasFunctionRiseFallTimeControls(int chan) override;
	virtual float GetFunctionChannelRiseTime(int chan) override;
	virtual void SetFunctionChannelRiseTime(int chan, float fs) override;
	virtual float GetFunctionChannelFallTime(int chan) override;
	virtual void SetFunctionChannelFallTime(int chan, float fs) override;

	virtual bool HasFunctionImpedanceControls(int chan) override;
	virtual OutputImpedance GetFunctionChannelOutputImpedance(int chan) override;
	virtual void SetFunctionChannelOutputImpedance(int chan, OutputImpedance z) override;

	virtual bool HasFunctionPhaseControls(int chan) override;
	virtual float GetFunctionChannelPhase(int chan) override;
	virtual void SetFunctionChannelPhase(int chan, float deg) override;

public:
	static std::string GetDriverNameInternal();
	GENERATOR_INITPROC(SiglentFunctionGenerator)

	static std::vector<SCPIInstrumentModel> GetDriverSupportedModels()
	{
		return {
			{"Siglent SDG2000X", {
				{ SCPITransportType::TRANSPORT_LAN, "<ip_address>:5025" },
				{ SCPITransportType::TRANSPORT_USBTMC, "/dev/usbtmc<x>" },
			}},
			{"Siglent SDG1000X", {
				{ SCPITransportType::TRANSPORT_LAN, "<ip_address>:5025" },
				{ SCPITransportType::TRANSPORT_USBTMC, "/dev/usbtmc<x>" },
			}},
			{"Siglent SDG6000X/X-E", {
				{ SCPITransportType::TRANSPORT_LAN, "<ip_address>:5025" },
				{ SCPITransportType::TRANSPORT_USBTMC, "/dev/usbtmc<x>" },
			}},
			{"Siglent SDG7000A", {
				{ SCPITransportType::TRANSPORT_LAN, "<ip_address>:5025" },
				{ SCPITransportType::TRANSPORT_USBTMC, "/dev/usbtmc<x>" },
			}},
			{"Siglent SDG1000", {
				{ SCPITransportType::TRANSPORT_LAN, "<ip_address>:5025" },
				{ SCPITransportType::TRANSPORT_USBTMC, "/dev/usbtmc<x>" },
			}},
			{"Siglent SDG5000", {
				{ SCPITransportType::TRANSPORT_LAN, "<ip_address>:5025" },
				{ SCPITransportType::TRANSPORT_USBTMC, "/dev/usbtmc<x>" },
			}},
			{"Siglent SDG800", {
				{ SCPITransportType::TRANSPORT_LAN, "<ip_address>:5025" },
				{ SCPITransportType::TRANSPORT_USBTMC, "/dev/usbtmc<x>" },
			}},
		};
	}

protected:

	/**
		@brief Internal channel class that exposes a public parameter accessor so the
		driver can add custom channel-level parameters.
	 */
	class SiglentFunctionGeneratorChannel : public FunctionGeneratorChannel
	{
	public:
		SiglentFunctionGeneratorChannel(
			FunctionGenerator* gen,
			const std::string& hwname,
			const std::string& color,
			size_t index)
			: FunctionGeneratorChannel(gen, hwname, color, index)
		{}

		virtual ~SiglentFunctionGeneratorChannel()
		{}

		FilterParameter& GetParam(const std::string& name)
		{ return m_parameters[name]; }
	};

	//Config cache
	bool m_cachedFrequencyValid[2];
	float m_cachedFrequency[2];
	bool m_cachedEnableStateValid[2];
	bool m_cachedOutputEnable[2];
	bool m_cachedAmplitudeValid[2];
	float m_cachedAmplitude[2];
	bool m_cachedOffsetValid[2];
	float m_cachedOffset[2];
	OutputImpedance m_cachedImpedance[2];
	bool m_cachedImpedanceValid[2];

	WaveShape m_cachedWaveShape[2];
	bool m_cachedWaveShapeValid[2];

	bool m_cachedDutyCycleValid[2];
	float m_cachedDutyCycle[2];

	bool m_cachedRiseTimeValid[2];
	float m_cachedRiseTime[2];
	bool m_cachedFallTimeValid[2];
	float m_cachedFallTime[2];

	bool m_cachedPhaseValid[2];
	float m_cachedPhase[2];

	bool m_cachedCombine[2];
	bool m_cachedCombineValid[2];

	//Time of the last CMBN? poll on each channel. Used to rate-limit the
	//front-panel combine state polling in AcquireData().
	std::chrono::steady_clock::time_point m_lastCombinePoll[2];

	bool m_supportsCHDR;

	//True once MODE PHASELOCKED has been sent, so relative phase offset between
	//the two channels takes effect. Set lazily on first phase write.
	bool m_phaseLockedSet = false;

	//Marks every cached parameter of a channel invalid so the next read goes out
	//to the instrument. Setters call this after queuing a write: the instrument
	//may clamp, quantize, or reject the requested value (e.g. the SDG2042X caps
	//square wave frequency at 25 MHz while sine allows 40 MHz), so the cached
	//state must reflect what the instrument actually did, not what we asked for.
	void InvalidateChannelCache(size_t chan);

	std::string RemoveHeader(const std::string& str);

	void ParseOutputState(const std::string& str, size_t i);
	void ParseBasicWaveform(const std::string& str, size_t i);
};

#endif
