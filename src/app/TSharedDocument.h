// What the windows of one document share (TDoc's window table, its command
// history and saved state): Window > New Window opens another TDocWindow
// on the same Document. Each BWindow has its own thread, so a window holds
// `lock` while it handles a message.
#ifndef GOBE_TSHAREDDOCUMENT_H
#define GOBE_TSHAREDDOCUMENT_H

#include <Locker.h>

#include <vector>

#include "Undo.h"

namespace gobe {

struct Document;
class TDocWindow;

struct TSharedDocument {
	Document* document = NULL;
	UndoStack undo;				// one history for all the windows
	BLocker lock;				// held while a window uses the document
	bool modified = false;		// changed since saved (" *")
	bool backedUp = false;		// the "~" backup made this session
	std::vector<TDocWindow*> windows;	// TDoc::AddWindow's order
	// bumped when a window changed the document; a window that sees it
	// moved lays its view out again
	uint32 generation = 0;

	// the document's (made with the first window, `document` may be NULL:
	// an empty window, not shared)
	static TSharedDocument* Attach(Document* document, TDocWindow* window);
	// the window closed; the object goes with the last
	static void Detach(TSharedDocument* shared, TDocWindow* window);
	// the window's number among the document's (from 1), 0 if alone
	int Number(const TDocWindow* window) const;
	// a window changed the document: the generation moves on and the other
	// windows are told ('Gdoc')
	void Changed(const TDocWindow* from);
	// the saved state changed: every window's title ('Gttl')
	void TitlesChanged();
};

// sent to a window when another window of its document changed it, and
// when the titles change (modified, saved, a window opened or closed)
static const uint32 kMsgSharedDocumentChanged = 'Gdoc';
static const uint32 kMsgSharedTitles = 'Gttl';

} // namespace gobe

#endif
