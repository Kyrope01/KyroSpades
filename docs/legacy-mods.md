# Legacy mod remasters

The installed packs use **original, unmodified KV6 models**:
10 selectable weapon skins and one grenade. The HK416 Aeon was removed on request. The AK-12 archive contains the
black iron-sights model. The original M9 pack supplied a sniper scope that did not match its pistol
model; it has been replaced with a model-rendered iron sight. The two Desert
Eagle packs supplied only a dot, so those sights and the seven previously
missing sights are also voxel-style images rendered from their own models and
edited for a clear aiming point. The preview backgrounds
are not part of the transparent in-game images.

Supplied shot and reload WAVs are copied byte-for-byte. Where a pack has no
sound, the existing default is used. Pistol models use the Rifle slot and
AK/HK/Uzi models use the SMG slot, matching the source filenames. The baseball
has no sight or sound.

Named credits: Penguano (baseball), Ember (USP-S and Uzi), Diamond (M9). Ownership and redistribution terms
were not independently verified. Original archives are kept in
`tools/legacy_mods/source/`. Run `python3 tools/legacy_mods/generate.py` with
Pillow installed to reproduce the assets and `python3 tests/legacy_mods_test.py`
to verify them. Previews and aiming alignment still require in-game review.

## AK-12 Black

Source: `AK-12.zip`.

![AK-12 Black model](<previews/legacy-mods/AK-12-Black-model.png>)

![AK-12 Black sight](<previews/legacy-mods/smg(AK-12-Black)-sight.png>)

## AK-12 Kobra

Source: `Black - Kobra RDS.zip`.

![AK-12 Kobra model](<previews/legacy-mods/AK-12-Kobra-model.png>)

![AK-12 Kobra sight](<previews/legacy-mods/smg(AK-12-Kobra)-sight.png>)

## AK-12 PSO-1

Source: `Black - PSO-1.zip`.

![AK-12 PSO-1 model](<previews/legacy-mods/AK-12-PSO-1-model.png>)

![AK-12 PSO-1 sight](<previews/legacy-mods/smg(AK-12-PSO-1)-sight.png>)

## AK-12 Nickel

Source: `Nickel and Black - Ironsights.zip`.

![AK-12 Nickel model](<previews/legacy-mods/AK-12-Nickel-model.png>)

![AK-12 Nickel sight](<previews/legacy-mods/smg(AK-12-Nickel)-sight.png>)

## Uzi Ember

Source: `Uzi (Ember).zip`.

![Uzi Ember model](<previews/legacy-mods/Uzi-Ember-model.png>)

![Uzi Ember sight](<previews/legacy-mods/smg(Uzi-Ember)-sight.png>)

## USP-S BluePrint

Source: `BluePrint USP-S (Ember).zip`.

![USP-S BluePrint model](<previews/legacy-mods/USP-S-BluePrint-model.png>)

![USP-S BluePrint sight](<previews/legacy-mods/semi(USP-S-BluePrint)-sight.png>)

## USP-S RoyalBlue

Source: `RoyalBlue USP-S (Ember).zip`.

![USP-S RoyalBlue model](<previews/legacy-mods/USP-S-RoyalBlue-model.png>)

![USP-S RoyalBlue sight](<previews/legacy-mods/semi(USP-S-RoyalBlue)-sight.png>)

## Desert Eagle Asiimov

Source: `DE Asiimov.zip`.

![Desert Eagle Asiimov model](<previews/legacy-mods/Desert-Eagle-Asiimov-model.png>)

![Desert Eagle Asiimov sight](<previews/legacy-mods/semi(Desert-Eagle-Asiimov)-sight.png>)

## Desert Eagle Blaze

Source: `DE Blaze.zip`.

![Desert Eagle Blaze model](<previews/legacy-mods/Desert-Eagle-Blaze-model.png>)

![Desert Eagle Blaze sight](<previews/legacy-mods/semi(Desert-Eagle-Blaze)-sight.png>)

## M9 Diamond

Source: `M9 (Diamond).zip`.

![M9 Diamond model](<previews/legacy-mods/M9-Diamond-model.png>)

![M9 Diamond sight](<previews/legacy-mods/semi(M9-Diamond)-sight.png>)

## Baseball Penguano

Source: `Baseball Grenade (Penguano).zip`.

![Baseball Penguano model](<previews/legacy-mods/Baseball-Penguano-model.png>)

### Reversed weapons and custom tracers

In the **Skins** menu, select a rifle, SMG, or shotgun skin and use **Flip selected weapon** if its model points backwards. The setting is saved separately for each weapon category and skin name in `skin-flips.txt` in the game's writable directory; it affects both the menu preview and the in-game model. It does not flip the sight image or the tracer model.

A weapon skin can provide a matching projectile model in `kv6/`. For example, `semi(RPG7).kv6` uses `semitracer(RPG7).kv6`; SMG and shotgun skins use `smgtracer(NAME).kv6` and `shotguntracer(NAME).kv6` respectively. If no matching tracer is available, the game's standard tracer for that weapon is used. Tracers use the usual KV6 tracer scale.
