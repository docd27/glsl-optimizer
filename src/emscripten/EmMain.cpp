
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "glsl_optimizer.h"

static glslopt_ctx* gContext = 0;

#include <emscripten/val.h>

#define str(str) std::string(str)

static bool compileShader2(const char* originalShader, bool vertexShader)
{
	if( !originalShader )
		return false;

	const glslopt_shader_type type = vertexShader ? kGlslOptShaderVertex : kGlslOptShaderFragment;

	glslopt_shader* shader = glslopt_optimize(gContext, type, originalShader, 0);
	if( !glslopt_get_status(shader) )
	{
		const char* failed_log = glslopt_get_log(shader);
		// printf( "Failed to compile:\n\n%s\n", failed_log);
		emscripten::val::global("GLSLOptimizer").call<void>("onError", str("Failed to compiled: ") + str(failed_log));
		return false;
	}

	const char* optimizedShader = glslopt_get_output(shader);
	// printf("Out: %s\n", optimizedShader);

	emscripten::val::global("GLSLOptimizer").call<void>("onSuccess", str(optimizedShader));

	return true;
}

extern "C" {

    int optimize_glsl(char* source, char* type)
    {

		bool vertexShader = false;
		glslopt_target languageTarget = kGlslTargetOpenGL;


		// fragment shader
		vertexShader = false;

		// ES 2
		languageTarget = kGlslTargetOpenGLES20;
		//kGlslTargetOpenGLES20  kGlslTargetOpenGLES30 kGlslTargetOpenGL

		if( !source )
			return printf("Must give a source");

		gContext = glslopt_initialize(languageTarget);

		if( !gContext )
		{
			printf("Failed to initialize glslopt!\n");
			return 1;
		}

		int result = 0;
		if( !compileShader2(source, vertexShader) )
			result = 1;

		glslopt_cleanup(gContext);

		return result;
	}

}

