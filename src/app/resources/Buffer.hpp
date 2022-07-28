#pragma once
#include "graphics/GPUTypes.hpp"
#include "core/Common.hpp"

class GPUBuffer;

/** \brief General purpose GPU buffer, with different use types determining its memory type, visibility  and access pattern.
 \ingroup Resources
 */
class Buffer {
public:

	/** Constructor.
	\param sizeInBytes the size of the buffer in bytes
	\param atype the use type of the buffer (uniform, index, vertex, storage...)
	 */
	Buffer(size_t sizeInBytes, BufferType atype);

	/** Upload data to the buffer. You have to take care of synchronization when
	 updating a subregion of the buffer that is currently in use.
	 \param sizeInBytes the size of the data to upload, in bytes
	 \param data the data to upload
	 \param offset offset in the buffer
	 */
	void upload(size_t sizeInBytes, unsigned char * data, size_t offset);

	/** Upload objects data from a vector to the buffer. You have to take care of synchronization when
	 updating a subregion of the buffer that is currently in use.
	 \param data the objects vector to upload
	 \param offset offset in the buffer
	 */
	template<typename T>
	void upload(const std::vector<T>& data, size_t offset = 0);

	/** Download data from the buffer.
	 \param sizeInBytes the size of the data to download, in bytes
	 \param data the storage to download to
	 \param offset offset in the buffer
	 */
	void download(size_t sizeInBytes, unsigned char * data, size_t offset);

	/** Cleanup all data.
	 */
	void clean();

	/** Copy assignment operator (disabled).
	 \return a reference to the object assigned to
	 */
	Buffer & operator=(const Buffer &) = delete;

	/** Copy constructor (disabled). */
	Buffer(const Buffer &) = delete;

	/** Move assignment operator .
	 \return a reference to the object assigned to
	 */
	Buffer & operator=(Buffer &&) = delete;

	/** Move constructor. */
	Buffer(Buffer &&) = delete;

	/** Destructor. */
	virtual ~Buffer();

	const BufferType type; ///< Buffer type.

protected:

	/** Internal constructor, exposed for subclasses that override the size.
	\param atype the use type of the buffer (uniform, index, vertex, storage...)
	*/
	Buffer(BufferType atype);

public:

	size_t size; ///< Buffer size in bytes.
	std::unique_ptr<GPUBuffer> gpu; ///< The GPU data (optional).

};

template<typename T>
void Buffer::upload(const std::vector<T>& data, size_t offset){
	upload(sizeof(T) * data.size(), (unsigned char*)data.data(), offset);
}
