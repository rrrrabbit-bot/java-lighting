# Java Edition lighting / shadow reference for a Bedrock native-mod port

Scope: Minecraft Java Edition **1.20.1** (CPU lightmap, matching the requested 1.20–1.21 range) and
**1.21.4** (same maths moved into the `lightmap` core shader). Code is quoted verbatim from
Mojang-mapped, Vineflower-decompiled sources. Claims marked **[INFERRED]** / **[NOT FOUND]** are
exactly that.

## 0. Sources and provenance

Decompiled mirror: `code.chipmunk.land/dinhero21/MinecraftDeobfuscated-Mojang` (mirror of
`github.com/NikitaCartes-archive/MinecraftDeobfuscated-Mojang`; Enigma + Vineflower, Mojang mappings).
Exact per-file URLs in §6. Quotes document the algorithm, not code to copy into the mod.
Version map: 1.20.1 builds the lightmap on the CPU into a 16×16 `NativeImage`, 1.21.2+ moved the same
maths into the `lightmap` core shader (1.21.4 verified); entity shadows live in
`EntityRenderDispatcher.renderShadow` in both (no `ShadowRenderer` class); AO constants are unchanged.

---

## 1. Lightmap: `LightTexture.updateLightTexture` (1.20.1)

Per-pixel body, verbatim (setup lines omitted, some statements condensed for width; full file in §6):

```java
float g = clientLevel.getSkyDarken(1.0F);          // 0.2 (night) .. 1.0 (noon)
float h;
if (clientLevel.getSkyFlashTime() > 0) { h = 1.0F; } else { h = g * 0.95F + 0.05F; }

float i = this.minecraft.options.darknessEffectScale().get().floatValue();
float j = this.getDarknessGamma(f) * i;            // 0 unless Darkness
float k = this.calculateDarknessScale(this.minecraft.player, j, f) * i;
float l = this.minecraft.player.getWaterVision();
float m;
if (this.minecraft.player.hasEffect(MobEffects.NIGHT_VISION)) {
    m = GameRenderer.getNightVisionScale(this.minecraft.player, f);
} else if (l > 0.0F && this.minecraft.player.hasEffect(MobEffects.CONDUIT_POWER)) { m = l; }
else { m = 0.0F; }

Vector3f vector3f = new Vector3f(g, g, 1.0F).lerp(new Vector3f(1.0F, 1.0F, 1.0F), 0.35F);
float n = this.blockLightRedFlicker + 1.5F;        // flicker ~0
Vector3f vector3f2 = new Vector3f();

for (int o = 0; o < 16; o++) {                    // o = skyLight   -> texture Y
    for (int p = 0; p < 16; p++) {                // p = blockLight -> texture X
        float q = getBrightness(clientLevel.dimensionType(), o) * h;
        float r = getBrightness(clientLevel.dimensionType(), p) * n;
        float t = r * ((r * 0.6F + 0.4F) * 0.6F + 0.4F);
        float u = r * (r * r * 0.6F + 0.4F);
        vector3f2.set(r, t, u);                   // block-light colour (R>G>B)
        boolean bl = clientLevel.effects().forceBrightLightmap();
        if (bl) {
            vector3f2.lerp(new Vector3f(0.99F, 1.12F, 1.0F), 0.25F);
            clampColor(vector3f2);
        } else {
            vector3f2.add(new Vector3f(vector3f).mul(q));
            vector3f2.lerp(new Vector3f(0.75F, 0.75F, 0.75F), 0.04F);
            if (this.renderer.getDarkenWorldAmount(f) > 0.0F) {
                float v = this.renderer.getDarkenWorldAmount(f);
                vector3f2.lerp(new Vector3f(vector3f2).mul(0.7F, 0.6F, 0.6F), v);
            }
        }
        if (m > 0.0F) {                           // night vision: scale to max component 1
            float w = Math.max(vector3f2.x(), Math.max(vector3f2.y(), vector3f2.z()));
            if (w < 1.0F) { vector3f2.lerp(new Vector3f(vector3f2).mul(1.0F / w), m); }
        }
        if (!bl) {
            if (k > 0.0F) { vector3f2.add(-k, -k, -k); }   // Darkness
            clampColor(vector3f2);
        }
        float w = this.minecraft.options.gamma().get().floatValue();   // 0..1, default 0.5
        Vector3f vector3f5 = new Vector3f(this.notGamma(vector3f2.x), this.notGamma(vector3f2.y), this.notGamma(vector3f2.z));
        vector3f2.lerp(vector3f5, Math.max(0.0F, w - j));
        vector3f2.lerp(new Vector3f(0.75F, 0.75F, 0.75F), 0.04F);
        clampColor(vector3f2).mul(255.0F);
        this.lightPixels.setPixelRGBA(p, o, 0xFF000000 | (int)vector3f2.z() << 16 | (int)vector3f2.y() << 8 | (int)vector3f2.x());
    }
}
this.lightTexture.upload();
```

Supporting functions, verbatim:

```java
private static void clampColor(Vector3f v) { v.set(Mth.clamp(v.x,0,1), Mth.clamp(v.y,0,1), Mth.clamp(v.z,0,1)); }
private float notGamma(float f) { float g = 1.0F - f; return 1.0F - g * g * g * g; }   // per component
public static float getBrightness(DimensionType d, int i) {
    float f = i / 15.0F;
    float g = f / (4.0F - 3.0F * f);                      // light curve
    return Mth.lerp(d.ambientLight(), g, 1.0F);
}
public static int pack(int i, int j) { return i << 4 | j << 20; }   // block | sky
```

Pixel byte order: `setPixelRGBA(x=blockLight, y=skyLight, 0xFF000000 | B<<16 | G<<8 | R)`; sampled
with **LINEAR** filtering (`turnOnLightLayer` → `GL_LINEAR`).

### 1.1 Exact colour constants (no fixed sky/block `Vec3` constants exist in 1.20+)

All colours are derived at runtime. Literal constants in the file:

| Constant | Value | Role |
|---|---|---|
| ambient mix colour | `(0.75, 0.75, 0.75)` weight **0.04**, applied **twice** | lifts / desaturates shadows |
| block-light cubic | `t = r*((r*0.6+0.4)*0.6+0.4)`, `u = r*(r*r*0.6+0.4)` | warm block light |
| block-light factor | `n = blockLightRedFlicker + 1.5` (~0 flicker) | over-drives block light |
| sky-light colour | `(g,g,1)` lerped **35 %** to white → `(0.65g+0.35, 0.65g+0.35, 1.0)` | neutral noon, blue night |
| sky factor | `h = g*0.95 + 0.05` | scales sky brightness |
| bright-lightmap mix | `(0.99, 1.12, 1.0)` weight 0.25 when dimension `forceBrightLightmap` | End-like effects **[INFERRED]** |
| night-vision weight | `1.0`, else `0.7 + 0.3*sin((duration - partialTick) * PI * 0.2)` in last 200 ticks | — |
| darken-world (boss) | multiply `(0.7, 0.6, 0.6)`, lerp by `getDarkenWorldAmount` | — |

`ClientLevel.getSkyDarken` (source of `g`), verbatim:

```java
float g = this.getTimeOfDay(f);
float h = 1.0F - (Mth.cos(g * (float)(Math.PI * 2)) * 2.0F + 0.2F);
h = Mth.clamp(h, 0.0F, 1.0F); h = 1.0F - h;
h *= 1.0F - this.getRainLevel(f) * 5.0F / 16.0F;
h *= 1.0F - this.getThunderLevel(f) * 5.0F / 16.0F;
return h * 0.8F + 0.2F;                         // range [0.2, 1.0]
```

### 1.2 Brightness ("gamma") semantics (verified)

`Options.java` 1.20.1: `new OptionInstance<>("options.gamma", ..., OptionInstance.UnitDouble.INSTANCE,
0.5, double_ -> {})`. Slider **0.0 … 1.0, default 0.5** (labels `gamma.min` at 0, `gamma.default` at
50, `gamma.max` at 100). It is not an exponent: it is the mix weight between the colour and
`notGamma(colour) = 1-(1-x)^4`. At the default, 50 % of that lift is applied — the main reason Java
looks lifted/contrasty. With Darkness the weight is `max(0, gamma - darknessBlend)`.

### 1.3 Same maths in 1.21.4 (shader form, verbatim)

`LightTexture` 1.21.4 only uploads uniforms (`AmbientLightFactor`, `SkyFactor=h`, `BlockFactor=n`,
`UseBrightLightmap`, `SkyLightColor=(g,g,1)→white 35%`, `NightVisionFactor=m`, `DarknessScale=k`,
`DarkenWorldFactor`, `BrightnessFactor=max(0, gamma-j)`). The maths lives in
`assets/minecraft/shaders/core/lightmap.fsh`:

```glsl
float get_brightness(float level) { float curved = level / (4.0 - 3.0 * level); return mix(curved, 1.0, AmbientLightFactor); }
vec3 notGamma(vec3 x) { vec3 nx = 1.0 - x; return 1.0 - nx*nx*nx*nx; }

float block_brightness = get_brightness(floor(texCoord.x * 16) / 15) * BlockFactor;
float sky_brightness   = get_brightness(floor(texCoord.y * 16) / 15) * SkyFactor;
vec3 color = vec3(block_brightness,
                  block_brightness * ((block_brightness * 0.6 + 0.4) * 0.6 + 0.4),
                  block_brightness * (block_brightness * block_brightness * 0.6 + 0.4));
if (UseBrightLightmap != 0) { color = mix(color, vec3(0.99,1.12,1.0), 0.25); color = clamp(color,0.0,1.0); }
else { color += SkyLightColor * sky_brightness; color = mix(color, vec3(0.75), 0.04);
       color = mix(color, color * vec3(0.7,0.6,0.6), DarkenWorldFactor); }
if (NightVisionFactor > 0.0) { float mx = max(color.r,max(color.g,color.b));
       if (mx < 1.0) color = mix(color, color / mx, NightVisionFactor); }
if (UseBrightLightmap == 0) color = clamp(color - vec3(DarknessScale), 0.0, 1.0);
color = mix(color, notGamma(color), BrightnessFactor);
color = mix(color, vec3(0.75), 0.04);
fragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
```

The shader (1.21.2+) applies the 0.04 grey mix only once, before `notGamma`; 1.20.1 applies it inside
the `else` branch and again at the end. Use the shader as canonical for 1.21.2+.

### 1.4 C++ port pseudocode

```cpp
Vec3 lightmap(int block15, int sky15, float skyDarken, float gamma /*0..1*/, float darkenWorld) {
    float skyFactor = skyDarken * 0.95f + 0.05f;
    float blockFactor = 1.5f;                                  // + optional flicker
    float q = brightness(dimAmbient, sky15) * skyFactor;
    float r = brightness(dimAmbient, block15) * blockFactor;
    Vec3 c{ r, r*((r*0.6f+0.4f)*0.6f+0.4f), r*(r*r*0.6f+0.4f) };
    Vec3 skyCol{ skyDarken*0.65f + 0.35f, skyDarken*0.65f + 0.35f, 1.0f };
    c += skyCol * q;
    c = mix(c, Vec3{0.75f,0.75f,0.75f}, 0.04f);
    c = mix(c, c * Vec3{0.7f,0.6f,0.6f}, darkenWorld);
    c = clamp01(c);
    c = mix(c, Vec3{notGamma(c.x), notGamma(c.y), notGamma(c.z)}, gamma);   // default 0.5
    c = mix(c, Vec3{0.75f,0.75f,0.75f}, 0.04f);
    return clamp01(c);        // -> 16x16 RGB8, linear filtering
}
```

---

## 2. Entity shadow

### 2.1 1.20.1: `EntityRenderDispatcher.renderShadow` (verbatim, trimmed)

```java
private static final RenderType SHADOW_RENDER_TYPE = RenderType.entityShadow(new ResourceLocation("textures/misc/shadow.png"));
private static final float MAX_SHADOW_RADIUS = 32.0F;
private static final float SHADOW_POWER_FALLOFF_Y = 0.5F;

// caller (render):
if (this.options.entityShadows().get() && this.shouldRenderShadow && entityRenderer.shadowRadius > 0.0F && !entity.isInvisible()) {
    double m = this.distanceToSqr(entity.getX(), entity.getY(), entity.getZ());   // camera dist^2
    float n = (float)((1.0 - m / 256.0) * entityRenderer.shadowStrength);         // fade over 16 blocks
    if (n > 0.0F) {
        renderShadow(poseStack, multiBufferSource, entity, n, h, this.level,
                     Math.min(entityRenderer.shadowRadius, 32.0F));
    }
}

private static void renderShadow(PoseStack poseStack, MultiBufferSource multiBufferSource, Entity entity,
                                 float f, float g, LevelReader levelReader, float h) {
    float i = h;
    if (entity instanceof Mob mob && mob.isBaby()) { i = h * 0.5F; }   // babies: half radius
    double d = Mth.lerp((double)g, entity.xOld, entity.getX());
    double e = Mth.lerp((double)g, entity.yOld, entity.getY());
    double j = Mth.lerp((double)g, entity.zOld, entity.getZ());
    float k = Math.min(f / 0.5F, i);                     // vertical search extent from alpha
    int l = Mth.floor(d - i); int m = Mth.floor(d + i);  // X span
    int n = Mth.floor(e - k); int o = Mth.floor(e);      // Y span
    int p = Mth.floor(j - i); int q = Mth.floor(j + i);  // Z span
    PoseStack.Pose pose = poseStack.last();
    VertexConsumer vertexConsumer = multiBufferSource.getBuffer(SHADOW_RENDER_TYPE);
    BlockPos.MutableBlockPos mutableBlockPos = new BlockPos.MutableBlockPos();

    for (int r = p; r <= q; r++) {                       // z
        for (int s = l; s <= m; s++) {                   // x
            mutableBlockPos.set(s, 0, r);
            ChunkAccess chunkAccess = levelReader.getChunk(mutableBlockPos);
            for (int t = n; t <= o; t++) {               // y
                mutableBlockPos.setY(t);
                float u = f - (float)(e - mutableBlockPos.getY()) * 0.5F;   // Y falloff
                renderBlockShadow(pose, vertexConsumer, chunkAccess, levelReader, mutableBlockPos, d, e, j, i, u);
            }
        }
    }
}

private static void shadowVertex(PoseStack.Pose pose, VertexConsumer vc, float f, float g, float h, float i, float j, float k) {
    Vector3f v = pose.pose().transformPosition(g, h, i, new Vector3f());
    vc.vertex(v.x(), v.y(), v.z(), 1.0F, 1.0F, 1.0F, f, j, k, OverlayTexture.NO_OVERLAY, 15728880, 0.0F, 1.0F, 0.0F);
}
```

Per-block shadow quad, verbatim (1.20.1 `renderBlockShadow`, condensed to the substantive lines):

```java
BlockPos blockPos2 = blockPos.below();
BlockState blockState = chunkAccess.getBlockState(blockPos2);
if (blockState.getRenderShape() != RenderShape.INVISIBLE && levelReader.getMaxLocalRawBrightness(blockPos) > 3) {
    if (blockState.isCollisionShapeFullBlock(chunkAccess, blockPos2)) {
        VoxelShape voxelShape = blockState.getShape(chunkAccess, blockPos2);
        float i = LightTexture.getBrightness(levelReader.dimensionType(), levelReader.getMaxLocalRawBrightness(blockPos));
        float j = h * 0.5F * i;                          // alpha = verticalFade * 0.5 * lightCurve
        if (j > 1.0F) j = 1.0F;
        AABB aABB = voxelShape.bounds();                 // quad = top face of colliding block below
        float p = (float)(blockPos.getX() + aABB.minX - d); float q = (float)(blockPos.getX() + aABB.maxX - d);
        float r = (float)(blockPos.getY() + aABB.minY - e);
        float s = (float)(blockPos.getZ() + aABB.minZ - f); float t = (float)(blockPos.getZ() + aABB.maxZ - f);
        float u = -p / 2.0F / g + 0.5F;   // texture coords, centred (+0.5)
        float v = -q / 2.0F / g + 0.5F;
        float w = -s / 2.0F / g + 0.5F;
        float x = -t / 2.0F / g + 0.5F;
        shadowVertex(pose, vertexConsumer, j, p, r, s, u, w);
        shadowVertex(pose, vertexConsumer, j, p, r, t, u, x);
        shadowVertex(pose, vertexConsumer, j, q, r, t, v, x);
        shadowVertex(pose, vertexConsumer, j, q, r, s, v, w);
    }
}
```

Key porting facts:
- **Radius**: not derived from `getBbWidth()`/`getBbHeight()`; it is `entityRenderer.shadowRadius`, a
  per-renderer constant (field default `0.0F`). `PlayerRenderer` passes `0.5F` to its superclass, so
  players use radius 0.5 **[INFERRED: field assignment is in the `LivingEntityRenderer` ctor body,
  which was not printed]**. Baby mobs halve it; clamped to 32; distance fade `1 - d²/256`.
- **Texture / colour**: `textures/misc/shadow.png`, vertex colour white, **alpha = computed fade**,
  standard alpha blending (`RenderType.entityShadow`). No separate height offset; the only `+0.5` is
  in the UVs.
- **Geometry**: one quad per (x,z) column per y layer, snapped to the top face of the full-collision
  block below, world-axis-aligned (not billboarded/entity-rotated). With radius 0.5 the footprint is
  a single 2×2 block neighbourhood — this is the "2×2 quad loop" from the brief.
- **Fades**: brightness gate `getMaxLocalRawBrightness > 3`; light fade via `LightTexture.getBrightness`
  (0.2 dark … 1.0 full); height fade `-0.5` alpha per block; distance fade at 16 blocks.
- **[NOT FOUND]** a `ShadowRenderer` class or a shadow radius computed from entity dimensions does
  not exist in 1.20.1 or 1.21.4; that pattern is from pre-1.15 code (not fetched, not reproduced).

### 2.2 1.21.4 shape of the same code

Uses `EntityRenderState` (pre-interpolated x/y/z) and `getShadowRadius(state)`/`getShadowStrength(state)`;
loops, baby `h*0.5F`, constants, texture and `> 3` gate unchanged. `extractRenderState` does store
`boundingBoxWidth = getBbWidth()` / `boundingBoxHeight = getBbHeight()`, but `renderShadow` never reads
them. Vertex alpha = `ARGB.color(floor(j*255), 255, 255, 255)` (same meaning).

---

## 3. Ambient occlusion / smooth lighting (1.20.1 `ModelBlockRenderer.AmbientOcclusionFace`)

Constants:
- `BlockBehaviour.getShadeBrightness`: `return blockState.isCollisionShapeFullBlock(...) ? 0.2F : 1.0F;`
- `ClientLevel.getShade`: `DOWN 0.5`, `UP 1.0`, `NORTH/SOUTH 0.8`, `WEST/EAST 0.6`; if the dimension's
  effects use `constantAmbientLight` (Nether-like) then `DOWN/UP = 0.9` and unshaded faces return 0.9.
- Lightmap blend: 4 corner samples, `i + j + k + l >> 2 & 16711935` (two packed channels averaged
  independently); a zero sample is replaced by the centre sample. Weighted variant averages
  `>>16&0xFF` and `&0xFF` channels with corner weights and repacks `n<<16 | o`.

Algorithm: take the quad's `bounds()`, get side1/side2/corner shade-brightness (0.2 solid / 1.0 open,
diagonals substituted when a side is open), `w` = shade of the block at the face (or in front if
non-cubic); vertex `= (m+f+p+w)*0.25` (cubic path also multiplies by the two shape-size weights from
`AdjacencyInfo`, so thin/partial blocks get scaled AO); finally multiply every vertex brightness by
`getShade(direction, bakedQuad.isShade())`.

Computed AO vertex brightness (matches Java arithmetic):

| centre | side1 | side2 | corner | AO vertex | UP | N/S | W/E | DOWN |
|---|---|---|---|---|---|---|---|---|
| 1.0 | 1.0 | 1.0 | 1.0 | 1.000 | 1.000 | 0.800 | 0.600 | 0.500 |
| 1.0 | 1.0 | 1.0 | 0.2 | 0.800 | 0.800 | 0.640 | 0.480 | 0.400 |
| 1.0 | 0.2 | 0.2 | 1.0 | 0.600 | 0.600 | 0.480 | 0.360 | 0.300 |
| 1.0 | 0.2 | 0.2 | 0.2 | 0.400 | 0.400 | 0.320 | 0.240 | 0.200 |
| 0.2 | 0.2 | 0.2 | 0.2 | 0.200 | 0.200 | 0.160 | 0.120 | 0.100 |

Smooth lighting is on by default: `Options.ambientOcclusion = createBoolean("options.ao", true, ...)`;
`ModelBlockRenderer.tesselateBlock` uses AO unless `!Minecraft.useAmbientOcclusion()` or the block
emits light (`getLightEmission() != 0`).

---

## 4. Numeric porting tables

Computed with the exact 1.20.1 arithmetic (`ambientLight = 0`, flicker 0, no Darkness/no bosses,
`gamma = 0.5`), R/G/B/A byte order of `setPixelRGBA`.

Block-light tint ramp (`r = getBrightness(block) * 1.5`):

| blockLight | r | tint | R/G/B floats |
|---|---|---|---|
| 0 | 0.0000 | #000000 | 0.0000 / 0.0000 / 0.0000 |
| 2 | 0.0370 | #090604 | 0.0370 / 0.0242 / 0.0148 |
| 4 | 0.0833 | #150E09 | 0.0833 / 0.0558 / 0.0337 |
| 6 | 0.1429 | #24190F | 0.1429 / 0.0988 / 0.0589 |
| 8 | 0.2222 | #392918 | 0.2222 / 0.1600 / 0.0955 |
| 10 | 0.3333 | #554128 | 0.3333 / 0.2533 / 0.1556 |
| 12 | 0.5000 | #806946 | 0.5000 / 0.4100 / 0.2750 |
| 15 | 1.0000 | #FFFFFF | 1.0000 / 1.0000 / 1.0000 |

Lightmap RGB, **day** (`skyDarken = 1.0`) top and **night** (`skyDarken = 0.2`) bottom; rows
blockLight, columns skyLight:

| block\sky | 0 | 6 | 12 | 15 |
|---|---|---|---|---|
| 0 day | #191919 | #5C5C5C | #B9B9B9 | #FBFBFB |
| 0 night | #191919 | #22222B | #373752 | #50507D |
| 6 day | #756046 | #9D8E7C | #D9D1C8 | #FCFCFC |
| 6 night | #756046 | #7A6655 | #877474 | #968697 |
| 12 day | #DDD5C2 | #EFE7D6 | #FCFCFC | #FCFCFC |
| 12 night | #DDD5C2 | #DFD7C7 | #E4DCD3 | #EBE3E3 |
| 15 day | #FCFCFC | #FCFCFC | #FCFCFC | #FCFCFC |
| 15 night | #FCFCFC | #FCFCFC | #FCFCFC | #FCFCFC |

Reference corners / gamma sweep (day):

| case | (15,0) | (0,15) | (0,0) |
|---|---|---|---|
| gamma 0.00 | #FCFCFC | #FAFAFA | #0F0F0F |
| gamma 0.50 (default) | #FCFCFC | #FBFBFB | #191919 |
| gamma 1.00 | #FCFCFC | #FCFCFC | #242424 |
| dusk, skyDarken 0.6 | #FCFCFC | #B2B2CC | #191919 |

Validation notes:
- **Full noon sky is 252 (`#FCFCFC`), not 255** (0.04 grey mixes + `notGamma` mix). Don't "fix" it;
  `FULL_BRIGHT` (15728880) bypasses the lightmap and is 255.
- Night sky light is blue (`#50507D`), noon sky light neutral (`#FBFBFB`); sample with **linear**
  filtering, texture X = block level, Y = sky level (`uv = level/15`).
- Dimension ambient light: Overworld `0.0`, Nether `0.1`, End `0.0` (fed to `getBrightness`'s lerp;
  verify against `DimensionType` before relying on non-Overworld values).

## 5. Why Java looks "warmer / more contrasty" than Bedrock

Java-side, verified above: (1) block light is warm by construction — `(r, 0.36r²+0.64r, 0.6r³+0.4r)`,
red rises fastest, blue slowest, so torches read orange; (2) sky light is neutral at noon but blue as
`skyDarken` falls (`(0.65g+0.35, 0.65g+0.35, 1.0)`), creating the classic warm-torch/cool-cave
contrast; (3) block and sky light are **added**, so warm and cool blend instead of one winning;
(4) two 0.04 mixes toward 0.75 grey lift and desaturate shadows; (5) the Brightness slider is a
`notGamma` mix (default 0.5), an aggressive lift of mid/lows without clipping; (6) AO is hard: an
enclosed corner can scale a face by `0.2 * 0.6 = 0.12` and the lightmap is averaged per vertex;
(7) shadows are block-projected quads with light- and height-based alpha, giving crisp block-edge
contact shadows.

Bedrock comparison **[UNVERIFIED — confirm with the Bedrock research, do not encode as fact]**:
Bedrock's lightmap is commonly described as bluer/flatter with weaker AO and softer shadows, which
would explain the general "Java is warmer and contrastier" impression. Not measured here.

---

## 6. Exact source URLs used

- LightTexture 1.20.1: http://code.chipmunk.land/dinhero21/MinecraftDeobfuscated-Mojang/raw/tag/1.20.1/minecraft/src/net/minecraft/client/renderer/LightTexture.java
- LightTexture 1.21.4: http://code.chipmunk.land/dinhero21/MinecraftDeobfuscated-Mojang/raw/tag/1.21.4/minecraft/src/net/minecraft/client/renderer/LightTexture.java
- lightmap.fsh 1.21.4: https://raw.githubusercontent.com/misode/mcmeta/1.21.4-assets/assets/minecraft/shaders/core/lightmap.fsh
- EntityRenderDispatcher 1.20.1: http://code.chipmunk.land/dinhero21/MinecraftDeobfuscated-Mojang/raw/tag/1.20.1/minecraft/src/net/minecraft/client/renderer/entity/EntityRenderDispatcher.java
- EntityRenderDispatcher 1.21.4: http://code.chipmunk.land/dinhero21/MinecraftDeobfuscated-Mojang/raw/tag/1.21.4/minecraft/src/net/minecraft/client/renderer/entity/EntityRenderDispatcher.java
- EntityRenderer 1.21.4: http://code.chipmunk.land/dinhero21/MinecraftDeobfuscated-Mojang/raw/tag/1.21.4/minecraft/src/net/minecraft/client/renderer/entity/EntityRenderer.java
- ModelBlockRenderer 1.20.1: http://code.chipmunk.land/dinhero21/MinecraftDeobfuscated-Mojang/raw/tag/1.20.1/minecraft/src/net/minecraft/client/renderer/block/ModelBlockRenderer.java
- BlockBehaviour 1.20.1: http://code.chipmunk.land/dinhero21/MinecraftDeobfuscated-Mojang/raw/tag/1.20.1/minecraft/src/net/minecraft/world/level/block/state/BlockBehaviour.java
- ClientLevel 1.20.1: http://code.chipmunk.land/dinhero21/MinecraftDeobfuscated-Mojang/raw/tag/1.20.1/minecraft/src/net/minecraft/client/multiplayer/ClientLevel.java
- GameRenderer 1.20.1: http://code.chipmunk.land/dinhero21/MinecraftDeobfuscated-Mojang/raw/tag/1.20.1/minecraft/src/net/minecraft/client/renderer/GameRenderer.java
- Options 1.20.1: http://code.chipmunk.land/dinhero21/MinecraftDeobfuscated-Mojang/raw/tag/1.20.1/minecraft/src/net/minecraft/client/Options.java
- PlayerRenderer 1.20.1: http://code.chipmunk.land/dinhero21/MinecraftDeobfuscated-Mojang/raw/tag/1.20.1/minecraft/src/net/minecraft/client/renderer/entity/player/PlayerRenderer.java
- Mirror index: http://code.chipmunk.land/dinhero21/MinecraftDeobfuscated-Mojang
