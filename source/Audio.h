#pragma once

#include "Controls.h"

#include <earshot/earshot.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace earshot::testing
{
class CaptureBackend;
}

/**
    Where the piston's motion comes from: earshot (a snapshot in
    external/earshot; SNAPSHOT says which commit).

    **A device** (earshot's own capture, opened by its worker thread, never on
    the render thread): the waveform tap at 16 kHz, contiguous, every sample
    once. That is the piston's velocity, times v0 (Volume). The duct integrates
    it at that rate, so the flames do not depend on the frame rate.

    **Resolume's FFT** (no device chosen, or a chosen one Opening, Missing or
    Failed): earshot's fallback gives band envelopes only, once a frame, no
    phase and no waveform, and the tap is empty. So the piston is driven by
    band-limited NOISE, one band per earshot band (40-160-640-2560-8000 Hz), a
    deterministic generator through a two-pole band-pass, its RMS that of the
    sine the band's level describes, ramped across the frame from the band's
    last level to this one. The tube still picks its own resonances out of it.
    Lost: the melody (which notes, so which modes), the waveform and transients
    inside a frame, calibration (FFT Law is the assumption, exposed) and
    frame-rate independence.

    **Pitch is not used**: the tap carries strictly more (SPEC-rubens.md).

    **The device menu** follows spasis, with earshot's rule that a device is
    remembered by name: element 0 "None (host FFT)" = 0, element 1 "System
    default" = deviceKey( kDefaultDevice ), then each device with
    deviceKey( name ), then "(none)" spares (value 0), kInputSlots in all.
    `Select` acts only when the value changes, so the initial state never calls
    selectDevice( "" ) -- which would end a feed() (woofer's wiring).
*/
namespace rubens
{

using BackendFactory = std::unique_ptr< earshot::testing::CaptureBackend > ( * )();

struct DeviceSlot
{
	std::string label;///< what the menu shows (30 characters at most)
	std::string name; ///< what selectDevice is given ("" for the host FFT)
	float value;      ///< the element's value: the device key
};

class AudioInput
{
public:
	/// For the harness: every AudioInput constructed afterwards uses this
	/// backend (a scripted device) and lists ITS devices. Never set by the plugin.
	static void SetBackendFactoryForTest( BackendFactory factory );

	AudioInput();
	~AudioInput();

	/// Enumerate again (constructor and Rescan). Never per frame.
	void Refresh();
	const std::vector< DeviceSlot >& Slots() const
	{
		return slots;
	}

	/// The Audio Input value the host stored; acts only when it changes.
	void Select( float value );

	/// This frame's piston signal (full scale = 1) at tube::kTapRate into
	/// `out`. `hostSeconds` is the host clock, `dt` this frame's step of it
	/// (the fallback's sample count).
	int Pull( double hostSeconds, double dt, const float* bins, int binCount, Channel channel, FftLaw law, std::vector< float >& out );

	const earshot::Frame& Frame() const;
	earshot::CaptureState State() const;
	bool FromDevice() const
	{
		return lastFromDevice;
	}

	//-- Harness ------------------------------------------------------------------
	/// Audio as if from a device, without one: earshot's feed().
	void Feed( const float* interleaved, int frames, int rate, int channels );
	earshot::Engine& Engine()
	{
		return *engine;
	}
	bool fallbackFromTap = false;///< negative: the fallback reads the (empty) tap
	bool menuByIndex     = false;///< negative: element values are positions

private:
	std::unique_ptr< earshot::Engine > engine;
	std::vector< DeviceSlot > slots;
	std::map< float, std::string > seen;///< every device key met, and its name
	float applied       = 0.0f;
	bool feeding        = false;
	int appliedLaw      = -1;
	bool lastFromDevice = false;

	//The fallback's noise: per band a biquad's state and its RMS correction.
	struct Band
	{
		double b0 = 0, b2 = 0, a1 = 0, a2 = 0;
		double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
		double gain  = 1.0;
		double level = 0.0;
	};
	std::vector< Band > bands;
	uint32_t noiseState = 0x9e3779b9u;
	double carry        = 0.0;
	std::vector< float > left, right;
	double nextNoise();
};

} // namespace rubens
