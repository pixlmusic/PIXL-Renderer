#include "Director/DirectorCameraPath.h"

#include <iostream>
#include <string>

int main()
{
	std::string failure;
	if (!DirectorCameraPath::RunDeterministicSelfTest(&failure)) {
		std::cerr << "Director camera-path regression failed: " << failure << '\n';
		return 1;
	}
	return 0;
}
