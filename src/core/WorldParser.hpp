#pragma once

#include "core/System.hpp"
#include "core/Geometry.hpp"
#include "core/Common.hpp"
#include "core/Bounds.hpp"
#include <map>
#include <unordered_map>

class World {

public:

	World();

	World(const Object& object);

	struct Instance {
		glm::mat4 frame;
		std::string name;
		uint object;

		Instance(const std::string& _name, uint _object, const glm::mat4& _frame);
	};

	struct Camera {
		glm::mat4 frame;
		std::string name;
		float fov;

		Camera(const std::string& _name, const glm::mat4& _frame, float _fov);
	};

	struct Light {

		static const uint NO_SHADOW = 0xFFFF;

		enum Type {
			POINT = 1, SPOT = 2, DIRECTIONAL = 3
		};

		glm::mat4 frame;
		glm::vec3 color;
		glm::vec3 radius;
		std::string name;
		float angle;
		uint material;
		Type type;
		bool shadow;

	};

	struct Zone {
		BoundingBox bbox;
		glm::vec4 ambientColor;
		glm::vec4 fogColor;
		glm::vec4 fogParams;
		std::string name;
		float fogDensity;
	};

	struct ParticleSystem {
		BoundingBox bbox;
		glm::mat4 frame;
		std::string name;
		uint material;
	};

	struct Billboard {
		glm::mat4 frame;
		glm::vec3 color;
		glm::vec2 size;
		std::string name;
		uint material;
	};

	bool load(const fs::path& path, const fs::path& resourcesPath);

	const std::vector<Object>& objects() const {  return _objects; };

	const std::vector<Instance>& instances() const {  return _instances; };
	
	const std::vector<Object::Material>& materials() const {  return _materials; };

	const std::vector<Camera>& cameras() const {  return _cameras; };

	const std::vector<Light>& lights() const {  return _lights; };

	const std::vector<ParticleSystem>& particles() const {  return _particles; };

	const std::vector<Billboard>& billboards() const {  return _billboards; };
	
	const std::vector<Zone>& zones() const {  return _zones; };

	const std::string& name() const{ return _name; };

private:

	using ObjectReferenceList = std::map<fs::path, uint>;
	using EntityFrameList = std::unordered_map<std::string, glm::mat4>;

	uint registerTextureMaterial(Object::Material::Type type, const std::string& textureName);

	void processEntity(const pugi::xml_node& entity, const glm::mat4& globalFrame, bool templated, const fs::path& resourcePath, ObjectReferenceList& objectRefs, EntityFrameList& entitiesList);

	std::vector<Object> _objects;
	std::vector<Instance> _instances;
	std::vector<Object::Material> _materials;
	std::vector<Camera> _cameras;
	std::vector<Light> _lights;
	std::vector<ParticleSystem> _particles;
	std::vector<Billboard> _billboards;
	std::vector<Zone> _zones;
	std::string _name;

};
