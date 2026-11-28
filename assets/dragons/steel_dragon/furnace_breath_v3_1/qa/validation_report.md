# V3.1 validation

- All three L1 sheets and the L2 sheet match V3 by SHA-256. Original L1–L3 sheets have the same 36 cells, 1024 × 512 alignment, Bonestorm phase timing and file keys.
- L3 adds softly shaded soot behind existing embers and molten fragments, using the inherited particle locations as guides. The particles were not redrawn. Terminal L3 opacity is capped at alpha 6.
- At alpha ≥48, mean horizontal reach 834.76 → 834.89px, mean central-band per-column thickness 103.38 → 104.94px. Alpha beneath the bright core is compared from the 63 complete sustain samples.
- L4 starts at alpha zero and finishes at alpha zero. There are 3–7 active objects on every sustain frame. Wrap alpha change 0.5019, interframe range 0.2827–0.6454.
- The three internal repeats are 480/600/800 ms. All share the same muzzle transform as the base sheets. Full native timelines have 480/2500/480 ms phases.
- Raw V3 baseline AssetQA findings {'FAIL': {'empty_layer': 1}, 'WARN': {'frame_shift': 5, 'loop_seam': 2}, 'REVIEW': {'game_readability': 1, 'visual_timing': 1, 'visual_alignment': 1}}. V3.1 base tracks {'FAIL': {'empty_layer': 1}, 'WARN': {'frame_shift': 5, 'loop_seam': 2}, 'REVIEW': {'game_readability': 1, 'visual_timing': 1, 'visual_alignment': 1}}. The final inherited L3 tail is below the alpha-16 threshold.
- V3.1 L4 AssetQA findings {'FAIL': {}, 'WARN': {'frame_shift': 16}, 'REVIEW': {'game_readability': 1, 'visual_timing': 1, 'visual_alignment': 1}}. Raw sprite checks and standalone previews have been run. Real game deployment and visual parity remain pending.
