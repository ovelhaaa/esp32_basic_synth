// Based on an implementation by Magnus Jonsson
// https://github.com/magnusjonsson/microtracker (unlicense)
// Modified for ESP32 float optimization

#pragma once

#ifndef MICROTRACKER_MODEL_H
#define MICROTRACKER_MODEL_H

#include "LadderFilterBase.h"
#include "MoogUtil.h"

class MicrotrackerMoog : public LadderFilterBase
{

public:

	MicrotrackerMoog(float sampleRate) : LadderFilterBase(sampleRate)
	{
		p0 = p1 = p2 = p3 = p32 = p33 = p34 = 0.0f;
		SetCutoff(1000.0f);
		SetResonance(0.10f);
	}

	virtual ~MicrotrackerMoog() {}

	virtual void Process(float * samples, uint32_t n) override
	{
		float k = resonance * 4.0f;
		for (int s = 0; s < n; ++s)
		{
			// Coefficients optimized using differential evolution
			// to make feedback gain 4.0 correspond closely to the
			// border of instability, for all values of omega.
			// Using float constants
			float out = p3 * 0.360891f + p32 * 0.417290f + p33 * 0.177896f + p34 * 0.0439725f;

			p34 = p33;
			p33 = p32;
			p32 = p3;

            // Using tanh (float version if available, or just standard tanh)
            // fast_tanh returns double in utils.h, let's cast or rely on optimization
			p0 += (fast_tanh(samples[s] - k * out) - fast_tanh(p0)) * cutoff;
			p1 += (fast_tanh(p0) - fast_tanh(p1)) * cutoff;
			p2 += (fast_tanh(p1) - fast_tanh(p2)) * cutoff;
			p3 += (fast_tanh(p2) - fast_tanh(p3)) * cutoff;

			samples[s] = out;
		}
	}

	virtual void SetResonance(float r) override
	{
		resonance = r;
	}

	virtual void SetCutoff(float c) override
	{
		cutoff = c * 2 * MOOG_PI / sampleRate;
		cutoff = moog_min(cutoff, 1.0f);
	}

private:

	float p0;
	float p1;
	float p2;
	float p3;
	float p32;
	float p33;
	float p34;
};

#endif
