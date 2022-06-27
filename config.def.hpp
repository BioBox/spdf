
#include <climits>

#include <X11/Xutil.h>
#include <X11/keysym.h>

#define AnyMask UINT_MAX
#define EmptyMask 0

enum Action {
  QUIT,
  NEXT,
  PREV,
  FIRST,
  LAST,
  FIT_PAGE,
  FIT_WIDTH,
  DOWN,
  UP,
  BACK,
  RELOAD,
  GOTO_PAGE,
  SEARCH,
  PAGE,
  MAGNIFY,
  ROTATE_CW,
  ROTATE_CCW
};

struct Shortcut {
  unsigned mask;
  KeySym ksym;
  Action action;
};

/*
 * Background color, X11 color name (see:
 * https://en.wikipedia.org/wiki/X11_color_names).
 */
static const char *bg_color = "Gray50";

/*
 * Parameters are: mask, keysym, action.
 * Mask can be any mask from XKeyEvent.state (see: man XKeyEvent) or
 * AnyMask (mask is ignored), or EmptyMask (empty mask, no key modifiers).
 * Keysym can be any keysym from /usr/include/X11/keysymdef.h.
 */
static Shortcut shortcuts[] = {{AnyMask, XK_q, QUIT},
                               {EmptyMask, XK_Escape, QUIT},
                               {ControlMask, XK_Page_Down, NEXT},
                               {ControlMask, XK_Page_Up, PREV},
                               {ControlMask, XK_Home, FIRST},
                               {ControlMask, XK_End, LAST},
                               {EmptyMask, XK_z, FIT_PAGE},
                               {EmptyMask, XK_w, FIT_WIDTH},
                               {EmptyMask, XK_Down, DOWN},
                               {EmptyMask, XK_Up, UP},
                               {EmptyMask, XK_b, BACK},
                               {AnyMask, XK_r, RELOAD},
                               {AnyMask, XK_g, GOTO_PAGE},
                               {AnyMask, XK_s, SEARCH},
                               {EmptyMask, XK_slash, SEARCH},
                               {EmptyMask, XK_p, PAGE},
                               {EmptyMask, XK_m, MAGNIFY},
                               {EmptyMask, XK_bracketright, ROTATE_CW},
                               {EmptyMask, XK_bracketleft, ROTATE_CCW}};

/*
 * Scrolling speed (in page fractions).
 */
static double arrow_scroll = 0.01;
static double page_scroll = 0.30;
static double mouse_scroll = 0.02;

/*
 * Status line font, must be in X logical font description format
 * (see: https://en.wikipedia.org/wiki/X_logical_font_description).
 */
static const char *font = "-misc-fixed-medium-r-normal-*-14-*-*-*-*-*-*-*";
