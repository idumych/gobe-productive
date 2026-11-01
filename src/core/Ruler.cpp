#include "Ruler.h"

namespace gobe {

const float kRulerUnitSize[6] = {72.0f, 28.3464f, 2.83464f, 12.0f, 1.0f, 1.0f};


RulerTicks
CalcRulerTicks(float unitPixels, int32_t sheetDivisions)
{
	if (sheetDivisions < 1)
		sheetDivisions = 1;
	RulerTicks t;
	t.count = 1;
	if (unitPixels > 0) {
		while (t.count * unitPixels < 40) {
			if (t.count < 2)
				t.count = 2;
			else if (t.count < 5)
				t.count = 5;
			else if (t.count < 10)
				t.count = 10;
			else if (t.count < 20)
				t.count = 20;
			else if (t.count < 50)
				t.count = 50;
			else if (t.count < 100)
				t.count = 100;
			else
				t.count *= 2;
		}
	}
	t.major = t.count * unitPixels;
	if (t.count == 1) {
		t.divisions = sheetDivisions;
		while (t.major < t.divisions * 5.0f) {
			if (t.divisions % 2 == 0)
				t.divisions /= 2;
			else if (t.divisions % 3 == 0)
				t.divisions /= 3;
			else if (t.divisions % 5 == 0)
				t.divisions /= 5;
			else if (t.divisions % 7 == 0)
				t.divisions /= 7;
			else if (t.divisions % 11 == 0)
				t.divisions /= 11;
			else if (t.divisions % 13 == 0)
				t.divisions /= 13;
			else {
				t.divisions = 1;
				break;
			}
		}
	} else if (t.count % 5 == 0)
		t.divisions = 5;
	else {
		t.divisions = t.major > 60 ? 8 : 4;
		if (sheetDivisions * t.count < t.divisions)
			t.divisions = sheetDivisions * t.count;
	}
	t.minor = t.major / t.divisions;
	return t;
}


float
RulerHashHeight(int32_t divisions, int32_t index)
{
	if (index == 0)
		return 0;
	switch (divisions) {
		case 2:
			return index == 1 ? 0.55f : -1;
		case 3:
		case 5:
			return 0.75f;
		case 4:
			if (index == 2)
				return 0.55f;
			return index == 1 || index == 3 ? 0.75f : -1;
		case 8:
			switch (index) {
				case 1: case 3: case 5: case 7:
					return 0.85f;
				case 2: case 6:
					return 0.75f;
				case 4:
					return 0.55f;
				default:
					return -1;
			}
		default:
			if (divisions % 2 != 0 || index != divisions / 2)
				return 0.85f;
			return 0.55f;
	}
}

} // namespace gobe
