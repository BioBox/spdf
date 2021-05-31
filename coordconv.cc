#include "coordconv.h"

CoordConv::CoordConv(const Page *p, const Rectangle &r) : rect(r)
{
	auto pbox = p->getCropBox();

	xscale = (pbox->x2 - pbox->x1) / double(rect.width);
	yscale = (pbox->y2 - pbox->y1) / double(rect.height);
}

double CoordConv::to_pdf_x(int x) const
{
	return (x - rect.x) * xscale;
}

double CoordConv::to_pdf_y(int y) const
{
	return (rect.height - (y - rect.y)) * yscale;
}
