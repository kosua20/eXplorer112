#include "resources/Mesh.hpp"
#include "graphics/GPUObjects.hpp"
#include "graphics/GPU.hpp"
#include "core/TextUtilities.hpp"

#include <mikktspace/mikktspace.h>
#include <sstream>
#include <fstream>
#include <cstddef>


// MikkTSpace helpers.

/** Data that will be needed in the MikkTSpace callbacks. */
struct MikktspaceWrapper {
	Mesh * mesh; ///< The mesh being processed.
	int faceCount; ///< Number of faces (cached).
	std::vector<glm::vec4> tangents; ///< Will store the tangents for each index of each face.
};

/** Query the number of faces in the mesh.
	\param context our internal data
	\return the number of faces in the mesh
 */
int mtsGetNumFaces(const SMikkTSpaceContext * context){
	const MikktspaceWrapper * meshWrap = reinterpret_cast<MikktspaceWrapper *>(context->m_pUserData);
	return meshWrap->faceCount;
}

/** Query the number of vertices in a given face of the mesh.
	\param context our internal data
	\param faceId the face index
	\return the number of vertices in the face
 */
int mtsGetNumVerticesOfFace(const SMikkTSpaceContext * context, const int faceId){
	(void)context;
	(void)faceId;
	return 3;
}

/** Query the position of a given vertex of a given face
	\param context our internal data
	\param posOut will be populated with the position
	\param faceId the face index
	\param vertId the vertex index in the face
 */
void mtsGetPosition(const SMikkTSpaceContext * context, float posOut[], const int faceId, const int vertId){
	Mesh& mesh = *(reinterpret_cast<MikktspaceWrapper *>(context->m_pUserData)->mesh);
	const glm::vec3 & position = mesh.positions[mesh.indices[3 * faceId + vertId]];
	posOut[0] = position[0];
	posOut[1] = position[1];
	posOut[2] = position[2];
}

/** Query the normal of a given vertex of a given face
	\param context our internal data
	\param normOut will be populated with the normal
	\param faceId the face index
	\param vertId the vertex index in the face
 */
void mtsGetNormal(const SMikkTSpaceContext * context, float normOut[], const int faceId, const int vertId){
	Mesh& mesh = *(reinterpret_cast<MikktspaceWrapper *>(context->m_pUserData)->mesh);
	const glm::vec3& normal = mesh.normals[mesh.indices[3 * faceId + vertId]];
	normOut[0] = normal[0];
	normOut[1] = normal[1];
	normOut[2] = normal[2];
}

/** Query the texture coordinates of a given vertex of a given face
	\param context our internal data
	\param texcOut will be populated with the texture coordinates
	\param faceId the face index
	\param vertId the vertex index in the face
 */
void mtsGetTexCoord(const SMikkTSpaceContext * context, float texcOut[], const int faceId, const int vertId){
	Mesh& mesh = *(reinterpret_cast<MikktspaceWrapper *>(context->m_pUserData)->mesh);
	const glm::vec2 & texcoord = mesh.texcoords[mesh.indices[3 * faceId + vertId]];
	texcOut[0] = texcoord[0];
	texcOut[1] = texcoord[1];
}

/** Store the computed tangent of a given vertex of a given face
	\param context our internal data
	\param tangent the computed tangent
	\param sign the orientation of the bitangent with respect to a standard frame
	\param faceId the face index
	\param vertId the vertex index in the face
 */
void mtsSetTSpaceBasic(const SMikkTSpaceContext * context, const float tangent[], const float sign, const int faceId, const int vertId){
	std::vector<glm::vec4> & tangents = reinterpret_cast<MikktspaceWrapper *>(context->m_pUserData)->tangents;
	tangents[faceId * 3 + vertId] = glm::vec4(tangent[0], tangent[1], tangent[2], sign);
}

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
	colors.clear();
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

void Mesh::computeNormals() {
	normals.resize(positions.size());
	for(auto & n : normals) {
		n = glm::vec3(0.0f);
	}
	// Iterate over faces.
	for(size_t tid = 0; tid < indices.size(); tid += 3) {
		const unsigned int i0 = indices[tid + 0];
		const unsigned int i1 = indices[tid + 1];
		const unsigned int i2 = indices[tid + 2];
		const glm::vec3 & v0  = positions[i0];
		const glm::vec3 & v1  = positions[i1];
		const glm::vec3 & v2  = positions[i2];
		// Compute cross product between the two edges of the triangle.
		const glm::vec3 d01	= glm::normalize(v1 - v0);
		const glm::vec3 d02	= glm::normalize(v2 - v0);
		const glm::vec3 normal = glm::cross(d01, d02);
		normals[i0] += normal;
		normals[i1] += normal;
		normals[i2] += normal;
	}
	// Average for each vertex normal.
	for(auto & n : normals) {
		n = glm::normalize(n);
	}

	updateMetrics();
}

void Mesh::computeTangentsAndBitangents(bool force) {
	const bool uvAvailable = !texcoords.empty();
	if(positions.empty() || normals.empty() || (!uvAvailable && !force)) {
		// No available info.
		return;
	}

	// Use dummy constant UVs.
	if(!uvAvailable && force){
		texcoords.resize(positions.size(), glm::vec2(0.5f));
	}

	// Prepare interface.
	SMikkTSpaceInterface interface;
	interface.m_getNumFaces = &mtsGetNumFaces;
	interface.m_getNumVerticesOfFace = &mtsGetNumVerticesOfFace;
	interface.m_getPosition = &mtsGetPosition;
	interface.m_getNormal = &mtsGetNormal;
	interface.m_getTexCoord = &mtsGetTexCoord;
	interface.m_setTSpaceBasic = &mtsSetTSpaceBasic;
	interface.m_setTSpace = nullptr;

	// Wrap our data.
	const uint indexCount = ( uint )indices.size();
	MikktspaceWrapper meshWrapper;
	meshWrapper.mesh = this;
	meshWrapper.faceCount = indexCount / 3;
	meshWrapper.tangents.resize(indexCount);

	// Run.
	SMikkTSpaceContext context;
	context.m_pInterface = &interface;
	context.m_pUserData = &meshWrapper;
	if(!genTangSpaceDefault(&context)){
		Log::error("Unable to generate tangent frame for %s.", _name .c_str());
		tangents.resize(positions.size(), glm::vec3(1.0f,0.0f,0.0f));
		bitangents.resize(positions.size(), glm::vec3(0.0f,1.0f,0.0f));
		return;
	}

	// Vertices can have a different tangent attributed for each face they belong to.
	// We need to duplicate these vertices.
	const size_t posCount = positions.size();
	struct RemapInfo {
		uint initialIndex = 0;
		uint remapIndex = 0;
		int refOffset = -1;
	};
	// Build a list of remapping infos for each vertex (as many as there are faces it belongs to).
	std::vector<std::vector<RemapInfo>> perVertexRemaps(posCount);
	for(uint iid = 0; iid < indexCount; ++iid){
		const uint vid = indices[iid];
		perVertexRemaps[vid].emplace_back();
		perVertexRemaps[vid].back().initialIndex = iid;
	}
	// Count how many collisions we have, and determine if instances of each vertex share the same tangent.
	uint collisions = 0;

	std::vector<bool> collidedVertices(posCount, false);
	for(uint vid = 0; vid < posCount; ++vid){
		auto & remaps = perVertexRemaps[vid];
		const uint remapCount = ( uint )remaps.size();
		if(remapCount == 0){
			continue;
		}
		// FIrst instance always map to itself.
		remaps[0].remapIndex = remaps[0].initialIndex;

		// For other instances, map them to the earliest instance with the same tangent value.
		for(uint rid = 1; rid < remapCount; ++rid){
			bool found = false;
			const glm::vec4 & newTangent = meshWrapper.tangents[remaps[rid].initialIndex];

			for(uint orid = rid - 1; orid < rid; ++orid){
				// Same tangent detected on an earlier instance.
				if(newTangent == meshWrapper.tangents[remaps[orid].initialIndex]){
					found = true;
					remaps[rid].remapIndex = remaps[orid].initialIndex;
					break;
				}
			}
			// If not found, insert the instance at the end of the position list.
			if(!found){
				collidedVertices[vid] = true;
				remaps[rid].remapIndex = remaps[rid].initialIndex;
				remaps[rid].refOffset = collisions;
				++collisions;
			}
		}
	}

	// Resize all mesh storages.
	const size_t newPosCount = posCount + collisions;
	positions.resize(newPosCount);
	normals.resize(newPosCount);
	texcoords.resize(newPosCount);
	tangents.resize(newPosCount);
	bitangents.resize(newPosCount);
	const bool hasColor = !colors.empty();
	if(hasColor){
		colors.resize(newPosCount);
	}

	// Compute tangents and bitangents, add new attribute copies and update faces with remapped indices.
	auto storeTangent = [&meshWrapper, this](uint tid, uint vid){
		const glm::vec4& tgt = meshWrapper.tangents[tid];
		tangents[vid] = glm::vec3(tgt);
		// Bitangent is recomputed from tangent and sign.
		const float delta = glm::length(glm::cross(normals[vid], tangents[vid]));
		// Fix for degenerate case where all UVs are equal.
		if(delta < 0.01f){
			tangents[vid] = std::abs(normals[vid].z) > 0.01f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
		}

		// Flip the frame.
		bitangents[vid] = -tgt.w * glm::cross(normals[vid], tangents[vid]);
		// Re-normalize.
		tangents[vid] = glm::normalize(tangents[vid]);
		bitangents[vid] = glm::normalize(bitangents[vid]);
	};

	for(uint vid = 0; vid < posCount; ++vid){
		const auto & remaps = perVertexRemaps[vid];
		if(remaps.empty()){
			continue;
		}
		// Initial instance is copied in place.
		storeTangent(remaps[0].initialIndex, vid);
		// Skip if no collisions detected.
		if(!collidedVertices[vid]){
			continue;
		}
		// Remapping.
		const uint remapCount = ( uint )remaps.size();
		for(uint i = 1; i < remapCount; ++i){
			// If no offset has been stored, we just have to update the vertex index in the corresponding face
			// to point to an earlier instance with the same tangent.
			if(remaps[i].refOffset < 0){
				indices[remaps[i].initialIndex] = indices[remaps[i].remapIndex];
				continue;
			}
			// Append to the end of the positions/...
			const uint finalIndex = ( uint )(posCount + remaps[i].refOffset);
			indices[remaps[i].initialIndex] = finalIndex;
			// Copy other attributes as-is.
			positions[finalIndex] = positions[vid];
			normals[finalIndex] = normals[vid];
			texcoords[finalIndex] = texcoords[vid];
			if(hasColor){
				colors[finalIndex] = colors[vid];
			}
			// Store tangent and compute bitangent from normal.
			storeTangent(remaps[i].initialIndex, finalIndex);
		}
	}
	Log::verbose("Tangents: Treated %u for %s.", collisions, name().c_str());

	updateMetrics();
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

