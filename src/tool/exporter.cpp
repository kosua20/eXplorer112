#include "core/Log.hpp"
#include "core/System.hpp"
#include "core/TextUtilities.hpp"
#include "core/Image.hpp"
#include "core/WorldParser.hpp"


#include <fstream>
#include <map>



int main(int argc, const char** argv)
{
	if(argc < 2){
		return 1;
	}

	const fs::path inputPath(argv[1]);
	const bool dryRun = argc == 2;
	const fs::path outputPath = dryRun ? "" : fs::path(argv[2]);

	const fs::path modelsPath = inputPath / "models";
	const fs::path texturesPath = inputPath / "textures";
	const fs::path templatesPath = inputPath / "templates";

	const fs::path zonesPath = inputPath / "zones";
	const fs::path worldsPath = zonesPath / "world";

	std::vector<fs::path> modelsList;
	System::listAllFilesOfType(modelsPath, ".dff", modelsList);

	std::vector<fs::path> templatesList;
	System::listAllFilesOfType(templatesPath, ".template", templatesList);

	std::vector<fs::path> worldsList;
	System::listAllFilesOfType(worldsPath, ".world", worldsList);

	std::vector<fs::path> texturesList;
	System::listAllFilesOfType(modelsPath, ".dds", texturesList);
	System::listAllFilesOfType(modelsPath, ".tga", texturesList);
	System::listAllFilesOfType(modelsPath, ".png", texturesList);
	System::listAllFilesOfType(texturesPath, ".dds", texturesList);
	System::listAllFilesOfType(texturesPath, ".tga", texturesList);
	System::listAllFilesOfType(texturesPath, ".png", texturesList);

	if(dryRun){
		Log::info("Dry run:");
		for(const auto& worldPath : worldsList){
			Log::info("Processing world %s", worldPath.filename().string().c_str());

			World world;
			if(!world.load(worldPath, inputPath)){
				Log::error("Unable to load world at path %s", worldPath.string().c_str());
			}
			Log::info("Summary for world %s", world.name().c_str());
			Log::info("\t* %lu objects", world.objects().size());
			Log::info("\t* %lu instances", world.instances().size());
			Log::info("\t* %lu materials", world.materials().size());
			Log::info("\t* %lu cameras", world.cameras().size());
			Log::info("\t* %lu lights", world.lights().size());
			Log::info("\t* %lu zones", world.zones().size());

		}
		return 0;
	}

//#define SCENE_FILE "tutoeco.world"

	fs::create_directory(outputPath);
#ifndef SCENE_FILE
	for(const auto& worldPath : worldsList)
#endif
	{
#ifdef SCENE_FILE
		const fs::path worldPath = worldsPath / SCENE_FILE;
#endif
		Log::info("Processing world %s", worldPath.string().c_str());



		// Save obj file
		const std::string baseName = worldPath.filename().replace_extension().string();
		const fs::path outPath = outputPath / baseName;
		const fs::path outTexturePath = outPath / "textures";

		fs::create_directory(outPath);
		fs::create_directory(outTexturePath);

		World world;
		if(!world.load(worldPath, inputPath)){
			Log::error("Unable to load world at path %s", worldPath.string().c_str());
#ifdef SCENE_FILE
			return 1;
#else
			continue;
#endif
		}
		// Now browse the hierarchy again, duplicating OBJ data for each instance.
		// Also keep track of all materials and used textures.

		ObjOffsets offsets;
		std::ofstream outputMtl(outPath / (baseName + ".mtl"));
		std::ofstream outputObj(outPath / (baseName + ".obj"));
		outputObj << "mtllib " << baseName << ".mtl" << "\n";
		
		// Flatten each instance by duplicating the object and applying the instance frame.
		for(const World::Instance& instance : world.instances()){
			const Object& object = world.objects()[instance.object];
			const glm::mat4& frame = instance.frame;
			writeObjToStream(object, outputObj, offsets, frame);
		}
		// Write materials only once.
		writeMtlsToStream(world.materials(), outputMtl);

		outputObj.close();
		outputMtl.close();

		// Try to find each texture.
		std::unordered_set<std::string> textureNames;
		for(const Object::Material& material : world.materials()){
			if(!material.color.empty()){
				textureNames.insert(material.color);
			}
			if(!material.normal.empty()){
				textureNames.insert(material.normal);
			}
		}

		for(const std::string& textureName : textureNames){
			bool found = false;
			fs::path selectedTexturePath;

			for(const fs::path& texturePath : texturesList){
				const std::string existingName = texturePath.filename().replace_extension().string();
				if(existingName == textureName){
					if(found){
						Log::warning("Conflict for texture %s, paths: %s and %s", textureName.c_str(), selectedTexturePath.string().c_str(), texturePath.string().c_str());
					} else {
						found = true;
						selectedTexturePath = texturePath;
					}
				}
			}

			const fs::path destinationPath = outTexturePath / (textureName + ".png");
			if(!fs::exists(destinationPath)){

				Image image;
				if(found){
					// Copy the file.
					if(!image.load(selectedTexturePath)){
						Log::error("Unsupported texture format for input file %s", selectedTexturePath.filename().string().c_str());
					}
					image.uncompress();

				} else {
					// Generate a dummy texture.
					Image::generateDefaultImage(image);
				}
				// Save image to disk as PNG.
				if(!image.save(destinationPath)){
					Log::error("Unsupported texture format for output file %s", destinationPath.filename().string().c_str());
				}
			}

		}

	}

	// texturesPath+modelPath many formats(dds,...)
	// zonesPath .rf3

	return 0;

}
