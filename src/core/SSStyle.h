// Spreadsheet cell appearance: the cell's effective style (the part's
// style, then the column's and row's header cell styles, then the cell's
// own, as TSSDisplayManager::DrawCell pushes them) and the cell text
// formatted with its number format (TSSDisplayManager::CellText).
#ifndef GOBE_SS_STYLE_H
#define GOBE_SS_STYLE_H

#include <string>
#include <vector>

#include "SSEval.h"
#include "Styles.h"

namespace gobe {

struct CellStyle {
	TextStyle text;
	Ink fill;					// FCol
	float fillOpacity = 1;		// FTra
	int hAlign = 0;				// HAln: 0 general, 1 left, 2 center, 3 right
	int vAlign = 3;				// VAln: 1 top, 2 center, 3 bottom
	std::string format = "General";	// SSNm
	bool wrap = false;			// ClWp
	Ink pen;					// PCol (grid lines in the grid style)
	float penOpacity = 1;		// PTra
	// TBorderProperties: 'BCol'/'BTra'/'BSiz' per side: 0 left, 1 top,
	// 2 right, 3 bottom
	struct Border {
		Ink ink;
		bool shown = false;
		float size = 0;
	} borders[4];
};

// Applies the styles in order (each with its parents first).
CellStyle ResolveCellStyle(const StyleMgr& styles, const std::vector<uint32_t>& stack);

// The text shown for a value with a number format ("General", "$#.00;
// [Red]-$#.00", "m/d/yyyy", ...); `red` is set by a [Red] section.
std::string FormatCellValue(const SSValue& value, const std::string& format, bool* red = nullptr);

// whether a number format shows dates or times (its first section has
// d, m, y, h or s outside quotes and brackets)
bool IsDateNumberFormat(const std::string& format);

// HAln 0 (general) resolved by value type (CellHAlignment): text left,
// numbers and times right, booleans and errors centered.
int CellHAlign(const CellStyle& style, const SSValue& value);

// Cell property encoders (inverse of the decoders in ApplyCellProperty):
Property NumberFormatProperty(const std::string& format);	// 'SSNm'
Property CellHAlignProperty(int align);		// 'HAln'
Property CellVAlignProperty(int align);		// 'VAln'
Property CellWrapProperty(bool wrap);		// 'ClWp'
Property CellFillProperty(Color color);		// 'FCol', solid ink
Property FillTransparencyProperty(float opacity);	// 'FTra', 0 to 1
// TPropBorderColor 'BCol': a solid ink and the side (0 left, 1 top, 2
// right, 3 bottom); TPropBorderTransparency 'BTra': the opacity (0 to 1)
// and the side
Property BorderColorProperty(int side, Color color);
Property BorderTransparencyProperty(int side, float opacity);
// the side a border property is for (its last four bytes), -1 if none
int BorderSide(const Property& property);

} // namespace gobe

#endif
