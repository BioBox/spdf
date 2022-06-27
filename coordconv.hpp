#ifndef SCALER_H
#define SCALER_H

#include "rectangle.hpp"
#include <poppler-page.h>

struct CoordConv {
  CoordConv(const poppler::page *p, const srect &r, bool i, int rotation);
  double to_pdf_x(int x) const;
  double to_pdf_y(int y) const;
  srect to_pdf(const srect &r) const;
  double to_screen_x(int x) const;
  double to_screen_y(int y) const;
  srect to_screen(const srectf &r) const;

private:
  double xscale, yscale;
  srect rect;
  bool inverty;
};

#endif
