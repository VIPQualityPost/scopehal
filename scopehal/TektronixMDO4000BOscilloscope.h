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
* THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES             *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR       *
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE       *
* POSSIBILITY OF SUCH DAMAGE.                                                                                          *
*                                                                                                                      *
***********************************************************************************************************************/

/**
	@file
	@author Andrew D. Zonenberg
	@brief Declaration of TektronixMDO4000BOscilloscope

	@ingroup scopedrivers
 */

#ifndef TektronixMDO4000BOscilloscope_h
#define TektronixMDO4000BOscilloscope_h

#include <TektronixOscilloscope.h>

/**
	@brief Driver for Tektronix MDO4000/B/C and MSO/DPO4000B series oscilloscopes

	The MDO4000B is a Mixed Domain Oscilloscope with 4 analog channels,
	optional 16-channel digital input (MSO), built-in RF spectrum analyzer,
	optional DVM, and optional AFG.

	Key differences from the MSO5/6 series:
	- No FlexChannel architecture; uses D0-D15 for digital (MSO pod)
	- Uses RF: command group instead of SV: for spectrum analysis
	- Uses TRIGger:A: prefix for trigger commands
	- Uses HORizontal:SCAle for time/div
	- Uses WFMOutpre? preamble format (not the mso56 format)

	@ingroup scopedrivers
 */
class TektronixMDO4000BOscilloscope
	: public virtual TektronixOscilloscope
{
public:
	TektronixMDO4000BOscilloscope(SCPITransport* transport);
	virtual ~TektronixMDO4000BOscilloscope();

	//not copyable or assignable
	TektronixMDO4000BOscilloscope(const TektronixMDO4000BOscilloscope& rhs) =delete;
	TektronixMDO4000BOscilloscope& operator=(const TektronixMDO4000BOscilloscope& rhs) =delete;

public:

	//Channel configuration
	virtual bool IsChannelEnabled(size_t i) override;
	virtual void EnableChannel(size_t i) override;
	virtual void DisableChannel(size_t i) override;
	virtual OscilloscopeChannel::CouplingType GetChannelCoupling(size_t i) override;
	virtual void SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type) override;
	virtual double GetChannelAttenuation(size_t i) override;
	virtual void SetChannelAttenuation(size_t i, double atten) override;
	virtual unsigned int GetChannelBandwidthLimit(size_t i) override;
	virtual void SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz) override;
	virtual std::vector<unsigned int> GetChannelBandwidthLimiters(size_t i) override;
	virtual float GetChannelOffset(size_t i, size_t stream) override;
	virtual void SetChannelOffset(size_t i, size_t stream, float offset) override;
	virtual float GetChannelVoltageRange(size_t i, size_t stream) override;
	virtual void SetChannelVoltageRange(size_t i, size_t stream, float range) override;

	//Triggering
	virtual Oscilloscope::TriggerMode PollTrigger() override;
	virtual bool PeekTriggerArmed() override;
	virtual bool AcquireData() override;
	virtual void Start() override;
	virtual void StartSingleTrigger() override;
	virtual void Stop() override;
	virtual void ForceTrigger() override;
	virtual void PushTrigger() override;
	virtual void PullTrigger() override;
	virtual std::vector<std::string> GetTriggerTypes() override;

	//Sample rate and depth
	virtual std::vector<uint64_t> GetSampleRatesNonInterleaved() override;
	virtual std::vector<uint64_t> GetSampleDepthsNonInterleaved() override;
	virtual uint64_t GetSampleRate() override;
	virtual uint64_t GetSampleDepth() override;
	virtual void SetSampleDepth(uint64_t depth) override;
	virtual void SetSampleRate(uint64_t rate) override;
	virtual void SetTriggerOffset(int64_t offset) override;
	virtual int64_t GetTriggerOffset() override;
	virtual bool HasInterleavingControls() override;

	//Logic analyzer - MDO4000B supports D0-D15 via MSO pod
	virtual std::vector<Oscilloscope::DigitalBank> GetDigitalBanks() override;
	virtual Oscilloscope::DigitalBank GetDigitalBank(size_t channel) override;
	virtual bool IsDigitalHysteresisConfigurable() override;
	virtual bool IsDigitalThresholdConfigurable() override;
	virtual float GetDigitalThreshold(size_t channel) override;
	virtual void SetDigitalThreshold(size_t channel, float level) override;

	//Spectrum analyzer (RF input)
	virtual bool HasFrequencyControls() override;
	virtual void SetSpan(int64_t span) override;
	virtual int64_t GetSpan() override;
	virtual void SetCenterFrequency(size_t channel, int64_t freq) override;
	virtual int64_t GetCenterFrequency(size_t channel) override;
	virtual void SetResolutionBandwidth(int64_t rbw) override;
	virtual int64_t GetResolutionBandwidth() override;

public:
	static std::string GetDriverNameInternal();
	OSCILLOSCOPE_INITPROC(TektronixMDO4000BOscilloscope);

protected:

	///@brief Waveform preamble for the MDO4000B WFMOutpre format
	struct mdo4k_preamble
	{
		int byte_nr;
		int bit_nr;
		char encdg[32];
		char bn_fmt[32];
		char byt_or[32];
		char wfid[256];
		int nr_pt;
		char pt_fmt[8];
		char pt_order[32];
		char xunit[32];
		double xincrement;
		double xzero;
		int pt_off;
		char yunit[32];
		double ymult;
		double yoff;
		double yzero;
		char domain[32];
		char wfmtype[32];
		double centerfreq;
		double span;
	};

	///@brief Parse a WFMOutpre? response into a preamble struct
	bool ReadWFMOutprePreamble(const std::string& preamble_in, struct mdo4k_preamble& preamble_out);

	///@brief Query and cache the maximum analog sample rate (CONFIG:ANALO:MAXSAMPLER?)
	uint64_t GetMaxAnalogSampleRate();

	///@brief Cached value and validity flag for GetMaxAnalogSampleRate()
	uint64_t m_maxSampleRate = 0;
	bool m_maxSampleRateValid = false;

	///@brief True if the instrument has an integrated RF spectrum analyzer
	///(MDO series only; MSO/DPO4000B models have no RF input)
	bool m_hasRF = false;

	///@brief Query and cache the supported record lengths (CONFIG:ANALO:RECLENS?)
	std::vector<uint64_t> GetSupportedSampleDepths();

	///@brief Cached value and validity flag for GetSupportedSampleDepths()
	std::vector<uint64_t> m_supportedSampleDepths;
	bool m_supportedSampleDepthsValid = false;

	///@brief 1-2-5 time/div steps inside the HOR:SCALE range (400 ps..1000 s)
	std::vector<double> GetTimebaseScales();

	///@brief Acquire analog data
	bool AcquireAnalogData(std::map<int, std::vector<WaveformBase*> >& pending_waveforms);

	///@brief Acquire digital data (D0-D15)
	bool AcquireDigitalData(std::map<int, std::vector<WaveformBase*> >& pending_waveforms);

	///@brief Acquire RF spectrum data
	bool AcquireRFData(std::map<int, std::vector<WaveformBase*> >& pending_waveforms);

	///@brief Set the trigger level for a given channel on the scope
	void SetTriggerLevel(Trigger* trig);

	///@brief Read the trigger level for a given channel
	float ReadTriggerLevel(OscilloscopeChannel* chan);

	///@brief Digital channel base/count are stored in the base class fields
	///m_digitalChannelBase / m_digitalChannelCount

	/**
		@brief Check if a channel is digital given the index

		@param index	Channel number

		@return	True if digital, false if analog or spectrum
	 */
	bool IsDigitalChannel(size_t index)
	{
		if(index < m_digitalChannelBase)
			return false;
		return (index < (m_digitalChannelBase + m_digitalChannelCount));
	}

	///@brief Rebuild the current trigger object as the given type, replacing any existing trigger
	template<class T>
	T* RecreateTrigger()
	{
		delete m_trigger;
		m_trigger = new T(this);
		return dynamic_cast<T*>(m_trigger);
	}
	
	void PushEdgeTrigger(EdgeTrigger* trig);
	void PullEdgeTrigger();
	void PushPulseWidthTrigger(PulseWidthTrigger* trig);
	void PullPulseWidthTrigger();
	void PushDropoutTrigger(DropoutTrigger* trig);
	void PullDropoutTrigger();
	void PushRuntTrigger(RuntTrigger* trig);
	void PullRuntTrigger();
	void PushSlewRateTrigger(SlewRateTrigger* trig);
	void PullSlewRateTrigger();
	void PushWindowTrigger(WindowTrigger* trig);
	void PullWindowTrigger();
};

#endif
