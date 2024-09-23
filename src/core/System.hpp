#pragma once

#define GHC_FILESYSTEM_FWD
#include <ghc/filesystem.hpp>

namespace fs = ghc::filesystem;

#include <pugixml/pugixml.hpp>
#include <string>
#include <thread>
#include <vector>

namespace System {

	void listAllFilesOfType(const fs::path& root, const std::string& ext, std::vector<fs::path>& paths);

	char * loadData(const fs::path& path, size_t & size);

	std::string loadString(const fs::path & path);

	void saveData(const fs::path & path, char * rawContent, size_t size);

	void saveString(const fs::path & path, const std::string & content);

	std::string getStringWithIncludes( const fs::path& filename, std::vector<fs::path>& names );

	uint64_t hash64( const void* data, size_t size );

	uint32_t hash32( const void* data, size_t size );

	/** Multi-threaded for-loop.
		 \param low lower (included) bound
		 \param high higher (excluded) bound
		 \param func the function to execute at each iteration, will receive the index of the
		 element as a unique argument. Signature: void func(size_t i)
		 \note For now only an increment by one is supported.
		 */
	template<typename ThreadFunc>
	static void forParallel(size_t low, size_t high, ThreadFunc func) {
		// Make sure the loop is increasing.
		if(high < low) {
			const size_t temp = low;
			low				  = high;
			high			  = temp;
		}
		// Prepare the threads pool.
		// Always leave one thread free.
		const size_t count = size_t(std::max(int(std::thread::hardware_concurrency())-1, 1));
		std::vector<std::thread> threads;
		threads.reserve(count);

		// Compute the span of each thread.
		const size_t span = std::max(size_t(1), (high - low) / count);
		// Helper to execute the function passed on a subset of the total interval.
		auto launchThread = [&func](size_t a, size_t b) {
			for(size_t i = a; i < b; ++i) {
				func(i);
			}
		};

		for(size_t tid = 0; tid < count; ++tid) {
			// For each thread, call the same lambda with different bounds as arguments.
			const size_t threadLow  = tid * span;
			const size_t threadHigh = tid == (count-1) ? high : ((tid + 1) * span);
			threads.emplace_back(launchThread, threadLow, threadHigh);
		}
		// Wait for all threads to finish.
		std::for_each(threads.begin(), threads.end(), [](std::thread & x) { x.join(); });
	}

}
