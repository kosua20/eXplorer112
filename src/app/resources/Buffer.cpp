#include "resources/Buffer.hpp"
#include "graphics/GPUObjects.hpp"
#include "graphics/GPU.hpp"
#include "graphics/GPUInternal.hpp"

#include <cstring>



Buffer::Buffer(size_t sizeInBytes, BufferType atype) : type(atype), size(sizeInBytes) {
	GPU::setupBuffer(*this);
}

Buffer::Buffer(BufferType atype) : type(atype), size(0u) {
	// Don't set it up immediately.
}

void Buffer::clean() {
	if(gpu) {
		gpu->clean();
		gpu.reset();
	}
	gpu = nullptr;
}

Buffer::~Buffer() {
	clean();
}

void Buffer::upload(size_t sizeInBytes, unsigned char * data, size_t offset){
	// If the GPU object is not allocated, do it first.
	if(!gpu){
		GPU::setupBuffer(*this);
	}
	// Then upload the data in one block.
	GPU::uploadBuffer(*this, sizeInBytes, data, offset);
}

void Buffer::download(size_t sizeInBytes, unsigned char * data, size_t offset){
	if(!gpu){
		Log::warning( "No GPU data to download for the buffer.");
		return;
	}
	GPU::downloadBufferSync(*this, sizeInBytes, data, offset);
}
