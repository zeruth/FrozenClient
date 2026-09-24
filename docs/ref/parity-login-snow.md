# Login screen snow — measured, not missing

Reported 2026-09-23: no snow on the main menu, on Windows **and** Android.

The obvious reading is that nothing runs. It was measured instead, with one-shot diagnostics on the
Android build (they land in logcat under the `Frozen` tag next to the existing terrain line), and
the obvious reading is wrong.

## What was measured

```
glue model: camera=yes m2DataLoaded=1 emitters=40
            (interface\glues\models\ui_mainmenu_northrend\ui_mainmenu_northrend.m2)
glue snow after 150 frames: renderState=2 quads=416 liveParticles=454
glue snow first quad: pos=(-29.00 5.68 7.59) cam=(11.12 -0.04 2.44)
                      alpha=16 tex=1 blend=4 rgb=(41 71 255) size=(0.081 1.733)
```

So every gate that could have stopped it is open:

| gate | state |
|---|---|
| `CSimpleModel::m_camera` | set — the particle draw call is reached |
| `CM2Shared::m_m2DataLoaded` | 1 |
| `M2Data::particles.Count()` | 40 emitters |
| UI shaders resolve | yes (`renderState=2`) |
| simulation | ~454 live particles |
| quads submitted | ~420 every frame, textured |

**The particles are simulated and drawn. They are not visible.** That is a different bug from the
one the symptom suggests, and it rules out the whole "the glue never runs the emitters" family --
including `CSimpleModel::SetCameraByID`, which is a stub but is not on this path (only
`SetCameraByIndex` is bound to Lua).

## The lead

The sampled quad is **0.081 x 1.733 world units**. At ~40 units from the camera, with a 1024-wide
viewport, one pixel is roughly 0.07 units — so that quad is about **one pixel wide and two dozen
tall**, drawn additively at alpha 16. A hairline that thin, added rather than blended, is
indistinguishable from nothing.

The two dimensions come from one place, `def.scaleTrack` sampled as a `C2Vector`, so a 21:1 aspect
is either authored or a layout problem in `M2Particle` around that track. Worth checking the struct
against the WotLK particle-emitter layout before assuming the data.

**Caveat, stated because it would be easy to over-read the above:** the model carries 40 emitters,
and the sampled quad is the first one built, from whichever emitter came first. Its colour is
`(41, 71, 255)` — blue — which is as likely to be an aurora or mist emitter as snow. So the sliver
is a real observation about *an* emitter, not established to be about the snow one. The next
measurement should report per emitter index rather than first-quad-only.

## A dead end, recorded so it is not retried

`blend=4` looks alarming: `M2BLEND_ADD` is 4 while `GxBlend_Mod` is also 4, and the two enums do
diverge from index 3 onward (M2 3/4/5/6 = NoAlphaAdd/Add/Mod/Mod2x against EGxBlend 3/4/5/6 =
Add/Mod/Mod2x/ModAdd). But `ParticleFx.cpp` keeps `q.blend` in M2 numbering and translates it at
the draw, `case 3: case 4: gxBlend = GxBlend_Add`, so blend 4 does reach the device as additive.
It is correct. Changing it would have broken working particles.

## How to re-measure

The diagnostics were removed rather than left in the tree. To get them back: a one-shot
`SysMsgPrintf` in `CSimpleModel`'s draw block reporting `m_camera`, `m_m2DataLoaded` and
`particles.Count()`; a frame-counted second one reporting quad and live-particle totals; and two
accessors in `ParticleFx.cpp` exposing the first quad's position, colour, alpha, texture, blend and
corner-to-corner size. Sample around 150 frames in -- at load nothing has been emitted yet and
every number is zero.
