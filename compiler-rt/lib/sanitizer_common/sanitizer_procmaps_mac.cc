//===-- sanitizer_procmaps_mac.cc -----------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Information about the process mappings (Mac-specific parts).
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"
#if SANITIZER_MAC
#include "sanitizer_common.h"
#include "sanitizer_placement_new.h"
#include "sanitizer_procmaps.h"

#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach/mach.h>

// These are not available in older macOS SDKs.
#ifndef CPU_SUBTYPE_X86_64_H
#define CPU_SUBTYPE_X86_64_H  ((cpu_subtype_t)8)   /* Haswell */
#endif
#ifndef CPU_SUBTYPE_ARM_V7S
#define CPU_SUBTYPE_ARM_V7S   ((cpu_subtype_t)11)  /* Swift */
#endif
#ifndef CPU_SUBTYPE_ARM_V7K
#define CPU_SUBTYPE_ARM_V7K   ((cpu_subtype_t)12)
#endif
#ifndef CPU_TYPE_ARM64
#define CPU_TYPE_ARM64        (CPU_TYPE_ARM | CPU_ARCH_ABI64)
#endif

namespace __sanitizer {
template <typename Section>
void MemoryMappedSegment::NextSectionLoad(LoadedModule *module) {
  const Section *sc = (const Section *)current_load_cmd_addr_;
  current_load_cmd_addr_ += sizeof(Section);

  uptr sec_start = sc->addr + base_virt_addr_;
  uptr sec_end = sec_start + sc->size;
  module->addAddressRange(sec_start, sec_end, IsExecutable(), IsWritable(),
                          sc->sectname);
}

void MemoryMappedSegment::AddAddressRanges(LoadedModule *module) {
  if (!nsects_) {
    module->addAddressRange(start, end, IsExecutable(), IsWritable(), name);
    return;
  }

  do {
    if (lc_type_ == LC_SEGMENT) {
      NextSectionLoad<struct section>(module);
#ifdef MH_MAGIC_64
    } else if (lc_type_ == LC_SEGMENT_64) {
      NextSectionLoad<struct section_64>(module);
#endif
    }
  } while (--nsects_);
}

MemoryMappingLayout::MemoryMappingLayout(bool cache_enabled) {
  Reset();
}

MemoryMappingLayout::~MemoryMappingLayout() {
}

// More information about Mach-O headers can be found in mach-o/loader.h
// Each Mach-O image has a header (mach_header or mach_header_64) starting with
// a magic number, and a list of linker load commands directly following the
// header.
// A load command is at least two 32-bit words: the command type and the
// command size in bytes. We're interested only in segment load commands
// (LC_SEGMENT and LC_SEGMENT_64), which tell that a part of the file is mapped
// into the task's address space.
// The |vmaddr|, |vmsize| and |fileoff| fields of segment_command or
// segment_command_64 correspond to the memory address, memory size and the
// file offset of the current memory segment.
// Because these fields are taken from the images as is, one needs to add
// _dyld_get_image_vmaddr_slide() to get the actual addresses at runtime.

void MemoryMappingLayout::Reset() {
  // Count down from the top.
  // TODO(glider): as per man 3 dyld, iterating over the headers with
  // _dyld_image_count is thread-unsafe. We need to register callbacks for
  // adding and removing images which will invalidate the MemoryMappingLayout
  // state.
  current_image_ = _dyld_image_count();
  current_load_cmd_count_ = -1;
  current_load_cmd_addr_ = 0;
  current_magic_ = 0;
  current_filetype_ = 0;
  current_arch_ = kModuleArchUnknown;
  internal_memset(current_uuid_, 0, kModuleUUIDSize);
}

// The dyld load address should be unchanged throughout process execution,
// and it is expensive to compute once many libraries have been loaded,
// so cache it here and do not reset.
static mach_header *dyld_hdr = 0;
static const char kDyldPath[] = "/usr/lib/dyld";
static const int kDyldImageIdx = -1;

// static
void MemoryMappingLayout::CacheMemoryMappings() {
  // No-op on Mac for now.
}

void MemoryMappingLayout::LoadFromCache() {
  // No-op on Mac for now.
}

// _dyld_get_image_header() and related APIs don't report dyld itself.
// We work around this by manually recursing through the memory map
// until we hit a Mach header matching dyld instead. These recurse
// calls are expensive, but the first memory map generation occurs
// early in the process, when dyld is one of the only images loaded,
// so it will be hit after only a few iterations.
static mach_header *get_dyld_image_header() {
  mach_port_name_t port;
  if (task_for_pid(mach_task_self(), internal_getpid(), &port) !=
      KERN_SUCCESS) {
    return nullptr;
  }

  unsigned depth = 1;
  vm_size_t size = 0;
  vm_address_t address = 0;
  kern_return_t err = KERN_SUCCESS;
  mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;

  while (true) {
    struct vm_region_submap_info_64 info;
    err = vm_region_recurse_64(port, &address, &size, &depth,
                               (vm_region_info_t)&info, &count);
    if (err != KERN_SUCCESS) return nullptr;

    if (size >= sizeof(mach_header) && info.protection & kProtectionRead) {
      mach_header *hdr = (mach_header *)address;
      if ((hdr->magic == MH_MAGIC || hdr->magic == MH_MAGIC_64) &&
          hdr->filetype == MH_DYLINKER) {
        return hdr;
      }
    }
    address += size;
  }
}

const mach_header *get_dyld_hdr() {
  if (!dyld_hdr) dyld_hdr = get_dyld_image_header();

  return dyld_hdr;
}

// Next and NextSegmentLoad were inspired by base/sysinfo.cc in
// Google Perftools, https://github.com/gperftools/gperftools.

// NextSegmentLoad scans the current image for the next segment load command
// and returns the start and end addresses and file offset of the corresponding
// segment.
// Note that the segment addresses are not necessarily sorted.
template <u32 kLCSegment, typename SegmentCommand>
bool MemoryMappingLayout::NextSegmentLoad(MemoryMappedSegment *segment) {
  const char *lc = current_load_cmd_addr_;
  current_load_cmd_addr_ += ((const load_command *)lc)->cmdsize;
  if (((const load_command *)lc)->cmd == kLCSegment) {
    const SegmentCommand* sc = (const SegmentCommand *)lc;
    segment->current_load_cmd_addr_ = (char *)lc + sizeof(SegmentCommand);
    segment->lc_type_ = kLCSegment;
    segment->nsects_ = sc->nsects;

    if (current_image_ == kDyldImageIdx) {
      segment->base_virt_addr_ = (uptr)get_dyld_hdr();
      // vmaddr is masked with 0xfffff because on macOS versions < 10.12,
      // it contains an absolute address rather than an offset for dyld.
      // To make matters even more complicated, this absolute address
      // isn't actually the absolute segment address, but the offset portion
      // of the address is accurate when combined with the dyld base address,
      // and the mask will give just this offset.
      segment->start = (sc->vmaddr & 0xfffff) + segment->base_virt_addr_;
    } else {
      segment->base_virt_addr_ =
          (uptr)_dyld_get_image_vmaddr_slide(current_image_);
      segment->start = sc->vmaddr + segment->base_virt_addr_;
    }
    segment->end = segment->start + sc->vmsize;

    // Return the initial protection.
    segment->protection = sc->initprot;
    segment->offset =
        (current_filetype_ == /*MH_EXECUTE*/ 0x2) ? sc->vmaddr : sc->fileoff;
    if (segment->filename) {
      const char *src = (current_image_ == kDyldImageIdx)
                            ? kDyldPath
                            : _dyld_get_image_name(current_image_);
      internal_strncpy(segment->filename, src, segment->filename_size);
    }
    internal_strncpy(segment->name, sc->segname, ARRAY_SIZE(segment->name));
    segment->arch = current_arch_;
    internal_memcpy(segment->uuid, current_uuid_, kModuleUUIDSize);
    return true;
  }
  return false;
}

ModuleArch ModuleArchFromCpuType(cpu_type_t cputype, cpu_subtype_t cpusubtype) {
  cpusubtype = cpusubtype & ~CPU_SUBTYPE_MASK;
  switch (cputype) {
    case CPU_TYPE_I386:
      return kModuleArchI386;
    case CPU_TYPE_X86_64:
      if (cpusubtype == CPU_SUBTYPE_X86_64_ALL) return kModuleArchX86_64;
      if (cpusubtype == CPU_SUBTYPE_X86_64_H) return kModuleArchX86_64H;
      CHECK(0 && "Invalid subtype of x86_64");
      return kModuleArchUnknown;
    case CPU_TYPE_ARM:
      if (cpusubtype == CPU_SUBTYPE_ARM_V6) return kModuleArchARMV6;
      if (cpusubtype == CPU_SUBTYPE_ARM_V7) return kModuleArchARMV7;
      if (cpusubtype == CPU_SUBTYPE_ARM_V7S) return kModuleArchARMV7S;
      if (cpusubtype == CPU_SUBTYPE_ARM_V7K) return kModuleArchARMV7K;
      CHECK(0 && "Invalid subtype of ARM");
      return kModuleArchUnknown;
    case CPU_TYPE_ARM64:
      return kModuleArchARM64;
    default:
      CHECK(0 && "Invalid CPU type");
      return kModuleArchUnknown;
  }
}

static const load_command *NextCommand(const load_command *lc) {
  return (const load_command *)((char *)lc + lc->cmdsize);
}

static void FindUUID(const load_command *first_lc, u8 *uuid_output) {
  for (const load_command *lc = first_lc; lc->cmd != 0; lc = NextCommand(lc)) {
    if (lc->cmd != LC_UUID) continue;

    const uuid_command *uuid_lc = (const uuid_command *)lc;
    const uint8_t *uuid = &uuid_lc->uuid[0];
    internal_memcpy(uuid_output, uuid, kModuleUUIDSize);
    return;
  }
}

static bool IsModuleInstrumented(const load_command *first_lc) {
  for (const load_command *lc = first_lc; lc->cmd != 0; lc = NextCommand(lc)) {
    if (lc->cmd != LC_LOAD_DYLIB) continue;

    const dylib_command *dylib_lc = (const dylib_command *)lc;
    uint32_t dylib_name_offset = dylib_lc->dylib.name.offset;
    const char *dylib_name = ((const char *)dylib_lc) + dylib_name_offset;
    dylib_name = StripModuleName(dylib_name);
    if (dylib_name != 0 && (internal_strstr(dylib_name, "libclang_rt."))) {
      return true;
    }
  }
  return false;
}

bool MemoryMappingLayout::Next(MemoryMappedSegment *segment) {
  for (; current_image_ >= kDyldImageIdx; current_image_--) {
    const mach_header *hdr = (current_image_ == kDyldImageIdx)
                                 ? get_dyld_hdr()
                                 : _dyld_get_image_header(current_image_);
    if (!hdr) continue;
    if (current_load_cmd_count_ < 0) {
      // Set up for this image;
      current_load_cmd_count_ = hdr->ncmds;
      current_magic_ = hdr->magic;
      current_filetype_ = hdr->filetype;
      current_arch_ = ModuleArchFromCpuType(hdr->cputype, hdr->cpusubtype);
      switch (current_magic_) {
#ifdef MH_MAGIC_64
        case MH_MAGIC_64: {
          current_load_cmd_addr_ = (char*)hdr + sizeof(mach_header_64);
          break;
        }
#endif
        case MH_MAGIC: {
          current_load_cmd_addr_ = (char*)hdr + sizeof(mach_header);
          break;
        }
        default: {
          continue;
        }
      }
      FindUUID((const load_command *)current_load_cmd_addr_, &current_uuid_[0]);
      current_instrumented_ =
          IsModuleInstrumented((const load_command *)current_load_cmd_addr_);
    }

    for (; current_load_cmd_count_ >= 0; current_load_cmd_count_--) {
      switch (current_magic_) {
        // current_magic_ may be only one of MH_MAGIC, MH_MAGIC_64.
#ifdef MH_MAGIC_64
        case MH_MAGIC_64: {
          if (NextSegmentLoad<LC_SEGMENT_64, struct segment_command_64>(
                  segment))
            return true;
          break;
        }
#endif
        case MH_MAGIC: {
          if (NextSegmentLoad<LC_SEGMENT, struct segment_command>(segment))
            return true;
          break;
        }
      }
    }
    // If we get here, no more load_cmd's in this image talk about
    // segments.  Go on to the next image.
  }
  return false;
}

void MemoryMappingLayout::DumpListOfModules(
    InternalMmapVector<LoadedModule> *modules) {
  Reset();
  InternalScopedString module_name(kMaxPathLength);
  MemoryMappedSegment segment(module_name.data(), kMaxPathLength);
  while (Next(&segment)) {
    if (segment.filename[0] == '\0') continue;
    LoadedModule *cur_module = nullptr;
    if (!modules->empty() &&
        0 == internal_strcmp(segment.filename, modules->back().full_name())) {
      cur_module = &modules->back();
    } else {
      modules->push_back(LoadedModule());
      cur_module = &modules->back();
      cur_module->set(segment.filename, segment.start, segment.arch,
                      segment.uuid, current_instrumented_);
    }
    segment.AddAddressRanges(cur_module);
  }
}

}  // namespace __sanitizer

#endif  // SANITIZER_MAC
