#include <string>
#include <fstream>

class txtfile{ 

public:
	bool openfile(const std::string& filename);
	void closefile();
	bool isopen();
	void offsetbeg(size_t offset);
	
	char readnextchar();
	char peeknextchar();
	

private:
	std::ifstream filestream;
};
