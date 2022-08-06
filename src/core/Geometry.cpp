#include "core/Geometry.hpp"

void writeMtlToStream(const Object& obj, std::ofstream& mtlFile){

	uint materialId = 0;
	for(const Object::Material& mat : obj.materials){
		const float amb = 1.0f;
		const float diff = 1.0f;
		const float spec = 1.0f;

		const std::string matName = obj.name + "_mat_" + std::to_string(materialId);
		mtlFile << "newmtl " << matName << "\n";
		mtlFile << "Ka " << amb << " " << amb << " " << amb << "\n";
		mtlFile << "Kd " << diff << " " << diff << " " << diff << "\n";
		mtlFile << "Ks " << spec << " " << spec << " " << spec << "\n";
		mtlFile << "Ns " << 100 << "\n";
		if(!mat.texture.empty()){
			mtlFile << "map_Kd " << "textures/" << mat.texture << ".png\n";
		}
		mtlFile << "\n";

		++materialId;
	}

}

void writeObjToStream(const Object& obj, std::ofstream& objFile, ObjOffsets & offsets, const glm::mat4& frame){

	for(const auto& pos : obj.positions){
		const glm::vec3 posf = glm::vec3(frame * glm::vec4(pos, 1.0f));
		objFile << "v " << posf.x << " " << posf.y << " " << posf.z << "\n";
	}

	const glm::mat3 frameNormal = glm::inverse(glm::transpose(glm::mat3(frame)));
	for(const auto& nor : obj.normals){
		const glm::vec3 norf = glm::normalize(frameNormal * nor);
		objFile << "vn " << norf.x << " " << norf.y << " " << norf.z << "\n";
	}

	for(const auto& uv : obj.uvs){
		// We need to flip UVs.
		objFile << "vt " << uv.x << " " << (1.f - uv.y) << "\n";
	}

	objFile << "s 1\n";
	uint setIndex = 0;
	for(const Object::Set& set : obj.faceSets){
		const std::string setName = obj.name + "_obj_" + std::to_string(setIndex);
		const std::string matName = obj.name + "_mat_" + std::to_string(set.material);
		objFile << "o " << setName << "\n";
		objFile << "usemtl " << matName << "\n";

		const bool hasUV = !set.faces.empty() && (set.faces[0].t0 != Object::Set::Face::INVALID);
		const bool hasNormals = !set.faces.empty() && (set.faces[0].n0 != Object::Set::Face::INVALID);

		for(const Object::Set::Face& f : set.faces){
			// OBJ indexing starts at 1.
			const uint32_t v0 = f.v0 + offsets.v + 1u;
			const uint32_t v1 = f.v1 + offsets.v + 1u;
			const uint32_t v2 = f.v2 + offsets.v + 1u;
			const uint32_t t0 = f.t0 + offsets.t + 1u;
			const uint32_t t1 = f.t1 + offsets.t + 1u;
			const uint32_t t2 = f.t2 + offsets.t + 1u;
			const uint32_t n0 = f.n0 + offsets.n + 1u;
			const uint32_t n1 = f.n1 + offsets.n + 1u;
			const uint32_t n2 = f.n2 + offsets.n + 1u;

			if(hasNormals && hasUV){
				objFile << "f "  << v0 << "/" << t0  << "/" << n0 << " ";
				objFile << 		    v1 << "/" << t1  << "/" << n1 << " ";
				objFile << 		    v2 << "/" << t2  << "/" << n2 << "\n";
			} else if(hasNormals){
				objFile << "f "  << v0 << "//" << n0 << " ";
				objFile << 		    v1 << "//" << n1 << " ";
				objFile << 		    v2 << "//" << n2 << "\n";
			} else if(hasUV){
				objFile << "f "  << v0 << "/" << t0 << " ";
				objFile << 		    v1 << "/" << t1 << " ";
				objFile << 		    v2 << "/" << t2 << "\n";
			} else {
				objFile << "f "  << v0 << " ";
				objFile << 		    v1 << " ";
				objFile << 		    v2 << "\n";
			}

		}
		++setIndex;
	}
	offsets.v += (uint32_t)obj.positions.size();
	offsets.t += (uint32_t)obj.uvs.size();
	offsets.n += (uint32_t)obj.normals.size();
}

void writeObjToStream(const Object& obj, std::ofstream& objFile){
	ObjOffsets offsets;
	writeObjToStream(obj, objFile, offsets, glm::mat4(1.0f));
}
