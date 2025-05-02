#version 330 core
out vec4 FragColor;

uniform vec2 iResolution;
uniform float iTime;

void mainImage(out vec4 fragColor, in vec2 fragCoord);

// Entry point wrapper
void main()
{
  mainImage(FragColor, gl_FragCoord.xy);
}

// IMPLEMENTATION BELOW:

// ========== SDFs ==========

/*
    Signed distance to a sphere centered at the origin.
    p: point in space
    r: sphere radius
    returns: signed distance from p to the sphere surface
*/
float sdSphere(vec3 p, float r)
{
  return length(p) - r; // Distance from center minus radius
}

/*
    Signed distance to an axis-aligned box centered at the origin.
    p: point in space
    b: box half-extents
    returns: signed distance to box surface
*/
float sdBox(vec3 p, vec3 b)
{
  vec3 q = abs(p) - b; // Get signed distance from each box face
  return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0); // Outside distance + interior correction
}

/*
    SDF for a 3D cross composed of a vertical and horizontal box.
    p: point in space
    size: base dimensions for the cross
    returns: distance to nearest part of the cross
*/
float sdCross(vec3 p, vec3 size)
{
  float d1 = sdBox(p, vec3(size.x * 0.3, size.y * 1.8, size.z * 0.3)); // Long vertical bar
  vec3 pOffset = p - vec3(0.0, size.y * 1.0, 0.0); // Shift point for horizontal bar
  float d2 = sdBox(pOffset, vec3(size.x, size.y * 0.25, size.z * 0.3)); // Horizontal bar
  return min(d1, d2); // Combine both bars by min() = union
}

/*
    Signed distance to a vertical capped cylinder.
    p: point in space
    h: half-height
    r: radius
*/
float sdCylinder(vec3 p, float h, float r)
{
  vec2 d = abs(vec2(length(p.xz), p.y)) - vec2(r, h); // Radial and vertical distance
  return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)); // Standard cylinder SDF
}

/*
    Signed distance to a flat disc aligned to the XZ plane.
    p: point in space
    r: radius
    h: half-thickness
*/
float sdDisc(vec3 p, float r, float h)
{
  vec2 d = vec2(length(p.xz) - r, abs(p.y) - h); // Horizontal and vertical distance from disc
  return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0); // Rounded edge SDF
}

/*
    Tapered spear shape stretching along X-axis.
    p: point in space
*/
float sdSpear(vec3 p)
{
  p -= vec3(0.0, 1.2, 0.0); // Raise spear origin up on Y
  float taper = clamp((p.x + 8.0) / 16.0, 0.0, 1.0); // Range: 0 to 1 across spear body
  float radius = mix(0.25, 0.05, taper); // Taper from thick to thin along X
  vec2 d = vec2(abs(p.y), abs(p.z)) - radius; // Projected 2D shape across YZ
  return max(length(max(d, 0.0)), abs(p.x) - 8.0); // Combine with finite length on X
}

// ========== Rotation ==========

/*
    Rotation matrix around Y-axis by angle a (in radians).
*/
mat3 rotationY(float a)
{
  float c = cos(a), s = sin(a); // Compute cosine and sine
  return mat3(
    c, 0.0, -s,
    0.0, 1.0, 0.0,
    s, 0.0, c
  );
}

/*
    Rotation matrix around X-axis by angle a (in radians).
*/
mat3 rotationX(float a)
{
  float c = cos(a), s = sin(a);
  return mat3(
    1.0, 0.0, 0.0,
    0.0, c, -s,
    0.0, s, c
  );
}

// ========== Hash ==========
/*
    1D float hash function used for pseudo-random values.
    n: input float
    returns: pseudo-random float in [0,1)
*/
float hash(float n)
{
  return fract(sin(n * 91.17) * 43758.5453); // Arbitrary constants for entropy
}

// ========== Noise ==========

/*
    3D smooth value noise using interpolation of hashed grid corners.
    p: input position
*/
float noise(vec3 p)
{
  vec3 i = floor(p); // Integer cell
  vec3 f = fract(p); // Local position within cell
  f = f * f * (3.0 - 2.0 * f); // Smoothstep interpolation
  float n = dot(i, vec3(1.0, 57.0, 113.0)); // Unique base per cube
  return mix(
    mix(mix(hash(n + 0.0), hash(n + 1.0), f.x),
    mix(hash(n + 57.0), hash(n + 58.0), f.x), f.y),
    mix(mix(hash(n + 113.0), hash(n + 114.0), f.x),
    mix(hash(n + 170.0), hash(n + 171.0), f.x), f.y),
    f.z); // Final trilinear interpolation
}

// ========== Volumetric Cloud Layer ==========

/*
    Computes a drifting volumetric cloud layer using ray sampling and noise.
    ro: ray origin
    rd: ray direction
    returns: accumulated cloud alpha in [0, 1]
*/
float cloudVolume(vec3 ro, vec3 rd)
{
  float density = 0.0; // Accumulated density
  for (float t = 0.0; t < 20.0; t += 0.5)
  {
    vec3 pos = ro + rd * t; // Sample position along ray
    pos.xz += iTime * vec2(0.3, 0.1); // Drift clouds over time
    float heightFade = smoothstep(1.2, 3.5, pos.y); // Fade with height
    float n = noise(pos * 0.25); // Noise at reduced frequency
    float d = smoothstep(0.4, 0.7, n); // Binary cloud density
    density += d * heightFade * 0.05; // Accumulate weighted alpha
  }
  return clamp(density, 0.0, 1.0); // Final alpha
}

// ========== Scene Mapping ==========

/*
    Scene SDF mapping function.
    Parameters:
        p     : The 3D point in space to evaluate.
        color : Output variable that receives the color of the closest object.
    Returns:
        The shortest signed distance from point p to any object in the scene.
*/
float map(vec3 p, out vec3 color)
{
  float minD = 100.0; // Initialize with a large distance so anything will be closer
  color = vec3(0.0);  // Default color (black) in case nothing is hit

  // Iterate over 45 procedurally animated shapes
  for (int i = 0; i < 45; i++)
  {
    float fi = float(i); // Convert loop index to float for arithmetic
    float id = mod(fi * 13.13, 4.0); // Use mod to pick one of 4 shape types pseudo-randomly
    float t = iTime * 0.4 + fi * 2.0; // Time-dependent phase offset, gives each shape unique orbit

    // Calculate object's orbiting position in space
    vec3 pos = vec3(
      sin(t) * 4.0,          // Horizontal X oscillation
      cos(t * 0.6) * 2.0,    // Vertical Y oscillation with different frequency
      sin(t * 0.2) * 4.0     // Depth Z oscillation with slower frequency
    );

    vec3 q = p - pos; // Move space so object is centered at the origin

    float angle = iTime + fi * 0.5; // Time-based rotation angle, varies per shape
    mat3 rot = rotationY(angle) * rotationX(angle * 0.5); // Combine Y and X rotation for spinning effect
    q = rot * q; // Rotate point q into the local space of the object

    float d;      // Distance from this object
    vec3 c;       // Color for this object
    float seed = fi * 13.37; // Unique seed per shape used for color variation and randomness

    // Choose SDF shape type and color based on pseudo-random ID
    if (id < 1.0)
    {
      if (i == 0) continue; // Skip the very first object entirely

      // Choose between two fixed colors: warm orange or cool blue
      c = mod(floor(seed), 2.0) < 1.0
        ? vec3(1.0, 0.6, 0.1)    // orange
        : vec3(0.7, 0.9, 1.0);   // light blue

      d = sdSphere(q, 0.3); // Use a simple sphere SDF
    }
    else if (id < 2.0)
    {
      // Alternate between black and deep red
      c = mod(floor(seed), 2.0) < 1.0
        ? vec3(0.0)
        : vec3(0.5, 0.0, 0.0);

      d = sdCross(q, vec3(0.3)); // Use cross shape scaled from base size
    }
    else if (id < 3.0)
    {
      float h = hash(seed + 2.0); // Hash used to vary color
      c = mix(
        vec3(1.0, 1.0, 0.5), // pale yellow
        vec3(1.0, 0.9, 0.3), // golden
        h                    // interpolate based on hash value
      );
      d = sdCylinder(q, 0.4, 0.1); // Use a thin vertical cylinder
    }
    else
    {
      float h = hash(seed + 3.0); // Another hash for variation
      c = mix(
        vec3(1.0, 0.85, 0.3), // amber
        vec3(1.0, 0.75, 0.1), // deeper orange
        h
      );
      d = sdDisc(q, 0.4, 0.08); // Flat disc shape
    }

    // If this object is the closest one so far, store its distance and color
    if (d < minD)
    {
      minD = d;
      color = c;
    }
  }

  // Add an animated spear object that is not orbiting
  vec3 q = p - vec3(0.0, 1.2, 0.0); // Move spear to fixed position above ground
  mat3 spearRot = rotationX(iTime * 0.2); // Slow rotation around X
  q = spearRot * q; // Rotate spear in place
  float spearD = sdSpear(q); // Compute spear SDF

  // Check if the spear is closer than any orbiting shape
  if (spearD < minD)
  {
    minD = spearD;
    color = vec3(0.0); // Spear is black
  }

  return minD; // Return distance to nearest object
}

// ========== Lighting & Raymarching Utilities ==========

/*
    Approximates soft shadowing using ray marching with early exits.
    Parameters:
        ro    : Ray origin (point from which shadow is cast).
        rd    : Ray direction (toward the light source).
        mint  : Minimum distance to start sampling from.
        maxt  : Maximum distance to sample up to.
        k     : Softness factor — higher values give sharper shadows.
    Returns:
        Shadow attenuation factor in [0, 1]. 0 = full shadow, 1 = fully lit.
*/
float softShadow(vec3 ro, vec3 rd, float mint, float maxt, float k)
{
  float res = 1.0;      // Start fully lit
  float t = mint;       // Current sampling distance along the ray

  for (int i = 0; i < 32; i++)
  {
    vec3 pos = ro + rd * t; // Sample point along the ray
    vec3 dummy;             // Placeholder for unused color output
    float h = map(pos, dummy); // Distance to nearest geometry from this point

    if (h < 0.001)
    {
      return 0.0; // Hit something — fully in shadow
    }

    // Estimate how much geometry blocks light — smaller h = more shadow
    res = min(res, k * h / t);

    // Step forward — clamp to avoid over-advancing in small/steep regions
    t += clamp(h, 0.01, 0.2);

    if (t > maxt)
    {
      break; // Past max shadow ray distance
    }
  }

  return clamp(res, 0.0, 1.0); // Clamp result for safety
}

/*
    Computes a surface normal at point p using central difference gradient.
    Parameters:
        p: Surface point.
    Returns:
        Normal vector pointing away from surface.
*/
vec3 getNormal(vec3 p)
{
  vec2 e = vec2(0.001, 0.0); // Small epsilon offset for finite differences
  vec3 dummy;                // Placeholder for unused color

  // Use central difference on each axis to estimate normal
  float dx = map(p + e.xyy, dummy) - map(p - e.xyy, dummy); // x-axis
  float dy = map(p + e.yxy, dummy) - map(p - e.yxy, dummy); // y-axis
  float dz = map(p + e.yyx, dummy) - map(p - e.yyx, dummy); // z-axis

  return normalize(vec3(dx, dy, dz)); // Normalize the gradient to get normal
}

/*
    Raymarching using sphere tracing to find the closest intersection.
    Parameters:
        ro    : Ray origin.
        rd    : Ray direction.
        color : Output variable — the color of the hit object.
        pHit  : Output variable — position of the hit point.
    Returns:
        Distance to the surface if hit, or -1.0 if no hit.
*/
float rayMarch(vec3 ro, vec3 rd, out vec3 color, out vec3 pHit)
{
  float t = 0.0; // Total distance traveled along the ray

  for (int i = 0; i < 100; i++)
  {
    vec3 p = ro + rd * t;      // Current position on ray
    float d = map(p, color);   // Distance to closest surface from p

    if (d < 0.001)
    {
      pHit = p;              // Save hit position
      return t;              // Return distance traveled to hit
    }

    t += d;                    // Advance by distance-to-surface (sphere tracing step)

    if (t > 50.0)
    {
      break; // Max ray distance reached — consider it a miss
    }
  }

  return -1.0; // No hit
}

// ========== Main Shader ==========

/*
    Main rendering function that runs for every fragment.
    Parameters:
        fragColor : Output final pixel color.
        fragCoord : Screen-space coordinate of the pixel.
*/
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
  // Normalize fragCoord to screen-space [-1, 1]
  vec2 uv = (fragCoord.xy / iResolution.xy) * 2.0 - 1.0;
  uv.x *= iResolution.x / iResolution.y; // Correct aspect ratio for wide screens

  // Set up camera
  vec3 ro = vec3(0.0, 0.0, -8.0);            // Camera origin (pulled back along -Z)
  vec3 rd = normalize(vec3(uv, 1.5));        // Camera ray direction toward screen plane

  // Sky gradient setup
  vec3 skyBottom = vec3(0.15, 0.25, 0.7);    // Blueish bottom
  vec3 skyTop = vec3(1.0, 0.55, 0.2);        // Orange-red top
  float f = smoothstep(-1.0, -0.4, uv.y);    // Interpolation factor based on Y screen position
  vec3 bg = mix(skyTop, skyBottom, f);       // Background sky gradient

  // Initialize final color as background
  vec3 col = bg;

  // Variables to receive ray hit info
  vec3 hitColor;
  vec3 pHit;

  // Perform raymarching into the scene
  float t = rayMarch(ro, rd, hitColor, pHit);

  if (t > 0.0)
  {
    // Surface normal at hit point
    vec3 normal = getNormal(pHit);

    // Direction of directional light (could be sun)
    vec3 lightDir = normalize(vec3(-0.4, 0.9, -0.3));

    // Direction from hit point back to camera
    vec3 viewDir = normalize(ro - pHit);

    // Lambertian diffuse lighting
    float diff = clamp(dot(normal, lightDir), 0.0, 1.0);

    // Constant ambient lighting factor
    float ambient = 0.07;

    // How much the surface faces the camera (used for rim/glow)
    float facing = clamp(dot(normal, viewDir), 0.0, 1.0);

    // Soft shadow factor — attenuates diffuse lighting
    float shadow = softShadow(pHit + normal * 0.02, lightDir, 0.1, 10.0, 16.0);

    // Combine lighting components
    vec3 litColor = hitColor * (ambient + diff * 0.5 * shadow); // Base shading

    // Add warm rim light effect
    litColor += vec3(1.0, 0.55, 0.25) * pow(1.0 - facing, 2.0) * 0.15;

    // Add cool backscatter/glow effect
    litColor += vec3(1.0, 0.9, 0.8) * pow(1.0 - dot(normal, viewDir), 3.0) * 0.08;

    // Add subtle high-frequency noise for vibrance modulation
    float noiseFactor =
      0.5 + 0.5 * sin(50.0 * pHit.x) *
      sin(50.0 * pHit.y) *
      sin(50.0 * pHit.z);
    litColor *= mix(0.95, 1.05, noiseFactor); // Slight flicker variation

    // Blend between lit object and background based on distance (fog)
    col = mix(litColor, bg, smoothstep(6.0, 18.0, t));
  }

  // Sample clouds and mix them in (adds haze)
  float clouds = cloudVolume(ro, rd);
  col = mix(col, vec3(1.0), clouds * 0.3); // Brighten scene with clouds

  // Add subtle glow if color intensity exceeds white
  float glow = clamp(length(col) - 1.0, 0.0, 1.0);
  col += vec3(1.0, 0.9, 0.8) * glow * 0.25;

  // Apply gamma correction to convert from linear to sRGB space
  col = pow(col, vec3(0.4545)); // Approximate gamma 2.2 inverse

  // Slight dimming of the whole image to give headroom for glow
  col *= 0.9;

  // Output final color with full opacity
  fragColor = vec4(col, 1.0);
}
