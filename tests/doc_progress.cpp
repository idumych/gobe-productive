// doc_progress: Every importer and exporter reports progress through
// ProgressScope / ReportProgress. For every sample document, round-trip
// every supported export → import pair inside a ProgressScope that
// collects every fraction; then verify the monotonic 0..1 sequence ending
// at 1.0f.  Then run the same call with a cancel reporter and verify it
// returns false and sets error to kCancelledError.
// Arguments: documents.
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "Document.h"
#include "Excel.h"
#include "HTMLExport.h"
#include "HTMLImport.h"
#include "Progress.h"
#include "RTF.h"
#include "SSSylk.h"
#include "TextImport.h"
#include "Word.h"

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

// Check that `fracs` meets all the progress requirements.
// Returns the number of individual checks performed (including the
// non-decreasing sub-checks).
static int
CheckFractions(const std::string& label, const std::vector<float>& fracs)
{
	int checks = 0;

	// 1. At least one fraction reported.
	checks++;
	Check(fracs.size() > 0, label + ": no fractions reported");

	// 2. All fractions in [0, 1].
	for (size_t i = 0; i < fracs.size(); i++) {
		checks++;
		if (fracs[i] < 0.0f || fracs[i] > 1.0f) {
			Check(false, label + ": fraction[" + std::to_string(i) + "] = "
				+ std::to_string(fracs[i]) + " (not in [0,1])");
			break;
		}
	}

	// 3. Non-decreasing.
	checks++;
	{
		bool ok = true;
		for (size_t i = 1; i < fracs.size(); i++) {
			if (fracs[i] < fracs[i - 1]) {
				ok = false;
				std::string detail = label + ": not non-decreasing: "
					+ std::to_string(fracs[i]) + " < "
					+ std::to_string(fracs[i - 1]);
				detail += " (fractions: ";
				for (size_t j = 0; j < fracs.size() && j < 20; j++) {
					if (j > 0) detail += ", ";
					detail += std::to_string(fracs[j]);
				}
				if (fracs.size() >= 20) detail += ", ...";
				detail += ")";
				Check(false, detail);
				break;
			}
		}
		if (ok)
			sOk++;
	}

	// 4. Last fraction is exactly 1.0f.
	checks++;
	if (fracs.size() > 0) {
		Check(fracs.back() == 1.0f, label + ": last fraction "
			+ std::to_string(fracs.back()) + " != 1.0f");
	}

	return checks;
}

// Run `work` inside a collecting ProgressScope; return collected fractions
// and the return value of the work function.
static std::pair<bool, std::vector<float>>
Collect(bool* result, const std::function<bool()>& work)
{
	std::vector<float> fractions;
	{
		ProgressScope scope([&fractions](float f) -> bool {
			fractions.push_back(f);
			return true;
		});
		*result = work();
	}
	return std::make_pair(*result, fractions);
}

// Run `work` inside a collecting ProgressScope where the reporter
// returns false on the first report (simulating cancel).
// Returns the (single) fraction that was reported and the return value of work.
static std::pair<bool, std::vector<float>>
CollectFirstCancel(bool* result, const std::function<bool()>& work)
{
	std::vector<float> fractions;
	{
		ProgressScope scope([&fractions](float /*f*/) -> bool {
			if (fractions.empty())
				fractions.push_back(0.0f);
			return false;
		});
		*result = work();
	}
	return std::make_pair(*result, fractions);
}

// Test one export → import round-trip.
template <typename ExportFunc, typename ImportFunc>
static void
TestRoundTrip(const std::string& docPath, int mainPart, const char* label,
	ExportFunc doExport, ImportFunc doImport)
{
	std::string tmpPath = std::string(getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp")
		+ "/progress." + label;

	// --- Export normal ---
	{
		Document newDoc;
		std::string error;
		bool ret;
		std::string checkLabel = docPath + " " + label + " export";
		auto [ok, expFracs] = Collect(&ret, [&]() {
			error.clear();
			return doExport(newDoc, size_t(mainPart), tmpPath, error);
		});
		(void)ok;
		Check(ret, checkLabel + ": export returned false");
		CheckFractions(checkLabel, expFracs);
	}

	// --- Import normal ---
	{
		Document newDoc;
		std::string error;
		bool ret;
		std::string checkLabel = docPath + " " + label + " import";
		auto [ok, impFracs] = Collect(&ret, [&]() {
			error.clear();
			return doImport(newDoc, tmpPath, error);
		});
		(void)ok;
		Check(ret, checkLabel + ": import returned false");
		CheckFractions(checkLabel, impFracs);
	}

	// --- Export cancelled ---
	{
		Document newDoc;
		std::string error;
		bool ret;
		auto [ok, fracs] = CollectFirstCancel(&ret, [&]() {
			error.clear();
			return doExport(newDoc, size_t(mainPart), tmpPath, error);
		});
		(void)ok; (void)fracs;
		Check(!ret, docPath + " " + label + ": cancelled export returned true");
		Check(error == kCancelledError, docPath + " " + label
			+ ": cancelled export error was \"" + error
			+ "\", expected \"" + kCancelledError + "\"");
	}

	// --- Import cancelled ---
	{
		Document newDoc;
		std::string error;
		bool ret;
		auto [ok, fracs] = CollectFirstCancel(&ret, [&]() {
			error.clear();
			return doImport(newDoc, tmpPath, error);
		});
		(void)ok; (void)fracs;
		Check(!ret, docPath + " " + label + ": cancelled import returned true");
		Check(error == kCancelledError, docPath + " " + label
			+ ": cancelled import error was \"" + error
			+ "\", expected \"" + kCancelledError + "\"");
	}
}

// --- Word processing round-trips ---

// RTF: ExportRTF(doc, part, path, error, fonts) → ImportRTF(path, doc, error, fonts)
static void
TestRTF(const std::string& docPath, Document& doc, int mainPart)
{
	FontCatalog fonts;
	TestRoundTrip(docPath, mainPart, "RTF",
		[&](Document& d, size_t part, const std::string& path, std::string& e) {
			(void)d;
			return ExportRTF(doc, part, path, e, fonts);
		},
		[&](Document& d, const std::string& path, std::string& e) {
			return ImportRTF(path, d, e, fonts);
		});
}

// Text: ExportText(doc, part, path, error, attributes, fonts) → ImportText(path, doc, error)
static void
TestText(const std::string& docPath, Document& doc, int mainPart)
{
	FontCatalog fonts;
	TestRoundTrip(docPath, mainPart, "Text",
		[&](Document& d, size_t part, const std::string& path, std::string& e) {
			(void)d;
			return ExportText(doc, part, path, e, nullptr, &fonts);
		},
		[&](Document& d, const std::string& path, std::string& e) {
			return ImportText(path, d, e);
		});
}

// HTML: ExportHTML(doc, path, title, error) → ImportHTML(path, doc, error)
static void
TestHTML(const std::string& docPath, Document& doc, int mainPart)
{
	TestRoundTrip(docPath, mainPart, "HTML",
		[&](Document& d, size_t part, const std::string& path, std::string& e) {
			(void)d; (void)part;
			return ExportHTML(doc, path, "progress", e);
		},
		[&](Document& d, const std::string& path, std::string& e) {
			return ImportHTML(path, d, e);
		});
}

// Word: ExportWord(doc, path, error) → ImportWord(path, doc, error)
static void
TestWord(const std::string& docPath, Document& doc, int mainPart)
{
	TestRoundTrip(docPath, mainPart, "Word",
		[&](Document& d, size_t part, const std::string& path, std::string& e) {
			(void)d; (void)part;
			return ExportWord(doc, path, e);
		},
		[&](Document& d, const std::string& path, std::string& e) {
			return ImportWord(path, d, e);
		});
}

// --- Spreadsheet round-trips ---

// SYLK: ExportSylk(doc, part, path, error) → ImportSylk(path, doc, error)
static void
TestSylk(const std::string& docPath, Document& doc, int mainPart)
{
	TestRoundTrip(docPath, mainPart, "SYLK",
		[&](Document& d, size_t part, const std::string& path, std::string& e) {
			return ExportSylk(doc, part, path, e);
		},
		[&](Document& d, const std::string& path, std::string& e) {
			return ImportSylk(path, d, e);
		});
}

// Table: ExportTable(doc, part, path, delimiter, error) → ImportTable(path, doc, error, delimiter)
static void
TestTable(const std::string& docPath, Document& doc, int mainPart)
{
	TestRoundTrip(docPath, mainPart, "Table",
		[&](Document& d, size_t part, const std::string& path, std::string& e) {
			return ExportTable(doc, part, path, ',', e);
		},
		[&](Document& d, const std::string& path, std::string& e) {
			return ImportTable(path, d, e, 0);
		});
}

// Excel: ExportExcel(doc, path, error) → ImportExcel(path, doc, error)
static void
TestExcel(const std::string& docPath, Document& doc, int mainPart)
{
	TestRoundTrip(docPath, mainPart, "Excel",
		[&](Document& d, size_t part, const std::string& path, std::string& e) {
			(void)d; (void)part;
			return ExportExcel(doc, path, e);
		},
		[&](Document& d, const std::string& path, std::string& e) {
			return ImportExcel(path, d, e);
		});
}

int
main(int argc, char** argv)
{
	size_t wpDocsChecked = 0;
	size_t ssDocsChecked = 0;

	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;

		int mainPart = doc.MainPart();
		if (mainPart < 0)
			continue;

		bool isWord = doc.WordPart(size_t(mainPart)) != nullptr;
		bool isSpreadsheet = doc.SpreadsheetPart(size_t(mainPart)) != nullptr;

		if (!isWord && !isSpreadsheet)
			continue;

		if (isWord) {
			wpDocsChecked++;
			TestRTF(argv[i], doc, mainPart);
			TestText(argv[i], doc, mainPart);
			TestHTML(argv[i], doc, mainPart);
			TestWord(argv[i], doc, mainPart);
		}

		if (isSpreadsheet) {
			ssDocsChecked++;
			TestSylk(argv[i], doc, mainPart);
			TestTable(argv[i], doc, mainPart);
			TestExcel(argv[i], doc, mainPart);
		}
	}

	if (wpDocsChecked == 0)
		sFailed++;
	if (ssDocsChecked == 0)
		sFailed++;

	printf("progress ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 ? 1 : 0;
}
