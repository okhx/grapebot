#pragma once



#include <Geode/platform/cplatform.h>

#ifdef GEODE_IS_IOS
#include <OpenGLES/ES3/gl.h>
#elif defined(GEODE_IS_MACOS)



#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <OpenGL/gl3.h>
#endif
