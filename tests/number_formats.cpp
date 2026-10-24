// NumberFormats: the Number dialog's categories and the format strings made
// from them (TNumberFormatFactory, TNumberFormatCategory, TNumberFormat::
// Parameterize in decomp/Gobe Productive). Expectations: the category list
// as '#NF#' 1 orders it (General, Number, Currency, Date, Time, Percentage,
// Fraction, Scientific, Special; Accounting isn't listed), format strings
// the documents hold ("$#.00;($#.00)" is Currency's "$#,###.0" and
// "($#,###.0)" with 2 decimals, no commas), and the decomp's rules (no
// decimals: the point goes too; commas: the digits before the point become
// "#,###"). Every format string in the documents' styles that a category
// makes must come back the same from Find and FormatString.
#include <cstdio>
#include <map>
#include <string>

#include "DocArchive.h"
#include "Document.h"
#include "NumberFormats.h"

using namespace gobe;

static int ok = 0, failed = 0;

static void
Check(bool cond, const std::string& desc)
{
	if (cond) {
		ok++;
		return;
	}
	failed++;
	if (failed <= 20)
		printf("FAIL: %s\n", desc.c_str());
}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		printf("usage: number_formats document...\n");
		return 1;
	}
	// the original binary: "Gobe Productive" next to Samples/ or Labels/
	std::string path = argv[1];
	path = path.substr(0, path.rfind('/'));
	path = path.substr(0, path.rfind('/')) + "/Gobe Productive";
	ResourceFile resources;
	if (!resources.Load(path)) {
		printf("FAIL: %s doesn't load\n", path.c_str());
		return 1;
	}
	NumberFormats formats;
	std::string error;
	Check(formats.Load(resources, error), "load: " + error);

	const char* names[] = {"General", "Number", "Currency", "Date", "Time", "Percentage",
		"Fraction", "Scientific", "Special"};
	Check(formats.Categories().size() == 9, "9 categories, not "
		+ std::to_string(formats.Categories().size()));
	for (size_t i = 0; i < 9 && i < formats.Categories().size(); i++)
		Check(formats.Categories()[i].name == names[i], "category " + std::to_string(i)
			+ " is " + formats.Categories()[i].name + ", not " + names[i]);
	if (formats.Categories().size() >= 3) {
		const NumberFormatCategory& number = formats.Categories()[1];
		Check(number.commas && number.decimals && number.groups.size() == 9,
			"Number: commas, decimals, 9 types");
		Check(formats.Categories()[0].groups.size() == 1
				&& formats.FormatString(0, 0, 0, false) == "General",
			"General's one type is \"General\"");
		// Currency's type 6: "$#,###.0" and "($#,###.0)"
		Check(formats.FormatString(2, 6, 2, false) == "$#.00;($#.00)",
			"Currency type 6, 2 decimals, no commas: " + formats.FormatString(2, 6, 2, false));
		Check(formats.FormatString(2, 6, 2, true) == "$#,###.00;($#,###.00)",
			"Currency type 6, 2 decimals, commas: " + formats.FormatString(2, 6, 2, true));
		// Number's type 1: "#.0" and "-#.0"
		Check(formats.FormatString(1, 1, 2, false) == "#.00;-#.00",
			"Number type 1, 2 decimals: " + formats.FormatString(1, 1, 2, false));
	}

	// Parameterize on its own
	Check(ParameterizeFormat("#.0", 2, false) == "#.00", "#.0, 2 decimals");
	Check(ParameterizeFormat("#.0", 0, false) == "#", "#.0, no decimals");
	Check(ParameterizeFormat("#.#", 3, false) == "#.###", "#.#, 3 decimals keeps #");
	Check(ParameterizeFormat("0.0", 1, true) == "#,###.0", "0.0 with commas");
	Check(ParameterizeFormat("$#,###.0", 1, true) == "$#,###.0", "commas already there");
	// without a point one is appended at the end (local_414 = its length),
	// whatever follows the digits: the original's formats all have one
	Check(ParameterizeFormat("#%", 2, false) == "#%.00", "a point added: "
		+ ParameterizeFormat("#%", 2, false));

	// round trips of the documents' format strings
	std::map<std::string, bool> seen;
	for (int i = 1; i < argc; i++) {
		Document doc;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		for (const Property& p : doc.styles.properties)
			if (p.type == Sig("SSNm")) {
				DocArchive a(p.data);
				try {
					seen[a.String()] = true;
				} catch (...) {
				}
			}
	}
	int roundTrips = 0;
	for (const auto& [format, unused] : seen) {
		NumberFormatGroupInfo info;
		if (!formats.Find(format, info))
			continue;
		std::string again = formats.FormatString(info.category, info.type, info.decimals,
			info.commas);
		// (a format without sections matches a type's first one)
		if (format.find(';') == std::string::npos && again.find(';') != std::string::npos)
			again.resize(again.find(';'));
		// "#.;-#." isn't what Parameterize makes for no decimals (it drops the
		// point): it finds Number's type by its look and comes back as "#;-#"
		if (format == "#.;-#.") {
			Check(again == "#;-#", format + " comes back as " + again);
			continue;
		}
		roundTrips++;
		Check(again == format, format + " comes back as " + again + " (category "
			+ std::to_string(info.category) + " type " + std::to_string(info.type) + ")");
	}
	printf("formats %d, ok %d, failed %d\n", roundTrips, ok, failed);
	return failed == 0 && roundTrips > 0 ? 0 : 1;
}
