// Branch conditioning of interaction envelopes: reachability/screen.py.
#pragma once

#include "pkc/value.hpp"

namespace pkc {

// condition_envelopes(envelopes, extra_rows, refine=refine, objective=objective) as {envelopes, proof}; a null
// `objective` is None. Envelopes with a model are rebuilt over it with the rows added, the result carrying its model.
Value condition_envelopes(const Value& envelopes, const Array& extra_rows, bool refine, const Value* objective);

}  // namespace pkc
