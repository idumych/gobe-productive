// zoom_slider: ZoomForSliderValue, SliderValueForZoom
// Expected values computed by /tmp/claude-1000/mytests/zoom_expect.py
// Run: python3 /tmp/claude-1000/mytests/zoom_expect.py
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Zoom.h"

using namespace gobe;

static int sOk = 0, sFailed = 0;

static void
Check(bool ok, const char* what)
{
	if (ok)
		sOk++;
	else {
		sFailed++;
		printf("FAIL %s\n", what);
	}
}

static void
CheckF(float got, float want, const char* what)
{
	Check(fabsf(got - want) < 1e-5f, what);
}


int
main(int argc, char** argv)
{
	(void)argc; (void)argv;

	// ===== 1. ZoomForSliderValue: 21 base + 7 extra =====
	{
		CheckF(ZoomForSliderValue(0.0f), 0.019999999552965164f,
			"zoom v=0.00");
		CheckF(ZoomForSliderValue(0.05f), 0.019999999552965164f,
			"zoom v=0.05");
		CheckF(ZoomForSliderValue(0.1f), 0.03999999910593033f,
			"zoom v=0.10");
		CheckF(ZoomForSliderValue(0.15f), 0.05000000074505806f,
			"zoom v=0.15");
		CheckF(ZoomForSliderValue(0.2f), 0.07999999821186066f,
			"zoom v=0.20");
		CheckF(ZoomForSliderValue(0.25f), 0.12999999523162842f,
			"zoom v=0.25");
		CheckF(ZoomForSliderValue(0.3f), 0.20000000298023224f,
			"zoom v=0.30");
		CheckF(ZoomForSliderValue(0.35f), 0.30000001192092896f,
			"zoom v=0.35");
		CheckF(ZoomForSliderValue(0.4f), 0.44999998807907104f,
			"zoom v=0.40");
		CheckF(ZoomForSliderValue(0.45f), 0.6499999761581421f,
			"zoom v=0.45");
		CheckF(ZoomForSliderValue(0.5f), 1.0f, "zoom v=0.50");
		CheckF(ZoomForSliderValue(0.55f), 1.5f, "zoom v=0.55");
		CheckF(ZoomForSliderValue(0.6f), 2.25f, "zoom v=0.60");
		CheckF(ZoomForSliderValue(0.65f), 3.0f, "zoom v=0.65");
		CheckF(ZoomForSliderValue(0.7f), 5.0f, "zoom v=0.70");
		CheckF(ZoomForSliderValue(0.75f), 8.0f, "zoom v=0.75");
		CheckF(ZoomForSliderValue(0.8f), 12.0f, "zoom v=0.80");
		CheckF(ZoomForSliderValue(0.85f), 18.0f, "zoom v=0.85");
		CheckF(ZoomForSliderValue(0.9f), 28.0f, "zoom v=0.90");
		CheckF(ZoomForSliderValue(0.95f), 42.0f, "zoom v=0.95");
		CheckF(ZoomForSliderValue(1.0f), 64.0f, "zoom v=1.00");

		CheckF(ZoomForSliderValue(-0.5f), 0.019999999552965164f,
			"zoom v=-0.50");
		CheckF(ZoomForSliderValue(1.5f), 64.0f, "zoom v=1.50");
		CheckF(ZoomForSliderValue(0.37f), 0.3499999940395355f,
			"zoom v=0.37");
		CheckF(ZoomForSliderValue(0.5f), 1.0f, "zoom v=0.50");
		CheckF(ZoomForSliderValue(0.62f), 2.75f, "zoom v=0.62");
		CheckF(ZoomForSliderValue(0.7f), 5.0f, "zoom v=0.70");
		CheckF(ZoomForSliderValue(0.83f), 16.0f, "zoom v=0.83");
	}

	// ===== 2. SliderValueForZoom =====
	{
		CheckF(SliderValueForZoom(1.0f / 64.0f), 0.0f,
			"sv z=1/64");
		CheckF(SliderValueForZoom(0.01f), 0.0f, "sv z=0.01");
		CheckF(SliderValueForZoom(0.1f), 0.22317266464233398f,
			"sv z=0.1");
		CheckF(SliderValueForZoom(0.25f), 0.3333333432674408f,
			"sv z=0.25");
		CheckF(SliderValueForZoom(0.5f), 0.4166666567325592f,
			"sv z=0.5");
		CheckF(SliderValueForZoom(1.0f), 0.5f, "sv z=1.0");
		CheckF(SliderValueForZoom(1.5f), 0.5487468838691711f,
			"sv z=1.5");
		CheckF(SliderValueForZoom(2.0f), 0.5833333134651184f,
			"sv z=2.0");
		CheckF(SliderValueForZoom(3.0f), 0.6320801973342896f,
			"sv z=3.0");
		CheckF(SliderValueForZoom(4.0f), 0.6666666865348816f,
			"sv z=4.0");
		CheckF(SliderValueForZoom(8.0f), 0.75f, "sv z=8.0");
		CheckF(SliderValueForZoom(16.0f), 0.8333333134651184f,
			"sv z=16.0");
		CheckF(SliderValueForZoom(64.0f), 1.0f, "sv z=64.0");
		CheckF(SliderValueForZoom(100.0f), 1.0f, "sv z=100.0");
	}

	// ===== 3. Round trip =====
	{
		CheckF(ZoomForSliderValue(SliderValueForZoom(0.25f)), 0.25f,
			"rt zoom=0.25");
		CheckF(ZoomForSliderValue(SliderValueForZoom(0.5f)), 0.5f,
			"rt zoom=0.5");
		CheckF(ZoomForSliderValue(SliderValueForZoom(1.0f)), 1.0f,
			"rt zoom=1.0");
		CheckF(ZoomForSliderValue(SliderValueForZoom(2.0f)), 2.0f,
			"rt zoom=2.0");
		CheckF(ZoomForSliderValue(SliderValueForZoom(4.0f)), 4.0f,
			"rt zoom=4.0");
	}

	printf("zoom ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 || sOk == 0;
}
