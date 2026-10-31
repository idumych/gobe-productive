// RTF in and out of word processing documents (Gobe RTF translator).
#include "RTF.h"

#include "Document.h"
#include "Editing.h"
#include "PNG.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>

namespace gobe {

namespace {

// TFontBE::setFontFamilyCode: the RTF font family of the BeOS fonts the
// original knows (0 nil, 1 roman, 2 swiss, 3 modern, 4 script, 5 decor,
// 6 tech), and whether the font is its family's default
int
FamilyCode(const std::string& family, bool* isDefault)
{
	*isDefault = false;
	if (family == "Baskerville" || family == "ClassGarmnd BT" || family == "Dutch801 Rm BT") {
		*isDefault = family == "Dutch801 Rm BT";
		return 1;
	}
	if (family == "Swis721 BT" || family == "Humnst777 BT" || family == "Monospac821 BT"
			|| family == "ProFont" || family == "ProFontISOLatin 1"
			|| family == "Swis911 XCm BT" || family == "VAGRounded BT") {
		*isDefault = family == "Swis721 BT";
		return 2;
	}
	if (family == "Courier 10 BT")
		return 3;
	if (family == "CommercialScript" || family == "Embassy") {
		*isDefault = true;
		return 4;
	}
	if (family == "CopprplGoth Bd BT") {
		*isDefault = true;
		return 5;
	}
	if (family == "SymbolProp BT") {
		*isDefault = true;
		return 6;
	}
	return 0;
}


// RTFfamilyNames
const char* const kFamilyNames[8] = {
	"\\fnil ", "\\froman ", "\\fswiss ", "\\fmodern ",
	"\\fscript ", "\\fdecor ", "\\ftech ", "\\fbidi "
};


// a font of the catalog, -1 for be_plain_font
struct FontRef {
	int index = -1;
};


// #pragma mark - reading

enum Kind { kBuffer, kDest, kProp, kFont, kColor, kFlag, kSpecial, kPict, kTab };

struct Symbol {
	const char* name;
	int32_t value;		// used when the keyword has no parameter, or always
	bool force;
	Kind kind;
	int32_t index;
};

// RTFSymbolTable (RTFTranslator, 0x33a00): two entries have empty names
// and never match
const Symbol kSymbols[] = {
	{"b", 1, false, kFlag, 2},
	{"i", 1, false, kFlag, 4},
	{"lang", 0, false, kProp, 3},
	{"uc", 0, false, kProp, 4},
	{"ul", 1, false, kFlag, 8},
	{"uld", 1, false, kFlag, 8},
	{"uldash", 1, false, kFlag, 8},
	{"uldashd", 1, false, kFlag, 8},
	{"uldashdd", 1, false, kFlag, 8},
	{"ulth", 1, false, kFlag, 8},
	{"ulw", 1, false, kFlag, 8},
	{"ulwave", 1, false, kFlag, 8},
	{"uldb", 1, false, kFlag, 16},
	{"ulnone", 0, true, kFlag, 8},
	{"strike", 1, false, kFlag, 32},
	{"sub", 1, false, kFlag, 128},
	{"super", 1, false, kFlag, 64},
	{"plain", 0, false, kProp, 31},
	{"pard", 0, false, kProp, 32},
	{"secd", 0, false, kProp, 33},
	{"li", 0, false, kProp, 5},
	{"ri", 0, false, kProp, 6},
	{"fi", 0, false, kProp, 7},
	{"qc", 1, true, kProp, 8},
	{"ql", 0, true, kProp, 8},
	{"qr", 2, true, kProp, 8},
	{"qj", 3, true, kProp, 8},
	{"sl", 12, false, kProp, 9},
	{"sb", 0, false, kProp, 10},
	{"sa", 0, false, kProp, 11},
	{"fs", 24, false, kProp, 1},
	{"cf", 0, false, kProp, 2},
	{"cols", 1, false, kProp, 12},
	{"sect", 0, false, kDest, 6},
	{"sbknone", 0, true, kProp, 15},
	{"sbkcol", 1, true, kProp, 15},
	{"sbkeven", 2, true, kProp, 15},
	{"sbkodd", 3, true, kProp, 15},
	{"sbkpage", 4, true, kProp, 15},
	{"pgnx", 0, false, kProp, 13},
	{"pgny", 0, false, kProp, 14},
	{"pgndec", 0, true, kProp, 16},
	{"pgnucrm", 1, true, kProp, 16},
	{"pgnlcrm", 2, true, kProp, 16},
	{"pgnucltr", 3, true, kProp, 16},
	{"pgnlcltr", 4, true, kProp, 16},
	{"paperw", 12240, false, kProp, 17},
	{"paperh", 15840, false, kProp, 18},
	{"margl", 1800, false, kProp, 19},
	{"margr", 1800, false, kProp, 20},
	{"margt", 1440, false, kProp, 21},
	{"margb", 1440, false, kProp, 22},
	{"deftab", 720, false, kProp, 24},
	{"deff", 0, false, kProp, 25},
	{"deflang", 0, false, kProp, 26},
	{"deflangfe", 0, false, kProp, 27},
	{"pgnstart", 1, true, kProp, 23},
	{"facingp", 1, true, kProp, 28},
	{"landscape", 1, true, kProp, 29},
	{"widowctrl", 1, true, kProp, 30},
	{"par", 0, false, kBuffer, 10},
	{"row", 0, false, kBuffer, 10},
	{"", 0, false, kBuffer, 10},
	{"", 0, false, kBuffer, 10},
	{"tab", 0, false, kBuffer, 9},
	{"cell", 0, false, kBuffer, 9},
	{"lquote", 0, false, kBuffer, 39},
	{"rquote", 0, false, kBuffer, 39},
	{"ldblquote", 0, false, kBuffer, 34},
	{"rdblquote", 0, false, kBuffer, 34},
	{"bullet", 0, false, kBuffer, 42},
	{"ansicpg", 0, false, kSpecial, 11},
	{"ansi", 0, true, kSpecial, 10},
	{"mac", 1, true, kSpecial, 10},
	{"pc", 2, true, kSpecial, 10},
	{"pca", 3, true, kSpecial, 10},
	{"chdate", 0, false, kSpecial, 4},
	{"chdpl", 0, false, kSpecial, 5},
	{"chdpa", 0, false, kSpecial, 6},
	{"chtime", 0, false, kSpecial, 7},
	{"chpgn", 0, false, kSpecial, 8},
	{"page", 0, false, kSpecial, 9},
	{"bin", 0, false, kSpecial, 0},
	{"*", 0, false, kSpecial, 2},
	{"\'", 0, false, kSpecial, 1},
	{"u", 0, false, kSpecial, 12},
	{"author", 0, false, kDest, 1},
	{"buptim", 0, false, kDest, 1},
	{"comment", 0, false, kDest, 1},
	{"creatim", 0, false, kDest, 1},
	{"doccomm", 0, false, kDest, 1},
	{"colortbl", 0, false, kDest, 3},
	{"red", 0, false, kColor, 0},
	{"green", 0, false, kColor, 1},
	{"blue", 0, false, kColor, 2},
	{"fonttbl", 0, false, kDest, 2},
	{"fnil", 0, true, kFont, 1},
	{"froman", 1, true, kFont, 1},
	{"fswiss", 2, true, kFont, 1},
	{"fmodern", 3, true, kFont, 1},
	{"fscript", 4, true, kFont, 1},
	{"fdecor", 5, true, kFont, 1},
	{"ftech", 6, true, kFont, 1},
	{"fbidi", 7, true, kFont, 1},
	{"fcharset", 0, false, kFont, 3},
	{"f", 0, false, kSpecial, 3},
	{"field", 0, false, kDest, 8},
	{"fldinst", 0, false, kDest, 9},
	{"fldrslt", 0, false, kDest, 10},
	{"footer", 0, false, kDest, 5},
	{"footerf", 0, false, kDest, 1},
	{"footerl", 0, false, kDest, 1},
	{"footerr", 0, false, kDest, 1},
	{"footnote", 0, false, kDest, 1},
	{"ftncn", 0, false, kDest, 1},
	{"ftnsep", 0, false, kDest, 1},
	{"ftnsepc", 0, false, kDest, 1},
	{"header", 0, false, kDest, 4},
	{"headerf", 0, false, kDest, 1},
	{"headerl", 0, false, kDest, 1},
	{"headerr", 0, false, kDest, 1},
	{"info", 0, false, kDest, 1},
	{"keywords", 0, false, kDest, 1},
	{"operator", 0, false, kDest, 1},
	{"pict", 0, false, kDest, 11},
	{"dibitmap", 0, true, kPict, 0},
	{"emfblip", 1, true, kPict, 0},
	{"jpegblip", 2, true, kPict, 0},
	{"macpict", 3, true, kPict, 0},
	{"nonshppict", 4, true, kPict, 0},
	{"pmmetafile", 5, true, kPict, 0},
	{"pngblip", 6, true, kPict, 0},
	{"shppict", 7, true, kPict, 0},
	{"wbitmap", 8, true, kPict, 0},
	{"wmetafile", 9, true, kPict, 0},
	{"pich", 0, false, kPict, 2},
	{"picw", 0, false, kPict, 1},
	{"tx", 0, false, kTab, 1},
	{"tqr", 1, true, kTab, 0},
	{"tqc", 2, true, kTab, 0},
	{"tqdec", 3, true, kTab, 0},
	{"tldot", 1, true, kTab, 2},
	{"tlhyph", 2, true, kTab, 2},
	{"tlul", 3, true, kTab, 2},
	{"tlth", 4, true, kTab, 2},
	{"tleq", 5, true, kTab, 2},
	{"printim", 0, false, kDest, 1},
	{"private1", 0, false, kDest, 1},
	{"revtim", 0, false, kDest, 1},
	{"rxe", 0, false, kDest, 1},
	{"stylesheet", 0, false, kDest, 1},
	{"subject", 0, false, kDest, 1},
	{"tc", 0, false, kDest, 1},
	{"title", 0, false, kDest, 1},
	{"txe", 0, false, kDest, 1},
	{"xe", 0, false, kDest, 1},
	{"shp", 0, false, kDest, 12},
	{"{", 0, false, kBuffer, 123},
	{"}", 0, false, kBuffer, 125},
	{"\\", 0, false, kBuffer, 92},
};


struct CharProps {
	int32_t font = 0, size = 24, color = 0, face = 1, lang = 0, uc = 1;
	bool operator==(const CharProps& o) const
	{
		return font == o.font && size == o.size && color == o.color && face == o.face
			&& lang == o.lang && uc == o.uc;
	}
};

struct RTFTab {
	float position;
	int32_t type, fill;
	uint8_t alignChar, fillChar;
	bool operator==(const RTFTab& o) const
	{
		return position == o.position && type == o.type && fill == o.fill
			&& alignChar == o.alignChar && fillChar == o.fillChar;
	}
};

struct ParaProps {
	int32_t li = 0, ri = 0, fi = 0, align = 0, sl = 240, sb = 0, sa = 0;
	bool SameValues(const ParaProps& o) const
	{
		return li == o.li && ri == o.ri && fi == o.fi && align == o.align && sl == o.sl
			&& sb == o.sb && sa == o.sa;
	}
};

struct SectProps {
	int32_t cols = 1, sbk = 4, pgnx = 1, pgny = 1, pgnFormat = 0;
};

struct DocProps {
	int32_t paperw = 12240, paperh = 15840, margl = 1800, margr = 1800, margt = 1440,
		margb = 1440, pgnstart = 1, deftab = 720, deff = 0, deflang = 0x409,
		deflangfe = 0x409, facingp = 0, landscape = 0, widowctrl = 0;
};

typedef std::shared_ptr<std::vector<RTFTab>> TabTable;	// null: none (\pard)

// a TTranslatorPart_WP being read: text, style runs and special characters
struct ReadPart {
	std::string text;
	struct ParaRun { size_t start, end; ParaProps props; TabTable tabs; };
	struct CharRun { size_t start, end; CharProps props; };
	std::vector<ParaRun> paraRuns;
	std::vector<CharRun> charRuns;
	struct Special { size_t offset; uint32_t kind; };	// "pgbk", "page"
	std::vector<Special> specials;
};

// TRTFFont (TFontTableRTF): a font table entry and the installed font it
// was matched to
struct RTFFont {
	int32_t number = 0, charset = 0, family = 0;
	std::string name;
	FontRef match;
};

struct SavedState {
	CharProps chp;
	ParaProps pap;
	TabTable tabs;
	SectProps sep;
	int rds, fieldState, ris;
	ReadPart* part;
};


class RTFReader {
public:
	RTFReader(const std::string& data, const FontCatalog& fonts);
	int Read();				// TImportRTF::DoTranslate; 0 or an error

	ReadPart main, header, footer;
	bool unmatched = false;
	const FontCatalog& fonts;
	std::vector<RTFFont> fontTable;
	std::vector<Color> colorTable;
	Color currentColor;

	FontRef FontFor(int32_t number) const;

private:
	bool GetChar(char& c);
	void Unget(int count) { fPos -= std::min<size_t>(fPos, size_t(count)); }

	int PushState();
	int PopState();
	int ParseKeyword();
	int TranslateKeyword(const std::string& keyword, bool hasParam, int32_t param);
	int ApplyPropChange(int32_t index, int32_t value);
	int ParseBuffer();
	int ChangeDest(int32_t dest);
	int ParseSpecialKeyword(int32_t symbol, bool hasParam, int32_t param);
	void EndGroupAction(int oldRds);
	int LoadAndHandleBuffer(char c, bool single);
	void LoadHexBuffer(bool chained);
	void ApplyFontTableChange(int32_t what, int32_t value);
	void ApplyColorTableChange(int32_t what, int32_t value);
	void ApplyTabData(int32_t what, int32_t value);
	void AddToMimeBuffer(const std::string& text);
	void FlushMimeBuffer();
	void AddSpecialCharacter(uint32_t kind);
	std::string ConvertBufferToUTF8();
	size_t CurrentLength() const { return fPart->text.size() + fMime.size(); }
	void CheckStyleChanges();

	const std::string& fData;
	size_t fPos = 0;
	int fDepth = 0;
	bool fSkipDestIfUnknown = false;
	int32_t fBinCount = 0, fLastParam = 0;
	int fRds = 0, fFieldState = 7, fRis = 0;
	CharProps fChp, fLastChp;
	ParaProps fPap, fLastPap;
	TabTable fTabs, fLastTabs;
	int32_t fTabType = 0, fTabLeader = 0;
	SectProps fSep;
	DocProps fDop;
	int32_t fCharsetFamily = 0, fCodePage = 0, fConversion = kConversionISO1;
	bool fHeaderDone = false, fFooterDone = false, fPageField = false;
	bool fHeaderCreated = false, fFooterCreated = false;
	bool fInShape = false, fShapePicture = false;
	std::vector<SavedState> fStack;
	std::string fBuffer, fMime;
	ReadPart* fPart;
	RTFFont fCurrentFont;
	bool fHaveCurrentFont = false;
};


RTFReader::RTFReader(const std::string& data, const FontCatalog& catalog)
	:
	fonts(catalog),
	fData(data),
	fPart(&main)
{
	fChp.font = fDop.deff;
	fChp.lang = fDop.deflang;
	fLastChp = fChp;
}


bool
RTFReader::GetChar(char& c)
{
	if (fPos >= fData.size())
		return false;
	c = fData[fPos++];
	return true;
}


FontRef
RTFReader::FontFor(int32_t number) const
{
	// TFontTableRTF::GetBeFontID: the last entry with that number
	for (size_t i = fontTable.size(); i-- > 0;)
		if (fontTable[i].number == number)
			return fontTable[i].match;
	return FontRef();
}


int
RTFReader::Read()
{
	char c;
	while (GetChar(c)) {
		if (fRis == 1) {
			// \bin data: no style check after it
			if (int error = LoadAndHandleBuffer(c, false))
				return error;
			continue;
		}
		int error = 0;
		if (c == '\\') {
			error = ParseKeyword();
		} else if (c == '{') {
			error = PushState();
		} else if (c == '}') {
			if (PopState() != 0)
				unmatched = true;
		} else if (c != '\n' && c != '\r') {
			if (fRds == 0xb) {
				Unget(1);
				LoadHexBuffer(false);
			} else if (fRis != 0) {
				if (fRis != 2)
					return -1;
				fRis = 0;
				Unget(1);
				LoadHexBuffer(true);
			} else
				error = LoadAndHandleBuffer(c, false);
		}
		if (error != 0)
			return error;
		CheckStyleChanges();
	}
	if (fDepth != 0)
		unmatched = true;
	FlushMimeBuffer();
	return 0;
}


// a new paragraph or character style run when the properties changed:
// the text since the last run gets the properties in effect until now
void
RTFReader::CheckStyleChanges()
{
	size_t length = CurrentLength();
	bool paraChanged = !fPap.SameValues(fLastPap);
	if (!paraChanged) {
		if (fTabs == nullptr)
			paraChanged = fLastTabs != nullptr;
		else
			paraChanged = fLastTabs == nullptr || *fTabs != *fLastTabs;
	}
	if (paraChanged) {
		size_t start = fPart->paraRuns.empty() ? 0 : fPart->paraRuns.back().end;
		if (start < length)
			fPart->paraRuns.push_back({start, length, fLastPap, fLastTabs});
		fLastPap = fPap;
		fLastTabs = fTabs == nullptr ? nullptr : std::make_shared<std::vector<RTFTab>>(*fTabs);
	}
	if (!(fChp == fLastChp)) {
		size_t start = fPart->charRuns.empty() ? 0 : fPart->charRuns.back().end;
		if (start < length)
			fPart->charRuns.push_back({start, length, fLastChp});
		fLastChp = fChp;
	}
}


int
RTFReader::PushState()
{
	SavedState s;
	s.chp = fChp;
	s.pap = fPap;
	s.tabs = fTabs == nullptr ? nullptr : std::make_shared<std::vector<RTFTab>>(*fTabs);
	s.sep = fSep;
	s.rds = fRds;
	s.fieldState = fFieldState;
	s.ris = fRis;
	s.part = fPart;
	fStack.push_back(std::move(s));
	fRis = 0;
	fDepth++;
	return 0;
}


int
RTFReader::PopState()
{
	if (fStack.empty())
		return 10;
	SavedState& s = fStack.back();
	if (fRds != s.rds)
		EndGroupAction(fRds);
	fChp = s.chp;
	fPap = s.pap;
	fSep = s.sep;
	fPart = s.part;
	fTabs = s.tabs;
	fRds = s.rds;
	fFieldState = s.fieldState;
	fRis = s.ris;
	fStack.pop_back();
	fDepth--;
	return 0;
}


int
RTFReader::ParseKeyword()
{
	char c;
	if (!GetChar(c))
		return -1;
	std::string keyword;
	bool hasParam = false;
	int32_t param = 0;
	if (!isalpha((unsigned char)c)) {
		keyword = c;
	} else {
		// at the end of the file `c` stays the last character read, and is
		// read again as text unless it's a space (as in the original)
		while (isalpha((unsigned char)c)) {
			keyword += c;
			if (!GetChar(c))
				break;
		}
		bool negative = false;
		if (c == '-') {
			negative = true;
			if (!GetChar(c))
				return -1;
		}
		if (isdigit((unsigned char)c)) {
			hasParam = true;
			std::string digits;
			while (isdigit((unsigned char)c)) {
				digits += c;
				if (!GetChar(c))
					break;
			}
			// the sample reader negates the parameter twice: negative
			// numbers come out positive
			param = int32_t(strtol(digits.c_str(), nullptr, 10));
			if (negative)
				param = -param;
			fLastParam = int32_t(strtol(digits.c_str(), nullptr, 10));
			if (negative)
				param = -param;
		}
		if (c != ' ')
			Unget(1);
	}
	return TranslateKeyword(keyword, hasParam, param);
}


int
RTFReader::TranslateKeyword(const std::string& keyword, bool hasParam, int32_t param)
{
	const size_t count = sizeof(kSymbols) / sizeof(kSymbols[0]);
	size_t i = 0;
	while (i < count && keyword != kSymbols[i].name)
		i++;
	if (i == count) {
		// unknown: a {\*\keyword ...} group is skipped
		if (fSkipDestIfUnknown)
			fRds = 1;
		fSkipDestIfUnknown = false;
		return 0;
	}
	fSkipDestIfUnknown = false;
	const Symbol& s = kSymbols[i];
	if (s.force || !hasParam)
		param = s.value;
	switch (s.kind) {
		case kBuffer:
			return LoadAndHandleBuffer(char(s.index), true);
		case kDest:
			return ChangeDest(s.index);
		case kProp:
			return ApplyPropChange(s.index, param);
		case kFont:
			ApplyFontTableChange(s.index, param);
			return 0;
		case kColor:
			ApplyColorTableChange(s.index, param);
			return 0;
		case kFlag:
			if (fRds != 1) {
				int32_t bit = s.index;
				if (param == 0) {
					fChp.face &= ~bit;
					if (fChp.face == 0)
						fChp.face = 1;
				} else if (bit == 1) {
					fChp.face = 1;
				} else {
					fChp.face |= bit;
					fChp.face &= ~1;
				}
			}
			return 0;
		case kSpecial:
			return ParseSpecialKeyword(int32_t(i), hasParam, param);
		case kPict:
			// picture properties: pictures aren't imported (see RTF.h)
			return 0;
		case kTab:
			ApplyTabData(s.index, param);
			return 0;
	}
	return 0xd;
}


int
RTFReader::ApplyPropChange(int32_t index, int32_t value)
{
	if (fRds == 1)
		return 0;
	switch (index) {
		case 0: fChp.font = value; break;
		case 1: fChp.size = value; break;
		case 2: fChp.color = value; break;
		case 3: fChp.lang = value; break;
		case 4: fChp.uc = value; break;
		case 5: fPap.li = value; break;
		case 6: fPap.ri = value; break;
		case 7: fPap.fi = value; break;
		case 8: fPap.align = value; break;
		case 9: fPap.sl = value; break;
		case 10: fPap.sb = value; break;
		case 11: fPap.sa = value; break;
		case 12: fSep.cols = value; break;
		case 13: fSep.pgnx = value; break;
		case 14: fSep.pgny = value; break;
		case 15: fSep.sbk = int8_t(value); break;		// byte properties
		case 16: fSep.pgnFormat = int8_t(value); break;
		case 17: fDop.paperw = value; break;
		case 18: fDop.paperh = value; break;
		case 19: fDop.margl = value; break;
		case 20: fDop.margr = value; break;
		case 21: fDop.margt = value; break;
		case 22: fDop.margb = value; break;
		case 23: fDop.pgnstart = value; break;
		case 24: fDop.deftab = value; break;
		case 25: fDop.deff = value; break;
		case 26: fDop.deflang = value; break;
		case 27: fDop.deflangfe = value; break;
		case 28: fDop.facingp = int8_t(value); break;
		case 29: fDop.landscape = int8_t(value); break;
		case 30: fDop.widowctrl = int8_t(value); break;
		case 31:	// \plain: setCharPropToDefaults
			fChp = CharProps();
			fChp.font = fDop.deff;
			fChp.lang = fDop.deflang;
			break;
		case 32:	// \pard: setParaPropToDefaults
			fPap = ParaProps();
			fTabs = nullptr;
			fTabType = fTabLeader = 0;
			break;
		case 33:	// \secd
			fSep = SectProps();
			break;
		default:
			return 0xd;
	}
	return 0;
}


int
RTFReader::ParseBuffer()
{
	if (fBuffer.empty())
		return 0;
	if (fFieldState != 7 && fRds != 1) {
		if (fFieldState == 9) {
			// \fldinst: the instruction's first word
			size_t at = 0;
			while (at < fBuffer.size() && fBuffer[at] == ' ')
				at++;
			size_t length = 0;
			while (at + length < fBuffer.size() && fBuffer[at + length] != ' ')
				length++;
			std::string word = fBuffer.substr(at, length);
			if (length == 4 && (word == "PAGE" || word == "page")) {
				fPageField = true;
				AddSpecialCharacter(Sig("page"));
			}
			if (length != 2 || (word != "=(" && word != "if"))
				return 0;
			fRds = 1;
			if (!fStack.empty() && fStack.back().fieldState == 9)
				fStack.back().rds = 1;
			return 0;
		}
		if (fFieldState == 8)
			return 0;
		if (fFieldState == 10 && fPageField)
			return 0;
	}
	switch (fRds) {
		case 0:
		case 4:
		case 5:
		case 6:
			if (fBuffer[0] != '\0') {
				std::string text = ConvertBufferToUTF8();
				if (!text.empty() && text[0] != '\0')
					AddToMimeBuffer(text);
			}
			break;
		case 2:
			ApplyFontTableChange(2, 0);
			break;
		case 3:
			ApplyColorTableChange(3, 0);
			break;
		case 0xb:
			// a picture: dropped (see RTF.h)
			break;
	}
	return 0;
}


int
RTFReader::ChangeDest(int32_t dest)
{
	if (fRds == 1 && dest != 0xb)
		return 0;
	switch (dest) {
		case 2:
		case 3:
		case 6:
			break;
		case 4:
		case 5:
			if ((dest == 4 && fHeaderDone) || (dest == 5 && fFooterDone)) {
				fRds = 1;
				return 0;
			}
			fRds = dest;
			if (dest == 4 && !fHeaderCreated) {
				FlushMimeBuffer();
				fHeaderCreated = true;
				fPart = &header;
			}
			if (dest != 5 || fFooterCreated)
				return 0;
			FlushMimeBuffer();
			fFooterCreated = true;
			fPart = &footer;
			return 0;
		case 8:
			fPageField = false;
			fFieldState = dest;
			return 0;
		case 9:
		case 10:
			fFieldState = dest;
			return 0;
		case 0xb:
			break;
		case 0xc:
			fInShape = true;
			fShapePicture = false;
			break;
		default:
			fRds = 1;
			return 0;
	}
	fRds = dest;
	return 0;
}


int
RTFReader::ParseSpecialKeyword(int32_t symbol, bool hasParam, int32_t param)
{
	int32_t what = kSymbols[symbol].index;
	if (fRds == 1 && what != 0)
		return 0;
	switch (what) {
		case 0:		// \bin
			fRis = 1;
			fBinCount = fLastParam;
			return 0;
		case 1:		// \'
			fRis = 2;
			return 0;
		case 2:		// \*
			fSkipDestIfUnknown = true;
			return 0;
		case 3:		// \f
			if (fRds != 2)
				return ApplyPropChange(0, param);
			ApplyFontTableChange(0, param);
			return 0;
		case 4:		// \chdate, \chdpl, \chdpa, \chtime: AddSpecialCharacter
		case 5:		// adds nothing for these
		case 6:
		case 7:
			return 0;
		case 8:		// \chpgn
			AddSpecialCharacter(Sig("page"));
			return 0;
		case 9:		// \page
			AddSpecialCharacter(Sig("pgbk"));
			return 0;
		case 10:	// \ansi, \mac, \pc, \pca
			fCharsetFamily = param;
			fConversion = param == 1 ? kConversionMacRoman : kConversionISO1;
			return 0;
		case 11:	// \ansicpg
			fCodePage = param;
			return 0;
		case 12: {	// \u
			if (!hasParam)
				return 0;
			uint16_t value = uint16_t(param & 0xffff);
			fBuffer.assign(1, char(value >> 8));
			fBuffer += char(value & 0xff);
			fRis = 3;
			ParseBuffer();
			fRis = 0;
			// skip one replacement character
			char c;
			while (GetChar(c)) {
				if (c == '\r' || c == '\t' || c == '\n' || c == ' ')
					continue;
				if (c == '\\') {
					if (!GetChar(c))
						return 0;
					if (c == '\'') {
						GetChar(c);
						GetChar(c);
						return 0;
					}
					Unget(1);
				}
				return 0;
			}
			return 0;
		}
	}
	return 0xd;
}


void
RTFReader::EndGroupAction(int oldRds)
{
	if (oldRds == 5) {
		FlushMimeBuffer();
		fFooterDone = true;
	} else if (oldRds == 4) {
		FlushMimeBuffer();
		fHeaderDone = true;
	} else if (oldRds == 6) {
		if (fSep.sbk != 0)
			AddSpecialCharacter(Sig("pgbk"));
	} else if (oldRds == 0xc) {
		fInShape = false;
	}
}


int
RTFReader::LoadAndHandleBuffer(char c, bool single)
{
	fBuffer.clear();
	auto keep = [](char ch) {
		return (unsigned char)ch >= 0x80 || (unsigned char)ch > 0x1f || ch == '\t';
	};
	if (fRis == 0) {
		if (keep(c) || c == '\n')
			fBuffer += c;
		if (!single) {
			char next;
			while (GetChar(next)) {
				if (next == '{' || next == '}' || next == '\\') {
					Unget(1);
					break;
				}
				if (keep(next))
					fBuffer += next;
			}
		}
	} else if (fRis == 1) {
		fBuffer += c;
		for (int32_t i = 1; i < fBinCount; i++) {
			char next;
			if (!GetChar(next))
				break;
			fBuffer += next;
		}
	} else
		return -1;
	fRis = 0;
	return ParseBuffer();
}


void
RTFReader::LoadHexBuffer(bool chained)
{
	fBuffer.clear();
	auto hexValue = [](char c, int& v) {
		if (c >= '0' && c <= '9')
			v = c - '0';
		else if (c >= 'a' && c <= 'f')
			v = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			v = c - 'A' + 10;
		else
			return false;
		return true;
	};
	char c;
	int digits = 2;
	uint8_t value = 0;
	while (true) {
		if (!GetChar(c))
			break;
		if (c == '{' || c == '}' || c == '\\') {
			Unget(1);
			break;
		}
		int v;
		if (!hexValue(c, v))
			continue;
		value = uint8_t(value * 16 + v);
		if (--digits != 0)
			continue;
		fBuffer += char(value);
		digits = 2;
		value = 0;
		if (!chained)
			continue;
		// \'xx\'yy...: one buffer
		c = 0;
		bool got;
		while ((got = GetChar(c)) && (unsigned char)c < ' ')
			;
		if (got && c != '\\') {
			Unget(1);
			break;
		}
		if (!got)
			break;
		c = 0;
		while ((got = GetChar(c)) && (unsigned char)c < ' ')
			;
		if (got && c == '\'')
			continue;
		if (got)
			Unget(2);
		break;
	}
	ParseBuffer();
}


void
RTFReader::ApplyFontTableChange(int32_t what, int32_t value)
{
	switch (what) {
		case 0:
			fCurrentFont.number = value;
			fHaveCurrentFont = true;
			break;
		case 1:
			fCurrentFont.family = value;
			fHaveCurrentFont = true;
			break;
		case 3:
			fCurrentFont.charset = value;
			fHaveCurrentFont = true;
			break;
		case 2: {
			// the font's name, up to ';' (the bytes as they are)
			size_t end = fBuffer.find(';');
			bool complete = end != std::string::npos;
			if (!complete)
				end = fBuffer.size();
			if (end > 0) {
				fCurrentFont.name = fBuffer.substr(0, end);
				fHaveCurrentFont = true;
			}
			if (complete && fHaveCurrentFont) {
				// TFontTable::FindBestMatch: the font of that name, else
				// the last one with that character set, else the
				// family's default font
				int32_t family = fCurrentFont.family;
				if (family < 0 || family > 7)
					family = 0;
				FontRef match;
				int byCharset = -1;
				bool found = false;
				for (size_t i = fonts.fonts.size(); i-- > 0;) {
					const FontInfo& f = fonts.fonts[i];
					if (!fCurrentFont.name.empty()
							&& fCurrentFont.name == f.family + " " + f.style) {
						match.index = int(i);
						found = true;
						break;
					}
					if (fCurrentFont.charset != 0 && byCharset < 0
							&& f.charset == fCurrentFont.charset)
						byCharset = int(i);
				}
				if (!found) {
					if (byCharset >= 0)
						match.index = byCharset;
					else {
						// initDefaultFamilies: be_plain_font, or the first
						// listed default font of the family
						for (size_t i = 0; i < fonts.fonts.size(); i++) {
							bool isDefault;
							if (FamilyCode(fonts.fonts[i].family, &isDefault) == family
									&& isDefault && family != 0) {
								match.index = int(i);
								break;
							}
						}
					}
				}
				fCurrentFont.match = match;
				fontTable.push_back(fCurrentFont);
				fCurrentFont = RTFFont();
				fHaveCurrentFont = false;
			}
			break;
		}
	}
}


void
RTFReader::ApplyColorTableChange(int32_t what, int32_t value)
{
	switch (what) {
		case 0: currentColor.r = uint8_t(value); break;
		case 1: currentColor.g = uint8_t(value); break;
		case 2: currentColor.b = uint8_t(value); break;
		case 3:
			// one color per buffer with a ';' in it
			if (fBuffer.find(';') != std::string::npos) {
				colorTable.push_back(currentColor);
				currentColor = Color();
			}
			break;
	}
}


void
RTFReader::ApplyTabData(int32_t what, int32_t value)
{
	if (what == 0) {
		fTabType = value;
	} else if (what == 2) {
		fTabLeader = value;
	} else if (what == 1) {
		// TTabTable::AddRTFTab
		RTFTab tab;
		tab.type = fTabType == 2 ? 1 : fTabType == 1 ? 2 : fTabType == 3 ? 3 : 0;
		tab.alignChar = fTabType == 3 ? '.' : 0;
		tab.fill = fTabLeader == 0 ? 0 : 2;
		tab.fillChar = fTabLeader == 0 ? 0 : fTabLeader == 2 ? '-'
			: fTabLeader == 3 || fTabLeader == 4 ? '_' : fTabLeader == 5 ? '=' : '.';
		tab.position = float(value / 20);
		if (fTabs == nullptr)
			fTabs = std::make_shared<std::vector<RTFTab>>();
		fTabs->push_back(tab);
		fTabType = fTabLeader = 0;
	}
}


void
RTFReader::AddToMimeBuffer(const std::string& text)
{
	if (fMime.size() > 0x400)
		FlushMimeBuffer();
	fMime += text;
}


void
RTFReader::FlushMimeBuffer()
{
	// TTranslatorPart_WP::AddText: control characters but tab, LF, 0x01
	// and FF become '?'
	for (char c : fMime) {
		unsigned char u = (unsigned char)c;
		fPart->text += u > 0x1f || u == 9 || u == 10 || u == 1 || u == 0xc ? c : '?';
	}
	fMime.clear();
}


void
RTFReader::AddSpecialCharacter(uint32_t kind)
{
	FlushMimeBuffer();
	if (kind != Sig("pgbk") && kind != Sig("page"))
		return;
	fPart->specials.push_back({fPart->text.size(), kind});
	fPart->text += '?';
}


std::string
RTFReader::ConvertBufferToUTF8()
{
	int conversion;
	if (fRis == 3)
		conversion = kConversionUnicode;
	else {
		// the current font's \fcharset (TFontTableRTF::GetRTFFontCharSet)
		int32_t charset = 0;
		for (size_t i = fontTable.size(); i-- > 0;)
			if (fontTable[i].number == fChp.font) {
				charset = fontTable[i].charset;
				break;
			}
		conversion = charset == 0x80 || fDop.deflang == 0x411 ? kConversionSJIS : fConversion;
	}
	return ConvertToUTF8(conversion, fBuffer, 0x1a);
}


} // namespace


// #pragma mark - import

bool
ImportRTF(const std::string& path, Document& doc, std::string& error,
	const FontCatalog& fonts, std::string* warning)
{
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		error = "cannot open " + path;
		return false;
	}
	std::string data((std::istreambuf_iterator<char>(file)),
		std::istreambuf_iterator<char>());

	RTFReader reader(data, fonts);
	if (reader.Read() != 0) {
		error = "not readable as RTF";
		return false;
	}
	if (reader.unmatched && warning != nullptr)
		*warning = "This file contains improper RTF (unmatched braces). "
			"Will attempt to open anyway.";

	if (!CreateBlankDocument(doc, error))
		return false;
	if (doc.wordParts.empty()) {
		error = "no word processing part";
		return false;
	}
	// the main text (headers and footers aren't ported yet)
	const ReadPart& in = reader.main;
	size_t part = doc.wordParts[0].first;
	WPPart* wp = doc.EditableWordPart(part);
	SetPartText(*wp, in.text);
	size_t length = in.text.size();

	// TWPImport::HandleStyleRuns, paragraph styles (NewParagraphStyle):
	// indents in points, first line (left - first) / 20, line spacing a
	// multiple (at least 1), spacing before and after in lines
	for (const ReadPart::ParaRun& run : in.paraRuns) {
		size_t end = std::min(run.end, length);
		if (run.start >= end)
			continue;
		size_t first = PositionAt(*wp, run.start).paragraph;
		size_t last = PositionAt(*wp, end - 1).paragraph;
		const ParaProps& p = run.props;
		float spacing = p.sl == 1000 ? 1.0f : std::fabs(float(p.sl) / 240.0f);
		if (spacing < 1.0f)
			spacing = 1.0f;
		const Property properties[] = {
			IndentProperty(Sig("PLef"), float(p.li / 20)),
			IndentProperty(Sig("PRig"), float(p.ri / 20)),
			IndentProperty(Sig("PInd"), float((p.li - p.fi) / 20)),
			AlignmentProperty(p.align),
			LineSpacingProperty(spacing, true),
			SpacingProperty(Sig("SpaB"), float(p.sb / 240)),
			SpacingProperty(Sig("SpaA"), float(p.sa / 240)),
		};
		for (const Property& property : properties)
			ApplyParagraphProperty(doc, part, first, last, property);
		if (run.tabs != nullptr) {
			for (const RTFTab& t : *run.tabs) {
				TabStop tab;
				tab.position = t.position;
				tab.type = t.type;
				tab.fill = t.fill;
				if (t.alignChar != 0)
					tab.alignChar = t.alignChar;
				if (t.fillChar != 0)
					tab.fillChar = t.fillChar;
				ApplyParagraphProperty(doc, part, first, last, TabsProperty({tab}));
			}
		}
	}

	// character styles (NewCharacterStyle): font, size, face, color
	for (const ReadPart::CharRun& run : in.charRuns) {
		size_t end = std::min(run.end, length);
		if (run.start >= end)
			continue;
		TextPosition from = PositionAt(*wp, run.start), to = PositionAt(*wp, end);
		const CharProps& c = run.props;
		FontRef font = reader.FontFor(c.font);
		std::string family = fonts.plainFamily, style = fonts.plainStyle;
		if (font.index >= 0) {
			family = fonts.fonts[font.index].family;
			style = fonts.fonts[font.index].style;
		}
		// TColorTable::GetColor: an index past the table is the color
		// being defined
		Color color = c.color >= 0 && size_t(c.color) < reader.colorTable.size()
			? reader.colorTable[c.color] : reader.currentColor;
		ApplyCharacterProperty(doc, part, from, to, TextFontProperty(family, style));
		ApplyCharacterProperty(doc, part, from, to, TextSizeProperty(float(c.size) * 0.5f));
		ApplyCharacterProperty(doc, part, from, to, TextFaceProperty(uint32_t(c.face)));
		ApplyCharacterProperty(doc, part, from, to, TextColorProperty(color));
	}

	// TWPImport::HandleSpecialCharacter: a page break replaces its '?';
	// a page number would too (not ported: the '?' goes)
	for (size_t i = in.specials.size(); i-- > 0;) {
		const ReadPart::Special& s = in.specials[i];
		if (s.offset >= length)
			continue;
		TextPosition at = PositionAt(*wp, s.offset);
		if (!DeleteText(*wp, at, at.offset + 1))
			continue;
		if (s.kind == Sig("pgbk"))
			InsertSpecialChar(*wp, at, 0x0c);
	}
	return true;
}


// #pragma mark - export

namespace {

// WPTranslateTable (as in TextImport.cpp)
const uint8_t kTranslate[32] = {
	0x20, 0x01, 0x0a, 0x0a, 0x3f, 0x20, 0x20, 0x20,
	0x20, 0x09, 0x0a, 0x0a, 0x0a, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
};


const Property*
Own(const StyleMgr& styles, uint32_t style, uint32_t type)
{
	if (style >= styles.styles.size())
		return nullptr;
	for (uint32_t p : styles.styles[style].props)
		if (p < styles.properties.size() && styles.properties[p].type == type)
			return &styles.properties[p];
	return nullptr;
}


bool
InheritedFont(const StyleMgr& styles, uint32_t style, std::string& family, std::string& face,
	int depth = 0)
{
	if (style >= styles.styles.size() || depth > 32)
		return false;
	if (const Property* p = Own(styles, style, Sig("TFon"))) {
		TextStyle t;
		ApplyProperty(*p, t);
		family = t.family;
		face = t.face;
		return true;
	}
	const Style& s = styles.styles[style];
	return InheritedFont(styles, s.links[1], family, face, depth + 1)
		|| InheritedFont(styles, s.links[0], family, face, depth + 1);
}


// TColorTable::GetColorIndex
int
ColorIndex(const Color& c)
{
	if (c.r < 0x40 && c.g < 0x40 && c.b < 0x40)
		return 0;
	if (c.r > 0xe0 && c.g > 0xe0 && c.b > 0xf4)
		return 8;
	if (c.r > 0xa0 && c.g > 0xa0 && c.b > 0xa0)
		return 16;
	if (c.r > 0x60 && c.g > 0x60 && c.b > 0x60)
		return 15;
	if (c.r > 0xc0 && c.g < 0x40 && c.b < 0x40)
		return 6;
	if (c.r < 0x40) {
		if (c.g > 0xc0 && c.b < 0x40)
			return 4;
		if (c.g < 0x40 && c.b > 0xc0)
			return 2;
		if (c.g > 0xc0 && c.b > 0xc0)
			return 3;
	}
	if (c.r > 0xc0) {
		if (c.g < 0x40 && c.b > 0xc0)
			return 5;
		if (c.g > 0xc0 && c.b < 0x40)
			return 7;
	}
	if (c.r < 0x40) {
		if (c.g < 0x40 && c.b > 0x60)
			return 9;
		if (c.g > 0x60 && c.b > 0x60)
			return 10;
		if (c.g > 0x60 && c.b < 0x40)
			return 11;
	}
	if (c.r > 0x60) {
		if (c.g < 0x40 && c.b > 0x60)
			return 12;
		if (c.g < 0x40 && c.b < 0x40)
			return 13;
		if (c.g > 0x60 && c.b < 0x40)
			return 14;
	}
	return 0;
}


// the TTranslatorPart_WP the original exports: text, runs, special
// characters
struct ExportPart {
	std::string text;
	std::vector<std::pair<size_t, uint32_t>> paraRuns, charRuns;	// start, style
	struct Special {
		size_t offset;
		bool hasText;
		std::string text;
	};
	std::vector<Special> specials;
};


ExportPart
MakeExportPart(const WPPart& wp)
{
	ExportPart out;
	size_t start = 0;
	for (const WPParagraph& p : wp.paragraphs) {
		// TWPExport: a run where the paragraph or character style changes
		if (out.paraRuns.empty() || out.paraRuns.back().second != p.parStyle)
			out.paraRuns.push_back({start, p.parStyle});
		for (const WPStyleRun& r : p.runs)
			if (out.charRuns.empty() || out.charRuns.back().second != r.style)
				out.charRuns.push_back({start + r.position, r.style});
		// break characters (scBR) and paragraph infos, by position
		std::vector<ExportPart::Special> specials;
		for (size_t i = 0; i < p.text.size(); i++) {
			unsigned char c = (unsigned char)p.text[i];
			if (c == 1 || c == 2 || c == 3 || c == 0xc)
				specials.push_back({start + i, false, ""});
		}
		for (const WPMiscInfo& m : p.misc) {
			ExportPart::Special s{start + m.position, false, ""};
			// TMiscInfo::Text: a footnote's mark; a formula's result (not
			// ported: formulas aren't evaluated, so '?' stays); frames are
			// written as pictures (not ported: nothing)
			if (m.kind == Sig("FREF") && !m.text.empty()) {
				s.hasText = true;
				s.text = m.text;
			}
			specials.push_back(s);
		}
		std::stable_sort(specials.begin(), specials.end(),
			[](const ExportPart::Special& a, const ExportPart::Special& b) {
				return a.offset < b.offset;
			});
		out.specials.insert(out.specials.end(), specials.begin(), specials.end());
		for (unsigned char c : p.text)
			out.text += c < 0x20 ? char(kTranslate[c]) : char(c);
		start += p.text.size();
	}
	return out;
}


class RTFWriter {
public:
	RTFWriter(const Document& doc, const WPPart& part, const FontCatalog& fonts)
		: fDoc(doc), fPart(MakeExportPart(part)), fFonts(fonts), fFrames(FramesOf(part))
	{
		fFont = fonts.PlainIndex();
	}

	std::string Write();

private:
	// the embedded frames: their offsets in the text, their frame pointers
	static std::vector<std::pair<size_t, const WPMiscInfo*>> FramesOf(const WPPart& part);
	void Picture(const WPMiscInfo& frame);
	void Header();
	void ParProps(uint32_t style);
	void CharProps(uint32_t style);
	void Text(const std::string& text);
	int FontIndex(const std::string& family, const std::string& style) const;

	const Document& fDoc;
	ExportPart fPart;
	const FontCatalog& fFonts;
	std::vector<std::pair<size_t, const WPMiscInfo*>> fFrames;
	std::string fOut;
	int fFont;						// the font of the last \f (TExportRTF +0x88)
};


std::vector<std::pair<size_t, const WPMiscInfo*>>
RTFWriter::FramesOf(const WPPart& part)
{
	std::vector<std::pair<size_t, const WPMiscInfo*>> frames;
	size_t start = 0;
	for (const WPParagraph& p : part.paragraphs) {
		for (const WPMiscInfo& m : p.misc)
			if (m.kind == Sig("FRAM"))
				frames.push_back({start + m.position, &m});
		start += p.text.size();
	}
	return frames;
}


// TExportRTF::writeBitmap: the frame as a picture, "{\pict\jpegblip
// \picwW\pichH " and the JPEG in hex (lowercase); the original draws
// the frame and has the system's JPEG translator write it. The port
// writes an image part's pixels (the rectangle the frame shows) as PNG
// (\pngblip), and nothing for other frames.
void
RTFWriter::Picture(const WPMiscInfo& frame)
{
	const Frame* f = fDoc.FrameOf(frame.frame[0], frame.frame[1]);
	const IMPart* image = f != nullptr ? fDoc.ImagePart(f->part) : nullptr;
	if (image == nullptr)
		return;
	std::vector<uint8_t> png = BitmapPNG(image->image, f->sheetRect);
	if (png.size() < 24)
		return;
	int width = png[16] << 24 | png[17] << 16 | png[18] << 8 | png[19];
	int height = png[20] << 24 | png[21] << 16 | png[22] << 8 | png[23];
	char buffer[64];
	snprintf(buffer, sizeof buffer, "{\\pict\\pngblip\\picw%d\\pich%d ", width, height);
	fOut += buffer;
	static const char kHex[] = "0123456789abcdef";
	for (uint8_t b : png) {
		fOut += kHex[b >> 4];
		fOut += kHex[b & 0xf];
	}
	fOut += "}";
}


int
RTFWriter::FontIndex(const std::string& family, const std::string& style) const
{
	// TFontTable::GetBeFontIndex of the BFont set to that family and
	// style; an unknown font leaves be_plain_font
	if (style.empty()) {
		for (size_t i = 0; i < fFonts.fonts.size(); i++)
			if (fFonts.fonts[i].family == family)
				return int(i);
	}
	int index = fFonts.Find(family, style);
	return index >= 0 ? index : fFonts.PlainIndex();
}


void
RTFWriter::Header()
{
	// TExportRTF::WriteHeader
	char buffer[64];
	snprintf(buffer, sizeof buffer, "{\\rtf1\\ansi\\deflang%i\\deflangfe%i", 0x409, 0x411);
	fOut += buffer;
	snprintf(buffer, sizeof buffer, "\\deff%i\n", fFonts.PlainIndex());
	fOut += buffer;
	fOut += "{\\fonttbl";
	for (size_t i = 0; i < fFonts.fonts.size(); i++) {
		const FontInfo& f = fFonts.fonts[i];
		bool isDefault;
		int family = FamilyCode(f.family, &isDefault);
		snprintf(buffer, sizeof buffer, "{\\f%i", int(i));
		fOut += buffer;
		fOut += kFamilyNames[family];
		snprintf(buffer, sizeof buffer, "\\fcharset%i ", f.charset);
		fOut += buffer;
		fOut += f.family + " " + f.style;
		fOut += ";}";
	}
	fOut += "}\n";
	fOut += "{\\colortbl;\\red0\\green0\\blue0;\\red0\\green0\\blue255;"
		"\\red0\\green255\\blue255;\\red0\\green255\\blue0;\\red255\\green0\\blue255;"
		"\\red255\\green0\\blue0;\\red255\\green255\\blue0;\\red255\\green255\\blue255;"
		"\\red0\\green0\\blue128;\\red0\\green128\\blue128;\\red0\\green128\\blue0;"
		"\\red128\\green0\\blue128;\\red128\\green0\\blue0;\\red128\\green128\\blue0;"
		"\\red128\\green128\\blue128;\\red192\\green192\\blue192;}\n";
	fOut += "\\paperw12240\\paperh15840";
	snprintf(buffer, sizeof buffer, "\\margt%i\\margb%i\\margl%i\\margr%i",
		0x5a0, 0x5a0, 0x5a0, 0x5a0);
	fOut += buffer;
	fOut += "\\widowctrl\\ftnbj\\ftnrestart\\ftnstart1\\pgnstart1";
	fOut += "\\deftab720\\linemod0\\linex0\\cols1\\colsx0";
	fOut += "\\sectd\\pard\\plain\\uc1\n";
}


void
RTFWriter::ParProps(uint32_t style)
{
	// TExportRTF::WriteParProps: the paragraph style's own properties
	const StyleMgr& styles = fDoc.styles;
	fOut += "\\pard";
	TextStyle t;
	int alignment = 0;
	if (const Property* p = Own(styles, style, Sig("PAlg"))) {
		ApplyProperty(*p, t);
		alignment = t.alignment;
	}
	fOut += alignment == 2 ? "\\qr" : alignment == 1 ? "\\qc" : alignment == 3 ? "\\qj" : "\\ql";
	char buffer[64];
	float left = 0;
	if (const Property* p = Own(styles, style, Sig("PLef"))) {
		ApplyProperty(*p, t);
		left = t.leftIndent;
		snprintf(buffer, sizeof buffer, "\\li%i", int(std::nearbyint(left * 20)));
		fOut += buffer;
	}
	if (const Property* p = Own(styles, style, Sig("PInd"))) {
		ApplyProperty(*p, t);
		snprintf(buffer, sizeof buffer, "\\fi%i",
			int(std::nearbyint((t.firstIndent - left) * 20)));
		fOut += buffer;
	}
	if (const Property* p = Own(styles, style, Sig("PRig"))) {
		ApplyProperty(*p, t);
		snprintf(buffer, sizeof buffer, "\\ri%i", int(std::nearbyint(t.rightIndent * 20)));
		fOut += buffer;
	}
}


void
RTFWriter::CharProps(uint32_t style)
{
	// TExportRTF::WriteCharProps: the run style's own properties (the font
	// only when the style has its own; its name as GetTextFont finds it)
	const StyleMgr& styles = fDoc.styles;
	fOut += "{\\plain";
	char buffer[64];
	uint32_t face = 1;
	bool hasFace = false;
	if (const Property* p = Own(styles, style, Sig("TFac"))) {
		TextStyle t;
		ApplyProperty(*p, t);
		hasFace = true;
		// TTranslatorStyle::GetTextFace: the flags, or without them when
		// the property says they're turned off
		DocArchive a(p->data);
		uint32_t flags = a.UInt32();
		bool on = a.Bool();
		face = on ? flags : (1 & ~flags);
		if (face == 0)
			face = 1;
	}
	if (Own(styles, style, Sig("TFon")) != nullptr) {
		std::string family = fFonts.plainFamily, fontStyle = fFonts.plainStyle;
		InheritedFont(styles, style, family, fontStyle);
		if (hasFace) {
			if (face & 1)
				fontStyle.clear();
			else if (face & 6) {
				std::string s = fFonts.StyleFor(family, (face & 2) != 0, (face & 4) != 0);
				if (!s.empty())
					fontStyle = s;
			}
		}
		fFont = FontIndex(family, fontStyle);
		snprintf(buffer, sizeof buffer, "\\f%i", fFont);
		fOut += buffer;
	}
	if (const Property* p = Own(styles, style, Sig("TSiz"))) {
		TextStyle t;
		ApplyProperty(*p, t);
		snprintf(buffer, sizeof buffer, "\\fs%i", int(std::nearbyint(t.size + t.size)));
		fOut += buffer;
	}
	if (hasFace) {
		if (face & 2)
			fOut += "\\b";
		if (face & 4)
			fOut += "\\i";
		if (face & 8)
			fOut += "\\ul";
		if (face & 0x10)
			fOut += "\\uldb";
		if (face & 0x20)
			fOut += "\\strike";
		if (face & 0x40)
			fOut += "\\super";
		if (face & 0x80)
			fOut += "\\sub";
	}
	if (const Property* p = Own(styles, style, Sig("TCol"))) {
		TextStyle t;
		ApplyProperty(*p, t);
		snprintf(buffer, sizeof buffer, "\\cf%i", ColorIndex(t.color));
		fOut += buffer;
	}
	fOut += " ";
}


void
RTFWriter::Text(const std::string& text)
{
	// TExportRTF::convertToRTFBuffer
	std::string out;
	bool japanese = fFont >= 0 && size_t(fFont) < fFonts.fonts.size()
		&& fFonts.fonts[fFont].charset == 0x80;
	if (japanese) {
		// Shift-JIS, every byte as \'xx
		std::string sjis = ConvertFromUTF8(kConversionSJIS, text, '?');
		char buffer[8];
		for (unsigned char c : sjis) {
			snprintf(buffer, sizeof buffer, "\\'%02x", c);
			out += buffer;
		}
	} else {
		// ISO 8859-1, one byte per character; characters it doesn't have
		// as \uN? with the UCS-2 value read in the x86 byte order (the
		// original doesn't swap it)
		size_t i = 0;
		char buffer[24];
		while (i < text.size()) {
			size_t from = i;
			unsigned char lead = (unsigned char)text[i];
			size_t length = ((0xe5000000u >> ((lead >> 3) & 0x1e)) & 3) + 1;
			uint32_t c = lead;
			if (length > 1 && i + length <= text.size()) {
				c = lead & (0x7f >> length);
				for (size_t k = 1; k < length; k++)
					c = (c << 6) | ((unsigned char)text[i + k] & 0x3f);
			} else
				length = 1;
			i = from + length;
			if (c == 0 || c > 0xff) {
				uint16_t ucs = c > 0xffff ? '?' : uint16_t(c);
				int16_t swapped = int16_t(uint16_t((ucs >> 8) | (ucs << 8)));
				snprintf(buffer, sizeof buffer, "\\u%d?", int(swapped));
				out += buffer;
			} else if (c == '\\' || c == '{' || c == '}') {
				out += '\\';
				out += char(c);
			} else if (c >= 0x80) {
				snprintf(buffer, sizeof buffer, "\\'%02x", unsigned(c));
				out += buffer;
			} else
				out += char(c);
		}
	}
	if (!out.empty() && out[0] != '\n')
		fOut += out;
}


std::string
RTFWriter::Write()
{
	Header();
	// TExportRTF::WriteWord with TTextIterator_WP::NextTextRun: runs of at
	// most 0x400 bytes, ending after a LF, at a character style change and
	// around special characters
	const std::string& text = fPart.text;
	const size_t kMax = 0x400;
	size_t pos = 0;
	size_t paraIndex = 0, charIndex = 0, specialIndex = 0;
	bool parStart = true, parEnd = true;
	while (pos < text.size()) {
		size_t runStart = pos;
		parStart = parEnd;
		if (paraIndex + 1 < fPart.paraRuns.size() && fPart.paraRuns[paraIndex + 1].first <= pos)
			paraIndex++;
		while (charIndex + 1 < fPart.charRuns.size() && fPart.charRuns[charIndex + 1].first <= pos)
			charIndex++;
		size_t nextChar = charIndex + 1 < fPart.charRuns.size()
			? fPart.charRuns[charIndex + 1].first : std::string::npos;
		while (specialIndex < fPart.specials.size() && fPart.specials[specialIndex].offset < pos)
			specialIndex++;
		const ExportPart::Special* special = specialIndex < fPart.specials.size()
			? &fPart.specials[specialIndex] : nullptr;
		std::string run;
		while (pos < text.size() && run.size() < kMax) {
			unsigned char c = (unsigned char)text[pos];
			bool atSpecial = special != nullptr && special->offset == pos;
			// a UTF-8 lead byte that might not fit
			if ((c & 0xc0) != 0x80 && run.size() + 4 >= kMax)
				break;
			run += char(c);
			pos++;
			parEnd = c == '\n';
			if (c == '\n' || atSpecial || pos == nextChar
					|| (special != nullptr && special->offset == pos))
				break;
		}
		if (pos >= text.size())
			parEnd = true;

		if (parStart) {
			uint32_t style = fPart.paraRuns.empty() ? 0xffffffff
				: fPart.paraRuns[paraIndex].second;
			ParProps(style);
		}
		uint32_t charStyle = fPart.charRuns.empty() ? 0xffffffff
			: fPart.charRuns[charIndex].second;
		if (charStyle == 0xffffffff)
			fOut += "{\\plain";
		else
			CharProps(charStyle);
		if (!run.empty() && run.back() == '\n')
			run.pop_back();
		if (special != nullptr && special->offset == runStart) {
			auto frame = std::find_if(fFrames.begin(), fFrames.end(),
				[&](const std::pair<size_t, const WPMiscInfo*>& f) { return f.first == runStart; });
			if (frame != fFrames.end()) {
				// the frame as a picture (writeBitmap)
				run.clear();
				Picture(*frame->second);
			} else if (special->hasText) {
				run = special->text;
			}
		}
		if (!run.empty())
			Text(run);
		fOut += parEnd ? "\\par}\n" : "}";
	}
	fOut += "}";
	return fOut;
}

} // namespace


std::string
ExportedRTF(const Document& doc, size_t part, const FontCatalog& fonts)
{
	const WPPart* wp = doc.WordPart(part);
	if (wp == nullptr) {
		// not a word processing sheet: the header and the closing brace
		WPPart empty;
		RTFWriter writer(doc, empty, fonts);
		return writer.Write();
	}
	RTFWriter writer(doc, *wp, fonts);
	return writer.Write();
}


bool
ExportRTF(const Document& doc, size_t part, const std::string& path, std::string& error,
	const FontCatalog& fonts)
{
	std::string rtf = ExportedRTF(doc, part, fonts);
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file) {
		error = "cannot write " + path;
		return false;
	}
	file.write(rtf.data(), std::streamsize(rtf.size()));
	if (!file) {
		error = "cannot write " + path;
		return false;
	}
	return true;
}

} // namespace gobe
