# Production function index

Generated from the current installed-source tree by `python -m tools.docs_index`.
Line numbers are a snapshot; symbol names are the stable lookup keys. Private helpers remain private.

## bridge/compiler.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `open_compiler` | [9](../src/parkour_solver/bridge/compiler.py#L9) | function |
| `coefficient_request` | [18](../src/parkour_solver/bridge/compiler.py#L18) | function |
| `JavaCompiler` | [29](../src/parkour_solver/bridge/compiler.py#L29) | class |
| `JavaCompiler.__init__` | [30](../src/parkour_solver/bridge/compiler.py#L30) | function |
| `JavaCompiler._start` | [38](../src/parkour_solver/bridge/compiler.py#L38) | function |
| `JavaCompiler._errors` | [45](../src/parkour_solver/bridge/compiler.py#L45) | function |
| `JavaCompiler.compile` | [53](../src/parkour_solver/bridge/compiler.py#L53) | function |
| `JavaCompiler.provenance` | [56](../src/parkour_solver/bridge/compiler.py#L56) | function |
| `JavaCompiler.derive_footprints` | [59](../src/parkour_solver/bridge/compiler.py#L59) | function |
| `JavaCompiler.derive_standing_footprints` | [62](../src/parkour_solver/bridge/compiler.py#L62) | function |
| `JavaCompiler.check_scene` | [65](../src/parkour_solver/bridge/compiler.py#L65) | function |
| `JavaCompiler._exchange` | [71](../src/parkour_solver/bridge/compiler.py#L71) | function |
| `JavaCompiler.close` | [100](../src/parkour_solver/bridge/compiler.py#L100) | function |
| `JavaCompiler.__enter__` | [112](../src/parkour_solver/bridge/compiler.py#L112) | function |
| `JavaCompiler.__exit__` | [115](../src/parkour_solver/bridge/compiler.py#L115) | function |

## bridge/counting.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `CountingCompiler` | [6](../src/parkour_solver/bridge/counting.py#L6) | class |
| `CountingCompiler.__init__` | [7](../src/parkour_solver/bridge/counting.py#L7) | function |
| `CountingCompiler.__getattr__` | [12](../src/parkour_solver/bridge/counting.py#L12) | function |
| `CountingCompiler.summary` | [28](../src/parkour_solver/bridge/counting.py#L28) | function |

## bridge/python_compiler.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `PythonCompiler` | [18](../src/parkour_solver/bridge/python_compiler.py#L18) | class |
| `PythonCompiler.compile` | [19](../src/parkour_solver/bridge/python_compiler.py#L19) | function |
| `PythonCompiler.derive_footprints` | [22](../src/parkour_solver/bridge/python_compiler.py#L22) | function |
| `PythonCompiler.derive_standing_footprints` | [25](../src/parkour_solver/bridge/python_compiler.py#L25) | function |
| `PythonCompiler.check_scene` | [28](../src/parkour_solver/bridge/python_compiler.py#L28) | function |
| `PythonCompiler._exchange` | [34](../src/parkour_solver/bridge/python_compiler.py#L34) | function |
| `PythonCompiler.provenance` | [52](../src/parkour_solver/bridge/python_compiler.py#L52) | function |
| `PythonCompiler.close` | [56](../src/parkour_solver/bridge/python_compiler.py#L56) | function |
| `PythonCompiler.__enter__` | [59](../src/parkour_solver/bridge/python_compiler.py#L59) | function |
| `PythonCompiler.__exit__` | [62](../src/parkour_solver/bridge/python_compiler.py#L62) | function |

## cli/numerical.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `read_json` | [17](../src/parkour_solver/cli/numerical.py#L17) | function |
| `write_json` | [24](../src/parkour_solver/cli/numerical.py#L24) | function |
| `main` | [39](../src/parkour_solver/cli/numerical.py#L39) | function |

## cli/solve.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `main` | [18](../src/parkour_solver/cli/solve.py#L18) | function |

## constraints/avoidance.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `consecutive` | [11](../src/parkour_solver/constraints/avoidance.py#L11) | function |
| `phase_states` | [15](../src/parkour_solver/constraints/avoidance.py#L15) | function |
| `outside` | [24](../src/parkour_solver/constraints/avoidance.py#L24) | function |
| `passage_kind` | [28](../src/parkour_solver/constraints/avoidance.py#L28) | function |
| `blockage_counts` | [33](../src/parkour_solver/constraints/avoidance.py#L33) | function |
| `block_key` | [38](../src/parkour_solver/constraints/avoidance.py#L38) | function |
| `blockage_usage` | [42](../src/parkour_solver/constraints/avoidance.py#L42) | function |
| `gate_timeline` | [51](../src/parkour_solver/constraints/avoidance.py#L51) | function |
| `intervals` | [83](../src/parkour_solver/constraints/avoidance.py#L83) | function |
| `validate_passages` | [97](../src/parkour_solver/constraints/avoidance.py#L97) | function |
| `face_clearance` | [124](../src/parkour_solver/constraints/avoidance.py#L124) | function |
| `compile_passages` | [137](../src/parkour_solver/constraints/avoidance.py#L137) | function |
| `passage_y_valid` | [160](../src/parkour_solver/constraints/avoidance.py#L160) | function |

## constraints/contacts.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `exposed_tops` | [9](../src/parkour_solver/constraints/contacts.py#L9) | function |
| `floor_checks` | [18](../src/parkour_solver/constraints/contacts.py#L18) | function |
| `walk_off_faces` | [63](../src/parkour_solver/constraints/contacts.py#L63) | function |
| `contact_conflicts` | [162](../src/parkour_solver/constraints/contacts.py#L162) | function |
| `avoid_contact_rows` | [173](../src/parkour_solver/constraints/contacts.py#L173) | function |

## constraints/encounters.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `canonical_rows` | [20](../src/parkour_solver/constraints/encounters.py#L20) | function |
| `rows_entailed` | [31](../src/parkour_solver/constraints/encounters.py#L31) | function |
| `landing_segments` | [46](../src/parkour_solver/constraints/encounters.py#L46) | function |
| `settled_through` | [57](../src/parkour_solver/constraints/encounters.py#L57) | function |
| `region` | [67](../src/parkour_solver/constraints/encounters.py#L67) | function |
| `same` | [71](../src/parkour_solver/constraints/encounters.py#L71) | function |
| `satisfied` | [79](../src/parkour_solver/constraints/encounters.py#L79) | function |
| `reaches` | [84](../src/parkour_solver/constraints/encounters.py#L84) | function |
| `side_dict` | [89](../src/parkour_solver/constraints/encounters.py#L89) | function |
| `vertical_side` | [99](../src/parkour_solver/constraints/encounters.py#L99) | function |
| `support_region` | [106](../src/parkour_solver/constraints/encounters.py#L106) | function |
| `prepare_encounter` | [121](../src/parkour_solver/constraints/encounters.py#L121) | function |
| `json_key` | [223](../src/parkour_solver/constraints/encounters.py#L223) | function |
| `_continuation` | [227](../src/parkour_solver/constraints/encounters.py#L227) | function |
| `describe` | [246](../src/parkour_solver/constraints/encounters.py#L246) | function |
| `encounter_option` | [279](../src/parkour_solver/constraints/encounters.py#L279) | function |

## constraints/hit_events.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `validate_hits` | [9](../src/parkour_solver/constraints/hit_events.py#L9) | function |
| `compile_hits` | [64](../src/parkour_solver/constraints/hit_events.py#L64) | function |
| `wall_hit_options` | [108](../src/parkour_solver/constraints/hit_events.py#L108) | function |
| `float_height` | [143](../src/parkour_solver/constraints/hit_events.py#L143) | function |
| `hit_seed` | [148](../src/parkour_solver/constraints/hit_events.py#L148) | function |

## constraints/variants.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `ConstraintVariants` | [20](../src/parkour_solver/constraints/variants.py#L20) | class |
| `ConstraintVariants.for_plan` | [22](../src/parkour_solver/constraints/variants.py#L22) | function |
| `ConstraintVariants.__init__` | [50](../src/parkour_solver/constraints/variants.py#L50) | function |
| `ConstraintVariants.conflict_driven` | [146](../src/parkour_solver/constraints/variants.py#L146) | function |
| `ConstraintVariants.obstacle_key` | [149](../src/parkour_solver/constraints/variants.py#L149) | function |
| `ConstraintVariants.with_obstacles` | [157](../src/parkour_solver/constraints/variants.py#L157) | function |
| `ConstraintVariants.metric` | [161](../src/parkour_solver/constraints/variants.py#L161) | function |
| `ConstraintVariants.coordinate` | [165](../src/parkour_solver/constraints/variants.py#L165) | function |
| `ConstraintVariants.check` | [170](../src/parkour_solver/constraints/variants.py#L170) | function |
| `ConstraintVariants.face_row` | [178](../src/parkour_solver/constraints/variants.py#L178) | function |
| `ConstraintVariants.effective_key` | [183](../src/parkour_solver/constraints/variants.py#L183) | function |
| `ConstraintVariants.analyze` | [195](../src/parkour_solver/constraints/variants.py#L195) | function |
| `ConstraintVariants.excluded` | [213](../src/parkour_solver/constraints/variants.py#L213) | function |
| `ConstraintVariants.prepare_item` | [220](../src/parkour_solver/constraints/variants.py#L220) | function |
| `ConstraintVariants.option` | [242](../src/parkour_solver/constraints/variants.py#L242) | function |
| `ConstraintVariants.__iter__` | [254](../src/parkour_solver/constraints/variants.py#L254) | function |
| `ConstraintVariants.__next__` | [257](../src/parkour_solver/constraints/variants.py#L257) | function |
| `ConstraintVariants.choose_item` | [265](../src/parkour_solver/constraints/variants.py#L265) | function |
| `ConstraintVariants.branch_choice` | [293](../src/parkour_solver/constraints/variants.py#L293) | function |
| `ConstraintVariants._next_variant` | [311](../src/parkour_solver/constraints/variants.py#L311) | function |

## io/capture.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `cell` | [15](../src/parkour_solver/io/capture.py#L15) | function |
| `attach_capture` | [19](../src/parkour_solver/io/capture.py#L19) | function |

## io/checkpoint_store.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `StoredValue` | [21](../src/parkour_solver/io/checkpoint_store.py#L21) | class |
| `StoredValue.__init__` | [22](../src/parkour_solver/io/checkpoint_store.py#L22) | function |
| `_json` | [26](../src/parkour_solver/io/checkpoint_store.py#L26) | function |
| `_atomic_json` | [30](../src/parkour_solver/io/checkpoint_store.py#L30) | function |
| `CheckpointStore` | [37](../src/parkour_solver/io/checkpoint_store.py#L37) | class |
| `CheckpointStore.__init__` | [44](../src/parkour_solver/io/checkpoint_store.py#L44) | function |
| `CheckpointStore.__enter__` | [62](../src/parkour_solver/io/checkpoint_store.py#L62) | function |
| `CheckpointStore.__exit__` | [65](../src/parkour_solver/io/checkpoint_store.py#L65) | function |
| `CheckpointStore.close` | [68](../src/parkour_solver/io/checkpoint_store.py#L68) | function |
| `CheckpointStore.save` | [71](../src/parkour_solver/io/checkpoint_store.py#L71) | function |
| `CheckpointStore.archive` | [76](../src/parkour_solver/io/checkpoint_store.py#L76) | function |
| `CheckpointStore.publish` | [154](../src/parkour_solver/io/checkpoint_store.py#L154) | function |
| `CheckpointStore.patch` | [161](../src/parkour_solver/io/checkpoint_store.py#L161) | function |
| `CheckpointStore.compact` | [175](../src/parkour_solver/io/checkpoint_store.py#L175) | function |
| `read_report` | [210](../src/parkour_solver/io/checkpoint_store.py#L210) | function |
| `main` | [266](../src/parkour_solver/io/checkpoint_store.py#L266) | function |

## io/provenance.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `source_hashes` | [6](../src/parkour_solver/io/provenance.py#L6) | function |
| `source_revision` | [12](../src/parkour_solver/io/provenance.py#L12) | function |

## io/reports.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `save` | [6](../src/parkour_solver/io/reports.py#L6) | function |

## io/search_checkpoint.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `SearchCheckpoint` | [8](../src/parkour_solver/io/search_checkpoint.py#L8) | class |
| `SearchCheckpoint.__init__` | [9](../src/parkour_solver/io/search_checkpoint.py#L9) | function |
| `SearchCheckpoint.state` | [15](../src/parkour_solver/io/search_checkpoint.py#L15) | function |
| `SearchCheckpoint.__call__` | [25](../src/parkour_solver/io/search_checkpoint.py#L25) | function |
| `SearchCheckpoint.begin_repair` | [37](../src/parkour_solver/io/search_checkpoint.py#L37) | function |
| `SearchCheckpoint.repair_progress` | [40](../src/parkour_solver/io/search_checkpoint.py#L40) | function |
| `SearchCheckpoint.finish` | [52](../src/parkour_solver/io/search_checkpoint.py#L52) | function |

## movement/cases.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `outside_row` | [16](../src/parkour_solver/movement/cases.py#L16) | function |
| `tangent_rows` | [22](../src/parkour_solver/movement/cases.py#L22) | function |
| `_faces` | [28](../src/parkour_solver/movement/cases.py#L28) | function |
| `_height` | [33](../src/parkour_solver/movement/cases.py#L33) | function |
| `exposed_side` | [39](../src/parkour_solver/movement/cases.py#L39) | function |
| `convex_corner` | [48](../src/parkour_solver/movement/cases.py#L48) | function |
| `top_choices` | [57](../src/parkour_solver/movement/cases.py#L57) | function |
| `ceiling_clearances` | [70](../src/parkour_solver/movement/cases.py#L70) | function |
| `side_run` | [101](../src/parkour_solver/movement/cases.py#L101) | function |
| `corner_rows` | [117](../src/parkour_solver/movement/cases.py#L117) | function |
| `corner_ledges` | [129](../src/parkour_solver/movement/cases.py#L129) | function |
| `case_profile` | [142](../src/parkour_solver/movement/cases.py#L142) | function |
| `entry_possible` | [175](../src/parkour_solver/movement/cases.py#L175) | function |

## movement/ceiling_generation.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `grind_options` | [8](../src/parkour_solver/movement/ceiling_generation.py#L8) | function |
| `CeilingGenerator` | [45](../src/parkour_solver/movement/ceiling_generation.py#L45) | class |
| `CeilingGenerator.generate` | [46](../src/parkour_solver/movement/ceiling_generation.py#L46) | function |
| `open_ceiling_choices` | [49](../src/parkour_solver/movement/ceiling_generation.py#L49) | function |

## movement/ceiling_profiles.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `ceiling_options` | [6](../src/parkour_solver/movement/ceiling_profiles.py#L6) | function |
| `ceiling_contact` | [15](../src/parkour_solver/movement/ceiling_profiles.py#L15) | function |
| `ceiling_opportunities` | [20](../src/parkour_solver/movement/ceiling_profiles.py#L20) | function |
| `ceiling_sequence` | [27](../src/parkour_solver/movement/ceiling_profiles.py#L27) | function |

## movement/contact_combinations.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `merge_contacts` | [11](../src/parkour_solver/movement/contact_combinations.py#L11) | function |
| `validate_contact_events` | [29](../src/parkour_solver/movement/contact_combinations.py#L29) | function |
| `expanded_face` | [85](../src/parkour_solver/movement/contact_combinations.py#L85) | function |
| `scene_contact_options` | [92](../src/parkour_solver/movement/contact_combinations.py#L92) | function |
| `top_faces` | [100](../src/parkour_solver/movement/contact_combinations.py#L100) | function |
| `solid_owners` | [105](../src/parkour_solver/movement/contact_combinations.py#L105) | function |
| `face_name` | [113](../src/parkour_solver/movement/contact_combinations.py#L113) | function |
| `face_owners` | [119](../src/parkour_solver/movement/contact_combinations.py#L119) | function |
| `contact_event` | [131](../src/parkour_solver/movement/contact_combinations.py#L131) | function |
| `region_choices` | [137](../src/parkour_solver/movement/contact_combinations.py#L137) | function |
| `reachable_option` | [147](../src/parkour_solver/movement/contact_combinations.py#L147) | function |
| `bounding_region` | [158](../src/parkour_solver/movement/contact_combinations.py#L158) | function |
| `wall_heights` | [162](../src/parkour_solver/movement/contact_combinations.py#L162) | function |
| `region_offset` | [167](../src/parkour_solver/movement/contact_combinations.py#L167) | function |
| `finite_rows` | [172](../src/parkour_solver/movement/contact_combinations.py#L172) | function |
| `compile_contact_events` | [178](../src/parkour_solver/movement/contact_combinations.py#L178) | function |
| `contact_combinations` | [208](../src/parkour_solver/movement/contact_combinations.py#L208) | function |

## movement/contact_profiles.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `contact_profile` | [9](../src/parkour_solver/movement/contact_profiles.py#L9) | function |
| `contact_opportunities` | [52](../src/parkour_solver/movement/contact_profiles.py#L52) | function |
| `generate_contact_profiles` | [67](../src/parkour_solver/movement/contact_profiles.py#L67) | function |

## movement/contacts.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `contact_signature` | [14](../src/parkour_solver/movement/contacts.py#L14) | function |
| `interleave_families` | [20](../src/parkour_solver/movement/contacts.py#L20) | function |
| `contact_variants` | [36](../src/parkour_solver/movement/contacts.py#L36) | function |
| `mixed_schedule_pool` | [118](../src/parkour_solver/movement/contacts.py#L118) | function |
| `program_seed` | [167](../src/parkour_solver/movement/contacts.py#L167) | function |

## movement/generation.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `register_movement_type` | [10](../src/parkour_solver/movement/generation.py#L10) | function |
| `generate_choices` | [17](../src/parkour_solver/movement/generation.py#L17) | function |
| `propose_choices` | [28](../src/parkour_solver/movement/generation.py#L28) | function |
| `assess_proposal` | [53](../src/parkour_solver/movement/generation.py#L53) | function |
| `_unit` | [64](../src/parkour_solver/movement/generation.py#L64) | function |
| `NormalGenerator` | [72](../src/parkour_solver/movement/generation.py#L72) | class |
| `NormalGenerator.sneak_inputs` | [73](../src/parkour_solver/movement/generation.py#L73) | function |
| `NormalGenerator.generate` | [76](../src/parkour_solver/movement/generation.py#L76) | function |
| `LadderFallGenerator` | [87](../src/parkour_solver/movement/generation.py#L87) | class |
| `LadderFallGenerator.sneak_inputs` | [88](../src/parkour_solver/movement/generation.py#L88) | function |
| `LadderFallGenerator.generate` | [91](../src/parkour_solver/movement/generation.py#L91) | function |
| `LadderClimbGenerator` | [105](../src/parkour_solver/movement/generation.py#L105) | class |
| `LadderClimbGenerator.sneak_inputs` | [106](../src/parkour_solver/movement/generation.py#L106) | function |
| `LadderClimbGenerator._choice` | [109](../src/parkour_solver/movement/generation.py#L109) | function |
| `LadderClimbGenerator.generate` | [131](../src/parkour_solver/movement/generation.py#L131) | function |
| `generate_unit_programs` | [161](../src/parkour_solver/movement/generation.py#L161) | function |
| `materialize_generated_units` | [186](../src/parkour_solver/movement/generation.py#L186) | function |
| `sneak_domain` | [198](../src/parkour_solver/movement/generation.py#L198) | function |

## movement/ladder/ceiling.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `_unit_at` | [7](../src/parkour_solver/movement/ladder/ceiling.py#L7) | function |
| `own_ceiling_contacts` | [11](../src/parkour_solver/movement/ladder/ceiling.py#L11) | function |
| `own_schedule_ceilings` | [48](../src/parkour_solver/movement/ladder/ceiling.py#L48) | function |
| `predict_ceiling_sequences` | [62](../src/parkour_solver/movement/ladder/ceiling.py#L62) | function |

## movement/ladder/contacts.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `climb_contacts` | [12](../src/parkour_solver/movement/ladder/contacts.py#L12) | function |
| `with_contacts` | [28](../src/parkour_solver/movement/ladder/contacts.py#L28) | function |
| `mixed_ladder_schedules` | [70](../src/parkour_solver/movement/ladder/contacts.py#L70) | function |

## movement/ladder/episodes.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `compose_ladder_episodes` | [11](../src/parkour_solver/movement/ladder/episodes.py#L11) | function |

## movement/ladder/events.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `validate_ladder_events` | [6](../src/parkour_solver/movement/ladder/events.py#L6) | function |
| `compile_ladder_events` | [57](../src/parkour_solver/movement/ladder/events.py#L57) | function |

## movement/ladder/generation.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `ladder_choices` | [9](../src/parkour_solver/movement/ladder/generation.py#L9) | function |
| `ladder_event_choice` | [86](../src/parkour_solver/movement/ladder/generation.py#L86) | function |

## movement/ladder/schedules.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `climb_variants` | [13](../src/parkour_solver/movement/ladder/schedules.py#L13) | function |
| `sneak_patterns` | [28](../src/parkour_solver/movement/ladder/schedules.py#L28) | function |
| `prepare_ladder_request` | [64](../src/parkour_solver/movement/ladder/schedules.py#L64) | function |
| `_round_robin` | [142](../src/parkour_solver/movement/ladder/schedules.py#L142) | function |
| `_context_skeletons` | [154](../src/parkour_solver/movement/ladder/schedules.py#L154) | function |
| `_mask_work` | [166](../src/parkour_solver/movement/ladder/schedules.py#L166) | function |
| `generate_ladder_schedules` | [236](../src/parkour_solver/movement/ladder/schedules.py#L236) | function |

## movement/ladder/settings.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `ladder_settings` | [6](../src/parkour_solver/movement/ladder/settings.py#L6) | function |

## movement/ladder/transfers.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `episode_from_event` | [13](../src/parkour_solver/movement/ladder/transfers.py#L13) | function |
| `open_program` | [29](../src/parkour_solver/movement/ladder/transfers.py#L29) | function |
| `recompose` | [38](../src/parkour_solver/movement/ladder/transfers.py#L38) | function |
| `append_ladder_programs` | [47](../src/parkour_solver/movement/ladder/transfers.py#L47) | function |
| `finish_programs` | [134](../src/parkour_solver/movement/ladder/transfers.py#L134) | function |
| `iter_chain_programs` | [159](../src/parkour_solver/movement/ladder/transfers.py#L159) | function |
| `chain_programs` | [222](../src/parkour_solver/movement/ladder/transfers.py#L222) | function |
| `transfer_programs` | [229](../src/parkour_solver/movement/ladder/transfers.py#L229) | function |

## movement/normal.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `replay` | [8](../src/parkour_solver/movement/normal.py#L8) | function |
| `approaches` | [24](../src/parkour_solver/movement/normal.py#L24) | function |
| `first_descent` | [33](../src/parkour_solver/movement/normal.py#L33) | function |
| `choice_approaches` | [47](../src/parkour_solver/movement/normal.py#L47) | function |
| `event_choice` | [54](../src/parkour_solver/movement/normal.py#L54) | function |
| `named_choice` | [66](../src/parkour_solver/movement/normal.py#L66) | function |
| `feet_allowed` | [77](../src/parkour_solver/movement/normal.py#L77) | function |
| `normal_choices` | [83](../src/parkour_solver/movement/normal.py#L83) | function |

## movement/profiles.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `ordinary_jump_profile` | [5](../src/parkour_solver/movement/profiles.py#L5) | function |
| `_flight_profile` | [9](../src/parkour_solver/movement/profiles.py#L9) | function |

## movement/program.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `scalar_initial` | [12](../src/parkour_solver/movement/program.py#L12) | function |
| `empty_program` | [22](../src/parkour_solver/movement/program.py#L22) | function |
| `boundary_state` | [37](../src/parkour_solver/movement/program.py#L37) | function |
| `profile_from_trace` | [47](../src/parkour_solver/movement/program.py#L47) | function |
| `_profile` | [54](../src/parkour_solver/movement/program.py#L54) | function |
| `movement_choice` | [70](../src/parkour_solver/movement/program.py#L70) | function |
| `_contacts` | [87](../src/parkour_solver/movement/program.py#L87) | function |
| `_shift_event` | [92](../src/parkour_solver/movement/program.py#L92) | function |
| `_events` | [109](../src/parkour_solver/movement/program.py#L109) | function |
| `append_choice` | [135](../src/parkour_solver/movement/program.py#L135) | function |
| `_extend` | [169](../src/parkour_solver/movement/program.py#L169) | function |
| `_rebuild` | [221](../src/parkour_solver/movement/program.py#L221) | function |
| `truncate_program` | [289](../src/parkour_solver/movement/program.py#L289) | function |
| `_canonical` | [350](../src/parkour_solver/movement/program.py#L350) | function |
| `_chain` | [354](../src/parkour_solver/movement/program.py#L354) | function |
| `prefix_chain` | [390](../src/parkour_solver/movement/program.py#L390) | function |
| `_remember` | [401](../src/parkour_solver/movement/program.py#L401) | function |
| `program_physics_key` | [408](../src/parkour_solver/movement/program.py#L408) | function |
| `program_search_key` | [419](../src/parkour_solver/movement/program.py#L419) | function |
| `compile_program` | [428](../src/parkour_solver/movement/program.py#L428) | function |

## movement/settings.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `movement_settings` | [6](../src/parkour_solver/movement/settings.py#L6) | function |

## movement/stepping.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `box_for` | [8](../src/parkour_solver/movement/stepping.py#L8) | function |
| `SteppingGenerator` | [12](../src/parkour_solver/movement/stepping.py#L12) | class |
| `SteppingGenerator.generate` | [13](../src/parkour_solver/movement/stepping.py#L13) | function |
| `_sequence_allowed` | [16](../src/parkour_solver/movement/stepping.py#L16) | function |
| `_fresh` | [27](../src/parkour_solver/movement/stepping.py#L27) | function |
| `_satisfies` | [37](../src/parkour_solver/movement/stepping.py#L37) | function |
| `side_variants` | [42](../src/parkour_solver/movement/stepping.py#L42) | function |
| `open_stepping_choices` | [66](../src/parkour_solver/movement/stepping.py#L66) | function |
| `_open_flight_cases` | [157](../src/parkour_solver/movement/stepping.py#L157) | function |

## movement/units.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `movement_unit` | [10](../src/parkour_solver/movement/units.py#L10) | function |
| `ladder_units` | [18](../src/parkour_solver/movement/units.py#L18) | function |
| `movement_program` | [52](../src/parkour_solver/movement/units.py#L52) | function |
| `compile_units` | [103](../src/parkour_solver/movement/units.py#L103) | function |
| `shifted_unit` | [132](../src/parkour_solver/movement/units.py#L132) | function |
| `profile_program` | [146](../src/parkour_solver/movement/units.py#L146) | function |
| `overlay_program` | [171](../src/parkour_solver/movement/units.py#L171) | function |
| `exit_matches` | [202](../src/parkour_solver/movement/units.py#L202) | function |

## movement/vertical_profiles.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `_flags` | [7](../src/parkour_solver/movement/vertical_profiles.py#L7) | function |
| `trace_ladder_schedule` | [15](../src/parkour_solver/movement/vertical_profiles.py#L15) | function |

## numerics/builders.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `waypoint_seed` | [6](../src/parkour_solver/numerics/builders.py#L6) | function |
| `ladder_waypoints` | [23](../src/parkour_solver/numerics/builders.py#L23) | function |

## numerics/constraints.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `reduce_walls` | [4](../src/parkour_solver/numerics/constraints.py#L4) | function |

## numerics/continuation.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `Incumbent` | [9](../src/parkour_solver/numerics/continuation.py#L9) | class |
| `Incumbent.__init__` | [10](../src/parkour_solver/numerics/continuation.py#L10) | function |
| `Incumbent.consider` | [15](../src/parkour_solver/numerics/continuation.py#L15) | function |
| `Incumbent.pattern` | [28](../src/parkour_solver/numerics/continuation.py#L28) | function |
| `run` | [34](../src/parkour_solver/numerics/continuation.py#L34) | function |

## numerics/disk.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `spec` | [38](../src/parkour_solver/numerics/disk.py#L38) | function |
| `solve` | [67](../src/parkour_solver/numerics/disk.py#L67) | function |
| `decide` | [85](../src/parkour_solver/numerics/disk.py#L85) | function |
| `screen` | [99](../src/parkour_solver/numerics/disk.py#L99) | function |
| `deficits` | [112](../src/parkour_solver/numerics/disk.py#L112) | function |
| `ipopt_seed` | [119](../src/parkour_solver/numerics/disk.py#L119) | function |
| `alarm_ticks` | [131](../src/parkour_solver/numerics/disk.py#L131) | function |
| `RelaxedDispatch` | [142](../src/parkour_solver/numerics/disk.py#L142) | class |
| `RelaxedDispatch.__init__` | [155](../src/parkour_solver/numerics/disk.py#L155) | function |
| `RelaxedDispatch.solve` | [161](../src/parkour_solver/numerics/disk.py#L161) | function |
| `RelaxedDispatch.confirmed` | [164](../src/parkour_solver/numerics/disk.py#L164) | function |
| `RelaxedDispatch._dispatch` | [167](../src/parkour_solver/numerics/disk.py#L167) | function |
| `RelaxedDispatch.confirm` | [203](../src/parkour_solver/numerics/disk.py#L203) | function |
| `_result` | [244](../src/parkour_solver/numerics/disk.py#L244) | function |
| `_attempt` | [260](../src/parkour_solver/numerics/disk.py#L260) | function |
| `_build` | [269](../src/parkour_solver/numerics/disk.py#L269) | function |
| `_run` | [375](../src/parkour_solver/numerics/disk.py#L375) | function |
| `_cost` | [395](../src/parkour_solver/numerics/disk.py#L395) | function |

## numerics/model.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `prepare` | [7](../src/parkour_solver/numerics/model.py#L7) | function |
| `fingerprint` | [194](../src/parkour_solver/numerics/model.py#L194) | function |

## numerics/post_alarm.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `_Stop` | [32](../src/parkour_solver/numerics/post_alarm.py#L32) | class |
| `_Node` | [36](../src/parkour_solver/numerics/post_alarm.py#L36) | class |
| `_Node.__init__` | [38](../src/parkour_solver/numerics/post_alarm.py#L38) | function |
| `_Node.step` | [58](../src/parkour_solver/numerics/post_alarm.py#L58) | function |
| `_Pipeline` | [76](../src/parkour_solver/numerics/post_alarm.py#L76) | class |
| `_Pipeline.__init__` | [77](../src/parkour_solver/numerics/post_alarm.py#L77) | function |
| `_Pipeline.check` | [91](../src/parkour_solver/numerics/post_alarm.py#L91) | function |
| `_Pipeline.decide` | [97](../src/parkour_solver/numerics/post_alarm.py#L97) | function |
| `_Pipeline.better` | [106](../src/parkour_solver/numerics/post_alarm.py#L106) | function |
| `_Pipeline.accept` | [109](../src/parkour_solver/numerics/post_alarm.py#L109) | function |
| `_Pipeline.push` | [125](../src/parkour_solver/numerics/post_alarm.py#L125) | function |
| `_Pipeline.quadrant` | [162](../src/parkour_solver/numerics/post_alarm.py#L162) | function |
| `_Pipeline.probes` | [183](../src/parkour_solver/numerics/post_alarm.py#L183) | function |
| `_Pipeline.race` | [199](../src/parkour_solver/numerics/post_alarm.py#L199) | function |
| `_Pipeline.answer` | [239](../src/parkour_solver/numerics/post_alarm.py#L239) | function |
| `run` | [275](../src/parkour_solver/numerics/post_alarm.py#L275) | function |
| `facings` | [302](../src/parkour_solver/numerics/post_alarm.py#L302) | function |
| `sector_cuts` | [307](../src/parkour_solver/numerics/post_alarm.py#L307) | function |
| `sector_children` | [313](../src/parkour_solver/numerics/post_alarm.py#L313) | function |

## numerics/solver.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `_command` | [17](../src/parkour_solver/numerics/solver.py#L17) | function |
| `solve` | [21](../src/parkour_solver/numerics/solver.py#L21) | function |

## numerics/worker.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `send` | [22](../src/parkour_solver/numerics/worker.py#L22) | function |
| `forward` | [27](../src/parkour_solver/numerics/worker.py#L27) | function |
| `Monitor` | [58](../src/parkour_solver/numerics/worker.py#L58) | class |
| `Monitor.__init__` | [59](../src/parkour_solver/numerics/worker.py#L59) | function |
| `Monitor.get_n_in` | [70](../src/parkour_solver/numerics/worker.py#L70) | function |
| `Monitor.get_n_out` | [73](../src/parkour_solver/numerics/worker.py#L73) | function |
| `Monitor.get_name_in` | [76](../src/parkour_solver/numerics/worker.py#L76) | function |
| `Monitor.get_name_out` | [79](../src/parkour_solver/numerics/worker.py#L79) | function |
| `Monitor.get_sparsity_in` | [82](../src/parkour_solver/numerics/worker.py#L82) | function |
| `Monitor.record` | [90](../src/parkour_solver/numerics/worker.py#L90) | function |
| `Monitor.eval` | [107](../src/parkour_solver/numerics/worker.py#L107) | function |
| `fixed_phase` | [114](../src/parkour_solver/numerics/worker.py#L114) | function |
| `phase` | [243](../src/parkour_solver/numerics/worker.py#L243) | function |
| `main` | [292](../src/parkour_solver/numerics/worker.py#L292) | function |

## observability/turns.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `record` | [10](../src/parkour_solver/observability/turns.py#L10) | function |
| `replay` | [32](../src/parkour_solver/observability/turns.py#L32) | function |
| `summary` | [42](../src/parkour_solver/observability/turns.py#L42) | function |

## physics/angles.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `wrap` | [5](../src/parkour_solver/physics/angles.py#L5) | function |

## physics/coefficients.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `_flags` | [13](../src/parkour_solver/physics/coefficients.py#L13) | function |
| `_schedule` | [21](../src/parkour_solver/physics/coefficients.py#L21) | function |
| `_policy` | [32](../src/parkour_solver/physics/coefficients.py#L32) | function |
| `shared` | [40](../src/parkour_solver/physics/coefficients.py#L40) | function |
| `legacy_forward_bits` | [104](../src/parkour_solver/physics/coefficients.py#L104) | function |
| `legacy` | [109](../src/parkour_solver/physics/coefficients.py#L109) | function |
| `compile_request` | [129](../src/parkour_solver/physics/coefficients.py#L129) | function |

## physics/vertical.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `f32` | [5](../src/parkour_solver/physics/vertical.py#L5) | function |
| `clamp` | [17](../src/parkour_solver/physics/vertical.py#L17) | function |
| `branch` | [21](../src/parkour_solver/physics/vertical.py#L21) | function |
| `vertical_move` | [25](../src/parkour_solver/physics/vertical.py#L25) | function |
| `vertical_carry` | [37](../src/parkour_solver/physics/vertical.py#L37) | function |
| `vertical_step` | [43](../src/parkour_solver/physics/vertical.py#L43) | function |

## reachability/intervals.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `travel_radii` | [6](../src/parkour_solver/reachability/intervals.py#L6) | function |
| `motion_bound` | [15](../src/parkour_solver/reachability/intervals.py#L15) | function |
| `reachable_envelopes` | [28](../src/parkour_solver/reachability/intervals.py#L28) | function |
| `interaction_reachability` | [142](../src/parkour_solver/reachability/intervals.py#L142) | function |

## reachability/screen.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `condition_envelopes` | [6](../src/parkour_solver/reachability/screen.py#L6) | function |

## reachability/service.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `interaction_context_key` | [11](../src/parkour_solver/reachability/service.py#L11) | function |
| `query_interaction` | [26](../src/parkour_solver/reachability/service.py#L26) | function |
| `block_interaction` | [68](../src/parkour_solver/reachability/service.py#L68) | function |
| `interaction_allowed` | [73](../src/parkour_solver/reachability/service.py#L73) | function |
| `bounds_allowed` | [77](../src/parkour_solver/reachability/service.py#L77) | function |
| `behind_decision` | [85](../src/parkour_solver/reachability/service.py#L85) | function |
| `entry_backward` | [112](../src/parkour_solver/reachability/service.py#L112) | function |
| `walk_off_allowed` | [126](../src/parkour_solver/reachability/service.py#L126) | function |
| `candidate_faces` | [134](../src/parkour_solver/reachability/service.py#L134) | function |
| `candidate_option` | [146](../src/parkour_solver/reachability/service.py#L146) | function |
| `face_table` | [154](../src/parkour_solver/reachability/service.py#L154) | function |
| `walk_off_tick` | [222](../src/parkour_solver/reachability/service.py#L222) | function |
| `landing_boxes` | [232](../src/parkour_solver/reachability/service.py#L232) | function |
| `InteractionGate` | [241](../src/parkour_solver/reachability/service.py#L241) | class |
| `InteractionGate.__init__` | [243](../src/parkour_solver/reachability/service.py#L243) | function |
| `InteractionGate.reachable_faces` | [254](../src/parkour_solver/reachability/service.py#L254) | function |
| `InteractionGate._cached` | [265](../src/parkour_solver/reachability/service.py#L265) | function |
| `InteractionGate.prepare` | [278](../src/parkour_solver/reachability/service.py#L278) | function |
| `InteractionGate.analyze_model` | [295](../src/parkour_solver/reachability/service.py#L295) | function |
| `InteractionGate.analyze` | [326](../src/parkour_solver/reachability/service.py#L326) | function |
| `InteractionGate.condition` | [333](../src/parkour_solver/reachability/service.py#L333) | function |
| `InteractionGate.envelopes` | [341](../src/parkour_solver/reachability/service.py#L341) | function |
| `InteractionGate.sequence` | [345](../src/parkour_solver/reachability/service.py#L345) | function |
| `InteractionGate.check_program` | [358](../src/parkour_solver/reachability/service.py#L358) | function |
| `InteractionGate.relaxation` | [400](../src/parkour_solver/reachability/service.py#L400) | function |
| `InteractionGate.approach` | [417](../src/parkour_solver/reachability/service.py#L417) | function |
| `InteractionGate.query` | [452](../src/parkour_solver/reachability/service.py#L452) | function |
| `InteractionGate.walk_off` | [468](../src/parkour_solver/reachability/service.py#L468) | function |
| `InteractionGate.required` | [484](../src/parkour_solver/reachability/service.py#L484) | function |
| `InteractionGate.context` | [507](../src/parkour_solver/reachability/service.py#L507) | function |
| `InteractionGate.summary` | [510](../src/parkour_solver/reachability/service.py#L510) | function |

## reachability/viability.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `prefix_viability` | [8](../src/parkour_solver/reachability/viability.py#L8) | function |

## scene/geometry.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `thin` | [17](../src/parkour_solver/scene/geometry.py#L17) | function |
| `box_bounds` | [22](../src/parkour_solver/scene/geometry.py#L22) | function |
| `normalize_geometry` | [32](../src/parkour_solver/scene/geometry.py#L32) | function |
| `_ordered` | [99](../src/parkour_solver/scene/geometry.py#L99) | function |
| `_unordered` | [104](../src/parkour_solver/scene/geometry.py#L104) | function |
| `_first` | [108](../src/parkour_solver/scene/geometry.py#L108) | function |
| `support_center_interval` | [126](../src/parkour_solver/scene/geometry.py#L126) | function |
| `route_footprints` | [142](../src/parkour_solver/scene/geometry.py#L142) | function |
| `expanded_box` | [157](../src/parkour_solver/scene/geometry.py#L157) | function |

## scene/ladders.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `ladder_columns` | [6](../src/parkour_solver/scene/ladders.py#L6) | function |
| `_rectangular_regions` | [29](../src/parkour_solver/scene/ladders.py#L29) | function |
| `ladder_regions` | [53](../src/parkour_solver/scene/ladders.py#L53) | function |
| `region_rectangles` | [79](../src/parkour_solver/scene/ladders.py#L79) | function |
| `region_contains` | [84](../src/parkour_solver/scene/ladders.py#L84) | function |
| `region_axis` | [89](../src/parkour_solver/scene/ladders.py#L89) | function |
| `climb_walls` | [94](../src/parkour_solver/scene/ladders.py#L94) | function |
| `movement_contexts` | [114](../src/parkour_solver/scene/ladders.py#L114) | function |
| `entry_channels` | [122](../src/parkour_solver/scene/ladders.py#L122) | function |
| `outside_row` | [137](../src/parkour_solver/scene/ladders.py#L137) | function |
| `schedule_constraints` | [142](../src/parkour_solver/scene/ladders.py#L142) | function |
| `exit_faces` | [198](../src/parkour_solver/scene/ladders.py#L198) | function |

## scene/normalize.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `prepare_scene` | [9](../src/parkour_solver/scene/normalize.py#L9) | function |

## scene/objectives.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `objective_spec` | [4](../src/parkour_solver/scene/objectives.py#L4) | function |

## scene/route.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `prepare_route` | [12](../src/parkour_solver/scene/route.py#L12) | function |
| `supports` | [146](../src/parkour_solver/scene/route.py#L146) | function |
| `center_bounds` | [157](../src/parkour_solver/scene/route.py#L157) | function |
| `row_tick` | [171](../src/parkour_solver/scene/route.py#L171) | function |
| `schedule` | [175](../src/parkour_solver/scene/route.py#L175) | function |
| `initial_guess` | [336](../src/parkour_solver/scene/route.py#L336) | function |

## scene/static_collision.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `_number` | [20](../src/parkour_solver/scene/static_collision.py#L20) | function |
| `_box` | [26](../src/parkour_solver/scene/static_collision.py#L26) | function |
| `_cell` | [35](../src/parkour_solver/scene/static_collision.py#L35) | function |
| `_contains` | [44](../src/parkour_solver/scene/static_collision.py#L44) | function |
| `_offset` | [49](../src/parkour_solver/scene/static_collision.py#L49) | function |
| `_sweep` | [53](../src/parkour_solver/scene/static_collision.py#L53) | function |
| `_center` | [58](../src/parkour_solver/scene/static_collision.py#L58) | function |
| `_clip` | [62](../src/parkour_solver/scene/static_collision.py#L62) | function |
| `_edge_step` | [82](../src/parkour_solver/scene/static_collision.py#L82) | function |
| `Motion` | [88](../src/parkour_solver/scene/static_collision.py#L88) | class |
| `Motion.__init__` | [92](../src/parkour_solver/scene/static_collision.py#L92) | function |
| `StaticScene` | [99](../src/parkour_solver/scene/static_collision.py#L99) | class |
| `StaticScene.__init__` | [102](../src/parkour_solver/scene/static_collision.py#L102) | function |
| `StaticScene.ladder` | [130](../src/parkour_solver/scene/static_collision.py#L130) | function |
| `StaticScene.collisions` | [133](../src/parkour_solver/scene/static_collision.py#L133) | function |
| `StaticScene.start_box` | [139](../src/parkour_solver/scene/static_collision.py#L139) | function |
| `StaticScene.move` | [148](../src/parkour_solver/scene/static_collision.py#L148) | function |
| `_vector` | [200](../src/parkour_solver/scene/static_collision.py#L200) | function |
| `_member` | [209](../src/parkour_solver/scene/static_collision.py#L209) | function |
| `_same_face` | [219](../src/parkour_solver/scene/static_collision.py#L219) | function |
| `_matches_face` | [235](../src/parkour_solver/scene/static_collision.py#L235) | function |
| `_contact_mismatch` | [245](../src/parkour_solver/scene/static_collision.py#L245) | function |
| `check_scene` | [282](../src/parkour_solver/scene/static_collision.py#L282) | function |

## schema.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `number` | [12](../src/parkour_solver/schema.py#L12) | function |
| `integer` | [18](../src/parkour_solver/schema.py#L18) | function |

## search/anchors/adapter.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `AnchorAdapter` | [11](../src/parkour_solver/search/anchors/adapter.py#L11) | class |
| `AnchorAdapter.__init__` | [12](../src/parkour_solver/search/anchors/adapter.py#L12) | function |
| `AnchorAdapter.initial_cursor` | [19](../src/parkour_solver/search/anchors/adapter.py#L19) | function |
| `AnchorAdapter.group_end` | [28](../src/parkour_solver/search/anchors/adapter.py#L28) | function |
| `AnchorAdapter.next_goals` | [32](../src/parkour_solver/search/anchors/adapter.py#L32) | function |
| `AnchorAdapter.for_goal` | [56](../src/parkour_solver/search/anchors/adapter.py#L56) | function |
| `AnchorAdapter.for_node` | [70](../src/parkour_solver/search/anchors/adapter.py#L70) | function |
| `AnchorAdapter.decision_point` | [76](../src/parkour_solver/search/anchors/adapter.py#L76) | function |
| `AnchorAdapter.program_objective` | [92](../src/parkour_solver/search/anchors/adapter.py#L92) | function |
| `AnchorAdapter.advance` | [97](../src/parkour_solver/search/anchors/adapter.py#L97) | function |
| `AnchorAdapter.context` | [104](../src/parkour_solver/search/anchors/adapter.py#L104) | function |
| `AnchorAdapter.program_allowed` | [113](../src/parkour_solver/search/anchors/adapter.py#L113) | function |
| `AnchorAdapter.candidate` | [120](../src/parkour_solver/search/anchors/adapter.py#L120) | function |
| `AnchorAdapter.summary` | [132](../src/parkour_solver/search/anchors/adapter.py#L132) | function |

## search/anchors/policy.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `settings` | [15](../src/parkour_solver/search/anchors/policy.py#L15) | function |
| `semantic_settings` | [54](../src/parkour_solver/search/anchors/policy.py#L54) | function |
| `owner` | [59](../src/parkour_solver/search/anchors/policy.py#L59) | function |
| `box_owners` | [64](../src/parkour_solver/search/anchors/policy.py#L64) | function |
| `face_namer` | [68](../src/parkour_solver/search/anchors/policy.py#L68) | function |
| `support_face` | [95](../src/parkour_solver/search/anchors/policy.py#L95) | function |
| `terminal_face` | [103](../src/parkour_solver/search/anchors/policy.py#L103) | function |
| `visits` | [108](../src/parkour_solver/search/anchors/policy.py#L108) | function |
| `launch` | [135](../src/parkour_solver/search/anchors/policy.py#L135) | function |
| `feet_touches` | [144](../src/parkour_solver/search/anchors/policy.py#L144) | function |
| `visit_state` | [157](../src/parkour_solver/search/anchors/policy.py#L157) | function |
| `audit` | [184](../src/parkour_solver/search/anchors/policy.py#L184) | function |
| `next_anchor` | [205](../src/parkour_solver/search/anchors/policy.py#L205) | function |
| `advantageous_targets` | [215](../src/parkour_solver/search/anchors/policy.py#L215) | function |
| `advantageous_candidates` | [241](../src/parkour_solver/search/anchors/policy.py#L241) | function |
| `same_face_fit` | [344](../src/parkour_solver/search/anchors/policy.py#L344) | function |
| `encounter_groups` | [361](../src/parkour_solver/search/anchors/policy.py#L361) | function |
| `local_counts` | [411](../src/parkour_solver/search/anchors/policy.py#L411) | function |
| `priority` | [416](../src/parkour_solver/search/anchors/policy.py#L416) | function |
| `within_caps` | [420](../src/parkour_solver/search/anchors/policy.py#L420) | function |
| `transfer_structure` | [434](../src/parkour_solver/search/anchors/policy.py#L434) | function |

## search/application.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `task_settings` | [16](../src/parkour_solver/search/application.py#L16) | function |
| `run` | [23](../src/parkour_solver/search/application.py#L23) | function |

## search/bounds/flight.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `state_covers` | [21](../src/parkour_solver/search/bounds/flight.py#L21) | function |
| `transfer_bound` | [32](../src/parkour_solver/search/bounds/flight.py#L32) | function |
| `transfer_bounds` | [87](../src/parkour_solver/search/bounds/flight.py#L87) | function |

## search/bounds/remaining.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `cover_distance` | [9](../src/parkour_solver/search/bounds/remaining.py#L9) | function |
| `_vertical_remaining` | [14](../src/parkour_solver/search/bounds/remaining.py#L14) | function |
| `remaining_bound` | [46](../src/parkour_solver/search/bounds/remaining.py#L46) | function |

## search/comparison.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `comparison_label` | [11](../src/parkour_solver/search/comparison.py#L11) | function |
| `priority_key` | [38](../src/parkour_solver/search/comparison.py#L38) | function |
| `_count_protections` | [44](../src/parkour_solver/search/comparison.py#L44) | function |
| `measurements` | [53](../src/parkour_solver/search/comparison.py#L53) | function |
| `_clock_protections` | [61](../src/parkour_solver/search/comparison.py#L61) | function |
| `_clock_decision` | [90](../src/parkour_solver/search/comparison.py#L90) | function |
| `compare_candidates` | [107](../src/parkour_solver/search/comparison.py#L107) | function |

## search/deferrals.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `remember_peer` | [11](../src/parkour_solver/search/deferrals.py#L11) | function |
| `assess` | [20](../src/parkour_solver/search/deferrals.py#L20) | function |

## search/dfs.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `side_outcome` | [23](../src/parkour_solver/search/dfs.py#L23) | function |
| `candidate_key` | [30](../src/parkour_solver/search/dfs.py#L30) | function |
| `decision_family` | [35](../src/parkour_solver/search/dfs.py#L35) | function |
| `commit_arrival` | [46](../src/parkour_solver/search/dfs.py#L46) | function |
| `search_goals` | [71](../src/parkour_solver/search/dfs.py#L71) | function |

## search/domain.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `InitialJumpDomain` | [1](../src/parkour_solver/search/domain.py#L1) | class |
| `InitialJumpDomain.__init__` | [3](../src/parkour_solver/search/domain.py#L3) | function |
| `InitialJumpDomain.__call__` | [10](../src/parkour_solver/search/domain.py#L10) | function |

## search/engine.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `ScopedWork` | [17](../src/parkour_solver/search/engine.py#L17) | class |
| `ScopedWork.__init__` | [18](../src/parkour_solver/search/engine.py#L18) | function |
| `ScopedWork.deadline` | [23](../src/parkour_solver/search/engine.py#L23) | function |
| `ScopedWork.termination` | [27](../src/parkour_solver/search/engine.py#L27) | function |
| `ScopedWork.check` | [30](../src/parkour_solver/search/engine.py#L30) | function |
| `ScopedWork.proposal` | [34](../src/parkour_solver/search/engine.py#L34) | function |
| `ScopedWork.before_solve` | [38](../src/parkour_solver/search/engine.py#L38) | function |
| `ScopedWork.accepted` | [43](../src/parkour_solver/search/engine.py#L43) | function |
| `segment_solids` | [47](../src/parkour_solver/search/engine.py#L47) | function |
| `evaluate_program` | [61](../src/parkour_solver/search/engine.py#L61) | function |
| `numerical_counts` | [181](../src/parkour_solver/search/engine.py#L181) | function |
| `find_checkpoint_route` | [203](../src/parkour_solver/search/engine.py#L203) | function |

## search/events/adapter.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `EventReference` | [17](../src/parkour_solver/search/events/adapter.py#L17) | class |
| `EventReference.to_dict` | [22](../src/parkour_solver/search/events/adapter.py#L22) | function |
| `EventGuideAdapter` | [26](../src/parkour_solver/search/events/adapter.py#L26) | class |
| `EventGuideAdapter.__init__` | [29](../src/parkour_solver/search/events/adapter.py#L29) | function |
| `EventGuideAdapter.initial_cursor` | [43](../src/parkour_solver/search/events/adapter.py#L43) | function |
| `EventGuideAdapter.next_goals` | [46](../src/parkour_solver/search/events/adapter.py#L46) | function |
| `EventGuideAdapter.for_goal` | [105](../src/parkour_solver/search/events/adapter.py#L105) | function |
| `EventGuideAdapter.advance` | [123](../src/parkour_solver/search/events/adapter.py#L123) | function |
| `EventGuideAdapter.program_allowed` | [126](../src/parkour_solver/search/events/adapter.py#L126) | function |
| `EventGuideAdapter.scoped` | [129](../src/parkour_solver/search/events/adapter.py#L129) | function |
| `EventGuideAdapter.bind_program` | [133](../src/parkour_solver/search/events/adapter.py#L133) | function |
| `EventGuideAdapter.eligible` | [137](../src/parkour_solver/search/events/adapter.py#L137) | function |
| `EventGuideAdapter.candidate` | [140](../src/parkour_solver/search/events/adapter.py#L140) | function |
| `EventGuideAdapter.summary` | [167](../src/parkour_solver/search/events/adapter.py#L167) | function |

## search/events/engine.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `discovery_settings` | [23](../src/parkour_solver/search/events/engine.py#L23) | function |
| `search_event_routes` | [36](../src/parkour_solver/search/events/engine.py#L36) | function |

## search/feasibility.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `policy_spec` | [9](../src/parkour_solver/search/feasibility.py#L9) | function |
| `node_identity` | [27](../src/parkour_solver/search/feasibility.py#L27) | function |
| `outcome` | [34](../src/parkour_solver/search/feasibility.py#L34) | function |
| `meaningful_choice` | [61](../src/parkour_solver/search/feasibility.py#L61) | function |
| `solve_due` | [68](../src/parkour_solver/search/feasibility.py#L68) | function |
| `unresolved_only` | [72](../src/parkour_solver/search/feasibility.py#L72) | function |
| `restore_unknown_nodes` | [78](../src/parkour_solver/search/feasibility.py#L78) | function |

## search/frontier.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `take_first` | [13](../src/parkour_solver/search/frontier.py#L13) | function |
| `restore_alternatives` | [19](../src/parkour_solver/search/frontier.py#L19) | function |
| `persistent_programs` | [58](../src/parkour_solver/search/frontier.py#L58) | function |

## search/goals.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `goal_spec` | [7](../src/parkour_solver/search/goals.py#L7) | function |
| `goal_key` | [19](../src/parkour_solver/search/goals.py#L19) | function |
| `landing_satisfied` | [24](../src/parkour_solver/search/goals.py#L24) | function |
| `_landing_prefixes` | [29](../src/parkour_solver/search/goals.py#L29) | function |

## search/identity.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `digest` | [9](../src/parkour_solver/search/identity.py#L9) | function |
| `motion_record` | [13](../src/parkour_solver/search/identity.py#L13) | function |
| `processing_key` | [22](../src/parkour_solver/search/identity.py#L22) | function |
| `solve_scope` | [26](../src/parkour_solver/search/identity.py#L26) | function |
| `solve_identity` | [52](../src/parkour_solver/search/identity.py#L52) | function |
| `scoped_geometry` | [60](../src/parkour_solver/search/identity.py#L60) | function |
| `full_witness` | [65](../src/parkour_solver/search/identity.py#L65) | function |
| `validation_prefix` | [70](../src/parkour_solver/search/identity.py#L70) | function |

## search/movement.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `iter_movement_programs` | [1](../src/parkour_solver/search/movement.py#L1) | function |
| `screen_program` | [13](../src/parkour_solver/search/movement.py#L13) | function |

## search/ordered_route.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `search_allocations` | [14](../src/parkour_solver/search/ordered_route.py#L14) | function |
| `derive_footprints` | [128](../src/parkour_solver/search/ordered_route.py#L128) | function |
| `numerical_problem` | [145](../src/parkour_solver/search/ordered_route.py#L145) | function |
| `find_route` | [166](../src/parkour_solver/search/ordered_route.py#L166) | function |

## search/ranking.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `candidate_rank` | [7](../src/parkour_solver/search/ranking.py#L7) | function |
| `objective_seed` | [13](../src/parkour_solver/search/ranking.py#L13) | function |
| `passage_seed` | [28](../src/parkour_solver/search/ranking.py#L28) | function |

## search/repair.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `usable` | [23](../src/parkour_solver/search/repair.py#L23) | function |
| `node_alternatives` | [29](../src/parkour_solver/search/repair.py#L29) | function |
| `alarmed` | [50](../src/parkour_solver/search/repair.py#L50) | function |
| `branch_meta` | [61](../src/parkour_solver/search/repair.py#L61) | function |
| `solve_contact_branches` | [66](../src/parkour_solver/search/repair.py#L66) | function |

## search/repair_state.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `_rows` | [8](../src/parkour_solver/search/repair_state.py#L8) | function |
| `context` | [13](../src/parkour_solver/search/repair_state.py#L13) | function |
| `compatible` | [21](../src/parkour_solver/search/repair_state.py#L21) | function |
| `branch_key` | [52](../src/parkour_solver/search/repair_state.py#L52) | function |

## search/work.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `SearchWork` | [9](../src/parkour_solver/search/work.py#L9) | class |
| `SearchWork.__init__` | [10](../src/parkour_solver/search/work.py#L10) | function |
| `SearchWork.check` | [23](../src/parkour_solver/search/work.py#L23) | function |
| `SearchWork.program` | [31](../src/parkour_solver/search/work.py#L31) | function |
| `SearchWork.proposal` | [42](../src/parkour_solver/search/work.py#L42) | function |
| `SearchWork.before_solve` | [47](../src/parkour_solver/search/work.py#L47) | function |
| `SearchWork.accepted` | [51](../src/parkour_solver/search/work.py#L51) | function |
| `SearchWork.summary` | [56](../src/parkour_solver/search/work.py#L56) | function |

## spatial/anchor_guides.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `nearest` | [9](../src/parkour_solver/spatial/anchor_guides.py#L9) | function |
| `landing_cover` | [16](../src/parkour_solver/spatial/anchor_guides.py#L16) | function |
| `terminal_point` | [23](../src/parkour_solver/spatial/anchor_guides.py#L23) | function |
| `unit` | [40](../src/parkour_solver/spatial/anchor_guides.py#L40) | function |
| `leg_class` | [49](../src/parkour_solver/spatial/anchor_guides.py#L49) | function |
| `build_guides` | [62](../src/parkour_solver/spatial/anchor_guides.py#L62) | function |
| `classify` | [96](../src/parkour_solver/spatial/anchor_guides.py#L96) | function |
| `decision_objective` | [108](../src/parkour_solver/spatial/anchor_guides.py#L108) | function |
| `onward_objective` | [129](../src/parkour_solver/spatial/anchor_guides.py#L129) | function |

## spatial/cells.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `dimension` | [9](../src/parkour_solver/spatial/cells.py#L9) | function |
| `contains` | [13](../src/parkour_solver/spatial/cells.py#L13) | function |
| `intersection` | [17](../src/parkour_solver/spatial/cells.py#L17) | function |
| `subtract_open` | [22](../src/parkour_solver/spatial/cells.py#L22) | function |
| `merge_boxes` | [44](../src/parkour_solver/spatial/cells.py#L44) | function |
| `union_subtract` | [70](../src/parkour_solver/spatial/cells.py#L70) | function |
| `Cell` | [82](../src/parkour_solver/spatial/cells.py#L82) | class |
| `Portal` | [89](../src/parkour_solver/spatial/cells.py#L89) | class |
| `ContactPatch` | [97](../src/parkour_solver/spatial/cells.py#L97) | class |
| `Decomposition` | [108](../src/parkour_solver/spatial/cells.py#L108) | class |
| `Decomposition.to_dict` | [117](../src/parkour_solver/spatial/cells.py#L117) | function |
| `cell_portals` | [121](../src/parkour_solver/spatial/cells.py#L121) | function |
| `contact_patches` | [133](../src/parkour_solver/spatial/cells.py#L133) | function |
| `refine_support_cells` | [149](../src/parkour_solver/spatial/cells.py#L149) | function |
| `decompose` | [187](../src/parkour_solver/spatial/cells.py#L187) | function |
| `box_segment_interval` | [239](../src/parkour_solver/spatial/cells.py#L239) | function |

## spatial/checkpoints.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `horizontal_segments` | [5](../src/parkour_solver/spatial/checkpoints.py#L5) | function |
| `direction_objective` | [20](../src/parkour_solver/spatial/checkpoints.py#L20) | function |
| `retain_prefix` | [25](../src/parkour_solver/spatial/checkpoints.py#L25) | function |

## spatial/event_routes.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `_json` | [22](../src/parkour_solver/spatial/event_routes.py#L22) | function |
| `SurfaceGroup` | [27](../src/parkour_solver/spatial/event_routes.py#L27) | class |
| `surface_groups` | [36](../src/parkour_solver/spatial/event_routes.py#L36) | function |
| `RouteDomain` | [76](../src/parkour_solver/spatial/event_routes.py#L76) | class |
| `RouteDomain.id` | [85](../src/parkour_solver/spatial/event_routes.py#L85) | function |
| `RouteDomain.to_dict` | [88](../src/parkour_solver/spatial/event_routes.py#L88) | function |
| `RouteDomain.from_dict` | [92](../src/parkour_solver/spatial/event_routes.py#L92) | function |
| `build_route_domain` | [102](../src/parkour_solver/spatial/event_routes.py#L102) | function |
| `support_episodes` | [135](../src/parkour_solver/spatial/event_routes.py#L135) | function |
| `build_event_graph` | [172](../src/parkour_solver/spatial/event_routes.py#L172) | function |
| `initial_cursor` | [201](../src/parkour_solver/spatial/event_routes.py#L201) | function |
| `next_goals` | [219](../src/parkour_solver/spatial/event_routes.py#L219) | function |
| `semantic_prefix` | [283](../src/parkour_solver/spatial/event_routes.py#L283) | function |
| `advance_cursor` | [296](../src/parkour_solver/spatial/event_routes.py#L296) | function |
| `observed_route` | [308](../src/parkour_solver/spatial/event_routes.py#L308) | function |

## spatial/guides.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `conditioned_anchor` | [11](../src/parkour_solver/spatial/guides.py#L11) | function |
| `task_reference` | [18](../src/parkour_solver/spatial/guides.py#L18) | function |
| `segment_in_cover` | [25](../src/parkour_solver/spatial/guides.py#L25) | function |
| `guide_for` | [35](../src/parkour_solver/spatial/guides.py#L35) | function |
| `outgoing_objective` | [109](../src/parkour_solver/spatial/guides.py#L109) | function |

## spatial/regions.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `geometry_key` | [19](../src/parkour_solver/spatial/regions.py#L19) | function |
| `canonical_cover` | [25](../src/parkour_solver/spatial/regions.py#L25) | function |
| `SupportComponent` | [59](../src/parkour_solver/spatial/regions.py#L59) | class |
| `support_components` | [69](../src/parkour_solver/spatial/regions.py#L69) | function |
| `RouteRegion` | [98](../src/parkour_solver/spatial/regions.py#L98) | class |
| `RouteTransition` | [110](../src/parkour_solver/spatial/regions.py#L110) | class |
| `RouteGraph` | [124](../src/parkour_solver/spatial/regions.py#L124) | class |
| `RouteGraph.to_dict` | [135](../src/parkour_solver/spatial/regions.py#L135) | function |
| `build_geometry_graph` | [138](../src/parkour_solver/spatial/regions.py#L138) | function |
| `_incidence_graph` | [150](../src/parkour_solver/spatial/regions.py#L150) | function |

## spatial/surfaces.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `_embed` | [23](../src/parkour_solver/spatial/surfaces.py#L23) | function |
| `_project` | [27](../src/parkour_solver/spatial/surfaces.py#L27) | function |
| `_cover` | [31](../src/parkour_solver/spatial/surfaces.py#L31) | function |
| `_overlap` | [35](../src/parkour_solver/spatial/surfaces.py#L35) | function |
| `_adjacent` | [42](../src/parkour_solver/spatial/surfaces.py#L42) | function |
| `_check` | [47](../src/parkour_solver/spatial/surfaces.py#L47) | function |
| `_components` | [52](../src/parkour_solver/spatial/surfaces.py#L52) | function |
| `Cluster` | [77](../src/parkour_solver/spatial/surfaces.py#L77) | class |
| `Side` | [85](../src/parkour_solver/spatial/surfaces.py#L85) | class |
| `Side.key` | [91](../src/parkour_solver/spatial/surfaces.py#L91) | function |
| `nested` | [95](../src/parkour_solver/spatial/surfaces.py#L95) | function |
| `intersects` | [102](../src/parkour_solver/spatial/surfaces.py#L102) | function |
| `free_rectangles` | [106](../src/parkour_solver/spatial/surfaces.py#L106) | function |
| `SceneGeometry` | [133](../src/parkour_solver/spatial/surfaces.py#L133) | class |
| `SceneGeometry.__init__` | [136](../src/parkour_solver/spatial/surfaces.py#L136) | function |
| `SceneGeometry.near` | [147](../src/parkour_solver/spatial/surfaces.py#L147) | function |
| `SceneGeometry._cells` | [158](../src/parkour_solver/spatial/surfaces.py#L158) | function |
| `SceneGeometry.clusters` | [163](../src/parkour_solver/spatial/surfaces.py#L163) | function |
| `SceneGeometry.cluster` | [166](../src/parkour_solver/spatial/surfaces.py#L166) | function |
| `SceneGeometry.touching` | [169](../src/parkour_solver/spatial/surfaces.py#L169) | function |
| `SceneGeometry._cluster_map` | [177](../src/parkour_solver/spatial/surfaces.py#L177) | function |
| `SceneGeometry.active` | [199](../src/parkour_solver/spatial/surfaces.py#L199) | function |
| `SceneGeometry.sides` | [202](../src/parkour_solver/spatial/surfaces.py#L202) | function |
| `SceneGeometry.contact_options` | [222](../src/parkour_solver/spatial/surfaces.py#L222) | function |
| `ContactOption` | [263](../src/parkour_solver/spatial/surfaces.py#L263) | class |
| `_maximal_rectangles` | [271](../src/parkour_solver/spatial/surfaces.py#L271) | function |
| `_erode` | [297](../src/parkour_solver/spatial/surfaces.py#L297) | function |
| `_scene_geometry` | [304](../src/parkour_solver/spatial/surfaces.py#L304) | function |
| `scene_geometry` | [308](../src/parkour_solver/spatial/surfaces.py#L308) | function |

## validation/numerical.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `validate` | [8](../src/parkour_solver/validation/numerical.py#L8) | function |
| `validate_controls` | [21](../src/parkour_solver/validation/numerical.py#L21) | function |
| `_replay` | [43](../src/parkour_solver/validation/numerical.py#L43) | function |

## validation/scene.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `check_geometry` | [5](../src/parkour_solver/validation/scene.py#L5) | function |

## validation/vertical.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `validate_vertical_schedule` | [6](../src/parkour_solver/validation/vertical.py#L6) | function |

## validation/witness.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `replay_witness` | [11](../src/parkour_solver/validation/witness.py#L11) | function |
| `witnesses` | [50](../src/parkour_solver/validation/witness.py#L50) | function |
