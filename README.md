# Anim Notify Tools — "Copy Missing Notifies" Editor Utility Tool

Copies every AnimNotify / AnimNotifyState from a **Source** montage onto a
**Target** montage, skipping any that already exist on the target at roughly
the same time. Existing notifies on the target are never touched or removed.

## Why this is a C++ plugin and not pure Blueprint/Python

A montage notify's actual **time and duration** live behind C++-only
accessors (`FAnimLinkableElement::GetTime()/SetTime()/Link()`,
`FAnimNotifyEvent::SetDuration()`). Those fields are not exposed as
Blueprint or Python-readable/writable properties, so there's no way to
correctly reposition a notify from pure Blueprint or a Python Editor script.
This plugin exposes one `BlueprintCallable` function that does the real work
in C++; you drive it from a small Editor Utility Widget.

## Install

1. Copy the `AnimNotifyTools` folder into your project's `Plugins/` folder
   (create one if it doesn't exist), so you have:
   `YourProject/Plugins/AnimNotifyTools/AnimNotifyTools.uplugin`
2. Regenerate project files (right-click your `.uproject` → *Generate Visual
   Studio project files*, or on Mac/Linux use the equivalent script).
3. Build the project (or just open the `.uproject` — the editor will offer
   to build the missing module for you).
4. In the editor, confirm it's enabled: **Edit → Plugins → search "Anim
   Notify Tools"** (it ships enabled by default via the `.uplugin`).

If your engine version's `FAnimNotifyEvent`/`FAnimLinkableElement` API differs
slightly (Epic has tweaked signatures like `Link()` across 5.x releases),
you may need to adjust `AnimNotifyToolsLibrary.cpp` to match your installed
engine headers (`Engine/Source/Runtime/Engine/Public/Animation/AnimLinkableElement.h`
and `AnimTypes.h`) — the compiler errors will point at exactly what changed.

## Build the Editor Utility Widget

1. In the Content Browser: **Add → Editor Utilities → Editor Utility
   Widget**. Name it e.g. `EUW_CopyMontageNotifies`.
2. Open it. In the widget's **Details panel** (select the root Canvas Panel
   or the widget itself), you'll build a tiny UI:
   - Add two **Object** variables of type **Anim Montage** (or use two
     "Asset Picker" widgets): `SourceMontage`, `TargetMontage`. Mark both
     **Instance Editable** and **Expose on Spawn** so they show up as
     pickers when you run the tool.
   - Add a **Button** labeled "Copy Missing Notifies".
   - Add a **Multi-Line Text Block** (or a Text Block) to show the result.
3. Select the Button → **Details → Events → OnClicked** → **+** to add an
   event, and wire it up in the graph:
   - Call **Copy Missing Anim Notifies** (from `AnimNotifyToolsLibrary`,
     it'll show up in the node search once the plugin is built).
   - `Source Montage` = your `SourceMontage` variable
   - `Target Montage` = your `TargetMontage` variable
   - `Time Tolerance` = `0.03` (about one frame at 30fps — good default)
   - `Save Target Asset` = your choice (check it to auto-save, or leave
     unchecked and save manually afterward)
   - Take the `Out Added Notify Descriptions` and `Out Skipped Notify
     Descriptions` array outputs, run each through **Join String Array**
     (with `\n` separator), and set the Text Block's text to something like:
     `"Added: <n>\n<added list>\n\nSkipped (already present):\n<skipped list>"`
4. Save the widget.

## Run it

Right-click the widget asset → **Run Editor Utility Widget**. Pick your
Source and Target montages, hit the button. It reports exactly what got
added and what was skipped because it already existed.

## Matching logic (what counts as "already there")

Two notifies are treated as the same notify if they have the same identity
**and** land within `TimeTolerance` seconds of each other on the target:

- Simple (name-only) notifies match by `NotifyName`.
- AnimNotify objects match by class (e.g. two `AnimNotify_PlayParticleEffect`
  entries at the same time are considered the same regardless of which
  particle system each one plays).
- AnimNotifyState objects match by class the same way, and duration is
  copied over as well.

If your source and target montages differ in length, a copied notify whose
time falls past the target's length is clamped to fit (and flagged in the
"added" description) rather than silently dropped or crashing.

## Notify tracks

Each copied notify is placed on the target track that has the **same name**
as its source track (e.g. a notify on source track "Sound" lands on a
"Sound" track on the target). If the target doesn't have a track with that
name yet, one is created automatically with the same color as the source
track, so you don't end up with everything piled onto a single row.
