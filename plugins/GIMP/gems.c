/* gems.c: RGB to HSL and back, after Ken Fishkin's in Graphics Gems I
 * (red, green, blue, hue, saturation and lightness all 0 to 1), as the
 * shipped sparkl has them; written for the Gobe Productive port. */
#include "gems.h"

#define MAX3(a, b, c) ((a) > (b) ? ((a) > (c) ? (a) : (c)) : ((b) > (c) ? (b) : (c)))
#define MIN3(a, b, c) ((a) < (b) ? ((a) < (c) ? (a) : (c)) : ((b) < (c) ? (b) : (c)))

void
RGB_to_HSL(D r, D g, D b, D *h, D *s, D *l)
{
	D v = MAX3(r, g, b), m = MIN3(r, g, b), vm, r2, g2, b2;
	*h = 0;
	*s = 0;
	if ((*l = (m + v) / 2.0) <= 0.0)
		return;
	if ((*s = vm = v - m) > 0.0)
		*s /= (*l <= 0.5) ? (v + m) : (2.0 - v - m);
	else
		return;
	r2 = (v - r) / vm;
	g2 = (v - g) / vm;
	b2 = (v - b) / vm;
	if (r == v)
		*h = (g == m ? 5.0 + b2 : 1.0 - g2);
	else if (g == v)
		*h = (b == m ? 1.0 + r2 : 3.0 - b2);
	else
		*h = (r == m ? 3.0 + g2 : 5.0 - r2);
	*h /= 6.0;
}


void
HSL_to_RGB(D h, D sl, D l, D *r, D *g, D *b)
{
	D v = (l <= 0.5) ? (l * (1.0 + sl)) : (l + sl - l * sl);
	if (v <= 0) {
		*r = *g = *b = 0.0;
	} else {
		D m = l + l - v, sv = (v - m) / v, fract, vsf, mid1, mid2;
		int sextant;
		h *= 6.0;
		sextant = (int)h;
		fract = h - sextant;
		vsf = v * sv * fract;
		mid1 = m + vsf;
		mid2 = v - vsf;
		switch (sextant) {
			case 0: *r = v; *g = mid1; *b = m; break;
			case 1: *r = mid2; *g = v; *b = m; break;
			case 2: *r = m; *g = v; *b = mid1; break;
			case 3: *r = m; *g = mid2; *b = v; break;
			case 4: *r = mid1; *g = m; *b = v; break;
			case 5: *r = v; *g = m; *b = mid2; break;
			default: *r = v; *g = m; *b = mid2; break;
		}
	}
}
