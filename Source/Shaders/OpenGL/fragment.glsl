#version 460 core

in vec3 fragColor;    // from vertex
in vec2 fragUV;       // from vertex

out vec4 FragColor;   // final pixel color

uniform float     hasTexture;  // 0 = vertex color, 1 = sample texture
uniform sampler2D albedoTex;   // bound to texture unit 0

void main()
{
	if (hasTexture > 0.5)
	{
		FragColor = texture(albedoTex, fragUV);
	}
	else
	{
		FragColor = vec4(fragColor, 1.0);
	}

	// Discard fragments that are barely opaque to work as cookie cut outs for like 2D foilage and cheap windows etc
	if (FragColor.a < 0.5)
	{
		discard;
	}
}
