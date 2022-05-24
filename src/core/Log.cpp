#include "core/Log.hpp"

#include <cstdio>
#include <cstdarg>
#include <cassert>

void Log::info(const char* format, ...){

	fprintf(stdout, "Info   : ");

	va_list argptr;
	va_start(argptr, format);
	vfprintf(stdout, format, argptr);
	va_end(argptr);

	fprintf(stdout, "\n");
}

void Log::warning(const char* format, ...){

	fprintf(stdout, "Warning: ");

	va_list argptr;
	va_start(argptr, format);
	vfprintf(stdout, format, argptr);
	va_end(argptr);

	fprintf(stdout, "\n");
}

void Log::error(const char* format, ...){

	fprintf(stderr, "Error  : ");

	va_list argptr;
	va_start(argptr, format);
	vfprintf(stderr, format, argptr);
	va_end(argptr);

	fprintf(stderr, "\n");
}

void Log::check(bool value, const char* format, ...){
	if(!value){

		fprintf(stderr, "[dffparser] Check failed  : ");

		va_list argptr;
		va_start(argptr, format);
		vfprintf(stderr, format, argptr);
		va_end(argptr);

		fprintf(stderr, "\n");

	}
	assert(value);
}
