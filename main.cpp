#include <algorithm>
#include <cctype>
#include <charconv>
#include <clocale>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stack>
#include <stdexcept>
#include <string>

#include <poppler-document.h>
#include <poppler-page-renderer.h>
#include <poppler-page.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include "coordconv.hpp"
#include "rectangle.hpp"

#include "config.hpp"

static bool error(const std::string &m) {
  throw std::runtime_error(m);
  return false;
}

struct PageAndOffset {
  int page, offset;
};

struct AppState {
  std::unique_ptr<poppler::document> doc;
  poppler::page *page = NULL;
  std::unique_ptr<poppler::page_renderer> renderer;
  int page_num;
  bool fit_page;
  bool scrolling_up;
  int next_pos_y = 0;
  std::stack<PageAndOffset> page_stack;

  Display *display = NULL;
  Window main;
  srect main_pos;
  Pixmap pdf = None;
  srect pdf_pos{0, 0, 0, 0};

  GC selection_gc;
  srect selection{0, 0, 0, 0};
  srectf pdf_selection{0, 0, 0, 0};
  bool selecting = false;

  GC status_gc;
  GC text_gc;
  XFontSet fset = NULL;
  int fheight;
  int fbase;
  srect status_pos;
  std::string prompt;
  std::string value;
  bool status = false;
  bool input = false;

  srectf pos;
  bool searching = false;

  bool xembed_init = false;

  bool magnifying = false;
  srectf magnify;
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
                         const std::string &file_name, Window root) {
  Display *display = XOpenDisplay(NULL);
  (!display) && error("Cannot open X display.");

  XColor sc, ec;
  XAllocNamedColor(display, DefaultColormap(display, DefaultScreen(display)),
                   bg_color, &sc, &ec);

  if (root == None)
    root = DefaultRootWindow(display);
  Window main =
      XCreateSimpleWindow(display, root, 0, 0, width, height, 2, 0, ec.pixel);

  std::string window_name("spdf: " + file_name);
  std::string icon_name("spdf");

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
    fbase = std::max(fbase, fonts[i]->descent);
    fheight = std::max(fheight, fonts[i]->ascent + fonts[i]->descent);
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
  srect pos;
  srect crop;
};

PdfRenderConf get_pdf_render_conf(bool fit_page, bool scrolling_up, int offset,
                                  srect p, const poppler::page *page,
                                  bool magnifying, srectf m, int rotation) {
  auto rect = page->page_rect();
  auto x0 = rect.x();
  auto y0 = rect.y();
  auto width = rect.width();
  auto height = rect.height();

  if (magnifying) {
    x0 = m.x();
    y0 = m.y();
    width = m.width();
    height = m.height();
  }

  int x, y, w, h;
  double dpi;
  if (fit_page) {
    if (double(p.width()) / double(p.height()) > width / height) {
      h = p.height();
      dpi = double(p.height()) * 72.0 / height;
      w = width * dpi / 72.0;

      y = 0;
      x = (p.width() - w) / 2;
    } else {
      w = p.width();
      dpi = double(p.width()) * 72.0 / width;
      h = height * dpi / 72.0;

      x = 0;
      y = (p.height() - h) / 2;
    }
  } else {
    w = p.width();
    dpi = double(p.width()) * 72.0 / width;
    h = height * dpi / 72.0;

    x = 0;
    if (double(p.width()) / double(p.height()) <= width / height) {
      y = (p.height() - h) / 2;
    } else {
      if (!scrolling_up)
        y = offset;
      else
        y = p.height() - h;
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

  auto img = st.renderer->render_page(st.page, prc.dpi, prc.dpi, prc.crop.x(),
                                      prc.crop.y(), prc.crop.width(),
                                      prc.crop.height());
  auto xim = XCreateImage(
      st.display, DefaultVisual(st.display, DefaultScreen(st.display)), 24,
      ZPixmap, 0, img.data(), img.width(), img.height(), 32, 0);

  Pixmap pxm = XCreatePixmap(
      st.display, DefaultRootWindow(st.display), img.width(), img.height(),
      DefaultDepth(st.display, DefaultScreen(st.display)));

  XPutImage(st.display, pxm, DefaultGC(st.display, DefaultScreen(st.display)),
            xim, 0, 0, 0, 0, img.width(), img.height());

  xim->data = NULL;
  XDestroyImage(xim);

  return pxm;
}

static void copy_pixmap_on_expose_event(const AppState &st, const srect &prev,
                                        const XExposeEvent &e) {
  if (st.pdf_pos != prev) {
    std::vector<srect> diff = subtract(prev, st.pdf_pos);
    for (size_t i = 0; i < diff.size(); ++i) {
      XClearArea(st.display, st.main, diff[i].x(), diff[i].y(), diff[i].width(),
                 diff[i].height(), False);
    }
  }

  srect dirty = intersect(srect{e.x, e.y, e.width, e.height}, st.pdf_pos);
  if (!is_invalid(dirty)) {
    XCopyArea(st.display, st.pdf, st.main,
              DefaultGC(st.display, DefaultScreen(st.display)),
              dirty.x() - st.pdf_pos.x(), dirty.y() - st.pdf_pos.y(),
              dirty.width(), dirty.height(), dirty.x(), dirty.y());

    const CoordConv cc(st.page, st.pdf_pos, false, st.rotation);
    srect rs = st.selecting ? st.selection.normalized()
                            : cc.to_screen(st.pdf_selection);
    if (rs.width() > 0 && rs.height() > 0) {
      dirty = intersect(srect{e.x, e.y, e.width, e.height}, rs);
      if (!is_invalid(dirty))
        XFillRectangle(st.display, st.main, st.selection_gc, dirty.x(),
                       dirty.y(), dirty.width(), dirty.height());
    }
  }

  if (st.status) {
    if (is_invalid(
            intersect(srect{e.x, e.y, e.width, e.height}, st.status_pos)))
      return;

    XFillRectangle(st.display, st.main, st.status_gc, st.status_pos.x(),
                   st.status_pos.y(), st.status_pos.width(),
                   st.status_pos.height());

    std::string str{st.prompt + st.value + "_"};
    if (!st.input)
      str = st.prompt;
    Xutf8DrawString(st.display, st.main, st.fset, st.text_gc,
                    st.status_pos.x() + 1,
                    st.status_pos.y() + st.status_pos.height() - (st.fbase + 1),
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

static void send_expose(const AppState &st, const srect &r) {
  XEvent e;
  e.type = Expose;
  e.xexpose.x = r.x();
  e.xexpose.y = r.y();
  e.xexpose.width = r.width();
  e.xexpose.height = r.height();
  XSendEvent(st.display, st.main, False, ExposureMask, &e);
}

static int get_pdf_scroll_diff(const AppState &st, double percent) {
  if (st.pdf_pos.height() < st.main_pos.height())
    return 0;

  int sc = st.pdf_pos.height() * percent;
  if (sc > 0)
    return std::min(sc, -st.pdf_pos.y());

  // As we scroll down the top decreases from zero, so the srect is actually
  // getting BIGGER.
  // TODO Fix this!
  int h = st.pdf_pos.bottom() + st.pdf_pos.top();
  if (h <= st.main_pos.height()) {
    return 0;
  }
  return -std::min(-sc, h - st.main_pos.height());
}

static srect get_status_pos(const AppState &st) {
  return {0, st.main_pos.height() - (st.fheight + 2), st.main_pos.width(),
          st.fheight + 2};
}

static void search_text(AppState &st) {
  poppler::page::search_direction_enum dir = poppler::page::search_next_result;
  poppler::case_sensitivity_enum case_search = poppler::case_sensitive;
  std::string str = st.value;
  while (str.back() == '?' || str.back() == '~') {
    char flag = str.back();
    if (flag == '?')
      dir = poppler::page::search_previous_result;
    if (flag == '~')
      case_search = poppler::case_insensitive;
    str.pop_back();
  }

  bool found = false;
  bool whole = false;
  int page = st.page_num;

  while (!whole) {
    st.renderer->render_page(st.page, 72, 72, 0, false, true, false);
    found = st.page->search(poppler::ustring::from_latin1(str), st.pos, dir,
                            case_search);
    /* found = tdev.takeText()->findText(search.data(), search.size(), */
    /*                                   !st.searching, true, st.searching,
     * false, */
    /*                                   !ignore_case, backwards, whole_words,
     */
    /*                                   &st.left, &st.top, &st.right,
     * &st.bottom); */

    if (found)
      break;

    if (dir != poppler::page::search_next_result && page < st.doc->pages()) {
      ++page;
      st.searching = false;
    } else if (dir == poppler::page::search_next_result && page > 1) {
      --page;
      st.searching = false;
    } else {
      whole = true;
    }
  }

  if (found) {
    if (page != st.page_num) {
      st.page_num = page;

      st.page = st.doc->create_page(st.page_num);
      (!st.page) &&
          error("Cannot create page: " + std::to_string(st.page_num) + ".");
      force_render_page(st);
    }

    const CoordConv cc(st.page, st.pdf_pos, false, st.rotation);

    st.pdf_selection = st.pos;
    st.selection = cc.to_screen(st.pdf_selection);
  } else {
    st.pdf_selection = {0, 0, 0, 0};
    st.selection = {0, 0, 0, 0};
  }

  st.searching = found;
}

struct Args {
  std::string fname;
  Window root;
};

Args parse_args(int argc, char **argv) {
  std::string fname = "";
  Window root = None;

  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "-w") {
      if (i < argc - 1) {
        root = strtol(argv[++i], NULL, 0);
        (root == 0) && error("Invalid window (-w) value.");
      } else
        error("Missing window (-w) parameter.");
    } else
      fname = std::string(argv[i]);
  }

  if (fname == "")
    error(std::string("Missing pdf file, usage: ") + argv[0] +
          " [-w window] pdf_file.");

  return {fname, root};
}

int main(int argc, char **argv) {
  setlocale(LC_ALL, "");

  AppState st;
  try {
    auto args = parse_args(argc, argv);

    std::string file_name(args.fname);

    st.doc = std::unique_ptr<poppler::document>(
        poppler::document::load_from_file(file_name));
    st.renderer =
        std::unique_ptr<poppler::page_renderer>(new poppler::page_renderer());
    st.renderer->set_render_hints(poppler::page_renderer::antialiasing |
                                  poppler::page_renderer::text_antialiasing);

    st.page_num = 1;
    st.page = st.doc->create_page(st.page_num);
    (!st.page) && error("Document has no pages.");

    auto rect = st.page->page_rect();
    auto xret = setup_x(rect.width(), rect.height(), file_name, args.root);

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

      auto render_page_lambda = [&]() {
        st.page = st.doc->create_page(st.page_num);
        (!st.page) &&
            error("Cannot create page: " + std::to_string(st.page_num) + ".");
        force_render_page(st);
        st.selection = {0, 0, 0, 0};
        st.pdf_selection = {0, 0, 0, 0};
        st.selecting = false;
      };

      switch(event.type) {
        case Expose: {
          auto prev = st.pdf_pos;
          if (st.pdf == None) {
            auto prc = get_pdf_render_conf(
                st.fit_page, st.scrolling_up, st.next_pos_y, st.main_pos, st.page,
                st.magnifying, st.magnify, st.rotation);
            st.scrolling_up = false;
            st.next_pos_y = 0;

            st.pdf = render_pdf_page_to_pixmap(st, prc);
            st.pdf_pos = prc.pos; // Add anohter contructor
          }
          copy_pixmap_on_expose_event(st, prev, event.xexpose);
          break;
        }

        case ConfigureNotify:
          if (st.main_pos.width() != event.xconfigure.width ||
              st.main_pos.height() != event.xconfigure.height) {
            st.main_pos = {event.xconfigure.x, event.xconfigure.y,
                           event.xconfigure.width, event.xconfigure.height};

            XClearWindow(st.display, st.main);
            if (st.pdf != None) {
              XFreePixmap(st.display, st.pdf);
              st.pdf = None;
            }

            st.status_pos = get_status_pos(st);
          }
        break;

        case ClientMessage: {
          Atom xembed_atom = XInternAtom(st.display, "_XEMBED", False);
          Atom wmdel_atom = XInternAtom(st.display, "WM_DELETE_WINDOW", False);

          if (event.xclient.message_type == xembed_atom &&
              event.xclient.format == 32) {
            if (!st.xembed_init) {
              force_render_page(st);
              st.xembed_init = true;
            }
          } else if (event.xclient.data.l[0] == (long)wmdel_atom)
            return 0;
          break;
        }

        case KeyPress: {
          KeySym ksym;
          char buf[64];
          XLookupString(&event.xkey, buf, sizeof(buf), &ksym, NULL);

          bool status = st.status;
          for (unsigned i = 0; i < sizeof(shortcuts) / sizeof(Shortcut); ++i) {
            auto sc = &shortcuts[i];
            if (!status && sc->ksym == ksym &&
                (sc->mask == AnyMask || sc->mask == event.xkey.state)) {
              switch (sc->action) {
                case QUIT:
                  return 0;
                break;

                case FIT_PAGE:
                  if (!st.fit_page) {
                    st.fit_page = true;
                    force_render_page(st);
                  }
                break;

                case FIT_WIDTH:
                  if (st.fit_page) {
                    st.fit_page = false;
                    force_render_page(st);
                  }
                break;

                case DOWN:
                  if (st.fit_page) {
                case NEXT:
                  if (st.page_num < st.doc->pages()) {
                    ++st.page_num;
                    render_page_lambda();
                  }
                break;
                  } else {
                    int diff = get_pdf_scroll_diff(st, -arrow_scroll);
                    if (diff != 0) {
                      st.pdf_pos.set_top(st.pdf_pos.y() + diff);
                      force_render_page(st, false);
                    } else {
                      if (st.page_num < st.doc->pages()) {
                        ++st.page_num;
                        render_page_lambda();
                      }
                    }
                  }
                break;

                case UP:
                  if (st.fit_page) {
                case PREV:
                  if (st.page_num > 1) {
                    --st.page_num;
                    render_page_lambda();
                    break;
                  }
                break;
                  } else {
                    int diff = get_pdf_scroll_diff(st, arrow_scroll);
                    if (diff != 0) {
                      st.pdf_pos.set_top(st.pdf_pos.y() + diff);
                      force_render_page(st, false);
                    } else {
                      if (st.page_num > 1) {
                        st.scrolling_up = true;
                        --st.page_num;
                        render_page_lambda();
                      }
                    }
                  }
                break;

                case FIRST:
                  st.page_num = 1;
                  render_page_lambda();
                break;

                case LAST:
                  st.page_num = st.doc->pages();
                  render_page_lambda();
                break;

                case BACK:
                  if (!st.page_stack.empty()) {
                    auto elem = st.page_stack.top();
                    st.page_stack.pop();

                    st.page_num = elem.page;
                    st.next_pos_y = elem.offset;
                    render_page_lambda();
                  }
                break;

                case RELOAD:
                  st.doc = std::unique_ptr<poppler::document>(
                      poppler::document::load_from_file(file_name));

                  if (st.page_num > st.doc->pages())
                    st.page_num = 1;

                  render_page_lambda();
                break;

                case GOTO_PAGE:
                  st.status = true;
                  st.input = true;
                  st.prompt =
                    "goto page [1, " + std::to_string(st.doc->pages()) + "]: ";
                  st.value = "";
                  send_expose(st, st.status_pos);
                break;

                case SEARCH:
                  st.status = true;
                  st.input = true;
                  st.prompt = "search: ";
                  st.value = "";
                  send_expose(st, st.status_pos);
                break;

                case PAGE:
                  st.status = true;
                  st.input = false;
                  st.prompt = "page " + std::to_string(st.page_num) + "/" +
                    std::to_string(st.doc->pages());
                  st.value = "";
                  send_expose(st, st.status_pos);
                break;

                case MAGNIFY:
                  if (st.pdf_selection.width() > 0 &&
                      st.pdf_selection.height() > 0) {
                    st.magnifying = true;
                    st.magnify = st.pdf_selection;
                    st.selection = {0, 0, 0, 0};
                    st.pdf_selection = {0, 0, 0, 0};

                    st.status = true;
                    st.input = false;
                    st.prompt = "magnify";
                    st.value = "";

                    st.pre_mag_y = st.pdf_pos.y();
                    st.pdf_pos.set_top(0);

                    force_render_page(st);
                  }
                break;

                case ROTATE_CW:
                  st.rotation += 90;
                  if (st.rotation > 270)
                    st.rotation = 0;
                  force_render_page(st, true);
                break;

                case ROTATE_CCW:
                  st.rotation -= 90;
                  if (st.rotation < 0)
                    st.rotation = 270;
                  force_render_page(st, true);
                break;
              }
            }
          }

          if (status) {
            switch (ksym) {
              case XK_Escape:
                st.status = st.searching = false;
                XClearArea(st.display, st.main, st.status_pos.x(),
                    st.status_pos.y(), st.status_pos.width(),
                    st.status_pos.height(), True);

                if (st.magnifying) {
                  st.magnifying = false;
                  st.next_pos_y = st.pre_mag_y;
                  force_render_page(st);
                }
              break;

              case XK_BackSpace:
                if (!st.value.empty()) {
                  size_t off = 0;
                  int num = 0;
                  while (off < st.value.size()) {
                    num = mblen(st.value.c_str() + off, st.value.size() - off);
                    off += num;
                  }

                  while (num-- > 0)
                    st.value.pop_back();

                  XClearArea(st.display, st.main, st.status_pos.x(),
                      st.status_pos.y(), st.status_pos.width(),
                      st.status_pos.height(), True);
                }
              break;

              case XK_Return:
                if (st.prompt.substr(0, 4) == "goto") {
                  int page;
                  auto [p, ec] = std::from_chars(
                      st.value.data(), st.value.data() + st.value.size(), page);
                  if (ec == std::errc() && page >= 1 && page <= st.doc->pages()) {
                    st.status = false;
                    st.page_num = page;

                    XClearArea(st.display, st.main, st.status_pos.x(),
                        st.status_pos.y(), st.status_pos.width(),
                        st.status_pos.height(), True);
                    render_page_lambda();
                  }
                }

                if (st.prompt.substr(0, 6) == "search") {
                  send_expose(st, st.selection.normalized());
                  search_text(st);
                  send_expose(st, st.selection.normalized());
                }
              break;
            }

            if (st.input) {
              std::string s{buf};
              if (s != "" && !iscntrl((unsigned char)s[0])) {
                st.value += s;
                send_expose(st, st.status_pos);
              }
            }
          }
          break;
        }

        case ButtonPress:
          switch (event.xbutton.button) {
            case Button4:
              if (st.fit_page) {
                if (!st.magnifying && st.page_num > 1) {
                  st.scrolling_up = true;
                  --st.page_num;
                  render_page_lambda();
                }
              } else {
                int diff = get_pdf_scroll_diff(st, mouse_scroll);
                if (diff != 0) {
                  st.pdf_pos.set_top(st.pdf_pos.y() + diff);
                  force_render_page(st, false);
                } else {
                  if (st.page_num > 1 && !st.magnifying) {
                    st.scrolling_up = true;
                    --st.page_num;
                    render_page_lambda();
                  }
                }
              }
            break;

            case Button5:
              if (st.fit_page) {
                if (!st.magnifying && st.page_num < st.doc->pages()) {
                  ++st.page_num;
                  render_page_lambda();
                }
              } else {
                int diff = get_pdf_scroll_diff(st, -mouse_scroll);
                if (diff != 0) {
                  st.pdf_pos.set_top(st.pdf_pos.y() + diff);
                  force_render_page(st, false);
                } else {
                  if (st.page_num < st.doc->pages() && !st.magnifying) {
                    ++st.page_num;
                    render_page_lambda();
                  }
                }
              }
            break;

            case Button1:
              if (!st.magnifying) {
                if (event.xbutton.x >= st.pdf_pos.x() &&
                    event.xbutton.y >= st.pdf_pos.y() &&
                    event.xbutton.x <= st.pdf_pos.x() + st.pdf_pos.width() &&
                    event.xbutton.y <= st.pdf_pos.y() + st.pdf_pos.height()) {
                  const CoordConv cc(st.page, st.pdf_pos, false, st.rotation);
                  st.selection = cc.to_screen(st.pdf_selection);

                  // Padding needed because of float rounding errors in cc.
                  send_expose(st, st.selection.normalized().padded(5));

                  st.selection = {event.xbutton.x, event.xbutton.y, 0, 0};
                  st.selecting = true;
                }
              }
            break;
          }
        break;

        case MotionNotify:
          if (st.selecting) {
            auto pr = st.selection.normalized();

            st.selection.set_left(st.selection.x());
            st.selection.set_right(event.xbutton.x);
            st.selection.set_top(st.selection.y());
            st.selection.set_bottom(event.xbutton.y);

            auto nr = st.selection.normalized();

            for (auto &r : subtract(pr, nr))
              send_expose(st, r);
            for (auto &r : subtract(nr, pr))
              send_expose(st, r);
          }
        break;
      }
    }
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
    cleanup_x(st);
    return EXIT_FAILURE;
  }

  cleanup_x(st);
  return 0;
}
