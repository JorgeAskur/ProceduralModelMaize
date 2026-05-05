#version 120
varying vec3 vEyePos;
varying vec3 vEyeNormal;
varying vec3 vColor;
varying vec2 vTexCoord;

void main() {
    vec4 eyePos = gl_ModelViewMatrix * gl_Vertex;
    vEyePos = eyePos.xyz;
    vEyeNormal = normalize(gl_NormalMatrix * gl_Normal);
    vColor = gl_Color.rgb;
    vTexCoord = gl_MultiTexCoord0.xy;
    gl_Position = gl_ProjectionMatrix * eyePos;
}
