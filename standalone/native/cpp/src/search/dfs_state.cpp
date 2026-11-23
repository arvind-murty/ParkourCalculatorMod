#include "pkc/search/dfs_state.hpp"

#include "../codec.hpp"

namespace pkc {
namespace codec {

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
PKC_RECORD(FinalAttempt);
PKC_RECORD(Finalization);
PKC_RECORD(Incumbent);
PKC_RECORD(DfsState);

// Fields in the order search_goals first writes them.

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
    v.req("family", r.family);
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
    v.opt("retrySession", f.retry_session);
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
void describe(R& a, V& v, FinalAttempt*) {
    v.req("status", a.status);
    v.req("termination", a.termination);
    v.req("ipoptCalls", a.ipopt_calls);
    v.req("frame", a.frame);
    v.req("record", a.record);
    v.req("seconds", a.seconds);
}

template <class R, class V>
void describe(R& f, V& v, Finalization*) {
    v.req("version", f.version);
    v.req("source", f.source);
    v.req("stage", f.stage);
    v.req("attempts", f.attempts);
    v.req("best", f.best);
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
    v.req("refinements", s.refinements);
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
    v.req("discoveryWork", s.discovery_work);
    v.opt("comparisonWork", s.comparison_work);
    v.opt("comparisonDecisions", s.comparison_decisions);
    v.opt("localSetupAliases", s.local_setup_aliases);
    v.opt("pendingGoalOwner", s.pending_goal_owner);
    v.opt("transferTimeBounds", s.transfer_time_bounds);
    v.opt("firstFullSolutionSeconds", s.first_full_solution_seconds);
    v.opt("finalization", s.finalization);
}

}  // namespace codec

DfsState dfs_state_from_value(const Value& value) { return codec::from_value<DfsState>(value, "dfs"); }

Value to_value(const DfsState& state) { return codec::encode(state); }

}  // namespace pkc
