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

} // namespace gobe
