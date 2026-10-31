#include "TSharedDocument.h"

#include <Autolock.h>

#include <algorithm>
#include <map>

#include "TDocWindow.h"

namespace gobe {

namespace {

BLocker sRegistryLock("documents");
std::map<Document*, TSharedDocument*> sRegistry;

} // namespace


TSharedDocument*
TSharedDocument::Attach(Document* document, TDocWindow* window)
{
	BAutolock registry(sRegistryLock);
	TSharedDocument* shared = NULL;
	if (document != NULL) {
		auto found = sRegistry.find(document);
		if (found != sRegistry.end())
			shared = found->second;
	}
	if (shared == NULL) {
		shared = new TSharedDocument;
		shared->document = document;
		if (document != NULL)
			sRegistry[document] = shared;
	}
	BAutolock locker(shared->lock);
	shared->windows.push_back(window);
	return shared;
}


void
TSharedDocument::Detach(TSharedDocument* shared, TDocWindow* window)
{
	if (shared == NULL)
		return;
	BAutolock registry(sRegistryLock);
	bool last;
	{
		BAutolock locker(shared->lock);
		shared->windows.erase(std::remove(shared->windows.begin(), shared->windows.end(),
			window), shared->windows.end());
		last = shared->windows.empty();
		if (!last) {
			shared->TitlesChanged();
			return;
		}
	}
	if (shared->document != NULL)
		sRegistry.erase(shared->document);
	delete shared;
}


int
TSharedDocument::Number(const TDocWindow* window) const
{
	if (windows.size() < 2)
		return 0;
	auto at = std::find(windows.begin(), windows.end(), window);
	return at == windows.end() ? 0 : int(at - windows.begin()) + 1;
}


void
TSharedDocument::Changed(const TDocWindow* from)
{
	generation++;
	for (TDocWindow* window : windows)
		if (window != from)
			window->PostMessage(kMsgSharedDocumentChanged);
}


void
TSharedDocument::TitlesChanged()
{
	for (TDocWindow* window : windows)
		window->PostMessage(kMsgSharedTitles);
}

} // namespace gobe
