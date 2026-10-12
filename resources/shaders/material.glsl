// Lighting with the normal and roughness maps of a texture pack. Include it after lighting.glsl and after declaring
// the sun and fspointlights uniforms and the num_point_lights macro that CalcMaterialLighting reads.

// Material alpha stores 1 + roughness * 254, alpha 0 means no material.
float MaterialShininess(float alpha) {
    float roughness = (alpha * 255.0 - 1.0) / 254.0;
    float roughness2 = roughness * roughness;
    return clamp(2.0 / max(roughness2 * roughness2, 1e-4) - 2.0, 1.0, 2048.0);
}

// Phong specular, scaled to keep the brightness of the stock exponent of 128 as the highlight narrows or widens.
float MaterialSpecular(vec3 normal, vec3 lightDir, vec3 viewDir, float shininess) {
    vec3 reflectDir = reflect(-lightDir, normal);
    return pow(max(dot(viewDir, reflectDir), 0.0), shininess) * (shininess + 2.0) / 130.0;
}

vec3 CalcSunLightMaterial(Sunlight light, vec3 normal, vec3 viewDir, float shininess) {
    vec3 lightDir = normalize(light.direction);
    float diff = max(dot(normal, lightDir), 0.0);
    float spec = MaterialSpecular(normal, lightDir, viewDir, shininess);
    return clamp(light.ambient + light.diffuse * diff + light.specular * spec, 0.0, 1.0);
}

// The geometric normal decides whether the light reaches the face at all, the mapped normal only shades it.
vec3 CalcPointLightMaterial(PointLight light, vec3 normal, vec3 geometricNormal, vec3 fragPos, vec3 viewDir, float shininess) {
    if (light.diffuse.r == 0.0 && light.diffuse.g == 0.0 && light.diffuse.b == 0.0)
        return vec3(0);
    if (light.radius < 1.0)
        return vec3(0);

    float distance = length(light.position - fragPos);

    if (distance > light.radius)
        return vec3(0);

    vec3 lightDir = normalize(light.position - fragPos);
    float diff = max(dot(normal, lightDir), 0.0);
    float spec = MaterialSpecular(normal, lightDir, viewDir, shininess);
    float attenuation = clamp(1.0 - ((distance * distance) / (light.radius * light.radius)), 0.0, 1.0);
    attenuation *= attenuation;

    vec3 ambient = (dot(geometricNormal, lightDir) > 0.0) ? light.ambient : vec3(0.0);
    return (ambient + light.diffuse * diff + light.specular * spec) * attenuation;
}

// Tangent and green-up bitangent of a planar face whose texture coordinates are affine in world space, from the
// screen space derivatives of both. Texture v runs down the image, so green-up is minus the v direction.
// Returns false where the derivatives are degenerate, e.g. on a face seen exactly edge-on.
bool FaceTangentFrame(vec3 pos, vec2 uv, vec3 normal, out vec3 tangent, out vec3 bitangent) {
    vec3 dpdx = dFdx(pos);
    vec3 dpdy = dFdy(pos);
    vec2 duvdx = dFdx(uv);
    vec2 duvdy = dFdy(uv);
    float det = duvdx.x * duvdy.y - duvdx.y * duvdy.x;
    vec3 dpdu = (dpdx * duvdy.y - dpdy * duvdx.y) * sign(det);
    vec3 dpdv = (dpdy * duvdx.x - dpdx * duvdy.x) * sign(det);

    tangent = dpdu - normal * dot(normal, dpdu);
    bitangent = -dpdv - normal * dot(normal, -dpdv);
    if (dot(tangent, tangent) == 0.0 || dot(bitangent, bitangent) == 0.0)
        return false;

    tangent = normalize(tangent);
    bitangent = bitangent - tangent * dot(tangent, bitangent);
    if (dot(bitangent, bitangent) == 0.0)
        return false;
    bitangent = normalize(bitangent);
    return true;
}

// Same light stacking as the stock world shaders, with the normal taken from the material.
vec3 CalcMaterialLighting(vec4 material, vec3 albedo, vec3 geometricNormal, vec3 tangent, vec3 bitangent, vec3 fragPos, vec3 viewDir) {
    vec3 normal = normalize(mat3(tangent, bitangent, geometricNormal) * (material.rgb * 2.0 - 1.0));
    float shininess = MaterialShininess(material.a);

    vec3 result = CalcSunLightMaterial(sun, normal, viewDir, shininess);
    result = clamp(result, 0.0, 0.85);

    result += CalcPointLightMaterial(fspointlights[0], normal, geometricNormal, fragPos, viewDir, shininess);

    for (int i = 1; i < num_point_lights; i++) {
        if (fspointlights[i].type == 1.0)
            result += CalcPointLightMaterial(fspointlights[i], normal, geometricNormal, fragPos, viewDir, shininess);
    }

#ifdef LINEAR_PIPELINE
    result *= linearToSrgb(albedo); // Lit in sRGB like the stock shaders, the caller decodes the result.
#else
    result *= albedo;
#endif

    for (int i = 1; i < num_point_lights; i++) {
        if (fspointlights[i].type == 2.0)
            result += CalcPointLightMaterial(fspointlights[i], normal, geometricNormal, fragPos, viewDir, shininess);
    }

    return result;
}
