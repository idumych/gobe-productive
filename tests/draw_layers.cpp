// draw_layers: Document views and layers management (ActiveLayer, ShownView,
// SetActiveLayer, ShowView, NewLayer, DeleteLayer, NewView, DeleteView,
// RenameLayer, RenameView, SetLayerFlag, CopyName), plus save/reload.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "Document.h"

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


// ---- CopyName tests (standalone free function) ----

static void
TestCopyName()
{
	// "Layer 1" -> "Layer 2"
	Check(CopyName("Layer 1") == "Layer 2", "CopyName Layer 1");
	// "View 9" -> "View 10"
	Check(CopyName("View 9") == "View 10", "CopyName View 9");
	// "Background" (no trailing digits) -> "Background 2"
	Check(CopyName("Background") == "Background 2", "CopyName Background");
	// "a" (single char, no trailing digits) -> "a 2"
	Check(CopyName("a") == "a 2", "CopyName a");
	// "Copy 2 of 3" -> "Copy 2 of 4" (trailing digits)
	Check(CopyName("Copy 2 of 3") == "Copy 2 of 4", "CopyName Copy 2 of 3");
	// "Layer  12" (double space) -> "Layer 13"
	Check(CopyName("Layer  12") == "Layer 13", "CopyName Layer  12");
	// Single character: too short for trailing number detection
	Check(CopyName("X") == "X 2", "CopyName single char");
	// Two chars with digit: "A1" -> "A 2"
	Check(CopyName("A1") == "A 2", "CopyName A1");
	// Already at 99
	Check(CopyName("Layer 99") == "Layer 100", "CopyName Layer 99");
}


// ---- Helper: create a blank drawing ----

static void
GetDrawing(Document& d, size_t& part, std::string& error)
{
	Check(CreateBlankDrawing(d, error), "CreateBlankDrawing " + error);
	part = size_t(d.MainPart());
}


// ---- ActiveLayer / SetActiveLayer ----

static void
TestActiveLayer(Document& d, size_t part, std::string& error)
{
	(void)error;
	// Default active layer is 0
	Check(d.ActiveLayer(part) == 0, "ActiveLayer default 0");

	// Out of range layer returns false (no change)
	Check(d.SetActiveLayer(part, 999) == false, "SetActiveLayer oob false");

	// Same layer returns false (no change)
	Check(d.SetActiveLayer(part, 0) == false, "SetActiveLayer same false");

	// Create a second layer, then set active to it
	d.NewLayer(part);
	Check(d.ActiveLayer(part) == 1, "ActiveLayer after NewLayer == 1");

	// SetActiveLayer(0) should return true (changes from 1 to 0)
	Check(d.SetActiveLayer(part, 0) == true, "SetActiveLayer to 0 true");
	Check(d.ActiveLayer(part) == 0, "ActiveLayer back to 0");
}


// ---- ShownView / ShowView ----

static void
TestShownView(Document& d, size_t part, std::string& error)
{
	(void)error;
	// Default shown view is 0
	Check(d.ShownView(part) == 0, "ShownView default 0");

	// Create a second view
	d.NewView(part);
	Check(d.ShownView(part) == 1, "ShownView after NewView == 1");

	// ShowView with invalid view returns false
	Check(d.ShowView(part, 999) == false, "ShowView oob false");

	// ShowView same view returns false
	Check(d.ShowView(part, 1) == false, "ShowView same false");

	// ShowView view 0 returns true (changes from 1 to 0)
	Check(d.ShowView(part, 0) == true, "ShowView to 0 true");
	Check(d.ShownView(part) == 0, "ShownView back to 0");
}


// ---- NewLayer ----

static void
TestNewLayer(Document& d, size_t part, std::string& error)
{
	(void)error;
	GRPart* gr = d.EditableGraphicsPart(part);
	// After CreateBlankDrawing: 1 layer ("Layer 1"), active 0
	Check(gr != nullptr && gr->layers.size() == 1, "NewLayer: 1 layer initially");

	// NewLayer adds "Layer 2"
	d.NewLayer(part);
	gr = d.EditableGraphicsPart(part);
	Check(gr != nullptr && gr->layers.size() == 2, "NewLayer: 2 layers");
	Check(gr->layers[1].name == "Layer 2", "NewLayer: name 'Layer 2'");
	Check(d.ActiveLayer(part) == 1, "NewLayer: active is new layer");

	// NewLayer adds "Layer 3"
	d.NewLayer(part);
	gr = d.EditableGraphicsPart(part);
	Check(gr != nullptr && gr->layers.size() == 3, "NewLayer: 3 layers");
	Check(gr->layers[2].name == "Layer 3", "NewLayer: name 'Layer 3'");
	Check(d.ActiveLayer(part) == 2, "NewLayer: active is new layer");

	// NewLayer adds "Layer 4", check flags {1,1,0,1,0} in the view shown
	d.NewLayer(part);
	gr = d.EditableGraphicsPart(part);
	Check(gr != nullptr && gr->layers.size() == 4, "NewLayer: 4 layers");
	Check(gr != nullptr, "NewLayer: EditableGraphicsPart not null");
	if (gr) {
		Check(gr->views.size() >= 1, "NewLayer: has views");
		if (gr->views.size() >= 1) {
			size_t lastLayer = 3;
			Check(gr->views[0].layers[lastLayer].flags[0], "NewLayer: flag 0 (visible)");
			// (made active: TPartGR::SetActiveLayer makes it the editable one,
			// as the original showed after New Layer)
			Check(gr->views[0].layers[lastLayer].flags[1], "NewLayer: flag 1 (editable)");
			Check(!gr->views[0].layers[lastLayer - 1].flags[1], "NewLayer: the old active not editable");
			Check(gr->views[0].layers[lastLayer].flags[2] == false, "NewLayer: flag 2 (not locked)");
			Check(gr->views[0].layers[lastLayer].flags[3], "NewLayer: flag 3 (printable)");
			Check(gr->views[0].layers[lastLayer].flags[4] == false, "NewLayer: flag 4");
		}
	}
}


// ---- DeleteLayer ----

static void
TestDeleteLayer(Document& d, size_t part, std::string& error)
{
	GRPart* gr = d.EditableGraphicsPart(part);
	// Need at least 2 layers
	// (We have 4 layers, so we can delete)
	Check(d.DeleteLayer(part, 2, error) == true, "DeleteLayer: delete layer 2");
	gr = d.EditableGraphicsPart(part);
	Check(gr != nullptr && gr->layers.size() == 3, "DeleteLayer: 3 layers after delete");

	// Layer 0 and 1 should still exist
	Check(gr->layers[0].name == "Layer 1", "DeleteLayer: Layer 1 still exists");
	Check(gr->layers[1].name == "Layer 2", "DeleteLayer: Layer 2 renumbered");

	// Check renumbering in views
	if (gr && gr->views.size() >= 1) {
		// After deleting original layer 2, original layer 3 became layer 2
		// and got renumbered from 3 to 2
		size_t count = 0;
		for (size_t i = 0; i < gr->views[0].layers.size(); i++) {
			if (gr->views[0].layers[i].layer == 2)
				count++;
		}
		Check(count == 1, "DeleteLayer: one layer info with layer==2");
	}

	// DeleteLayer with only 1 layer should fail
	// First, delete down to 1 layer
	gr = d.EditableGraphicsPart(part);
	while (gr && gr->layers.size() > 1) {
		size_t idx = gr->layers.size() - 1;
		std::string e2;
		d.DeleteLayer(part, idx, e2);
		gr = d.EditableGraphicsPart(part);
	}
	Check(gr != nullptr && gr->layers.size() == 1, "DeleteLayer: down to 1 layer");

	// Now deleting the last layer should fail
	std::string e3;
	Check(d.DeleteLayer(part, 0, e3) == false, "DeleteLayer: 1 layer fails");

	// Create 2 layers again for further tests
	d.NewLayer(part);
	gr = d.EditableGraphicsPart(part);
	Check(gr != nullptr && gr->layers.size() == 2, "DeleteLayer test setup: 2 layers");

	// Delete layer 0 (active is 1): active becomes 0
	d.DeleteLayer(part, 0, error);
	gr = d.EditableGraphicsPart(part);
	Check(d.ActiveLayer(part) == 0, "DeleteLayer(0): active becomes 0");
	Check(gr->layers[0].name == "Layer 2", "DeleteLayer(0): remaining layer named Layer 2");

	// Create 2 layers again
	d.NewLayer(part);
	gr = d.EditableGraphicsPart(part);
	Check(gr != nullptr && gr->layers.size() == 2, "DeleteLayer test setup: 2 layers");

	// Delete layer 1 (not 0, active is 1): active becomes 0
	d.DeleteLayer(part, 1, error);
	gr = d.EditableGraphicsPart(part);
	Check(d.ActiveLayer(part) == 0, "DeleteLayer(1): active becomes 0");
}


// ---- NewView ----

static void
TestNewView(Document& d, size_t part, std::string& error)
{
	(void)error;
	// After CreateBlankDrawing: 1 view ("View 1")
	GRPart* gr = d.EditableGraphicsPart(part);
	Check(gr != nullptr, "NewView: EditableGraphicsPart not null");
	if (gr) {
		Check(gr->views.size() == 1, "NewView: 1 view initially");
	}

	// NewView adds "View 2"
	d.NewView(part);
	if (gr) {
		Check(gr->views.size() == 2, "NewView: 2 views");
		Check(gr->views[1].name == "View 2", "NewView: name 'View 2'");
	}
	Check(d.ShownView(part) == 1, "NewView: shown is new view");

	// NewView adds "View 3"
	d.NewView(part);
	if (gr) {
		Check(gr->views.size() == 3, "NewView: 3 views");
		Check(gr->views[2].name == "View 3", "NewView: name 'View 3'");
	}

	// Check flags {1,0,0,1,0} for all layers in the new view
	if (gr && gr->layers.size() >= 1 && gr->views.size() >= 3) {
		for (size_t l = 0; l < gr->layers.size(); l++) {
			// Find the layer info for layer l in view 2
			bool found = false;
			for (size_t i = 0; i < gr->views[2].layers.size(); i++) {
				if (gr->views[2].layers[i].layer == l) {
					found = true;
					Check(gr->views[2].layers[i].flags[0], "NewView: flag 0 on layer " + std::to_string(l));
					Check(gr->views[2].layers[i].flags[1] == false, "NewView: flag 1 on layer " + std::to_string(l));
					Check(gr->views[2].layers[i].flags[2] == false, "NewView: flag 2 on layer " + std::to_string(l));
					Check(gr->views[2].layers[i].flags[3], "NewView: flag 3 on layer " + std::to_string(l));
					Check(gr->views[2].layers[i].flags[4] == false, "NewView: flag 4 on layer " + std::to_string(l));
				}
			}
			Check(found, "NewView: layer " + std::to_string(l) + " found in view 2");
		}
	}
}


// ---- DeleteView ----

static void
TestDeleteView(Document& d, size_t part, std::string& error)
{
	(void)error;
	GRPart* gr = d.EditableGraphicsPart(part);
	if (!gr)
		return;

	// Need at least 2 views
	// Ensure we have 3 views
	while (gr->views.size() < 3)
		d.NewView(part);
	Check(gr->views.size() >= 3, "DeleteView setup: 3+ views");

	// Delete view 1: portal shows view 0
	d.DeleteView(part, 1);
	Check(d.ShownView(part) == 0, "DeleteView(1): portal shows view 0");
	Check(gr->views.size() == 2, "DeleteView: 2 views after delete");

	// Delete view 0: portal shows view 1 (not view -1)
	d.DeleteView(part, 0);
	gr = d.EditableGraphicsPart(part);
	Check(d.ShownView(part) == 0, "DeleteView(0): portal shows 0");
	Check(gr->views.size() == 1, "DeleteView: 1 view after delete");

	// Only 1 view: DeleteView returns false
	Check(d.DeleteView(part, 0) == false, "DeleteView: 1 view fails");

	// Create 2 views again
	d.NewView(part);
	Check(gr->views.size() == 2, "DeleteView re-setup: 2 views");

	// Delete view 1: portal shows view 0
	d.DeleteView(part, 1);
	Check(d.ShownView(part) == 0, "DeleteView last: portal shows 0");
}


// ---- RenameLayer / RenameView ----

static void
TestRenameLayer(Document& d, size_t part, std::string& error)
{
	(void)error;
	GRPart* gr = d.EditableGraphicsPart(part);
	Check(gr != nullptr, "RenameLayer: EditableGraphicsPart not null");
	if (!gr)
		return;

	// Ensure at least 2 layers for rename tests
	while (gr->layers.size() < 2)
		d.NewLayer(part);

	// RenameLayer valid
	Check(d.RenameLayer(part, 0, "MyLayer") == true, "RenameLayer: valid rename");
	Check(gr->layers[0].name == "MyLayer", "RenameLayer: name changed");

	// RenameLayer empty name
	Check(d.RenameLayer(part, 0, "") == false, "RenameLayer: empty name false");

	// RenameLayer out of range
	Check(d.RenameLayer(part, 999, "X") == false, "RenameLayer: oob false");

	// RenameLayer same name
	Check(d.RenameLayer(part, 0, "MyLayer") == false, "RenameLayer: same name false");

	// RenameView valid
	Check(d.RenameView(part, 0, "MyView") == true, "RenameView: valid rename");
	Check(gr->views[0].name == "MyView", "RenameView: name changed");

	// RenameView empty name
	Check(d.RenameView(part, 0, "") == false, "RenameView: empty name false");

	// RenameView out of range
	Check(d.RenameView(part, 999, "X") == false, "RenameView: oob false");

	// RenameView same name
	Check(d.RenameView(part, 0, "MyView") == false, "RenameView: same name false");
}


// ---- SetLayerFlag ----

static void
TestSetLayerFlag(Document& d, size_t part, std::string& error)
{
	(void)error;
	GRPart* gr = d.EditableGraphicsPart(part);
	Check(gr != nullptr, "SetLayerFlag: EditableGraphicsPart not null");
	if (!gr || gr->views.size() == 0 || gr->layers.size() == 0)
		return;

	// Ensure we have at least 1 layer and 1 view
	// The current shown view should have layer info
	uint32_t sv = d.ShownView(part);
	if (sv >= gr->views.size() || gr->views[sv].layers.empty()) {
		d.NewView(part);
		sv = d.ShownView(part);
	}

	// SetLayerFlag valid: set flag 0 (visible)
	// First make sure the layer is visible to test setting it off
	Check(gr->views[sv].layers[0].flags[0] == true, "SetLayerFlag: flag 0 initially true");
	Check(d.SetLayerFlag(part, 0, 0, false) == true, "SetLayerFlag: set flag 0 off");
	Check(gr->views[sv].layers[0].flags[0] == false, "SetLayerFlag: flag 0 is now false");

	// SetLayerFlag same value returns false
	Check(d.SetLayerFlag(part, 0, 0, false) == false, "SetLayerFlag: same value false");

	// SetLayerFlag attr out of range
	Check(d.SetLayerFlag(part, 0, 5, true) == false, "SetLayerFlag: attr 5 false");
	Check(d.SetLayerFlag(part, 0, -1, true) == false, "SetLayerFlag: attr -1 false");

	// SetLayerFlag row out of range
	Check(d.SetLayerFlag(part, 999, 0, true) == false, "SetLayerFlag: row oob false");

	// SetLayerFlag attr 2 (locked) clears attr 1 (selectable)
	// First set flag 1 true
	d.SetLayerFlag(part, 0, 1, true);
	Check(gr->views[sv].layers[0].flags[1] == true, "SetLayerFlag: flag 1 set true");
	// Now set flag 2 (locked) on
	d.SetLayerFlag(part, 0, 2, true);
	Check(gr->views[sv].layers[0].flags[2] == true, "SetLayerFlag: flag 2 is true");
	Check(gr->views[sv].layers[0].flags[1] == false, "SetLayerFlag: flag 1 cleared by lock");
}


// ---- Save/Reload round-trip ----

static void
TestSaveReload(Document& d, size_t part, std::string& error)
{
	(void)d; (void)part; (void)error;
	// Build a doc with some layers and views
	Document d2;
	std::string e2;
	CreateBlankDrawing(d2, e2);
	size_t p2 = size_t(d2.MainPart());

	// Add a layer and view
	d2.NewLayer(p2);
	d2.NewView(p2);

	// Rename them
	d2.RenameLayer(p2, 0, "BG");
	d2.RenameLayer(p2, 1, "FG");
	d2.RenameView(p2, 0, "Default");
	d2.RenameView(p2, 1, "Draft");

	// Set active layer and shown view
	d2.SetActiveLayer(p2, 1);
	d2.ShowView(p2, 1);

	// Set a flag
	d2.SetLayerFlag(p2, 0, 0, false);

	// Save and reload
	const char* tmpdir = getenv("TMPDIR");
	if (!tmpdir)
		tmpdir = "/tmp";
	std::string savePath(tmpdir);
	savePath += "/drawlayers.gobe";

	Check(d2.Save(savePath, e2), "Save round-trip " + e2);

	Document d3;
	Check(LoadDocument(savePath, d3, e2), "Load round-trip " + e2);
	size_t p3 = size_t(d3.MainPart());

	// Check layers
	GRPart* gr3 = d3.EditableGraphicsPart(p3);
	Check(gr3 != nullptr, "Reload: GRPart not null");
	if (gr3) {
		Check(gr3->layers.size() == 2, "Reload: 2 layers");
		Check(gr3->layers[0].name == "BG", "Reload: layer 0 name");
		Check(gr3->layers[1].name == "FG", "Reload: layer 1 name");

		Check(gr3->views.size() == 2, "Reload: 2 views");
		Check(gr3->views[0].name == "Default", "Reload: view 0 name");
		Check(gr3->views[1].name == "Draft", "Reload: view 1 name");

		// Check active layer (values[0])
		Check(gr3->values[0] == 1, "Reload: active layer == 1");

		// Check shown view via portal
		Check(d3.ShownView(p3) == 1, "Reload: shown view == 1");

		// Check flag was saved: flag 0 of layer 0 in view 1 should be false
		if (gr3->views[1].layers.size() > 0) {
			bool found = false;
			for (size_t i = 0; i < gr3->views[1].layers.size(); i++) {
				if (gr3->views[1].layers[i].layer == 0) {
					found = true;
					Check(gr3->views[1].layers[i].flags[0] == false, "Reload: flag 0 saved false");
				}
			}
			Check(found, "Reload: layer 0 info found in view 1");
		}
	}

	remove(savePath.c_str());
}


int
main(int argc, char** argv)
{
	(void)argc; (void)argv;

	// ---- CopyName ----
	TestCopyName();

	// ---- ActiveLayer / SetActiveLayer ----
	{
		Document d;
		size_t part;
		std::string error;
		GetDrawing(d, part, error);
		TestActiveLayer(d, part, error);
	}

	// ---- ShownView / ShowView ----
	{
		Document d;
		size_t part;
		std::string error;
		GetDrawing(d, part, error);
		TestShownView(d, part, error);
	}

	// ---- NewLayer ----
	{
		Document d;
		size_t part;
		std::string error;
		GetDrawing(d, part, error);
		TestNewLayer(d, part, error);
	}

	// ---- DeleteLayer ----
	{
		Document d;
		size_t part;
		std::string error;
		GetDrawing(d, part, error);
		// Create enough layers for delete tests
		d.NewLayer(part);
		d.NewLayer(part);
		d.NewLayer(part);
		TestDeleteLayer(d, part, error);
	}

	// ---- NewView ----
	{
		Document d;
		size_t part;
		std::string error;
		GetDrawing(d, part, error);
		d.NewLayer(part);
		TestNewView(d, part, error);
	}

	// ---- DeleteView ----
	{
		Document d;
		size_t part;
		std::string error;
		GetDrawing(d, part, error);
		TestDeleteView(d, part, error);
	}

	// ---- RenameLayer / RenameView ----
	{
		Document d;
		size_t part;
		std::string error;
		GetDrawing(d, part, error);
		d.NewLayer(part);
		d.NewLayer(part);
		d.NewView(part);
		TestRenameLayer(d, part, error);
	}

	// ---- SetLayerFlag ----
	{
		Document d;
		size_t part;
		std::string error;
		GetDrawing(d, part, error);
		d.NewLayer(part);
		TestSetLayerFlag(d, part, error);
	}

	// ---- Save/Reload ----
	{
		Document d;
		size_t part;
		std::string error;
		GetDrawing(d, part, error);
		TestSaveReload(d, part, error);
	}

	printf("layers ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 || sOk == 0;
}
