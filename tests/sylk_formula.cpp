// sylk_formula: FormulaToSylk and FormulaFromSylk (Sylk.h), A1 and R1C1
// references. Each expected string is worked out by hand from the
// comments in Sylk.h (rows and columns from 0 in the calls).
// Arguments: none (sample paths, if given, are ignored).
#include <cstdio>
#include <string>

#include "Sylk.h"

using namespace gobe;

static int sOk = 0, sFailed = 0;

static void
To(const std::string& formula, int row, int column, const std::string& expected)
{
	std::string got = FormulaToSylk(formula, uint16_t(row), uint16_t(column));
	if (got == expected)
		sOk++;
	else {
		sFailed++;
		printf("FAIL to \"%s\" at %d,%d: \"%s\", expected \"%s\"\n", formula.c_str(), row, column,
			got.c_str(), expected.c_str());
	}
}


static void
From(const std::string& formula, int row, int column, const std::string& expected)
{
	std::string got = FormulaFromSylk(formula, uint16_t(row), uint16_t(column));
	if (got == expected)
		sOk++;
	else {
		sFailed++;
		printf("FAIL from \"%s\" at %d,%d: \"%s\", expected \"%s\"\n", formula.c_str(), row, column,
			got.c_str(), expected.c_str());
	}
}


int
main()
{
	// Sylk.h's examples
	To("=A1+$B$2", 1, 1, "R[-1]C[-1]+R2C2");
	To("=SUM(B1..B3)", 3, 1, "SUM(R[-3]C:R[-1]C)");
	From("R[-1]C[-1]+R2C2", 1, 1, "=A1+$B$2");
	// zero offsets: "R", "C" alone
	To("=A1", 0, 0, "RC");
	To("=C5", 4, 2, "RC");
	To("=A2", 0, 0, "R[1]C");			// row 1 from row 0
	To("=A1", 0, 1, "RC[-1]");			// column A from column B
	To("=D7", 2, 1, "R[4]C[2]");		// row 6 - 2, column 3 - 1
	// mixed
	To("=$A1", 5, 5, "R[-5]C1");		// row 0 - 5, column absolute 1
	To("=A$1", 5, 5, "R1C[-5]");
	To("=$C$10", 0, 0, "R10C3");
	// two letters: AA = 26, AZ = 51, BA = 52
	To("=AA1", 0, 26, "RC");
	To("=AZ1", 0, 0, "RC[51]");
	To("=$BA$3", 9, 9, "R3C53");
	To("=AB2", 1, 0, "RC[27]");
	// ranges, functions, several references
	To("=SUM(A1..C3)*2", 1, 1, "SUM(R[-1]C[-1]:R[1]C[1])*2");
	To("=IF(A1>B1,A1-B1,0)", 0, 2, "IF(RC[-2]>RC[-1],RC[-2]-RC[-1],0)");
	To("A1", 0, 0, "RC");				// no '=' to drop
	// not references
	To("=1.5+1E10", 0, 0, "1.5+1E10");
	To("=LOG10(2)", 0, 0, "LOG10(2)");
	To("=ABC1+A1B+Rate_2+A.B1", 0, 0, "ABC1+A1B+Rate_2+A.B1");
	To("=a1", 0, 0, "a1");				// capitals only
	To("=A0", 0, 0, "A0");				// no row 0
	// quoted text
	To("=\"A1..B2\"&A1", 0, 0, "\"A1..B2\"&RC");

	// back
	From("RC", 2, 2, "=C3");
	From("R[+2]C[-1]", 3, 3, "=C6");	// row 5, column 2
	From("R5C[1]", 0, 0, "=B$5");
	From("R[1]C2", 0, 0, "=$B2");
	From("R2C2", 1, 1, "=$B$2");
	From("R1C27", 0, 0, "=$AA$1");
	From("R1C52", 0, 0, "=$AZ$1");
	From("R1C53", 0, 0, "=$BA$1");
	From("SUM(R[-1]C[-1]:R[1]C[1])", 1, 1, "=SUM(A1..C3)");
	From("ROUND(RC,2)+RATE+RC_X", 0, 0, "=ROUND(A1,2)+RATE+RC_X");
	From("\"R1C1:A2\"&RC", 0, 0, "=\"R1C1:A2\"&A1");
	From("R[-5]C", 2, 0, "=R[-5]C");	// before row 1: left as it is
	From("RC[-5]", 0, 3, "=RC[-5]");	// before column A
	From("R0C1", 0, 0, "=R0C1");		// no row 0

	// round trips
	const char* formulas[] = {"=A1", "=$A$1+B2*$C3-D$4", "=SUM(A1..Z9)/COUNT(B2..B5)",
		"=IF(AA10>0,\"x:y\",AB$2)", "=LOG10(A1)+Rate_2", "=$AZ$100", "=A1..A1"};
	int cells[][2] = {{0, 0}, {5, 3}, {40, 27}, {200, 60}};
	for (const char* f : formulas)
		for (auto& c : cells) {
			std::string back = FormulaFromSylk(FormulaToSylk(f, uint16_t(c[0]), uint16_t(c[1])),
				uint16_t(c[0]), uint16_t(c[1]));
			if (back == f)
				sOk++;
			else {
				sFailed++;
				printf("FAIL round trip \"%s\" at %d,%d: \"%s\"\n", f, c[0], c[1], back.c_str());
			}
		}
	printf("sylk_formula: ok %d, failed %d\n", sOk, sFailed);
	return sFailed != 0 || sOk == 0;
}
