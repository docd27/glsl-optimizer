
#include <stdio.h>
#include <string>
#include <stdlib.h>
#include "glsl_optimizer.h"

static glslopt_ctx* gContext = 0;

#ifdef USE_EMBINDS
	#include <emscripten/val.h>
#endif

#define str(str) std::string(str)
#define concat(a, b) str(a) + str(b)

extern "C" {

    const char* optimize_glsl(char* source, int shaderType, bool vertexShader)
    {

		// bool vertexShader = false;
		glslopt_target languageTarget = kGlslTargetOpenGL;

		// fragment shader
		// vertexShader = false;

		// ES 2
		languageTarget = kGlslTargetOpenGLES20;

		// printf("%d\n", shaderType);

		switch(shaderType) {
			case 1:
				languageTarget = kGlslTargetOpenGL;
				break;
			case 2:
				languageTarget = kGlslTargetOpenGLES20;
				break;
			case 3:
				languageTarget = kGlslTargetOpenGLES30;
				break;
		}

		if( !source )
			return "Error: Must give a source";

		gContext = glslopt_initialize(languageTarget);

		if( !gContext )
		{
			printf("Error: Failed to initialize glslopt!\n");
			return "Error:\nFailed to initialize glslopt";
		}

		const char* failed_log = 0;
		const glslopt_shader_type type = vertexShader ? kGlslOptShaderVertex : kGlslOptShaderFragment;

		glslopt_shader* shader = glslopt_optimize(gContext, type, source, 0);

		const char* optimizedShader;

		if( !glslopt_get_status(shader) )
		{
			failed_log = glslopt_get_log(shader);
			// printf( "Failed to compile:\n\n%s\n", failed_log);
			#ifdef USE_EMBINDS
				emscripten::val::global("GLSLOptimizer").call<void>("onError", str("Failed to compiled: ").append(str(failed_log)));
			#endif
		} else {
			optimizedShader = glslopt_get_output(shader);
			// printf("Out: %s\n", optimizedShader);

			#ifdef USE_EMBINDS
				emscripten::val::global("GLSLOptimizer").call<void>("onSuccess", str(optimizedShader));
			#endif
		}

		glslopt_cleanup(gContext);

		if (failed_log) {
			return str("Error:\n").append(str(failed_log)).data();
		}

		return optimizedShader;
	}

}

