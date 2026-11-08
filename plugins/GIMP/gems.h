/* gems.h: the colour conversions sparkl.c takes from Graphics Gems (rebuilt
 * for the Gobe Productive port) */
#ifndef GEMS_H
#define GEMS_H
#include "hcon.h"
void RGB_to_HSL(D r, D g, D b, D *h, D *s, D *l);
void HSL_to_RGB(D h, D s, D l, D *r, D *g, D *b);
#endif
