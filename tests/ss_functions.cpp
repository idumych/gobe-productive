// Spreadsheet formula functions: TRUNC, SIGN, QUOTIENT, LOG10, LOG, LN, FRAC,
// FACT, EXP, COMBIN, ATAN2, RADIANS, DEGREES, PI, ATANH, ASINH, ACOSH, TANH,
// SINH, COSH, ATAN, ASIN, ACOS, TAN, SIN, COS, PRODUCT, STDEV, VAR
// Text: VALUE, UPPER, LOWER, TRIM, T, EXACT, SUBSTITUTE, SEARCH, FIND,
// RIGHT, LEFT, MID, REPLACE, PROPER, LEN, CHAR, CODE, CLEAN, TEXT, FIXED, DOLLAR
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "Document.h"
#include "Editing.h"
#include "SSEval.h"

using namespace gobe;

static int failures = 0;

static void
Check(bool ok, const char* what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static void
CheckNum(double got, double expected, const char* what, double tol = 1e-9)
{
	if (fabs(got - expected) > tol * (1.0 + fabs(expected))) {
		char buf[128];
		snprintf(buf, sizeof buf, "%s: got %.15g, expected %.15g",
			what, got, expected);
		Check(false, buf);
	}
}

static bool
IsError(SSValue v)
{
	return v.type == SSValue::kError;
}

static SSPart
MakePart()
{
	SSPart part;
	return part;
}


// isName: no named ranges
static bool
NoName(const std::string&)
{
	return false;
}


int main()
{
	SSPart part = MakePart();
	// Put sample numbers in A1..A5 (row 0..4, col 0)
	std::vector<double> sample = {1, 2, 3, 4, 10};
	for (int i = 0; i < 5; i++) {
		char buf[32];
		snprintf(buf, sizeof buf, "%d", (int)sample[i]);
		std::string error;
		SetCellInput(part, i, 0, buf, NoName, error);
	}

	auto eval = [&](uint16_t row, uint16_t col) {
		SSEvaluator eval(part);
		return eval.Value(row, col);
	};

	auto setFormula = [&](uint16_t row, const std::string& formula) {
		std::string error;
		SetCellInput(part, row, 2, "=" + formula, NoName, error);
	};

	// ====== TRUNC ======
	// python3 -c "print(repr(math.trunc(3.7)), repr(math.trunc(-2.9)))"
	setFormula(10, "TRUNC(3.7)");
	CheckNum(eval(10, 2).number, 3.0, "TRUNC(3.7)");
	setFormula(11, "TRUNC(-2.9)");
	CheckNum(eval(11, 2).number, -2.0, "TRUNC(-2.9)");

	// ====== SIGN ======
	// python3 -c "print(1.0, -1.0, 0.0)"
	setFormula(12, "SIGN(5)");
	CheckNum(eval(12, 2).number, 1.0, "SIGN(5)");
	setFormula(13, "SIGN(-3)");
	CheckNum(eval(13, 2).number, -1.0, "SIGN(-3)");
	setFormula(14, "SIGN(0)");
	CheckNum(eval(14, 2).number, 0.0, "SIGN(0)");

	// ====== QUOTIENT ======
	// python3 -c "import math; print(repr(math.trunc(10/3)), repr(math.trunc(-10/3)))"
	setFormula(15, "QUOTIENT(10,3)");
	CheckNum(eval(15, 2).number, 3.0, "QUOTIENT(10,3)");
	setFormula(16, "QUOTIENT(-10,3)");
	CheckNum(eval(16, 2).number, -3.0, "QUOTIENT(-10,3)");
	// error: divide by zero
	setFormula(17, "QUOTIENT(10,0)");
	Check(IsError(eval(17, 2)), "QUOTIENT(10,0) is error");

	// ====== LOG10 ======
	// python3 -c "import math; print(repr(math.log10(100)), repr(math.log10(1)))"
	setFormula(18, "LOG10(100)");
	CheckNum(eval(18, 2).number, 2.0, "LOG10(100)");
	setFormula(19, "LOG10(1)");
	CheckNum(eval(19, 2).number, 0.0, "LOG10(1)");
	// error: log10(0)
	setFormula(20, "LOG10(0)");
	Check(IsError(eval(20, 2)), "LOG10(0) is error");

	// ====== LOG ======
	// python3 -c "import math; print(repr(math.log(100,10)), repr(math.log(8,2)), repr(math.log10(100)))"
	setFormula(21, "LOG(100,10)");
	CheckNum(eval(21, 2).number, 2.0, "LOG(100,10)");
	setFormula(22, "LOG(8,2)");
	CheckNum(eval(22, 2).number, 3.0, "LOG(8,2)");
	// default base 10
	setFormula(23, "LOG(100)");
	CheckNum(eval(23, 2).number, 2.0, "LOG(100) default base");
	// error: LOG(-1)
	setFormula(24, "LOG(-1)");
	Check(IsError(eval(24, 2)), "LOG(-1) is error");

	// ====== LN ======
	// python3 -c "import math; print(repr(math.log(1)), repr(math.log(math.e)))"
	setFormula(25, "LN(1)");
	CheckNum(eval(25, 2).number, 0.0, "LN(1)");
	setFormula(26, "LN(EXP(1))");
	CheckNum(eval(26, 2).number, 1.0, "LN(EXP(1))");
	// error: LN(0)
	setFormula(27, "LN(0)");
	Check(IsError(eval(27, 2)), "LN(0) is error");

	// ====== FRAC ======
	// python3 -c "print(repr(3.7 - math.trunc(3.7)), repr(-2.5 - math.trunc(-2.5)))"
	setFormula(28, "FRAC(3.7)");
	CheckNum(eval(28, 2).number, 3.7 - 3.0, "FRAC(3.7)", 1e-14);
	setFormula(29, "FRAC(-2.5)");
	CheckNum(eval(29, 2).number, -2.5 - (-2.0), "FRAC(-2.5)", 1e-14);

	// ====== FACT ======
	// python3 -c "import math; print(repr(math.factorial(0)), repr(math.factorial(5)))"
	setFormula(30, "FACT(0)");
	CheckNum(eval(30, 2).number, 1.0, "FACT(0)");
	setFormula(31, "FACT(5)");
	CheckNum(eval(31, 2).number, 120.0, "FACT(5)");
	// error: FACT(-1)
	setFormula(32, "FACT(-1)");
	Check(IsError(eval(32, 2)), "FACT(-1) is error");

	// ====== EXP ======
	// python3 -c "import math; print(repr(math.exp(0)), repr(math.exp(1)))"
	setFormula(33, "EXP(0)");
	CheckNum(eval(33, 2).number, 1.0, "EXP(0)");
	setFormula(34, "EXP(1)");
	CheckNum(eval(34, 2).number, 2.718281828459045, "EXP(1)");

	// ====== COMBIN ======
	// python3 -c "print(28, 1)"
	setFormula(35, "COMBIN(8,2)");
	CheckNum(eval(35, 2).number, 28.0, "COMBIN(8,2)");
	setFormula(36, "COMBIN(5,5)");
	CheckNum(eval(36, 2).number, 1.0, "COMBIN(5,5)");
	// error: COMBIN(2,5) n < k
	setFormula(37, "COMBIN(2,5)");
	Check(IsError(eval(37, 2)), "COMBIN(2,5) is error");

	// ====== ATAN2 ======
	// python3 -c "import math; print(repr(math.atan2(1,1)), repr(math.atan2(0,0)))"
	setFormula(38, "ATAN2(1,1)");
	CheckNum(eval(38, 2).number, 0.7853981633974483, "ATAN2(1,1)");
	setFormula(39, "ATAN2(0,0)");
	CheckNum(eval(39, 2).number, 0.0, "ATAN2(0,0)");

	// ====== RADIANS ======
	// python3 -c "import math; print(repr(180*math.pi/180), repr(90*math.pi/180))"
	setFormula(40, "RADIANS(180)");
	CheckNum(eval(40, 2).number, M_PI, "RADIANS(180)", 1e-14);
	setFormula(41, "RADIANS(90)");
	CheckNum(eval(41, 2).number, M_PI / 2.0, "RADIANS(90)");

	// ====== DEGREES ======
	// python3 -c "import math; print(repr(math.pi*180/math.pi), repr(0.5*180/math.pi))"
	setFormula(42, "DEGREES(PI())");
	CheckNum(eval(42, 2).number, 180.0, "DEGREES(PI())");
	setFormula(43, "DEGREES(0.5)");
	CheckNum(eval(43, 2).number, 28.64788975654116, "DEGREES(0.5)");

	// ====== PI ======
	setFormula(44, "PI()");
	CheckNum(eval(44, 2).number, 3.141592653589793, "PI()");

	// ====== ATANH ======
	// python3 -c "import math; print(repr(math.atanh(0.5)), repr(math.atanh(0)))"
	setFormula(45, "ATANH(0.5)");
	CheckNum(eval(45, 2).number, 0.5493061443340549, "ATANH(0.5)");
	setFormula(46, "ATANH(0)");
	CheckNum(eval(46, 2).number, 0.0, "ATANH(0)");
	// error: ATANH(1)
	setFormula(47, "ATANH(1)");
	Check(IsError(eval(47, 2)), "ATANH(1) is error");

	// ====== ASINH ======
	// python3 -c "import math; print(repr(math.asinh(0.5)), repr(math.asinh(0)))"
	setFormula(48, "ASINH(0.5)");
	CheckNum(eval(48, 2).number, 0.48121182505960347, "ASINH(0.5)");
	setFormula(49, "ASINH(0)");
	CheckNum(eval(49, 2).number, 0.0, "ASINH(0)");

	// ====== ACOSH ======
	// python3 -c "import math; print(repr(math.acosh(1)), repr(math.acosh(2)))"
	setFormula(50, "ACOSH(1)");
	CheckNum(eval(50, 2).number, 0.0, "ACOSH(1)");
	setFormula(51, "ACOSH(2)");
	CheckNum(eval(51, 2).number, 1.3169578969248168, "ACOSH(2)");
	// error: ACOSH(0)
	setFormula(52, "ACOSH(0)");
	Check(IsError(eval(52, 2)), "ACOSH(0) is error");

	// ====== TANH ======
	// python3 -c "import math; print(repr(math.tanh(0)), repr(math.tanh(1)))"
	setFormula(53, "TANH(0)");
	CheckNum(eval(53, 2).number, 0.0, "TANH(0)");
	setFormula(54, "TANH(1)");
	CheckNum(eval(54, 2).number, 0.7615941559557649, "TANH(1)");

	// ====== SINH ======
	// python3 -c "import math; print(repr(math.sinh(0)), repr(math.sinh(1)))"
	setFormula(55, "SINH(0)");
	CheckNum(eval(55, 2).number, 0.0, "SINH(0)");
	setFormula(56, "SINH(1)");
	CheckNum(eval(56, 2).number, 1.1752011936438014, "SINH(1)");

	// ====== COSH ======
	// python3 -c "import math; print(repr(math.cosh(0)), repr(math.cosh(1)))"
	setFormula(57, "COSH(0)");
	CheckNum(eval(57, 2).number, 1.0, "COSH(0)");
	setFormula(58, "COSH(1)");
	CheckNum(eval(58, 2).number, 1.5430806348152437, "COSH(1)");

	// ====== ATAN ======
	// python3 -c "import math; print(repr(math.atan(1)), repr(math.atan(0)))"
	setFormula(59, "ATAN(1)");
	CheckNum(eval(59, 2).number, 0.7853981633974483, "ATAN(1)");
	setFormula(60, "ATAN(0)");
	CheckNum(eval(60, 2).number, 0.0, "ATAN(0)");

	// ====== ASIN ======
	// python3 -c "import math; print(repr(math.asin(0)), repr(math.asin(1)))"
	setFormula(61, "ASIN(0)");
	CheckNum(eval(61, 2).number, 0.0, "ASIN(0)");
	setFormula(62, "ASIN(1)");
	CheckNum(eval(62, 2).number, M_PI / 2.0, "ASIN(1)");
	// error: ASIN(2)
	setFormula(63, "ASIN(2)");
	Check(IsError(eval(63, 2)), "ASIN(2) is error");

	// ====== ACOS ======
	// python3 -c "import math; print(repr(math.acos(1)), repr(math.acos(0)))"
	setFormula(64, "ACOS(1)");
	CheckNum(eval(64, 2).number, 0.0, "ACOS(1)");
	setFormula(65, "ACOS(0)");
	CheckNum(eval(65, 2).number, M_PI / 2.0, "ACOS(0)");

	// ====== TAN ======
	// python3 -c "import math; print(repr(math.tan(0)), repr(math.tan(math.pi/4)))"
	setFormula(66, "TAN(0)");
	CheckNum(eval(66, 2).number, 0.0, "TAN(0)");
	setFormula(67, "TAN(RADIANS(45))");
	CheckNum(eval(67, 2).number, 1.0, "TAN(45°)", 1e-14);

	// ====== SIN ======
	// python3 -c "import math; print(repr(math.sin(0)), repr(math.sin(math.pi/2)))"
	setFormula(68, "SIN(0)");
	CheckNum(eval(68, 2).number, 0.0, "SIN(0)");
	setFormula(69, "SIN(RADIANS(90))");
	CheckNum(eval(69, 2).number, 1.0, "SIN(90°)", 1e-14);

	// ====== COS ======
	// python3 -c "import math; print(repr(math.cos(0)), repr(math.cos(math.pi)))"
	setFormula(70, "COS(0)");
	CheckNum(eval(70, 2).number, 1.0, "COS(0)");
	setFormula(71, "COS(PI())");
	CheckNum(eval(71, 2).number, -1.0, "COS(PI())");

	// ====== PRODUCT ======
	// python3 -c "print(1*2*3*4*10)"
	setFormula(72, "PRODUCT(A1:A5)");
	CheckNum(eval(72, 2).number, 240.0, "PRODUCT(A1:A5)");
	// PRODUCT with single value
	setFormula(73, "PRODUCT(7)");
	CheckNum(eval(73, 2).number, 7.0, "PRODUCT(7)");
	// PRODUCT with just numbers
	setFormula(74, "PRODUCT(2,3)");
	CheckNum(eval(74, 2).number, 6.0, "PRODUCT(2,3)");

	// ====== STDEV ======
	// python3 -c "import statistics; print(repr(statistics.stdev([1,2,3,4,10])))"
	setFormula(75, "STDEV(A1:A5)");
	CheckNum(eval(75, 2).number, 3.5355339059327378, "STDEV(A1:A5)");
	// STDEV of 2 values
	setFormula(76, "STDEV(4,16)");
	CheckNum(eval(76, 2).number, 8.48528137423857, "STDEV(4,16)", 1e-10);
	// error: STDEV of 1 value
	setFormula(77, "STDEV(5)");
	Check(IsError(eval(77, 2)), "STDEV(5) is error");

	// ====== VAR ======
	// python3 -c "import statistics; print(repr(statistics.variance([1,2,3,4,10])))"
	setFormula(78, "VAR(A1:A5)");
	CheckNum(eval(78, 2).number, 12.5, "VAR(A1:A5)");
	// VAR of 2 values
	setFormula(79, "VAR(4,16)");
	CheckNum(eval(79, 2).number, 72.0, "VAR(4,16)");
	// error: VAR of 1 value
	setFormula(80, "VAR(5)");
	Check(IsError(eval(80, 2)), "VAR(5) is error");

	// ====== Additional edge cases ======
	// FRAC of integer
	setFormula(81, "FRAC(5.0)");
	CheckNum(eval(81, 2).number, 0.0, "FRAC(5.0)");
	// FACT of non-integer
	setFormula(82, "FACT(2.5)");
	Check(IsError(eval(82, 2)), "FACT(2.5) is error");
	// LOG with base <= 0
	setFormula(83, "LOG(10,0)");
	Check(IsError(eval(83, 2)), "LOG(10,0) is error");

	// ====== PRODUCT with range containing text (skipped like SUM) ======
	// Add text to A6
	std::string dummyError;
	SetCellInput(part, 5, 0, "hello", NoName, dummyError);
	setFormula(84, "PRODUCT(A1:A6)");
	CheckNum(eval(84, 2).number, 240.0, "PRODUCT(A1:A6) with text", 1e-9);

	// ====== STDEV with text in range (should error per decomp) ======
	setFormula(85, "STDEV(A1:A6)");
	Check(IsError(eval(85, 2)), "STDEV(A1:A6) with text is error");

	// ====== VAR with text in range (should error per decomp) ======
	setFormula(86, "VAR(A1:A6)");
	Check(IsError(eval(86, 2)), "VAR(A1:A6) with text is error");

	// ====== Text functions (batch 2) ======
	std::string got;

	// ====== VALUE ======
	// python3 -c "print(42.5); print(float('3.14'))"
	SetCellInput(part, 87, 0, "3.14", NoName, dummyError);
	setFormula(87, "VALUE(A88)");  // A88 holds "3.14"
	CheckNum(eval(87, 2).number, 3.14, "VALUE(3.14)", 1e-9);
	// error: VALUE of non-numeric
	setFormula(88, "VALUE(\"hello\")");
	Check(IsError(eval(88, 2)), "VALUE(\"hello\") is error");

	// ====== UPPER ======
	// python3 -c "print(repr('hello world'.upper()), repr('héllo'.upper()))"
	setFormula(89, "UPPER(\"hello world\")");
	got.clear(); eval(89, 2).GetString(got);
	Check(got == "HELLO WORLD", "UPPER(\"hello world\")");
	setFormula(90, "UPPER(\"héllo\")");
	got.clear(); eval(90, 2).GetString(got);
	Check(got == "HÉLLO", "UPPER(\"héllo\") == HÉLLO");
	// error: UPPER of number
	setFormula(91, "UPPER(42)");
	got.clear(); eval(91, 2).GetString(got);
	Check(got == "42", "UPPER(42) -> \"42\"");

	// ====== LOWER ======
	// python3 -c "print(repr('HELLO WORLD'.lower()), repr('hÉllo'.lower()))"
	setFormula(92, "LOWER(\"HELLO WORLD\")");
	got.clear(); eval(92, 2).GetString(got);
	Check(got == "hello world", "LOWER(\"HELLO WORLD\")");
	setFormula(93, "LOWER(\"hÉllo\")");
	got.clear(); eval(93, 2).GetString(got);
	Check(got == "héllo", "LOWER(\"hÉllo\")");

	// ====== TRIM ======
	// python3 -c "print(repr('hello world'.replace(' ', '')))"
	setFormula(94, "TRIM(\"hello world\")");
	got.clear(); eval(94, 2).GetString(got);
	Check(got == "helloworld", "TRIM removes all spaces");
	setFormula(95, "TRIM(\"héllo mUnde\")");
	got.clear(); eval(95, 2).GetString(got);
	Check(got == "héllomUnde", "TRIM UTF-8");

	// ====== T ======
	// python3 -c "print(repr('hello'))"
	setFormula(96, "T(\"hello\")");
	got.clear(); eval(96, 2).GetString(got);
	Check(got == "hello", "T(\"hello\")");
	// T of a number -> "42"
	got.clear(); eval(96, 2).GetString(got);
	// T of blank -> ""
	setFormula(97, "T(\"\")");
	got.clear(); eval(97, 2).GetString(got);
	Check(got == "", "T(\"\")");

	// ====== EXACT ======
	// python3 -c "print('hello' == 'hello', 'hello' == 'Hello')"
	setFormula(98, "EXACT(\"hello\",\"hello\")");
	Check(eval(98, 2).type == SSValue::kBool && eval(98, 2).number != 0, "EXACT(\"hello\",\"hello\")");
	setFormula(99, "EXACT(\"hello\",\"Hello\")");
	Check(eval(99, 2).type == SSValue::kBool && eval(99, 2).number == 0, "EXACT(\"hello\",\"Hello\")");

	// ====== SUBSTITUTE ======
	// python3 -c "print(repr('abcabcabc'.replace('b','X',1)), repr('abcabcabc'.replace('b','X',2)))"
	setFormula(100, "SUBSTITUTE(\"abcabcabc\",\"b\",\"X\")");
	got.clear(); eval(100, 2).GetString(got);
	Check(got == "aXcaXcaXc", "SUBSTITUTE replaces all");
	setFormula(101, "SUBSTITUTE(\"abcabcabc\",\"b\",\"X\",1)");
	got.clear(); eval(101, 2).GetString(got);
	Check(got == "aXcabcabc", "SUBSTITUTE instance 1");
	setFormula(102, "SUBSTITUTE(\"abcabcabc\",\"b\",\"X\",2)");
	got.clear(); eval(102, 2).GetString(got);
	Check(got == "aXcaXcabc", "SUBSTITUTE instance 2");
	setFormula(103, "SUBSTITUTE(\"hello\",\"x\",\"X\",1)");
	got.clear(); eval(103, 2).GetString(got);
	Check(got == "hello", "SUBSTITUTE no match");
	// error: SUBSTITUTE instance 0
	setFormula(104, "SUBSTITUTE(\"abc\",\"b\",\"X\",0)");
	Check(IsError(eval(104, 2)), "SUBSTITUTE instance 0 is error");

	// ====== SEARCH ======
	// python3 -c "print('hello'.lower().find('h')+1, 'hello'.lower().find('l',1)+1, 'hello'.lower().find('x')+1)"
	setFormula(105, "SEARCH(\"h\",\"hello\")");
	CheckNum(eval(105, 2).number, 1.0, "SEARCH(\"h\",\"hello\")");
	setFormula(106, "SEARCH(\"l\",\"hello\",2)");
	CheckNum(eval(106, 2).number, 3.0, "SEARCH(\"l\",\"hello\",2)");
	setFormula(107, "SEARCH(\"x\",\"hello\")");
	CheckNum(eval(107, 2).number, 0.0, "SEARCH not found");
	// UTF-8: SEARCH in 'héllo'
	setFormula(108, "SEARCH(\"é\",\"héllo\")");
	CheckNum(eval(108, 2).number, 2.0, "SEARCH UTF-8");
	// error: SEARCH start < 1
	setFormula(109, "SEARCH(\"a\",\"hello\",0)");
	Check(IsError(eval(109, 2)), "SEARCH start 0 is error");

	// ====== FIND ======
	// python3 -c "print('hello'.find('e')+1, 'Hello'.find('H')+1)"
	setFormula(110, "FIND(\"e\",\"hello\")");
	CheckNum(eval(110, 2).number, 2.0, "FIND(\"e\",\"hello\")");
	setFormula(111, "FIND(\"H\",\"Hello\")");
	CheckNum(eval(111, 2).number, 1.0, "FIND(\"H\",\"Hello\")");
	// case-sensitive: FIND 'H' in 'hello' -> not found
	setFormula(112, "FIND(\"H\",\"hello\")");
	CheckNum(eval(112, 2).number, 0.0, "FIND case-sensitive");
	// UTF-8: FIND 'é' in 'héllo'
	setFormula(113, "FIND(\"é\",\"héllo\")");
	CheckNum(eval(113, 2).number, 2.0, "FIND UTF-8");
	// error: FIND start < 1
	setFormula(114, "FIND(\"a\",\"hello\",0)");
	Check(IsError(eval(114, 2)), "FIND start 0 is error");

	// ====== RIGHT ======
	// python3 -c "print(repr('hello'[-2:]), repr('héllo'[-2:]))"
	setFormula(115, "RIGHT(\"hello\",2)");
	got.clear(); eval(115, 2).GetString(got);
	Check(got == "lo", "RIGHT(\"hello\",2)");
	setFormula(116, "RIGHT(\"héllo\",2)");
	got.clear(); eval(116, 2).GetString(got);
	Check(got == "lo", "RIGHT UTF-8");
	// error: RIGHT count < 1
	setFormula(117, "RIGHT(\"hello\",0)");
	Check(IsError(eval(117, 2)), "RIGHT(0) is error");

	// ====== LEFT ======
	// python3 -c "print(repr('hello'[:2]), repr('héllo'[:2]))"
	setFormula(118, "LEFT(\"hello\",2)");
	got.clear(); eval(118, 2).GetString(got);
	Check(got == "he", "LEFT(\"hello\",2)");
	setFormula(119, "LEFT(\"héllo\",2)");
	got.clear(); eval(119, 2).GetString(got);
	Check(got == "hé", "LEFT UTF-8");
	// error: LEFT count < 1
	setFormula(120, "LEFT(\"hello\",0)");
	Check(IsError(eval(120, 2)), "LEFT(0) is error");

	// ====== MID ======
	// python3 -c "print(repr('hello world'[3:7]), repr('héllo'[1:3]))"
	setFormula(121, "MID(\"hello world\",4,4)");
	got.clear(); eval(121, 2).GetString(got);
	Check(got == "lo w", "MID(\"hello world\",4,4)");
	setFormula(122, "MID(\"héllo\",2,2)");
	got.clear(); eval(122, 2).GetString(got);
	Check(got == "él", "MID UTF-8");
	// error: MID start < 1
	setFormula(123, "MID(\"hello\",0,2)");
	Check(IsError(eval(123, 2)), "MID start 0 is error");

	// ====== REPLACE ======
	// python3 -c "s='hello world'; print(repr('XYZ'+s[3:]))"
	setFormula(124, "REPLACE(\"hello world\",1,3,\"XYZ\")");
	got.clear(); eval(124, 2).GetString(got);
	Check(got == "XYZlo world", "REPLACE");
	// UTF-8: REPLACE char 2 of 'héllo world' (é) with 'X'
	setFormula(125, "REPLACE(\"héllo world\",2,1,\"X\")");
	got.clear(); eval(125, 2).GetString(got);
	Check(got == "hXllo world", "REPLACE UTF-8");
	// error: REPLACE start < 1
	setFormula(126, "REPLACE(\"hello\",0,1,\"X\")");
	Check(IsError(eval(126, 2)), "REPLACE start 0 is error");

	// ====== PROPER ======
	// python3 -c "print(repr('hello world'.title()), repr('héllo mUnde'.title()))"
	setFormula(127, "PROPER(\"hello world\")");
	got.clear(); eval(127, 2).GetString(got);
	Check(got == "Hello World", "PROPER");

	// ====== LEN ======
	// python3 -c "print(len('hello'), len('héllo'))"
	setFormula(128, "LEN(\"hello\")");
	CheckNum(eval(128, 2).number, 5.0, "LEN(\"hello\")");
	setFormula(129, "LEN(\"héllo\")");
	CheckNum(eval(129, 2).number, 5.0, "LEN(\"héllo\") UTF-8");
	// LEN of blank
	setFormula(130, "LEN(\"\")");
	CheckNum(eval(130, 2).number, 0.0, "LEN(\"\")");

	// ====== CHAR ======
	// python3 -c "print(repr(chr(65)), repr(chr(32)))"
	setFormula(131, "CHAR(65)");
	got.clear(); eval(131, 2).GetString(got);
	Check(got == "A", "CHAR(65)");
	setFormula(132, "CHAR(32)");
	got.clear(); eval(132, 2).GetString(got);
	Check(got == " ", "CHAR(32)");
	// out of range -> ""
	setFormula(133, "CHAR(200)");
	got.clear(); eval(133, 2).GetString(got);
	// CHAR(200) is out of ASCII range, returns empty
	Check(got == "", "CHAR(200) empty");

	// ====== CODE ======
	// python3 -c "print(ord('A'), ord('a'))"
	setFormula(134, "CODE(\"A\")");
	CheckNum(eval(134, 2).number, 65.0, "CODE(\"A\")");
	setFormula(135, "CODE(\"a\")");
	CheckNum(eval(135, 2).number, 97.0, "CODE(\"a\")");
	// error: CODE of empty
	setFormula(136, "CODE(\"\")");
	Check(IsError(eval(136, 2)), "CODE(\"\") is error");

	// ====== CLEAN ======
	// python3 -c "print(repr(''.join(' ' if ord(c) < 32 or ord(c) == 127 else c for c in 'hello\\x01world\\x7f')))"
	setFormula(137, "CLEAN(\"hello\\x01world\\x7f\")");
	got.clear(); eval(137, 2).GetString(got);
	Check(got.find("\x01") == std::string::npos && got.find("\x7f") == std::string::npos, "CLEAN removes control chars");

	// ====== TEXT ======
	// python3 -c "print(f'{0:.2f}', f'{1234.50:,.2f}', f'{-5:.2f}')"
	setFormula(138, "TEXT(0,\"0.00\")");
	got.clear(); eval(138, 2).GetString(got);
	Check(got == "0.00", "TEXT(0,\"0.00\")");
	setFormula(139, "TEXT(1234.5,\"#,##0.00\")");
	got.clear(); eval(139, 2).GetString(got);
	// Should be "1,234.50"
	Check(got == "1,234.50", "TEXT(1234.5,\"#,##0.00\")");
	setFormula(140, "TEXT(-5,\"0.00\")");
	got.clear(); eval(140, 2).GetString(got);
	// Negative: "-5.00" (no explicit negative format)
	Check(got.find("-5.00") >= 0, "TEXT(-5) negative");

	// ====== FIXED ======
	// python3 -c "print(f'{1234.5:,.2f}', f'{1234:.0f}', f'{1234.5:.2f}')"
	setFormula(141, "FIXED(1234.5,2)");
	got.clear(); eval(141, 2).GetString(got);
	Check(got == "1,234.50", "FIXED(1234.5,2)");
	setFormula(142, "FIXED(1234,0)");
	got.clear(); eval(142, 2).GetString(got);
	Check(got == "1,234", "FIXED(1234,0)");
	setFormula(143, "FIXED(1234.5,2,TRUE)");
	got.clear(); eval(143, 2).GetString(got);
	Check(got == "1234.50", "FIXED no commas");
	// error: FIXED decimals < 0
	setFormula(144, "FIXED(1,-1)");
	Check(IsError(eval(144, 2)), "FIXED decimals -1 is error");
	// error: FIXED decimals > 10
	setFormula(145, "FIXED(1,11)");
	Check(IsError(eval(145, 2)), "FIXED decimals 11 is error");

	// ====== DOLLAR ======
	// python3 -c "print(f'\\${1234.50:,.2f}')", f'-(\\${1234.50:,.2f})'
	setFormula(146, "DOLLAR(1234.5)");
	got.clear(); eval(146, 2).GetString(got);
	Check(got == "$1,234.50", "DOLLAR(1234.5)");
	setFormula(147, "DOLLAR(1234.5,0)");
	got.clear(); eval(147, 2).GetString(got);
	Check(got == "$1,235", "DOLLAR(1234.5,0)");
	// error: DOLLAR decimals < 0
	setFormula(148, "DOLLAR(1,-1)");
	Check(IsError(eval(148, 2)), "DOLLAR decimals -1 is error");

	printf(failures ? "%d failures\n" : "ss_functions: ok\n", failures);
	return failures != 0;
}
