#ifndef __RAMBRAIN_EXPORT_H
#define __RAMBRAIN_EXPORT_H

#ifdef USERAMBRAINLIBRARY
#ifdef  RAMBRAINLIBRARY_EXPORTS 
#define RAMBRAINAPI __declspec(dllexport)
#else
#define RAMBRAINAPI __declspec(dllimport)
#endif
#else
#define RAMBRAINAPI
#endif

#endif