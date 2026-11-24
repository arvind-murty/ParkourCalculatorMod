// Port of scene/static_collision.py: one sweep per tick, Y then X then Z
// clipping in solid order, sneak edge reduction, the two step-up alternatives
// and ladder carry. Every replayed quantity is a Python float, so plain doubles
// match it.
#include "pkc/scene/static_collision.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "../value_ops.hpp"
#include "pkc/vertical.hpp"

namespace pkc {
namespace {

using Aabb = std::array<double, 6>;  // min x, y, z, max x, y, z
using Vector = std::array<double, 3>;
using Cell = std::array<std::int64_t, 3>;

const double kHalfWidth = f32(0.6) / 2;  // scene/route.HALF_WIDTH
const double kBodyHeight = f32(1.8);     // scene/geometry.BODY_HEIGHT
constexpr double kReplayTolerance = 1e-4;  // schema.REPLAY_TOLERANCE
const char* const kSceneFields[] = {"version", "profile", "bounds", "solids", "ladders", "surfaces", "stepHeight",
                                    "metadata"};

double scene_number(const Value& value) {  // _number
    if (auto* i = std::get_if<std::int64_t>(&value.data)) return static_cast<double>(*i);
    if (auto* d = std::get_if<double>(&value.data))
        if (std::isfinite(*d)) return *d;
    fail("Nonfinite scene number");
}

Aabb box_of(const Value* value) {  // _box
    if (!value || !is_list(*value) || items(*value).size() != 6) fail("A box needs six finite bounds");
    Aabb box{};
    for (int i = 0; i < 6; ++i) box[i] = scene_number(items(*value)[i]);
    if (!(box[0] < box[3] && box[1] < box[4] && box[2] < box[5])) fail("A box must have positive volume");
    return box;
}

Cell cell_of(const Value* value) {  // _cell
    if (!value || !is_list(*value) || items(*value).size() != 3) fail("A cell needs three integer coordinates");
    Cell cell{};
    for (int i = 0; i < 3; ++i) {
        double v = scene_number(items(*value)[i]);
        if (v != std::floor(v)) fail("A cell coordinate must be an integer");
        cell[i] = static_cast<std::int64_t>(v);
    }
    return cell;
}

bool contains_box(const Aabb& outer, const Aabb& inner) {
    return inner[0] >= outer[0] && inner[3] <= outer[3] && inner[1] >= outer[1] && inner[4] <= outer[4] &&
           inner[2] >= outer[2] && inner[5] <= outer[5];
}

Aabb offset(const Aabb& b, double x, double y, double z) {
    return {b[0] + x, b[1] + y, b[2] + z, b[3] + x, b[4] + y, b[5] + z};
}

// min(0., v) / max(0., v) keep 0. on ties, as Python's do.
double lower(double v) { return v < 0. ? v : 0.; }
double upper(double v) { return v > 0. ? v : 0.; }

Aabb sweep(const Aabb& b, double x, double y, double z) {
    return {b[0] + lower(x), b[1] + lower(y), b[2] + lower(z), b[3] + upper(x), b[4] + upper(y), b[5] + upper(z)};
}

Vector center(const Aabb& b) { return {(b[0] + b[3]) / 2, b[1], (b[2] + b[5]) / 2}; }

struct Clip {
    Aabb obstacle;
    int axis;
    double requested, value;
};

double clip(const std::vector<Aabb>& obstacles, const Aabb& body, double value, int axis, std::vector<Clip>* clips) {
    int first = (axis + 1) % 3, second = (axis + 2) % 3;
    for (const auto& obstacle : obstacles) {
        if (body[first + 3] <= obstacle[first] || body[first] >= obstacle[first + 3] ||
            body[second + 3] <= obstacle[second] || body[second] >= obstacle[second + 3])
            continue;
        double requested = value;
        if (value > 0 && body[axis + 3] <= obstacle[axis]) {
            double gap = obstacle[axis] - body[axis + 3];
            if (gap < value) value = gap;
        } else if (value < 0 && body[axis] >= obstacle[axis + 3]) {
            double gap = obstacle[axis + 3] - body[axis];
            if (gap > value) value = gap;
        }
        if (clips && value != requested) clips->push_back({obstacle, axis, requested, value});
    }
    return value;
}

double edge_step(double value) {
    if (-0.05 <= value && value < 0.05) return 0.;
    return value > 0 ? value - 0.05 : value + 0.05;
}

struct Motion {
    Aabb box;
    Vector position, velocity;
    bool on_ground, blocked_x, blocked_z, vertical, stepped;
    std::vector<Clip> clips, step_triggers;
};

class StaticScene {
public:
    explicit StaticScene(const Value* value) {
        if (!value || !is_object(*value)) fail("Expected a collision scene object");
        const Object& scene = fields(*value);
        for (const auto& entry : scene)
            if (std::none_of(std::begin(kSceneFields), std::end(kSceneFields),
                             [&](const char* k) { return entry.first == k; }))
                fail("Unsupported scene field: " + entry.first);
        if (!py_equal(get_or_null(scene, "version"), integer_value(1)) ||
            !py_equal(get_or_null(scene, "profile"), text("minecraft-1.8.9")))
            fail("Expected scene version 1, minecraft-1.8.9");
        bounds_ = box_of(get(scene, "bounds"));
        step_height_ = kStepHeight;
        if (const Value* step = get(scene, "stepHeight")) step_height_ = f32(scene_number(*step));
        if (step_height_ != 0. && step_height_ != kStepHeight) fail("stepHeight must be 0 or the widened float 0.6F");
        const Value* solids = get(scene, "solids");
        if (!solids || !is_list(*solids)) fail("A scene needs a solids list");
        for (const auto& solid : items(*solids)) {
            Aabb box = box_of(&solid);
            if (!contains_box(bounds_, box)) fail("Solid outside scene bounds");
            solids_.push_back(box);
        }
        solid_set_.insert(solids_.begin(), solids_.end());
        if (const Value* ladders = get(scene, "ladders"); ladders && py_truthy(*ladders))
            for (const auto& cell : items(*ladders)) ladders_.insert(cell_of(&cell));
        if (const Value* surfaces = get(scene, "surfaces"); surfaces && py_truthy(*surfaces))
            for (const auto& surface : items(*surfaces)) {
                double slip = scene_number(is_object(surface) ? get_or_null(fields(surface), "slipperiness") : kNull);
                if (!(0 < slip && slip <= 1)) fail("Invalid slipperiness");
                cell_of(get(fields(surface), "cell"));
            }
    }

    bool ladder(const Vector& position) const {
        if (ladders_.empty()) return false;
        return ladders_.count({static_cast<std::int64_t>(std::floor(position[0])),
                               static_cast<std::int64_t>(std::floor(position[1])),
                               static_cast<std::int64_t>(std::floor(position[2]))}) > 0;
    }

    std::vector<Aabb> collisions(const Aabb& area) const {
        if (!contains_box(bounds_, area)) fail("Replay collision query leaves the supplied scene bounds");
        std::vector<Aabb> found;
        for (const auto& s : solids_)
            if (s[0] < area[3] && s[3] > area[0] && s[1] < area[4] && s[4] > area[1] && s[2] < area[5] &&
                s[5] > area[2])
                found.push_back(s);
        return found;
    }

    Aabb start_box(const Vector& p) const {
        Aabb box{p[0] - kHalfWidth, p[1], p[2] - kHalfWidth, p[0] + kHalfWidth, p[1] + kBodyHeight, p[2] + kHalfWidth};
        if (!contains_box(bounds_, box)) fail("Player starts outside scene bounds");
        if (!collisions(box).empty()) fail("Player starts inside a solid");
        return box;
    }

    const Aabb* member(const Value& value) const {  // _member
        if (!is_list(value) || items(value).size() != 6) return nullptr;
        Aabb box{};
        for (int i = 0; i < 6; ++i)
            if (!numeric(items(value)[i], box[i])) return nullptr;
        auto found = solid_set_.find(box);
        return found == solid_set_.end() ? nullptr : &*found;
    }

    Motion move(const Aabb& original, const Vector& velocity, bool grounded, bool sneak) const {
        double x = velocity[0], y = velocity[1], z = velocity[2];
        double ex = x, ey = y, ez = z;
        if (grounded && sneak) {
            while (x != 0 && collisions(offset(original, x, -1., 0.)).empty()) ex = x = edge_step(x);
            while (z != 0 && collisions(offset(original, 0., -1., z)).empty()) ez = z = edge_step(z);
            while (x != 0 && z != 0 && collisions(offset(original, x, -1., z)).empty()) {
                ex = x = edge_step(x);
                ez = z = edge_step(z);
            }
        }
        std::vector<Aabb> obstacles = collisions(sweep(original, x, y, z));
        std::vector<Clip> clips, triggers;
        y = clip(obstacles, original, y, 1, &clips);
        Aabb moved = offset(original, 0., y, 0.);
        bool can_step = grounded || (ey != y && ey < 0);
        x = clip(obstacles, moved, x, 0, &clips);
        moved = offset(moved, x, 0., 0.);
        z = clip(obstacles, moved, z, 2, &clips);
        moved = offset(moved, 0., 0., z);
        bool stepped = false;
        if (step_height_ > 0 && can_step && (ex != x || ez != z)) {
            std::vector<Aabb> steps = collisions(sweep(original, ex, step_height_, ez));
            std::vector<Clip> clips_a, clips_b;
            double rise_a = clip(steps, sweep(original, ex, 0., ez), step_height_, 1, nullptr);
            Aabb a = offset(original, 0., rise_a, 0.);
            double ax = clip(steps, a, ex, 0, &clips_a);
            a = offset(a, ax, 0., 0.);
            double az = clip(steps, a, ez, 2, &clips_a);
            a = offset(a, 0., 0., az);
            double rise_b = clip(steps, original, step_height_, 1, nullptr);
            Aabb b = offset(original, 0., rise_b, 0.);
            double bx = clip(steps, b, ex, 0, &clips_b);
            b = offset(b, bx, 0., 0.);
            double bz = clip(steps, b, ez, 2, &clips_b);
            b = offset(b, 0., 0., bz);
            bool use_a = ax * ax + az * az > bx * bx + bz * bz;
            Aabb step_box = use_a ? a : b;
            double sx = use_a ? ax : bx, sz = use_a ? az : bz;
            std::vector<Clip>& step_clips = use_a ? clips_a : clips_b;
            double sy = clip(steps, step_box, -(use_a ? rise_a : rise_b), 1, &step_clips);
            step_box = offset(step_box, 0., sy, 0.);
            if (x * x + z * z < sx * sx + sz * sz) {
                moved = step_box;
                x = sx;
                y = sy;
                z = sz;
                stepped = true;
                triggers = std::move(clips);
                clips = std::move(step_clips);
            }
        }
        bool blocked_x = ex != x, blocked_z = ez != z, vertical = ey != y;
        Vector carry{blocked_x ? 0. : velocity[0], vertical ? 0. : velocity[1], blocked_z ? 0. : velocity[2]};
        if ((blocked_x || blocked_z) && ladder(center(moved))) carry[1] = 0.2;
        return {moved, center(moved), carry, vertical && ey < 0, blocked_x, blocked_z, vertical, stepped,
                std::move(clips), std::move(triggers)};
    }

private:
    Aabb bounds_{};
    double step_height_ = 0.;
    std::vector<Aabb> solids_;
    std::set<Aabb> solid_set_;
    std::set<Cell> ladders_;
};

Vector vector_of(const Value& value) {  // _vector
    if (!is_list(value) || items(value).size() != 3) fail("Expected X/Y/Z");
    Vector v{};
    for (int i = 0; i < 3; ++i) {
        const Value& item = items(value)[i];
        if (std::holds_alternative<bool>(item.data) || !numeric(item, v[i]) || !std::isfinite(v[i]))
            fail("Nonfinite scene check");
    }
    return v;
}

double num(const Value& value) {
    double out;
    if (!numeric(value, out)) fail("expected a number");
    return out;
}

double field(const Object& event, const char* key, double fallback) {  // event.get(key, fallback)
    const Value* value = get(event, key);
    return value ? num(*value) : fallback;
}

int axis_of(const Object& event) { return static_cast<int>(field(event, "axis", 0)); }

bool same_face(const Aabb& box, const Object& contact, const Aabb& expected, const Aabb& before, const Motion& hit) {
    int axis = axis_of(contact);
    double direction = field(contact, "direction", 0), plane = field(contact, "plane", 0.);
    double width = axis == 1 ? kBodyHeight : kHalfWidth;
    double lo = axis == 0 ? box[0] : axis == 2 ? box[2] : box[1];
    double hi = axis == 0 ? box[3] : axis == 2 ? box[5] : box[4];
    double actual = direction > 0 ? lo - width : hi + (axis == 1 ? 0 : width);
    double x = axis == 2 ? hit.position[0] : (before[0] + before[3]) / 2;
    double y = hit.position[1];
    double z = (before[2] + before[5]) / 2;
    double expected_plane =
        direction > 0 ? expected.at(axis) - width : expected.at(axis + 3) + (axis == 1 ? 0 : width);
    bool overlap_x = x + kHalfWidth > expected[0] && x - kHalfWidth < expected[3];
    bool overlap_y = y + kBodyHeight > expected[1] && y < expected[4];
    bool overlap_z = z + kHalfWidth > expected[2] && z - kHalfWidth < expected[5];
    return actual == plane && expected_plane == plane &&
           (axis == 0 ? overlap_y && overlap_z : axis == 2 ? overlap_x && overlap_y : overlap_x && overlap_z);
}

std::vector<const Aabb*> members(const StaticScene& scene, const Object& event) {
    std::vector<const Aabb*> found;
    const Value* boxes = get(event, "boxes");
    if (boxes && py_truthy(*boxes))
        for (const auto& values : items(*boxes)) found.push_back(scene.member(values));
    return found;
}

bool matches_face(const StaticScene& scene, const Aabb& box, const Object& contact, const Aabb& before,
                  const Motion& hit) {
    const Value* boxes = get(contact, "boxes");
    if (!boxes || is_null(*boxes)) return true;
    for (const auto& values : items(*boxes)) {
        const Aabb* m = scene.member(values);
        if (m && (*m == box || same_face(box, contact, *m, before, hit))) return true;
    }
    return false;
}

bool has(const Object& event, const char* key) {  // event.get(key) is not None
    const Value* value = get(event, key);
    return value && !is_null(*value);
}

bool truthy(const Object& event, const char* key) {  // event.get(key, False)
    const Value* value = get(event, key);
    return value && py_truthy(*value);
}

bool contact_mismatch(const StaticScene& scene, const std::vector<const Object*>& events, double move_y,
                      const Aabb& before, const Motion& hit) {
    if (hit.stepped && std::none_of(events.begin(), events.end(), [](const Object* e) {
            return truthy(*e, "step") || has(*e, "movementCase");
        }))
        return true;
    if (!hit.stepped && hit.vertical && move_y > 0 &&
        std::none_of(events.begin(), events.end(), [](const Object* e) { return field(*e, "axis", 0) == 1; }))
        return true;
    bool axis_x = false, axis_z = false;
    for (const Object* e : events) {
        axis_x = axis_x || field(*e, "axis", 0) == 0;
        axis_z = axis_z || field(*e, "axis", 0) == 2;
    }
    if (hit.blocked_x != axis_x || hit.blocked_z != axis_z) return true;
    for (const Object* e : events) {
        const Object& event = *e;
        int axis = axis_of(event);
        double plane = field(event, "plane", 0.);
        if (has(event, "movementCase")) {
            bool trigger = false;
            for (const Aabb* m : members(scene, event))
                if (m)
                    for (const auto& c : hit.step_triggers) trigger = trigger || (c.axis != 1 && c.obstacle == *m);
            if (!hit.stepped || !trigger || axis != 1 || std::abs(hit.position[1] - plane) > kReplayTolerance) return true;
            continue;
        }
        if (truthy(event, "step")) {
            const Aabb& box = hit.box;
            bool supported = false;
            for (const Aabb* m : members(scene, event))
                supported = supported || (m && (*m)[4] == plane && box[3] > (*m)[0] && box[0] < (*m)[3] &&
                                          box[5] > (*m)[2] && box[2] < (*m)[5]);
            if (!hit.stepped || axis != 1 || field(event, "direction", 0) != -1 || !supported ||
                std::abs(hit.position[1] - plane) > kReplayTolerance)
                return true;
            continue;
        }
        const Clip* last = nullptr;
        for (const auto& c : hit.clips)
            if (c.axis == axis) last = &c;
        if (!last || last->requested == last->value || last->requested * field(event, "direction", 0) <= 0 ||
            std::abs(hit.position.at(axis) - plane) > kReplayTolerance ||
            !matches_face(scene, last->obstacle, event, before, hit))
            return true;
    }
    return false;
}

Value box_value(const Aabb& box) {
    Array values;
    for (double v : box) values.push_back(real(v));
    return array(std::move(values));
}

Value vector_value(const Vector& v) { return array({real(v[0]), real(v[1]), real(v[2])}); }

}  // namespace

Value check_scene(const Value& request_value, std::optional<double> seconds) {
    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now();
    const Object& request = fields(request_value);
    StaticScene scene(get(request, "scene"));
    const Value* positions = get(request, "positions");
    const Value* movement = get(request, "movement");
    const Value* ground = get(request, "expectedGround");
    if (!movement || !is_list(*movement) || !positions || !is_list(*positions) || !ground || !is_list(*ground))
        fail("Scene check needs positions, movement and expectedGround");
    const std::size_t n = items(*movement).size();
    if (items(*positions).size() != n + 1 || items(*ground).size() != n + 1)
        fail("Scene check arrays have inconsistent lengths");
    auto optional_list = [&](const char* key) -> const Array* {
        const Value* value = get(request, key);
        return value && !is_null(*value) ? &items(*value) : nullptr;
    };
    const Array *sneak = optional_list("sneak"), *ladder = optional_list("ladderTicks");
    const Array *velocities = optional_list("velocities"), *friction = optional_list("friction");
    std::map<double, std::vector<const Object*>> contacts;
    if (const Value* events = get(request, "contacts"); events && py_truthy(*events))
        for (const auto& event : items(*events)) contacts[field(fields(event), "state", 0)].push_back(&fields(event));
    const Array& expected_ground = items(*ground);
    Aabb body = scene.start_box(vector_of(items(*positions)[0]));
    if (ladder && (ladder->size() != n + 1 ||
                   !py_equal(boolean(scene.ladder(vector_of(items(*positions)[0]))), (*ladder)[0])))
        return object({{"feasible", boolean(false)}, {"reason", text("initial_ladder_membership")}});
    for (std::size_t t = 0; t < n; ++t) {
        if (seconds && std::chrono::duration<double>(Clock::now() - started).count() > *seconds)
            throw Timeout("Scene check exceeded the budget");
        const Value& move = items(*movement)[t];
        bool sneaking = sneak && py_truthy((*sneak)[t]);
        const auto tick = integer_value(static_cast<std::int64_t>(t));
        if (scene.ladder(center(body))) {
            const Array& m = items(move);
            if (std::abs(num(m[0])) > kCap || std::abs(num(m[2])) > kCap || num(m[1]) < -.15 ||
                (sneaking && num(m[1]) < 0))
                return object({{"feasible", boolean(false)}, {"tick", tick}, {"reason", text("ladder_cap_contract")}});
        }
        Motion hit;
        try {
            hit = scene.move(body, vector_of(move), py_truthy(expected_ground[t]), sneaking);
        } catch (const std::invalid_argument& error) {
            return object({{"feasible", boolean(false)}, {"tick", tick}, {"reason", text("scene_query")},
                           {"message", text(error.what())}});
        }
        Vector expected = vector_of(items(*positions)[t + 1]);
        const Vector& position = hit.position;
        double error = std::max({std::abs(position[0] - expected[0]), std::abs(position[1] - expected[1]),
                                 std::abs(position[2] - expected[2])});
        bool occupancy = ladder && !py_equal(boolean(scene.ladder(position)), (*ladder)[t + 1]);
        double velocity_error = 0.;
        if (velocities) {
            Vector target = vector_of((*velocities)[t + 1]);
            double f = num((*friction)[t]);
            velocity_error = std::max({std::abs(hit.velocity[0] * f - target[0]),
                                       std::abs((hit.velocity[1] - kGravity) * kYDrag - target[1]),
                                       std::abs(hit.velocity[2] * f - target[2])});
        }
        static const std::vector<const Object*> kNone;
        auto found = contacts.find(static_cast<double>(t + 1));
        bool mismatch = contact_mismatch(scene, found == contacts.end() ? kNone : found->second,
                                         num(items(move)[1]), body, hit);
        bool ground_differs = !py_equal(boolean(hit.on_ground), expected_ground[t + 1]);
        if (mismatch || occupancy || velocity_error > kReplayTolerance || error > kReplayTolerance || ground_differs) {
            const std::vector<Clip>& diagnostics = hit.stepped ? hit.step_triggers : hit.clips;
            std::map<int, std::size_t> last;
            for (std::size_t i = 0; i < diagnostics.size(); ++i) last[diagnostics[i].axis] = i;
            Array clips;
            for (std::size_t i = 0; i < diagnostics.size(); ++i) {
                int axis = diagnostics[i].axis;
                if (i != last[axis] || (hit.stepped && axis == 1)) continue;
                if (axis == 1 && std::abs(position[1] - expected[1]) <= kReplayTolerance && !ground_differs) continue;
                clips.push_back(object({{"box", box_value(diagnostics[i].obstacle)}, {"axis", integer_value(axis)}}));
            }
            return object({{"feasible", boolean(false)},
                           {"tick", tick},
                           {"reason", text(hit.stepped ? "unplanned_step" : "scene_collision")},
                           {"positionError", real(error)},
                           {"contactMismatch", boolean(mismatch)},
                           {"occupancyMismatch", boolean(occupancy)},
                           {"velocityError", real(velocity_error)},
                           {"actualPosition", vector_value(position)},
                           {"clips", array(std::move(clips))}});
        }
        body = hit.box;
    }
    return object({{"feasible", boolean(true)}, {"ticks", integer_value(static_cast<std::int64_t>(n))}});
}

}  // namespace pkc
