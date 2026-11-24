#include "pkc/search/dfs_state.hpp"

#include "../codec.hpp"
#include "../value_ops.hpp"

namespace pkc {

// A generation is {} until its frontier first reports (found by argument-dependent lookup from the codec).
void decode(const Value& value, Generation& out, const codec::Path& path);
Value encode(const Generation& generation);

namespace codec {

PKC_RECORD(SideMove);
PKC_RECORD(ExpansionState);
PKC_RECORD(HeldAlternative);
PKC_RECORD(SideReserve);
PKC_RECORD(Continuation);
PKC_RECORD(GenerationReport);
PKC_RECORD(FrameParent);
PKC_RECORD(InFlight);
PKC_RECORD(Evaluation);
PKC_RECORD(Record);
PKC_RECORD(Frame);
PKC_RECORD(Transition);
PKC_RECORD(WindowCheckpoint);
PKC_RECORD(Retention);
PKC_RECORD(Window);
PKC_RECORD(HardLatch);
PKC_RECORD(NodeResult);
PKC_RECORD(QueryService);
PKC_RECORD(ObservationQuery);
PKC_RECORD(ObservationReplay);
PKC_RECORD(ObservationDecision);
PKC_RECORD(Observations);
PKC_RECORD(TimeBoundExclusion);
PKC_RECORD(MinimumRunExclusion);
PKC_RECORD(Discovery);
PKC_RECORD(DiscoveryWork);
PKC_RECORD(ComparisonWork);
PKC_RECORD(LocalSetupAlias);
PKC_RECORD(Incumbent);
PKC_RECORD(DfsState);

// Fields in the order search_goals and persistent_programs first write them.

template <class R, class V>
void describe(R& m, V& v, SideMove*) {
    v.req("forwards", m.forwards);
    v.req("held", m.held);
    v.req("complete", m.complete);
}

template <class R, class V>
void describe(R& e, V& v, ExpansionState*) {
    v.req("program", e.program);
    v.req("kind", e.kind);
    v.req("cursor", e.cursor);
    v.opt("reached", e.reached);
    v.opt("landings", e.landings);
    v.opt("held", e.held);
    v.opt("choice", e.choice);
    v.opt("child", e.child);
    v.opt("fullProgram", e.full_program);
    v.opt("projected", e.projected);
    v.opt("proposalChecked", e.proposal_checked);
    v.opt("sides", e.sides);
}

template <class R, class V>
void describe(R& h, V& v, HeldAlternative*) {
    v.req("programs", h.programs);
    v.req("restored", h.restored);
    v.req("next", h.next);
    v.opt("watch", h.watch);
    v.opt("side", h.side);
}

template <class R, class V>
void describe(R& r, V& v, SideReserve*) {
    v.req("programs", r.programs);
    v.req("forwards", r.forwards);
    v.req("outcomes", r.outcomes);
}

template <class R, class V>
void describe(R& c, V& v, Continuation*) {
    v.req("version", c.version);
    v.req("context", c.context);
    v.req("pending", c.pending);
    v.req("ready", c.ready);
    v.req("expansion", c.expansion);
    v.req("seen", c.seen);
    v.req("generated", c.generated);
    v.req("expanded", c.expanded);
    v.req("emitted", c.emitted);
    v.req("screened", c.screened);
    v.req("policyRejections", c.policy_rejections);
    v.req("exhausted", c.exhausted);
    v.req("exclusions", c.exclusions);
    v.opt("nodes", c.nodes);
    v.opt("decisionParents", c.decision_parents);
    v.opt("alternatives", c.alternatives);
    v.opt("succeededNodes", c.succeeded_nodes);
    v.opt("minimumRunCandidates", c.minimum_run_candidates);
    v.opt("nodeInFlight", c.node_in_flight);
    v.opt("sideReserves", c.side_reserves);
    v.opt("sideVariants", c.side_variants);
}

template <class R, class V>
void describe(R& r, V& v, GenerationReport*) {
    v.req("termination", r.termination);
    v.req("expandedPrefixes", r.expanded_prefixes);
    v.req("totalExpandedPrefixes", r.total_expanded_prefixes);
    v.req("generated", r.generated);
    v.req("emittedPrograms", r.emitted_programs);
    v.req("screenedPrefixes", r.screened_prefixes);
    v.req("remainingPrefixes", r.remaining_prefixes);
    v.req("queuedCompletePrograms", r.queued_complete_programs);
    v.req("queueDrops", r.queue_drops);
    v.req("partialQueueDrops", r.partial_queue_drops);
    v.req("completeQueueDrops", r.complete_queue_drops);
    v.req("policyRejections", r.policy_rejections);
    v.req("queuePolicy", r.queue_policy);
    v.req("types", r.types);
    v.req("explicitProgramPriority", r.explicit_program_priority);
    v.req("deferredPrefixes", r.deferred_prefixes);
    v.req("continuation", r.continuation);
    v.req("minimumTicksProven", r.minimum_ticks_proven);
}


template <class R, class V>
void describe(R& p, V& v, FrameParent*) {
    v.req("frame", p.frame);
    v.req("candidate", p.candidate);
}

template <class R, class V>
void describe(R& i, V& v, InFlight*) {
    v.req("id", i.id);
    v.req("program", i.program);
}

template <class R, class V>
void describe(R& e, V& v, Evaluation*) {
    v.req("session", e.session);
    v.req("work", e.work);
}

template <class R, class V>
void describe(R& r, V& v, Record*) {
    v.req("id", r.id);
    v.req("program", r.program);
    v.req("status", r.status);
    v.req("termination", r.termination);
    v.req("nodeFeasibility", r.node_feasibility);
    v.req("objectiveMode", r.objective_mode);
    v.req("arrival", r.arrival);
    v.req("value", r.value);
    v.req("work", r.work);
    v.req("goal", r.goal);
    v.req("goalKey", r.goal_key);
    v.req("evaluations", r.evaluations);
    v.req("tried", r.tried);
    v.req("solveScope", r.solve_scope);
    v.req("solveIdentity", r.solve_identity);
    v.req("numericalAttempts", r.numerical_attempts);
    v.req("contactAttempts", r.contact_attempts);
    v.req("candidateKey", r.candidate_key);
    v.req("scoreContextKey", r.score_context_key);
    v.req("guideId", r.guide_id);
    v.req("ticks", r.ticks);
    v.req("complete", r.complete);
    v.opt("witness", r.witness);
    v.opt("child", r.child);
    v.opt("guidanceWitness", r.guidance_witness);
    v.opt("transferStructure", r.transfer_structure);
    v.opt("earlierTransfers", r.earlier_transfers);
    v.opt("refinementSession", r.refinement_session);
    v.opt("preparationDeferral", r.preparation_deferral);
    v.opt("timeBoundExclusion", r.time_bound_exclusion);
    v.opt("refinementTimeExclusion", r.refinement_time_exclusion);
    v.opt("variantOf", r.variant_of);
    v.opt("restoredFallbacks", r.restored_fallbacks);
    v.opt("frameEnd", r.frame_end);
}

template <class R, class V>
void describe(R& f, V& v, Frame*) {
    v.req("id", f.id);
    v.req("index", f.index);
    v.req("parents", f.parents);
    v.req("parent", f.parent);
    v.req("goal", f.goal);
    v.req("phase", f.phase);
    v.req("generation", f.generation);
    v.req("ready", f.ready);
    v.req("records", f.records);
    v.req("inFlight", f.in_flight);
    v.req("sourceExhausted", f.source_exhausted);
    v.req("batches", f.batches);
    v.req("goalRequest", f.goal_request);
    v.req("semanticCursor", f.semantic_cursor);
    v.req("owner", f.owner);
    v.opt("guide", f.guide);
    v.opt("scoreContextKey", f.score_context_key);
    v.opt("comparisonContextId", f.comparison_context_id);
    v.opt("tickPeers", f.tick_peers);
    v.opt("tickPeerRevision", f.tick_peer_revision);
    v.opt("recoveryFailure", f.recovery_failure);
    v.opt("validatedFinish", f.validated_finish);
    v.opt("failedCandidates", f.failed_candidates);
}

template <class R, class V>
void describe(R& t, V& v, Transition*) {
    v.req("kind", t.kind);
    v.req("frame", t.frame);
    v.req("depth", t.depth);
    v.req("session", t.session);
    v.req("elapsedSeconds", t.elapsed_seconds);
}

template <class R, class V>
void describe(R& c, V& v, WindowCheckpoint*) {
    v.req("tick", c.tick);
}

template <class R, class V>
void describe(R& r, V& v, Retention*) {
    v.req("policy", r.policy);
}

template <class R, class V>
void describe(R& w, V& v, Window*) {
    v.req("checkpoint", w.checkpoint);
    v.req("goal", w.goal);
    v.req("frame", w.frame);
    v.req("candidates", w.candidates);
    v.req("retained", w.retained);
    v.req("deferred", w.deferred);
    v.req("scored", w.scored);
    v.req("unknown", w.unknown);
    v.req("heuristicDrops", w.heuristic_drops);
    v.req("generation", w.generation);
    v.req("retention", w.retention);
}

template <class R, class V>
void describe(R& l, V& v, HardLatch*) {
    v.req("version", l.version);
    v.req("revision", l.revision);
    v.req("candidate", l.candidate);
    v.req("frame", l.frame);
    v.req("model", l.model);
    v.req("inputs", l.inputs);
    v.req("transferStructure", l.transfer_structure);
}

template <class R, class V>
void describe(R& n, V& v, NodeResult*) {
    v.req("parent", n.parent);
    v.req("depth", n.depth);
    v.opt("kind", n.kind);
}

template <class R, class V>
void describe(R& s, V& v, QueryService*) {
    v.req("latchRevision", s.latch_revision);
    v.req("hardLatched", s.hard_latched);
    v.req("runDeferralEnabled", s.run_deferral_enabled);
    v.req("tickDeferralEnabled", s.tick_deferral_enabled);
    v.req("comparisonPolicy", s.comparison_policy);
}

template <class R, class V>
void describe(R& q, V& v, ObservationQuery*) {
    v.req("model", q.model);
    v.req("status", q.status);
    v.req("elapsedSeconds", q.elapsed_seconds);
    v.req("metrics", q.metrics);
    v.req("service", q.service);
}

template <class R, class V>
void describe(R& r, V& v, ObservationReplay*) {
    v.req("model", r.model);
    v.req("point", r.point);
    v.req("distances", r.distances);
    v.req("wouldFlip", r.would_flip);
}

template <class R, class V>
void describe(R& d, V& v, ObservationDecision*) {
    v.req("id", d.id);
    v.req("parent", d.parent);
    v.req("depth", d.depth);
    v.req("lastSolvedDepth", d.last_solved_depth);
    v.req("pointTick", d.point_tick);
    v.req("pointSource", d.point_source);
    v.req("version", d.version);
    v.req("guide", d.guide);
    v.req("span", d.span);
    v.req("point", d.point);
    v.req("isTurnaround", d.is_turnaround);
    v.req("distances", d.distances);
    v.req("counts", d.counts);
    v.req("mode", d.mode);
    v.req("key", d.key);
    v.req("kind", d.kind);
    v.req("queries", d.queries);
    v.req("replays", d.replays);
    v.opt("selected", d.selected);
}

template <class R, class V>
void describe(R& o, V& v, Observations*) {
    v.req("enabled", o.enabled);
    v.req("decisions", o.decisions);
}

template <class R, class V>
void describe(R& e, V& v, TimeBoundExclusion*) {
    v.req("id", e.id);
    v.req("proof", e.proof);
}

template <class R, class V>
void describe(R& e, V& v, MinimumRunExclusion*) {
    v.req("frame", e.frame);
    v.req("candidate", e.candidate);
    v.req("proof", e.proof);
    v.req("program", e.program);
    v.req("span", e.span);
    v.req("decision", e.decision);
    v.req("isTurnaround", e.is_turnaround);
    v.req("latchRevision", e.latch_revision);
    v.opt("restored", e.restored);
    v.opt("recovery", e.recovery);
    v.opt("retired", e.retired);
    v.opt("restorationExclusion", e.restoration_exclusion);
}

template <class R, class V>
void describe(R& d, V& v, Discovery*) {
    v.req("owner", d.owner);
    v.req("index", d.index);
    v.req("parents", d.parents);
    v.req("parent", d.parent);
    v.req("cursor", d.cursor);
    v.req("continuation", d.continuation);
    v.req("exhausted", d.exhausted);
}

template <class R, class V>
void describe(R& w, V& v, DiscoveryWork*) {
    v.req("expansions", w.expansions);
    v.req("materializations", w.materializations);
}

template <class R, class V>
void describe(R& w, V& v, ComparisonWork*) {
    v.req("assessments", w.assessments);
    v.req("cacheHits", w.cache_hits);
    v.req("peerChecks", w.peer_checks);
}

template <class R, class V>
void describe(R& a, V& v, LocalSetupAlias*) {
    v.req("owner", a.owner);
    v.req("request", a.request);
}

template <class R, class V>
void describe(R& i, V& v, Incumbent*) {
    v.req("movementProgram", i.movement_program);
    v.req("blockIds", i.block_ids);
}

template <class R, class V>
void describe(R& s, V& v, DfsState*) {
    v.req("version", s.version);
    v.req("kind", s.kind);
    v.req("context", s.context);
    v.req("initialRoots", s.initial_roots);
    v.req("stack", s.stack);
    v.req("suspended", s.suspended);
    v.req("frames", s.frames);
    v.req("transitions", s.transitions);
    v.req("windows", s.windows);
    v.req("finalAttempts", s.final_attempts);
    v.req("best", s.best);
    v.req("cumulativeCalls", s.cumulative_calls);
    v.req("session", s.session);
    v.req("expanded", s.expanded);
    v.req("nextFrame", s.next_frame);
    v.req("goalQueue", s.goal_queue);
    v.req("goalOwners", s.goal_owners);
    v.req("discovery", s.discovery);
    v.req("hardLatches", s.hard_latches);
    v.req("latchRevision", s.latch_revision);
    v.req("nodeResults", s.node_results);
    v.req("observations", s.observations);
    v.req("timeBoundExclusions", s.time_bound_exclusions);
    v.req("minimumRunExclusions", s.minimum_run_exclusions);
    v.opt("discoveryWork", s.discovery_work);
    v.opt("comparisonWork", s.comparison_work);
    v.opt("comparisonDecisions", s.comparison_decisions);
    v.opt("localSetupAliases", s.local_setup_aliases);
    v.opt("pendingGoalOwner", s.pending_goal_owner);
    v.opt("transferTimeBounds", s.transfer_time_bounds);
    v.opt("firstFullSolutionSeconds", s.first_full_solution_seconds);
}

}  // namespace codec

void decode(const Value& value, Generation& out, const codec::Path& path) {
    if (codec::as<std::shared_ptr<const Object>>(value, path, "object")->empty()) {
        out.report.reset();
    } else {
        codec::decode(value, out.report.emplace(), path);
    }
}

Value encode(const Generation& generation) {
    return generation.report ? codec::encode(*generation.report) : object({});
}

DfsState dfs_state_from_value(const Value& value) { return codec::from_value<DfsState>(value, "dfs"); }

Value to_value(const DfsState& state) { return codec::encode(state); }
Value to_value(const Incumbent& incumbent) { return codec::encode(incumbent); }
Value to_value(const HardLatch& latch) { return codec::encode(latch); }
MinimumRunExclusion minimum_run_exclusion_from_value(const Value& value) {
    return codec::from_value<MinimumRunExclusion>(value, "minimumRunExclusion");
}
Value to_value(const ObservationQuery& query) { return codec::encode(query); }
Value to_value(const ObservationReplay& replay) { return codec::encode(replay); }

}  // namespace pkc
