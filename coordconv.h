#ifndef SCALER_H
#define SCALER_H

#include <poppler/Page.h>
#include "rectangle.h"

struct CoordConv
{
	CoordConv(const Page *p, const Rectangle &r);
	double to_pdf_x(int x) const;
	double to_pdf_y(int y) const;

private:
	double xscale, yscale;
	Rectangle rect;
};

#endif
