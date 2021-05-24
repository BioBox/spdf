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
#include <poppler/goo/GooString.h>

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
	if (st.display != NULL)
		XCloseDisplay(st.display);
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
