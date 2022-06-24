#include <algorithm>
#include <cctype>
#include <charconv>
#include <climits>
#include <clocale>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stack>
#include <stdexcept>
#include <string>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <poppler/Annot.h>
#include <poppler/GlobalParams.h>
#include <poppler/Link.h>
#include <poppler/PDFDoc.h>
#include <poppler/Page.h>
#include <poppler/SplashOutputDev.h>
#include <poppler/TextOutputDev.h>
#include <poppler/goo/GooString.h>
#include <poppler/splash/SplashBitmap.h>

#include "coordconv.hpp"
#include "rectangle.hpp"

using namespace std;

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
  PG_DOWN,
  PG_UP,
  BACK,
  RELOAD,
  COPY,
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

#include "config.h"

static bool error(const string &m) {
  throw runtime_error(m);
  return false;
}

struct PageAndOffset {
  int page, offset;
};

struct AppState {
  unique_ptr<PDFDoc> doc;
  Page *page = NULL;
  int page_num;
  bool fit_page;
  bool scrolling_up;
  int next_pos_y = 0;
  stack<PageAndOffset> page_stack;

  Display *display = NULL;
  Window main;
  Rectangle main_pos;
  Pixmap pdf = None;
  Rectangle pdf_pos{0, 0, 0, 0};

  GC selection_gc;
  Rectangle selection{0, 0, 0, 0};
  Rectangle pdf_selection{0, 0, 0, 0};
  bool selecting = false;

  unique_ptr<GooString> primary;
  unique_ptr<GooString> clipboard;

  GC status_gc;
  GC text_gc;
  XFontSet fset = NULL;
  int fheight;
  int fbase;
  Rectangle status_pos;
  string prompt;
  string value;
  bool status = false;
  bool input = false;

  double left, top, right, bottom;
  bool searching = false;

  bool xembed_init = false;

  bool magnifying = false;
  Rectangle magnify;
  int pre_mag_y;

  int rotation = 0;
};

struct SetupXRet {
  Display *display;
  Window main;
  GC selection;
  GC status;
  GC text;
  XFontSet fset;
  int fheight;
  int fbase;
};

static SetupXRet setup_x(unsigned width, unsigned height,
                         const string &file_name, Window root) {
  Display *display = XOpenDisplay(NULL);
  (!display) && error("Cannot open X display.");

  XColor sc, ec;
  XAllocNamedColor(display, DefaultColormap(display, DefaultScreen(display)),
                   bg_color, &sc, &ec);

  if (root == None)
    root = DefaultRootWindow(display);
  Window main =
      XCreateSimpleWindow(display, root, 0, 0, width, height, 2, 0, ec.pixel);

  string window_name("spdf: " + file_name);
  string icon_name("spdf");

  Xutf8SetWMProperties(display, main, window_name.c_str(), icon_name.c_str(),
                       NULL, 0, NULL, NULL, NULL);

  /*
   * If WM does not understand WM_NAME and WM_ICON_NAME of type COMPOUND_TEXT
   * set by Xutf8SetWMProperties() and needs _NET_WM_* properties of type
   * UTF8_STRING.
   */
  Atom utf8_string_atom = XInternAtom(display, "UTF8_STRING", False);
  Atom wm_name_atom = XInternAtom(display, "_NET_WM_NAME", False);
  Atom wm_icon_name_atom = XInternAtom(display, "_NET_WM_ICON_NAME", False);

  XChangeProperty(display, main, wm_name_atom, utf8_string_atom, 8,
                  PropModeReplace, (const unsigned char *)window_name.c_str(),
                  window_name.size());
  XChangeProperty(display, main, wm_icon_name_atom, utf8_string_atom, 8,
                  PropModeReplace, (const unsigned char *)icon_name.c_str(),
                  icon_name.size());

  Atom wmdel_atom = XInternAtom(display, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(display, main, &wmdel_atom, 1);

  XGCValues gcvals;
  gcvals.function = GXinvert;
  GC gc = XCreateGC(display, main, GCFunction, &gcvals);

  XGCValues gcvals2;
  gcvals2.foreground = WhitePixel(display, DefaultScreen(display));
  GC gc2 = XCreateGC(display, main, GCForeground, &gcvals2);

  int nmissing;
  char **missing;
  char *def_string;
  XFontSet fset =
      XCreateFontSet(display, font, &missing, &nmissing, &def_string);
  (!fset) && error("Cannot create font set.");
  XFreeStringList(missing);

  int fheight = 0, fbase = 0;
  XFontStruct **fonts;
  char **font_names;
  int nfonts = XFontsOfFontSet(fset, &fonts, &font_names);
  for (int i = 0; i < nfonts; ++i) {
    fbase = max(fbase, fonts[i]->descent);
    fheight = max(fheight, fonts[i]->ascent + fonts[i]->descent);
  }

  XMapWindow(display, main);
  XSelectInput(display, main,
               KeyPressMask | ButtonPressMask | ButtonReleaseMask |
                   Button1MotionMask | StructureNotifyMask | ExposureMask);

  return {display, main, gc,      DefaultGC(display, DefaultScreen(display)),
          gc2,     fset, fheight, fbase};
}

static void cleanup_x(const AppState &st) {
  if (st.fset != NULL)
    XFreeFontSet(st.display, st.fset);
  if (st.pdf != None)
    XFreePixmap(st.display, st.pdf);
  if (st.display != NULL)
    XCloseDisplay(st.display);
}

struct PdfRenderConf {
  double dpi;
  Rectangle pos;
  Rectangle crop;
};

PdfRenderConf get_pdf_render_conf(bool fit_page, bool scrolling_up, int offset,
                                  Rectangle p, const Page *page,
                                  bool magnifying, Rectangle m, int rotation) {
  auto rect = page->getCropBox();
  auto x0 = rect->x1;
  auto y0 = rect->y1;
  auto width = rect->x2 - rect->x1;
  auto height = rect->y2 - rect->y1;

  if (abs(page->getRotate() - rotation) % 180 != 0)
    swap(height, width);

  if (magnifying) {
    x0 = m.x;
    y0 = m.y;
    width = m.width;
    height = m.height;
  }

  int x, y, w, h;
  double dpi;
  if (fit_page) {
    if (double(p.width) / double(p.height) > width / height) {
      h = p.height;
      dpi = double(p.height) * 72.0 / height;
      w = width * dpi / 72.0;

      y = 0;
      x = (p.width - w) / 2;
    } else {
      w = p.width;
      dpi = double(p.width) * 72.0 / width;
      h = height * dpi / 72.0;

      x = 0;
      y = (p.height - h) / 2;
    }
  } else {
    w = p.width;
    dpi = double(p.width) * 72.0 / width;
    h = height * dpi / 72.0;

    x = 0;
    if (double(p.width) / double(p.height) <= width / height) {
      y = (p.height - h) / 2;
    } else {
      if (!scrolling_up)
        y = offset;
      else
        y = p.height - h;
    }
  }

  auto scale = dpi / 72.0;
  return {dpi,
          {x, y, w, h},
          {int(x0 * scale), int(y0 * scale), int(width * scale),
           int(height * scale)}};
}

static Pixmap render_pdf_page_to_pixmap(const AppState &st,
                                        const PdfRenderConf &prc) {
  SplashColor paper{0xff, 0xff, 0xff};

  SplashOutputDev sdev(splashModeXBGR8, 4, false, paper);
  sdev.setFontAntialias(true);
  sdev.setVectorAntialias(true);

  sdev.startDoc(st.doc.get());
  st.doc->displayPageSlice(&sdev, st.page_num, prc.dpi, prc.dpi, st.rotation,
                           false, true, false, prc.crop.x, prc.crop.y,
                           prc.crop.width, prc.crop.height);

  SplashBitmap *img = sdev.getBitmap();

  auto xim = XCreateImage(st.display,
                          DefaultVisual(st.display, DefaultScreen(st.display)),
                          24, ZPixmap, 0, (char *)img->getDataPtr(),
                          img->getWidth(), img->getHeight(), 32, 0);

  Pixmap pxm = XCreatePixmap(
      st.display, DefaultRootWindow(st.display), img->getWidth(),
      img->getHeight(), DefaultDepth(st.display, DefaultScreen(st.display)));

  XPutImage(st.display, pxm, DefaultGC(st.display, DefaultScreen(st.display)),
            xim, 0, 0, 0, 0, img->getWidth(), img->getHeight());

  xim->data = NULL;
  XDestroyImage(xim);

  return pxm;
}

static void copy_pixmap_on_expose_event(const AppState &st,
                                        const Rectangle &prev,
                                        const XExposeEvent &e) {
  if (st.pdf_pos != prev) {
    auto diff = subtract(prev, st.pdf_pos);
    for (size_t i = 0; i < diff.size(); ++i) {
      XClearArea(st.display, st.main, diff[i].x, diff[i].y, diff[i].width,
                 diff[i].height, False);
    }
  }

  Rectangle dirty = intersect({e.x, e.y, e.width, e.height}, st.pdf_pos);
  if (!is_invalid(dirty)) {
    XCopyArea(st.display, st.pdf, st.main,
              DefaultGC(st.display, DefaultScreen(st.display)),
              dirty.x - st.pdf_pos.x, dirty.y - st.pdf_pos.y, dirty.width,
              dirty.height, dirty.x, dirty.y);

    const CoordConv cc(st.page, st.pdf_pos, false, st.rotation);
    auto rs = st.selecting ? st.selection.normalized()
                           : cc.to_screen(st.pdf_selection);
    if (rs.width > 0 && rs.height > 0) {
      dirty = intersect({e.x, e.y, e.width, e.height}, rs);
      if (!is_invalid(dirty))
        XFillRectangle(st.display, st.main, st.selection_gc, dirty.x, dirty.y,
                       dirty.width, dirty.height);
    }
  }

  if (st.status) {
    if (is_invalid(intersect({e.x, e.y, e.width, e.height}, st.status_pos)))
      return;

    XFillRectangle(st.display, st.main, st.status_gc, st.status_pos.x,
                   st.status_pos.y, st.status_pos.width, st.status_pos.height);

    string str{st.prompt + st.value + "_"};
    if (!st.input)
      str = st.prompt;
    Xutf8DrawString(st.display, st.main, st.fset, st.text_gc,
                    st.status_pos.x + 1,
                    st.status_pos.y + st.status_pos.height - (st.fbase + 1),
                    str.c_str(), str.size());
  }
}

static void force_render_page(AppState &st, bool clear = true) {
  if (st.pdf != None && clear) {
    XFreePixmap(st.display, st.pdf);
    st.pdf = None;
  }

  XWindowAttributes attrs;
  XGetWindowAttributes(st.display, st.main, &attrs);

  XEvent e;
  e.type = ConfigureNotify;
  e.xconfigure.x = attrs.x;
  e.xconfigure.y = attrs.y;
  e.xconfigure.width = attrs.width;
  e.xconfigure.height = attrs.height;
  XSendEvent(st.display, st.main, False, StructureNotifyMask, &e);

  e.type = Expose;
  e.xexpose.x = 0;
  e.xexpose.y = 0;
  e.xexpose.width = attrs.width;
  e.xexpose.height = attrs.height;
  XSendEvent(st.display, st.main, False, ExposureMask, &e);
}

static void send_expose(const AppState &st, const Rectangle &r) {
  XEvent e;
  e.type = Expose;
  e.xexpose.x = r.x;
  e.xexpose.y = r.y;
  e.xexpose.width = r.width;
  e.xexpose.height = r.height;
  XSendEvent(st.display, st.main, False, ExposureMask, &e);
}

static int get_pdf_scroll_diff(const AppState &st, double percent) {
  if (st.pdf_pos.height < st.main_pos.height)
    return 0;

  int sc = st.pdf_pos.height * percent;
  if (sc > 0)
    return min(sc, -st.pdf_pos.y);

  return -min(-sc, st.pdf_pos.height - st.main_pos.height + st.pdf_pos.y);
}

static bool find_page_link(AppState &st, const XButtonEvent &e) {
  unique_ptr<Links> links(st.page->getLinks());
  if (links->getLinks().empty())
    return false;

  const CoordConv cc(st.page, st.pdf_pos, true, st.rotation);
  double ex = cc.to_pdf_x(e.x);
  double ey = cc.to_pdf_y(e.y);

  for (int i = 0; i < links->getLinks().size(); ++i) {
    auto link = links->getLinks()[i];
    auto rect = link->getRect();

    if (ex >= rect.x1 && ex <= rect.x2 && ey >= rect.y1 && ey <= rect.y2) {
      auto action = link->getAction();
      if (action->isOk() && action->getKind() == actionGoTo) {
        unique_ptr<LinkDest> dptr;
        auto dest = ((LinkGoTo *)action)->getDest();

        if (!dest) {
          auto name = ((LinkGoTo *)action)->getNamedDest();

          dptr = st.doc->findDest(name);
          dest = dptr.get();
        }

        if (dest) {
          int prev_page = st.page_num;

          if (dest->isPageRef())
            st.page_num = st.doc->findPage(dest->getPageRef());
          else
            st.page_num = dest->getPageNum();

          if (prev_page != st.page_num) {
            st.page_stack.push({prev_page, st.pdf_pos.y});
            return true;
          }
        }
      }
    }
  }

  return false;
}

static void copy_text(AppState &st, bool primary) {
  TextOutputDev tdev(nullptr, false, 0, false, false);

  st.doc->displayPage(&tdev, st.page_num, 72, 72, st.rotation, false, true,
                      false);

  const CoordConv cc(st.page, st.pdf_pos, false, st.rotation);

  auto sr = st.selection.normalized();
  auto x1 = cc.to_pdf_x(sr.x);
  auto y1 = cc.to_pdf_y(sr.y);

  auto x2 = cc.to_pdf_x(sr.x + sr.width);
  auto y2 = cc.to_pdf_y(sr.y + sr.height);

  if (primary) {
    st.primary.reset(tdev.getText(x1, y1, x2, y2));
    XSetSelectionOwner(st.display, XA_PRIMARY, st.main, CurrentTime);
  } else {
    st.clipboard.reset(tdev.getText(x1, y1, x2, y2));
    XSetSelectionOwner(st.display, XInternAtom(st.display, "CLIPBOARD", 0),
                       st.main, CurrentTime);
  }
}

static Rectangle get_status_pos(const AppState &st) {
  return {0, st.main_pos.height - (st.fheight + 2), st.main_pos.width,
          st.fheight + 2};
}

static void search_text(AppState &st) {
  bool backwards = false;
  bool ignore_case = false;
  bool whole_words = false;
  string str = st.value;
  while (str.back() == '?' || str.back() == '~' || str.back() == '%') {
    char flag = str.back();
    if (flag == '?')
      backwards = true;
    if (flag == '~')
      ignore_case = true;
    if (flag == '%')
      whole_words = true;
    str.pop_back();
  }

  vector<wchar_t> wstr(str.size());
  mbstowcs(wstr.data(), str.c_str(), str.size());

  vector<Unicode> search{wstr.begin(), find(wstr.begin(), wstr.end(), 0)};

  bool found = false;
  bool whole = false;
  int page = st.page_num;

  while (!whole) {
    TextOutputDev tdev(nullptr, false, 0, false, false);

    st.doc->displayPage(&tdev, page, 72, 72, 0, false, true, false);

    found = tdev.takeText()->findText(search.data(), search.size(),
                                      !st.searching, true, st.searching, false,
                                      !ignore_case, backwards, whole_words,
                                      &st.left, &st.top, &st.right, &st.bottom);

    if (found)
      break;

    if (!backwards && page < st.doc->getNumPages()) {
      ++page;
      st.searching = false;
    } else if (backwards && page > 1) {
      --page;
      st.searching = false;
    } else {
      whole = true;
    }
  }

  if (found) {
    if (page != st.page_num) {
      st.page_num = page;

      st.page = st.doc->getPage(st.page_num);
      (!st.page) &&
          error("Cannot create page: " + to_string(st.page_num) + ".");
      force_render_page(st);
    }

    const CoordConv cc(st.page, st.pdf_pos, false, st.rotation);

    st.pdf_selection = {int(st.left), int(st.top), int(st.right - st.left),
                        int(st.bottom - st.top)};
    st.selection = cc.to_screen(st.pdf_selection);
  } else {
    st.pdf_selection = st.selection = {0, 0, 0, 0};
  }

  st.searching = found;
}

struct Args {
  string fname;
  Window root;
};

Args parse_args(int argc, char **argv) {
  string fname = "";
  Window root = None;

  for (int i = 1; i < argc; ++i) {
    if (string(argv[i]) == "-w") {
      if (i < argc - 1) {
        root = strtol(argv[++i], NULL, 0);
        (root == 0) && error("Invalid window (-w) value.");
      } else
        error("Missing window (-w) parameter.");
    } else
      fname = string(argv[i]);
  }

  if (fname == "")
    error(string("Missing pdf file, usage: ") + argv[0] +
          " [-w window] pdf_file.");

  return {fname, root};
}

int main(int argc, char **argv) {
  setlocale(LC_ALL, "");

  AppState st;
  try {
    auto args = parse_args(argc, argv);

    string file_name(args.fname);

    GlobalParamsIniter global_params(NULL);

    st.doc.reset(new PDFDoc(
        std::unique_ptr<GooString>(new GooString(file_name.c_str()))));
    (!st.doc->isOk()) && error("Error loading specified pdf file.");

    st.page_num = 1;
    st.page = st.doc->getPage(st.page_num);
    (!st.page) && error("Document has no pages.");

    auto rect = st.page->getCropBox();
    auto xret =
        setup_x(rect->x2 - rect->x1, rect->y2 - rect->y1, file_name, args.root);

    st.display = xret.display;
    st.main = xret.main;

    st.fit_page = true;
    st.scrolling_up = false;

    st.selection_gc = xret.selection;
    st.status_gc = xret.status;
    st.text_gc = xret.text;

    st.fset = xret.fset;
    st.fheight = xret.fheight;
    st.fbase = xret.fbase;

    XEvent event;
    while (true) {
      XNextEvent(st.display, &event);

      if (event.type == Expose) {
        auto prev = st.pdf_pos;
        if (st.pdf == None) {
          auto prc = get_pdf_render_conf(
              st.fit_page, st.scrolling_up, st.next_pos_y, st.main_pos, st.page,
              st.magnifying, st.magnify, st.rotation);
          st.scrolling_up = false;
          st.next_pos_y = 0;

          st.pdf = render_pdf_page_to_pixmap(st, prc);
          st.pdf_pos = prc.pos;
        }
        copy_pixmap_on_expose_event(st, prev, event.xexpose);
      }

      if (event.type == ConfigureNotify) {
        if (st.main_pos.width != event.xconfigure.width ||
            st.main_pos.height != event.xconfigure.height) {
          st.main_pos = {event.xconfigure.x, event.xconfigure.y,
                         event.xconfigure.width, event.xconfigure.height};

          XClearWindow(st.display, st.main);
          if (st.pdf != None) {
            XFreePixmap(st.display, st.pdf);
            st.pdf = None;
          }

          st.status_pos = get_status_pos(st);
        }
      }

      if (event.type == ClientMessage) {
        Atom xembed_atom = XInternAtom(st.display, "_XEMBED", False);
        Atom wmdel_atom = XInternAtom(st.display, "WM_DELETE_WINDOW", False);

        if (event.xclient.message_type == xembed_atom &&
            event.xclient.format == 32) {
          if (!st.xembed_init) {
            force_render_page(st);
            st.xembed_init = true;
          }
        } else if (event.xclient.data.l[0] == (long)wmdel_atom)
          goto endloop;
      }

      auto render_page_lambda = [&]() {
        st.page = st.doc->getPage(st.page_num);
        (!st.page) &&
            error("Cannot create page: " + to_string(st.page_num) + ".");
        force_render_page(st);
        st.selection = st.pdf_selection = {0, 0, 0, 0};
        st.selecting = false;
      };

      if (event.type == KeyPress) {
        KeySym ksym;
        char buf[64];
        XLookupString(&event.xkey, buf, sizeof(buf), &ksym, NULL);

        bool status = st.status;
        for (unsigned i = 0; i < sizeof(shortcuts) / sizeof(Shortcut); ++i) {
          auto sc = &shortcuts[i];
          if (!status && sc->ksym == ksym &&
              (sc->mask == AnyMask || sc->mask == event.xkey.state)) {
            if (sc->action == QUIT)
              goto endloop;

            if (sc->action == FIT_PAGE && !st.fit_page) {
              st.fit_page = true;
              force_render_page(st);
            }

            if (sc->action == FIT_WIDTH && st.fit_page) {
              st.fit_page = false;
              force_render_page(st);
            }

            if (sc->action == NEXT || (sc->action == PG_DOWN && st.fit_page)) {
              if (st.page_num < st.doc->getNumPages()) {
                ++st.page_num;
                render_page_lambda();
              }
            }

            if (sc->action == PREV || (sc->action == PG_UP && st.fit_page)) {
              if (st.page_num > 1) {
                --st.page_num;
                render_page_lambda();
              }
            }

            if (sc->action == FIRST) {
              st.page_num = 1;
              render_page_lambda();
            }

            if (sc->action == LAST) {
              st.page_num = st.doc->getNumPages();
              render_page_lambda();
            }

            if (sc->action == DOWN && !st.fit_page) {
              int diff = get_pdf_scroll_diff(st, -arrow_scroll);
              if (diff != 0) {
                st.pdf_pos.y += diff;
                force_render_page(st, false);
              }
            }

            if (sc->action == UP && !st.fit_page) {
              int diff = get_pdf_scroll_diff(st, arrow_scroll);
              if (diff != 0) {
                st.pdf_pos.y += diff;
                force_render_page(st, false);
              }
            }

            if (sc->action == PG_DOWN && !st.fit_page) {
              int diff = get_pdf_scroll_diff(st, -page_scroll);
              if (diff != 0) {
                st.pdf_pos.y += diff;
                force_render_page(st, false);
              } else {
                if (st.page_num < st.doc->getNumPages()) {
                  ++st.page_num;
                  render_page_lambda();
                }
              }
            }

            if (sc->action == PG_UP && !st.fit_page) {
              int diff = get_pdf_scroll_diff(st, page_scroll);
              if (diff != 0) {
                st.pdf_pos.y += diff;
                force_render_page(st, false);
              } else {
                if (st.page_num > 1) {
                  st.scrolling_up = true;
                  --st.page_num;
                  render_page_lambda();
                }
              }
            }

            if (sc->action == BACK) {
              if (!st.page_stack.empty()) {
                auto elem = st.page_stack.top();
                st.page_stack.pop();

                st.page_num = elem.page;
                st.next_pos_y = elem.offset;
                render_page_lambda();
              }
            }

            if (sc->action == RELOAD) {
              st.doc.reset(new PDFDoc(
                  unique_ptr<GooString>(new GooString(file_name.c_str()))));
              (!st.doc->isOk()) && error("Error re-loading pdf file.");

              if (st.page_num > st.doc->getNumPages())
                st.page_num = 1;

              render_page_lambda();
            }

            if (sc->action == COPY) {
              if (st.pdf_selection.width > 0 && st.pdf_selection.height > 0)
                copy_text(st, false);
            }

            if (sc->action == GOTO_PAGE) {
              st.status = true;
              st.input = true;
              st.prompt =
                  "goto page [1, " + to_string(st.doc->getNumPages()) + "]: ";
              st.value = "";
              send_expose(st, st.status_pos);
            }

            if (sc->action == SEARCH) {
              st.status = true;
              st.input = true;
              st.prompt = "search: ";
              st.value = "";
              send_expose(st, st.status_pos);
            }

            if (sc->action == PAGE) {
              st.status = true;
              st.input = false;
              st.prompt = "page " + to_string(st.page_num) + "/" +
                          to_string(st.doc->getNumPages());
              st.value = "";
              send_expose(st, st.status_pos);
            }

            if (sc->action == MAGNIFY) {
              if (st.pdf_selection.width > 0 && st.pdf_selection.height > 0) {
                st.magnifying = true;
                st.magnify = st.pdf_selection;
                st.selection = st.pdf_selection = {0, 0, 0, 0};

                st.status = true;
                st.input = false;
                st.prompt = "magnify";
                st.value = "";

                st.pre_mag_y = st.pdf_pos.y;
                st.pdf_pos.y = 0;

                force_render_page(st);
              }
            }

            if (sc->action == ROTATE_CW) {
              st.rotation += 90;
              if (st.rotation > 270)
                st.rotation = 0;
              force_render_page(st, true);
            }

            if (sc->action == ROTATE_CCW) {
              st.rotation -= 90;
              if (st.rotation < 0)
                st.rotation = 270;
              force_render_page(st, true);
            }
          }
        }

        if (status) {
          if (ksym == XK_Escape) {
            st.status = st.searching = false;
            XClearArea(st.display, st.main, st.status_pos.x, st.status_pos.y,
                       st.status_pos.width, st.status_pos.height, True);

            if (st.magnifying) {
              st.magnifying = false;
              st.next_pos_y = st.pre_mag_y;
              force_render_page(st);
            }
          }

          if (ksym == XK_BackSpace) {
            if (!st.value.empty()) {
              size_t off = 0;
              int num = 0;
              while (off < st.value.size()) {
                num = mblen(st.value.c_str() + off, st.value.size() - off);
                off += num;
              }

              while (num-- > 0)
                st.value.pop_back();

              XClearArea(st.display, st.main, st.status_pos.x, st.status_pos.y,
                         st.status_pos.width, st.status_pos.height, True);
            }
          }

          if (ksym == XK_Return) {
            if (st.prompt.substr(0, 4) == "goto") {
              int page;
              auto [p, ec] = from_chars(
                  st.value.data(), st.value.data() + st.value.size(), page);
              if (ec == errc() && page >= 1 && page <= st.doc->getNumPages()) {
                st.status = false;
                st.page_num = page;

                XClearArea(st.display, st.main, st.status_pos.x,
                           st.status_pos.y, st.status_pos.width,
                           st.status_pos.height, True);
                render_page_lambda();
              }
            }

            if (st.prompt.substr(0, 6) == "search") {
              send_expose(st, st.selection.normalized());
              search_text(st);
              send_expose(st, st.selection.normalized());
            }
          }

          if (st.input) {
            string s{buf};
            if (s != "" && !iscntrl((unsigned char)s[0])) {
              st.value += s;
              send_expose(st, st.status_pos);
            }
          }
        }
      }

      if (event.type == ButtonPress) {
        auto button = event.xbutton.button;
        if (button == Button4 && st.fit_page && !st.magnifying) {
          if (st.page_num > 1) {
            st.scrolling_up = true;
            --st.page_num;
            render_page_lambda();
          }
        }

        if (button == Button5 && st.fit_page && !st.magnifying) {
          if (st.page_num < st.doc->getNumPages()) {
            ++st.page_num;
            render_page_lambda();
          }
        }

        if (button == Button4 && !st.fit_page) {
          int diff = get_pdf_scroll_diff(st, mouse_scroll);
          if (diff != 0) {
            st.pdf_pos.y += diff;
            force_render_page(st, false);
          } else {
            if (st.page_num > 1 && !st.magnifying) {
              st.scrolling_up = true;
              --st.page_num;
              render_page_lambda();
            }
          }
        }

        if (button == Button5 && !st.fit_page) {
          int diff = get_pdf_scroll_diff(st, -mouse_scroll);
          if (diff != 0) {
            st.pdf_pos.y += diff;
            force_render_page(st, false);
          } else {
            if (st.page_num < st.doc->getNumPages() && !st.magnifying) {
              ++st.page_num;
              render_page_lambda();
            }
          }
        }

        if (button == Button1 && !st.magnifying) {
          if (event.xbutton.x >= st.pdf_pos.x &&
              event.xbutton.y >= st.pdf_pos.y &&
              event.xbutton.x <= st.pdf_pos.x + st.pdf_pos.width &&
              event.xbutton.y <= st.pdf_pos.y + st.pdf_pos.height) {
            if (find_page_link(st, event.xbutton)) {
              render_page_lambda();
            } else {
              const CoordConv cc(st.page, st.pdf_pos, false, st.rotation);
              st.selection = cc.to_screen(st.pdf_selection);

              // Padding needed because of float rounding errors in cc.
              send_expose(st, st.selection.normalized().padded(5));

              st.selection = {event.xbutton.x, event.xbutton.y, 0, 0};
              st.selecting = true;
            }
          }
        }
      }

      if (event.type == ButtonRelease && event.xbutton.button == Button1) {
        if (st.selecting) {
          st.selection.width = event.xbutton.x - st.selection.x;
          st.selection.height = event.xbutton.y - st.selection.y;

          const CoordConv cc(st.page, st.pdf_pos, false, st.rotation);
          st.pdf_selection = cc.to_pdf(st.selection.normalized());
          st.selecting = false;

          copy_text(st, true);
        }
      }

      if (event.type == MotionNotify && st.selecting) {
        auto pr = st.selection.normalized();

        st.selection.width = event.xbutton.x - st.selection.x;
        st.selection.height = event.xbutton.y - st.selection.y;

        auto nr = st.selection.normalized();

        for (auto &r : subtract(pr, nr))
          send_expose(st, r);
        for (auto &r : subtract(nr, pr))
          send_expose(st, r);
      }

      if (event.type == SelectionRequest) {
        Atom targets_atom = XInternAtom(st.display, "TARGETS", False);
        Atom utf8_string_atom = XInternAtom(st.display, "UTF8_STRING", False);
        Atom clipboard_atom = XInternAtom(st.display, "CLIPBOARD", False);

        auto xselreq = event.xselectionrequest;

        XEvent e;
        e.type = SelectionNotify;
        e.xselection.requestor = xselreq.requestor;
        e.xselection.selection = xselreq.selection;
        e.xselection.target = xselreq.target;
        e.xselection.time = xselreq.time;
        e.xselection.property = None;

        if (xselreq.target == targets_atom) {
          XChangeProperty(xselreq.display, xselreq.requestor, xselreq.property,
                          XA_ATOM, 32, PropModeReplace,
                          (unsigned char *)&utf8_string_atom, 1);
          e.xselection.property = xselreq.property;
        }

        /*
         * XA_STRING is not exactly correct for sending utf8 chars but
         * let's try it anyway, maybe it will work somehow.
         */
        if (xselreq.target == utf8_string_atom || xselreq.target == XA_STRING) {
          GooString *ptr = nullptr;
          if (xselreq.selection == XA_PRIMARY)
            ptr = st.primary.get();
          if (xselreq.selection == clipboard_atom)
            ptr = st.clipboard.get();

          if (ptr) {
            XChangeProperty(
                xselreq.display, xselreq.requestor, xselreq.property,
                xselreq.target, 8, PropModeReplace,
                (const unsigned char *)ptr->c_str(), ptr->getLength());
            e.xselection.property = xselreq.property;
          }
        }

        XSendEvent(xselreq.display, xselreq.requestor, True, EmptyMask, &e);
      }
    }
  endloop:;
  } catch (exception &e) {
    cerr << e.what() << endl;
    cleanup_x(st);
    return EXIT_FAILURE;
  }

  cleanup_x(st);
  return 0;
}
