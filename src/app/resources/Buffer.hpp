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
	Buffer(size_t sizeInBytes, BufferType atype, const std::string& name);

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

	const std::string& name() const { return _name; }
	
protected:

	/** Internal constructor, exposed for subclasses that override the size.
	\param atype the use type of the buffer (uniform, index, vertex, storage...)
	*/
	Buffer(BufferType atype, const std::string & name);

	size_t size; ///< Buffer size in bytes.
	
public:

	size_t sizeInBytes() const { return size; }

	std::unique_ptr<GPUBuffer> gpu; ///< The GPU data (optional).

private:

	std::string _name;
};

template<typename T>
void Buffer::upload(const std::vector<T>& data, size_t offset){
	upload(sizeof(T) * data.size(), (unsigned char*)data.data(), offset);
}

/**
\brief Represents a buffer containing uniform data, stored on the CPU and GPU.
Depending on the update frequency of the CPU data, the buffer will maintain one or multiple copies of the data on the GPU.
\ingroup Resources
*/
template<typename T>
class StructuredBuffer : public Buffer {
public:

   /** Constructor.
	\param count the number of elements
	*/
	StructuredBuffer(size_t count, BufferType type, const std::string& name);

   /** Accessor.
	\param i the location of the item to retrieve
	\return a reference to the item
	*/
   T & operator[](size_t i){
	   return data[i];
   }

   /** Accessor.
	\param i the location of the item to retrieve
	\return a reference to the item
	*/
   const T & operator[](size_t i) const {
	   return data[i];
   }

   /** Accessor.
	\param i the location of the item to retrieve
	\return a reference to the item
	*/
   T & at(size_t i){
	   return data[i];
   }

   /** Accessor.
	\param i the location of the item to retrieve
	\return a reference to the item
	*/
   const T & at(size_t i) const {
	   return data[i];
   }

   /** \return the CPU size of the buffer. */
   size_t size() const {
	   return data.size();
   }

   /** Send the buffer data to the GPU.
	Previously uploaded content will potentially be erased.
	*/
   void upload();

   /** Copy assignment operator (disabled).
	\return a reference to the object assigned to
	*/
   StructuredBuffer & operator=(const StructuredBuffer &) = delete;

   /** Copy constructor (disabled). */
	StructuredBuffer(const StructuredBuffer &) = delete;

   /** Move assignment operator .
	\return a reference to the object assigned to
	*/
	StructuredBuffer & operator=(StructuredBuffer &&) = default;

   /** Move constructor. */
	StructuredBuffer(StructuredBuffer &&) = default;

   /** Destructor. */
   ~StructuredBuffer() = default;

   std::vector<T> data; ///< The CPU data.

};

template <typename T>
StructuredBuffer<T>::StructuredBuffer(size_t count, BufferType type, const std::string& name) :
   Buffer(count * sizeof(T), type, name) {
   data.resize(count);
}

template <typename T>
void StructuredBuffer<T>::upload() {
   Buffer::upload(data, 0);
}



class UniformBufferBase : public Buffer {
public:

   /** Constructor.
	\param sizeInBytes size of the uniform buffer
	\param use the update frequency
	*/
   UniformBufferBase(size_t sizeInBytes, int updateFrequency, const std::string& name);

   /** Upload data. The buffer will internally copy the data (using the internal size)
	to a region of mapped GPU memory. Buffering will be handled based on the update frequency.
	\param data the data to copy
	*/
   void upload(unsigned char * data);

   /** Clean the buffer. */
   void clean();

   /** \return the current offset in bytes in the internal GPU buffer.*/
   size_t currentOffset() const { return _offset; }

   /** \return the size of one instance of the buffer */
   size_t baseSize() const { return _baseSize; }

   /** Copy assignment operator (disabled).
	\return a reference to the object assigned to
	*/
   UniformBufferBase & operator=(const UniformBufferBase &) = delete;

   /** Copy constructor (disabled). */
   UniformBufferBase(const UniformBufferBase &) = delete;

   /** Move assignment operator .
	\return a reference to the object assigned to
	*/
   UniformBufferBase & operator=(UniformBufferBase &&) = delete;

   /** Move constructor. */
   UniformBufferBase(UniformBufferBase &&) = delete;

   /** Destructor. */
   virtual ~UniformBufferBase();

private:

   const size_t _baseSize; ///< The uniform buffer size (ie the size of one instance).
   size_t _alignment = 0; ///< The alignment constraint to respect between successive instances.
   size_t _offset = 0; ///< The current offset in bytes in the array of instances.

};

/**
\brief Represents a buffer containing uniform data, stored on the CPU and GPU.
Depending on the update frequency of the CPU data, the buffer will maintain one or multiple copies of the data on the GPU.
\ingroup Resources
*/
template<typename T>
class UniformBuffer : public UniformBufferBase {
public:

   /** Constructor.
	\param count the number of elements
	\param frequency the update frequency of the buffer content
	*/
   UniformBuffer(size_t count, int updateFrequency, const std::string& name);

   /** Accessor.
	\param i the location of the item to retrieve
	\return a reference to the item
	*/
   T & operator[](size_t i){
	   return data[i];
   }

   /** Accessor.
	\param i the location of the item to retrieve
	\return a reference to the item
	*/
   const T & operator[](size_t i) const {
	   return data[i];
   }

   /** Accessor.
	\param i the location of the item to retrieve
	\return a reference to the item
	*/
   T & at(size_t i){
	   return data[i];
   }

   /** Accessor.
	\param i the location of the item to retrieve
	\return a reference to the item
	*/
   const T & at(size_t i) const {
	   return data[i];
   }

   /** \return the CPU size of the buffer. */
   size_t size() const {
	   return data.size();
   }

   /** Send the buffer data to the GPU.
	Previously uploaded content will potentially be erased.
	*/
   void upload();

   /** Copy assignment operator (disabled).
	\return a reference to the object assigned to
	*/
   UniformBuffer & operator=(const UniformBuffer &) = delete;

   /** Copy constructor (disabled). */
   UniformBuffer(const UniformBuffer &) = delete;

   /** Move assignment operator .
	\return a reference to the object assigned to
	*/
   UniformBuffer & operator=(UniformBuffer &&) = default;

   /** Move constructor. */
   UniformBuffer(UniformBuffer &&) = default;

   /** Destructor. */
   ~UniformBuffer() = default;

   std::vector<T> data; ///< The CPU data.

};

template <typename T>
UniformBuffer<T>::UniformBuffer(size_t count, int updateFrequency, const std::string& name) :
   UniformBufferBase(count * sizeof(T), updateFrequency, name) {
   data.resize(count);
}

template <typename T>
void UniformBuffer<T>::upload() {
   UniformBufferBase::upload(reinterpret_cast<unsigned char*>(data.data()));
}
