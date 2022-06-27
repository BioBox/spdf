#include <algorithm>

#include "coordconv.hpp"

CoordConv::CoordConv(const poppler::page *p, const srect &r, bool i,
                     int rotation)
    : rect(r), inverty(i) {
  auto pbox = p->page_rect();

  xscale = (pbox.width()) / double(rect.width());
  yscale = (pbox.height()) / double(rect.height());
}

double CoordConv::to_pdf_x(int x) const { return (x - rect.x()) * xscale; }

double CoordConv::to_pdf_y(int y) const {
  if (inverty)
    return (rect.height() - (y - rect.y())) * yscale;
  return (y - rect.y()) * yscale;
}

srect CoordConv::to_pdf(const srect &r) const {
  return {(int)to_pdf_x(r.x()), (int)to_pdf_y(r.y()),
          (int)(to_pdf_x(r.x() + r.width()) - to_pdf_x(r.x())),
          (int)(to_pdf_y(r.y() + r.height()) - to_pdf_y(r.y()))};
}

double CoordConv::to_screen_x(int x) const { return x / xscale + rect.x(); }

double CoordConv::to_screen_y(int y) const {
  if (inverty)
    return rect.y() + rect.height() - y / yscale;
  return y / yscale + rect.y();
}

srect CoordConv::to_screen(const srectf &r) const {
  return {(int)to_screen_x(r.x()), (int)to_screen_y(r.y()),
          (int)(to_screen_x(r.x() + r.width()) - to_screen_x(r.x())),
          (int)(to_screen_y(r.y() + r.height()) - to_screen_y(r.y()))};
}
