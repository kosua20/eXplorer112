#pragma once

#include "core/System.hpp"

bool convertDDStoPNG(const fs::path& ddsPath, const fs::path& pngPath);

bool convertTGAtoPNG(const fs::path& tgaPath, const fs::path& pngPath);

bool writeDefaultTexture(const fs::path& pngPath);
