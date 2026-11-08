// libprefs for the plug-ins (see libprefs.h). Public domain.
#include "libprefs.h"

#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>

#include <string>

struct PREFHandleRec {
	BPath folder;
};

struct PREFDataRec {
	BPath path;
	BMessage message;
};


status_t
PREFInit(const char* signature, PREFHandle* outHandle)
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path, true);
	if (status != B_OK)
		return status;
	path.Append(signature);
	create_directory(path.Path(), 0755);
	*outHandle = new PREFHandleRec;
	(*outHandle)->folder = path;
	return B_OK;
}


status_t
PREFShutdown(PREFHandle handle)
{
	delete handle;
	return B_OK;
}


status_t
PREFLoadSet(PREFHandle handle, const char* name, bool create, PREFData* outData)
{
	if (handle == NULL)
		return B_BAD_VALUE;
	BPath path(handle->folder.Path(), name);
	BFile file(path.Path(), B_READ_ONLY);
	PREFDataRec* data = new PREFDataRec;
	data->path = path;
	if (file.InitCheck() != B_OK || data->message.Unflatten(&file) != B_OK) {
		if (!create) {
			delete data;
			*outData = NULL;
			return B_ENTRY_NOT_FOUND;
		}
		data->message.MakeEmpty();
	}
	*outData = data;
	return B_OK;
}


status_t
PREFDisposeSet(PREFData* data)
{
	if (data != NULL) {
		delete *data;
		*data = NULL;
	}
	return B_OK;
}


status_t
PREFSaveSet(PREFData data)
{
	if (data == NULL)
		return B_BAD_VALUE;
	BFile file(data->path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();
	return data->message.Flatten(&file);
}


status_t
PREFGetData(PREFData data, const char* name, const void** outData, ssize_t* outSize,
	uint32* outType)
{
	if (data == NULL)
		return B_BAD_VALUE;
	type_code type;
	if (data->message.GetInfo(name, &type) != B_OK)
		return B_NAME_NOT_FOUND;
	if (outType != NULL)
		*outType = type;
	return data->message.FindData(name, type, outData, outSize);
}


status_t
PREFSetData(PREFData data, const char* name, const void* value, ssize_t size, uint32 type)
{
	if (data == NULL)
		return B_BAD_VALUE;
	data->message.RemoveName(name);
	return data->message.AddData(name, type, value, size);
}
