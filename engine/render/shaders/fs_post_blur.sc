$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_sceneColor, 0);

uniform vec4 u_postBlur;

void main()
{
    vec2 delta = u_viewTexel.xy * u_postBlur.xy * u_postBlur.z;

    vec3 result = texture2D(s_sceneColor, v_texcoord0).rgb * 0.2270270270;
    result += texture2D(s_sceneColor, v_texcoord0 + delta * 1.3846153846).rgb * 0.3162162162;
    result += texture2D(s_sceneColor, v_texcoord0 - delta * 1.3846153846).rgb * 0.3162162162;
    result += texture2D(s_sceneColor, v_texcoord0 + delta * 3.2307692308).rgb * 0.0702702703;
    result += texture2D(s_sceneColor, v_texcoord0 - delta * 3.2307692308).rgb * 0.0702702703;

    gl_FragColor = vec4(result, 1.0);
}