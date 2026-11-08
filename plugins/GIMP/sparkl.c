/* Sparkle --- image filter plug-in for The Gimp image manipulation program
 * Copyright (C) 1996 by John Beale;  ported to Gimp by Michael J. Hammel
 *
 * This program is free software; you can redistribute it and/or modify
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * You can contact Michael at mjhammel@csn.net
 * Note: set tabstops to 3 to make this more readable.
 */

/*
* This file has been modified from the original. Relationships between labels and fields
* in the dialog have been changed for aesthetic reasons.  <Scott Lindsey 7/21/98>
*/

/*
 * Sparkle -  simulate pixel bloom and diffraction effects 
 */

/* === System Headers === */ 
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* === Gimp specific headers === */
#include "gimp.h"

/* === Speckle specific includes === */
#include "hcon.h"
#include "hf_io.h"
#include "gems.h"

/*
 * Used by cancel_callback only
 */
typedef struct _sparkl_cb {
	Image	input;
	Image output;
	int	dialog_id;
	int	cancel;
} SPARKL_CB;

/* === Speckle specific defined values === */
#define LUMTYPE unsigned short
#define LUMMAX 65535
#define PSV 2                   /* point-spread value  */
 

/* === Macros === */

/* === Prototypes === */

/* convert Gimp image to/from Speckle structure */
int convert_to_jbimage(Cimg *im, unsigned char *input_image);
int convert_to_gimp(Cimg *im, unsigned char *input_image);

/* generate luminosity array */
int genhsl(Cimg *im, LUMTYPE **lum);
extern void RGB_to_HSL (D r, D g, D b, D *h, D *s, D *l);
extern void HSL_to_RGB (D h, D s, D l, D *r, D *g, D *b);

/*
 * find the pixel value for which "frac" percent of the pixels exceed
 * it in magnitude.
 */
D histl(LUMTYPE *lum, int xsize, int ysize, D frac);

/* generate diff. effects */
int sparkl(Cimg *im1,Cimg *im2p,LUMTYPE *lum,
		D lumlim, D csatf, D lscale,D iscale);
void fspike(Cimg *im1p, Cimg *im2p, D xr, D yr, D intens,
			D csatf, D rscale, D th1);

/* draw an anti-aliased point at (real) coords xr,yr, intensity mag */
void rpnt(Cimg *im2p, D xr, D yr, D ri, D gi, D bi);

/* draw a gaussian-profiled ring around a point */
/* future enhancement...
void ring(PIXTYPE **im2p, D xr, D yr, D rad, D width, D intens);
*/
 
static void Threshold_callback(int item_id, void *client_data, void *call_data);
static void Flare_callback(int item_id, void *client_data, void *call_data);
static void Spike_Angle_callback(int item_id, void *client_data, void *call_data);
static void U_Text_callback(int item_id, void *client_data, void *call_data);
static void D_Text_callback(int item_id, void *client_data, void *call_data);
static void close_callback(int item_id, void *client_data, void *call_data);

/* === Globals === */
U xsize, ysize;
static int	   channels;
/*PIXTYPE fmin = 0, fmax = 1.0;		/* scaling limits */
U s_points = 6;						/* number of diffraction points */
D sp_angle = 15;						/* initial spike angle, degrees */
Boolean norenorm = FALSE;
Boolean negate = FALSE;
Boolean rescale = FALSE;
Boolean clip = FALSE;
D pi = 3.1415926535897932385;  

static int     dialog_id; 			/* Gimp-provided dialog id */
static int     group_id; 			/* Gimp-provided group id */


/*========================================================================
 * Name:       main
 * Prototype:  main( int argc, char ** argv )
 *
 * Description:
 *    Initialize communication with the Gimp.
 *    Provide dialog boxes.
 *
 *========================================================================*/
int main(
	int	argc, 
	char	**argv)
{
	char		buf[128];
	int		rc;
	LUMTYPE	*lum;
	Cimg		im1, im2;				/* 3-plane color image (input and output) */
	D			lumlim, lumper, csatf;
	D			lscale, iscale;

	Image		input, output;
	int		threshold_id, spike_length_id, flare_intensity_id,
				spike_points_id, spike_angle_id;
	unsigned char		*input_image, *output_image;

	SPARKL_CB	okcb, cancelcb;
 

	/*
	 * Initialize communications with the Gimp.  If this fails, return.
	 */
	if (! gimp_init(argc, argv))
		return 0;
 
	/*
	 * Get default input and output images.  If this fails, cleanup and
	 * return.
	 */
	if ( ! (input = gimp_get_input_image(0)) )
		return 0;
	if ( ! (output = gimp_get_output_image(0)) )
	{
		gimp_free_image(input);
		return 0;
	}

	/*
	 * We can only work on 24-bit color images, so make sure thats what
	 * we have.
	 */
	if ( gimp_image_type(input) != RGB_IMAGE )
	{
		gimp_message("Sparkle only works on 24-bit RBG images.");
		gimp_free_image(input);
		gimp_free_image(output);
		return 0;
	}
	
	channels = gimp_image_channels (input);

	/*
	 * Ok, we're going to have to do some work.  Start by setting
	 * the default values.
	 */
	lumper = 0.001;	/* percentile thresh = 0.1 %  */
	lscale = 20;		/* spike length */
	iscale = 0.5;		/* flare intensity */
	csatf  = 1.0;		/* constant */

	/* Do we need to initialize this value??? */
	/* lumlim = 0.5; */


	/*
	 * Provide the dialog in which the user can specify Sparkle 
	 * parameters.
	 */
	dialog_id = gimp_new_dialog("Sparkle");
	group_id  = gimp_new_row_group(dialog_id, DEFAULT, NORMAL, "");

	/* Threshold slider */
	threshold_id = gimp_new_scale(dialog_id, group_id, 0, 10000, 1000*lumper, 4);
	gimp_new_label(dialog_id, threshold_id, "Threshold" );
 
	/* Flare Intensity Slider */
	flare_intensity_id = gimp_new_scale(dialog_id, group_id, 0, 1000, 100*iscale, 3);
	gimp_new_label(dialog_id, flare_intensity_id, "Flare Intensity" );
 
	/* Spike length text input field */
	sprintf(buf, "%0.0f", lscale);
	spike_length_id = gimp_new_text(dialog_id, group_id, buf);
	gimp_new_label(dialog_id, spike_length_id, "Spike Length" );
	
	/* Number of Spike points text input field */
	sprintf(buf, "%d", s_points);
	spike_points_id = gimp_new_text(dialog_id, group_id, buf);
	gimp_new_label(dialog_id, spike_points_id, "Spike Points" );

	/* Spike Angle (in degrees) slider */
	spike_angle_id = gimp_new_scale(dialog_id, group_id, 0, 360, sp_angle, 0);
	gimp_new_label(dialog_id, spike_angle_id, "Spike Angle in degrees" );
 
	/*
	 * Input field callbacks
	 * basically, all these just stuff the input field (converted to 
	 * an appropriate format) into their associated global/local variables.
	 */
	gimp_add_callback(dialog_id, threshold_id, Threshold_callback, &lumper);
	gimp_add_callback(dialog_id, spike_length_id, D_Text_callback, &lscale);
	gimp_add_callback(dialog_id, flare_intensity_id, Flare_callback, &iscale);
	gimp_add_callback(dialog_id, spike_points_id, U_Text_callback, &s_points);
	gimp_add_callback(dialog_id, spike_angle_id, Spike_Angle_callback, &sp_angle);
 
	/* Cancel/OK Button Callbacks */
	okcb.input = (Image)NULL;
	okcb.output = (Image)NULL;
	okcb.dialog_id = dialog_id;
	okcb.cancel = 0;
	gimp_add_callback(dialog_id, gimp_ok_item_id(dialog_id), close_callback, (void *)&okcb);
	cancelcb.input = input;
	cancelcb.output = output;
	cancelcb.dialog_id = dialog_id;
	cancelcb.cancel = 1;
	gimp_add_callback(dialog_id, gimp_cancel_item_id(dialog_id), close_callback, (void *)&cancelcb);
 
	/*
	 * Aftger the dialog has been closed, gimp_show_dialog() will return 0 or 1.
	 * If it doesn't, I'm not sure what the return values mean, but it 
	 * can't be good.
	 */
	if ( ( (rc=gimp_show_dialog(dialog_id)) != 0 ) && (rc != 1) )
	{
		sprintf(buf, "Gimp can't display Sparkle dialog: rc=%d", rc);
		gimp_message(buf);
		gimp_free_image(input);
		gimp_free_image(output);
		return 0;
	}

	/*
	 * Aaaaaaaaaaaaaaaaallrighty then......
	 */
	

	/* DEBUG
	printf("Dialog should be closed now.\n");
	sprintf(buf, "New values: \n"
		"threshold: %f\n"
		"lscale   : %f\n"
		"iscale   : %f\n"
		"s_points : %d\n"
		"s_angle  : %f\n",
		lumper, lscale, iscale, (int)s_points, sp_angle);
	fprintf (stderr, buf);

	gimp_free_image(input);
	gimp_free_image(output);
	gimp_quit();
	exit(0);
	*/

	/*
	 * Convert the image to John's Cimg format (makes it easeir to port
	 * this program)
	 */
	input_image = gimp_image_data(input);
	xsize = im1.xsize = (U) gimp_image_width(input);
	ysize = im1.ysize = (U) gimp_image_height(input);
	if ( convert_to_jbimage(&im1, input_image) != 0 )
	{
		gimp_message("Sparkle: Could not allocate memory for image conversion.");
		gimp_free_image(input);
		gimp_free_image(output);
		return 0;
	}

	/* Debug
	fprintf(stderr,"Done converting to jbimage\n");
	*/


	/*
	 * Generate the lunimosity array
	 */
	if ( genhsl(&im1, &lum) != 0 )
	{
		gimp_message("Sparkle: Could not allocate memory for luminosity array.");
		gimp_free_image(input);
		gimp_free_image(output);
		return 0;
	}

	/* Debug
	fprintf(stderr,"Done generating luminosity array\n");
	*/

	/* compute the intensity level based on the threshold requested */
	lumlim = histl(lum,xsize,ysize,lumper); 

	/* Debug
	fprintf(stderr,"Done computing intensity level\n");
	*/

	/* now, make it sparkle.... */
	if ( sparkl(&im1,&im2,lum,lumlim,csatf,lscale,iscale) != 0 )
	{
		gimp_message("Sparkle: Could not allocate memory for sparkle function.");
		gimp_free_image(input);
		gimp_free_image(output);
		return 0;
	}

	/* Debug
	fprintf(stderr,"Done adding sparkle\n");
	*/


	/* now convert the new image back to Gimp format */
	output_image = gimp_image_data(output);
	if ( convert_to_gimp(&im2, output_image) != 0 )
	{
		gimp_message("Sparkle: rgb image pointers are NULL!");
		gimp_free_image(input);
		gimp_free_image(output);
		return 0;
	}

	/* Debug
	fprintf(stderr,"Done converting to gimp\n");
	*/


	/* and display the new image */
	gimp_update_image(output);

	/* clean up */
	gimp_free_image(input);
	gimp_free_image(output);
	gimp_quit();

	/* b'bye! */
	return (0);
}


/*
 * Callback routines
 * Since the scales are different for the various sliders, and the value
 * returned is a long, I have to transform the returned value to the
 * correct scale depending on which scale is being updated.  I could
 * do this with a single callback and pass in a struct with the info
 * I need, or use multiple callbacks.  I'm lazy right now, so the easy
 * way out is cut and paste multiple callbacks. -- mjh
 *
 * Since both text fields use int values, the same callback works for
 * either.
 *
 */
static void
Threshold_callback(int item_id, void *client_data, void *call_data)
{
	*((D *) client_data) = (D) ( (D)(*((long *) call_data)) / 10000.0);
}

static void
Flare_callback(int item_id, void *client_data, void *call_data)
{
	*((D *) client_data) = (D) ( (D)(*((long *) call_data)) / 1000.0);
}
 
static void
Spike_Angle_callback(int item_id, void *client_data, void *call_data)
{
	*((D *) client_data) = (D) (*((long *) call_data));
}
 
static void
D_Text_callback(int item_id, void *client_data, void *call_data)
{
   *((D *) client_data) = atof(call_data);
}
 
static void
U_Text_callback(int item_id, void *client_data, void *call_data)
{
   *((U *) client_data) = (U)atof(call_data);
}
 
static void
close_callback(int item_id, void *client_data, void *call_data)
{
	SPARKL_CB	*cb = (SPARKL_CB *)client_data;
   gimp_close_dialog(cb->dialog_id, 0);
	if (cb->cancel)
	{
		gimp_free_image(cb->input);
		gimp_free_image(cb->output);
		gimp_quit();
	}
}
 


/*========================================================================
 * Name:       convert_to_jbimage
 * Prototype:  convert_to_jbimage( Cimg *im, unsigned char *input_image )
 *
 * Description:
 *    Convert the image from GIMP into a Speckle specific structure.
 *		This isn't the most efficient use of memory, but it allows
 *    for a quicker and easier port - keeps most of the code the same.
 *
 *========================================================================*/
int
convert_to_jbimage(
	Cimg				*im, 
	unsigned char	*input_image
)
{
	size_t	mem;
	int		ix,iy;
	PIXTYPE	*ipt;
	PIXTYPE	*rpt,*gpt,*bpt,*apt;

	/*
	 * Get some memory for the rgb channels
	 */
	mem = (size_t) sizeof(PIXTYPE) * im->xsize * im->ysize;
	rpt = im->r = (PIXTYPE *) malloc(mem);
	gpt = im->g = (PIXTYPE *) malloc(mem);
	bpt = im->b = (PIXTYPE *) malloc(mem);
	apt = im->a = (PIXTYPE *) malloc(mem);
	if (im->r==NULL || im->g==NULL || im->b==NULL || im->a==NULL)
		return(1);

	ipt = (PIXTYPE *)input_image;

	/*
	 * Transfer the image from the GIMP image struct to the Sparkle
	 * image struct.  Doesn't error check the bounds of the Gimp image.
	 */
	for (iy = 0; iy < im->ysize; iy++)
	{
		for (ix = 0; ix < im->xsize; ix++)
		{
			*rpt = *ipt;
			*gpt = *(ipt+1);
			*bpt = *(ipt+2);
			*apt = *(ipt+3);

			ipt += channels;
			rpt++; gpt++; bpt++; apt++;
		}
	}
	return (0);
}



/*========================================================================
 * Name:       convert_to_gimp
 * Prototype:  convert_to_gimp( Cimg *im, unsigned char *output_image )
 *
 * Description:
 *    Convert the image from Speckle specific format to a gimp image.
 *		Based on convert_to_jbimage();
 *
 *========================================================================*/
int
convert_to_gimp(
	Cimg				*im, 
	unsigned char	*output_image
)
{
	int		ix,iy;
	unsigned char		*opt;
	PIXTYPE	*rpt,*gpt,*bpt,*apt;

	/*
	 * Get some memory for the rgb channels
	 */
	rpt = im->r;
	gpt = im->g;
	bpt = im->b;
	apt = im->a;
	if (im->r==NULL || im->g==NULL || im->b==NULL || im->a==NULL)
		return(1);

	opt = output_image;

	/*
	 * Transfer the image from the Sparkle image struct to the Gimp
	 * image struct.  Doesn't error check the bounds of the image.
	 */
	for (iy = 0; iy < im->ysize; iy++)
	{
		for (ix = 0; ix < im->xsize; ix++)
		{
			*opt     = *rpt;
			*(opt+1) = *gpt;
			*(opt+2) = *bpt;
			*(opt+3) = *apt;

			opt += channels;
			rpt++; gpt++; bpt++; apt++;
		}
	}
	return (0);
}



/*========================================================================
 * Name:       genhsl
 * Prototype:  genhsl( Cimg *im, LUMTYPE **lum )
 *
 * Description:
 *    Generate the luninosity array
 *
 *========================================================================*/
int
genhsl(
	Cimg		*im, 
	LUMTYPE	**lum
)
{
	int		ix,iy;
	double	r,g,b;
	double	h,s,l;
	U			xsize, ysize;
	PIXTYPE	*rpt,*gpt,*bpt;
	LUMTYPE	*ll;
	size_t	bufsize;
	int		cur_progress, max_progress;

	
	/* Provide progress feedback to user */
	gimp_init_progress("Generating luminosity array...");

	xsize = im->xsize;
	ysize = im->ysize;

	cur_progress=0;
	max_progress=ysize;

	bufsize = (size_t)(xsize * ysize * sizeof(LUMTYPE));
	ll = (LUMTYPE *) malloc(bufsize);

	if (ll == NULL)
		return(1);
	*lum = ll;

	rpt = im->r;
	gpt = im->g;
	bpt = im->b;

	for (iy = 0; iy<ysize; iy++)
	{
		for (ix = 0; ix<xsize; ix++)
		{
			r = (double)*rpt / (double)(PIXMAX);
			g = (double)*gpt / (double)(PIXMAX);
			b = (double)*bpt / (double)(PIXMAX);
			rpt++; gpt++; bpt++;

			/* external: in hsltorgb.c */
			RGB_to_HSL(r,g,b,&h,&s,&l);
			El(ll,ix,iy) = (LUMTYPE)(l * (LUMTYPE)(LUMMAX));
		}
		if ((++cur_progress % 5) == 0)
			gimp_do_progress (cur_progress, max_progress);
	}

	/* close progress dialog */
	gimp_do_progress (1, 1);

	return(0);
}


/*========================================================================
 * Name:       histl
 * Prototype:  histl( LUMTYPE *im1, int xsize, int ysize, D threshold )
 *
 * Description:
 *    Compute intensity level based on threshold value.
 *		This means to return the pixel value of which the specified
 *		percentage (the threshold value) of pixels exceed in
 *		magnitude.
 *
 *========================================================================*/
D
histl(
	LUMTYPE	*im1, 
	int		xsize, 
	int		ysize, 
	D			threshold
)
{
	int 		hist[1001];		/* histogram with 1001 bins */
	D			dmax, dmin, tmp;
	D			retval;			/* value returned to caller */
	int		ix, iy;
	int		hlim = 1000;
	int		p10;
	D			p10i;
	int		p10lim;
	long		num_pixels;		/* total number of pixels in this image */
 
 

	/* clear the historgram array (some systems won't initialize this) */
	for (ix=0;ix<1001;ix++) hist[ix] = 0;
	
	dmax = El(im1,0,0)/(D)LUMMAX;
	dmin = El(im1,0,0)/(D)LUMMAX;
 
	/* Find minimum and maximum values in array. */
	for (iy = 0; iy<ysize; iy++)
	{
		for (ix = 0; ix<xsize; ix++)
		{
			tmp = (double)El(im1,ix,iy)/LUMMAX;

			/* increment this histogram bin */
			hist[(int)((hlim+0.999)*tmp)]++;

			/* update min/max values, if necessary */
			if (tmp > dmax) dmax = tmp;
			if (tmp < dmin) dmin = tmp;
		}
	} 

	p10 = 0;
	num_pixels = xsize*ysize;
	p10lim = num_pixels*threshold;	/* how many pixels need to exceed level */
	ix = hlim;						/* maximum value */
	p10i = ix;

	/*
	 * Find the bucket that has "threshold" percent of the pixels in buckets
	 * above it.
	 */
	do {
		p10 += hist[ix];
		p10i = ix;
		ix--;
	} while ((p10 < p10lim) && (ix>0));

	/*
	 * Return the bucket value divided by the number of buckets.
	 * We're returning a percentage? -- mjh
	 */
	retval = p10i/hlim;
	return(retval);
 
}



/*========================================================================
 * Name:       sparkl
 * Prototype:  sparkl( Cimg *im1, Cimg *im2p, LUMTYPE *lum, 
 *							  D lumlim, D cstatf, D lscale, D iscale )
 *
 * Description:
 * Make a new image array im2[x,y], filling it with a  processed
 * copy of im1[x,y].
 *
 * This is the main routine here. All else is, um, else.
 *
 *========================================================================*/
int 
sparkl(
	Cimg 		*im1,
	Cimg		*im2p,
	LUMTYPE	*lum,
	D			lumlim, 
	D			csatf, 
	D			lscale,
	D			iscale
)
{
	unsigned char		*rpt1, *gpt1, *bpt1;
	unsigned char		*rpt2, *gpt2, *bpt2, *apt2;

	D			tmp;
	D			nfrac;			/* normalized fraction */
	int		ix,iy;
	size_t	bufsize;
	D			length, intens;
	U			xsize, ysize;
	int		cur_progress, max_progress;

	/* initialize the progress dialog*/
	gimp_init_progress("Sparkling the image...");

	/* allocate a new image that is the same size as the original */
	xsize = im2p->xsize = im1->xsize;
	ysize = im2p->ysize = im1->ysize;
	bufsize = (size_t) (im1->xsize * im1->ysize * sizeof(PIXTYPE));
	rpt2 = im2p->r = (PIXTYPE *) malloc(bufsize);
	gpt2 = im2p->g = (PIXTYPE *) malloc(bufsize);
	bpt2 = im2p->b = (PIXTYPE *) malloc(bufsize);
	apt2 = im2p->a = (PIXTYPE *) malloc(bufsize);
 
	if (im2p->r == NULL || im2p->g==NULL || im2p->b==NULL || im2p->a==NULL)
		return(1);
 
	rpt1 = im1->r;
	gpt1 = im1->g;
	bpt1 = im1->b;

	/* copy old array into new one */
	for (iy = 0; iy < ysize; iy++)
	{
		for (ix = 0; ix < xsize; ix++)
		{
			/*
			*rpt2 = *rpt1; rpt1++; rpt2++;
			*gpt2 = *gpt1; gpt1++; gpt2++;
			*bpt2 = *bpt1; bpt1++; bpt2++;
			*/
			Elrp(im2p,ix,iy) = Elrp(im1,ix,iy);  /* red, green, blue planes */
			Elgp(im2p,ix,iy) = Elgp(im1,ix,iy);
			Elbp(im2p,ix,iy) = Elbp(im1,ix,iy);
			Elap(im2p,ix,iy) = Elap(im1,ix,iy);
		}
	}
 
	cur_progress=0;
	max_progress=ysize;

	/* add effects to new image based on intensity of old pixels */
	for (iy = 0; iy<ysize; iy++)
	{
		for (ix = 0; ix<xsize; ix++)
		{
			tmp = El(lum,ix,iy)/(double)(LUMMAX);
			if (tmp > lumlim)
			{
				nfrac = ABS((tmp-lumlim)/(1-lumlim));
				length = lscale * pow(nfrac,0.8);
				intens = iscale * pow(nfrac,1);

				/* fspike im x,y intens rlength angle */
				if (s_points > 0)
				{
					/* major spikes */
					fspike(im1, im2p, ix, iy, intens, csatf, length, sp_angle);

					/* minor spikes */
					fspike( im1, im2p, ix, iy, (intens*0.7), csatf,
						(length*0.7), (sp_angle+180.0/s_points));
				}

				/* feature enhancment: ring im x,y rad width intens */
				/* ring(im2p,ix,iy,length,length/6,intens*0.7); */

			}
		}
		if ((++cur_progress % 5) == 0)
			gimp_do_progress (cur_progress, max_progress);

	}
	gimp_do_progress (1, 1);
	return(0);
}


/*========================================================================
 * Name:       fspike
 * Prototype:  fspike( Cimg *im1p, Cimg *im2p, D xr, D yr, D intens, 
 *							  D csatf, D rscale, D th1 )
 *
 * Description:
 * Draw a diffraction spike pattern with a set of fading lines.
 *
 *	Parameters:
 *	im1p			pointer to original image array 
 *	im2p			pointer to copy of original image array (which is updated)
 *	th1			angle in degrees
 *	xr,yr			start position
 *	intens		pixel intensity
 *	rscale		1/e length of line (in pixels)
 *
 *	Local Variables:
 *	s_points		number of major spike points
 *	xrt,yrt		x,y temporary position
 *	dx,dy			increments to xrt,yrt position variables
 *	rpos			current distance drawn
 *	inloc			pixel intensity at this location
 *	theta			temporary angle variable
 *	efac			falloff exponent
 *
 *========================================================================*/
void 
fspike(
	Cimg	*im1p,
	Cimg	*im2p,
	D		xr,
	D		yr,
	D		intens,
	D		csatf,
	D		rscale,
	D		th1
)
{
	D	xrt, yrt, dx, dy, rpos, inr, ing, inb;
	D	valr, valg, valb;  /* floating-point red,green,blue value on [0..1] */
	D	ho, so, vo;        /* HSV [0..1] values for initial pixel */
	D	snew;              /* current value of saturation */
	D	theta, efac;
	D	sfac;              /* scaling factor 1..0 in distance along spike */
	int i;
	int x,y;

	theta = th1;
	efac = 2.0;    /* exponent on intensity falloff with radius */

	/* draw the major spikes */
	for (i=0;i<s_points;i++)
	{
		x = (int)(xr+0.5); y=(int)(yr+0.5);
		valr = Elr(im1p[0],x,y)/(D)PIXMAX;
		valg = Elg(im1p[0],x,y)/(D)PIXMAX;
		valb = Elb(im1p[0],x,y)/(D)PIXMAX;
		RGB_to_HSL(valr,valg,valb,&ho,&so,&vo);
		dx = 0.2* cos(theta*pi/180);
		dy = 0.2* sin(theta*pi/180);
		xrt = xr; yrt = yr;
		rpos = 0.2;
		inr = ing = inb =1.0;
		while ((inr > 0.01) || (ing > 0.01) || (inb>0.01))
		{
			sfac = exp(-pow(rpos/rscale,efac));
			snew = (sfac)*csatf + (1-sfac)*so; /* saturation interpolation */
			snew = 1.0;
			sfac *= intens;
			HSL_to_RGB(ho,snew,vo,&valr,&valg,&valb);
			inr = 0.2 * valr * sfac;
			ing = 0.2 * valg * sfac;
			inb = 0.2 * valb * sfac;

			rpnt(im2p,xrt,yrt,inr,ing,inb);

			xrt += dx;
			yrt += dy;
			rpos += 0.2;

		}
	
		theta += 360.0/s_points;
	}
}



/*========================================================================
 * Name:       rpnt
 * Prototype:  rpnt(Cimg *im2p,D xr, D yr, D ri, D gi, D bi)
 *
 * Description:
 *	Given the coordinates xr,yr in a 3-channel image, redraw the point
 *	using a gaussian point-spread function truncated to the nearest
 *	four pixels. ri,gi,bi are real-valued red,green,blue increments.
 *
 *========================================================================*/
void 
rpnt(
	Cimg	*im2p,
	D		xr, 
	D		yr, 
	D		ri, 
	D		gi, 
	D		bi
)
{
	int	x,y;
	D		dx,dy,rs;
	D		valr, valg, valb;
	Cimg	im2;
 
	/*
	 * I don't like macros like this, but I'm unclear on the interrelation
	 * between iterations of the macro so I left John's original code as
	 * is. I think it could have been done if four calls to rpnt(), but
	 * I'm not positive (or got lazy).  -- mjh
	 */
#define ADDPIXEL \
			dx = xr - x; dy = yr - y;  \
			rs = dx*dx + dy*dy; \
			valr = ri * exp(-rs/PSV); \
			valg = gi * exp(-rs/PSV); \
			valb = bi * exp(-rs/PSV); \
			if (x>=0 && y>=0 && x<xsize && y<ysize) \
			{ \
				valr += Elr(im2,x,y)/(double)PIXMAX; \
				valg += Elg(im2,x,y)/(double)PIXMAX; \
				valb += Elb(im2,x,y)/(double)PIXMAX; \
				if (valr > 1.0) valr = 1.0; \
				if (valg > 1.0) valg = 1.0; \
				if (valb > 1.0) valb = 1.0; \
				Elr(im2,x,y) = (PIXTYPE)(valr*PIXMAX); \
				Elg(im2,x,y) = (PIXTYPE)(valg*PIXMAX); \
				Elb(im2,x,y) = (PIXTYPE)(valb*PIXMAX); \
			}

	im2 = im2p[0];		/* dereference the image pointer */

	x = (int) (xr);	/* integer coord. to upper left of real point */
	y = (int) (yr);

	ADDPIXEL;
	x++;                   /* upper right */
	ADDPIXEL;
	y++;                   /* lower right */
	ADDPIXEL;
	x--;                   /* lower left */
	ADDPIXEL;
 
}
