#ifndef RECTANGLE_H
#define RECTANGLE_H

#include <vector>

struct Rectangle {
	int x, y;
	int width, height;
};

Rectangle intersect(const Rectangle &a, const Rectangle &b);
std::vector<Rectangle> subtract(const Rectangle &a, const Rectangle &b);
bool is_invalid(const Rectangle &p);
bool operator==(const Rectangle &a, const Rectangle &b);
bool operator!=(const Rectangle &a, const Rectangle &b);

#endif
