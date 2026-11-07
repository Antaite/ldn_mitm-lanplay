/* JPEG-only build of stb_image (public domain / MIT), for game icons. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_FAILURE_STRINGS
#include "../../third_party/stb/stb_image.h"
