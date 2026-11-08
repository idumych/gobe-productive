// libprefs (Jon Watte's preference library for BeOS) as the plug-ins use
// it: named sets of typed data under an application signature, kept here
// as a flattened BMessage per set in the user's settings folder. Written
// for the Gobe Productive port; public domain.
#ifndef GOBE_PLUGINS_LIBPREFS_H
#define GOBE_PLUGINS_LIBPREFS_H

#include <SupportDefs.h>

#ifndef P_IMPEXP
#define P_IMPEXP
#endif

typedef struct PREFHandleRec* PREFHandle;
typedef struct PREFDataRec* PREFData;

#ifdef __cplusplus
extern "C" {
#endif

// all return B_OK (0) on success
P_IMPEXP status_t PREFInit(const char* signature, PREFHandle* outHandle);
P_IMPEXP status_t PREFShutdown(PREFHandle handle);
P_IMPEXP status_t PREFLoadSet(PREFHandle handle, const char* name, bool create, PREFData* outData);
P_IMPEXP status_t PREFDisposeSet(PREFData* data);
P_IMPEXP status_t PREFSaveSet(PREFData data);
P_IMPEXP status_t PREFGetData(PREFData data, const char* name, const void** outData,
	ssize_t* outSize, uint32* outType);
P_IMPEXP status_t PREFSetData(PREFData data, const char* name, const void* value, ssize_t size,
	uint32 type);

#ifdef __cplusplus
}
#endif

#endif
