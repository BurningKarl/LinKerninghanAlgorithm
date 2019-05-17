//
// Created by karl on 14.05.19.
//

#include "SimpleHeuristic.h"
#include <algorithm>
#include <vector>
#include <tuple>
#include <utility>
#include <numeric>
#include "LinKerninghanHeuristic.h"


// ============================================= AlternatingWalk class =================================================

AlternatingWalk AlternatingWalk::close() const {
    AlternatingWalk result(*this);
    result.push_back(at(0));
    return result;
}

AlternatingWalk AlternatingWalk::appendAndClose(vertex_t vertex) const {
    AlternatingWalk result(*this);
    result.push_back(vertex);
    result.push_back(at(0));
    return result;
}

bool AlternatingWalk::containsEdge(vertex_t vertex1, vertex_t vertex2) const {
    for (dimension_t i = 0; i < size() - 1; ++i) {
        if ((at(i) == vertex1 and at(i + 1) == vertex2) or
            (at(i) == vertex2 and at(i + 1) == vertex1)) {
            return true;
        }
    }
    return false;
}


// This is implemented as described in Combinatorial Optimization with p_1 = 5, p_2 = 2 and G = K_n

Tour linKerninghanHeuristic(const TsplibProblem &tsplibProblem, const Tour &startTour) {
    const size_t backtrackingDepth = 5;
    const size_t infeasibilityDepth = 2;

    const dimension_t dimension = tsplibProblem.getDimension();
    Tour currentTour = startTour;
    std::vector<std::vector<vertex_t>> vertexChoices;
    AlternatingWalk currentWalk;
    AlternatingWalk bestAlternatingWalk;
    distance_t highestGain = 0;

    while (true) {
        // Create set X_0 with all vertices
        vertexChoices.clear();
        vertexChoices.emplace_back(dimension);
        std::iota(vertexChoices.at(0).begin(), vertexChoices.at(0).end(), 0);

        bestAlternatingWalk.clear();
        highestGain = 0;
        size_t i = 0;
        while (true) {
            if (vertexChoices.at(i).empty() and highestGain > 0) {
                currentTour.exchange(bestAlternatingWalk);
                break;
            } else if (vertexChoices.at(i).empty() and highestGain == 0) {
                if (i == 0) {
                    return currentTour;
                } else {
                    i = std::min(i - 1, backtrackingDepth);
                    vertexChoices.erase(vertexChoices.begin() + i, vertexChoices.end());
                    currentWalk.erase(currentWalk.begin() + i - 1, currentWalk.end());
                    continue;
                }
            }

            currentWalk.push_back(vertexChoices.at(i).back());
            vertexChoices.at(i).pop_back();

            // DEBUG:
            if (vertexChoices.size() == i) {
                throw std::runtime_error(
                        "vertexChoices.size() (=" + std::to_string(vertexChoices.size()) + ") is not i-1 (=" +
                        std::to_string(i - 1) + ")");
            }
            if (currentWalk.size() == i) {
                throw std::runtime_error(
                        "currentWalk.size() (=" + std::to_string(currentWalk.size()) + ") is not i-1 (=" +
                        std::to_string(i - 1) + ")");
            }

            if (i % 2 == 1 and i >= 3) {
                AlternatingWalk closedWalk = currentWalk.close(); // closedWalk = (x_0, x_1, ..., x_i, x_0)
                distance_t gain;
                if ((gain = tsplibProblem.exchangeGain(closedWalk)) > highestGain
                    and currentTour.isTourAfterExchange(closedWalk)) {
                    bestAlternatingWalk = closedWalk; // TODO: This should be a copy operation, is it?
                    highestGain = gain;
                }
            }

            vertexChoices.emplace_back(); // Add set X_{i+1}
            vertex_t xi = currentWalk.at(i);
            if (i % 2 == 1) { // i is odd
                for (vertex_t x = 0; x < dimension; ++x) {
                    if (x != xi and x != currentWalk.at(0)
                        and !currentTour.containsEdge(xi, x)
                        and !currentWalk.containsEdge(xi, x)
                        and tsplibProblem.exchangeGain(currentWalk) - tsplibProblem.dist(xi, x) > highestGain) {

                        vertexChoices.at(i + 1).push_back(x);
                    }

                }
            } else { // i is even
                if (i <= infeasibilityDepth) {
                    for (vertex_t neighbor : currentTour.getNeighbors(xi)) {
                        if (!currentWalk.containsEdge(xi, neighbor)) {
                            vertexChoices.at(i + 1).push_back(neighbor);
                        }
                    }
                } else {
                    for (vertex_t neighbor : currentTour.getNeighbors(xi)) {
                        if (!currentWalk.containsEdge(xi, neighbor)
                            and currentTour.isTourAfterExchange(currentWalk.appendAndClose(neighbor))) {

                            vertexChoices.at(i + 1).push_back(neighbor);
                        }
                    }
                }
            }

            ++i;
        }

    }
}