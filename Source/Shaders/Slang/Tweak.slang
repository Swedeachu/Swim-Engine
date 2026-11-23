#pragma once

// [Tweak(min, max)] on a scalar push-constant field (float, int or uint) of a runtime
// compute program makes it a console variable, shader_<program>_<field> in lower case.
// While the variable is at its default (min) the value the program's owner writes is
// used; any other value replaces it in every dispatch of that program.
[__AttributeUsage(_AttributeTargets.Var)]
struct TweakAttribute
{
	float min;
	float max;
};
