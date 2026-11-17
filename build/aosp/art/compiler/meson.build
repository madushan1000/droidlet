libart_compiler_includes = include_directories(['export', 'optimizing'])

libart_compiler_srcs = [
  'debug/elf_debug_writer.cc',
  'dex/inline_method_analyser.cc',
  'driver/assume_value_options.cc',
  'driver/compiler_options.cc',
  'driver/dex_compilation_unit.cc',
  'jit/jit_compiler.cc',
  'jit/jit_logger.cc',
  'jni/quick/calling_convention.cc',
  'jni/quick/jni_compiler.cc',
  'optimizing/block_builder.cc',
  'optimizing/block_namer.cc',
  'optimizing/bounds_check_elimination.cc',
  'optimizing/builder.cc',
  'optimizing/cha_guard_optimization.cc',
  'optimizing/code_generation_data.cc',
  'optimizing/code_generator.cc',
  'optimizing/code_generator_utils.cc',
  'optimizing/code_sinking.cc',
  'optimizing/constant_folding.cc',
  'optimizing/constructor_fence_redundancy_elimination.cc',
  'optimizing/control_flow_simplifier.cc',
  'optimizing/data_type.cc',
  'optimizing/dead_code_elimination.cc',
  'optimizing/environment_input_elimination.cc',
  'optimizing/escape.cc',
  'optimizing/graph.cc',
  'optimizing/graph_checker.cc',
  'optimizing/graph_visualizer.cc',
  'optimizing/gvn.cc',
  'optimizing/handle_cache.cc',
  'optimizing/induction_var_analysis.cc',
  'optimizing/induction_var_range.cc',
  'optimizing/inliner.cc',
  'optimizing/instruction_builder.cc',
  'optimizing/instruction_list.cc',
  'optimizing/instruction_simplifier.cc',
  'optimizing/intrinsic_objects.cc',
  'optimizing/intrinsics.cc',
  'optimizing/licm.cc',
  'optimizing/linear_order.cc',
  'optimizing/load_store_analysis.cc',
  'optimizing/load_store_elimination.cc',
  'optimizing/locations.cc',
  'optimizing/loop_analysis.cc',
  'optimizing/loop_information.cc',
  'optimizing/loop_optimization.cc',
  'optimizing/nodes.cc',
  'optimizing/nodes_vector.cc',
  'optimizing/optimization.cc',
  'optimizing/optimizing_compiler.cc',
  'optimizing/parallel_move_resolver.cc',
  'optimizing/prepare_for_register_allocation.cc',
  'optimizing/profiling_info_builder.cc',
  'optimizing/reference_type_info.cc',
  'optimizing/reference_type_propagation.cc',
  'optimizing/register_allocation_resolver.cc',
  'optimizing/register_allocator.cc',
  'optimizing/register_allocator_linear_scan.cc',
  'optimizing/scheduler.cc',
  'optimizing/sharpening.cc',
  'optimizing/side_effects_analysis.cc',
  'optimizing/ssa_builder.cc',
  'optimizing/ssa_liveness_analysis.cc',
  'optimizing/ssa_phi_elimination.cc',
  'optimizing/stack_map_stream.cc',
  'optimizing/superblock_cloner.cc',
  'optimizing/write_barrier_elimination.cc',
  'trampolines/trampoline_compiler.cc',
  'utils/assembler.cc',
  'utils/jni_macro_assembler.cc',
  'compiler.cc',
]

if target_machine.cpu() == 'x86_64'
  libart_compiler_srcs += [
    'jni/quick/x86_64/calling_convention_x86_64.cc',
    'optimizing/intrinsics_x86_64.cc',
    'optimizing/instruction_simplifier_x86_64.cc',
    'optimizing/code_generator_x86_64.cc',
    'optimizing/code_generator_vector_x86_64.cc',
    'utils/x86_64/assembler_x86_64.cc',
    'utils/x86_64/jni_macro_assembler_x86_64.cc',
    'utils/x86_64/managed_register_x86_64.cc',
  ]
  #x86 stuff
  libart_compiler_srcs += [
    'jni/quick/x86/calling_convention_x86.cc',
    'optimizing/code_generator_x86.cc',
    'optimizing/code_generator_vector_x86.cc',
    'optimizing/intrinsics_x86.cc',
    'optimizing/instruction_simplifier_x86_shared.cc',
    'optimizing/instruction_simplifier_x86.cc',
    'optimizing/pc_relative_fixups_x86.cc',
    'optimizing/x86_memory_gen.cc',
    'utils/x86/assembler_x86.cc',
    'utils/x86/jni_macro_assembler_x86.cc',
    'utils/x86/managed_register_x86.cc',
  ]
endif

art_compiler_operator_gen_srcs = [
  'linker/linker_patch.h',
  'optimizing/graph.h',
  'optimizing/locations.h',
  'optimizing/nodes.h',
  'optimizing/optimizing_compiler_stats.h',
  'optimizing/physical_register_type.h',

  #'utils/arm/constants_arm.h',
]
art_compiler_operator_gen_outs = []

foreach name : art_compiler_operator_gen_srcs
  art_compiler_operator_gen_outs += operator_srcs_gen.process(
    name,
    extra_args: 'art/compiler',
  )
endforeach

libart_compiler = static_library(
  'libart-compiler',
  art_compiler_operator_gen_outs + libart_compiler_srcs,
  include_directories: [libart_runtime_includes, libart_compiler_includes],
  cpp_args: art_default_cflags + ['-std=gnu++23'],
  dependencies: [
    libartbase_dep,
    libbase_dep,
    libdexfile_dep,
    libnativehelper_dep,
    libelffile_dep,
    libartpalette_dep,
    libart_cmdlineparser_dep,
    libprofile_dep,
    libdlmalloc_dep,
    libart_disassembler_dep,
    com_android_art_flags_cpp_dep,
  ],
)

libart_compiler_dep = declare_dependency(include_directories: libart_compiler_includes, link_with: libart_compiler)
