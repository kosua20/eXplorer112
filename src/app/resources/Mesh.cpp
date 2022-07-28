#include "resources/Mesh.hpp"
#include "graphics/GPUObjects.hpp"
#include "graphics/GPU.hpp"
#include "core/TextUtilities.hpp"

#include <sstream>
#include <fstream>
#include <cstddef>



// Mesh implementation.

Mesh::Mesh(const std::string & name) : _name(name) {

}


void Mesh::upload() {
	GPU::setupMesh(*this);
}

void Mesh::clearGeometry() {
	positions.clear();
	normals.clear();
	tangents.clear();
	bitangents.clear();
	texcoords.clear();
	indices.clear();
	// Don't update the metrics automatically
}

void Mesh::clean() {
	clearGeometry();
	bbox = BoundingBox();
	if(gpu) {
		gpu->clean();
	}
	// Both CPU and GPU are reset, so we can update the metrics.
	updateMetrics();
}

Buffer& Mesh::vertexBuffer(){
	assert(gpu != nullptr);
	return *gpu->vertexBuffer;
}

Buffer& Mesh::indexBuffer(){
	assert(gpu != nullptr);
	return *gpu->indexBuffer;
}

BoundingBox Mesh::computeBoundingBox() {
	bbox = BoundingBox();
	if(positions.empty()) {
		return bbox;
	}
	bbox.minis = bbox.maxis  = positions[0];
	const size_t numVertices = positions.size();
	for(size_t vid = 1; vid < numVertices; ++vid) {
		bbox.minis = glm::min(bbox.minis, positions[vid]);
		bbox.maxis = glm::max(bbox.maxis, positions[vid]);
	}

	updateMetrics();

	return bbox;
}

int Mesh::saveAsObj(const std::string & path, bool defaultUVs) {

	std::ofstream objFile(path);
	if(!objFile.is_open()) {
		Log::error( "Unable to create file at path %s.", path.c_str());
		return 1;
	}

	// Write vertices information.
	for(const auto & v : positions) {
		objFile << "v " << v.x << " " << v.y << " " << v.z << std::endl;
	}
	for(const auto & t : texcoords) {
		objFile << "vt " << t.x << " " << (1.0f - t.y) << std::endl;
	}
	for(const auto & n : normals) {
		objFile << "vn " << n.x << " " << n.y << " " << n.z << std::endl;
	}

	const bool hasNormals   = !normals.empty();
	const bool hasTexCoords = !texcoords.empty();
	// If the mesh has no UVs, it's probably using a uniform color material. We can force all vertices to have 0.5,0.5 UVs.
	std::string defUV;
	if(!hasTexCoords && defaultUVs) {
		objFile << "vt 0.5 0.5" << "\n";
		defUV = "1";
	}

	// Faces indices.
	for(size_t tid = 0; tid < indices.size(); tid += 3) {
		const std::string t0 = std::to_string(indices[tid + 0] + 1);
		const std::string t1 = std::to_string(indices[tid + 1] + 1);
		const std::string t2 = std::to_string(indices[tid + 2] + 1);
		objFile << "f";
		objFile << " " << t0 << "/" << (hasTexCoords ? t0 : defUV) << "/" << (hasNormals ? t0 : "");
		objFile << " " << t1 << "/" << (hasTexCoords ? t1 : defUV) << "/" << (hasNormals ? t1 : "");
		objFile << " " << t2 << "/" << (hasTexCoords ? t2 : defUV) << "/" << (hasNormals ? t2 : "");
		objFile << std::endl;
	}
	objFile.close();
	return 0;
}


const std::string & Mesh::name() const {
	return _name;
}

bool Mesh::hadNormals() const {
	return _metrics.normals != 0;
}

bool Mesh::hadTexcoords() const {
	return _metrics.texcoords != 0;
}

bool Mesh::hadColors() const {
	return _metrics.colors != 0;
}

const Mesh::Metrics & Mesh::metrics() const {
	return _metrics;
}

void Mesh::updateMetrics(){
	_metrics.vertices = positions.size();
	_metrics.normals = normals.size();
	_metrics.tangents = tangents.size();
	_metrics.bitangents = bitangents.size();
	_metrics.colors = colors.size();
	_metrics.texcoords = texcoords.size();
	_metrics.indices = indices.size();
}

Mesh & Mesh::operator=(Mesh &&) = default;

Mesh::Mesh(Mesh &&) = default;

Mesh::~Mesh() = default;

