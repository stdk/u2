#ifndef EXPORT_H
#define EXPORT_H

#ifdef WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((__visibility__("default")))
#endif

#endif //EXPORT_H