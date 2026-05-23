#ifndef PACKED_H
#define PACKED_H

#ifdef _MSC_VER
#define PACKED_STRUCT __pragma(pack(push, 1)) struct
#define END_PACKED __pragma(pack(pop))
#else
#define PACKED_STRUCT struct __attribute__((packed))
#define END_PACKED
#endif

#endif
