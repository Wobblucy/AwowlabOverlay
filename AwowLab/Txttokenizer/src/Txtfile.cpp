#include "../include/Txttokenizer/Txtfile.h"
#include <stdexcept>

bool txtfile::openfile(const std::string& filename) {
	filestream.open(filename, std::ios::in);
	return filestream.is_open();
}

void txtfile::closefile() {
	if (filestream.is_open()) {
		filestream.close();
	}
}
;

bool txtfile:: isopen() {
	return filestream.is_open();
};

/*
TO IMPLEMENT OFFSETS SO WE CAN CHUNK THE FILE FOR THREADING LATER!!!
*/
void txtfile::offsetbeg(size_t offset) {
	filestream.seekg(offset, std::ios::beg);
	}

char txtfile::readnextchar() {
	int c = filestream.get();
	if (filestream.eof()) {
		return static_cast<char>(EOF);
	}
	return static_cast<char>(c);
};

char txtfile::peeknextchar(){
	int c = filestream.peek();
	if (filestream.eof()) {
		return static_cast<char>(EOF);
	}
	return static_cast<char>(c);
};
