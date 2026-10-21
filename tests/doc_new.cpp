// Testing CreateBlankDocument: builds a blank WORD document and verifies its
// structure. Usage: doc_new [document ...]
// When called with no arguments, it creates a blank document from scratch and
// checks it against the expected structure.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sys/stat.h>

#include "DocArchive.h"
#include "DocSections.h"
#include "DocWriter.h"
#include "Document.h"
#include "Editing.h"
#include "WPPart.h"

using namespace gobe;

static bool FileExists(const std::string& path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0;
}

// Validate a blank document. Returns bad count.
static int
ValidateBlank(const std::string& name, Document& doc)
{
	int bad = 0;

	// 1. Document has a name starting with "untitled"
	if (doc.name.empty()) {
		printf("%s: FAIL: blank document has no name\n", name.c_str());
		bad++;
	} else if (doc.name.find("untitled") != 0) {
		printf("%s: FAIL: expected name starting with 'untitled', got '%s'\n",
			name.c_str(), doc.name.c_str());
		bad++;
	} else {
		printf("%s: ok: name = '%s'\n", name.c_str(), doc.name.c_str());
	}

	// 2. Header fileType == 'DOC!'
	if (doc.file.header.fileType != Sig("DOC!")) {
		printf("%s: FAIL: bad header fileType %#x (expected DOC!)\n",
			name.c_str(), doc.file.header.fileType);
		bad++;
	} else {
		printf("%s: ok: header fileType is DOC!\n", name.c_str());
	}

	// 3. Header identity == "GOBE SCOTT/01/02"
	if (doc.file.header.identity != "GOBE SCOTT/01/02") {
		printf("%s: FAIL: bad header identity '%s'\n",
			name.c_str(), doc.file.header.identity.c_str());
		bad++;
	} else {
		printf("%s: ok: header identity correct\n", name.c_str());
	}

	// 4. 13 sections in the TOC in the expected order
	const char* expectedSections[] = {
		"VersionMgr", "PartFactory Table", "Document Misc",
		"SetMgr", "PenMgr", "StyleMgr", "SheetMgr",
		"SelectionMgr", "LinksMgr", "PartMgr",
		"CommandMgr", "HistoryMgr", "WindowMgr"
	};
	size_t expectedCount = sizeof(expectedSections) / sizeof(expectedSections[0]);
	if (doc.file.toc.size() >= expectedCount) {
		printf("%s: ok: TOC has %zu entries (expected >= %zu)\n",
			name.c_str(), doc.file.toc.size(), expectedCount);
	} else {
		printf("%s: FAIL: TOC has %zu entries (expected >= %zu)\n",
			name.c_str(), doc.file.toc.size(), expectedCount);
		bad++;
	}

	// 5. Check required sections in TOC in correct order
	size_t foundSections = 0;
	for (size_t i = 0; i < expectedCount && i < doc.file.toc.size(); i++) {
		if (doc.file.toc[i].name == expectedSections[i]) {
			foundSections++;
		} else {
			printf("%s: FAIL: expected '%s' at TOC[%zu], got '%s'\n",
				name.c_str(), expectedSections[i], i, doc.file.toc[i].name.c_str());
			bad++;
		}
	}
	if (foundSections == expectedCount) {
		printf("%s: ok: all %zu sections in correct order\n",
			name.c_str(), foundSections);
	}

	// 6. PartFactory Table has 4 entries (WORD, GRFX, PROT, PAGE)
	if (doc.factories.size() == 4) {
		bool hasWord = false, hasGrfx = false, hasProt = false, hasPage = false;
		for (const auto& f : doc.factories) {
			if (f.kind == Sig("WORD")) hasWord = true;
			if (f.kind == Sig("GRFX")) hasGrfx = true;
			if (f.kind == Sig("PROT")) hasProt = true;
			if (f.kind == Sig("PAGE")) hasPage = true;
		}
		if (hasWord && hasGrfx && hasProt && hasPage) {
			printf("%s: ok: PartFactory Table has 4 correct entries\n",
				name.c_str());
		} else {
			printf("%s: FAIL: PartFactory Table entries incorrect (word=%d grfx=%d prot=%d page=%d)\n",
				name.c_str(), hasWord, hasGrfx, hasProt, hasPage);
			bad++;
		}
	} else {
		printf("%s: FAIL: PartFactory Table has %zu entries (expected 4)\n",
			name.c_str(), doc.factories.size());
		bad++;
	}

	// 7. StyleMgr has "Bas" (0x42617369) and "GRFX" (0x47524658) styles
	if (doc.styles.styles.size() >= 2) {
		bool hasBas = false, hasGrfx = false;
		for (const auto& s : doc.styles.styles) {
			if (s.id == 0x42617369u) hasBas = true;
			if (s.id == 0x47524658u) hasGrfx = true;
		}
		if (hasBas && hasGrfx) {
			printf("%s: ok: StyleMgr has Bas and GRFX styles\n",
				name.c_str());
		} else {
			printf("%s: FAIL: missing Bas=%d, GRFX=%d\n",
				name.c_str(), hasBas, hasGrfx);
			bad++;
		}
	} else {
		printf("%s: FAIL: StyleMgr has %zu styles (expected >= 2)\n",
			name.c_str(), doc.styles.styles.size());
		bad++;
	}

	// 8. PartMgr has exactly 3 parts (WORD, PROT, PAGE)
	if (doc.parts.parts.size() == 3) {
		bool hasWord = false, hasProt = false, hasPage = false;
		for (const auto& p : doc.parts.parts) {
			if (p.kind == Sig("WORD")) hasWord = true;
			if (p.kind == Sig("PROT")) hasProt = true;
			if (p.kind == Sig("PAGE")) hasPage = true;
		}
		if (hasWord && hasProt && hasPage) {
			printf("%s: ok: PartMgr has 3 parts (WORD, PROT, PAGE)\n", name.c_str());
		} else {
			printf("%s: FAIL: PartMgr parts incorrect (word=%d prot=%d page=%d)\n",
				name.c_str(), hasWord, hasProt, hasPage);
			bad++;
		}
	} else {
		printf("%s: FAIL: PartMgr has %zu parts (expected 3)\n",
			name.c_str(), doc.parts.parts.size());
		bad++;
	}

	// 9. WORD part has 1 empty paragraph
	if (!doc.wordParts.empty()) {
		const auto& [partIndex, wp] = doc.wordParts[0];
		if (wp.paragraphs.size() == 1) {
			const WPParagraph& p = wp.paragraphs[0];
			if (p.start == 0 && p.length == 0) {
				printf("%s: ok: WORD part has 1 empty paragraph\n",
					name.c_str());
			} else {
				printf("%s: FAIL: paragraph start=%u, length=%u\n",
					name.c_str(), p.start, p.length);
				bad++;
			}
		} else {
			printf("%s: FAIL: WORD part has %zu paragraphs (expected 1)\n",
				name.c_str(), wp.paragraphs.size());
			bad++;
		}
		// Check for 1 section
		if (wp.sections.size() == 1) {
			printf("%s: ok: WORD part has 1 section\n", name.c_str());
		} else {
			printf("%s: FAIL: WORD part has %zu sections (expected 1)\n",
				name.c_str(), wp.sections.size());
			bad++;
		}
	} else {
		printf("%s: FAIL: no word parts parsed\n", name.c_str());
		bad++;
	}

	// 10. LinksMgr is empty
	if (doc.links.links.empty()) {
		printf("%s: ok: LinksMgr is empty\n", name.c_str());
	} else {
		printf("%s: FAIL: LinksMgr has %zu links (expected 0)\n",
			name.c_str(), doc.links.links.size());
		bad++;
	}

	// 11. SelectionMgr is empty
	if (doc.selections.selections.empty()) {
		printf("%s: ok: SelectionMgr is empty\n", name.c_str());
	} else {
		printf("%s: FAIL: SelectionMgr has %zu selections (expected 0)\n",
			name.c_str(), doc.selections.selections.size());
		bad++;
	}

	// 12. No presentations (slides)
	if (doc.presentationPart < 0 && doc.slides.empty()) {
		printf("%s: ok: no presentations\n", name.c_str());
	} else {
		printf("%s: FAIL: unexpected presentation data\n", name.c_str());
		bad++;
	}

	return bad;
}

// Get the first 8 and last 8 bytes of a section by TOC name.
static void GetSectionBounds(const Document& doc, const char* sectionName,
	std::vector<uint8_t>& first8, std::vector<uint8_t>& last8)
{
	for (const TocEntry& e : doc.file.toc) {
		if (e.name == sectionName) {
			size_t start = e.offset;
			size_t end = 0;
			// find next TOC entry or file end
			for (size_t j = 0; j < doc.file.toc.size(); j++) {
				if (doc.file.toc[j].offset > start) {
					end = doc.file.toc[j].offset;
					break;
				}
			}
			if (end == 0)
				end = doc.bytes.size();
			size_t total = end - start;
			if (total >= 16) {
				first8.assign(doc.bytes.begin() + start,
					doc.bytes.begin() + start + 8);
				last8.assign(doc.bytes.begin() + end - 8,
					doc.bytes.begin() + end);
			}
			return;
		}
	}
}

int main(int argc, char** argv)
{
	int okCount = 0;
	int failCount = 0;

	// Create a blank document from scratch.
	printf("=== CreateBlankDocument from scratch ===\n");
	Document doc;
	std::string error;
	if (!CreateBlankDocument(doc, error)) {
		printf("FAIL: CreateBlankDocument failed: %s\n", error.c_str());
		printf("doc_new ok 0, failed 1\n");
		return 1;
	}
	okCount++;

	// Validate basic structure.
	int bad = ValidateBlank("(scratch)", doc);
	if (bad == 0)
		okCount++;
	else {
		failCount += bad;
		printf("FAIL: CreateBlankDocument — %d issues\n", bad);
	}

	// Round-trip: save and reload.
	const std::string tmpPath = "/tmp/gobe_doc_new_test.doc";
	{
		DocChanges changes;  // no changes: copy everything
		std::vector<uint8_t> saved = SaveDocument(doc, changes);
		std::ofstream out(tmpPath.c_str(), std::ios::binary);
		out.write(reinterpret_cast<const char*>(saved.data()), saved.size());
		out.close();

		// Check that SaveDocument produced identical bytes (no changes = copy).
		if (saved == doc.bytes) {
			printf("doc_new: ok: SaveDocument produced identical bytes\n");
			okCount++;
		} else {
			printf("doc_new: FAIL: SaveDocument bytes differ (orig %zu, saved %zu)\n",
				doc.bytes.size(), saved.size());
			failCount++;
		}

		// Load the saved document.
		Document loaded;
		std::string loadError;
		if (!LoadDocument(tmpPath, loaded, loadError)) {
			printf("doc_new: FAIL: LoadDocument after save failed: %s\n",
				loadError.c_str());
			failCount++;
		} else {
			printf("doc_new: ok: loaded saved document\n");
			okCount++;

			// Set name (window-level concept, not persisted in file).
			loaded.name = doc.name;

			// Compare loaded vs original structure.
			int loadBad = ValidateBlank("(loaded)", loaded);
			if (loadBad == 0) {
				printf("doc_new: ok: loaded document passes all checks\n");
				okCount++;
			} else {
				printf("doc_new: FAIL: loaded document has %d issues\n",
					loadBad);
				failCount += loadBad;
			}

			// Verify bytes are identical after round-trip.
			if (loaded.bytes == doc.bytes) {
				printf("doc_new: ok: round-trip bytes identical\n");
				okCount++;
			} else {
				printf("doc_new: FAIL: round-trip bytes differ\n");
				failCount++;
			}

			// Test text insertion.
			WPPart* wp = loaded.EditableWordPart(0);
			if (wp != nullptr) {
				TextPosition pos{0, 0};
				TextPosition after = InsertText(*wp, pos, "Hello");
				if (after.paragraph == 0 && after.offset == 5) {
					printf("doc_new: ok: InsertText inserted \"Hello\" at (0,0)\n");
					okCount++;

					// Check paragraph text.
					if (!wp->paragraphs[0].text.empty()
						&& wp->paragraphs[0].text.find("Hello") != std::string::npos) {
						printf("doc_new: ok: paragraph text contains \"Hello\"\n");
						okCount++;
					} else {
						printf("doc_new: FAIL: paragraph text doesn't contain \"Hello\"\n");
						failCount++;
					}

					// Save again with the edit.
					loaded.dirtyParts.insert(0);
					std::string saveError;
					if (loaded.Save(tmpPath, saveError)) {
						Document reloaded;
						std::string reloadError;
						if (LoadDocument(tmpPath, reloaded, reloadError)) {
							const WPPart* rwp = reloaded.WordPart(0);
							if (rwp != nullptr && !rwp->paragraphs[0].text.empty()
								&& rwp->paragraphs[0].text.find("Hello") != std::string::npos) {
								printf("doc_new: ok: saved+reloaded paragraph text contains \"Hello\"\n");
								okCount++;
							} else {
								printf("doc_new: FAIL: reloaded paragraph missing \"Hello\"\n");
								failCount++;
							}
						} else {
							printf("doc_new: FAIL: reload after edit failed: %s\n",
								reloadError.c_str());
							failCount++;
						}
					} else {
						printf("doc_new: FAIL: save with edit failed: %s\n",
							saveError.c_str());
						failCount++;
					}
				} else {
					printf("doc_new: FAIL: InsertText returned wrong position\n");
					failCount++;
				}
			} else {
				printf("doc_new: FAIL: no editable word part\n");
				failCount++;
			}
		}
	}

	// Check structural integrity of unparseable sections against a sample.
	{
		// Find a sample document to compare against.
		const char* samplePath = nullptr;
		const char* samples[] = {
			"../original/installed/*/Gobe Productive 2.0/Samples/Resume.doc",
			"../original/installed/*/*Gobe Productive 2.0/Samples/Resume.doc",
			"/home/ian/Projects/Gobe_Productive/"
				"original/installed/*/Gobe Productive 2.0/"
				"Samples/Resume.doc",
			nullptr
		};
		for (int i = 0; samples[i]; i++) {
			if (FileExists(samples[i])) {
				samplePath = samples[i];
				break;
			}
		}

		const char* unparseableSections[] = {
			"SetMgr", "PenMgr", "SheetMgr",
			"CommandMgr", "HistoryMgr", "WindowMgr"
		};
		int sectionsChecked = 0;

		if (samplePath) {
			Document sample;
			std::string sampleError;
			if (LoadDocument(samplePath, sample, sampleError)) {
				for (const char* section : unparseableSections) {
					std::vector<uint8_t> blankFirst8, blankLast8;
					std::vector<uint8_t> sampleFirst8, sampleLast8;
					GetSectionBounds(doc, section, blankFirst8, blankLast8);
					GetSectionBounds(sample, section, sampleFirst8, sampleLast8);
					if (!blankFirst8.empty() && !sampleFirst8.empty()) {
						bool firstMatch = (blankFirst8 == sampleFirst8);
						bool lastMatch = (blankLast8 == sampleLast8);
						if (firstMatch && lastMatch) {
							printf("doc_new: ok: %s structural integrity matches sample\n",
								section);
							okCount++;
							sectionsChecked++;
						} else {
							if (!firstMatch)
								printf("doc_new: FAIL: %s first 8 bytes differ\n", section);
							if (!lastMatch)
								printf("doc_new: FAIL: %s last 8 bytes differ\n", section);
							failCount++;
						}
					} else {
						printf("doc_new: FAIL: could not get section bounds for %s\n",
							section);
						failCount++;
					}
				}
			} else {
				printf("doc_new: skip: could not load sample %s: %s\n",
					samplePath, sampleError.c_str());
			}
		} else {
			printf("doc_new: skip: no sample document found for section integrity\n");
		}

		if (sectionsChecked > 0) {
			okCount++;  // bonus for structural integrity checks
		}
	}

	// Cleanup
	remove(tmpPath.c_str());

	printf("doc_new ok %d, failed %d\n", okCount, failCount);
	return failCount > 0 ? 1 : 0;
}
