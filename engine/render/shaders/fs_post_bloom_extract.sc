$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_sceneColor, 0);

uniform vec4 u_postBloom;

float luminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

void main()
{
    vec3 color = texture2D(s_sceneColor, v_texcoord0).rgb;
    float knee = max(1.0 - u_postBloom.x, 0.0001);
    float bloom = clamp((luminance(color) - u_postBloom.x) / knee, 0.0, 1.0);
    gl_FragColor = vec4(color * bloom, 1.0);
}