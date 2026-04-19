$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_sceneColor, 0);
SAMPLER2D(s_bloomColor, 1);
SAMPLER2D(s_feedbackColor, 2);

uniform vec4 u_postBloom;
uniform vec4 u_postColor;
uniform vec4 u_postFeedback;
uniform vec4 u_postTint;

float luminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

vec3 apply_saturation(vec3 color, float saturation)
{
    float luma = luminance(color);
    return mix(vec3(luma, luma, luma), color, saturation);
}

void main()
{
    vec3 scene = texture2D(s_sceneColor, v_texcoord0).rgb;
    vec3 bloom = texture2D(s_bloomColor, v_texcoord0).rgb * u_postBloom.y;

    vec2 feedback_uv = (v_texcoord0 - 0.5) / max(u_postFeedback.z, 0.0001) + 0.5;
    vec3 feedback = texture2D(s_feedbackColor, feedback_uv).rgb * u_postFeedback.y;

    vec3 color = (scene + bloom) * u_postColor.x;
    color *= u_postTint.rgb;
    color = apply_saturation(color, u_postColor.y);
    color = (color - 0.5) * u_postColor.z + 0.5;

    float feedback_mix = u_postFeedback.x * u_postFeedback.w;
    color = mix(color, max(color, feedback), feedback_mix);

    vec2 vignette_uv = v_texcoord0 * 2.0 - 1.0;
    float vignette = smoothstep(1.15, 0.20, dot(vignette_uv, vignette_uv));
    color *= mix(1.0 - u_postColor.w, 1.0, vignette);

    gl_FragColor = vec4(color, 1.0);
}