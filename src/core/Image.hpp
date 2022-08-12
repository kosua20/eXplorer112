#pragma once
#include "core/Common.hpp"
#include "core/System.hpp"


class Image {

public:

	enum class Compression {
		NONE = 0,
		BC1,
		BC2,
		BC3
	};

	/** Default constructor. */
	Image() = default;

	/**
	 Constructor that allocates an empty image with the given dimensions.
	 \param awidth the width of the image
	 \param aheight the height of the image
	 \param acomponents the number of components of the image
	 \param value the default value to use
	 */
	Image(unsigned int awidth, unsigned int aheight, unsigned int acomponents, char value = 0);

	/** Load an image from disk. Will contain the image raw data as [0,255] chars.
	 \param path the path to the image
	 \return a success/error flag
	 */
	bool load(const fs::path & path);

	bool uncompress();

	/** Save an image to disk.
	 \param path the path to the image
	 \return a success/error flag
	 */
	bool save(const fs::path & path) const;
	
	static void generateDefaultImage(Image & image);

	/** Copy assignment operator (disabled).
	 \return a reference to the object assigned to
	 */
	Image & operator=(const Image &) = delete;

	/** Copy constructor (disabled). */
	Image(const Image &) = delete;

	/** Move assignment operator .
	 \return a reference to the object assigned to
	 */
	Image & operator=(Image &&) = default;

	/** Move constructor. */
	Image(Image &&) = default;

	unsigned int width = 0;		 ///< The width of the image
	unsigned int height = 0;	 ///< The height of the image
	unsigned int components = 0; ///< Number of components/channels
	std::vector<unsigned char> pixels;	 ///< The pixels values of the image
	Compression compressedFormat = Compression::NONE;

};

