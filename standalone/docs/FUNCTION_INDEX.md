# Production function index

Generated from the current installed-source tree by `python -m tools.docs_index`.
Line numbers are a snapshot; symbol names are the stable lookup keys. Private helpers remain private.

## bridge/compiler.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `open_compiler` | [9](../src/parkour_solver/bridge/compiler.py#L9) | function |
| `coefficient_request` | [18](../src/parkour_solver/bridge/compiler.py#L18) | function |
| `JavaCompiler` | [29](../src/parkour_solver/bridge/compiler.py#L29) | class |
| `JavaCompiler.__init__` | [33](../src/parkour_solver/bridge/compiler.py#L33) | function |
| `JavaCompiler._start` | [41](../src/parkour_solver/bridge/compiler.py#L41) | function |
| `JavaCompiler._errors` | [48](../src/parkour_solver/bridge/compiler.py#L48) | function |
| `JavaCompiler.compile` | [56](../src/parkour_solver/bridge/compiler.py#L56) | function |
| `JavaCompiler.provenance` | [59](../src/parkour_solver/bridge/compiler.py#L59) | function |
| `JavaCompiler.derive_footprints` | [62](../src/parkour_solver/bridge/compiler.py#L62) | function |
| `JavaCompiler.derive_standing_footprints` | [65](../src/parkour_solver/bridge/compiler.py#L65) | function |
| `JavaCompiler.check_scene` | [68](../src/parkour_solver/bridge/compiler.py#L68) | function |
| `JavaCompiler._exchange` | [74](../src/parkour_solver/bridge/compiler.py#L74) | function |
| `JavaCompiler.close` | [103](../src/parkour_solver/bridge/compiler.py#L103) | function |
| `JavaCompiler.__enter__` | [115](../src/parkour_solver/bridge/compiler.py#L115) | function |
| `JavaCompiler.__exit__` | [118](../src/parkour_solver/bridge/compiler.py#L118) | function |

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
| `PythonCompiler.__init__` | [22](../src/parkour_solver/bridge/python_compiler.py#L22) | function |
| `PythonCompiler.compile` | [26](../src/parkour_solver/bridge/python_compiler.py#L26) | function |
| `PythonCompiler.derive_footprints` | [29](../src/parkour_solver/bridge/python_compiler.py#L29) | function |
| `PythonCompiler.derive_standing_footprints` | [32](../src/parkour_solver/bridge/python_compiler.py#L32) | function |
| `PythonCompiler.check_scene` | [35](../src/parkour_solver/bridge/python_compiler.py#L35) | function |
| `PythonCompiler._exchange` | [41](../src/parkour_solver/bridge/python_compiler.py#L41) | function |
| `PythonCompiler.provenance` | [61](../src/parkour_solver/bridge/python_compiler.py#L61) | function |
| `PythonCompiler.close` | [65](../src/parkour_solver/bridge/python_compiler.py#L65) | function |
| `PythonCompiler.__enter__` | [68](../src/parkour_solver/bridge/python_compiler.py#L68) | function |
| `PythonCompiler.__exit__` | [71](../src/parkour_solver/bridge/python_compiler.py#L71) | function |

## canonical.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `digest` | [11](../src/parkour_solver/canonical.py#L11) | function |

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
| `consecutive` | [9](../src/parkour_solver/constraints/avoidance.py#L9) | function |
| `phase_states` | [13](../src/parkour_solver/constraints/avoidance.py#L13) | function |
| `outside` | [22](../src/parkour_solver/constraints/avoidance.py#L22) | function |
| `passage_kind` | [26](../src/parkour_solver/constraints/avoidance.py#L26) | function |
| `blockage_counts` | [31](../src/parkour_solver/constraints/avoidance.py#L31) | function |
| `block_key` | [36](../src/parkour_solver/constraints/avoidance.py#L36) | function |
| `blockage_usage` | [40](../src/parkour_solver/constraints/avoidance.py#L40) | function |
| `gate_timeline` | [49](../src/parkour_solver/constraints/avoidance.py#L49) | function |
| `intervals` | [81](../src/parkour_solver/constraints/avoidance.py#L81) | function |
| `validate_passages` | [95](../src/parkour_solver/constraints/avoidance.py#L95) | function |
| `face_clearance` | [122](../src/parkour_solver/constraints/avoidance.py#L122) | function |
| `compile_passages` | [135](../src/parkour_solver/constraints/avoidance.py#L135) | function |
| `passage_y_valid` | [158](../src/parkour_solver/constraints/avoidance.py#L158) | function |

## constraints/contacts.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `exposed_tops` | [8](../src/parkour_solver/constraints/contacts.py#L8) | function |
| `floor_checks` | [17](../src/parkour_solver/constraints/contacts.py#L17) | function |
| `walk_off_faces` | [62](../src/parkour_solver/constraints/contacts.py#L62) | function |
| `contact_conflicts` | [161](../src/parkour_solver/constraints/contacts.py#L161) | function |
| `avoid_contact_rows` | [172](../src/parkour_solver/constraints/contacts.py#L172) | function |

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
| `json_key` | [226](../src/parkour_solver/constraints/encounters.py#L226) | function |
| `_continuation` | [230](../src/parkour_solver/constraints/encounters.py#L230) | function |
| `describe` | [249](../src/parkour_solver/constraints/encounters.py#L249) | function |
| `encounter_option` | [282](../src/parkour_solver/constraints/encounters.py#L282) | function |

## constraints/hit_events.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `validate_hits` | [9](../src/parkour_solver/constraints/hit_events.py#L9) | function |
| `compile_hits` | [64](../src/parkour_solver/constraints/hit_events.py#L64) | function |
| `wall_hit_options` | [109](../src/parkour_solver/constraints/hit_events.py#L109) | function |
| `float_height` | [144](../src/parkour_solver/constraints/hit_events.py#L144) | function |
| `hit_seed` | [149](../src/parkour_solver/constraints/hit_events.py#L149) | function |

## constraints/variants.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `ConstraintVariants` | [20](../src/parkour_solver/constraints/variants.py#L20) | class |
| `ConstraintVariants.for_plan` | [22](../src/parkour_solver/constraints/variants.py#L22) | function |
| `ConstraintVariants.__init__` | [50](../src/parkour_solver/constraints/variants.py#L50) | function |
| `ConstraintVariants.conflict_driven` | [145](../src/parkour_solver/constraints/variants.py#L145) | function |
| `ConstraintVariants.obstacle_key` | [148](../src/parkour_solver/constraints/variants.py#L148) | function |
| `ConstraintVariants.with_obstacles` | [158](../src/parkour_solver/constraints/variants.py#L158) | function |
| `ConstraintVariants.metric` | [162](../src/parkour_solver/constraints/variants.py#L162) | function |
| `ConstraintVariants.coordinate` | [166](../src/parkour_solver/constraints/variants.py#L166) | function |
| `ConstraintVariants.check` | [171](../src/parkour_solver/constraints/variants.py#L171) | function |
| `ConstraintVariants.face_row` | [179](../src/parkour_solver/constraints/variants.py#L179) | function |
| `ConstraintVariants.effective_key` | [184](../src/parkour_solver/constraints/variants.py#L184) | function |
| `ConstraintVariants.analyze` | [196](../src/parkour_solver/constraints/variants.py#L196) | function |
| `ConstraintVariants.excluded` | [214](../src/parkour_solver/constraints/variants.py#L214) | function |
| `ConstraintVariants.prepare_item` | [221](../src/parkour_solver/constraints/variants.py#L221) | function |
| `ConstraintVariants.option` | [243](../src/parkour_solver/constraints/variants.py#L243) | function |
| `ConstraintVariants.__iter__` | [255](../src/parkour_solver/constraints/variants.py#L255) | function |
| `ConstraintVariants.__next__` | [258](../src/parkour_solver/constraints/variants.py#L258) | function |
| `ConstraintVariants.choose_item` | [266](../src/parkour_solver/constraints/variants.py#L266) | function |
| `ConstraintVariants.branch_choice` | [294](../src/parkour_solver/constraints/variants.py#L294) | function |
| `ConstraintVariants._next_variant` | [312](../src/parkour_solver/constraints/variants.py#L312) | function |

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
| `side_run` | [70](../src/parkour_solver/movement/cases.py#L70) | function |
| `corner_rows` | [86](../src/parkour_solver/movement/cases.py#L86) | function |
| `case_profile` | [97](../src/parkour_solver/movement/cases.py#L97) | function |
| `landing_wall_options` | [130](../src/parkour_solver/movement/cases.py#L130) | function |
| `entry_possible` | [147](../src/parkour_solver/movement/cases.py#L147) | function |

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
| `contact_clearance` | [86](../src/parkour_solver/movement/contact_combinations.py#L86) | function |
| `expanded_face` | [90](../src/parkour_solver/movement/contact_combinations.py#L90) | function |
| `scene_contact_options` | [97](../src/parkour_solver/movement/contact_combinations.py#L97) | function |
| `top_faces` | [105](../src/parkour_solver/movement/contact_combinations.py#L105) | function |
| `solid_owners` | [110](../src/parkour_solver/movement/contact_combinations.py#L110) | function |
| `face_name` | [118](../src/parkour_solver/movement/contact_combinations.py#L118) | function |
| `face_owners` | [124](../src/parkour_solver/movement/contact_combinations.py#L124) | function |
| `contact_event` | [136](../src/parkour_solver/movement/contact_combinations.py#L136) | function |
| `region_choices` | [142](../src/parkour_solver/movement/contact_combinations.py#L142) | function |
| `reachable_option` | [152](../src/parkour_solver/movement/contact_combinations.py#L152) | function |
| `bounding_region` | [161](../src/parkour_solver/movement/contact_combinations.py#L161) | function |
| `wall_heights` | [165](../src/parkour_solver/movement/contact_combinations.py#L165) | function |
| `region_offset` | [170](../src/parkour_solver/movement/contact_combinations.py#L170) | function |
| `finite_rows` | [175](../src/parkour_solver/movement/contact_combinations.py#L175) | function |
| `compile_contact_events` | [181](../src/parkour_solver/movement/contact_combinations.py#L181) | function |
| `contact_combinations` | [212](../src/parkour_solver/movement/contact_combinations.py#L212) | function |

## movement/contact_profiles.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `contact_profile` | [8](../src/parkour_solver/movement/contact_profiles.py#L8) | function |
| `contact_opportunities` | [51](../src/parkour_solver/movement/contact_profiles.py#L51) | function |
| `generate_contact_profiles` | [66](../src/parkour_solver/movement/contact_profiles.py#L66) | function |

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
| `climb_contacts` | [11](../src/parkour_solver/movement/ladder/contacts.py#L11) | function |
| `with_contacts` | [27](../src/parkour_solver/movement/ladder/contacts.py#L27) | function |
| `mixed_ladder_schedules` | [69](../src/parkour_solver/movement/ladder/contacts.py#L69) | function |

## movement/ladder/episodes.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `compose_ladder_episodes` | [11](../src/parkour_solver/movement/ladder/episodes.py#L11) | function |

## movement/ladder/events.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `validate_ladder_events` | [5](../src/parkour_solver/movement/ladder/events.py#L5) | function |
| `compile_ladder_events` | [56](../src/parkour_solver/movement/ladder/events.py#L56) | function |

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
| `box_for` | [5](../src/parkour_solver/movement/stepping.py#L5) | function |
| `SteppingGenerator` | [9](../src/parkour_solver/movement/stepping.py#L9) | class |
| `SteppingGenerator.generate` | [10](../src/parkour_solver/movement/stepping.py#L10) | function |
| `_sequence_allowed` | [13](../src/parkour_solver/movement/stepping.py#L13) | function |
| `_fresh` | [24](../src/parkour_solver/movement/stepping.py#L24) | function |
| `open_stepping_choices` | [34](../src/parkour_solver/movement/stepping.py#L34) | function |
| `_open_flight_cases` | [113](../src/parkour_solver/movement/stepping.py#L113) | function |

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

## native_core.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `load` | [11](../src/parkour_solver/native_core.py#L11) | function |
| `from_environment` | [20](../src/parkour_solver/native_core.py#L20) | function |

## numerics/builders.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `waypoint_seed` | [7](../src/parkour_solver/numerics/builders.py#L7) | function |
| `ladder_waypoints` | [24](../src/parkour_solver/numerics/builders.py#L24) | function |
| `numerical_problem` | [76](../src/parkour_solver/numerics/builders.py#L76) | function |

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
| `_native` | [32](../src/parkour_solver/numerics/disk.py#L32) | function |
| `spec` | [40](../src/parkour_solver/numerics/disk.py#L40) | function |
| `solve` | [72](../src/parkour_solver/numerics/disk.py#L72) | function |
| `screen` | [97](../src/parkour_solver/numerics/disk.py#L97) | function |
| `deficits` | [112](../src/parkour_solver/numerics/disk.py#L112) | function |
| `ipopt_seed` | [122](../src/parkour_solver/numerics/disk.py#L122) | function |
| `alarm_ticks` | [137](../src/parkour_solver/numerics/disk.py#L137) | function |
| `RelaxedDispatch` | [151](../src/parkour_solver/numerics/disk.py#L151) | class |
| `RelaxedDispatch.__init__` | [163](../src/parkour_solver/numerics/disk.py#L163) | function |
| `RelaxedDispatch.solve` | [166](../src/parkour_solver/numerics/disk.py#L166) | function |
| `RelaxedDispatch.confirmed` | [169](../src/parkour_solver/numerics/disk.py#L169) | function |
| `RelaxedDispatch._dispatch` | [172](../src/parkour_solver/numerics/disk.py#L172) | function |
| `RelaxedDispatch.confirm` | [209](../src/parkour_solver/numerics/disk.py#L209) | function |
| `_result` | [236](../src/parkour_solver/numerics/disk.py#L236) | function |
| `_attempt` | [252](../src/parkour_solver/numerics/disk.py#L252) | function |
| `_solve` | [260](../src/parkour_solver/numerics/disk.py#L260) | function |
| `_cost` | [363](../src/parkour_solver/numerics/disk.py#L363) | function |

## numerics/model.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `_native` | [7](../src/parkour_solver/numerics/model.py#L7) | function |
| `prepare` | [13](../src/parkour_solver/numerics/model.py#L13) | function |
| `fingerprint` | [196](../src/parkour_solver/numerics/model.py#L196) | function |

## numerics/solver.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `_command` | [17](../src/parkour_solver/numerics/solver.py#L17) | function |
| `solve` | [25](../src/parkour_solver/numerics/solver.py#L25) | function |

## numerics/worker.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `send` | [20](../src/parkour_solver/numerics/worker.py#L20) | function |
| `forward` | [25](../src/parkour_solver/numerics/worker.py#L25) | function |
| `Monitor` | [56](../src/parkour_solver/numerics/worker.py#L56) | class |
| `Monitor.__init__` | [57](../src/parkour_solver/numerics/worker.py#L57) | function |
| `Monitor.get_n_in` | [68](../src/parkour_solver/numerics/worker.py#L68) | function |
| `Monitor.get_n_out` | [71](../src/parkour_solver/numerics/worker.py#L71) | function |
| `Monitor.get_name_in` | [74](../src/parkour_solver/numerics/worker.py#L74) | function |
| `Monitor.get_name_out` | [77](../src/parkour_solver/numerics/worker.py#L77) | function |
| `Monitor.get_sparsity_in` | [80](../src/parkour_solver/numerics/worker.py#L80) | function |
| `Monitor.record` | [88](../src/parkour_solver/numerics/worker.py#L88) | function |
| `Monitor.eval` | [105](../src/parkour_solver/numerics/worker.py#L105) | function |
| `fixed_phase` | [112](../src/parkour_solver/numerics/worker.py#L112) | function |
| `phase` | [241](../src/parkour_solver/numerics/worker.py#L241) | function |
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
| `_native` | [142](../src/parkour_solver/reachability/intervals.py#L142) | function |
| `interaction_reachability` | [148](../src/parkour_solver/reachability/intervals.py#L148) | function |
| `interaction_reachability_many` | [214](../src/parkour_solver/reachability/intervals.py#L214) | function |

## reachability/screen.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `condition_envelopes` | [6](../src/parkour_solver/reachability/screen.py#L6) | function |
| `contradiction` | [42](../src/parkour_solver/reachability/screen.py#L42) | function |

## reachability/service.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `interaction_context_key` | [11](../src/parkour_solver/reachability/service.py#L11) | function |
| `analysis_key` | [26](../src/parkour_solver/reachability/service.py#L26) | function |
| `query_interaction` | [32](../src/parkour_solver/reachability/service.py#L32) | function |
| `block_interaction` | [74](../src/parkour_solver/reachability/service.py#L74) | function |
| `interaction_allowed` | [79](../src/parkour_solver/reachability/service.py#L79) | function |
| `bounds_allowed` | [83](../src/parkour_solver/reachability/service.py#L83) | function |
| `bounds_allowed_many` | [91](../src/parkour_solver/reachability/service.py#L91) | function |
| `behind_decision` | [101](../src/parkour_solver/reachability/service.py#L101) | function |
| `entry_backward` | [128](../src/parkour_solver/reachability/service.py#L128) | function |
| `walk_off_allowed` | [142](../src/parkour_solver/reachability/service.py#L142) | function |
| `candidate_faces` | [150](../src/parkour_solver/reachability/service.py#L150) | function |
| `candidate_option` | [162](../src/parkour_solver/reachability/service.py#L162) | function |
| `face_table` | [170](../src/parkour_solver/reachability/service.py#L170) | function |
| `walk_off_tick` | [238](../src/parkour_solver/reachability/service.py#L238) | function |
| `landing_boxes` | [248](../src/parkour_solver/reachability/service.py#L248) | function |
| `InteractionGate` | [257](../src/parkour_solver/reachability/service.py#L257) | class |
| `InteractionGate.__init__` | [259](../src/parkour_solver/reachability/service.py#L259) | function |
| `InteractionGate.reachable_faces` | [273](../src/parkour_solver/reachability/service.py#L273) | function |
| `InteractionGate._cached` | [284](../src/parkour_solver/reachability/service.py#L284) | function |
| `InteractionGate.prepare` | [297](../src/parkour_solver/reachability/service.py#L297) | function |
| `InteractionGate._prepared` | [301](../src/parkour_solver/reachability/service.py#L301) | function |
| `InteractionGate.analyze_model` | [325](../src/parkour_solver/reachability/service.py#L325) | function |
| `InteractionGate.analyze` | [356](../src/parkour_solver/reachability/service.py#L356) | function |
| `InteractionGate._analysis` | [361](../src/parkour_solver/reachability/service.py#L361) | function |
| `InteractionGate.condition` | [381](../src/parkour_solver/reachability/service.py#L381) | function |
| `InteractionGate.envelopes` | [389](../src/parkour_solver/reachability/service.py#L389) | function |
| `InteractionGate.sequence` | [393](../src/parkour_solver/reachability/service.py#L393) | function |
| `InteractionGate._sequenced` | [405](../src/parkour_solver/reachability/service.py#L405) | function |
| `InteractionGate.check_program` | [425](../src/parkour_solver/reachability/service.py#L425) | function |
| `InteractionGate.relaxation` | [475](../src/parkour_solver/reachability/service.py#L475) | function |
| `InteractionGate.approach` | [493](../src/parkour_solver/reachability/service.py#L493) | function |
| `InteractionGate.query` | [528](../src/parkour_solver/reachability/service.py#L528) | function |
| `InteractionGate.query_many` | [531](../src/parkour_solver/reachability/service.py#L531) | function |
| `InteractionGate.walk_off` | [562](../src/parkour_solver/reachability/service.py#L562) | function |
| `InteractionGate.required` | [578](../src/parkour_solver/reachability/service.py#L578) | function |
| `InteractionGate.context` | [603](../src/parkour_solver/reachability/service.py#L603) | function |
| `InteractionGate.summary` | [606](../src/parkour_solver/reachability/service.py#L606) | function |

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
| `ladder_columns` | [5](../src/parkour_solver/scene/ladders.py#L5) | function |
| `_rectangular_regions` | [28](../src/parkour_solver/scene/ladders.py#L28) | function |
| `ladder_regions` | [52](../src/parkour_solver/scene/ladders.py#L52) | function |
| `region_rectangles` | [78](../src/parkour_solver/scene/ladders.py#L78) | function |
| `region_contains` | [83](../src/parkour_solver/scene/ladders.py#L83) | function |
| `region_axis` | [88](../src/parkour_solver/scene/ladders.py#L88) | function |
| `climb_walls` | [93](../src/parkour_solver/scene/ladders.py#L93) | function |
| `movement_contexts` | [113](../src/parkour_solver/scene/ladders.py#L113) | function |
| `entry_channels` | [121](../src/parkour_solver/scene/ladders.py#L121) | function |
| `outside_row` | [136](../src/parkour_solver/scene/ladders.py#L136) | function |
| `schedule_constraints` | [141](../src/parkour_solver/scene/ladders.py#L141) | function |
| `exit_faces` | [197](../src/parkour_solver/scene/ladders.py#L197) | function |

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
| `_native` | [12](../src/parkour_solver/scene/route.py#L12) | function |
| `prepare_route` | [18](../src/parkour_solver/scene/route.py#L18) | function |
| `supports` | [161](../src/parkour_solver/scene/route.py#L161) | function |
| `center_bounds` | [172](../src/parkour_solver/scene/route.py#L172) | function |
| `row_tick` | [186](../src/parkour_solver/scene/route.py#L186) | function |
| `schedule` | [190](../src/parkour_solver/scene/route.py#L190) | function |
| `initial_guess` | [359](../src/parkour_solver/scene/route.py#L359) | function |
| `derive_footprints` | [390](../src/parkour_solver/scene/route.py#L390) | function |

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
| `number` | [7](../src/parkour_solver/schema.py#L7) | function |
| `integer` | [13](../src/parkour_solver/schema.py#L13) | function |

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
| `same_face_fit` | [331](../src/parkour_solver/search/anchors/policy.py#L331) | function |
| `encounter_groups` | [352](../src/parkour_solver/search/anchors/policy.py#L352) | function |
| `local_counts` | [402](../src/parkour_solver/search/anchors/policy.py#L402) | function |
| `priority` | [407](../src/parkour_solver/search/anchors/policy.py#L407) | function |
| `within_caps` | [411](../src/parkour_solver/search/anchors/policy.py#L411) | function |
| `transfer_structure` | [425](../src/parkour_solver/search/anchors/policy.py#L425) | function |

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
| `candidate_key` | [19](../src/parkour_solver/search/dfs.py#L19) | function |
| `decision_family` | [24](../src/parkour_solver/search/dfs.py#L24) | function |
| `commit_arrival` | [35](../src/parkour_solver/search/dfs.py#L35) | function |
| `search_goals` | [60](../src/parkour_solver/search/dfs.py#L60) | function |

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
| `numerical_counts` | [180](../src/parkour_solver/search/engine.py#L180) | function |
| `find_checkpoint_route` | [200](../src/parkour_solver/search/engine.py#L200) | function |

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
| `take_first` | [14](../src/parkour_solver/search/frontier.py#L14) | function |
| `restore_alternatives` | [20](../src/parkour_solver/search/frontier.py#L20) | function |
| `persistent_programs` | [56](../src/parkour_solver/search/frontier.py#L56) | function |

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
| `motion_record` | [8](../src/parkour_solver/search/identity.py#L8) | function |
| `processing_key` | [17](../src/parkour_solver/search/identity.py#L17) | function |
| `solve_scope` | [21](../src/parkour_solver/search/identity.py#L21) | function |
| `solve_identity` | [47](../src/parkour_solver/search/identity.py#L47) | function |
| `scoped_geometry` | [55](../src/parkour_solver/search/identity.py#L55) | function |
| `validation_prefix` | [60](../src/parkour_solver/search/identity.py#L60) | function |

## search/movement.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `iter_movement_programs` | [1](../src/parkour_solver/search/movement.py#L1) | function |
| `screen_program` | [13](../src/parkour_solver/search/movement.py#L13) | function |

## search/ordered_route.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `search_allocations` | [16](../src/parkour_solver/search/ordered_route.py#L16) | function |
| `find_route` | [130](../src/parkour_solver/search/ordered_route.py#L130) | function |

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

## spatial/anchor_guides.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `nearest` | [9](../src/parkour_solver/spatial/anchor_guides.py#L9) | function |
| `landing_cover` | [16](../src/parkour_solver/spatial/anchor_guides.py#L16) | function |
| `terminal_point` | [23](../src/parkour_solver/spatial/anchor_guides.py#L23) | function |
| `unit` | [40](../src/parkour_solver/spatial/anchor_guides.py#L40) | function |
| `build_guides` | [45](../src/parkour_solver/spatial/anchor_guides.py#L45) | function |
| `classify` | [79](../src/parkour_solver/spatial/anchor_guides.py#L79) | function |
| `decision_objective` | [91](../src/parkour_solver/spatial/anchor_guides.py#L91) | function |
| `onward_objective` | [112](../src/parkour_solver/spatial/anchor_guides.py#L112) | function |

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
| `_native` | [8](../src/parkour_solver/validation/numerical.py#L8) | function |
| `validate` | [14](../src/parkour_solver/validation/numerical.py#L14) | function |
| `validate_controls` | [30](../src/parkour_solver/validation/numerical.py#L30) | function |
| `_replay` | [55](../src/parkour_solver/validation/numerical.py#L55) | function |

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
| `replay_witness` | [10](../src/parkour_solver/validation/witness.py#L10) | function |
| `witnesses` | [49](../src/parkour_solver/validation/witness.py#L49) | function |
| `full_witness` | [59](../src/parkour_solver/validation/witness.py#L59) | function |

## work.py

| Symbol | Line | Kind |
| --- | ---: | --- |
| `SearchWork` | [9](../src/parkour_solver/work.py#L9) | class |
| `SearchWork.__init__` | [10](../src/parkour_solver/work.py#L10) | function |
| `SearchWork.check` | [23](../src/parkour_solver/work.py#L23) | function |
| `SearchWork.program` | [31](../src/parkour_solver/work.py#L31) | function |
| `SearchWork.proposal` | [42](../src/parkour_solver/work.py#L42) | function |
| `SearchWork.before_solve` | [47](../src/parkour_solver/work.py#L47) | function |
| `SearchWork.accepted` | [51](../src/parkour_solver/work.py#L51) | function |
| `SearchWork.summary` | [56](../src/parkour_solver/work.py#L56) | function |
