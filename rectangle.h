#ifndef RECTANGLE_H
#define RECTANGLE_H

struct Rectangle {
	int x, y;
	int width, height;
};

Rectangle intersect(const Rectangle &a, const Rectangle &b);
bool is_invalid(const Rectangle &p);

#endif
