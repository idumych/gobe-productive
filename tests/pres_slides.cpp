// pres_slides: a presentation's slides and their layers (Presentation.cpp).
// For each sample document: loading, save round trip, AddSlide,
// SlideAddLayer/RemoveLayer, SetCurrentSlide, MoveSlide, SetSlideTransition,
// AddPresentationLayer, RemoveSlide, WritePresentationBlock.
// Arguments: documents.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Document.h"
#include "DocWriter.h"
#include "GRPart.h"

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
main(int argc, char** argv)
{
	// Collect all documents that have a presentation part
	std::vector<std::string> present;
	std::string error;
	for (int i = 1; i < argc; i++) {
		Document doc;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		if (doc.presentationPart < 0)
			continue;
		present.push_back(argv[i]);
	}
	Check(present.size() > 0, "at least one presentation found");

	size_t presCount = 0;
	for (const std::string& path : present) {
		// ---- 1. Loading ----
		{
			Document doc;
			Check(LoadDocument(path, doc, error), path + std::string(": load: ") + error);
			Check(doc.presentationPart >= 0, path + ": has presentation");
			Check(doc.slides.size() > 0, path + ": slides.size() > 0");
			for (size_t k = 0; k < doc.slides.size(); k++) {
				Check(doc.SlideName(k) == doc.slides[k].name,
					path + std::string(", slide ") + std::to_string(k)
					+ ": name matches");
				Check(doc.SlideLayers(k).size() == doc.slides[k].layers,
					path + std::string(", slide ") + std::to_string(k)
					+ ": layers count matches");
			}
			// save these originals for step 2
			size_t origSlideCount = doc.slides.size();
			std::vector<Document::Slide> origSlides = doc.slides;
			Document::PresentationSettings origSettings = doc.presentation;
			std::vector<uint8_t> origBytes = doc.bytes;

			// ---- 2. Save round trip ----
			{
				const char* tmpDir = getenv("TMPDIR");
				std::string outPath = std::string(tmpDir ? tmpDir : "/tmp") + "/pres.gobe";
				bool renamed = doc.RenameSlide(0, "Renamed");
				Check(renamed, path + std::string(": RenameSlide(0, 'Renamed')"));
				bool flagSet = doc.SetPresentationFlag(true, !doc.presentation.cursor);
				Check(flagSet, path + std::string(": SetPresentationFlag(cursor flipped)"));
				bool saved = doc.Save(outPath, error);
				Check(saved, path + std::string(": SaveDocument: ") + error);

				Document loaded;
				Check(LoadDocument(outPath, loaded, error),
					path + std::string(": reloaded: ") + error);
				Check(loaded.slides.size() == origSlideCount,
					path + std::string(": round-trip slide count same"));
				Check(loaded.SlideName(0) == "Renamed",
					path + std::string(": slide 0 renamed 'Renamed'"));
				// every slide's view, transition, options, collapsed
				for (size_t k = 0; k < origSlides.size(); k++) {
					Check(loaded.slides[k].view == origSlides[k].view,
						path + std::string(", slide ") + std::to_string(k)
						+ ": view round-trip");
					Check(loaded.slides[k].transition == origSlides[k].transition,
						path + std::string(", slide ") + std::to_string(k)
						+ ": transition round-trip");
					Check(loaded.slides[k].options == origSlides[k].options,
						path + std::string(", slide ") + std::to_string(k)
						+ ": options round-trip");
					Check(loaded.slides[k].collapsed == origSlides[k].collapsed,
						path + std::string(", slide ") + std::to_string(k)
						+ ": collapsed round-trip");
				}
				Check(loaded.presentation.cursor != origSettings.cursor,
					path + std::string(": cursor flipped"));
				Check(loaded.presentation.autoStart == origSettings.autoStart,
					path + std::string(": autoStart unchanged"));
				// the GRPart values[0] should be updated
				const GRPart* gr = loaded.PresentationGraphics();
				if (gr) {
					int activePartLayer = loaded.currentSlide >= 0
						&& loaded.slides.size() > size_t(loaded.currentSlide)
						? loaded.SlideLayerPart(size_t(loaded.currentSlide), 0) : -1;
					if (activePartLayer >= 0) {
						// after round-trip, the active layer info should match
					}
				}
			}

			// ---- 3. AddSlide (sequential with step 6 below) ----
			{
				Document doc;
				Check(LoadDocument(path, doc, error),
					path + std::string(": fresh load for AddSlide: ") + error);
				const GRPart* gr = doc.PresentationGraphics();
				size_t partLayers = gr ? gr->layers.size() : 0;
				size_t at = doc.AddSlide(1, "Added");
				Check(at == 1, path + std::string(": AddSlide(1,'Added') returns 1"));
				Check(doc.slides.size() == origSlideCount + 1,
					path + std::string(": slide count one more"));
				Check(doc.SlideName(1) == "Added",
					path + std::string(": SlideName(1) == 'Added'"));
				Check(doc.SlideLayers(1).empty(),
					path + std::string(": SlideLayers(1) empty"));
				// the new slide's view is the last view
				size_t lastViewIdx = doc.slides.size() - 1;
				Check(doc.slides[lastViewIdx].view < doc.slides.size(),
					path + std::string(": new slide has valid view"));
				// has one LayerInfo per graphics layer
				if (gr && gr->layers.size() > 0) {
					const GRView& v = gr->views[doc.slides[lastViewIdx].view];
					Check(v.layers.size() == gr->layers.size(),
						path + std::string(": new slide view layer infos == part layers"));
				}
				// Save for step 6 (MoveSlide needs the "Added" slide):
				// ---- 6. MoveSlide ----
				if (doc.slides.size() >= 2) {
					bool moved = doc.MoveSlide(1, 0);
					Check(moved, path + std::string(": MoveSlide(1,0)"));
					Check(doc.SlideName(0) == "Added",
						path + std::string(": MoveSlide(1,0), SlideName(0)=='Added'"));
					bool badMove = doc.MoveSlide(0, 0);
					Check(!badMove, path + std::string(": MoveSlide(0,0) false"));
				}
			}

			// ---- 4. SlideAddLayer / SlideRemoveLayer ----
			{
				Document doc;
				Check(LoadDocument(path, doc, error),
					path + std::string(": fresh load for layer ops: ") + error);
				const GRPart* gr = doc.PresentationGraphics();
				// Out-of-range remove should fail
				Check(!doc.SlideRemoveLayer(0, 999),
					path + std::string(": SlideRemoveLayer(0,999) false"));
				if (gr && gr->layers.size() > 0 && doc.slides.size() > 0) {
					bool found = false;
					for (size_t s = 0; s < doc.slides.size() && !found; s++) {
						size_t viewIdx = doc.slides[s].view;
						if (viewIdx >= gr->views.size()) continue;
						const GRView& view = gr->views[viewIdx];
						auto slideInfos = doc.SlideLayers(s);
						// Find a view layer entry that is not in the slide
						for (size_t vi = 0; vi < view.layers.size() && !found; vi++) {
							if (view.layers[vi].flags[4]) continue; // already in slide
							size_t pl = view.layers[vi].layer;
							size_t beforeCount = slideInfos.size();
							bool added = doc.SlideAddLayer(s, pl, 0);
							Check(added, path + std::string(": SlideAddLayer(")
								+ std::to_string(s) + ","
								+ std::to_string(pl) + ",0)");
							if (added) {
								Check(doc.SlideLayers(s).size()
									== beforeCount + 1,
									path + std::string(
										": layer count +1 after add"));
								// position=0 → new layer at index 0
								Check(doc.SlideLayerPart(s, 0) == int(pl),
									path + std::string(
										": SlideLayerPart(s,0)==" )
									+ std::to_string(pl));
								Check(doc.SlideLayerIndex(s, pl) == 0,
									path + std::string(
										": SlideLayerIndex(s,")
										+ std::to_string(pl)
										+ ") == 0");
								// duplicate add fails
								Check(!doc.SlideAddLayer(s, pl, 0),
									path + std::string(
										": duplicate add false"));
								// remove the newly added layer (index 0)
								Check(doc.SlideRemoveLayer(s, 0),
									path + std::string(
										": SlideRemoveLayer(s,0)"));
								Check(doc.SlideLayers(s).size()
									== beforeCount,
									path + std::string(
										": layer count back"));
								found = true;
							}
						}
					}
				}
				// 2+ layers: insert at position 1, verify ordering, then remove
				if (gr && gr->layers.size() >= 2 && doc.slides.size() > 0) {
					Document doc2;
					Check(LoadDocument(path, doc2, error),
						path + std::string(": fresh load for 2+ layer test: ")
						+ error);
					size_t viewIdx = doc2.slides[0].view;
					if (viewIdx < gr->views.size()) {
						const GRView& view = gr->views[viewIdx];
						auto slideInfos = doc2.SlideLayers(0);
						size_t beforeCount = slideInfos.size();
						// Find a view layer entry that is not in the slide
						for (size_t vi = 0; vi < view.layers.size(); vi++) {
							if (view.layers[vi].flags[4]) continue;
							size_t pl = view.layers[vi].layer;
							int firstBefore = doc2.SlideLayerPart(0, 0);
							bool added = doc2.SlideAddLayer(0, pl, 1);
							Check(added,
								path + std::string(
									": SlideAddLayer(0," )
									+ std::to_string(pl) + ",1)");
							if (added) {
								// existing first layer stays at 0
								Check(doc2.SlideLayerPart(0, 0) == firstBefore,
									path + std::string(
										": SlideLayerPart(0,0) intact"));
								// new layer at position 1
								Check(doc2.SlideLayerPart(0, 1)
									== int(pl),
									path + std::string(
										": SlideLayerPart(0,1)==" )
									+ std::to_string(pl));
								// remove in reverse order
								Check(doc2.SlideRemoveLayer(0, 1),
									path + std::string(
										": SlideRemoveLayer(0,1)"));
								Check(doc2.SlideRemoveLayer(0, 0),
									path + std::string(
										": SlideRemoveLayer(0,0)"));
								break;
							}
						}
					}
				}
			}

			// ---- 5. SetCurrentSlide ----
			{
				Document doc;
				Check(LoadDocument(path, doc, error),
					path + std::string(": fresh load for SetCurrentSlide: ") + error);
				if (doc.slides.size() >= 2) {
					bool set = doc.SetCurrentSlide(1);
					Check(set, path + std::string(": SetCurrentSlide(1)"));
					Check(doc.currentSlide == 1,
						path + std::string(": currentSlide == 1"));
					// in slide 1's view: its layer infos have flags[0] true,
					// every other info flags[0] false
					const GRPart* gr = doc.PresentationGraphics();
					if (gr) {
						size_t viewIdx = doc.slides[1].view;
						Check(viewIdx < gr->views.size(),
							path + std::string(": slide 1 view valid"));
						if (viewIdx < gr->views.size()) {
							const GRView& view = gr->views[viewIdx];
							auto layers = doc.SlideLayers(1);
							// build set of slide layer indices in this view
							std::vector<bool> isSlideLayer(view.layers.size(), false);
							for (size_t li : layers) {
								if (li < view.layers.size())
									isSlideLayer[li] = true;
							}
							for (size_t j = 0; j < view.layers.size(); j++) {
								if (isSlideLayer[j]) {
									Check(view.layers[j].flags[0],
										path + std::string(", view layer ")
										+ std::to_string(j) + " flags[0] true (slide layer)");
								} else {
									Check(!view.layers[j].flags[0],
										path + std::string(", view layer ")
										+ std::to_string(j) + " flags[0] false (not slide)");
								}
							}
							// if it has one layer, that info's flags[1] is true
							// and GRPart.values[0] equals its part layer
							if (layers.size() == 1 && !layers.empty()) {
								size_t idx = layers[0];
								if (idx < view.layers.size()) {
									Check(view.layers[idx].flags[1],
										path + std::string(": single slide layer flags[1] true"));
									Check(gr->values[0] == view.layers[idx].layer,
										path + std::string(": values[0] == part layer"));
								}
							}
						}
					}
				}
			}

			// ---- 7. SetSlideTransition ----
			{
				Document doc;
				Check(LoadDocument(path, doc, error),
					path + std::string(": fresh load for transition: ") + error);
				if (doc.slides.size() > 0) {
					bool set = doc.SetSlideTransition(0, 3, 7);
					Check(set, path + std::string(": SetSlideTransition(0,3,7)"));
					bool same = doc.SetSlideTransition(0, 3, 7);
					Check(!same, path + std::string(": SetSlideTransition same again false"));
					Check(doc.slides[0].transition == 3,
						path + std::string(": transition == 3"));
					Check(doc.slides[0].options == 7,
						path + std::string(": options == 7"));
				}
			}

			// ---- 8. AddPresentationLayer ----
			{
				Document doc;
				Check(LoadDocument(path, doc, error),
					path + std::string(": fresh load for AddPresentationLayer: ") + error);
				const GRPart* gr = doc.PresentationGraphics();
				if (gr) {
					size_t oldCount = gr->layers.size();
					size_t it = doc.AddPresentationLayer("Extra");
					Check(it == oldCount,
						path + std::string(": AddPresentationLayer returns old count"));
					// every view has one more info, the new one with flags[4] false
					for (size_t v = 0; v < gr->views.size(); v++) {
						Check(gr->views[v].layers.size() == oldCount + 1,
							path + std::string(": view ") + std::to_string(v)
							+ " has one more layer info");
						const GRView::LayerInfo& li = gr->views[v].layers.back();
						Check(li.layer == it,
							path + std::string(": new layer info layer == ")
							+ std::to_string(it));
						Check(!li.flags[4],
							path + std::string(": new layer info flags[4] false"));
					}
					int uses = doc.PresentationLayerUses(it);
					Check(uses == 0, path + std::string(": PresentationLayerUses(it)==0"));
					// after SlideAddLayer(0, it, 99), uses == 1
					doc.SlideAddLayer(0, it, 99);
					uses = doc.PresentationLayerUses(it);
					Check(uses == 1,
						path + std::string(": after SlideAddLayer, uses==1"));
					bool renamed = doc.RenamePresentationLayer(it, "X");
					Check(renamed, path + std::string(": RenamePresentationLayer(it,'X')"));
					bool renamed2 = doc.RenamePresentationLayer(it, "X");
					Check(!renamed2,
						path + std::string(": RenamePresentationLayer again false"));
					bool deleted = doc.DeletePresentationLayer(it);
					Check(deleted, path + std::string(": DeletePresentationLayer(it)"));
					Check(gr->layers.size() == oldCount,
						path + std::string(": layer count back to old"));
				}
			}

			// ---- 9. RemoveSlide ----
			{
				Document doc;
				Check(LoadDocument(path, doc, error),
					path + std::string(": fresh load for RemoveSlide: ") + error);
				size_t startCount = doc.slides.size();
				// Remove until one is left
				while (doc.slides.size() > 1) {
					bool removed = doc.RemoveSlide(0);
					Check(removed, path + std::string(": RemoveSlide(0) while >1"));
				}
				Check(doc.slides.size() == 1,
					path + std::string(": one slide left"));
				bool lastRemove = doc.RemoveSlide(0);
				Check(!lastRemove,
					path + std::string(": RemoveSlide(0) with one slide left false"));
			}

			// ---- 10. WritePresentationBlock ----
			{
				Document doc;
				Check(LoadDocument(path, doc, error),
					path + std::string(": fresh load for WritePresentationBlock: ") + error);
				std::vector<uint8_t> block = doc.WritePresentationBlock();
				Check(block.size() >= 8, path + std::string(": block has header+footer"));
				if (block.size() >= 8) {
					// starts with 'prp+' as big-endian uint32
					Check(block[0] == 0x70 && block[1] == 0x72
						&& block[2] == 0x70 && block[3] == 0x2b,
						path + std::string(": starts with 'prp+'"));
					// ends with 'prp-'
					size_t n = block.size();
					Check(block[n-4] == 0x70 && block[n-3] == 0x72
						&& block[n-2] == 0x70 && block[n-1] == 0x2d,
						path + std::string(": ends with 'prp-'"));
					// size = 4 + 4*3 + 2 + 4 + slides*(4*4+1) + 4
					size_t expectedSize = 4 + 4*3 + 2 + 4
						+ doc.slides.size() * (4*4 + 1) + 4;
					Check(block.size() == expectedSize,
						path + std::string(": size == ")
						+ std::to_string(expectedSize) + " (got "
						+ std::to_string(block.size()) + ")");
				}
			}

			presCount++;
		}
	}

	printf("pres ok %d, failed %d\n", sOk, sFailed);
	return sFailed ? 1 : 0;
}
