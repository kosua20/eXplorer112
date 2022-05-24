#include "core/Log.hpp"
#include "core/System.hpp"
#include "core/DFFParser.hpp"

int main(int argc, const char** argv)
{
	if(argc < 2){
		return 1;
	}
	const fs::path rootPath(argv[1]);
	const fs::path modelsPath = rootPath / "models";
	const fs::path modelsPath = rootPath / "models";
	const fs::path modelsPath = rootPath / "models";

	for (const fs::directory_entry& file : fs::recursive_directory_iterator(modelsPath)) {
		const fs::path& path = file.path();
		if(path.extension() == ".dff"){

			Log::info("%s", path.c_str());

			Dff::Context context;
			Dff::parse(path, context);

		}
	}

	return 0;
}
