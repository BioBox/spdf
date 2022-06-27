
// Templated code must be put in the header, otherwise the compiler won't know
// what code to generate and give linker errors

#ifndef RECTANGLE_H
#define RECTANGLE_H

#include <algorithm>
#include <concepts>
#include <vector>

#include <poppler-rectangle.h>

template <typename T>
concept numeric = std::integral<T> or std::floating_point<T>;

template <class T>
requires numeric<T>
class srectangle : public poppler::rectangle<T> {
public:
  srectangle();
  srectangle(T x, T y, T w, T h);
  srectangle(const srectangle<T> &r);
  srectangle<T> &operator=(const srectangle<T> &r);
  srectangle<T> normalized() const;
  srectangle padded(int p) const;
};

template <numeric T>
srectangle<T> intersect(const srectangle<T> &a, const srectangle<T> &b);

template <numeric T>
std::vector<srectangle<T>> subtract(const srectangle<T> &a,
                                    const srectangle<T> &b);

template <numeric T> bool is_invalid(const srectangle<T> &p);

template <numeric T>
bool operator==(const srectangle<T> &a, const srectangle<T> &b);

template <numeric T>
bool operator!=(const srectangle<T> &a, const srectangle<T> &b);

template <class T>
requires numeric<T> srectangle<T>::srectangle() : poppler::rectangle<T>() {}

template <class T>
requires numeric<T> srectangle<T>::srectangle(T x, T y, T w, T h)
    : poppler::rectangle<T>(x, y, w, h) {}

template <class T>
requires numeric<T> srectangle<T>::srectangle(const srectangle<T> &r) {
  this->operator=(r);
}

template <class T>
requires numeric<T> srectangle<T>
&srectangle<T>::operator=(const srectangle<T> &r) {
  this->set_left(r.x());
  this->set_top(r.y());
  this->set_right(r.x() + r.width());
  this->set_bottom(r.y() + r.height());
  return *this;
}

template <numeric T>
srectangle<T> intersect(const srectangle<T> &a, const srectangle<T> &b) {
  T x1 = std::max(a.x(), b.x());
  T x2 = std::min(a.x() + a.width(), b.x() + b.width());

  T y1 = std::max(a.y(), b.y());
  T y2 = std::min(a.y() + a.height(), b.y() + b.height());

  return {x1, y1, x2 - x1, y2 - y1};
}

template <numeric T>
std::vector<srectangle<T>> subtract(const srectangle<T> &a,
                                    const srectangle<T> &b) {

  if (is_invalid(intersect(a, b)))
    return {a};

  std::vector<srectangle<T>> d;

  if (a.y() < b.y())
    d.push_back(srectangle<T>{a.x(), a.y(), a.width(), b.y() - a.y()});

  if (a.y() + a.height() > b.y() + b.height())
    d.push_back(srectangle<T>{a.x(), b.y() + b.height(), a.width(),
                              a.y() + a.height() - b.y() + b.height()});

  if (a.x() < b.x())
    d.push_back(srectangle<T>{a.x(), b.y(), b.x() - a.x(), b.height()});

  if (a.x() + a.width() > b.x() + b.width())
    d.push_back(srectangle<T>{b.x() + b.width(), b.y(),
                              a.x() + a.width() - b.x() + b.width(),
                              b.height()});
  return d;
}

template <numeric T> bool is_invalid(const srectangle<T> &p) {
  return p.x() < 0 || p.y() < 0 || p.width() < 0 || p.height() < 0;
}

template <numeric T>
bool operator==(const srectangle<T> &a, const srectangle<T> &b) {
  return a.x() == b.x() && a.y() == b.y() && a.width() == b.width() &&
         a.height() == b.height();
}

template <numeric T>
bool operator!=(const srectangle<T> &a, const srectangle<T> &b) {
  return !(a == b);
}

template <class T>
requires numeric<T> srectangle<T> srectangle<T>::normalized()
const {
  T nx = this->x();
  T ny = this->y();
  T nw = this->width();
  T nh = this->height();

  if (nw < 0) {
    nw *= -1;
    nx -= nw;
  }

  if (nh < 0) {
    nh *= -1;
    ny -= nh;
  }

  return {nx, ny, nw, nh};
}

template <class T>
requires numeric<T> srectangle<T> srectangle<T>::padded(int p)
const {
  return {this->x() - p, this->y() - p, this->width() + 2 * p,
          this->height() + 2 * p};
}

using srect = srectangle<int>;
using srectf = srectangle<double>;

#endif
