# SPDX-FileCopyrightText: 2024-2026 LATS Project Authors
#
# SPDX-License-Identifier: GPL-2.0-only

capStone_data = configuration_data()
capStone_data.set('CAPSTONE_USE_SYS_DYN_MEM', '1')

if 'CONFIG_LATX_DEBUG' in config_host
    capStone_data.set('CONFIG_LATX_DEBUG', '1')
endif
if ('CONFIG_CAPSTONE_DIET' in config_host)
    capStone_data.set('CAPSTONE_DIET', '1')
endif
if ('CONFIG_LATX_CAPSTONE_OP_ACCESS' in config_host)
    capStone_data.set('LATX_CAPSTONE_OP_ACCESS', '1')
endif

capStone_files = files(
  'capStone/cs.c',
  'capStone/MCInstrDesc.c',
  'capStone/MCRegisterInfo.c',
  'capStone/SStream.c',
  'capStone/utils.c'
)

capStone_data.set('CAPSTONE_HAS_X86', 1)
capStone_files += files(
  'capStone/arch/X86/X86Disassembler.c',
  'capStone/arch/X86/X86DisassemblerDecoder.c',
  'capStone/arch/X86/X86ATTInstPrinter.c',
  'capStone/arch/X86/X86IntelInstPrinter.c',
  'capStone/arch/X86/X86Mapping.c',
  'capStone/arch/X86/X86Module.c'
)

configure_file(output: 'capStone-defs.h', configuration: capStone_data)

capStone_cargs = [
  # FIXME: There does not seem to be a way to completely replace the c_args
  # that come from add_project_arguments() -- we can only add to them.
  # So: disable all warnings with a big hammer.
  '-Wno-error', '-w',

  # Include all configuration defines via a header file, which will wind up
  # as a dependency on the object file, and thus changes here will result
  # in a rebuild.
  '-include', 'capStone-defs.h'
]

libcapStone = static_library('capStone',
                             build_by_default: false,
                             sources: capStone_files,
                             c_args: capStone_cargs,
                             include_directories: 'capStone/include')
capStone = declare_dependency(link_with: libcapStone,
                              include_directories: ['capStone/include', 'capStone/include/capstone'])


if 'CONFIG_LATX_DEBUG' in config_host or 'CONFIG_LATX_PROFILER' in config_host
  i386_ss.add(files('perfmap.c'))
endif

i386_ss.add(files(
  'error.c',
  'mem.c',
  'user-stub.c',
  'tb-record.c',
  'sys-tb-trace.c',
  'latx-config.c',
  'latx-hack.c',
  'latx-options.c',
  'latx-tb-exec-trace.c',
  'latx-misc.c',
  'aot-verify-mode.c'
))

i386_ss.add(capStone)

subdir('ir1')
subdir('ir2')
subdir('optimization')
subdir('translator')
subdir('sbt')
subdir('offline')
