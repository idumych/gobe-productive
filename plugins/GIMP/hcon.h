/* hcon.h: the types sparkl.c takes from John Beale's HF-Lab (the original's
 * src.zip left this header out; rebuilt for the Gobe Productive port from
 * how sparkl.c uses them) */
#ifndef HCON_H
#define HCON_H
typedef double D;
typedef unsigned int U;
typedef unsigned char PIXTYPE;
#ifndef Boolean
typedef int Boolean;
#endif
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
/* an image as separate red, green, blue and alpha planes */
typedef struct {
	U xsize, ysize;
	PIXTYPE *r, *g, *b, *a;
} Cimg;
/* a pixel's value at its brightest, and an element of a plane (row by row,
 * as convert_to_jbimage fills them; El's width the global xsize) */
#define PIXMAX 255
#ifndef ABS
#define ABS(a) ((a) < 0 ? -(a) : (a))
#endif
extern U xsize, ysize;
#define El(a, x, y) ((a)[(y) * xsize + (x)])
#define Elrp(im, x, y) ((im)->r[(y) * (im)->xsize + (x)])
#define Elgp(im, x, y) ((im)->g[(y) * (im)->xsize + (x)])
#define Elbp(im, x, y) ((im)->b[(y) * (im)->xsize + (x)])
#define Elap(im, x, y) ((im)->a[(y) * (im)->xsize + (x)])
/* the same of an image itself (not a pointer to one) */
#define Elr(im, x, y) ((im).r[(y) * (im).xsize + (x)])
#define Elg(im, x, y) ((im).g[(y) * (im).xsize + (x)])
#define Elb(im, x, y) ((im).b[(y) * (im).xsize + (x)])
#define Ela(im, x, y) ((im).a[(y) * (im).xsize + (x)])
#endif
