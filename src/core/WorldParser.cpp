#include "core/WorldParser.hpp"
#include "core/Log.hpp"
#include "core/TextUtilities.hpp"
#include "core/AreaParser.hpp"
#include "core/DFFParser.hpp"
#include "core/GameCode.hpp"
#include "core/Common.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <unordered_map>


//#define LOG_WORLD_LOADING

World::Instance::Instance(const std::string& _name, uint _object, const glm::mat4& _frame) :
	frame(_frame), name(_name), object(_object){

}
World::Camera::Camera(const std::string& _name, const glm::mat4& _frame, float _fov) :
	frame(_frame), name(_name), fov(_fov){

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

	// Default zone.
	BoundingBox bbox;
	const auto& positions = object.positions;
	for(const glm::vec3& pos : positions){
		bbox.merge(pos);
	}
	BoundingSphere bsphere = bbox.getSphere();


	Zone& zone = _zones.emplace_back();
	zone.name = "Default";
	zone.bbox = bbox;
	zone.ambientColor = glm::vec4(0.1f);
	zone.fogColor = glm::vec4(0.2f);
	zone.fogParams = glm::vec4(0.0f);
	zone.fogDensity = 0.0f;
	
	// Default light.
	Light& light = _lights.emplace_back();
	light.type = Light::DIRECTIONAL;

	const glm::vec3 position = bsphere.center + glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f)) * bsphere.radius;
	glm::mat4 view = glm::lookAt(position, bsphere.center, glm::vec3(0.0f, 1.0f, 0.0f));
	// Handedness.
	view[0][2] *= -1.0f;
	view[2][2] *= -1.0f;
	view[1][2] *= -1.0f;
	view[3][2] *= -1.0f;
	
	light.frame = glm::inverse(view);
	light.color = glm::vec3(1.0f);
	light.name = "Default";
	light.radius = glm::vec3(glm::length(bbox.getSize()));
	light.angle = 0.0f;
	light.shadow = true;
	light.material = Object::Material::NO_MATERIAL;
}

std::string getEntityAttribute(const pugi::xml_node& entity, const char* key){
	std::string val = entity.attribute(key).value();
	if(!val.empty()){
		return TextUtilities::lowercase(val);
	}
	val = entity.find_child_by_attribute("name", key).child_value();
	if(!val.empty()){
		return TextUtilities::lowercase(val);
	}
	return "";
}

const char* getLightAttribute(const pugi::xml_node& parent, const pugi::xml_node& child, const char* key){
	const char* valStr = child.find_child_by_attribute("name", key).child_value();
	if(!valStr || valStr[0] == '\0'){
		valStr = parent.find_child_by_attribute("name", key).child_value();
	}
	return valStr;
}

bool isEntityVisible(const pugi::xml_node& entity){
	const char* boolVal = entity.find_child_by_attribute("name", "visible").child_value();
	return Area::parseBool(boolVal, true);
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


std::string getMaterialTexture(const std::string& inMaterialStr, const fs::path& resourcePath){

	if(inMaterialStr.empty()){
		return "";
	}

	std::string materialStr(inMaterialStr);
	TextUtilities::replace(materialStr, "\\", "/");
	materialStr = TextUtilities::trim(materialStr, "/");

	const fs::path materialPath = materialStr;
	const std::string extension = TextUtilities::lowercase(materialPath.filename().extension().string());
	// Two possibilities: either a texture file, or a material definition.
	// In both cases we want to retrieve a non-empty texture name.
	std::string textureName;

	if(extension == ".mtl"){
		// Load the mtl XML file.
		const fs::path mtlPath = resourcePath / materialPath;
		pugi::xml_document mtlDef;
		if(mtlDef.load_file(mtlPath.c_str())){
			pugi::xml_node frame = mtlDef.child("matDef").child("framelist").first_child();
			// Extract texture name, assume it is unique.
			std::string textureStr = frame.attribute("sourcename").value();
			if(!textureStr.empty()){
				TextUtilities::replace(textureStr, "\\", "/");
				const fs::path texturePath = TextUtilities::trim(textureStr, "/");
				textureName = texturePath.filename().replace_extension().string();
			}
		} else {
			Log::error("Unable to load mtl file at path %s", mtlPath.string().c_str());
		}

	} else if(extension == ".tga" || extension == ".dds" || extension == ".png"){
		textureName = materialPath.filename().replace_extension().string();
	}
	return textureName;
}

uint World::registerTextureMaterial(Object::Material::Type type, const std::string& textureName){
	if(textureName.empty()){
		return Object::Material::NO_MATERIAL;
	}

	Object::Material material;
	material.color = textureName;
	material.normal = "";
	material.type = type;

	// Insert material in list, except if already present.
	const uint matCount = (uint)_materials.size();
	uint mid = 0;
	for(; mid < matCount; ++mid){
		if(_materials[mid] == material){
			break;
		}
	}
	if(mid == matCount){
		_materials.push_back(material);
	}
	return mid;
}

void World::processFxDef(const pugi::xml_document& fxDef, const std::string& baseName, const glm::mat4& frame, const fs::path& resourcePath){

	const auto& emitters = fxDef.child("fxDef").child("emitters");
	uint i = 0;
	for(const auto& emitter : emitters.children("emitter")){
		//"position" and "rotation" always zero
		/*
	   <param name="type" data="int">2</param>
	   <param name="particletype" data="int">0</param>
	   <param name="tanksize" data="int">400</param>
	   <param name="timing" data="real[2]">(0.800, 2.500)</param>
	   <param name="size" data="real[2]">(0.500, 1.000)</param>
	   <param name="angle" data="real[2]">(-70.000, 70.000)</param>
	   <param name="velocity" data="real[2]">(-1.000, -1.000)</param>
	   <param name="rollspeed" data="real[2]">(1500.000, 150.000)</param>
	   <param name="radius" data="real">0.000</param>
	   <param name="regenrate" data="real">400.000</param>
	   <param name="blending" data="int">1</param>
		*/

		std::string materialStr = emitter.find_child_by_attribute("name", "material").child_value();
		materialStr = TextUtilities::trim(materialStr, "\"");
		const std::string textureName = getMaterialTexture(materialStr, resourcePath);
		const uint materialId = registerTextureMaterial(Object::Material::PARTICLE, textureName);
		//Log::info("* %s", textureName.c_str());

		const char* minDimStr =  emitter.find_child_by_attribute("name", "dimension_min").child_value();
		const char* maxDimStr =  emitter.find_child_by_attribute("name", "dimension_max").child_value();
		const glm::vec3 minDim = Area::parseVec3(minDimStr, glm::vec3(0.f));
		const glm::vec3 maxDim = Area::parseVec3(maxDimStr, glm::vec3(0.f));

		const char* minColorStr =  emitter.find_child_by_attribute("name", "color_min").child_value();
		const char* maxColorStr =  emitter.find_child_by_attribute("name", "color_max").child_value();
		const glm::vec4 minColor = Area::parseVec4(minColorStr, glm::vec4(1.f));
		const glm::vec4 maxColor = Area::parseVec4(maxColorStr, glm::vec4(1.f));

		ParticleSystem& fx = _particles.emplace_back();
		fx.name = baseName + "_emitter_" + std::to_string(i);
		fx.frame = frame;
		fx.color = 0.5f * (minColor + maxColor);
		fx.material = materialId;
		fx.bbox = BoundingBox(minDim, maxDim);
		++i;

		const glm::vec3 sizes = fx.bbox.getSize();
		Log::info("Emitter %s: %fx%fx%f, %s, %f,%f,%f", fx.name.c_str(), sizes[0], sizes[1], sizes[2], textureName.c_str(), fx.color[0], fx.color[1], fx.color[2]);

	}
}

void World::processEntity(const pugi::xml_node& entity, const glm::mat4& globalFrame, bool templated, const fs::path& resourcePath, ObjectReferenceList& objectRefs, EntityFrameList& entitiesList){

	const std::string type = getEntityAttribute(entity, "type");
	if(type.empty()){
		return;
	}

	const std::string objName = getEntityAttribute(entity, "name");
	glm::mat4 frame = globalFrame;

	pugi::xml_node linkedEntity = entity.find_child_by_attribute("param", "name", "link");
	if(linkedEntity){
		std::string linkedEntityName(linkedEntity.child_value());
		linkedEntityName = TextUtilities::lowercase(linkedEntityName);

		if(!linkedEntity.empty()){
			auto linkEntityRecord = entitiesList.find(linkedEntityName);
			if(linkEntityRecord == entitiesList.end()){
				Log::warning("Unable to find linked entity %s.", linkedEntityName.c_str(), objName.c_str());
			} else {
				frame = linkEntityRecord->second;
			}
		}
	}

	// Skip the local frame for the main element of a template.
	bool useLocalFrame = !templated || linkedEntity;
	if(useLocalFrame){
		glm::mat4 localFrame = getEntityFrame(entity);
		frame = frame * localFrame;
	}

	// Store entity.
	if(entitiesList.count(objName) != 0){
		Log::warning("Entity named %s already exists.", objName.c_str());
	}
	entitiesList[objName] = frame;
	const glm::mat4 entityFrame = frame;
	glm::mat4 mdlFrame(1.0f);

	// Special case for lights
	if(type == "light"){
		const char* mdlPosStr = entity.find_child_by_attribute("name", "modelPosition").child_value();
		const char* mdlRotStr = entity.find_child_by_attribute("name", "modelRotation").child_value();

		const glm::vec3 mdlPosition = Area::parseVec3(mdlPosStr);
		const glm::vec3 mdlRotAngles = Area::parseVec3(mdlRotStr) / 180.0f * glm::pi<float>();
		mdlFrame = glm::translate(glm::mat4(1.0f), mdlPosition)
				* glm::rotate(glm::mat4(1.0f), mdlRotAngles[2], glm::vec3(0.0f, 0.0f, 1.0f))
				* glm::rotate(glm::mat4(1.0f), mdlRotAngles[1], glm::vec3(0.0f, 1.0f, 0.0f))
				* glm::rotate(glm::mat4(1.0f), mdlRotAngles[0], glm::vec3(1.0f, 0.0f, 0.0f));

		frame = frame * mdlFrame;
	} else if(type == "camera"){
		const char* cam2DRotStr = entity.find_child_by_attribute("name", "cameraInitialRotation").child_value();
		const glm::vec2 cam2DRot = Area::parseVec2(cam2DRotStr); // Conversion to radians will be done below.
		glm::mat4 mdlFrame = GameCode::cameraRotationMatrix(cam2DRot[0], cam2DRot[1]);
		frame = frame * mdlFrame;

		const std::string uiName = entity.find_child_by_attribute("name", "uiName").child_value();
		const std::string camName = !uiName.empty() ? std::string(uiName) : (!objName.empty() ? objName : "Unknown camera");
		const char* fovStr = entity.find_child_by_attribute("name", "fov").child_value();
		float fov = ((fovStr && fovStr[0] != '\0') ? std::stof(fovStr) : 45.0f) * glm::pi<float>() / 180.0f;
		// Adjust the frame, putting the viewpoint at the front of the default camera.
		glm::mat4 renderFrame = frame * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -2.75f, 11.0f));
		// Store the camera
		_cameras.emplace_back(camName, renderFrame, fov);
	}


	// We can't early exit earlier because of linking.
	if((type != "actor") && (type != "door") && (type != "creature")
	   && (type != "light") && (type != "camera") && (type != "solid") && (type != "particle")&& (type != "fx") ){
		return;
	}

	// Some non-visible items are visible in the game (second light outside wake-up room in tutoeco for instance)
	//if(!isEntityVisible(entity)){
	//	return;
	//}

	if(type == "particle" || type == "fx"){
		// Two types of FX

		const char* fxTypeStr = entity.find_child_by_attribute("name", "fxType").child_value();
		const int fxType = Area::parseInt(fxTypeStr, 0);

		if(fxType == 9){
			// Billboard
			// Type: 0,1,3
			// 0 is probably world space?
			const char* billboardTypeStr = entity.find_child_by_attribute("name", "billboardType").child_value();
			const int billboardType = Area::parseInt(billboardTypeStr, 0);
			// Blending 1 is additive?
			const char* blendingStr = entity.find_child_by_attribute("name", "blending").child_value();
			const int blending = Area::parseInt(blendingStr, 0);

			const char* widthStr = entity.find_child_by_attribute("name", "width").child_value();
			const float width = Area::parseFloat(widthStr, 1.f);
			const char* heightStr = entity.find_child_by_attribute("name", "height").child_value();
			const float height = Area::parseFloat(heightStr, 1.f);

			const char* colorStr = entity.find_child_by_attribute("name", "color").child_value();
			const glm::vec3 color = Area::parseVec3(colorStr, glm::vec3(1.0f));

			const std::string materialStr = getEntityAttribute(entity, "material");
			const std::string textureName = getMaterialTexture(materialStr, resourcePath);
			const uint materialId = registerTextureMaterial(Object::Material::BILLBOARD, textureName);

			Billboard& fx = _billboards.emplace_back();
			fx.name = objName;
			fx.frame = frame;
			fx.material = materialId;
			fx.size = glm::vec2( width, height );
			fx.color = color;

			Log::info("Billboard %s: %d,%d, %fx%f, %s, %f,%f,%f", objName.c_str(), billboardType, blending, width, height, textureName.c_str(), color[0], color[1], color[2]);
		} else if(fxType == 7){
			// System
			// Immediately retrieve fxDef path.
			std::string fxDefStr = getEntityAttribute(entity, "sourceName");
			TextUtilities::replace(fxDefStr, "\\", "/");
			fxDefStr = TextUtilities::trim(fxDefStr, "/");
			if(fxDefStr.empty()){
				return;
			}
			// Load the FXDEF XML file.
			const fs::path fxDefPath = resourcePath / fxDefStr;
			std::string fxDefContent = System::loadString(fxDefPath);
			if(fxDefContent.empty()){
				return;
			}

			// Uuuuurgh.
			TextUtilities::replace(fxDefContent, "\"name=\"", "\" name=\"");
			pugi::xml_document fxDef;
			pugi::xml_parse_result res = fxDef.load_string(fxDefContent.c_str());
			if(!res){
				Log::error("Unable to load fxDef file at path %s: %s", fxDefPath.string().c_str(), res.description());
				return;
			}
			processFxDef(fxDef, objName, frame, resourcePath);
		}
		// No model associated, exit.
		return;
	}

	if(type == "light"){
		// Some lights have a child named "light".
		const pugi::xml_node lightChild =  entity.child("light");
		// Retrieve type.
		std::string lightTypeStr = lightChild.attribute("type").value();
		if(lightTypeStr.empty()){
			lightTypeStr = entity.find_child_by_attribute("name", "lightType").child_value();
		}
		const int lightType = Area::parseInt(lightTypeStr.c_str(), 1);
		Log::check(lightType >= 1 && lightType <= 3, "Unexpected type for light %s", objName.c_str());

		Light& light = _lights.emplace_back();
		light.frame = entityFrame;
		light.name = objName;
		light.type = (Light::Type)lightType;

		light.color = Area::parseVec3(getLightAttribute(entity, lightChild, "color"), glm::vec3(1.0f));
		light.radius = Area::parseVec3(getLightAttribute(entity, lightChild, "radius"), glm::vec3(10000.0f));

		const char* coneTypeStr = getLightAttribute(entity, lightChild, "coneAngle");
		if(!coneTypeStr || coneTypeStr[0] == '\0'){
			coneTypeStr = getLightAttribute(entity, lightChild, "cone angle");
		}
		light.angle = (float)(Area::parseInt(coneTypeStr)) * glm::pi<float>() / 180.0f * 0.5f;
		// Fallback based on radii if angle is null for a spotlight.
		if( ( light.type == Light::SPOT ) && ( light.angle < 0.001f ) )
		{
			Log::verbose( "Detected spot light with small angle (%.f), falling back to point light.", light.angle );
			glm::vec2 angles = glm::atan2(glm::vec2(light.radius.z), glm::vec2(light.radius));
			angles = glm::abs(angles);
			light.angle = std::max( angles.x, angles.y );
			light.type = Light::POINT;
		}
		const char* shadowStr = getLightAttribute(entity, lightChild, "shadow");
		light.shadow = Area::parseBool(shadowStr);

		const std::string materialStr = getLightAttribute(entity, lightChild, "material");
		const std::string textureName = getMaterialTexture(materialStr, resourcePath);
		light.material = registerTextureMaterial(Object::Material::LIGHT, textureName);
	}

	std::string objPathStr = getEntityAttribute(entity, "sourceName");
	// Camera model has a few options including  default fallback.
	if(type == "camera"){
		objPathStr = entity.find_child_by_attribute("name", "cameramodel").child_value();
		if(objPathStr.empty()){
			objPathStr = entity.find_child_by_attribute("name", "cameraModel").child_value();
		}
		if(objPathStr.empty()){
			objPathStr = "models\\objets\\cameras\\camera.dff";
		}
	}

	// Only keep elements linked with a model.
	if(objPathStr.empty())
		return;

	// Cleanup model path.
	TextUtilities::replace(objPathStr, "\\", "/");
	fs::path objPath = TextUtilities::lowercase(objPathStr);
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
	EntityFrameList entitiesList;

	const auto& items = world.child("World").child("scene").child("entities").children();

	for(const auto& item : items){
		if(strcmp(item.name(), "entity") == 0){
			processEntity(item, glm::mat4(1.0f), false, resourcePath, referencedObjects, entitiesList);
			continue;
		}
		if(strcmp(item.name(), "instance") == 0){
			const std::string name = getEntityAttribute(item, "name");
			glm::mat4 frame = getEntityFrame(item);
			entitiesList[name] = frame;

			//if(!isEntityVisible(item)){
			//	continue;
			//}

			std::string xmlFile = item.find_child_by_attribute("name", "template").child_value();
			TextUtilities::replace(xmlFile, "\\", "/");
			const fs::path xmlPath = resourcePath / xmlFile;

			pugi::xml_document templateDef;
			if(!templateDef.load_file(xmlPath.c_str())){
				Log::error("Unable to load template file at path %s", xmlPath.string().c_str());
				continue;
			}

			// Assume no instances in template.
			const auto& entities = templateDef.child("template").child("entities");
			// Use a local entity list.
			EntityFrameList templateEntitiesList;
			for(const auto& entity : entities.children("entity")){
				processEntity(entity, frame, true, resourcePath, referencedObjects, templateEntitiesList);
			}
			continue;
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

		// Load geometry
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

		// Parse postprocess infos.
		Zone& zone = _zones.emplace_back();
		zone.name = area.attribute("name").value();
		// Update area bounding box.
		zone.bbox = BoundingBox();
		const auto& positions = _objects.back().positions;
		for(const glm::vec3& pos : positions){
			zone.bbox.merge(pos);
		}
		// Other information.
		const char* ambientStr = area.find_child_by_attribute("name", "ambientColor").child_value();
		const char* fogColorStr = area.find_child_by_attribute("name", "fogColor").child_value();
		const char* fogParamsStr = area.find_child_by_attribute("name", "hfogParams").child_value();
		const char* fogDensityStr = area.find_child_by_attribute("name", "fogDensity").child_value();
		zone.ambientColor = Area::parseVec4(ambientStr);
		zone.fogColor = Area::parseVec4(fogColorStr);
		zone.fogParams = Area::parseVec4(fogParamsStr);
		zone.fogDensity = Area::parseFloat(fogDensityStr);
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

	/// Extract list of unique materials.
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
