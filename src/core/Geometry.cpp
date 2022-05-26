#include "core/Geometry.hpp"


void writeObjToStreams(const Obj& obj, std::ofstream& objFile, std::ofstream& mtlFile){

	mtlFile << obj.materials << "\n";

	for(const auto& pos : obj.positions){
		objFile << "v " << pos.x << " " << pos.y << " " << pos.z << "\n";
	}
	for(const auto& nor : obj.normals){
		objFile << "vn " << nor.x << " " << nor.y << " " << nor.z << "\n";
	}
	for(const auto& uv : obj.uvs){
		objFile << "vt " << uv.x << " " << uv.y << "\n";
	}

	objFile << "s 1\n";
	for(const Obj::Set& set : obj.faceSets){
		objFile << "usemtl " << set.material << "\n";

		const bool hasUV = !set.faces.empty() && (set.faces[0].t0 != 0xFFFF);
		const bool hasNormals = !set.faces.empty() && (set.faces[0].n0 != 0xFFFF);
		for(const Obj::Set::Face& f : set.faces){
			if(hasNormals && hasUV){
				objFile << " f " << f.v0 << "/" << f.t0 << "/" << f.n0 << " ";
				objFile << 		  f.v1 << "/" << f.t1 << "/" << f.n1 << " ";
				objFile << 		  f.v2 << "/" << f.t2 << "/" << f.n2 << "\n";
			} else if(hasNormals){
				objFile << " f " << f.v0 << "//" << f.n0 << " ";
				objFile << 		  f.v1 << "//" << f.n1 << " ";
				objFile << 		  f.v2 << "//" << f.n2 << "\n";
			} else if(hasUV){
				objFile << " f " << f.v0 << "/" << f.t0 << " ";
				objFile << 		  f.v1 << "/" << f.t1 << " ";
				objFile << 		  f.v2 << "/" << f.t2 << "\n";
			} else {
				objFile << " f " << f.v0 << " ";
				objFile << 		  f.v1 << " ";
				objFile << 		  f.v2 << "\n";
			}

		}
	}
}

