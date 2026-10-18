#include "steam.hpp"

#include "log.hpp"
#include "globals.hpp"

#include <dlfcn.h>
#include <cstdint>


namespace Steam
{
	Plat_Alloc_t Plat_Alloc;
	Plat_Free_t Plat_Free;
	Plat_Realloc_t Plat_Realloc;
}

// ─── steamclient.so allocator resolution ────────────────────────────────────
//
// Problem: The Steam process loads TWO independent copies of libtier0_s.so —
//   Mapping A in the main linker namespace (used by SLSsteam.so)
//   Mapping B in steamclient.so's dlmopen namespace
// Each copy has its own g_pMemAllocSteam instance with a separate small-block
// pool.  Before this fix, Steam::init() called dlopen("libtier0_s.so") which
// resolved to Mapping A.  When appendDepotInfo() reallocated a CUtlVector
// buffer via Plat_Realloc (Mapping A), the caller in steamclient.so later
// freed it via g_pMemAllocSteam->Free() from Mapping B → heap corruption.
//
// Fix: Read steamclient.so's GOT slot for g_pMemAllocSteam (R_386_GLOB_DAT at
// file offset 0x02f0ee38).  At runtime this slot holds the address of the
// g_pMemAllocSteam *variable* inside Mapping B's libtier0_s.so.  Dereference
// it to obtain the IMemAlloc* and call its vtable methods directly.
//
// IMemAlloc vtable layout (32-bit, from disassembly of libtier0_s.so):
//   vtable[2] = Alloc(size)          → call *0x08(%edx)
//   vtable[3] = Realloc(ptr, size)   → call *0x0c(%edx)
//   vtable[4] = Free(ptr)            → call *0x10(%edx)
// ────────────────────────────────────────────────────────────────────────────

#include <elf.h>
#include <cstring>

static constexpr uintptr_t kKnownGotOffsets[] = {
	0x02f0ee38, // ubuntu12_32 (Debian / Arch host)
	0x02f4ac18, // steamdeck_stable / SteamOS (Neptune)
};

// Dynamically locate the R_386_GLOB_DAT relocation for g_pMemAllocSteam in steamclient.so
static uintptr_t findMemAllocGotOffset(const lm_module_t& mod)
{
	if (!mod.base) return 0;

	auto ehdr = reinterpret_cast<const Elf32_Ehdr*>(mod.base);
	if (std::memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0)
	{
		LOG_WARN("findMemAllocGotOffset: bad ELF magic\n");
		return 0;
	}

	auto phdr = reinterpret_cast<const Elf32_Phdr*>(mod.base + ehdr->e_phoff);
	const Elf32_Dyn* dyn = nullptr;
	for (uint16_t i = 0; i < ehdr->e_phnum; ++i)
	{
		if (phdr[i].p_type == PT_DYNAMIC)
		{
			dyn = reinterpret_cast<const Elf32_Dyn*>(mod.base + phdr[i].p_vaddr);
			break;
		}
	}
	if (!dyn)
	{
		LOG_WARN("findMemAllocGotOffset: no PT_DYNAMIC found\n");
		return 0;
	}

	uintptr_t strtabAddr = 0;
	uintptr_t symtabAddr = 0;
	uintptr_t relAddr = 0;
	size_t relSize = 0;

	auto resolveAddr = [&](uintptr_t ptr) -> uintptr_t {
		if (ptr >= mod.base && ptr < mod.end) return ptr;
		return mod.base + ptr;
	};

	for (const Elf32_Dyn* d = dyn; d->d_tag != DT_NULL; ++d)
	{
		switch (d->d_tag)
		{
		case DT_STRTAB: strtabAddr = resolveAddr(d->d_un.d_ptr); break;
		case DT_SYMTAB: symtabAddr = resolveAddr(d->d_un.d_ptr); break;
		case DT_REL:    relAddr    = resolveAddr(d->d_un.d_ptr); break;
		case DT_RELSZ:  relSize    = d->d_un.d_val; break;
		}
	}

	LOG_INFO("findMemAllocGotOffset: strtab=0x%x, symtab=0x%x, rel=0x%x, relSize=%zu\n",
	         strtabAddr, symtabAddr, relAddr, relSize);

	if (!strtabAddr || !symtabAddr || !relAddr || !relSize) return 0;

	const char* strtab = reinterpret_cast<const char*>(strtabAddr);
	const Elf32_Sym* symtab = reinterpret_cast<const Elf32_Sym*>(symtabAddr);
	const Elf32_Rel* rel = reinterpret_cast<const Elf32_Rel*>(relAddr);
	const size_t numRel = relSize / sizeof(Elf32_Rel);

	for (size_t i = 0; i < numRel; ++i)
	{
		const uint8_t type = ELF32_R_TYPE(rel[i].r_info);
		if (type == R_386_GLOB_DAT)
		{
			const uint32_t symIdx = ELF32_R_SYM(rel[i].r_info);
			const char* symName = strtab + symtab[symIdx].st_name;
			if (std::strcmp(symName, "g_pMemAllocSteam") == 0)
			{
				LOG_INFO("findMemAllocGotOffset: found g_pMemAllocSteam at rel[%zu].r_offset=0x%x\n",
				         i, rel[i].r_offset);
				return rel[i].r_offset;
			}
		}
	}

	LOG_WARN("findMemAllocGotOffset: g_pMemAllocSteam not found in %zu rels\n", numRel);
	return 0;
}

#include <atomic>
#include <mutex>

static uintptr_t s_gotSlotAddr = 0;
static std::atomic<bool> s_allocResolved{false};
static std::mutex s_allocMutex;

// IMemAlloc* resolved from steamclient.so's namespace
static void** s_pAllocVtable = nullptr;     // vtable pointer (first field of IMemAlloc)
static void*  s_pAllocThis   = nullptr;     // the IMemAlloc* itself

static Steam::Plat_Alloc_t   s_fallbackPlat_Alloc   = nullptr;
static Steam::Plat_Free_t    s_fallbackPlat_Free    = nullptr;
static Steam::Plat_Realloc_t s_fallbackPlat_Realloc = nullptr;

static bool EnsureSteamClientAlloc()
{
	if (s_allocResolved.load(std::memory_order_acquire))
	{
		return (s_pAllocThis != nullptr && s_pAllocVtable != nullptr);
	}

	std::lock_guard<std::mutex> lock(s_allocMutex);
	if (s_allocResolved.load(std::memory_order_relaxed))
	{
		return (s_pAllocThis != nullptr && s_pAllocVtable != nullptr);
	}

	if (!s_gotSlotAddr)
	{
		return false;
	}

	auto gotSlot = reinterpret_cast<uintptr_t*>(s_gotSlotAddr);
	uintptr_t rawSlotVal = *gotSlot;
	if (!rawSlotVal)
	{
		return false;
	}

	void** ppMemAlloc = reinterpret_cast<void**>(rawSlotVal);
	void* pMemAlloc = *ppMemAlloc;
	if (!pMemAlloc)
	{
		return false;
	}

	s_pAllocThis   = pMemAlloc;
	s_pAllocVtable = *reinterpret_cast<void***>(pMemAlloc);
	s_allocResolved.store(true, std::memory_order_release);

	LOG_INFO("Steam::init: Resolved allocator lazily from steamclient.so GOT (slot=0x%x, "
	         "IMemAlloc*=%p, vtable=%p)\n", s_gotSlotAddr, pMemAlloc, (void*)s_pAllocVtable);
	return true;
}

// Wrappers that call through steamclient.so's allocator vtable
static void* SteamClientAlloc(int size)
{
	if (EnsureSteamClientAlloc())
	{
		typedef void* (__attribute__((cdecl)) *AllocFn)(void* thisPtr, int size);
		auto fn = reinterpret_cast<AllocFn>(s_pAllocVtable[2]);
		return fn(s_pAllocThis, size);
	}
	return s_fallbackPlat_Alloc ? s_fallbackPlat_Alloc(size) : nullptr;
}

static void* SteamClientRealloc(void* ptr, int size)
{
	if (EnsureSteamClientAlloc())
	{
		typedef void* (__attribute__((cdecl)) *ReallocFn)(void* thisPtr, void* ptr, int size);
		auto fn = reinterpret_cast<ReallocFn>(s_pAllocVtable[3]);
		return fn(s_pAllocThis, ptr, size);
	}
	return s_fallbackPlat_Realloc ? s_fallbackPlat_Realloc(ptr, size) : nullptr;
}

static void SteamClientFree(void* ptr)
{
	if (EnsureSteamClientAlloc())
	{
		typedef void (__attribute__((cdecl)) *FreeFn)(void* thisPtr, void* ptr, int pFileName);
		auto fn = reinterpret_cast<FreeFn>(s_pAllocVtable[4]);
		fn(s_pAllocThis, ptr, 0);
		return;
	}
	if (s_fallbackPlat_Free)
	{
		s_fallbackPlat_Free(ptr);
	}
}

bool Steam::init()
{
	// ── 1. Resolve fallback dlopen path first ────────────────────────────
	void* tier0 = dlopen("libtier0_s.so", RTLD_NOW);
	if (tier0)
	{
		s_fallbackPlat_Alloc   = reinterpret_cast<Plat_Alloc_t>(dlsym(tier0, "Plat_Alloc"));
		s_fallbackPlat_Free    = reinterpret_cast<Plat_Free_t>(dlsym(tier0, "Plat_Free"));
		s_fallbackPlat_Realloc = reinterpret_cast<Plat_Realloc_t>(dlsym(tier0, "Plat_Realloc"));
	}

	// ── 2. Identify GOT slot in steamclient.so ──────────────────────────
	if (g_modSteamClient.base)
	{
		uintptr_t gotOffset = findMemAllocGotOffset(g_modSteamClient);
		std::vector<uintptr_t> candidateOffsets;
		if (gotOffset != 0) candidateOffsets.push_back(gotOffset);
		for (uintptr_t known : kKnownGotOffsets)
		{
			if (known != gotOffset) candidateOffsets.push_back(known);
		}

		for (uintptr_t off : candidateOffsets)
		{
			uintptr_t slotAddr = (off >= g_modSteamClient.base && off < g_modSteamClient.end)
				? off
				: reinterpret_cast<uintptr_t>(g_modSteamClient.base) + off;

			if (slotAddr + sizeof(void*) <= reinterpret_cast<uintptr_t>(g_modSteamClient.end))
			{
				s_gotSlotAddr = slotAddr;
				LOG_INFO("Steam::init: Registered g_pMemAllocSteam GOT slot at 0x%x (offset=0x%x)\n",
				         slotAddr, off);
				EnsureSteamClientAlloc();
				break;
			}
		}
	}

	// ── 3. Expose the wrappers as Plat_* functions ──────────────────────
	Plat_Alloc   = SteamClientAlloc;
	Plat_Free    = SteamClientFree;
	Plat_Realloc = SteamClientRealloc;

	if (!s_fallbackPlat_Alloc && !s_gotSlotAddr)
	{
		LOG_ERROR("Didn't find any Steam platform allocators!\n");
		return false;
	}

	return true;
}
