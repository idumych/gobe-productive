// doc_fonts: MissingFonts (TStyleManager::CheckFonts): the style table's
// font properties not installed, "family style", in the table's order
#include <cstdio>
#include <string>

#include "TextImport.h"

using namespace gobe;

static int sOk = 0, sFailed = 0;

static void
Check(bool ok, const std::string& what)
{
	if (ok)
		sOk++;
	else {
		sFailed++;
		printf("FAIL %s\n", what.c_str());
	}
}


int
main()
{
	StyleMgr styles;
	styles.properties.push_back(TextFontProperty("Dutch801 Rm BT", "Roman"));
	styles.properties.push_back(Property{Sig("TSiz"), {0x00, 0x0c}});
	styles.properties.push_back(TextFontProperty("Swis721 BT", "Bold"));
	styles.properties.push_back(TextFontProperty("Noto Sans", "Regular"));
	FontCatalog installed;
	installed.fonts.push_back({"Noto Sans", "Regular", 0});
	installed.fonts.push_back({"Swis721 BT", "Roman", 0});
	std::vector<std::string> missing = MissingFonts(styles, installed);
	Check(missing.size() == 2, "two missing, got " + std::to_string(missing.size()));
	Check(missing.size() > 0 && missing[0] == "Dutch801 Rm BT Roman", "first: Dutch801 Rm BT Roman");
	Check(missing.size() > 1 && missing[1] == "Swis721 BT Bold",
		"second: Swis721 BT Bold (its family is installed, not that style)");
	installed.fonts.push_back({"Dutch801 Rm BT", "Roman", 0});
	installed.fonts.push_back({"Swis721 BT", "Bold", 0});
	Check(MissingFonts(styles, installed).empty(), "none missing once installed");
	Check(MissingFonts(StyleMgr(), installed).empty(), "an empty table");
	printf("fonts ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 ? 1 : 0;
}
