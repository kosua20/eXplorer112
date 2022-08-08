#pragma once

#include "core/System.hpp"
#include "core/Geometry.hpp"
#include "core/Common.hpp"
#include <map>

class World {

public:

	struct Instance {
		glm::mat4 frame;
		uint object;

		Instance(uint _object, const glm::mat4& _frame);
	};

	bool load(const fs::path& path, const fs::path& resourcesPath);

	const std::vector<Object>& objects() const {  return _objects; };

	const std::vector<Instance>& instances() const {  return _instances; };
	
	const TexturesList& textures() const {  return _textures; };


private:


	using ObjectReferenceList = std::map<fs::path, uint>;
	
	void processEntity(const pugi::xml_node& entity, const glm::mat4& globalFrame, bool templated, ObjectReferenceList& objectRefs);

	std::vector<Object> _objects;
	std::vector<Instance> _instances;
	TexturesList _textures;

};
