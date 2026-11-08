// GIMPGlue.h

#include "BitmapAccessor.h"
#include "gimp.h"

extern BRect SelBounds;
// This is an object wrapper for BitmapAccessor to be used as the GIMP's abstract Image

class GIMPImage
{
public:
	GIMPImage(BitmapAccessor* bitmap, bool output = false);
	virtual ~GIMPImage();
	void Copy(GIMPImage* bitmap, BRect bounds = SelBounds);

	void CopyRow(const uchar* bits, int x1, int row, int count);
	void SetRow(const uchar* bits, int x1, int row, int count)		{CopyRow(bits, x1, row, count);}
	void GetRow(uchar* bits, int x, int y, int count) const;
	
	void GetCol(uchar* bits, int x, int y, int count) const;
	void SetCol(const uchar* bits, int x, int y, int count);

	void GetRect(uchar* bits, int x, int y, int w, int h) const;
	void SetRect(const uchar* bits, int x, int y, int w, int h) const;

	int Height() const { return (int) mBitmap->Bounds().Height();}
	int Width() const { return (int) mBitmap->Bounds().Width();}

	BitmapAccessor*		mBitmap;
	int32				mRowBytes;
	bool				mReadOnly;
	int32				bpp;
};

void gimp_progress_init(const char*);

bool gimp_progress_update(BInvoker*, float);
