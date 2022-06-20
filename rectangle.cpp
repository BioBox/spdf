#include <algorithm>

#include "rectangle.hpp"

using namespace std;

Rectangle intersect(const Rectangle &a, const Rectangle &b) {
  int x1 = max(a.x, b.x);
  int x2 = min(a.x + a.width, b.x + b.width);

  int y1 = max(a.y, b.y);
  int y2 = min(a.y + a.height, b.y + b.height);

  return {x1, y1, x2 - x1, y2 - y1};
}

vector<Rectangle> subtract(const Rectangle &a, const Rectangle &b) {
  if (is_invalid(intersect(a, b)))
    return {a};

  vector<Rectangle> d;

  if (a.y < b.y)
    d.push_back(Rectangle{a.x, a.y, a.width, b.y - a.y});

  if (a.y + a.height > b.y + b.height)
    d.push_back(Rectangle{a.x, b.y + b.height, a.width,
                          a.y + a.height - b.y + b.height});

  if (a.x < b.x)
    d.push_back(Rectangle{a.x, b.y, b.x - a.x, b.height});

  if (a.x + a.width > b.x + b.width)
    d.push_back(
        Rectangle{b.x + b.width, b.y, a.x + a.width - b.x + b.width, b.height});

  return d;
}

bool is_invalid(const Rectangle &p) {
  return p.x < 0 || p.y < 0 || p.width < 0 || p.height < 0;
}

bool operator==(const Rectangle &a, const Rectangle &b) {
  return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

bool operator!=(const Rectangle &a, const Rectangle &b) { return !(a == b); }

Rectangle Rectangle::normalized() const {
  auto [nx, ny, nw, nh] = *this;

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

Rectangle Rectangle::padded(int p) const {
  return {x - p, y - p, width + 2 * p, height + 2 * p};
}
