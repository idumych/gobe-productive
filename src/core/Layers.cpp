// Views and layers management for Document (TDialogLayers, TPartGR commands).
#include "Document.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ---- CopyName ----

namespace gobe {

std::string
CopyName(const std::string& name)
{
	size_t n = name.size();
	if (n < 2)
		return name + " 2";
	size_t p = n - 1;
	while (p > 0 && (name[p] >= '0' && name[p] <= '9' || name[p] == ' '))
		p--;
	p++;
	if (p > 0 && p < n) {
		const char* s = name.c_str() + p;
		int k;
		if (sscanf(s, " %d", &k) == 1) {
			std::string base = name.substr(0, p);
			char buf[32];
			snprintf(buf, sizeof buf, "%s %d", base.c_str(), k + 1);
			return std::string(buf);
		}
	}
	return name + " 2";
}

// ---- ActiveLayer / ShownView ----

size_t
Document::ActiveLayer(size_t part) const
{
	const GRPart* gr = GraphicsPart(part);
	if (gr == nullptr)
		return 0;
	size_t v = gr->values[0];
	if (v >= gr->layers.size())
		return 0;
	return v;
}

uint32_t
Document::ShownView(size_t part) const
{
	if (part >= tails.size() || !tailRead[part]
		|| tails[part].portals.empty())
		return 0;
	uint32_t v = tails[part].portals[0].view;
	auto over = viewOverride.find(part);
	if (over != viewOverride.end())
		v = over->second;
	return v;
}

// ---- SetActiveLayer ----

namespace {

// TPartGR::SetActiveLayer: the active layer, and in the view shown only
// it editable (flag 1) of the layers that aren't locked (flag 2)
void
MakeActive(GRPart& gr, uint32_t view, size_t layer)
{
	gr.values[0] = uint32_t(layer);
	if (view < gr.views.size())
		for (GRView::LayerInfo& info : gr.views[view].layers)
			if (!info.flags[2])
				info.flags[1] = info.layer == layer;
}

} // namespace


bool
Document::SetActiveLayer(size_t part, size_t layer)
{
	const GRPart* gr = GraphicsPart(part);
	if (gr == nullptr || layer >= gr->layers.size() || layer == gr->values[0])
		return false;
	MakeActive(*EditableGraphicsPart(part), ShownView(part), layer);
	return true;
}

// ---- ShowView ----

bool
Document::ShowView(size_t part, uint32_t view)
{
	const GRPart* gr = GraphicsPart(part);
	if (gr == nullptr)
		return false;
	if (view >= gr->views.size())
		return false;
	// Check portal conditions
	if (part >= tails.size() || !tailRead[part]
		|| tails[part].portals.empty())
		return false;
	uint32_t current = tails[part].portals[0].view;
	auto over = viewOverride.find(part);
	if (over != viewOverride.end())
		current = over->second;
	if (view == current)
		return false;
	tails[part].portals[0].view = view;
	dirtyTails.insert(part);
	return true;
}

// ---- NewLayer ----

bool
Document::NewLayer(size_t part)
{
	GRPart* gr = EditableGraphicsPart(part);
	if (gr == nullptr)
		return false;

	std::string name;
	if (!gr->layers.empty())
		name = CopyName(gr->layers.back().name);
	else
		name = "Layer 1";

	GRLayer layer;
	layer.name = name;
	// empty shapes

	size_t newIndex = gr->layers.size();
	gr->layers.push_back(std::move(layer));

	// Add to every view with flags {1,0,0,1,0}
	for (size_t v = 0; v < gr->views.size(); v++) {
		GRView::LayerInfo info;
		info.layer = newIndex;
		info.flags[0] = true;
		info.flags[1] = false;
		info.flags[2] = false;
		info.flags[3] = true;
		info.flags[4] = false;
		gr->views[v].layers.push_back(info);
	}

	MakeActive(*gr, ShownView(part), newIndex);
	return true;
}

// ---- DeleteLayer ----

bool
Document::DeleteLayer(size_t part, size_t layer, std::string& error)
{
	const GRPart* gr = GraphicsPart(part);
	if (gr == nullptr || layer >= gr->layers.size() || gr->layers.size() < 2)
		return false;
	// (frames placed by a layer's FRAM shapes can't be deleted yet)
	for (const Shape& s : gr->layers[layer].shapes)
		if (s.kind == Sig("FRAM")) {
			error = "This layer holds frames or groups, which the port can't delete yet.";
			return false;
		}
	GRPart* writable = EditableGraphicsPart(part);
	writable->layers.erase(writable->layers.begin() + long(layer));
	for (GRView& view : writable->views) {
		auto& infos = view.layers;
		for (size_t i = 0; i < infos.size(); ) {
			if (infos[i].layer == layer)
				infos.erase(infos.begin() + long(i));
			else {
				if (infos[i].layer > layer)
					infos[i].layer--;
				i++;
			}
		}
	}
	// the layer before it active, or the first
	MakeActive(*writable, ShownView(part), layer > 0 ? layer - 1 : 0);
	return true;
}

// ---- NewView ----

bool
Document::NewView(size_t part)
{
	if (GraphicsPart(part) == nullptr || part >= tails.size() || !tailRead[part]
			|| tails[part].portals.empty())
		return false;
	GRPart* gr = EditableGraphicsPart(part);

	std::string name;
	if (!gr->views.empty())
		name = CopyName(gr->views.back().name);
	else
		name = "View 1";

	GRView view;
	view.name = name;

	// Every layer in it with flags {1,0,0,1,0}
	for (size_t l = 0; l < gr->layers.size(); l++) {
		GRView::LayerInfo info;
		info.layer = l;
		info.flags[0] = true;
		info.flags[1] = false;
		info.flags[2] = false;
		info.flags[3] = true;
		info.flags[4] = false;
		view.layers.push_back(info);
	}

	size_t newIndex = gr->views.size();
	gr->views.push_back(std::move(view));

	tails[part].portals[0].view = uint32_t(newIndex);
	dirtyTails.insert(part);
	return true;
}

// ---- DeleteView ----

bool
Document::DeleteView(size_t part, uint32_t view)
{
	const GRPart* gr = GraphicsPart(part);
	if (gr == nullptr)
		return false;
	if (gr->views.size() < 2)
		return false;

	GRPart* writable = EditableGraphicsPart(part);
	if (writable == nullptr)
		return false;

	if (view >= writable->views.size())
		return false;

	// the portal shows view - 1, or the view after view 0 (then 0)
	uint32_t newView = view == 0 ? 0 : view - 1;
	if (part >= tails.size() || !tailRead[part] || tails[part].portals.empty())
		return false;

	// Remove the view
	writable->views.erase(writable->views.begin() + long(view));

	// Clamp newView to remaining views
	if (newView >= writable->views.size())
		newView = uint32_t(writable->views.size() - 1);

	tails[part].portals[0].view = newView;
	dirtyTails.insert(part);
	return true;
}

// ---- RenameLayer ----

bool
Document::RenameLayer(size_t part, size_t layer, const std::string& name)
{
	if (name.empty())
		return false;

	const GRPart* gr = GraphicsPart(part);
	if (gr == nullptr || layer >= gr->layers.size())
		return false;
	if (gr->layers[layer].name == name)
		return false;

	GRPart* writable = EditableGraphicsPart(part);
	if (writable == nullptr)
		return false;

	writable->layers[layer].name = name;
	dirtyParts.insert(part);
	return true;
}

// ---- RenameView ----

bool
Document::RenameView(size_t part, uint32_t view, const std::string& name)
{
	if (name.empty())
		return false;

	const GRPart* gr = GraphicsPart(part);
	if (gr == nullptr || view >= gr->views.size())
		return false;
	if (gr->views[view].name == name)
		return false;

	GRPart* writable = EditableGraphicsPart(part);
	if (writable == nullptr)
		return false;

	writable->views[view].name = name;
	dirtyParts.insert(part);
	return true;
}

// ---- SetLayerFlag ----

bool
Document::SetLayerFlag(size_t part, size_t row, int attr, bool on)
{
	if (attr < 0 || attr > 4)
		return false;

	const GRPart* gr = GraphicsPart(part);
	if (gr == nullptr)
		return false;

	uint32_t v = ShownView(part);
	if (v >= gr->views.size())
		return false;

	// Check portal conditions for showing
	if (part >= tails.size() || !tailRead[part]
		|| tails[part].portals.empty())
		return false;

	if (row >= gr->views[v].layers.size())
		return false;

	if (gr->views[v].layers[row].flags[attr] == on)
		return false;
	GRView::LayerInfo& info = EditableGraphicsPart(part)->views[v].layers[row];
	info.flags[attr] = on;
	// TGRLayerSetAttrCommand: locking or unlocking also clears flag 1
	// (editable)
	if (attr == 2)
		info.flags[1] = false;
	return true;
}
} // namespace gobe
