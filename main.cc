#include <climits>
#include <clocale>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <poppler/GlobalParams.h>
#include <poppler/PDFDoc.h>
#include <poppler/Page.h>
#include <poppler/SplashOutputDev.h>
#include <poppler/goo/GooString.h>
#include <poppler/splash/SplashBitmap.h>

#include "rectangle.h"

using namespace std;

#define AnyMask   UINT_MAX
#define EmptyMask 0

enum Action {
	QUIT
};

struct Shortcut {
	unsigned mask;
	KeySym ksym;
	Action action;
};

#include "config.h"

static bool error(const string &m) {throw runtime_error(m); return false;}

struct AppState {
	unique_ptr<PDFDoc> doc;
	Page *page = NULL;
	int page_num;

	Display *display = NULL;
	Window main;
	Rectangle main_pos;
	Pixmap pdf = None;
	Rectangle pdf_pos;
};

struct SetupXRet {
	Display *display;
	Window main;
};

static SetupXRet setup_x(unsigned width, unsigned height, const string &file_name)
{
	Display *display = XOpenDisplay(NULL);
	(!display) && error("Cannot open X display.");

	XColor sc, ec;
	XAllocNamedColor(display, DefaultColormap(display, DefaultScreen(display)),
		bg_color, &sc, &ec);

	Window main = XCreateSimpleWindow(display, DefaultRootWindow(display),
		0, 0, width, height, 2, 0, ec.pixel);

	string window_name("lpdf: " + file_name);
	string icon_name("lpdf");

	Xutf8SetWMProperties(display, main, window_name.c_str(), icon_name.c_str(),
		NULL, 0, NULL, NULL, NULL);

	/*
	 * If WM does not understand WM_NAME and WM_ICON_NAME of type COMPOUND_TEXT
	 * set by Xutf8SetWMProperties() and needs _NET_WM_* properties of type UTF8_STRING.
	 */
	Atom utf8_string_atom  = XInternAtom(display, "UTF8_STRING", False);
	Atom wm_name_atom      = XInternAtom(display, "_NET_WM_NAME", False);
	Atom wm_icon_name_atom = XInternAtom(display, "_NET_WM_ICON_NAME", False);

	XChangeProperty(display, main, wm_name_atom, utf8_string_atom, 8,
		PropModeReplace, (const unsigned char*)window_name.c_str(), window_name.size());
	XChangeProperty(display, main, wm_icon_name_atom, utf8_string_atom, 8,
		PropModeReplace, (const unsigned char*)icon_name.c_str(), icon_name.size());

	XMapWindow(display, main);
	XSelectInput(display, main, KeyPressMask | StructureNotifyMask | ExposureMask);

	return {display, main};
}

static void cleanup_x(const AppState &st)
{
	if (st.pdf != None)
		XFreePixmap(st.display, st.pdf);
	if (st.display != NULL)
		XCloseDisplay(st.display);
}

struct PdfRenderConf {
	double dpi;
	Rectangle pos;
};

PdfRenderConf get_pdf_render_conf(Rectangle p, const Page *page)
{
	auto rect   = page->getCropBox();
	auto width  = rect->x2 - rect->x1;
	auto height = rect->y2 - rect->y1;

	int x, y, w, h;
	double dpi;
	if (double(p.width) / double(p.height) > width / height)
	{
		h   = p.height;
		dpi = double(p.height) * 72.0 / height;
		w   = width * dpi / 72.0;

		y = 0;
		x = (p.width - w) / 2;
	}
	else {
		w   = p.width;
		dpi = double(p.width) * 72.0 / width;
		h   = height * dpi / 72.0;

		x = 0;
		y = (p.height - h) / 2;
	}
	return {dpi, {x, y, w, h}};
}

static Pixmap render_pdf_page_to_pixmap(const AppState &st, const PdfRenderConf &prc)
{
	SplashColor paper{0xff, 0xff, 0xff};

	SplashOutputDev sdev(splashModeXBGR8, 4, false, paper);
	sdev.setFontAntialias(true);
	sdev.setVectorAntialias(true);

	sdev.startDoc(st.doc.get());
	st.doc->displayPage(&sdev, st.page_num, prc.dpi, prc.dpi, st.page->getRotate(),
		false, true, false);

	SplashBitmap *img = sdev.getBitmap();

	auto xim = XCreateImage(st.display,
		DefaultVisual(st.display, DefaultScreen(st.display)), 24, ZPixmap, 0,
		(char*)img->getDataPtr(), img->getWidth(), img->getHeight(), 32, 0);

	Pixmap pxm = XCreatePixmap(st.display,
		DefaultRootWindow(st.display), img->getWidth(), img->getHeight(),
		DefaultDepth(st.display, DefaultScreen(st.display)));

	XPutImage(st.display, pxm,
		DefaultGC(st.display, DefaultScreen(st.display)),
		xim, 0, 0, 0, 0, img->getWidth(), img->getHeight());

	xim->data = NULL;
	XDestroyImage(xim);

	return pxm;
}

static void copy_pixmap_on_expose_event(const AppState &st, const XExposeEvent &e)
{
	Rectangle dirty = intersect({e.x, e.y, e.width, e.height}, st.pdf_pos);
	if (is_invalid(dirty))
		return;

	XCopyArea(st.display, st.pdf, st.main,
		DefaultGC(st.display, DefaultScreen(st.display)),
		dirty.x - st.pdf_pos.x, dirty.y - st.pdf_pos.y,
		dirty.width, dirty.height, dirty.x, dirty.y);
}

int main(int argc, char **argv)
{
	setlocale(LC_ALL, "");

	AppState st;
	try {
		(argc != 2) && error(string("Missing pdf file, usage: ") + argv[0] + " pdf_file.");

		string file_name(argv[1]);

		GlobalParamsIniter global_params(NULL);

		st.doc.reset(new PDFDoc(new GooString(file_name.c_str())));
		(!st.doc->isOk()) && error("Error loading specified pdf file.");

		st.page_num = 1;
		st.page = st.doc->getPage(st.page_num);
		(!st.page) && error("Document has no pages.");

		auto rect = st.page->getCropBox();
		auto xret = setup_x(rect->x2 - rect->x1, rect->y2 - rect->y1, file_name);

		st.display = xret.display;
		st.main    = xret.main;

		XEvent event;
		while (true)
		{
			XNextEvent(st.display, &event);

			if (event.type == Expose)
			{
				if (st.pdf == None)
				{
					auto prc = get_pdf_render_conf(st.main_pos, st.page);

					st.pdf = render_pdf_page_to_pixmap(st, prc);
					st.pdf_pos = prc.pos;
				}
				copy_pixmap_on_expose_event(st, event.xexpose);
			}

			if (event.type == ConfigureNotify)
			{
				if (st.main_pos.width != event.xconfigure.width ||
					st.main_pos.height != event.xconfigure.height)
				{
					st.main_pos = {
						event.xconfigure.x,
						event.xconfigure.y,
						event.xconfigure.width,
						event.xconfigure.height
					};

					XClearWindow(st.display, st.main);
					if (st.pdf != None)
					{
						XFreePixmap(st.display, st.pdf);
						st.pdf = None;
					}
				}
			}

			if (event.type == KeyPress)
			{
				KeySym ksym;
				char buf[64];
				XLookupString(&event.xkey, buf, sizeof(buf), &ksym, NULL);

				for (unsigned i = 0; i < sizeof(shortcuts) / sizeof(Shortcut); ++i)
				{
					auto sc = &shortcuts[i];
					if (sc->ksym == ksym &&
						(
							sc->mask == AnyMask ||
							sc->mask == event.xkey.state
						)
					)
					{
						if (sc->action == QUIT)
							goto endloop;
					}
				}
			}
		}
		endloop:;
	}
	catch (exception &e) {
		cerr << e.what() << endl;
		cleanup_x(st);
		return EXIT_FAILURE;
	}

	cleanup_x(st);
	return 0;
}
