# Harvest Temple CM: encounter data

Reference data for building HTCM helpers. Generated files next to this one:

- `htcm_ei.json`: every skill, buff, species and effect ID that Elite Insights' Harvest Temple logic uses, plus EI's mechanic list grouped by phase. Regenerate with `tools/extract_ei_htcm.py` (EI commit `5bddc4c`).
- `htcm_phase_durations.json`: phase lengths of 15 recent CM kills from [GW2 Wingman](https://gw2wingman.nevermindcreations.de) (era 26-11). Regenerate with `tools/wingman_phase_stats.py`.

Everything here comes from EI's parser and Wingman summaries, not from our own recordings yet. Treat IDs as "very likely right" and timings as rough until a recording confirms them.

## Detecting the fight

| What | How |
|---|---|
| Map | MumbleLink map ID **1437** |
| Encounter / log trigger | **Void Amalgamate**, species `24375` (Wingman boss ID 24375, CM shown as -24375) |
| The dragons | **All six dragons are one agent: "The Dragonvoid"**, a *gadget*. EI lists two gadget IDs for it, `43488` and `1378`. EI's per-dragon IDs (`-17`..`-22`) are labels it adds after parsing; they don't exist in game. |
| Which dragon is up | The Dragonvoid becomes attackable once per dragon phase (`ATTACKTARGET` / `TARGETABLE` state changes). The order is fixed, so counting "targetable on" events gives the phase: Jormag → Primordus → Kralkatorrik → Mordremoth → Zhaitan → Soo-Won (twice). EI double-checks it by skill: Jormag `66216`/`67772`, Kralkatorrik `66790`, Mordremoth `64576`, Zhaitan `65132` (missiles), Primordus/Soo-Won by effect GUIDs. |
| Purifications | Orb = **Void Amalgamate** `24375` with buff **Void Shell** `65563` (EI calls it "PushableVoidAmalgamate"). The last one (Purification 4) is **Killable Void Amalgamate** `23956`. |
| CM vs normal | Any dragon with **> 10,000,000 HP**, or **> 5 Void Melters** (`24223`) spawning at once, or the **Void Empowerment** buff `68083`, or a **Void Goliath** (`24761`) existing. |
| Greens | Effect GUIDs: spawn `3EEDE16455C8C8449237BCC77F107548`, success `72EE47DE4F63D3438E193578011FBCBF`, fail `F4F80E9AF2B6AF49AFE46D8CF797B604`. EI works out which green by angle from arena center (600, -20400, -15420). |
| Your void debuff | **Influence of the Void** buff `64524` on you; its hit is skill `66566`. |

Effects (ground AoEs, orbs) arrive as `51:EFFECT2` state changes carrying a per-session effect ID. The matching `46:IDTOGUID` event maps that ID to the GUIDs listed here.

## Phase timeline (15 CM kills, seconds)

| Phase | min | median | max | Notes |
|---|---|---|---|---|
| Full Fight | 559 | 617 | 691 | kill time |
| Purification 1 | 23 | 25 | 30 |  |
| Jormag | 47 | 56 | 59 |  |
| Primordus | 61 | 67 | 95 |  |
| Kralkatorrik | 48 | 53 | 57 |  |
| Purification 2 | 26 | 28 | 32 |  |
| Void Time Caster | 10 | 11 | 16 | during Purification 2 |
| Mordremoth | 48 | 55 | 58 |  |
| Giants | 10 | 12 | 18 | during Mordremoth |
| Zhaitan | 52 | 57 | 64 |  |
| Purification 3 | 26 | 28 | 43 |  |
| Void Saltspray Dragon | 12 | 15 | 26 | during Purification 3 |
| Soo-Won | 140 | 169 | 197 | both Soo-Won halves + Purification 4 |
| Soo-Won 1 | 52 | 60 | 76 |  |
| Void Obliterator | 11 | 15 | 60 | during Soo-Won |
| Void Goliath | 11 | 12 | 38 | during Soo-Won |
| Soo-Won 2 | 64 | 82 | 92 |  |
| Purification 4 | 22 | 28 | 38 |  |

These are lengths, not start times. Wingman doesn't serve full timelines without a manual reparse, so *when* each attack happens inside a phase has to come from our own recordings (or from `.zevtc` logs run through Elite Insights).

## Mechanics by phase (from Elite Insights)

### General

| Mechanic | Sub | Type | IDs |
|---|---|---|---|
| Baited spread mechanic |  | effect on player | effect `F39933B190100B4C87E808EF8E6C654A` (HarvestTempleTargetedExpulsionSpreadNM)<br>effect `BDF708225224C64183BA3CE2A609D37F` (HarvestTempleTargetedExpulsionSpreadCM) |
| Hit by Targeted Expulsion (Spread) |  | hits player | `66025` HarvestTempleTargetedExpulsionNM<br>`65680` HarvestTempleTargetedExpulsionCM |
| Baited red puddle mechanic |  | effect on player | effect `0CD6F76C1BF9C049A2FCE4D86CB46475` (HarvestTempleVoidPoolRedPuddleSelectionNM)<br>effect `61C1CD7E89346843B04FCE613EC487AA` (HarvestTempleVoidPoolRedPuddleSelectionCM) |
| Hit by Red Void Pool |  | hits player | `65578` VoidPoolNM<br>`63910` VoidPoolCM |
| Received Void debuff |  | debuff on player | `64524` InfluenceOfTheVoidBuff |
| Hit by Void |  | hits player | `66566` InfluenceOfTheVoidSkill |
| Orb was pushed by player |  | player hits | — |
| Hit by Void Explosion (Last Laugh) |  | hits player | `65595` VoidExplosion120Radius<br>`64557` VoidExplosion180Radius |
| Hit by Void Explosion (Last Laugh, Champion) |  | hits player | `64557` VoidExplosion180Radius<br>`64585` VoidExplosion240Radius |
| Hit by Magic Discharge (Orb Explosion Wave) |  | hits player | `65488` MagicDischarge |
| Green Successful |  | effect from enemy | effect `72EE47DE4F63D3438E193578011FBCBF` (HarvestTempleSuccessGreen) |
| Green Failed |  | effect from enemy | effect `F4F80E9AF2B6AF49AFE46D8CF797B604` (HarvestTempleFailedGreen) |

### Purification 1

| Mechanic | Sub | Type | IDs |
|---|---|---|---|
| Hit by Lightning of Jormag |  | hits player | `63881` LightningOfJormag |
| Hit by Flames of Primordus |  | hits player | `66646` FlamesOfPrimordus |
| Hit by Kralkatorrik's Stormfall |  | hits player | `63607` Stormfall |

### Jormag

| Mechanic | Sub | Type | IDs |
|---|---|---|---|
| Hit by Jormag Breath |  | hits player | `65517` BreathOfJormagNorth<br>`66216` BreathOfJormagSouth<br>`67607` BreathOfJormagCenter |
| Hit by Grasp of Jormag |  | hits player | `67772` GraspOfJormag |
| Hit by Jormag Meteor |  | hits player | `65687` FrostMeteor |

### Primordus

| Mechanic | Sub | Type | IDs |
|---|---|---|---|
| Hit by Primordus Slam |  | hits player | `65427` LavaSlam |
| Hit by Primordus Jaws |  | hits player | `65704` JawsOfDestruction |

### Kralkatorrik

| Mechanic | Sub | Type | IDs |
|---|---|---|---|
| Hit by Crystal Barrage |  | hits player | `66790` CrystalBarrage |
| Hit by Kralkatorrik's Branding Beam |  | hits player | `65017` BrandingBeam |
| Hit by Brandbomber Artillery |  | hits player | `64317` BrandedArtillery |
| Hit by Kralkatorrik Void Pool |  | hits player | `63746` VoidPoolKralkatorrik |

### Purification 2

| Mechanic | Sub | Type | IDs |
|---|---|---|---|
| Hit by goop left by heart |  | hits player | `63544` SwarmOfMordremoth_PoolOfUndeath |
| Hit by bees from heart |  | hits player | `67688` SwarmOfMordremoth |
| Hit by Gravity Crush | Timecaster | hits player | `65180` GravityCrushDamage |
| Hit by Nightmare Epoch | Timecaster | hits player | `63691` NightmareEpochDamage |

### Mordremoth

| Mechanic | Sub | Type | IDs |
|---|---|---|---|
| Hit by Mordremoth's Shockwave |  | hits player | `64810` MordremothShockwave |
| Mordremoth's Shockwave started |  | enemy cast start | `64810` MordremothShockwave |
| Hit by Mordremoth's Poison Roar |  | hits player | `64576` PoisonRoar |
| Kicked by Void Skullpiercer | Skullpiercer | hits player | `27266` Kick |
| Hit by Void Skullpiercer's Charged Shot | Skullpiercer | hits player | `63772` ChargedShot |

### Giants

| Mechanic | Sub | Type | IDs |
|---|---|---|---|
| CC'd by Giant's Death Scream |  | hits player | `66886` DeathScream |
| Hit by Giant's Rotting Bile |  | hits player | `67040` RottingBile |
| CC'd by Giant's Stomp |  | hits player | `63767` Stomp |

### Zhaitan

| Mechanic | Sub | Type | IDs |
|---|---|---|---|
| Hit by Zhaitan Scream |  | hits player | `66658` ScreamOfZhaitanNM<br>`64428` ScreamOfZhaitanCM |
| Hit by Zhaitan Poison |  | hits player | `65132` PutridDeluge |
| Hit by Zhaitan's Tail Slam |  | hits player | `64932` ZhaitanTailSlam |

### Purification 3

| Mechanic | Sub | Type | IDs |
|---|---|---|---|
| Hit by Corrupted Waters (Heart Projectile) |  | hits player | `67703` SwarmOfMordremoth_CorruptedWaters |
| Hit by Hydro Burst (Whirlpool) | Saltspray | hits player | `65252` HydroBurst |
| Hit by Call Lightning | Saltspray | hits player | `66424` CallLightning |
| Hit by Frozen Fury | Saltspray | hits player | `65498` FrozenFury |
| Hit by Rolling Flame | Saltspray | hits player | `67646` RollingFlame |
| Hit by Shatter Earth | Saltspray | hits player | `67104` ShatterEarth<br>`65241` ShatterEarth2 |

### Soo Won

| Mechanic | Sub | Type | IDs |
|---|---|---|---|
| Hit by Soo-Won Tsunami |  | hits player | `64748` TsunamiSlamClawOrb<br>`66489` TsunamiSlamTailOrb |
| Hit by Soo-Won Claw |  | hits player | `63588` ClawSlap |
| Hit by Soo-Won Void Pool |  | hits player | `67739` VoidPoolSooWon |
| Hit by Soo-Won Tail |  | hits player | `65192` TsunamiSlamTail |
| Hit by Torment of the Void (Bouncing Orbs) |  | hits player | `64150` TormentOfTheVoid |
| Hit by Magic Hail |  | hits player | `64582` MagicHail |
| Hit by Firebomb | Obliterator | hits player | `61838` VoidObliteratorFirebomb |
| Hit by Wyvern Breath | Obliterator | hits player | `65462` VoidObliteratorWyvernBreathDamage |
| Hit by Obliterator's Charge | Obliterator | hits player | `61895` VoidObliteratorCharge |
| Hit by Glacial Slam | Goliath | hits player | `66446` GlacialSlam |

### Purification 4

| Mechanic | Sub | Type | IDs |
|---|---|---|---|
| Hit by Grasp of the Void (Final Orb Projectile) |  | hits player | `64161` GraspOfTheVoid |

## Species

| Name | ID |
|---|---|
| DragonEnergyOrb | `21941` |
| JormagMovingFrostBeamCenter | `23747` |
| JormagMovingFrostBeamNorth | `24541` |
| KillableVoidAmalgamate | `23956` |
| VoidAbomination | `23936` |
| VoidAmalgamate | `24375` |
| VoidBrandbomber | `24783` |
| VoidBurster | `24464` |
| VoidColdsteel | `23945` |
| VoidGiant | `24450` |
| VoidGoliath | `24761` |
| VoidMelter | `24223` |
| VoidObliterator | `23995` |
| VoidRotswarmer | `24590` |
| VoidSaltsprayDragon | `23846` |
| VoidSkullpiercer | `25177` |
| VoidStormseer | `24677` |
| VoidTangler | `25138` |
| VoidTimeCaster | `25025` |
| VoidWarforgedElite | `24129` |
| VoidWarforgedVeteran | `24855` |
| ZhaitansReach | `23839` |
| GravityBall | `-50` |
| JormagMovingFrostBeam | `-51` |
| PushableVoidAmalgamate | `-23` |
| TheDragonVoidJormag | `-21` |
| TheDragonVoidKralkatorrik | `-19` |
| TheDragonVoidMordremoth | `-20` |
| TheDragonVoidPrimordus | `-22` |
| TheDragonVoidSooWon | `-18` |
| TheDragonVoidUnknown | `-109` |
| TheDragonVoidZhaitan | `-17` |
