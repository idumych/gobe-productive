// A presentation's slides and their layers (TPartPR, TSlide and the
// TPR...Command classes of the Presentation part).
#include "Document.h"

#include <algorithm>
#include <cstdio>

#include "DocWriter.h"
#include "InlineParts.h"

namespace gobe {

namespace {

// TGRLayerInfo's flags (Layers.cpp): 0 visible, 1 editable, 4 in the slide
const int kVisible = 0, kEditable = 1, kInSlide = 4;

} // namespace


const GRPart*
Document::PresentationGraphics() const
{
	return presentationPart >= 0 ? GraphicsPart(size_t(presentationPart)) : nullptr;
}


std::string
Document::SlideName(size_t slide) const
{
	const GRPart* gr = PresentationGraphics();
	if (gr == nullptr || slide >= slides.size() || slides[slide].view >= gr->views.size())
		return std::string();
	return gr->views[slides[slide].view].name;
}


std::vector<size_t>
Document::SlideLayers(size_t slide) const
{
	std::vector<size_t> infos;
	const GRPart* gr = PresentationGraphics();
	if (gr == nullptr || slide >= slides.size() || slides[slide].view >= gr->views.size())
		return infos;
	const GRView& view = gr->views[slides[slide].view];
	for (size_t i = 0; i < view.layers.size(); i++)
		if (view.layers[i].flags[kInSlide])
			infos.push_back(i);
	return infos;
}


int
Document::SlideLayerPart(size_t slide, size_t layer) const
{
	std::vector<size_t> infos = SlideLayers(slide);
	if (layer >= infos.size())
		return -1;
	return int(PresentationGraphics()->views[slides[slide].view].layers[infos[layer]].layer);
}


int
Document::SlideLayerIndex(size_t slide, size_t partLayer) const
{
	std::vector<size_t> infos = SlideLayers(slide);
	for (size_t i = 0; i < infos.size(); i++)
		if (SlideLayerPart(slide, i) == int(partLayer))
			return int(i);
	return -1;
}


void
Document::SyncSlides()
{
	for (size_t k = 0; k < slides.size(); k++) {
		slides[k].name = SlideName(k);
		slides[k].layers = uint32_t(SlideLayers(k).size());
	}
}


bool
Document::SetCurrentSlide(size_t slide)
{
	if (presentationPart < 0 || slide >= slides.size())
		return false;
	size_t part = size_t(presentationPart);
	GRPart* gr = EditableGraphicsPart(part);
	if (gr == nullptr || slides[slide].view >= gr->views.size())
		return false;
	GRView& view = gr->views[slides[slide].view];
	for (auto& info : view.layers) {
		info.flags[kVisible] = false;
		info.flags[kEditable] = false;
	}
	std::vector<size_t> infos = SlideLayers(slide);
	for (size_t i = 0; i < infos.size(); i++) {
		GRView::LayerInfo& info = view.layers[infos[i]];
		info.flags[kVisible] = true;
		// the background (the first) isn't edited unless it's alone; the
		// last is the active layer
		if (i != 0 || infos.size() == 1) {
			info.flags[kEditable] = true;
			if (i + 1 == infos.size())
				gr->values[0] = info.layer;
		}
	}
	presentation.slide = uint32_t(slide);
	presentation.layer = uint32_t(-1);
	if (part < tails.size() && tailRead[part] && !tails[part].portals.empty()) {
		tails[part].portals[0].view = slides[slide].view;
		dirtyTails.insert(part);
	}
	ShowSlide(int(slide));
	return true;
}


bool
Document::ActivateSlideLayer(size_t slide, int layer)
{
	if (!SetCurrentSlide(slide))
		return false;
	// TPartPR::ActivateLayer: one layer chosen, it alone shown and edited;
	// -1, every layer shown, all but the background edited
	GRPart* gr = EditableGraphicsPart(size_t(presentationPart));
	GRView& view = gr->views[slides[slide].view];
	std::vector<size_t> infos = SlideLayers(slide);
	if (layer >= int(infos.size()))
		layer = -1;
	for (auto& info : view.layers) {
		info.flags[kVisible] = false;
		info.flags[kEditable] = false;
	}
	for (size_t i = 0; i < infos.size(); i++) {
		if (layer != -1 && int(i) != layer)
			continue;
		GRView::LayerInfo& info = view.layers[infos[i]];
		info.flags[kVisible] = true;
		info.flags[kEditable] = int(i) == layer || i != 0 || infos.size() == 1;
	}
	if (layer >= 0)
		gr->values[0] = view.layers[infos[size_t(layer)]].layer;
	else if (!infos.empty())
		gr->values[0] = view.layers[infos.back()].layer;
	presentation.layer = uint32_t(layer);
	ComputeHiddenFrames();
	return true;
}


size_t
Document::AddSlide(size_t at, const std::string& name)
{
	GRPart* gr = presentationPart >= 0 ? EditableGraphicsPart(size_t(presentationPart)) : nullptr;
	if (gr == nullptr)
		return size_t(-1);
	// TSlide::TSlide: a view of its own (TGRView: every layer, here none of
	// them in the slide, shown or editable)
	GRView view;
	view.name = name;
	for (size_t l = 0; l < gr->layers.size(); l++) {
		GRView::LayerInfo info{uint32_t(l), {false, false, false, true, false}};
		view.layers.push_back(info);
	}
	gr->views.push_back(view);
	Slide slide;
	slide.view = uint32_t(gr->views.size() - 1);
	slide.name = name;
	at = std::min(at, slides.size());
	slides.insert(slides.begin() + long(at), slide);
	return at;
}


bool
Document::SlideAddLayer(size_t slide, size_t partLayer, size_t position)
{
	GRPart* gr = presentationPart >= 0 ? EditableGraphicsPart(size_t(presentationPart)) : nullptr;
	if (gr == nullptr || slide >= slides.size() || slides[slide].view >= gr->views.size()
			|| partLayer >= gr->layers.size())
		return false;
	GRView& view = gr->views[slides[slide].view];
	int from = -1;
	for (size_t i = 0; i < view.layers.size(); i++)
		if (view.layers[i].layer == partLayer)
			from = int(i);
	if (from < 0 || view.layers[from].flags[kInSlide])
		return false;
	// TSlide::AddLayer: the info moved before the slide's layer at
	// `position` (or after its last one), then in the slide
	std::vector<size_t> infos = SlideLayers(slide);
	GRView::LayerInfo info = view.layers[from];
	view.layers.erase(view.layers.begin() + from);
	size_t to;
	if (position < infos.size()) {
		to = infos[position];
		if (size_t(from) < to)
			to--;
	} else if (infos.empty())
		to = 0;
	else {
		to = infos.back();
		if (size_t(from) < to)
			to--;
		to++;
	}
	to = std::min(to, view.layers.size());
	info.flags[kInSlide] = true;
	view.layers.insert(view.layers.begin() + long(to), info);
	slides[slide].layers++;
	return true;
}


bool
Document::SlideRemoveLayer(size_t slide, size_t layer)
{
	GRPart* gr = presentationPart >= 0 ? EditableGraphicsPart(size_t(presentationPart)) : nullptr;
	std::vector<size_t> infos = SlideLayers(slide);
	if (gr == nullptr || layer >= infos.size())
		return false;
	// TSlide::RemoveLayer: hidden and out of the slide
	GRView::LayerInfo& info = gr->views[slides[slide].view].layers[infos[layer]];
	info.flags[kVisible] = false;
	info.flags[kInSlide] = false;
	if (slides[slide].layers > 0)
		slides[slide].layers--;
	return true;
}


bool
Document::RemoveSlide(size_t slide)
{
	GRPart* gr = presentationPart >= 0 ? EditableGraphicsPart(size_t(presentationPart)) : nullptr;
	if (gr == nullptr || slide >= slides.size() || slides.size() < 2)
		return false;
	// ~TSlide: its view goes; the later views move down
	uint32_t view = slides[slide].view;
	slides.erase(slides.begin() + long(slide));
	if (view < gr->views.size()) {
		gr->views.erase(gr->views.begin() + view);
		for (Slide& s : slides)
			if (s.view > view)
				s.view--;
		size_t part = size_t(presentationPart);
		if (part < tails.size() && tailRead[part])
			for (Portal& portal : tails[part].portals)
				if (portal.view > view)
					portal.view--;
				else if (portal.view == view)
					portal.view = slides.empty() ? 0 : slides[0].view;
	}
	return true;
}


bool
Document::MoveSlide(size_t from, size_t to)
{
	if (presentationPart < 0 || from >= slides.size() || to >= slides.size() || from == to)
		return false;
	EditableGraphicsPart(size_t(presentationPart));
	Slide moving = slides[from];
	slides.erase(slides.begin() + long(from));
	slides.insert(slides.begin() + long(to), moving);
	return true;
}


bool
Document::RenameSlide(size_t slide, const std::string& name)
{
	if (name.empty() || slide >= slides.size() || SlideName(slide) == name)
		return false;
	GRPart* gr = EditableGraphicsPart(size_t(presentationPart));
	gr->views[slides[slide].view].name = name;
	slides[slide].name = name;
	return true;
}


bool
Document::SetSlideTransition(size_t slide, uint32_t type, uint32_t options)
{
	if (presentationPart < 0 || slide >= slides.size()
			|| (slides[slide].transition == type && slides[slide].options == options))
		return false;
	EditableGraphicsPart(size_t(presentationPart));
	slides[slide].transition = type;
	slides[slide].options = options;
	return true;
}


bool
Document::SetPresentationFlag(bool cursor, bool on)
{
	bool& flag = cursor ? presentation.cursor : presentation.autoStart;
	if (presentationPart < 0 || flag == on)
		return false;
	EditableGraphicsPart(size_t(presentationPart));
	flag = on;
	return true;
}


size_t
Document::AddPresentationLayer(const std::string& name)
{
	GRPart* gr = presentationPart >= 0 ? EditableGraphicsPart(size_t(presentationPart)) : nullptr;
	if (gr == nullptr)
		return size_t(-1);
	GRLayer layer;
	layer.name = name;
	gr->layers.push_back(layer);
	uint32_t index = uint32_t(gr->layers.size() - 1);
	// (in every view, in no slide)
	for (GRView& view : gr->views)
		view.layers.push_back({index, {false, false, false, true, false}});
	return index;
}


bool
Document::DeletePresentationLayer(size_t partLayer)
{
	GRPart* gr = presentationPart >= 0 ? EditableGraphicsPart(size_t(presentationPart)) : nullptr;
	if (gr == nullptr || partLayer >= gr->layers.size() || gr->layers.size() < 2)
		return false;
	for (size_t k = 0; k < slides.size(); k++) {
		int at = SlideLayerIndex(k, partLayer);
		if (at >= 0)
			SlideRemoveLayer(k, size_t(at));
	}
	gr->layers.erase(gr->layers.begin() + long(partLayer));
	for (GRView& view : gr->views) {
		for (size_t i = view.layers.size(); i-- > 0;)
			if (view.layers[i].layer == partLayer)
				view.layers.erase(view.layers.begin() + long(i));
			else if (view.layers[i].layer > partLayer)
				view.layers[i].layer--;
	}
	if (gr->values[0] >= gr->layers.size())
		gr->values[0] = 0;
	return true;
}


bool
Document::RenamePresentationLayer(size_t partLayer, const std::string& name)
{
	const GRPart* gr = PresentationGraphics();
	if (gr == nullptr || name.empty() || partLayer >= gr->layers.size()
			|| gr->layers[partLayer].name == name)
		return false;
	EditableGraphicsPart(size_t(presentationPart))->layers[partLayer].name = name;
	return true;
}


int
Document::PresentationLayerUses(size_t partLayer) const
{
	int uses = 0;
	for (size_t k = 0; k < slides.size(); k++)
		uses += SlideLayerIndex(k, partLayer) >= 0;
	return uses;
}


size_t
Document::NewSlide(size_t at, const std::string& name, const std::string& newLayer,
	bool shareBackground)
{
	// TPRNewSlideCommand::ActuallyDoIt: the slide; the previous slide's
	// background (its first layer) first, a new layer after
	size_t slide = AddSlide(at, name);
	if (slide == size_t(-1))
		return slide;
	if (shareBackground && slide > 0) {
		int background = SlideLayerPart(slide - 1, 0);
		if (background >= 0)
			SlideAddLayer(slide, size_t(background), 0);
	}
	if (!newLayer.empty()) {
		size_t layer = AddPresentationLayer(newLayer);
		SlideAddLayer(slide, layer, size_t(-1));
	}
	SetCurrentSlide(slide);
	return slide;
}


size_t
Document::DuplicateSlide(size_t source, const std::string& name, bool duplicateLayers,
	const std::string& newLayer, bool shareBackground, std::string& error)
{
	if (presentationPart < 0 || source >= slides.size()) {
		error = "no such slide";
		return size_t(-1);
	}
	// TPRDuplicateSlideCommand: after it, its transition; its layers, or
	// copies of them (the background kept, when shared), and a new layer
	std::vector<int> layers;
	for (size_t i = 0; i < SlideLayers(source).size(); i++)
		layers.push_back(SlideLayerPart(source, i));
	Slide original = slides[source];
	size_t slide = AddSlide(source + 1, name);
	if (slide == size_t(-1)) {
		error = "no presentation";
		return slide;
	}
	slides[slide].transition = original.transition;
	slides[slide].options = original.options;
	for (size_t i = 0; i < layers.size(); i++) {
		if (layers[i] < 0)
			continue;
		size_t layer = size_t(layers[i]);
		if (duplicateLayers && !(i == 0 && shareBackground)) {
			size_t copy = AddPresentationLayer(
				CopyName(PresentationGraphics()->layers[layer].name));
			if (!CopyLayerShapes(*this, size_t(presentationPart), layer, copy, error))
				return size_t(-1);
			layer = copy;
		}
		SlideAddLayer(slide, layer, size_t(-1));
	}
	if (!duplicateLayers && !newLayer.empty())
		SlideAddLayer(slide, AddPresentationLayer(newLayer), size_t(-1));
	SetCurrentSlide(slide);
	return slide;
}


bool
Document::NewSlideLayer(size_t slide, size_t after, const std::string& name)
{
	// TPRNewLayerCommand: after the layer chosen (TPartPR passes its index
	// + 1; a slide chosen, -1: the first), the active one
	if (slide >= slides.size() || name.empty())
		return false;
	size_t count = SlideLayers(slide).size();
	size_t layer = AddPresentationLayer(name);
	size_t position = after == size_t(-1) ? 0 : std::min(after + 1, count);
	SlideAddLayer(slide, layer, position);
	SetCurrentSlide(slide);
	ActivateSlideLayer(slide, int(position));
	return true;
}


bool
Document::DuplicateSlideLayer(size_t slide, size_t layer, std::string& error)
{
	// TPRDuplicateLayerCommand: a copy (CopyName) right after it
	int source = SlideLayerPart(slide, layer);
	if (source < 0) {
		error = "no such layer";
		return false;
	}
	size_t copy = AddPresentationLayer(
		CopyName(PresentationGraphics()->layers[size_t(source)].name));
	if (!CopyLayerShapes(*this, size_t(presentationPart), size_t(source), copy, error))
		return false;
	SlideAddLayer(slide, copy, layer + 1);
	SetCurrentSlide(slide);
	return true;
}


bool
Document::MoveSlideLayer(size_t slide, size_t from, size_t to)
{
	// TPRMoveLayerCommand: out of the slide and back at `to`
	int layer = SlideLayerPart(slide, from);
	if (layer < 0 || from == to || to > SlideLayers(slide).size())
		return false;
	SlideRemoveLayer(slide, from);
	SlideAddLayer(slide, size_t(layer), to);
	SetCurrentSlide(slide);
	return true;
}


std::vector<uint8_t>
Document::WritePresentationBlock() const
{
	// TPartPR::Write, TSlide::Write
	DocWriter w;
	w.Sig(Sig("prp+"));
	w.UInt32(uint32_t(currentSlide));
	w.UInt32(presentation.layer);
	w.UInt32(presentation.scroll);
	w.Bool(presentation.autoStart);
	w.Bool(presentation.cursor);
	w.UInt32(uint32_t(slides.size()));
	for (size_t k = 0; k < slides.size(); k++) {
		w.UInt32(slides[k].view);
		w.UInt32(uint32_t(SlideLayers(k).size()));
		w.UInt32(slides[k].transition);
		w.UInt32(slides[k].options);
		w.Bool(slides[k].collapsed);
	}
	w.Sig(Sig("prp-"));
	return w.Data();
}

} // namespace gobe
