#ifndef RANDOM_PROJECTION_H
#define RANDOM_PROJECTION_H

#include <unordered_set>
#include <vector>
#include <cstdint>

std::vector<int32_t> transform_set_into_vector(const std::unordered_set<unsigned long int> &hashes, int d);

#endif // RANDOM_PROJECTION_H
