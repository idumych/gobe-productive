#include "Zoom.h"
#include <cmath>

namespace gobe {

float
ZoomForSliderValue(float value)
{
	// clamp to [0, 1]
	if (value < 0.0f)
		value = 0.0f;
	if (value > 1.0f)
		value = 1.0f;

	// z = 2^(value * 12 - 6)
	float z = powf(2.0f, value * 12.0f - 6.0f);

	// round to nearest allowed step
	int n;
	float r;
	if (z < 0.15f) {
		n = int(z * 100.0f + 0.5f);
		r = n / 100.0f;
	} else if (z < 1.0f) {
		n = int(z * 100.0f / 5.0f + 0.5f);
		r = 5.0f * n / 100.0f;
	} else if (z <= 3.0f) {
		n = int(z * 100.0f / 25.0f + 0.5f);
		r = 25.0f * n / 100.0f;
	} else {
		n = int(z + 0.5f);
		r = float(n);
	}

	// clamp to [1/64, 64]
	if (r < 1.0f / 64.0f)
		r = 1.0f / 64.0f;
	if (r > 64.0f)
		r = 64.0f;

	return r;
}

float
SliderValueForZoom(float zoom)
{
	// clamp to [1/64, 64]
	if (zoom < 1.0f / 64.0f)
		zoom = 1.0f / 64.0f;
	if (zoom > 64.0f)
		zoom = 64.0f;

	float r = (6.0f + log2f(zoom)) / 12.0f;
	if (r < 0.0f)
		r = 0.0f;
	if (r > 1.0f)
		r = 1.0f;

	return r;
}

} // namespace gobe
