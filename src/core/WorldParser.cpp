#include "core/WorldParser.hpp"
#include "core/Log.hpp"
#include "core/TextUtilities.hpp"
#include "core/AreaParser.hpp"
#include "core/DFFParser.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <unordered_map>


//#define LOG_WORLD_LOADING

World::Instance::Instance(const std::string& _name, uint _object, const glm::mat4& _frame) :
	frame(_frame), name(_name), object(_object){

}


World::World() {

};

World::World(const Object& object){
	_name = object.name;
	_objects.push_back(object);
	Object& localObject = _objects.back();
	_materials = localObject.materials;
	localObject.materials.clear();

	const size_t posCount = localObject.positions.size();
	if(localObject.uvs.empty()){
		localObject.uvs.resize(posCount, glm::vec2(0.5f));

		for(Object::Set& set : localObject.faceSets){
			for(Object::Set::Face& f : set.faces){
				f.t0 = f.v0;
				f.t1 = f.v1;
				f.t2 = f.v2;
			}
		}
	}
	if(localObject.normals.empty()){
		localObject.normals.resize(posCount, glm::vec3(0.0f, 0.0f, 1.0f));
		for(Object::Set& set : localObject.faceSets){
			for(Object::Set::Face& f : set.faces){
				f.n0 = f.v0;
				f.n1 = f.v1;
				f.n2 = f.v2;
			}
		}
	}

	_instances.emplace_back(object.name, 0, glm::mat4(1.0f));
}

bool isEntityVisible(const pugi::xml_node& entity){
	const char* objVisibility = entity.find_child_by_attribute("name", "visible").child_value();
	const bool visible = !objVisibility || strcmp(objVisibility, "true") == 0 || strcmp(objVisibility, "1") == 0;
	return visible;
}

glm::mat4 getEntityFrame(const pugi::xml_node& entity){
	const char* objPosStr = entity.find_child_by_attribute("name", "position").child_value();
	const char* objRotStr = entity.find_child_by_attribute("name", "rotation").child_value();
	const char* objScaStr = entity.find_child_by_attribute("name", "scale").child_value();
	const glm::vec3 position = Area::parseVec3(objPosStr);
	const glm::vec3 rotAngles = Area::parseVec3(objRotStr) / 180.0f * glm::pi<float>();
	const glm::vec3 scale = Area::parseVec3(objScaStr, glm::vec3(1.0f));

	glm::mat4 frame = glm::translate(glm::mat4(1.0f), position)
	* glm::rotate(glm::mat4(1.0f), rotAngles[2], glm::vec3(0.0f, 0.0f, 1.0f))
	* glm::rotate(glm::mat4(1.0f), rotAngles[1], glm::vec3(0.0f, 1.0f, 0.0f))
	* glm::rotate(glm::mat4(1.0f), rotAngles[0], glm::vec3(1.0f, 0.0f, 0.0f))
	* glm::scale(glm::mat4(1.0f), scale);

	return frame;
}

void World::processEntity(const pugi::xml_node& entity, const glm::mat4& globalFrame, bool templated, ObjectReferenceList& objectRefs){

	const auto typeNode = entity.find_child_by_attribute("name", "type");
	if(!typeNode){
		return;
	}

	const char* type = typeNode.first_child().value();
	// Early exit.
	if((strcmp(type, "ACTOR") != 0) && (strcmp(type, "DOOR") != 0) && (strcmp(type, "CREATURE") != 0)
	   && (strcmp(type, "LIGHT") != 0) && (strcmp(type, "CAMERA") != 0) ){
		return;
	}

	if(!isEntityVisible(entity)){
		return;
	}

	// Application of the frame on templates is weird.
	// It seems the template frame takes priority. Maybe it's the delta from the template frame to the first sub-element frame that should be used on other elements?
	glm::mat4 localFrame = getEntityFrame(entity);

	const bool useLocalFrame = !templated || (entity.find_child_by_attribute("param", "name", "link"));

	glm::mat4 frame = globalFrame;
	if(useLocalFrame){
		frame = frame * localFrame;
	}

	// Special case for lights
	if(strcmp(type, "LIGHT") == 0){

		const char* mdlPosStr = entity.find_child_by_attribute("name", "modelPosition").child_value();
		const char* mdlRotStr = entity.find_child_by_attribute("name", "modelRotation").child_value();

		const glm::vec3 mdlPosition = Area::parseVec3(mdlPosStr);
		const glm::vec3 mdlRotAngles = Area::parseVec3(mdlRotStr) / 180.0f * glm::pi<float>();
		const glm::mat4 mdlFrame =  glm::translate(glm::mat4(1.0f), mdlPosition)
			* glm::eulerAngleYXZ(mdlRotAngles[1], mdlRotAngles[0], mdlRotAngles[2]);

		frame = frame * mdlFrame;
	} else if(strcmp(type, "CAMERA") == 0){
		const char* cam2DRotStr = entity.find_child_by_attribute("name", "camerarotation").child_value();
		const glm::vec2 cam2DRot = Area::parseVec2(cam2DRotStr) / 180.0f * glm::pi<float>();
		// This is a wild guess.
		glm::mat4 mdlFrame = glm::rotate(glm::mat4(1.0f), cam2DRot[1], glm::vec3(0.0f, 1.0f, 0.0f));
		mdlFrame = glm::rotate(mdlFrame, cam2DRot[0], glm::vec3(glm::transpose(mdlFrame)[0]));
		frame = frame * mdlFrame;
	}

	// If there is a model, retrieve it.
	const char* objName = entity.find_child_by_attribute("name", "name").child_value();
	const char* objPathStr = entity.find_child_by_attribute("name", "sourceName").child_value();
	// Camera model has a few options including  default fallback.
	if(strcmp(type, "CAMERA") == 0){
		objPathStr = entity.find_child_by_attribute("name", "cameramodel").child_value();
		if(!objPathStr || objPathStr[0] == '\0'){
			objPathStr = entity.find_child_by_attribute("name", "cameraModel").child_value();
		}
		if(!objPathStr || objPathStr[0] == '\0'){
			objPathStr = "models\\objets\\cameras\\camera.dff";
		}
	}

	// Only keep elements linked with a model.
	if(!objPathStr || objPathStr[0] == '\0')
		return;

	// Cleanup model path.
	std::string objPathCleaned(objPathStr);
	TextUtilities::replace(objPathCleaned, "\\", "/");
	objPathCleaned = TextUtilities::lowercase(objPathCleaned);
	fs::path objPath = objPathCleaned;
	objPath.replace_extension("dff");

	// Has this model already been encountered?

	if(objectRefs.count(objPath) == 0){
		const uint objCount = (uint)objectRefs.size();
		objectRefs[objPath] = objCount;
	}
	_instances.emplace_back(objName, objectRefs[objPath], frame);

#ifdef LOG_WORLD_LOADING
	Log::info("Actor: %s", objName);
#endif
	//Log::info("Actor: %s, rot: (%f %f %f), model: %s, visible: %s", objName, rotAngles[0], rotAngles[1], rotAngles[2], modelName.c_str(), visible ? "yes" : "no");

}

bool World::load(const fs::path& path, const fs::path& resourcePath){

	pugi::xml_document world;
	pugi::xml_parse_result res = world.load_file(path.c_str());
	if(!res){
		Log::error("Unable to load world file at path %s:%llu %s", path.string().c_str(), res.offset, res.description());
		return false;
	}

	_name = path.filename().replace_extension().string();
	
	ObjectReferenceList referencedObjects;

	/// Instances parsing.
	const auto& entities = world.child("World").child("scene").child("entities");
	for(const auto& entity : entities.children("entity")){
		processEntity(entity, glm::mat4(1.0f), false, referencedObjects);

		// TODO: Interesting types to investigate: FX?
	}

	/// Templates parsing.
	for(const auto& instance : entities.children("instance")){
		if(!isEntityVisible(instance)){
			continue;
		}

		glm::mat4 frame = getEntityFrame(instance);

		std::string xmlFile = instance.find_child_by_attribute("name", "template").child_value();
		TextUtilities::replace(xmlFile, "\\", "/");
		const fs::path xmlPath = resourcePath / xmlFile;

		pugi::xml_document templateDef;
		if(!templateDef.load_file(xmlPath.c_str())){
			Log::error("Unable to load template file at path %s", xmlPath.string().c_str());
			continue;
		}

		const auto& entities = templateDef.child("template").child("entities");

		for(const auto& entity : entities.children("entity")){
			processEntity(entity, frame, true, referencedObjects );
		}
	}

	/// Objects loading.
	// Create objects from reference list.
	_objects.resize(referencedObjects.size());
	for(const auto& objRef : referencedObjects){
		const fs::path objPath = resourcePath / objRef.first;
		const std::string modelName = objPath.filename().replace_extension().string();
#ifdef LOG_WORLD_LOADING
		Log::info("Retrieving model %s", modelName.c_str());
#endif
		Dff::load(objPath, _objects[objRef.second]);
	}

	/// Areas loading.
	const auto& areas = world.child("World").child("scene").child("areas");
	for(const auto& area : areas.children()){

		const char* areaPathStr = area.attribute("sourceName").value();
		// Cleanup model path.
		std::string areaPathStrUp(areaPathStr);
		TextUtilities::replace(areaPathStrUp, "\\", "/");
		areaPathStrUp = TextUtilities::lowercase(areaPathStrUp);

		const fs::path areaPath = resourcePath / areaPathStrUp;
		const std::string areaName = areaPath.filename().replace_extension().string();
#ifdef LOG_WORLD_LOADING
		Log::info("Area: %s", areaName.c_str());
#endif
		if(!Area::load(areaPath, _objects.emplace_back())){
			_objects.pop_back();
			continue;
		}

		_instances.emplace_back(areaName, _objects.size()-1, glm::mat4(1.0f));
	}

	/// Empty objects cleanup.
	// Remove empty objects, and update instance indices.
	const uint objCount = _objects.size();
	std::vector<uint> indicesToDelete;
	std::vector<bool> shouldBeDeleted(objCount, false);
	std::vector<bool> isSwappable(objCount, true);
	std::unordered_map<uint, uint> indicesToReplace;

	for(uint oid = 0; oid < objCount; ++oid){
		const Object& obj = _objects[oid];
		if(!obj.name.empty() && !obj.positions.empty()){
			continue;
		}
		shouldBeDeleted[oid] = true;
		isSwappable[oid] = false;
		indicesToDelete.push_back(oid);
	}

	int lastSwappable = (int)objCount-1;
	for(uint iid : indicesToDelete){

		for(; lastSwappable > (int)iid; --lastSwappable){
			if(isSwappable[lastSwappable]){
				break;
			}
		}
		if(lastSwappable == (int)iid){
			// Unable to find another element to replace, we are after the last valid element, nothing else to do.
			break;
		}
		indicesToReplace[lastSwappable] = iid;
		// Swap the two elements.
		std::swap(_objects[lastSwappable], _objects[iid]);
		std::swap(isSwappable[lastSwappable], isSwappable[iid]);
		--lastSwappable;
	}

	_objects.resize(objCount - indicesToDelete.size());

	// Erase instances that reference empty objects.
	auto newInstancesEnd = std::remove_if(_instances.begin(), _instances.end(), [&shouldBeDeleted](const Instance& instance){
		return shouldBeDeleted[instance.object];
	});
	_instances.erase(newInstancesEnd, _instances.end());

	// Update indices of other instances.
	for(Instance& instance : _instances){
		auto elem = indicesToReplace.find(instance.object);
		if(elem == indicesToReplace.end()){
			continue;
		}
		instance.object = elem->second;
	}

	/// Extract list unique materials.
	for(Object& object : _objects){
		for(Object::Set& set : object.faceSets){
			const Object::Material& material = object.materials[set.material];

			const size_t matCount = _materials.size();
			uint mid = 0u;
			for(; mid < matCount; ++mid){
				if(_materials[mid] == material){
					break;
				}
			}
			if(mid == matCount){
				// Could not find an existing equivalent material.
				_materials.push_back(material);
			}
			// Point to global material.
			set.material = mid;
		}
		// Remove local materials.
		object.materials.clear();

	}
	return true;
}
