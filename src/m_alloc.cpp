#include "i_system.h"
#include "m_alloc.h"

void *Malloc (size_t size)
{
	void *zone = malloc (size);

	if (!zone)
		I_FatalError ("Could not malloc %u bytes", size);

	return zone;
}

void *Calloc (size_t num, size_t size)
{
	void *zone = calloc (num, size);

	if (!zone)
		I_FatalError ("Could not calloc %u bytes", num * size);

	return zone;
}

void *Realloc (void *memblock, size_t size)
{
	void *zone = realloc (memblock, size);

	if (!zone)
		I_FatalError ("Could not realloc %u bytes", size);

	return zone;
}