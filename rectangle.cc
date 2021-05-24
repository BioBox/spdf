#include <algorithm>

#include "rectangle.h"

using namespace std;

Rectangle intersect(const Rectangle &a, const Rectangle &b)
{
	int x1 = max(a.x, b.x);
	int x2 = min(a.x + a.width, b.x + b.width);

	int y1 = max(a.y, b.y);
	int y2 = min(a.y + a.height, b.y + b.height);

	return {x1, y1, x2 - x1, y2 - y1};
}

bool is_invalid(const Rectangle &p)
{
	return p.x < 0 || p.y < 0 || p.width < 0 || p.height < 0;
}
