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

#ifndef RigolSpectrumAnalyzer_h
#define RigolSpectrumAnalyzer_h

/**
	@brief Driver for Rigol DSA800 series spectrum analyzers

	Supports DSA815, DSA832, DSA875, and the DSA800E series.

	@ingroup oscilloscopedrivers
 */
class RigolSpectrumAnalyzer : public virtual SCPIOscilloscope
{
public:
	RigolSpectrumAnalyzer(SCPITransport* transport);
	virtual ~RigolSpectrumAnalyzer();

	//not copyable or assignable
	RigolSpectrumAnalyzer(const RigolSpectrumAnalyzer& rhs) = delete;
	RigolSpectrumAnalyzer& operator=(const RigolSpectrumAnalyzer& rhs) = delete;

public:

	virtual unsigned int GetInstrumentTypes() const override;
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;

	virtual void FlushConfigCache() override;

	//Channel configuration
	virtual bool IsChannelEnabled(size_t i) override;
	virtual void EnableChannel(size_t i) override;
	virtual void DisableChannel(size_t i) override;
	virtual OscilloscopeChannel::CouplingType GetChannelCoupling(size_t i) override;
	virtual void SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type) override;
	virtual std::vector<OscilloscopeChannel::CouplingType> GetAvailableCouplings(size_t i) override;
	virtual double GetChannelAttenuation(size_t i) override;
	virtual void SetChannelAttenuation(size_t i, double atten) override;
	virtual std::vector<unsigned int> GetChannelBandwidthLimiters(size_t i) override;
	virtual unsigned int GetChannelBandwidthLimit(size_t i) override;
	virtual void SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz) override;
	virtual float GetChannelVoltageRange(size_t i, size_t stream) override;
	virtual void SetChannelVoltageRange(size_t i, size_t stream, float range) override;
	virtual OscilloscopeChannel* GetExternalTrigger() override;
	virtual float GetChannelOffset(size_t i, size_t stream) override;
	virtual void SetChannelOffset(size_t i, size_t stream, float offset) override;

	//Triggering
	virtual Oscilloscope::TriggerMode PollTrigger() override;
	virtual bool AcquireData() override;
	virtual void Start() override;
	virtual void StartSingleTrigger() override;
	virtual void Stop() override;
	virtual void ForceTrigger() override;
	virtual bool IsTriggerArmed() override;
	virtual void PushTrigger() override;
	virtual void PullTrigger() override;

	//Sample rate / memory depth (not really meaningful for a spectrum analyzer)
	virtual std::vector<uint64_t> GetSampleRatesNonInterleaved() override;
	virtual std::vector<uint64_t> GetSampleRatesInterleaved() override;
	virtual std::set<InterleaveConflict> GetInterleaveConflicts() override;
	virtual std::vector<uint64_t> GetSampleDepthsNonInterleaved() override;
	virtual std::vector<uint64_t> GetSampleDepthsInterleaved() override;
	virtual uint64_t GetSampleRate() override;
	virtual uint64_t GetSampleDepth() override;
	virtual void SetSampleDepth(uint64_t depth) override;
	virtual void SetSampleRate(uint64_t rate) override;
	virtual void SetTriggerOffset(int64_t offset) override;
	virtual int64_t GetTriggerOffset() override;
	virtual bool IsInterleaving() override;
	virtual bool SetInterleaving(bool combine) override;

	//Frequency domain controls
	virtual bool HasFrequencyControls() override;
	virtual bool HasResolutionBandwidth() override;
	virtual bool HasTimebaseControls() override;
	virtual void SetSpan(int64_t span) override;
	virtual int64_t GetSpan() override;
	virtual void SetCenterFrequency(size_t channel, int64_t freq) override;
	virtual int64_t GetCenterFrequency(size_t channel) override;
	virtual void SetResolutionBandwidth(int64_t rbw) override;
	virtual int64_t GetResolutionBandwidth() override;

protected:

	///@brief The analog input channel
	OscilloscopeChannel* m_spectrumChannel;

	///@brief Current frequency configuration (cached)
	int64_t m_centerFreq;
	int64_t m_span;
	int64_t m_rbw;
	int64_t m_vbw;
	int64_t m_sweepTime;

	///@brief Sweep points
	uint64_t m_sweepPoints;

	///@brief Current reference level (dBm)
	float m_refLevel;

	///@brief Current scale/div (dB)
	float m_scalePerDiv;

	///@brief Input attenuation (dB)
	float m_inputAttenuation;

	///@brief Preamplifier enabled
	bool m_preampEnabled;

	///@brief Frequency limits for the instrument (derived from model)
	int64_t m_freqMax;
	int64_t m_freqMin;

	///@brief Convert a Rigol model string to max frequency
	int64_t ModelToMaxFreq(const std::string& model);

public:
	static std::string GetDriverNameInternal();

	static std::vector<SCPIInstrumentModel> GetDriverSupportedModels()
	{
		return {
			{"Rigol DSA815", {
				{ SCPITransportType::TRANSPORT_LAN, "<ip_address>:5555" },
				{ SCPITransportType::TRANSPORT_USBTMC, "/dev/usbtmc<x>" },
			}},
			{"Rigol DSA832", {
				{ SCPITransportType::TRANSPORT_LAN, "<ip_address>:5555" },
				{ SCPITransportType::TRANSPORT_USBTMC, "/dev/usbtmc<x>" },
			}},
			{"Rigol DSA875", {
				{ SCPITransportType::TRANSPORT_LAN, "<ip_address>:5555" },
				{ SCPITransportType::TRANSPORT_USBTMC, "/dev/usbtmc<x>" },
			}},
			{"Rigol DSA810", {
				{ SCPITransportType::TRANSPORT_LAN, "<ip_address>:5555" },
				{ SCPITransportType::TRANSPORT_USBTMC, "/dev/usbtmc<x>" },
			}},
			{"Rigol DSA815E", {
				{ SCPITransportType::TRANSPORT_LAN, "<ip_address>:5555" },
				{ SCPITransportType::TRANSPORT_USBTMC, "/dev/usbtmc<x>" },
			}},
		};
	}

	OSCILLOSCOPE_INITPROC(RigolSpectrumAnalyzer)
};

#endif
