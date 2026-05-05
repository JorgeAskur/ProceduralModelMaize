#version 120
uniform sampler2D uTexture;
uniform float uUseTexture;
uniform float uUseLighting;
uniform float uLeafFactor;
uniform vec3 uLightPosEye;
uniform vec3 uSkyDirEye;
uniform vec3 uAmbientGround;
uniform vec3 uAmbientSky;
uniform float uDiffuseStrength;
uniform float uWrap;
uniform float uSpecularStrength;
uniform float uShininess;
uniform float uTransmission;
uniform float uExposure;

varying vec3 vEyePos;
varying vec3 vEyeNormal;
varying vec3 vColor;
varying vec2 vTexCoord;

void main() {
    vec4 texSample = vec4(1.0);
    if (uUseTexture > 0.5) texSample = texture2D(uTexture, vTexCoord);
    vec3 albedo = vColor * mix(vec3(1.0), texSample.rgb, uUseTexture);
    if (uUseTexture > 0.5 && texSample.a < 0.05) discard;

    vec3 linear = albedo;
    if (uUseLighting > 0.5) {
        vec3 N = normalize(vEyeNormal);
        vec3 V = normalize(-vEyePos);
        vec3 L = normalize(uLightPosEye - vEyePos);
        vec3 H = normalize(L + V);
        vec3 skyDir = normalize(uSkyDirEye);

        float hemiMask = clamp(0.5 + 0.5 * dot(N, skyDir), 0.0, 1.0);
        vec3 ambient = mix(uAmbientGround, uAmbientSky, hemiMask);

        float wrapped = max((dot(N, L) + uWrap) / (1.0 + uWrap), 0.0);
        float diffuse = wrapped * uDiffuseStrength;

        float specPower = pow(max(dot(N, H), 0.0), max(uShininess, 1.0));
        float specMask = mix(0.45, 1.0, 1.0 - uLeafFactor);
        vec3 specular = vec3(specPower * uSpecularStrength * specMask);

        float rim = pow(clamp(1.0 - max(dot(N, V), 0.0), 0.0, 1.0), 0.75);
        float backLight = pow(max(dot(-N, L), 0.0), 1.35);
        vec3 transmission = albedo * vec3(0.92, 1.0, 0.88) * (uLeafFactor * uTransmission * backLight * (0.3 + 0.7 * rim));

        linear = albedo * (ambient + diffuse) + specular + transmission;
    }

    vec3 mapped = vec3(1.0) - exp(-linear * uExposure);
    vec3 gammaCorrected = pow(clamp(mapped, 0.0, 1.0), vec3(1.0 / 2.2));
    gl_FragColor = vec4(gammaCorrected, 1.0);
}
