// simple script for building

var EMCC = '/usr/lib/emsdk_portable/emscripten/1.30.0/emcc';

var DEBUG = false;
var DEBUG_FLAGS = '-g';
var OPTIMIZE_FLAGS = ' -O1'; // -O2 closure optimizations seems to be breaking

var fs = require('fs');

var FLAGS = DEBUG ? DEBUG_FLAGS : OPTIMIZE_FLAGS;

var switches = {
	TOTAL_MEMORY: 33554432, // 67108864 - 64MB
	EMTERPRETIFY: 1,
	// ALLOW_MEMORY_GROWTH: 1,
	// DEMANGLE_SUPPORT: 1
};

FLAGS += ' ' + Object.keys(switches).map(function(s) {
	return '-s ' + s + '=' + switches[s];
}).join(' ');


var includes = [
	//  libmesa
	'src/mesa/program/prog_hash_table.c',
	'src/mesa/program/symbol_table.c',
	'src/mesa/main/imports.c',

	// libglcpp
	'src/glsl/glcpp/glcpp-lex.c',
	'src/glsl/glcpp/glcpp-parse.c',
	'src/glsl/glcpp/pp.c',
	'src/util/hash_table.c',
	'src/util/ralloc.c',

	// libglslopt
	'src/glsl/ast_array_index.cpp',
	'src/glsl/ast_expr.cpp',
	'src/glsl/ast_function.cpp',
	'src/glsl/ast_to_hir.cpp',
	'src/glsl/ast_type.cpp',
	'src/glsl/builtin_functions.cpp',
	'src/glsl/builtin_types.cpp',
	'src/glsl/builtin_variables.cpp',
	'src/glsl/glsl_lexer.cpp',
	'src/glsl/glsl_optimizer.cpp',
	'src/glsl/glsl_parser.cpp',
	'src/glsl/glsl_parser_extras.cpp',
	'src/glsl/glsl_symbol_table.cpp',
	'src/glsl/glsl_types.cpp',
	'src/glsl/hir_field_selection.cpp',
	'src/glsl/ir.cpp',
	'src/glsl/ir_basic_block.cpp',
	'src/glsl/ir_builder.cpp',
	'src/glsl/ir_clone.cpp',
	'src/glsl/ir_constant_expression.cpp',
	'src/glsl/ir_equals.cpp',
	'src/glsl/ir_expression_flattening.cpp',
	'src/glsl/ir_function.cpp',
	'src/glsl/ir_function_can_inline.cpp',
	'src/glsl/ir_function_detect_recursion.cpp',
	'src/glsl/ir_hierarchical_visitor.cpp',
	'src/glsl/ir_hv_accept.cpp',
	'src/glsl/ir_import_prototypes.cpp',
	'src/glsl/ir_print_glsl_visitor.cpp',
	'src/glsl/ir_print_metal_visitor.cpp',
	'src/glsl/ir_print_visitor.cpp',
	'src/glsl/ir_rvalue_visitor.cpp',
	'src/glsl/ir_stats.cpp',
	'src/glsl/ir_unused_structs.cpp',
	'src/glsl/ir_validate.cpp',
	'src/glsl/ir_variable_refcount.cpp',
	'src/glsl/link_atomics.cpp',
	'src/glsl/link_functions.cpp',
	'src/glsl/link_interface_blocks.cpp',
	'src/glsl/link_uniform_block_active_visitor.cpp',
	'src/glsl/link_uniform_blocks.cpp',
	'src/glsl/link_uniform_initializers.cpp',
	'src/glsl/link_uniforms.cpp',
	'src/glsl/link_varyings.cpp',
	'src/glsl/linker.cpp',
	'src/glsl/loop_analysis.cpp',
	'src/glsl/loop_controls.cpp',
	'src/glsl/loop_unroll.cpp',
	'src/glsl/lower_clip_distance.cpp',
	'src/glsl/lower_discard.cpp',
	'src/glsl/lower_discard_flow.cpp',
	'src/glsl/lower_if_to_cond_assign.cpp',
	'src/glsl/lower_instructions.cpp',
	'src/glsl/lower_jumps.cpp',
	'src/glsl/lower_mat_op_to_vec.cpp',
	'src/glsl/lower_named_interface_blocks.cpp',
	'src/glsl/lower_noise.cpp',
	'src/glsl/lower_offset_array.cpp',
	'src/glsl/lower_output_reads.cpp',
	'src/glsl/lower_packed_varyings.cpp',
	'src/glsl/lower_packing_builtins.cpp',
	'src/glsl/lower_ubo_reference.cpp',
	'src/glsl/lower_variable_index_to_cond_assign.cpp',
	'src/glsl/lower_vec_index_to_cond_assign.cpp',
	'src/glsl/lower_vec_index_to_swizzle.cpp',
	'src/glsl/lower_vector.cpp',
	'src/glsl/lower_vector_insert.cpp',
	'src/glsl/lower_vertex_id.cpp',
	'src/glsl/opt_algebraic.cpp',
	'src/glsl/opt_array_splitting.cpp',
	'src/glsl/opt_constant_folding.cpp',
	'src/glsl/opt_constant_propagation.cpp',
	'src/glsl/opt_constant_variable.cpp',
	'src/glsl/opt_copy_propagation.cpp',
	'src/glsl/opt_copy_propagation_elements.cpp',
	'src/glsl/opt_cse.cpp',
	'src/glsl/opt_dead_builtin_variables.cpp',
	'src/glsl/opt_dead_builtin_varyings.cpp',
	'src/glsl/opt_dead_code.cpp',
	'src/glsl/opt_dead_code_local.cpp',
	'src/glsl/opt_dead_functions.cpp',
	'src/glsl/opt_flatten_nested_if_blocks.cpp',
	'src/glsl/opt_flip_matrices.cpp',
	'src/glsl/opt_function_inlining.cpp',
	'src/glsl/opt_if_simplification.cpp',
	'src/glsl/opt_minmax.cpp',
	'src/glsl/opt_noop_swizzle.cpp',
	'src/glsl/opt_rebalance_tree.cpp',
	'src/glsl/opt_redundant_jumps.cpp',
	'src/glsl/opt_structure_splitting.cpp',
	'src/glsl/opt_swizzle_swizzle.cpp',
	'src/glsl/opt_tree_grafting.cpp',
	'src/glsl/opt_vector_splitting.cpp',
	'src/glsl/opt_vectorize.cpp',
	'src/glsl/s_expression.cpp',
	'src/glsl/strtod.c',
	'src/glsl/standalone_scaffolding.cpp',
];

var compile_glsl_opt = EMCC + ' -Isrc -Isrc/mesa -Iinclude '
	+ includes.join(' ')
	+ ' -DHAVE___BUILTIN_FFS=0 -o glslopt.bc '
	+ FLAGS;

var package_glsl_opt = EMCC + ' glslopt.bc -Isrc/glsl src/emscripten/EmMain.cpp '
	+ ' -o glsl-optimizer.js -DUSE_EMBINDS=1 --bind -s EXPORTED_FUNCTIONS="[\'_optimize_glsl\']" '
	+ FLAGS;

var compile_all = EMCC + ' -Isrc -Isrc/mesa -Iinclude -Isrc/glsl '
	+ includes.join(' ')
	+ ' src/emscripten/EmMain.cpp -DHAVE___BUILTIN_FFS=0 -o glsl-optimizer.js -s EXPORTED_FUNCTIONS="[\'_optimize_glsl\']"  '
	+ FLAGS;

var exec = require('child_process').exec;

var jobs = [
	// compile_glsl_opt,
	// package_glsl_opt
	compile_all
];

function onExec(error, stdout, stderr) {
	if (stdout) console.log('stdout: ' + stdout);
	if (stderr) console.log('stderr: ' + stderr);
	if (error !== null) {
		console.log('exec error: ' + error);
	} else {
		nextJob();
	}
}

function nextJob() {
	if (!jobs.length) {
		console.log('jobs done');
		return;
	}
	var cmd = jobs.shift();
	console.log('running ' + cmd);
	exec(cmd, onExec);
}

nextJob();
