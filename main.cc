#include <clocale>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <poppler/GlobalParams.h>
#include <poppler/PDFDoc.h>
#include <poppler/Page.h>
#include <poppler/goo/GooString.h>

using namespace std;

static bool error(const string &m) {throw runtime_error(m); return false;}

struct AppState {
	unique_ptr<PDFDoc> doc;
	Page *page = NULL;
	int page_num;
};

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
	}
	catch (exception &e) {
		cerr << e.what() << endl;
		return EXIT_FAILURE;
	}

	return 0;
}
