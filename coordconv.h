#ifndef SCALER_H
#define SCALER_H

#include "rectangle.h"
#include <poppler/Page.h>

struct CoordConv {
  CoordConv(const Page *p, const Rectangle &r, bool i, int rotation);
  double to_pdf_x(int x) const;
  double to_pdf_y(int y) const;
  Rectangle to_pdf(const Rectangle &r) const;
  double to_screen_x(int x) const;
  double to_screen_y(int y) const;
  Rectangle to_screen(const Rectangle &r) const;

private:
  double xscale, yscale;
  Rectangle rect;
  bool inverty;
};

#endif
