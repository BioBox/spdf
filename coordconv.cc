#include "coordconv.h"

CoordConv::CoordConv(const Page *p, const Rectangle &r, bool i) : rect(r), inverty(i)
{
	auto pbox = p->getCropBox();

	xscale = (pbox->x2 - pbox->x1) / double(rect.width);
	yscale = (pbox->y2 - pbox->y1) / double(rect.height);
}

CoordConv::CoordConv(const Page *p, const Rectangle &r) : CoordConv(p, r, true) {}

double CoordConv::to_pdf_x(int x) const
{
	return (x - rect.x) * xscale;
}

double CoordConv::to_pdf_y(int y) const
{
	if (inverty)
		return (rect.height - (y - rect.y)) * yscale;
	return (y - rect.y) * yscale;
}

Rectangle CoordConv::to_pdf(const Rectangle &r) const
{
	return {
		(int)to_pdf_x(r.x),
		(int)to_pdf_y(r.y),
		(int)(to_pdf_x(r.x + r.width) - to_pdf_x(r.x)),
		(int)(to_pdf_y(r.y + r.height) - to_pdf_y(r.y))
	};
}

double CoordConv::to_screen_x(int x) const
{
	return x / xscale + rect.x;
}

double CoordConv::to_screen_y(int y) const
{
	if (inverty)
		return rect.y + rect.height - y / yscale;
	return y / yscale + rect.y;
}

Rectangle CoordConv::to_screen(const Rectangle &r) const
{
	return {
		(int)to_screen_x(r.x),
		(int)to_screen_y(r.y),
		(int)(to_screen_x(r.x + r.width) - to_screen_x(r.x)),
		(int)(to_screen_y(r.y + r.height) - to_screen_y(r.y))
	};
}
