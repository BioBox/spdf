#include <clocale>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace std;

static bool error(const string &m) {throw runtime_error(m); return false;}

int main(int argc, char **argv)
{
	setlocale(LC_ALL, "");

	try {
		(argc != 2) && error(string("Missing pdf file, usage: ") + argv[0] + " pdf_file.");
	}
	catch (exception &e) {
		cerr << e.what() << endl;
		return EXIT_FAILURE;
	}

	return 0;
}
