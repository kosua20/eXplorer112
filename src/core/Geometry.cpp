#include "core/Geometry.hpp"

void writeMtlToStream(const Obj& obj, std::ofstream& mtlFile){
	mtlFile << obj.materials << "\n";
}

void writeObjToStream(const Obj& obj, std::ofstream& objFile, ObjOffsets & offsets, const glm::mat4& frame){

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
		objFile << "vt " << uv.x << " " << uv.y << "\n";
	}

	objFile << "s 1\n";
	for(const Obj::Set& set : obj.faceSets){
		objFile << "o " << set.name << "\n";
		objFile << "usemtl " << set.material << "\n";

		const bool hasUV = !set.faces.empty() && (set.faces[0].t0 != 0xFFFF);
		const bool hasNormals = !set.faces.empty() && (set.faces[0].n0 != 0xFFFF);

		for(const Obj::Set::Face& f : set.faces){
			const uint32_t v0 = f.v0 + offsets.v;
			const uint32_t v1 = f.v1 + offsets.v;
			const uint32_t v2 = f.v2 + offsets.v;
			const uint32_t t0 = f.t0 + offsets.t;
			const uint32_t t1 = f.t1 + offsets.t;
			const uint32_t t2 = f.t2 + offsets.t;
			const uint32_t n0 = f.n0 + offsets.n;
			const uint32_t n1 = f.n1 + offsets.n;
			const uint32_t n2 = f.n2 + offsets.n;

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
	}
	offsets.v += obj.positions.size();
	offsets.t += obj.uvs.size();
	offsets.n += obj.normals.size();
}

void writeObjToStream(const Obj& obj, std::ofstream& objFile){
	ObjOffsets offsets;
	writeObjToStream(obj, objFile, offsets, glm::mat4(1.0f));
}
