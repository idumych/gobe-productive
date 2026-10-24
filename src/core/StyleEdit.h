// Format > Edit Styles…'s commands on a document's named styles: Modify
// Style… (TModifyStyleCommand), Clear Style (TDeleteStyleCommand) and a
// format command while a style is chosen there (TFormatCommand on the
// edit style).
#ifndef GOBE_STYLEEDIT_H
#define GOBE_STYLEEDIT_H

#include <cstdint>
#include <string>

#include "DocSections.h"

namespace gobe {

struct Document;

// TStyle flags (+8)
enum {
	kStyleDeleted = 0x400,		// cleared: kept, but not shown or found
	kStyleUserLock = 0x800		// the parts' own styles ("Body", ...)
};

// TStyleManager::FindNamedStyle: the style named `name` that isn't cleared,
// or 0xffffffff
uint32_t FindNamedStyle(const StyleMgr& styles, const std::string& name);
// TStyleManager::IsAncestor: `style` is `ancestor` or comes from it (its
// links)
bool IsStyleAncestor(const StyleMgr& styles, uint32_t ancestor, uint32_t style);
// TStyleID::UpdateDependents: what each style that comes from `style`
// (and it) inherits (props2), parents before children
void UpdateDependents(StyleMgr& styles, uint32_t style);

// TModifyStyleCommand::Perform: the named style `target` renamed `name`
// and based on the named style `base` ("" none). False (nothing changed)
// if there is no such style, the name is another's, the base doesn't
// exist or comes from the style, or nothing would change.
bool ModifyNamedStyle(Document& doc, const std::string& target, const std::string& name,
	const std::string& base);
// whether ModifyNamedStyle would change something
bool CanModifyNamedStyle(const StyleMgr& styles, const std::string& target,
	const std::string& name, const std::string& base);
// TStyleManager::CanDelete: no style comes from or is applied with it
bool CanDeleteStyle(const StyleMgr& styles, uint32_t style);
// TDeleteStyleCommand::Perform, TStyleManager::DeleteStyle: the named style
// cleared (kStyleDeleted). False if there's none or it can't be.
bool DeleteNamedStyle(Document& doc, const std::string& name);
// TFormatCommand::Perform on the edit style: `property` set in the style's
// own (replacing one it replaces), those equal to what it inherits dropped
// (TStyle::Reduce), its dependents updated. False if nothing changed.
bool SetStyleProperty(Document& doc, uint32_t style, const Property& property);

} // namespace gobe

#endif
