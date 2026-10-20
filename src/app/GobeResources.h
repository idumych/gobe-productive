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
	// Finds and loads the original binary: $GOBE_ORIGINAL, then
	// "Gobe Productive" next to this executable, then in an "original"
	// folder next to it. Returns false and fills error if none loads.
	static bool Init(BString& error);

	static const ResourceFile& File();
	static const BString& Path();
};

} // namespace gobe

#endif
