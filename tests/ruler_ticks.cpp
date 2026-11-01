// ruler_ticks: kRulerUnitSize, CalcRulerTicks, RulerHashHeight
// Expected values computed by /tmp/claude-1000/mytests/ruler_expect.py
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Ruler.h"

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
	Check(fabsf(got - want) < 0.001f, what);
}


int
main(int argc, char** argv)
{
	(void)argc; (void)argv;

	// ===== 1. kRulerUnitSize (6 checks) =====
	{
		const float expected[6] = {72.0f, 28.3464f, 2.83464f, 12.0f, 1.0f, 1.0f};
		for (int i = 0; i < 6; i++) {
			char buf[64];
			snprintf(buf, sizeof buf, "kRulerUnitSize[%d] == %.4f", i, expected[i]);
			CheckF(kRulerUnitSize[i], expected[i], buf);
		}
	}

	// ===== 2. CalcRulerTicks =====
	// 66 combinations × 4 fields = 264 checks
	{
		static const float unitPixels[] = {
			72.0f, 36.0f, 28.3464f, 14.1732f, 2.83464f,
			12.0f, 6.0f, 1.0f, 0.5f, 0.25f, 144.0f,
		};
		static const int32_t sheetDiv[] = {8, 10, 16, 12, 7, 1};
		static const int32_t expCount[] = {
			// unit=72:
			1, 1, 1, 1, 1, 1,
			// unit=36:
			2, 2, 2, 2, 2, 2,
			// unit=28.3464:
			2, 2, 2, 2, 2, 2,
			// unit=14.1732:
			5, 5, 5, 5, 5, 5,
			// unit=2.83464:
			20, 20, 20, 20, 20, 20,
			// unit=12:
			5, 5, 5, 5, 5, 5,
			// unit=6:
			10, 10, 10, 10, 10, 10,
			// unit=1:
			50, 50, 50, 50, 50, 50,
			// unit=0.5:
			100, 100, 100, 100, 100, 100,
			// unit=0.25:
			200, 200, 200, 200, 200, 200,
			// unit=144:
			1, 1, 1, 1, 1, 1,
		};
		static const int32_t expDiv[] = {
			// unit=72:
			8, 10, 8, 12, 7, 1,
			// unit=36:
			8, 8, 8, 8, 8, 2,
			// unit=28.3464:
			4, 4, 4, 4, 4, 2,
			// unit=14.1732:
			5, 5, 5, 5, 5, 5,
			// unit=2.83464:
			5, 5, 5, 5, 5, 5,
			// unit=12:
			5, 5, 5, 5, 5, 5,
			// unit=6:
			5, 5, 5, 5, 5, 5,
			// unit=1:
			5, 5, 5, 5, 5, 5,
			// unit=0.5:
			5, 5, 5, 5, 5, 5,
			// unit=0.25:
			5, 5, 5, 5, 5, 5,
			// unit=144:
			8, 10, 16, 12, 7, 1,
		};
		static const float expMajor[] = {
			// unit=72:
			72.0f, 72.0f, 72.0f, 72.0f, 72.0f, 72.0f,
			// unit=36:
			72.0f, 72.0f, 72.0f, 72.0f, 72.0f, 72.0f,
			// unit=28.3464:
			56.6928f, 56.6928f, 56.6928f, 56.6928f, 56.6928f, 56.6928f,
			// unit=14.1732:
			70.8660f, 70.8660f, 70.8660f, 70.8660f, 70.8660f, 70.8660f,
			// unit=2.83464:
			56.6928f, 56.6928f, 56.6928f, 56.6928f, 56.6928f, 56.6928f,
			// unit=12:
			60.0f, 60.0f, 60.0f, 60.0f, 60.0f, 60.0f,
			// unit=6:
			60.0f, 60.0f, 60.0f, 60.0f, 60.0f, 60.0f,
			// unit=1:
			50.0f, 50.0f, 50.0f, 50.0f, 50.0f, 50.0f,
			// unit=0.5:
			50.0f, 50.0f, 50.0f, 50.0f, 50.0f, 50.0f,
			// unit=0.25:
			50.0f, 50.0f, 50.0f, 50.0f, 50.0f, 50.0f,
			// unit=144:
			144.0f, 144.0f, 144.0f, 144.0f, 144.0f, 144.0f,
		};
		static const float expMinor[] = {
			// unit=72:
			9.0f, 7.2f, 9.0f, 6.0f, 10.2857f, 72.0f,
			// unit=36:
			9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 36.0f,
			// unit=28.3464:
			14.1732f, 14.1732f, 14.1732f, 14.1732f, 14.1732f, 28.3464f,
			// unit=14.1732:
			14.1732f, 14.1732f, 14.1732f, 14.1732f, 14.1732f, 14.1732f,
			// unit=2.83464:
			11.3386f, 11.3386f, 11.3386f, 11.3386f, 11.3386f, 11.3386f,
			// unit=12:
			12.0f, 12.0f, 12.0f, 12.0f, 12.0f, 12.0f,
			// unit=6:
			12.0f, 12.0f, 12.0f, 12.0f, 12.0f, 12.0f,
			// unit=1:
			10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f,
			// unit=0.5:
			10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f,
			// unit=0.25:
			10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f,
			// unit=144:
			18.0f, 14.4f, 9.0f, 12.0f, 20.5714f, 144.0f,
		};
		const int nUnits = 11;
		const int nDivs = 6;
		char buf[256];

		for (int i = 0; i < nUnits; i++) {
			for (int j = 0; j < nDivs; j++) {
				int idx = i * nDivs + j;
				RulerTicks r = CalcRulerTicks(unitPixels[i], sheetDiv[j]);

				snprintf(buf, sizeof buf,
					"unit=%.4f div=%d: count==%d",
					unitPixels[i], sheetDiv[j], expCount[idx]);
				Check(r.count == expCount[idx], buf);

				snprintf(buf, sizeof buf,
					"unit=%.4f div=%d: div==%d",
					unitPixels[i], sheetDiv[j], expDiv[idx]);
				Check(r.divisions == expDiv[idx], buf);

				snprintf(buf, sizeof buf,
					"unit=%.4f div=%d: major==%.4f",
					unitPixels[i], sheetDiv[j], expMajor[idx]);
				CheckF(r.major, expMajor[idx], buf);

				snprintf(buf, sizeof buf,
					"unit=%.4f div=%d: minor==%.4f",
					unitPixels[i], sheetDiv[j], expMinor[idx]);
				CheckF(r.minor, expMinor[idx], buf);
			}
		}
	}

	// ===== 3. RulerHashHeight =====
	// Expected: 66 checks
	{
		// divisions=2, indices 0..1
		CheckF(RulerHashHeight(2, 0), 0.0f, "hash div=2 idx=0 -> 0");
		CheckF(RulerHashHeight(2, 1), 0.55f, "hash div=2 idx=1 -> 0.55");

		// divisions=3, indices 0..2
		CheckF(RulerHashHeight(3, 0), 0.0f, "hash div=3 idx=0 -> 0");
		CheckF(RulerHashHeight(3, 1), 0.75f, "hash div=3 idx=1 -> 0.75");
		CheckF(RulerHashHeight(3, 2), 0.75f, "hash div=3 idx=2 -> 0.75");

		// divisions=4, indices 0..3
		CheckF(RulerHashHeight(4, 0), 0.0f, "hash div=4 idx=0 -> 0");
		CheckF(RulerHashHeight(4, 1), 0.75f, "hash div=4 idx=1 -> 0.75");
		CheckF(RulerHashHeight(4, 2), 0.55f, "hash div=4 idx=2 -> 0.55");
		CheckF(RulerHashHeight(4, 3), 0.75f, "hash div=4 idx=3 -> 0.75");

		// divisions=5, indices 0..4
		CheckF(RulerHashHeight(5, 0), 0.0f, "hash div=5 idx=0 -> 0");
		CheckF(RulerHashHeight(5, 1), 0.75f, "hash div=5 idx=1 -> 0.75");
		CheckF(RulerHashHeight(5, 2), 0.75f, "hash div=5 idx=2 -> 0.75");
		CheckF(RulerHashHeight(5, 3), 0.75f, "hash div=5 idx=3 -> 0.75");
		CheckF(RulerHashHeight(5, 4), 0.75f, "hash div=5 idx=4 -> 0.75");

		// divisions=8, indices 0..7
		CheckF(RulerHashHeight(8, 0), 0.0f, "hash div=8 idx=0 -> 0");
		CheckF(RulerHashHeight(8, 1), 0.85f, "hash div=8 idx=1 -> 0.85");
		CheckF(RulerHashHeight(8, 2), 0.75f, "hash div=8 idx=2 -> 0.75");
		CheckF(RulerHashHeight(8, 3), 0.85f, "hash div=8 idx=3 -> 0.85");
		CheckF(RulerHashHeight(8, 4), 0.55f, "hash div=8 idx=4 -> 0.55");
		CheckF(RulerHashHeight(8, 5), 0.85f, "hash div=8 idx=5 -> 0.85");
		CheckF(RulerHashHeight(8, 6), 0.75f, "hash div=8 idx=6 -> 0.75");
		CheckF(RulerHashHeight(8, 7), 0.85f, "hash div=8 idx=7 -> 0.85");

		// divisions=10, indices 0..9
		CheckF(RulerHashHeight(10, 0), 0.0f, "hash div=10 idx=0 -> 0");
		CheckF(RulerHashHeight(10, 1), 0.85f, "hash div=10 idx=1 -> 0.85");
		CheckF(RulerHashHeight(10, 2), 0.85f, "hash div=10 idx=2 -> 0.85");
		CheckF(RulerHashHeight(10, 3), 0.85f, "hash div=10 idx=3 -> 0.85");
		CheckF(RulerHashHeight(10, 4), 0.85f, "hash div=10 idx=4 -> 0.85");
		CheckF(RulerHashHeight(10, 5), 0.55f, "hash div=10 idx=5 -> 0.55");
		CheckF(RulerHashHeight(10, 6), 0.85f, "hash div=10 idx=6 -> 0.85");
		CheckF(RulerHashHeight(10, 7), 0.85f, "hash div=10 idx=7 -> 0.85");
		CheckF(RulerHashHeight(10, 8), 0.85f, "hash div=10 idx=8 -> 0.85");
		CheckF(RulerHashHeight(10, 9), 0.85f, "hash div=10 idx=9 -> 0.85");

		// divisions=7, indices 0..6
		CheckF(RulerHashHeight(7, 0), 0.0f, "hash div=7 idx=0 -> 0");
		CheckF(RulerHashHeight(7, 1), 0.85f, "hash div=7 idx=1 -> 0.85");
		CheckF(RulerHashHeight(7, 2), 0.85f, "hash div=7 idx=2 -> 0.85");
		CheckF(RulerHashHeight(7, 3), 0.85f, "hash div=7 idx=3 -> 0.85");
		CheckF(RulerHashHeight(7, 4), 0.85f, "hash div=7 idx=4 -> 0.85");
		CheckF(RulerHashHeight(7, 5), 0.85f, "hash div=7 idx=5 -> 0.85");
		CheckF(RulerHashHeight(7, 6), 0.85f, "hash div=7 idx=6 -> 0.85");

		// divisions=16, indices 0..15
		CheckF(RulerHashHeight(16, 0), 0.0f, "hash div=16 idx=0 -> 0");
		CheckF(RulerHashHeight(16, 1), 0.85f, "hash div=16 idx=1 -> 0.85");
		CheckF(RulerHashHeight(16, 2), 0.85f, "hash div=16 idx=2 -> 0.85");
		CheckF(RulerHashHeight(16, 3), 0.85f, "hash div=16 idx=3 -> 0.85");
		CheckF(RulerHashHeight(16, 4), 0.85f, "hash div=16 idx=4 -> 0.85");
		CheckF(RulerHashHeight(16, 5), 0.85f, "hash div=16 idx=5 -> 0.85");
		CheckF(RulerHashHeight(16, 6), 0.85f, "hash div=16 idx=6 -> 0.85");
		CheckF(RulerHashHeight(16, 7), 0.85f, "hash div=16 idx=7 -> 0.85");
		CheckF(RulerHashHeight(16, 8), 0.55f, "hash div=16 idx=8 -> 0.55");
		CheckF(RulerHashHeight(16, 9), 0.85f, "hash div=16 idx=9 -> 0.85");
		CheckF(RulerHashHeight(16, 10), 0.85f, "hash div=16 idx=10 -> 0.85");
		CheckF(RulerHashHeight(16, 11), 0.85f, "hash div=16 idx=11 -> 0.85");
		CheckF(RulerHashHeight(16, 12), 0.85f, "hash div=16 idx=12 -> 0.85");
		CheckF(RulerHashHeight(16, 13), 0.85f, "hash div=16 idx=13 -> 0.85");
		CheckF(RulerHashHeight(16, 14), 0.85f, "hash div=16 idx=14 -> 0.85");
		CheckF(RulerHashHeight(16, 15), 0.85f, "hash div=16 idx=15 -> 0.85");

		// Edge case: index 9 with divisions 8 (expect -1)
		{
			float h = RulerHashHeight(8, 9);
			Check(h < -0.5f, "hash div=8 idx=9 -> -1");
		}
	}

	printf("ruler ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 || sOk == 0;
}
