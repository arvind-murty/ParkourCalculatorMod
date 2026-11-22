// Port of spatial/cells.subtract_open.
#include "pkc/spatial/cells.hpp"

namespace pkc {

std::vector<NumBox> subtract_open(const NumBox& box, const NumBox& obstacle, bool boundaries) {
    for (int a = 0; a < 3; ++a)
        if (box[a].value == box[a + 3].value &&
            !(obstacle[a].value < box[a].value && box[a].value < obstacle[a + 3].value))
            return {box};
    for (int a = 0; a < 3; ++a)
        if (box[a].value < box[a + 3].value &&
            (box[a + 3].value <= obstacle[a].value || box[a].value >= obstacle[a + 3].value))
            return {box};
    NumBox middle = box;
    std::vector<NumBox> pieces;
    for (int axis = 0; axis < 3; ++axis) {
        if (middle[axis].value == middle[axis + 3].value) continue;
        const Num lo = obstacle[axis].value > middle[axis].value ? obstacle[axis] : middle[axis];   // max(m, o)
        const Num hi = obstacle[axis + 3].value < middle[axis + 3].value ? obstacle[axis + 3] : middle[axis + 3];  // min
        if (middle[axis].value < lo.value ||
            (boundaries && middle[axis].value == lo.value && lo.value == obstacle[axis].value)) {
            NumBox piece = middle;
            piece[axis + 3] = lo;
            pieces.push_back(piece);
        }
        if (hi.value < middle[axis + 3].value ||
            (boundaries && hi.value == middle[axis + 3].value && middle[axis + 3].value == obstacle[axis + 3].value)) {
            NumBox piece = middle;
            piece[axis] = hi;
            pieces.push_back(piece);
        }
        middle[axis] = lo;
        middle[axis + 3] = hi;
    }
    return pieces;
}

}  // namespace pkc
