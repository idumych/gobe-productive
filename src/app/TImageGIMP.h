// The GIMP filters (Parts/ImageProcessing: GIMPPlugin, GIMPDialog,
// GIMPImage): programs built against the GIMP 0.54 plug-in library, run on
// pipes ("-gimp <read> <write> <info>"), handed the image in shared areas,
// their dialogs built from the items they ask for, their settings kept
// between runs.
#ifndef GOBE_TIMAGEGIMP_H
#define GOBE_TIMAGEGIMP_H

#include <Messenger.h>
#include <OS.h>
#include <Rect.h>
#include <String.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "IMPart.h"

class BWindow;

namespace gobe {

struct ImagePlugin;

// the GIMP folders' filters (GIMPPlugin::GIMPDir), as plug-ins of kind 0
void AddGIMPPlugins(std::vector<ImagePlugin>& plugins);
// GIMPPlugin::About: its name, author, address and description
void GIMPAbout(const ImagePlugin& plugin);

class TGIMPDialog;

// a filter running on an image: its process, the pipes, the images it was
// given (1 the input, 2 the output) and its dialogs
class GIMPRun {
public:
	GIMPRun(BMessenger view, const ImagePlugin& plugin, const ArchiveBitmap& bits,
		IMRect selection, BWindow* window, const uint8 colors[2][4]);
	~GIMPRun();

	// GIMPPlugin's constructor: the pipes, the program started, the
	// thread that answers it
	status_t Start();
	// what the view asked for: a callback of one of its dialogs' items
	void SendCallback(int32 dialog, int32 item, const void* data, size_t size);
	void About();

	enum {
		kMsgUpdate = 'IMgu',	// "bits" (ArchiveBitmap*): the output so far
		kMsgDone = 'IMgd'		// the filter quit
	};

private:
	static int32 Thread(void* data);
	void Run();
	bool ReadAll(void* buffer, size_t size);
	void Reply(int32 type, size_t size, const void* data);
	void Ack(int32 type, size_t size, const void* data);
	void HandleImage(int32 type, void* data);
	void HandleUpdate(void* data);
	void HandleParams(void* data);
	void HandleProgress(void* data);
	void HandleDialog(void* data);
	area_id PackedArea(bool output);
	void Close();

	BMessenger fView;
	BString fName, fPath, fDescription, fAuthor, fEmail;
	ArchiveBitmap fBits;
	IMRect fSelection;
	BWindow* fWindow;			// the document's (the dialogs' subset)
	BRect fWindowFrame;
	BString fDocumentName;
	uint8 fColors[2][4];
	int fToPlugin[2] = {-1, -1}, fFromPlugin[2] = {-1, -1}, fInfo[2] = {-1, -1};
	thread_id fThread = -1;
	thread_id fTeam = -1;
	std::atomic<bool> fQuit{false};
	area_id fAreas[2] = {-1, -1};
	void* fAreaBits[2] = {NULL, NULL};
	std::mutex fWriteLock;
	std::map<int32, TGIMPDialog*> fDialogs;
	int32 fNextDialog = 1;
	class TStatusDialog* fStatus = NULL;
};

} // namespace gobe

#endif
