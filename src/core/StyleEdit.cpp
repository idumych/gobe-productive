#include "StyleEdit.h"

#include <algorithm>

#include "Document.h"
#include "Editing.h"
#include "Styles.h"

namespace gobe {

namespace {

const uint32_t kNone = 0xffffffff;


bool
Valid(const StyleMgr& styles, uint32_t style)
{
	return style != kNone && style < styles.styles.size();
}


// TProperty::Replaces: the same type; TPropParTab::Replaces: a tab at the
// same position
bool
Replaces(const Property& a, const Property& b)
{
	if (a.type != b.type)
		return false;
	if (a.type != Sig("PTab"))
		return true;
	TextStyle x, y;
	ApplyProperty(a, x);
	ApplyProperty(b, y);
	return !x.tabs.empty() && !y.tabs.empty() && x.tabs[0].position == y.tabs[0].position;
}


// TPropertyManager: the property's index, an equal one's if there is one
uint32_t
PropertyIndex(StyleMgr& styles, const Property& property)
{
	for (size_t i = 0; i < styles.properties.size(); i++)
		if (styles.properties[i].type == property.type
				&& styles.properties[i].data == property.data)
			return uint32_t(i);
	styles.properties.push_back(property);
	return uint32_t(styles.properties.size() - 1);
}


// the users count (value4) of a style gaining or losing a child
void
Retain(StyleMgr& styles, uint32_t style)
{
	if (Valid(styles, style))
		styles.styles[style].value4++;
}


void
Release(StyleMgr& styles, uint32_t style)
{
	if (Valid(styles, style) && styles.styles[style].value4 > 1)
		styles.styles[style].value4--;
}


int
Depth(const StyleMgr& styles, uint32_t style)
{
	int depth = 0;
	for (uint32_t s = style; Valid(styles, s) && depth < 64; s = styles.styles[s].links[0])
		depth++;
	return depth;
}

} // namespace


uint32_t
FindNamedStyle(const StyleMgr& styles, const std::string& name)
{
	if (name.empty())
		return kNone;
	for (size_t i = 0; i < styles.styles.size(); i++)
		if (styles.styles[i].name == name && !(styles.styles[i].value8 & kStyleDeleted))
			return uint32_t(i);
	return kNone;
}


bool
IsStyleAncestor(const StyleMgr& styles, uint32_t ancestor, uint32_t style)
{
	std::vector<uint32_t> todo = {style};
	std::vector<bool> seen(styles.styles.size(), false);
	while (!todo.empty()) {
		uint32_t s = todo.back();
		todo.pop_back();
		if (!Valid(styles, s) || seen[s])
			continue;
		if (s == ancestor)
			return true;
		seen[s] = true;
		for (uint32_t link : styles.styles[s].links)
			todo.push_back(link);
	}
	return false;
}


void
UpdateDependents(StyleMgr& styles, uint32_t style)
{
	if (!Valid(styles, style))
		return;
	std::vector<uint32_t> dependents;
	for (size_t i = 0; i < styles.styles.size(); i++)
		if (IsStyleAncestor(styles, style, uint32_t(i)))
			dependents.push_back(uint32_t(i));
	std::stable_sort(dependents.begin(), dependents.end(), [&](uint32_t a, uint32_t b) {
		return Depth(styles, a) < Depth(styles, b);
	});
	for (uint32_t s : dependents)
		styles.styles[s].props2 = StyleUnionSet(styles, styles.styles[s]);
}


bool
CanModifyNamedStyle(const StyleMgr& styles, const std::string& target, const std::string& name,
	const std::string& base)
{
	uint32_t style = FindNamedStyle(styles, target);
	if (!Valid(styles, style))
		return false;
	if (!name.empty() && name != target && Valid(styles, FindNamedStyle(styles, name)))
		return false;
	uint32_t parent = kNone;
	if (!base.empty()) {
		parent = FindNamedStyle(styles, base);
		if (!Valid(styles, parent) || IsStyleAncestor(styles, style, parent))
			return false;
	}
	uint32_t old = Valid(styles, styles.styles[style].links[0]) ? styles.styles[style].links[0]
		: kNone;
	return (!name.empty() && name != target) || old != parent;
}


bool
ModifyNamedStyle(Document& doc, const std::string& target, const std::string& name,
	const std::string& base)
{
	StyleMgr& styles = doc.styles;
	if (!CanModifyNamedStyle(styles, target, name, base))
		return false;
	uint32_t style = FindNamedStyle(styles, target);
	uint32_t parent = base.empty() ? kNone : FindNamedStyle(styles, base);
	// TModifyStyleCommand::Swap: the name, then the base, if they differ
	if (!name.empty() && name != target)
		styles.styles[style].name = name;
	uint32_t old = Valid(styles, styles.styles[style].links[0]) ? styles.styles[style].links[0]
		: kNone;
	if (old != parent) {
		Release(styles, old);
		styles.styles[style].links[0] = parent;
		Retain(styles, parent);
		UpdateDependents(styles, style);
	}
	doc.stylesChanged = true;
	return true;
}


bool
CanDeleteStyle(const StyleMgr& styles, uint32_t style)
{
	if (!Valid(styles, style))
		return false;
	for (const Style& s : styles.styles)
		for (uint32_t link : s.links)
			if (link == style)
				return false;
	return true;
}


bool
DeleteNamedStyle(Document& doc, const std::string& name)
{
	uint32_t style = FindNamedStyle(doc.styles, name);
	if (!CanDeleteStyle(doc.styles, style))
		return false;
	doc.styles.styles[style].value8 |= kStyleDeleted;
	doc.stylesChanged = true;
	return true;
}


bool
SetStyleProperty(Document& doc, uint32_t style, const Property& property)
{
	StyleMgr& styles = doc.styles;
	if (!Valid(styles, style))
		return false;
	uint32_t index = PropertyIndex(styles, property);
	std::vector<uint32_t> props = styles.styles[style].props;
	props.erase(std::remove_if(props.begin(), props.end(), [&](uint32_t p) {
			return p < styles.properties.size() && Replaces(property, styles.properties[p]);
		}), props.end());
	props.push_back(index);
	// TStyle::Reduce: an own property the same as the first inherited one
	// of its type goes
	const std::vector<uint32_t>& inherited = styles.styles[style].props2;
	props.erase(std::remove_if(props.begin(), props.end(), [&](uint32_t p) {
			if (p >= styles.properties.size())
				return false;
			for (uint32_t q : inherited)
				if (q < styles.properties.size()
						&& styles.properties[q].type == styles.properties[p].type)
					return styles.properties[p].data == styles.properties[q].data;
			return false;
		}), props.end());
	SortProps(styles, props);
	if (props == styles.styles[style].props)
		return false;
	styles.styles[style].props = props;
	UpdateDependents(styles, style);
	doc.stylesChanged = true;
	return true;
}



bool
RemoveStyleProperty(Document& doc, uint32_t style, const Property& property)
{
	StyleMgr& styles = doc.styles;
	if (!Valid(styles, style))
		return false;
	std::vector<uint32_t>& props = styles.styles[style].props;
	size_t before = props.size();
	props.erase(std::remove_if(props.begin(), props.end(), [&](uint32_t p) {
			return p < styles.properties.size() && Replaces(property, styles.properties[p]);
		}), props.end());
	if (props.size() == before)
		return false;
	UpdateDependents(styles, style);
	doc.stylesChanged = true;
	return true;
}



namespace {

void
ArchiveStyle(const StyleMgr& styles, uint32_t style, std::vector<bool>& archived,
	const std::vector<bool>& copied, std::vector<StyleDefinition>& out, int depth = 0)
{
	if (!Valid(styles, style) || archived[style] || depth > 64)
		return;
	const Style& s = styles.styles[style];
	ArchiveStyle(styles, s.links[0], archived, copied, out, depth + 1);
	ArchiveStyle(styles, s.links[1], archived, copied, out, depth + 1);
	StyleDefinition d;
	d.name = s.name;
	d.type = s.value10;
	d.level = s.valueC;
	d.flags = (s.value8 & ~uint32_t(0x300)) | (copied[style] ? kStyleCopied : 0);
	auto nameOf = [&](uint32_t link) {
		return Valid(styles, link) ? styles.styles[link].name : std::string();
	};
	d.base = nameOf(s.links[0]);
	d.parent = nameOf(s.links[1]);
	d.owner = nameOf(s.links[2]);
	for (uint32_t p : s.props)
		if (p < styles.properties.size())
			d.properties.push_back(styles.properties[p]);
	out.push_back(d);
	archived[style] = true;
}

} // namespace


std::vector<StyleDefinition>
CopyStyles(const StyleMgr& styles, const std::vector<std::string>& names)
{
	std::vector<StyleDefinition> out;
	std::vector<bool> archived(styles.styles.size(), false);
	std::vector<bool> copied(styles.styles.size(), false);
	// all of them marked first (TStyle::SetFlag 0x100), then archived
	// (ArchiveMarkedStyles): one that another comes from is marked too
	std::vector<uint32_t> chosen;
	for (const std::string& name : names) {
		uint32_t style = FindNamedStyle(styles, name);
		if (!Valid(styles, style))
			continue;
		copied[style] = true;
		chosen.push_back(style);
	}
	for (uint32_t style : chosen)
		ArchiveStyle(styles, style, archived, copied, out);
	return out;
}


bool
PasteStyles(Document& doc, const std::vector<StyleDefinition>& definitions)
{
	StyleMgr& styles = doc.styles;
	std::vector<uint32_t> changed;
	for (const StyleDefinition& d : definitions) {
		uint32_t existing = FindNamedStyle(styles, d.name);
		std::vector<uint32_t> props;
		for (const Property& p : d.properties)
			props.push_back(PropertyIndex(styles, p));
		SortProps(styles, props);
		if (Valid(styles, existing)) {
			// TStyleID::SetProperties
			if (!(d.flags & kStyleCopied) || styles.styles[existing].props == props)
				continue;
			styles.styles[existing].props = props;
			changed.push_back(existing);
			continue;
		}
		// TStyleManager::RestoreStyle, TStyle::Restore: its links by name
		Style style;
		style.id = styles.styles.empty() ? 0 : styles.styles[0].id;
		style.value8 = d.flags & ~uint32_t(0x300);
		style.valueC = d.level;
		style.value10 = d.type;
		style.name = d.name;
		bool found = true;
		const std::string* names[3] = {&d.base, &d.parent, &d.owner};
		for (int i = 0; i < 3; i++) {
			style.links[i] = kNone;
			if (names[i]->empty())
				continue;
			style.links[i] = FindNamedStyle(styles, *names[i]);
			found = found && Valid(styles, style.links[i]);
		}
		if (!found)
			continue;
		style.props = props;
		style.props2 = StyleUnionSet(styles, style);
		style.value4 = 1;
		for (uint32_t link : style.links)
			Retain(styles, link);
		styles.styles.push_back(style);
		changed.push_back(uint32_t(styles.styles.size() - 1));
	}
	for (uint32_t style : changed)
		UpdateDependents(styles, style);
	if (!changed.empty())
		doc.stylesChanged = true;
	return !changed.empty();
}



namespace {

bool
SameStyle(const Style& a, const Style& b)
{
	return a.id == b.id && a.value8 == b.value8 && a.valueC == b.valueC
		&& a.value10 == b.value10 && a.name == b.name && a.props == b.props
		&& a.props2 == b.props2 && a.substyles.empty() && b.substyles.empty()
		&& std::equal(a.links, a.links + 3, b.links);
}


} // namespace


uint32_t
StyleWithAppliedStyle(StyleMgr& styles, uint32_t style, uint32_t named, bool unapply)
{
	if (!Valid(styles, style) || !Valid(styles, named))
		return style;
	Style copy = styles.styles[style];
	uint32_t applied = named;
	if (unapply) {
		// TFormatCommand::UnApplyStyle: only the style applied
		if (copy.links[1] != named)
			return style;
		applied = kNone;
	} else if (IsStyleAncestor(styles, style, named) && named != style) {
		// (TStyle::SetParentStyle: not a style that comes from this one)
		return style;
	}
	if (!copy.name.empty()) {
		// TStyleID::BeginModify of a named style: an unnamed child of it
		copy.name.clear();
		copy.value8 = 32;
		copy.props.clear();
		copy.links[0] = style;
		copy.links[1] = copy.links[2] = kNone;
	}
	if (Valid(styles, applied)) {
		// TStyle::SetParentStyle: own properties the applied style (its own
		// or inherited ones) replaces go
		const Style& a = styles.styles[applied];
		copy.props.erase(std::remove_if(copy.props.begin(), copy.props.end(), [&](uint32_t p) {
				if (p >= styles.properties.size())
					return false;
				for (const std::vector<uint32_t>* set : {&a.props2, &a.props})
					for (uint32_t q : *set)
						if (q < styles.properties.size()
								&& Replaces(styles.properties[q], styles.properties[p]))
							return true;
				return false;
			}), copy.props.end());
	}
	copy.links[1] = applied;
	copy.props2 = StyleUnionSet(styles, copy);
	copy.substyles.clear();
	if (SameStyle(copy, styles.styles[style]))
		return style;
	for (size_t i = 0; i < styles.styles.size(); i++)
		if (SameStyle(styles.styles[i], copy))
			return uint32_t(i);
	copy.value4 = 1;
	for (uint32_t link : copy.links)
		Retain(styles, link);
	styles.styles.push_back(copy);
	return uint32_t(styles.styles.size() - 1);
}


bool
ApplyCharacterStyle(Document& doc, size_t part, TextPosition from, TextPosition to,
	uint32_t named, bool unapply)
{
	StyleMgr& styles = doc.styles;
	if (!Valid(styles, named))
		return false;
	if (to.paragraph < from.paragraph
			|| (to.paragraph == from.paragraph && to.offset < from.offset))
		std::swap(from, to);
	WPPart* wp = doc.EditableWordPart(part);
	if (wp == nullptr || from.paragraph >= wp->paragraphs.size())
		return false;
	to.paragraph = std::min(to.paragraph, wp->paragraphs.size() - 1);
	bool changed = false;
	for (size_t k = from.paragraph; k <= to.paragraph; k++) {
		WPParagraph& p = wp->paragraphs[k];
		if (p.runs.empty())
			continue;
		size_t length = EditableLength(p);
		size_t a = k == from.paragraph ? std::min(from.offset, length) : 0;
		size_t b = k == to.paragraph ? std::min(to.offset, length) : length;
		if (a >= b && !(a == 0 && length == 0))
			continue;
		// runs starting at a and b
		for (size_t at : {a, b}) {
			if (at >= length && at != 0)
				continue;
			size_t r = 0;
			for (size_t i = 0; i < p.runs.size(); i++)
				if (p.runs[i].position <= at)
					r = i;
			if (p.runs[r].position != at) {
				p.runs.insert(p.runs.begin() + r + 1, {uint32_t(at), p.runs[r].style});
				Retain(styles, p.runs[r].style);
			}
		}
		for (WPStyleRun& run : p.runs) {
			if (run.position < a || (run.position >= b && b < length))
				continue;
			uint32_t style = StyleWithAppliedStyle(styles, run.style, named, unapply);
			if (style != run.style) {
				Retain(styles, style);
				Release(styles, run.style);
				run.style = style;
				changed = true;
			}
		}
		// neighbours alike: one run
		for (size_t i = 1; i < p.runs.size();) {
			if (p.runs[i].style == p.runs[i - 1].style) {
				Release(styles, p.runs[i].style);
				p.runs.erase(p.runs.begin() + i);
			} else
				i++;
		}
	}
	if (changed)
		doc.stylesChanged = true;
	return changed;
}

} // namespace gobe
