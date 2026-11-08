/*
 * unsharp.c 0.5 -- This is a plug-in for the GIMP 1.0
 *  http://www.steppe.com/~winston/gimp/unsharp.html
 *
 *
 * Copyright (C) 1999 Winston Chang
 *                    <wchang3@students.wisc.edu>
 *                    <winston@steppe.com>
 */

/* This file has been modified from the original; converted to the ImageManip API */
/* Scott Lindsey 8/1/99 */

 
/* This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */


#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <algobase.h>
#include <malloc.h>


#include "ImageManipAddon.h"
#include "BitmapAccessor.h"
#include "Config.h"
#include "GIMPGlue.h"

const char addonName[] = "Unsharp Mask";
const char addonInfo[] = "An unsharp mask filter by Winston Chang; Ported from the GIMP by Scott Lindsey.";
const char addonCategory[] = "Sharpen";
int32 addonVersion = 100;

/* to show both pretty unoptimized code and ugly optimized code blocks
   There's really no reason to define this, unless you want to see how
   much pointer aritmetic can speed things up.  I find that it is about
   45% faster with the optimized code. */
// BOGUS - with my compiler, it's the opposite.
//#define READABLE_CODE

/* local function prototypes */
static inline void blur_line(double* ctable, double* cmatrix, int cmatrix_length,
                             uchar* cur_col, uchar* dest_col,
                             int y, int32 bytes);
static int gen_convolve_matrix(double std_dev, double** cmatrix);
static double* gen_lookup_table(double* cmatrix, int cmatrix_length);

static inline int roundit(double i);

static status_t unsharp_region (GIMPImage* srcPTR, GIMPImage* dstPTR,
                   int width, int height, int bytes,
				   double radius, double amount, int threshold,
                   int x1, int x2, int y1, int y2,
					bool multithread, BInvoker* prog);

static status_t unsharp_mask(BitmapAccessor* sourceBitmap, BRect sel_rect, BInvoker* prog, const Prefs* prefs);

class RowBlur
{
public:
	RowBlur(bool spawn, int32 x1, int32 y1, int32 w, int32 h,
			GIMPImage* srcPR, GIMPImage* destPR,
			const uchar* src, int src_stride, uchar* dst, int dst_stride,
			double* table, double* matrix, int matrix_length,
			BInvoker* prog);
	~RowBlur();
	void Blur();
	static int32 Go(void*);

	int32 x1, y1, width, height;
	GIMPImage* srcPR;
	GIMPImage* destPR;
	double* ctable;
	double* cmatrix;
	int cmatrix_length;

	const uchar* src;
	uchar* dst;
	int32 src_stride;
	int32 dst_stride;

	BInvoker* prog;
	uchar* cur_row;
	uchar* dest_row;
	thread_id mThread;
};

class ColBlur
{
public:
	ColBlur(bool spawn, int32 x1, int32 y1, int32 y2, int32 w, int32 h,
			GIMPImage* srcPR, GIMPImage* destPR,
			uchar* dst, int dst_stride,
			double* table, double* matrix, int matrix_length,
			BInvoker* prog);
	~ColBlur();
	void Blur();
	static int32 Go(void*);

	int32 x1, y1, y2, width, height;
	GIMPImage* srcPR;
	GIMPImage* destPR;
	double* ctable;
	double* cmatrix;
	int cmatrix_length;

	uchar* dst;
	int32 dst_stride;

	BInvoker* prog;
	uchar* cur_col;
	uchar* dest_col;
	thread_id mThread;
};





status_t
Manipulate(
	BitmapAccessor *sourceBitmap,
	BMessage *ioExtension,
	bool checkOnly)
{
	Prefs prefs;

	switch(sourceBitmap->ColorSpace())
	{
		case B_RGBA32_LITTLE:
		case B_RGB32_LITTLE:
			break;
		
		default:
			return B_NOT_ALLOWED;
	}

	if (checkOnly)
		return B_OK;

	gimp_init_glue(sourceBitmap, NULL, ioExtension);

	// If we can't find the selection rect extension, do the whole bitmap
	BRect sel_rect = sourceBitmap->Bounds();

	LoadPrefs(&prefs);
	BInvoker* prog;
	GetSettings(ioExtension, &prefs, &sel_rect, NULL, &prog);

	return unsharp_mask(sourceBitmap, sel_rect, prog, &prefs);
}



/* -------------------------- Unsharp Mask ------------------------- */
static status_t unsharp_mask(BitmapAccessor* sourceBitmap, BRect sel, BInvoker* prog, const Prefs* prefs)
{
	int32 width, height;
	int32 bytes;
	int x1, y1, x2, y2;
	double* ctable;
	GIMPImage srcPR(sourceBitmap);
	GIMPImage destPR(sourceBitmap, true);

	gimp_progress_init("Blurring...");

	x1 = (int) sel.left;
	x2 = (int) sel.right;
	y1 = (int) sel.top;
	y2 = (int) sel.bottom;
	
	width = (int32) sourceBitmap->Bounds().Width() + 1;
	height = (int32) sourceBitmap->Bounds().Height() + 1;
	bytes = (int32) srcPR.bpp;

	status_t result;
	result = unsharp_region (&srcPR, &destPR, width, height, bytes,
					prefs->radius, prefs->amount, prefs->threshold,
	                x1, x2, y1, y2,
					prefs->multithread, prog);

	if (B_OK == result)
		srcPR.Copy(&destPR);
	return result;
}



/* perform an unsharp mask on the region, given a source region, dest.
   region, width and height of the regions, and corner coordinates of
   a subregion to act upon.  Everything outside the subregion is unaffected.
  */
static status_t unsharp_region (GIMPImage* srcPR, GIMPImage* destPR,
                   int width, int height, int bytes,
                   double radius, double amount, int threshold,
                   int x1, int x2, int y1, int y2,
					bool multithread, BInvoker* prog)
{
	status_t result = B_OK;
	uchar* cur_row;
	uchar* dest_row;
	int w;
	int h;
	double* cmatrix = NULL;
	int cmatrix_length;
	double* ctable;

	int row, col;  //these are counters for loops

	/* these are used for the merging step */
	int diff;
	int value;
	int u,v;

	/* generate convolution matrix */
	cmatrix_length = gen_convolve_matrix(radius, &cmatrix);
	/* generate lookup table */
	ctable = gen_lookup_table(cmatrix, cmatrix_length);



	/* find height and width of subregion to act on */
	w = x2-x1+1;
	h = y2-y1+1;

	system_info info;
	get_system_info(&info);
	const uchar* src;
	uchar* dst;
	int32 src_stride;
	int32 dst_stride;
	
	src = (uchar*) srcPR->mBitmap->AccessBits(BRect(x1, y1, x2, y2), &src_stride);
	dst = (uchar*) destPR->mBitmap->AccessBits(BRect(x1, y1, x2, y2), &dst_stride);

	/* blur the rows */
	RowBlur* main_row_runner;
	if (!multithread || h < 10 * info.cpu_count)
	{
		main_row_runner = new RowBlur(false, x1, y1, w, h, srcPR, destPR, src, src_stride, dst, dst_stride, ctable, cmatrix, cmatrix_length, prog);
		main_row_runner->Blur();
	}
	else
	{
		RowBlur* runners[B_MAX_CPU_COUNT];
		
		int32 start_row = y1;
		int32 row_count = h/info.cpu_count;
		int32 rows_left = h;
		
		for (int cpu = 0; cpu < info.cpu_count - 1; cpu++)
		{
			runners[cpu] = new RowBlur(true, x1, start_row, w, row_count, srcPR, destPR, src + (start_row - y1) * src_stride, src_stride, dst + (start_row - y1) * dst_stride, dst_stride, ctable, cmatrix, cmatrix_length, prog);
			start_row += row_count + 1;
			rows_left -= row_count;
			row_count = min(row_count, rows_left);
		}
		
		main_row_runner = new RowBlur(false, x1, start_row, w, row_count, srcPR, destPR, src + (start_row - y1) * src_stride, src_stride, dst + (start_row - y1) * dst_stride, dst_stride, ctable, cmatrix, cmatrix_length, prog);
		main_row_runner->Blur();

		status_t err;
		for (int cpu = 0; cpu < info.cpu_count - 1; cpu++)
		{
			wait_for_thread(runners[cpu]->mThread, &err);
			delete runners[cpu];
		}
	}

	dest_row = main_row_runner->dest_row;
	cur_row = main_row_runner->cur_row;

	/* blur the cols */
	if (!multithread || w < 10 * info.cpu_count)
	{
		ColBlur(false, x1, y1, y2, w, h, srcPR, destPR, dst, dst_stride, ctable, cmatrix, cmatrix_length, prog).Blur();
	}
	else
	{
		ColBlur* runners[B_MAX_CPU_COUNT];

		int32 start_col = x1;
		int32 col_count = w/info.cpu_count;
		int32 cols_left = w;
		
		for (int cpu = 0; cpu < info.cpu_count - 1; cpu++)
		{
			runners[cpu] = new ColBlur(true, start_col, y1, y2, col_count, h, srcPR, destPR, dst + (start_col - x1) * (int32) destPR->bpp, dst_stride, ctable, cmatrix, cmatrix_length, prog);
			start_col += col_count + 1;
			cols_left -= col_count;
			col_count = min(col_count, cols_left);
		}
		
		ColBlur(false, start_col, y1, y2, col_count, h, srcPR, destPR, dst + (start_col - x1) * (int32) destPR->bpp, dst_stride, ctable, cmatrix, cmatrix_length, prog).Blur();

		status_t err;
		for (int cpu = 0; cpu < info.cpu_count - 1; cpu++)
		{
			wait_for_thread(runners[cpu]->mThread, &err);
			delete runners[cpu];
		}
	}

	gimp_progress_init("Merging...");

	/* merge the source and destination (which currently contains the blurred version) images */
	for (row = 0; row < h; row++) {
		value = 0;
		/* get source row */
		srcPR->GetRow(cur_row, x1, y1+row, w);
		/* get dest row */
		destPR->GetRow(dest_row, x1, y1+row, w);
		/* combine the two */
		for (u=0;u<w;u++) {
			for (v=0; v<bytes; v++) {
				diff = (cur_row[u*bytes+v] - dest_row[u*bytes+v]);
				/* do tresholding */
				if (abs(2*diff) < threshold)
					diff = 0;
				
				value = (int) (cur_row[u*bytes+v] + amount * diff);
				
				if (value < 0) dest_row[u*bytes+v] =0;
				else if (value > 255) dest_row[u*bytes+v] = 255;
				else  dest_row[u*bytes+v] = value;
			}
		}
		/* update progress bar every five rows */
		if (row%5 == 0 && !gimp_progress_update(prog, (double)row/(7*h) + 0.857143))
		{
			result = B_CANCELED;
			break;
		}
		destPR->SetRow(dest_row, x1, y1+row, w);
	}

	/* free the memory we took */
	delete main_row_runner;
	//free(cur_row);
	//free(dest_row);
	//free(cur_col);
	//free(dest_col);

	free(cmatrix);
	free(ctable);
	
	return result;
}

#pragma mark -
RowBlur::RowBlur(bool spawn, int32 _x1, int32 _y1, int32 w, int32 h, GIMPImage* _srcPR, GIMPImage* _destPR,
	const uchar* _src, int _src_stride, uchar* _dst, int _dst_stride,
	double* _ctable, double* _cmatrix, int _cmatrix_length, BInvoker* _prog)
{
	x1 = _x1;
	y1 = _y1;
	width = w;
	height = h;
	srcPR = _srcPR;
	destPR = _destPR;
	ctable = _ctable;
	cmatrix = _cmatrix;
	cmatrix_length = _cmatrix_length;
	prog = _prog;

	src = _src;
	dst = _dst;
	src_stride = _src_stride;
	dst_stride = _dst_stride;

	cur_row = dest_row = NULL;

	if (spawn)
	{
		mThread = spawn_thread (Go, "unsharp_blur_row", B_NORMAL_PRIORITY, this);
		resume_thread(mThread);
	}
	else
		mThread = B_ERROR;
}

RowBlur::~RowBlur()
{
	free(dest_row);
	free(cur_row);
}

void RowBlur::Blur()
{
	int32 bytes = srcPR->bpp;
	int32 bytelength = bytes * width;
	int32 row;

	/*  allocate row, col buffers  */
	cur_row = (uchar *) malloc (bytelength);
	dest_row = (uchar *) malloc (bytelength);

	memset(dest_row, 0, bytelength);

	/* blank out a region of the destination memory area, I think */
	for (row = 0; row < height; row++)
		destPR->SetRow(dest_row, x1, y1 + row, width);


	for (row = 0; row < height; row++)
	{
		memcpy(cur_row, src, bytelength);
		memcpy(dest_row, dst, bytelength);
		blur_line(ctable, cmatrix, cmatrix_length, cur_row, dest_row, width, bytes);
		memcpy(dst, dest_row, bytelength);

		if (row%5 == 0 && mThread == B_ERROR && !gimp_progress_update(prog, 3*(double)row/(7*height)))
			break;

		src += src_stride;
		dst += dst_stride;
	}

}

int32 RowBlur::Go(void*p)
{
	static_cast<RowBlur*>(p)->Blur();
	return B_OK;
}


#pragma mark -

ColBlur::ColBlur(bool spawn, int32 _x1, int32 _y1, int32 _y2, int32 w, int32 h, GIMPImage* _srcPR, GIMPImage* _destPR,
			uchar* _dst, int _dst_stride,
			double* _ctable, double* _cmatrix, int _cmatrix_length, BInvoker* _prog)
{
	x1 = _x1;
	y1 = _y1;
	y2 = _y2;
	width = w;
	height = h;
	srcPR = _srcPR;
	destPR = _destPR;
	ctable = _ctable;
	cmatrix = _cmatrix;
	cmatrix_length = _cmatrix_length;
	prog = _prog;
	
	dst = _dst;
	dst_stride = _dst_stride;

	cur_col = cur_col = NULL;

	if (spawn)
	{
		mThread = spawn_thread (Go, "unsharp_blur_col", B_NORMAL_PRIORITY, this);
		resume_thread(mThread);
	}
	else
		mThread = B_ERROR;
}

ColBlur::~ColBlur()
{
	free(dest_col);
	free(cur_col);
}

void ColBlur::Blur()
{
	int32 bytes = destPR->bpp;
	int32 rows = (y2 - y1 + 1);
	int32 bytelength = bytes * rows;
	int32 col;

	// Blur columns from/to the dest bitmap, since it already has the row-blurred data.

	/* allocate column buffers */
	cur_col = (uchar *) malloc (bytelength);
	dest_col = (uchar *) malloc (bytelength);

	for (col = 0; col < width; col++) {
		uchar* dstP = dst;
		
		for (int row = 0; row < rows; row++)
		{
			for (int b = 0; b < bytes; b++)
			{
				cur_col[row * bytes + b] = dest_col[row * bytes + b] = dstP[b];
			}
			dstP += dst_stride;
		}
		
		blur_line(ctable, cmatrix, cmatrix_length, cur_col, dest_col, height, bytes);

		dstP = dst;
		
		for (int row = 0; row < rows; row++)
		{
			for (int b = 0; b < bytes; b++)
			{
				dstP[b] = dest_col[row * bytes + b];
			}
			dstP += dst_stride;
		}

		if (col%5 == 0 && mThread == B_ERROR && !gimp_progress_update(prog, 3*(double)col/(7*width) + 0.428571))
			break;

		dst += bytes;
	}
}


int32 ColBlur::Go(void*p)
{
	static_cast<ColBlur*>(p)->Blur();
	return B_OK;
}

#pragma mark -

/* this function is written as if it is blurring a column at a time,
   even though it can operate on rows, too.  There is no difference
   in the processing of the lines, at least to the blur_line function. */
static inline void blur_line(double* ctable, double* cmatrix,
                             int cmatrix_length,
                             uchar* cur_col, uchar* dest_col,
                             int y, int32 bytes) {

#if 1 //def READABLE_CODE // Hmm, this is actually faster with my compiler!
/* ------------- semi-readable code ------------------- */
	double scale;
	double sum;
	int i,j;
	int row;

	// this is to take care cases in which the matrix can go over
	// both edges at once.  It's not efficient, but this can only
	// happen in small pictures anyway.
	if (cmatrix_length > y) {
	
		for (row = 0; row < y ; row++) {
			scale=0;
			// find the scale factor
			for (j = 0; j < y ; j++) {
				// if the index is in bounds, add it to the scale counter
				if ((j + cmatrix_length/2 - row >= 0) &&
				    (j + cmatrix_length/2 - row < cmatrix_length))
					scale += cmatrix[j + cmatrix_length/2 - row];
			}
			for (i = 0; i<bytes; i++) {
				sum = 0;
				for (j = 0; j < y; j++) {
					if ( (j >= row - cmatrix_length/2) && (j <= row + cmatrix_length/2) )
						sum += cur_col[j*bytes + i] * cmatrix[j];
				}
 				dest_col[row*bytes + i] = (uchar)roundit(sum / scale);
			}
		}
	}
	
	else {  // when the cmatrix is smaller than row length

		// for the edge condition, we only use available info, and scale to one
		for (row = 0; row < cmatrix_length/2; row++) {
			// find scale factor
			scale=0;
			for (j = cmatrix_length/2 - row; j<cmatrix_length; j++)
				scale += cmatrix[j];

			for (i = 0; i<bytes; i++) {
				sum = 0;
				for (j = cmatrix_length/2 - row; j<cmatrix_length; j++) {
					sum += cur_col[(row + j-cmatrix_length/2)*bytes + i] * cmatrix[j];
				}
				dest_col[row*bytes + i] = (uchar)roundit(sum / scale);
			}
		}
		// go through each pixel in each col
		for ( ; row < y-cmatrix_length/2; row++) {
			for (i = 0; i<bytes; i++) {
				sum = 0;
				for (j = 0; j<cmatrix_length; j++) {
					sum += cur_col[(row + j-cmatrix_length/2)*bytes + i] * cmatrix[j];
				}
				dest_col[row*bytes + i] = (uchar)roundit(sum);
			}
		}
		// for the edge condition , we only use available info, and scale to one
		for ( ; row < y; row++) {
			// find scale factor
			scale=0;
			for (j = 0; j< y-row + cmatrix_length/2; j++)
				scale += cmatrix[j];
				
			for (i = 0; i<bytes; i++) {
				sum = 0;
				for (j = 0; j<y-row + cmatrix_length/2; j++) {
					sum += cur_col[(row + j-cmatrix_length/2)*bytes + i] * cmatrix[j];
				}
				dest_col[row*bytes + i] = (uchar)roundit(sum / scale);
			}
		}
	}
#else
	/* --------------- optimized, unreadable code -------------------*/

	double scale;
	double sum;
	int i=0, j=0;
	int row;
	int cmatrix_middle = cmatrix_length/2;

	double *cmatrix_p;
	uchar  *cur_col_p;
	uchar  *cur_col_p1;
	uchar  *dest_col_p;
	double *ctable_p;

	// this first block is the same as the non-optimized version --
	// it is only used for very small pictures, so speed isn't a
	// big concern.
	if (cmatrix_length > y) {
	
		for (row = 0; row < y ; row++) {
			scale=0;
			// find the scale factor
			for (j = 0; j < y ; j++) {
				// if the index is in bounds, add it to the scale counter
				if ((j + cmatrix_length/2 - row >= 0) &&
				    (j + cmatrix_length/2 - row < cmatrix_length))
					scale += cmatrix[j + cmatrix_length/2 - row];
			}
			for (i = 0; i<bytes; i++) {
				sum = 0;
				for (j = 0; j < y; j++) {
					if ( (j >= row - cmatrix_length/2) && (j <= row + cmatrix_length/2) )
						sum += cur_col[j*bytes + i] * cmatrix[j];
				}
 				dest_col[row*bytes + i] = (uchar)roundit(sum / scale);
			}
		}
	}

	else {
		// for the edge condition, we only use available info and scale to one
		for (row = 0; row < cmatrix_middle; row++) {
			// find scale factor
			scale=0;
			for (j = cmatrix_middle - row; j<cmatrix_length; j++)
				scale += cmatrix[j];
			for (i = 0; i<bytes; i++) {
				sum = 0;
				for (j = cmatrix_middle - row; j<cmatrix_length; j++) {
					sum += cur_col[(row + j-cmatrix_middle)*bytes + i] * cmatrix[j];
				}
				dest_col[row*bytes + i] = (uchar)roundit(sum / scale);
			}
		}
		// go through each pixel in each col
		dest_col_p = dest_col + row*bytes;
		for ( ; row < y-cmatrix_middle; row++) {
			cur_col_p = (row - cmatrix_middle) * bytes + cur_col;
			for (i = 0; i<bytes; i++) {
				sum = 0;
				cmatrix_p = cmatrix;
				cur_col_p1 = cur_col_p;
				ctable_p = ctable;
				for (j = cmatrix_length; j>0; j--) {
					sum += *(ctable_p + *cur_col_p1);
					cur_col_p1 += bytes;
					ctable_p += 256;
				}
				cur_col_p++;
				*(dest_col_p++) = roundit(sum);
			}
		}
	
		// for the edge condition , we only use available info, and scale to one
		for ( ; row < y; row++) {
			// find scale factor
			scale=0;
			for (j = 0; j< y-row + cmatrix_middle; j++)
				scale += cmatrix[j];
			for (i = 0; i<bytes; i++) {
				sum = 0;
				for (j = 0; j<y-row + cmatrix_middle; j++) {
					sum += cur_col[(row + j-cmatrix_middle)*bytes + i] * cmatrix[j];
				}
				dest_col[row*bytes + i] = (uchar)roundit(sum / scale);
			}
		}
	}
#endif

}

static inline int roundit(double i) {
	return (int)(i + 0.5);
}


/* generates a 1-D convolution matrix to be used for each pass of 
 * a two-pass gaussian blur.  Returns the length of the matrix.
 */
static int gen_convolve_matrix(double radius, double** cmatrix_p) {
	int matrix_length;
	int matrix_midpoint;
	double* cmatrix;
	int i,j;
	double std_dev;
	double sum;
	
	/* we want to generate a matrix that goes out a certain radius
	 * from the center, so we have to go out ceil(rad-0.5) pixels,
	 * inlcuding the center pixel.  Of course, that's only in one direction,
	 * so we have to go the same amount in the other direction, but not count
	 * the center pixel again.  So we double the previous result and subtract
	 * one.
	 * The radius parameter that is passed to this function is used as
	 * the standard deviation, and the radius of effect is the
	 * standard deviation * 2.  It's a little confusing.
	 */
	radius = fabs(radius) + 1.0;

	std_dev = radius;
	radius = std_dev * 2;
	

	/* go out 'radius' in each direction */
	matrix_length = (int) (2 * ceil(radius-0.5) + 1);
	if (matrix_length <= 0) matrix_length = 1;
	matrix_midpoint = matrix_length/2 + 1;
	*cmatrix_p = (double*)(malloc(matrix_length * sizeof(double)));
	cmatrix = *cmatrix_p;

	/*  Now we fill the matrix by doing a numeric integration approximation
	 * from -2*std_dev to 2*std_dev, sampling 50 points per pixel.
	 * We do the bottom half, mirror it to the top half, then compute the
	 * center point.  Otherwise asymmetric quantization errors will occur.
	 *  The formula to integrate is e^-(x^2/2s^2).
	 */
	for (i=matrix_length/2 + 1; i<matrix_length; i++) {
		double base_x = i - floor(matrix_length/2) - 0.5;
		sum = 0;
		for (j=1; j<=50; j++) {
			if ( base_x+0.02*j <= radius ) 
				sum += exp( -(base_x+0.02*j)*(base_x+0.02*j) / 
				                 (2*std_dev*std_dev) );
		}
		cmatrix[i] = sum/50;
	}

	/* mirror the thing to the bottom half */
	for (i=0; i<=matrix_length/2; i++) {
		cmatrix[i] = cmatrix[matrix_length-1-i];
	}
	
	/* find center val -- calculate an odd number of quanta to make it symmetric,
	 * even if the center point is weighted slightly higher than others. */
	sum = 0;
	for (j=0; j<=50; j++) {
		sum += exp( -(0.5+0.02*j)*(0.5+0.02*j) /
		                 (2*std_dev*std_dev) );
	}
	cmatrix[matrix_length/2] = sum/51;

	
	/* normalize the distribution by scaling the total sum to one */
	sum=0;
	for (i=0; i<matrix_length; i++) sum += cmatrix[i];
	for (i=0; i<matrix_length; i++) cmatrix[i] = cmatrix[i] / sum;

	return matrix_length;
}





/* ----------------------- gen_lookup_table ----------------------- */
/* generates a lookup table for every possible product of 0-255 and
   each value in the convolution matrix.  The returned array is
   indexed first by matrix position, then by input multiplicand (?)
   value.
*/
static double* gen_lookup_table(double* cmatrix, int cmatrix_length) {
	int i, j;
	double* lookup_table = (double*) malloc(cmatrix_length * 256 * sizeof(double));

// This doesn't seem to make much difference;
// it's a small percentage of the total computation.
#ifdef READABLE_CODE
	for (i=0; i<cmatrix_length; i++) {
		for (j=0; j<256; j++) {
			lookup_table[i*256 + j] = cmatrix[i] * (double)j;
		}
	}
#else
	double* lookup_table_p = lookup_table;
	double* cmatrix_p      = cmatrix;
	for (i=0; i<cmatrix_length; i++) {
		for (j=0; j<256; j++) {
			*(lookup_table_p++) = *cmatrix_p * (double)j;
		}
		cmatrix_p++;
	}
#endif

	return lookup_table;
}

