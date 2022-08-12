#pragma once

namespace Log {

void verbose(const char* format, ...);

void info(const char* format, ...);

void warning(const char* format, ...);

void error(const char* format, ...);

bool check(bool value, const char* format, ...);

}

