// Access to the resources of the user's original Gobe Productive binary,
// which the port reads at runtime (nothing from it is in the source tree).
#ifndef GOBE_RESOURCES_H
#define GOBE_RESOURCES_H

#include <String.h>

#include "ResourceFile.h"

namespace gobe {

// Gobe stores menu values under this name (ValueString, set to "value" by a
// static initializer in the original).
extern const char* const kValueString;

class GobeResources {
public:
	// Finds and loads the original binary: $GOBE_ORIGINAL, where it was
	// found before, next to this executable (or in "original" there), the
	// usual install folders, then a file named "Gobe Productive" on any
	// volume. Returns false and fills error if none loads.
	static bool Init(BString& error);
	// loads the binary at `path` (false, with its error, if it doesn't);
	// Remember keeps where it is for the next start
	static bool Use(const char* path, BString& error);
	static void Remember();

	static const ResourceFile& File();
	static const BString& Path();
	// GetIndexString: string `index` (from 0) of STR# `id`
	static BString IndexString(int32 id, int32 index);
};

} // namespace gobe

#endif
